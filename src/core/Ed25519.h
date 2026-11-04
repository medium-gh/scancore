#pragma once

#include <QByteArray>
#include <QByteArrayView>

class QIODevice;

namespace sc::ed25519 {

// Whether `signature` (64 bytes) is `publicKey`'s (32 bytes) Ed25519
// signature (RFC 8032, as OpenSSL makes it) of the message. Verification
// only: SCANCORE checks what its releases were signed with.
bool verify(QByteArrayView signature, QByteArrayView publicKey, QByteArrayView message);
// The same over a file or other device, read from its current position.
bool verify(QByteArrayView signature, QByteArrayView publicKey, QIODevice &message);

} // namespace sc::ed25519
