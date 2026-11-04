#include "Ed25519.h"

#include <QCryptographicHash>
#include <QIODevice>

#include <cstdint>
#include <functional>

// Ed25519 signature verification after TweetNaCl (Bernstein, Janssen,
// Lange, Schwabe; public domain): field arithmetic in sixteen 16-bit limbs,
// points in extended coordinates. SHA-512 is Qt's.

namespace sc::ed25519 {

namespace {

using i64 = std::int64_t;
using u8 = std::uint8_t;
using gf = i64[16];

const gf gf0 = {0};
const gf gf1 = {1};
const gf D = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
              0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
const gf D2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
               0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
const gf X = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
              0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
const gf Y = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
              0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
const gf I = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
              0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};
// The group order, little-endian.
const i64 L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                   0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

void set(gf r, const gf a) {
    for (int i = 0; i < 16; ++i)
        r[i] = a[i];
}

void carry(gf o) {
    for (int i = 0; i < 16; ++i) {
        o[i] += i64(1) << 16;
        const i64 c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * (i64(1) << 16);
    }
}

void select(gf p, gf q, int b) {
    const i64 c = ~(i64(b) - 1);
    for (int i = 0; i < 16; ++i) {
        const i64 t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

void pack25519(u8 *o, const gf n) {
    gf m, t;
    set(t, n);
    carry(t);
    carry(t);
    carry(t);
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = int((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        select(t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i) {
        o[2 * i] = u8(t[i] & 0xff);
        o[2 * i + 1] = u8(t[i] >> 8);
    }
}

bool same32(const u8 *a, const u8 *b) {
    unsigned d = 0;
    for (int i = 0; i < 32; ++i)
        d |= unsigned(a[i] ^ b[i]);
    return d == 0;
}

bool equal(const gf a, const gf b) {
    u8 c[32], d[32];
    pack25519(c, a);
    pack25519(d, b);
    return same32(c, d);
}

u8 parity(const gf a) {
    u8 d[32];
    pack25519(d, a);
    return d[0] & 1;
}

void unpack25519(gf o, const u8 *n) {
    for (int i = 0; i < 16; ++i)
        o[i] = n[2 * i] + (i64(n[2 * i + 1]) << 8);
    o[15] &= 0x7fff;
}

void add(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; ++i)
        o[i] = a[i] + b[i];
}

void sub(gf o, const gf a, const gf b) {
    for (int i = 0; i < 16; ++i)
        o[i] = a[i] - b[i];
}

void mul(gf o, const gf a, const gf b) {
    i64 t[31] = {0};
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j)
            t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; ++i)
        t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; ++i)
        o[i] = t[i];
    carry(o);
    carry(o);
}

void square(gf o, const gf a) { mul(o, a, a); }

void invert(gf o, const gf i) {
    gf c;
    set(c, i);
    for (int a = 253; a >= 0; --a) {
        square(c, c);
        if (a != 2 && a != 4)
            mul(c, c, i);
    }
    set(o, c);
}

void pow2523(gf o, const gf i) {
    gf c;
    set(c, i);
    for (int a = 250; a >= 0; --a) {
        square(c, c);
        if (a != 1)
            mul(c, c, i);
    }
    set(o, c);
}

// Points: p[0..3] = X, Y, Z, T.
void addPoint(gf p[4], gf q[4]) {
    gf a, b, c, d, t, e, f, g, h;
    sub(a, p[1], p[0]);
    sub(t, q[1], q[0]);
    mul(a, a, t);
    add(b, p[0], p[1]);
    add(t, q[0], q[1]);
    mul(b, b, t);
    mul(c, p[3], q[3]);
    mul(c, c, D2);
    mul(d, p[2], q[2]);
    add(d, d, d);
    sub(e, b, a);
    sub(f, d, c);
    add(g, d, c);
    add(h, b, a);
    mul(p[0], e, f);
    mul(p[1], h, g);
    mul(p[2], g, f);
    mul(p[3], e, h);
}

void swapPoints(gf p[4], gf q[4], u8 b) {
    for (int i = 0; i < 4; ++i)
        select(p[i], q[i], b);
}

void packPoint(u8 *r, gf p[4]) {
    gf tx, ty, zi;
    invert(zi, p[2]);
    mul(tx, p[0], zi);
    mul(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= u8(parity(tx) << 7);
}

void scalarmult(gf p[4], gf q[4], const u8 *s) {
    set(p[0], gf0);
    set(p[1], gf1);
    set(p[2], gf1);
    set(p[3], gf0);
    for (int i = 255; i >= 0; --i) {
        const u8 b = (s[i / 8] >> (i & 7)) & 1;
        swapPoints(p, q, b);
        addPoint(q, p);
        addPoint(p, p);
        swapPoints(p, q, b);
    }
}

void scalarbase(gf p[4], const u8 *s) {
    gf q[4];
    set(q[0], X);
    set(q[1], Y);
    set(q[2], gf1);
    mul(q[3], X, Y);
    scalarmult(p, q, s);
}

void modL(u8 *r, i64 x[64]) {
    for (int i = 63; i >= 32; --i) {
        i64 c = 0;
        int j;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += c - 16 * x[i] * L[j - (i - 32)];
            c = (x[j] + 128) >> 8;
            x[j] -= c * 256;
        }
        x[j] += c;
        x[i] = 0;
    }
    i64 c = 0;
    for (int j = 0; j < 32; ++j) {
        x[j] += c - (x[31] >> 4) * L[j];
        c = x[j] >> 8;
        x[j] &= 255;
    }
    for (int j = 0; j < 32; ++j)
        x[j] -= c * L[j];
    for (int i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        r[i] = u8(x[i] & 255);
    }
}

void reduce(u8 *r) {
    i64 x[64];
    for (int i = 0; i < 64; ++i)
        x[i] = r[i];
    for (int i = 0; i < 64; ++i)
        r[i] = 0;
    modL(r, x);
}

// -A from the public key's encoding; false when it is not a point.
bool unpackNegative(gf r[4], const u8 p[32]) {
    gf t, chk, num, den, den2, den4, den6;
    set(r[2], gf1);
    unpack25519(r[1], p);
    square(num, r[1]);
    mul(den, num, D);
    sub(num, num, r[2]);
    add(den, r[2], den);
    square(den2, den);
    square(den4, den2);
    mul(den6, den4, den2);
    mul(t, den6, num);
    mul(t, t, den);
    pow2523(t, t);
    mul(t, t, num);
    mul(t, t, den);
    mul(t, t, den);
    mul(r[0], t, den);
    square(chk, r[0]);
    mul(chk, chk, den);
    if (!equal(chk, num))
        mul(r[0], r[0], I);
    square(chk, r[0]);
    mul(chk, chk, den);
    if (!equal(chk, num))
        return false;
    if (parity(r[0]) == (p[31] >> 7))
        sub(r[0], gf0, r[0]);
    mul(r[3], r[0], r[1]);
    return true;
}

// S below the group order (no second form of the same signature).
bool canonical(const u8 *s) {
    for (int i = 31; i >= 0; --i) {
        if (s[i] < L[i])
            return true;
        if (s[i] > L[i])
            return false;
    }
    return false;
}

bool check(QByteArrayView signature, QByteArrayView publicKey, const std::function<void(QCryptographicHash &)> &feed) {
    if (signature.size() != 64 || publicKey.size() != 32)
        return false;
    const auto *sig = reinterpret_cast<const u8 *>(signature.data());
    const auto *key = reinterpret_cast<const u8 *>(publicKey.data());
    if (!canonical(sig + 32))
        return false;
    gf p[4], q[4];
    if (!unpackNegative(q, key))
        return false;
    QCryptographicHash hash(QCryptographicHash::Sha512);
    hash.addData(signature.first(32));
    hash.addData(publicKey);
    feed(hash);
    const auto digest = hash.result();
    u8 h[64];
    for (int i = 0; i < 64; ++i)
        h[i] = u8(digest[i]);
    reduce(h);
    // R' = h(-A) + sB must be the signature's R.
    scalarmult(p, q, h);
    scalarbase(q, sig + 32);
    addPoint(p, q);
    u8 t[32];
    packPoint(t, p);
    return same32(sig, t);
}

} // namespace

bool verify(QByteArrayView signature, QByteArrayView publicKey, QByteArrayView message) {
    return check(signature, publicKey, [&](QCryptographicHash &hash) { hash.addData(message); });
}

bool verify(QByteArrayView signature, QByteArrayView publicKey, QIODevice &message) {
    bool ok = true;
    const bool good = check(signature, publicKey, [&](QCryptographicHash &hash) { ok = hash.addData(&message); });
    return good && ok;
}

} // namespace sc::ed25519
