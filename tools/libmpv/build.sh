#!/bin/bash
# Builds libmpv for SCANCORE's video player, LGPL throughout: mpv with
# -Dgpl=false, FFmpeg without --enable-gpl, and libraries under permissive
# licences (or LGPL). Sources are downloaded at pinned versions and checked
# against their SHA-256; nothing built here is committed.
#
#   tools/libmpv/build.sh android-arm64     phones
#   tools/libmpv/build.sh android-armv7     32-bit phones
#   tools/libmpv/build.sh android-x86_64    the emulator
#   tools/libmpv/build.sh linux-x86_64      the AppImage (on its build system)
#   tools/libmpv/build.sh windows-x64       cross-built with mingw-w64
#   tools/libmpv/build.sh macos-universal   arm64 + x86_64 (on a Mac)
#
# The result goes to build/libmpv/<target>/ (SCANCORE_LIBMPV_DIR to change
# the base; SCANCORE_JOBS=4 for fewer parallel compiles), with include/ and
# lib/ for CMake's MPV_DIR:
#   Android  libmpv.so and FFmpeg's shared libraries (the app hands the Java
#            VM to libavcodec, so it stays its own library)
#   Linux    libmpv.so
#   Windows  bin/libmpv-2.dll, lib/libmpv.dll.a and lib/mpv.def (for MSVC)
#   macOS    libmpv.2.dylib (universal)
# Windows, macOS and Android also get libavif, for AVIF covers and pages (Qt
# has no decoder for them, Android's own reads them only from Android 12;
# Linux takes the system's): bin/libavif.dll with lib/avif.def,
# libavif.16.dylib or libavif.so, with dav1d inside.
# and licenses/, the licence texts of what is linked in.
# Everything else is linked into libmpv, with its symbols kept inside: the
# app's own Qt may load another FFmpeg or FreeType beside it.
#
# Needs meson, ninja, pkg-config, make, Python 3 with jinja2, curl or wget,
# git; the Android NDK (ANDROID_NDK_ROOT, else the newest in
# ~/Android/Sdk/ndk), mingw-w64 (Windows) or Xcode (macOS). gperf is built
# here when missing. Without nasm, x86 builds go without assembly. Linux
# builds use the system's fontconfig, FreeType, HarfBuzz, FriBidi, libass,
# libva and EGL (shared, as the AppImage expects of the system).
#
# The dependency list and most build flags follow mpv-android's build
# scripts (MIT, https://github.com/mpv-android/mpv-android), with GPL parts
# and what SCANCORE does not use (Lua, curl, encoders, muxers) left out.
set -euo pipefail

target=${1:-}
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$here/../.." && pwd)
base=${SCANCORE_LIBMPV_DIR:-$root/build/libmpv}

# macOS: both architectures, then one universal library.
if [ "$target" = macos-universal ]; then
    "$0" macos-arm64
    "$0" macos-x86_64
    out=$base/macos-universal
    rm -rf "$out" && mkdir -p "$out/lib"
    cp -R "$base/macos-arm64/include" "$out/include"
    cp -R "$base/macos-arm64/licenses" "$out/licenses"
    for lib in libmpv.2 libavif.16; do
        lipo -create "$base/macos-arm64/lib/$lib.dylib" "$base/macos-x86_64/lib/$lib.dylib" -output "$out/lib/$lib.dylib"
        codesign --force -s - "$out/lib/$lib.dylib"
        lipo -info "$out/lib/$lib.dylib" >&2
    done
    ln -sf libmpv.2.dylib "$out/lib/libmpv.dylib"
    ln -sf libavif.16.dylib "$out/lib/libavif.dylib"
    exit 0
fi

case "$target" in
android-arm64) os=android cpu=aarch64 triple=aarch64-linux-android ffarch=aarch64 ffcpu=armv8-a ;;
# 32-bit phones (issue #19). The NDK's clang is armv7a-linux-androideabi<api>,
# its other tools (and autotools' --host) arm-linux-androideabi.
android-armv7) os=android cpu=arm triple=armv7a-linux-androideabi ffarch=arm ffcpu=armv7-a ;;
android-x86_64) os=android cpu=x86_64 triple=x86_64-linux-android ffarch=x86_64 ffcpu=generic ;;
linux-x86_64) os=linux cpu=x86_64 triple= ffarch=x86_64 ffcpu=generic ;;
windows-x64) os=windows cpu=x86_64 triple=x86_64-w64-mingw32 ffarch=x86_64 ffcpu=generic ;;
macos-arm64) os=macos cpu=aarch64 triple= ffarch=aarch64 ffcpu=armv8-a ;;
macos-x86_64) os=macos cpu=x86_64 triple= ffarch=x86_64 ffcpu=generic ;;
*)
    echo "usage: $0 android-arm64|android-armv7|android-x86_64|linux-x86_64|windows-x64|macos-universal" >&2
    exit 2
    ;;
esac

cache=$base/cache
work=$base/work/$target
prefix=$base/$target
tools=$base/host-tools
# SCANCORE_JOBS caps parallel compiles (a laptop stays cool); all cores by
# default.
cores=${SCANCORE_JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu)}
api=28              # Android: the app's minimum version
macos_min=13.0      # macOS: the app's minimum version

# ---------------------------------------------------------------- versions
# name version url sha256 ("-" while bumping a version: the build prints
# the new file's checksum into build/libmpv/checksums.txt). A url may name
# mirrors after "|", tried in turn when the first cannot be reached.
sources=(
    "mbedtls 3.6.7 https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2 a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6"
    "dav1d 1.5.4 https://downloads.videolan.org/pub/videolan/dav1d/1.5.4/dav1d-1.5.4.tar.xz 686616b7c69eb88d44459391ab25cac13b6647a3b288835c5784e71c1514a5c5"
    "libavif 1.3.0 https://github.com/AOMediaCodec/libavif/archive/refs/tags/v1.3.0.tar.gz 0a545e953cc049bf5bcf4ee467306a2f113a75110edf59e61248873101cd26c1"
    "libxml2 2.15.4 https://download.gnome.org/sources/libxml2/2.15/libxml2-2.15.4.tar.xz 98087fd181d9070724f3fbc65c7377db03038eb92bd882374daff44940138821"
    "freetype 2.15.0 https://download.savannah.gnu.org/releases/freetype/freetype-2.15.0.tar.xz 93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5"
    "fontconfig 2.18.3 https://gitlab.freedesktop.org/fontconfig/fontconfig/-/archive/2.18.3/fontconfig-2.18.3.tar.gz 9ae01e1d53acdef56010c5451cd34aa41d325b2faccd8606448d8fa01b2496b3"
    "fribidi 1.0.17 https://github.com/fribidi/fribidi/releases/download/v1.0.17/fribidi-1.0.17.tar.xz 6949dcde27d41cebad1fd741fcafc36d55a1020d2d872d4a6eb3914caabbada2"
    "harfbuzz 14.6.0 https://github.com/harfbuzz/harfbuzz/releases/download/14.6.0/harfbuzz-14.6.0.tar.xz d07a007327277708a2a73ae437887cdbaf282937f6d03ca5467723e9099af586"
    "unibreak 8.0 https://github.com/adah1972/libunibreak/releases/download/libunibreak_8_0/libunibreak-8.0.tar.gz 9c4fad6e517338a098373acc9f35579ae2c325e6446666fb9ac2666ba15ceba4"
    "libass 0.17.5 https://github.com/libass/libass/releases/download/0.17.5/libass-0.17.5.tar.xz 2dca25c0e0c837ddf00b52011b3f82cac1e4ddd3ad018227806b0c2288864acc"
    "ffmpeg 9.0.2 https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz|https://src.fedoraproject.org/repo/pkgs/ffmpeg/ffmpeg-9.0.2.tar.xz/sha512/7756f8434371b9bc1d01cf578bf49014e058b0869c3c1df05030f5f4035f1def64b2e7dba3cf340da4dc8dd9239dd5f894530c9e45793ac27876807079aa546a/ffmpeg-9.0.2.tar.xz 8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e"
    "mpv 0.41.0 https://github.com/mpv-player/mpv/archive/refs/tags/v0.41.0.tar.gz ee21092a5ee427353392360929dc64645c54479aefdb5babc5cfbb5fad626209"
    "zlib 1.3.1 https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23"
    "gperf 3.1 https://ftp.gnu.org/pub/gnu/gperf/gperf-3.1.tar.gz 588546b945bba4b70b6a3a616e80b4ab466e3f33024a352fc2198112cdbb3ae2"
)
# libplacebo needs its submodules (glad, fast_float): a shallow clone of the
# tag, checked against its commit.
libplacebo_tag=v7.360.1
libplacebo_commit=cee9b076f2c63104ccfd497fa79c39a867293ec4

download() { # url[|mirror…] file
    local url
    for url in ${1//|/ }; do
        if command -v curl >/dev/null; then
            curl -fsSL --retry 3 --connect-timeout 30 -o "$2" "$url" && return
        else
            wget -q -O "$2" "$url" && return
        fi
        echo "  $url failed" >&2
    done
    return 1
}

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

# Unpacks a source into $work/<name> (once).
fetch() {
    local name=$1 line
    for line in "${sources[@]}"; do
        set -- $line
        [ "$1" = "$name" ] || continue
        local version=$2 url=$3 want=$4 file=$cache/${3##*/}
        mkdir -p "$cache"
        if [ ! -f "$file" ]; then
            echo "Downloading $name $version" >&2
            download "$url" "$file.part"
            mv "$file.part" "$file"
        fi
        local have
        have=$(sha256 "$file")
        if [ "$want" != "-" ] && [ "$have" != "$want" ]; then
            echo "$file: SHA-256 $have, expected $want" >&2
            exit 1
        fi
        [ "$want" = "-" ] && echo "  $name $version sha256 $have" >>"$base/checksums.txt"
        if [ ! -d "$work/$name" ]; then
            mkdir -p "$work/$name"
            tar -xf "$file" -C "$work/$name" --strip-components=1
        fi
        return
    done
    echo "unknown source $name" >&2
    exit 1
}

# ---------------------------------------------------------------- toolchain
mkdir -p "$prefix/lib/pkgconfig" "$prefix/include" "$work"
unset PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR CFLAGS CXXFLAGS CPPFLAGS LDFLAGS
export PATH="$tools/bin:$PATH"
# Only this prefix's libraries, except on Linux, where the system's are
# wanted too (fontconfig, libva, EGL, …).
export PKG_CONFIG_LIBDIR=$prefix/lib/pkgconfig
cxxlib=stdc++
host=$triple        # autotools --host
meson_system=$os
case $os in
android)
    ndk=${ANDROID_NDK_ROOT:-${ANDROID_NDK_HOME:-}}
    [ -n "$ndk" ] || ndk=$(ls -d "$HOME"/Android/Sdk/ndk/* 2>/dev/null | sort -V | tail -1)
    [ -d "$ndk" ] || { echo "No Android NDK (set ANDROID_NDK_ROOT)" >&2; exit 1; }
    export PATH="$(echo "$ndk"/toolchains/llvm/prebuilt/*)/bin:$PATH"
    export CC=$triple$api-clang CXX=$triple$api-clang++ AR=llvm-ar RANLIB=llvm-ranlib NM=llvm-nm STRIP=llvm-strip
    # 16 KB pages (Android 15 and later devices).
    export LDFLAGS="-Wl,-O1,--icf=safe -Wl,-z,max-page-size=16384"
    cxxlib=c++
    ;;
linux)
    export CC=${CC:-gcc} CXX=${CXX:-g++} AR=${AR:-ar} RANLIB=${RANLIB:-ranlib} NM=${NM:-nm} STRIP=${STRIP:-strip}
    export PKG_CONFIG_LIBDIR=$prefix/lib/pkgconfig:$(pkg-config --variable pc_path pkg-config)
    export CFLAGS="-O2 -fPIC" CXXFLAGS="-O2 -fPIC"
    host=
    ;;
windows)
    export CC=$triple-gcc CXX=$triple-g++ AR=$triple-ar RANLIB=$triple-ranlib NM=$triple-nm STRIP=$triple-strip WINDRES=$triple-windres
    # The DLL carries its own C, C++ and thread runtimes.
    export LDFLAGS="-static -static-libgcc -static-libstdc++"
    ;;
macos)
    carch=$([ $cpu = aarch64 ] && echo arm64 || echo x86_64)
    export CC="clang -arch $carch -mmacosx-version-min=$macos_min" CXX="clang++ -arch $carch -mmacosx-version-min=$macos_min"
    export AR=ar RANLIB=ranlib NM=nm STRIP=strip MACOSX_DEPLOYMENT_TARGET=$macos_min
    export LDFLAGS="-arch $carch -mmacosx-version-min=$macos_min"
    cxxlib=c++
    host=$([ $cpu = aarch64 ] && echo aarch64-apple-darwin || echo x86_64-apple-darwin)
    meson_system=darwin
    ;;
esac

asm=yes
[ "$cpu" = x86_64 ] && ! command -v nasm >/dev/null && asm=no

# Cross builds: meson reads no CFLAGS/LDFLAGS from the environment, so the
# compiler's own flags and the link flags of the moment go in the file.
cross=$work/crossfile.txt
quoted() { local out= w; for w in "$@"; do out+="'$w', "; done; echo "${out%, }"; }
write_cross() {
    local c=${CC%% *} cxx=${CXX%% *} cflags
    cflags=$(echo "$CC" | cut -s -d' ' -f2-)
    cat >"$cross" <<EOF
[built-in options]
c_args = [$(quoted $cflags)]
cpp_args = [$(quoted $cflags)]
c_link_args = [$(quoted $cflags ${LDFLAGS:-})]
cpp_link_args = [$(quoted $cflags ${LDFLAGS:-})]
$([ $os = macos ] && printf "objc_args = [%s]\nobjc_link_args = [%s]" "$(quoted $cflags)" "$(quoted $cflags ${LDFLAGS:-})")
[binaries]
c = '$c'
cpp = '$cxx'
ar = '${AR}'
nm = '${NM}'
strip = '${STRIP}'
pkg-config = 'pkg-config'
$([ $os = windows ] && printf "windres = '%s'\ndlltool = '%s'" "$WINDRES" "$triple-dlltool")
$([ $os = macos ] && printf "objc = '%s'" "$c")
[host_machine]
system = '$meson_system'
cpu_family = '$cpu'
cpu = '$cpu'
endian = 'little'
EOF
}

meson_build() { # name [meson options...]
    local name=$1 args=()
    shift
    if [ $os != linux ]; then
        write_cross
        args=(--cross-file "$cross")
    fi
    (
        cd "$work/$name"
        rm -rf _build
        env -u CC -u CXX meson setup _build "${args[@]}" --prefix="$prefix" --libdir=lib --buildtype=release \
            --default-library=static -Dwrap_mode=nodownload "$@"
        ninja -C _build -j"$cores"
        ninja -C _build install
    )
}

configure_build() { # name [configure options...]
    local name=$1
    shift
    (
        cd "$work/$name"
        ./configure ${host:+--host=$host} --prefix="$prefix" --with-pic --enable-static --disable-shared "$@"
        make -j"$cores"
        make install
    )
}

done_mark() { [ -f "$work/.$1.done" ]; }
mark() { touch "$work/.$1.done"; }

# ---------------------------------------------------------------- host tools
if [ $os = android ] && ! command -v gperf >/dev/null; then
    (
        fetch gperf
        cd "$work/gperf"
        env -u CC -u CXX -u AR -u RANLIB -u NM -u STRIP -u LDFLAGS ./configure --prefix="$tools" >/dev/null
        env -u CC -u CXX -u AR -u RANLIB -u NM -u STRIP -u LDFLAGS make -j"$cores" >/dev/null
        env -u CC -u CXX -u AR -u RANLIB -u NM -u STRIP -u LDFLAGS make install >/dev/null
    )
fi

# ---------------------------------------------------------------- libraries
# TLS for FFmpeg: the system's on Windows (Schannel) and macOS
# (SecureTransport), mbedTLS elsewhere.
if [ $os = android ] || [ $os = linux ]; then
    if ! done_mark mbedtls; then
        fetch mbedtls
        (
            cd "$work/mbedtls"
            make clean >/dev/null 2>&1 || true
            [ "$cpu" = x86_64 ] && ./scripts/config.py set MBEDTLS_AESNI_C
            ./scripts/config.py set MBEDTLS_PLATFORM_DEV_RANDOM '"/dev/urandom"'
            make -j"$cores" no_test
            make DESTDIR="$prefix" install
        )
        mark mbedtls
    fi
fi

# Windows has no zlib of its own (FFmpeg's PNG, compressed MKV tracks).
if [ $os = windows ] && ! done_mark zlib; then
    fetch zlib
    (
        cd "$work/zlib"
        make -f win32/Makefile.gcc PREFIX=$triple- -j"$cores" libz.a
        mkdir -p "$prefix/include" "$prefix/lib/pkgconfig"
        cp zlib.h zconf.h "$prefix/include/"
        cp libz.a "$prefix/lib/"
        printf 'prefix=%s\nincludedir=${prefix}/include\nlibdir=${prefix}/lib\n\nName: zlib\nDescription: zlib\nVersion: 1.3.1\nLibs: -L${libdir} -lz\nCflags: -I${includedir}\n' "$prefix" \
            >"$prefix/lib/pkgconfig/zlib.pc"
    )
    mark zlib
fi

if ! done_mark dav1d; then
    fetch dav1d
    meson_build dav1d -Denable_tests=false -Denable_tools=false -Dstack_alignment=16 -Denable_asm=$([ $asm = yes ] && echo true || echo false)
    mark dav1d
fi

# AVIF covers and pages (Windows, macOS, Android): libavif as a library of
# its own, decoding with the dav1d above, linked inside it.
if [ $os != linux ] && ! done_mark libavif; then
    fetch libavif
    (
        cd "$work/libavif"
        rm -rf _build
        cc=${CC%% *} cflags=$(echo "$CC" | cut -s -d' ' -f2-)
        platform=()
        # Android's ABI name (kept out of the case below: macOS's bash 3.2
        # cannot parse a case inside $() inside a case).
        abi=$cpu
        [ $cpu = aarch64 ] && abi=arm64-v8a
        [ $cpu = arm ] && abi=armeabi-v7a
        case $os in
        windows) platform=(-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_RC_COMPILER="$WINDRES"
            -DCMAKE_SHARED_LINKER_FLAGS="$LDFLAGS -Wl,--exclude-libs,ALL") ;;
        macos) platform=(-DCMAKE_OSX_ARCHITECTURES=$carch -DCMAKE_OSX_DEPLOYMENT_TARGET=$macos_min
            -DCMAKE_INSTALL_NAME_DIR=@rpath -DCMAKE_SHARED_LINKER_FLAGS="$LDFLAGS") ;;
        # The NDK's own CMake setup; an APK only carries lib*.so names, so
        # no versioned soname.
        android) platform=(-DCMAKE_TOOLCHAIN_FILE="$ndk/build/cmake/android.toolchain.cmake" -DANDROID_PLATFORM=$api
            -DANDROID_ABI=$abi
            -DCMAKE_PLATFORM_NO_VERSIONED_SONAME=ON -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH
            -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH
            -DCMAKE_SHARED_LINKER_FLAGS="$LDFLAGS -Wl,--exclude-libs,ALL") ;;
        esac
        compiler=()
        [ $os = android ] || compiler=(-DCMAKE_C_COMPILER="$cc" -DCMAKE_C_FLAGS="$cflags")
        env -u CC -u CXX cmake -S . -B _build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
            -DCMAKE_INSTALL_LIBDIR=lib "${compiler[@]}" -DCMAKE_FIND_ROOT_PATH="$prefix" \
            -DBUILD_SHARED_LIBS=ON -DAVIF_CODEC_DAV1D=SYSTEM -DAVIF_LIBYUV=OFF -DAVIF_LIBSHARPYUV=OFF -DAVIF_LIBXML2=OFF \
            -DAVIF_BUILD_APPS=OFF -DAVIF_BUILD_TESTS=OFF "${platform[@]}"
        ninja -C _build -j"$cores"
        ninja -C _build install
    )
    mark libavif
fi

# Subtitles: libass with FreeType, FriBidi and HarfBuzz, and fonts from
# fontconfig (Android, Linux), DirectWrite (Windows) or Core Text (macOS).
# Linux takes them all from the system.
if [ $os != linux ]; then
    if [ $os = android ]; then
        if ! done_mark libxml2; then
            fetch libxml2
            meson_build libxml2 -Dminimum=true -D{push,reader,sax1,iso8859x,pattern}=enabled
            mark libxml2
        fi
    fi
    if ! done_mark freetype; then
        fetch freetype
        meson_build freetype -D{png,bzip2,brotli,harfbuzz}=disabled -Dzlib=internal
        mark freetype
    fi
    if [ $os = android ] && ! done_mark fontconfig; then
        fetch fontconfig
        meson_build fontconfig -D{tests,doc,tools,nls}=disabled -Dxml-backend=libxml2
        mark fontconfig
    fi
    if ! done_mark fribidi; then
        fetch fribidi
        meson_build fribidi -D{tests,docs}=false
        mark fribidi
    fi
    if ! done_mark harfbuzz; then
        fetch harfbuzz
        meson_build harfbuzz -D{tests,docs,raster,vector,gpu,subset,cairo,glib,gobject,icu,chafa}=disabled -Dfreetype=enabled \
            $([ $os = macos ] && echo -Dcoretext=disabled)
        mark harfbuzz
    fi
    if ! done_mark unibreak; then
        fetch unibreak
        configure_build unibreak
        mark unibreak
    fi
    if ! done_mark libass; then
        fetch libass
        configure_build libass --enable-libunibreak \
            $([ $os = android ] && echo --enable-fontconfig || echo --disable-fontconfig) \
            $([ $asm = yes ] && [ "$cpu" = x86_64 ] && echo --enable-asm || echo --disable-asm)
        mark libass
    fi
fi

if ! done_mark libplacebo; then
    [ -d "$work/libplacebo" ] || git clone -q --depth 1 -b "$libplacebo_tag" --recurse-submodules --shallow-submodules \
        https://github.com/haasn/libplacebo "$work/libplacebo"
    [ "$(git -C "$work/libplacebo" rev-parse HEAD)" = "$libplacebo_commit" ] || { echo "libplacebo $libplacebo_tag is not $libplacebo_commit" >&2; exit 1; }
    # Its objects mark the API dllexport even in a static build, which would
    # export all of libplacebo from libmpv-2.dll.
    [ $os = windows ] && sed -i "s/c_args: \['-DPL_EXPORT'\]/c_args: ['-DPL_STATIC']/" "$work/libplacebo/src/meson.build"
    meson_build libplacebo -Dvulkan=disabled -Dopengl=enabled -Ddemos=false -D{lcms,dovi,libdovi,shaderc,glslang,d3d11,unwind,xxhash}=disabled
    # Static linking needs the C++ library too.
    sed -i.bak "/^Libs:/ s|\$| -l$cxxlib|" "$prefix/lib/pkgconfig/libplacebo.pc" && rm -f "$prefix/lib/pkgconfig/libplacebo.pc.bak"
    mark libplacebo
fi

# What anime streams use, and nothing more (smaller downloads, less memory
# on low-end phones): video and audio codecs (with the platform's hardware
# decoders), text and picture subtitles, cover art; HLS, DASH and the usual
# containers; the network with TLS and HLS's AES-128.
decoders=h264,hevc,vp8,vp9,av1,libdav1d,mpeg1video,mpeg2video,mpeg4,h263,msmpeg4v3
decoders+=,aac,aac_fixed,aac_latm,mp1,mp2,mp3,mp3float,opus,vorbis,ac3,eac3,dca,truehd,flac,alac
decoders+=,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s32le,pcm_f32le,pcm_u8,pcm_alaw,pcm_mulaw
decoders+=,ass,ssa,srt,subrip,webvtt,mov_text,text,microdvd,hdmv_pgs_subtitle,dvb_subtitle,dvd_subtitle
decoders+=,png,mjpeg,bmp,webp,gif
demuxers=hls,dash,mov,matroska,mpegts,flv,avi,mpegps,ogg,aac,mp3,ac3,eac3,flac,wav,h264,hevc
demuxers+=,webvtt,ass,srt,microdvd,concat,data,image_png_pipe,image_jpeg_pipe,image_webp_pipe
parsers=h264,hevc,vp8,vp9,av1,mpeg4video,mpegvideo,h263,aac,aac_latm,mpegaudio,ac3,dca,opus,vorbis,flac,dvbsub,dvdsub,png,mjpeg
protocols=file,http,https,tcp,tls,crypto,data,httpproxy,async,cache,subfile

if ! done_mark ffmpeg; then
    fetch ffmpeg
    platform=()
    case $os in
    android)
        decoders+=,h264_mediacodec,hevc_mediacodec,vp8_mediacodec,vp9_mediacodec,av1_mediacodec,mpeg4_mediacodec
        # mbedTLS is Apache-2.0, so FFmpeg is LGPLv3 (--enable-version3).
        platform=(--target-os=android --enable-cross-compile --cross-prefix=$triple- --enable-jni --enable-mediacodec
            --enable-mbedtls --enable-version3 --enable-libxml2 --enable-shared --disable-static)
        ;;
    linux) platform=(--enable-mbedtls --enable-version3 --enable-vaapi --disable-vdpau --disable-xlib --enable-static --disable-shared --enable-pic) ;;
    windows)
        platform=(--target-os=mingw32 --enable-cross-compile --cross-prefix=$triple- --enable-schannel
            --enable-d3d11va --enable-dxva2 --enable-static --disable-shared)
        ;;
    macos)
        platform=(--target-os=darwin --enable-securetransport --enable-videotoolbox --enable-static --disable-shared
            $([ "$(uname -m)" = "$carch" ] || echo --enable-cross-compile))
        ;;
    esac
    (
        cd "$work/ffmpeg"
        rm -rf _build && mkdir _build && cd _build
        # LGPL: no --enable-gpl.
        ../configure "${platform[@]}" \
            --cc="$CC" --cxx="$CXX" --nm="$NM" --ar="$AR" --ranlib="$RANLIB" --strip="$STRIP" --pkg-config=pkg-config \
            --arch=$ffarch --cpu=$ffcpu --prefix="$prefix" \
            --extra-cflags="-I$prefix/include" --extra-ldflags="-L$prefix/lib ${LDFLAGS:-}" \
            --enable-libdav1d --disable-vulkan \
            --disable-stripping --disable-doc --disable-programs \
            --disable-muxers --disable-encoders --disable-devices --disable-avdevice \
            --disable-decoders --enable-decoder="$decoders" --disable-demuxers --enable-demuxer="$demuxers" \
            --disable-parsers --enable-parser="$parsers" --disable-protocols --enable-protocol="$protocols" \
            $([ $asm = yes ] || echo --disable-x86asm)
        make -j"$cores"
        make install
    )
    mark ffmpeg
fi

if ! done_mark mpv; then
    fetch mpv
    # Linux: the system's static libraries are not built for a shared
    # library, so mpv links shared ones, and ours (static only) list their
    # private dependencies as public ones to be linked in full.
    if [ $os = linux ]; then
        python3 - "$prefix/lib/pkgconfig" <<'PY'
import glob, sys
for path in glob.glob(sys.argv[1] + "/*.pc"):
    fields, lines = {}, []
    for line in open(path).read().splitlines():
        key = line.split(":", 1)[0] if ":" in line and "=" not in line.split(":", 1)[0] else None
        if key in ("Libs", "Libs.private", "Requires", "Requires.private"):
            fields[key] = line.split(":", 1)[1].strip()
        else:
            lines.append(line)
    libs = " ".join(v for v in (fields.get("Libs"), fields.get("Libs.private")) if v)
    requires = ", ".join(v for v in (fields.get("Requires"), fields.get("Requires.private")) if v)
    lines += ["Libs: " + libs] + (["Requires: " + requires] if requires else [])
    open(path, "w").write("\n".join(lines) + "\n")
PY
    fi
    # The static libraries' symbols stay inside libmpv. Optional features the
    # player does not use stay off, so no system FFmpeg or other extras get
    # linked in beside ours.
    case $os in
    android | linux | windows) export LDFLAGS="${LDFLAGS:-} -Wl,--exclude-libs,ALL" ;;
    macos) export LDFLAGS="$LDFLAGS -Wl,-exported_symbol,_mpv_*" ;;
    esac
    options=(-Dlibmpv=true -Dcplayer=false -Dgpl=false -Dlua=disabled -Djavascript=disabled -Dmanpage-build=disabled
        -Dlibarchive=disabled -Dlcms2=disabled -Dvulkan=disabled -Dcdda=disabled -Ddvdnav=disabled -Dlibbluray=disabled
        -Drubberband=disabled -Duchardet=disabled -Dzimg=disabled -Dsdl2-audio=disabled -Dsdl2-video=disabled
        -Dsdl2-gamepad=disabled -Djack=disabled -Dopenal=disabled -Dlibavdevice=disabled -Djpeg=disabled -Dsixel=disabled
        -Dcaca=disabled -Dshaderc=disabled -Dspirv-cross=disabled -Ddvbin=disabled -Dvapoursynth=disabled -Dcplugins=disabled
        -Dsndio=disabled -Dvdpau=disabled -Doss-audio=disabled)
    case $os in
    android) options+=(-Diconv=disabled) ;;
    linux) options+=(-Dwayland=disabled -Dpulse=enabled -Dalsa=enabled) ;;
    windows) options+=(-Dprefer_static=true -Diconv=disabled) ;;
    # Cocoa (and with it OpenGL) needs mpv's Swift part.
    macos) options+=(-Dprefer_static=true -Dswift-build=enabled "-Dswift-flags=-target $carch-apple-macos$macos_min"
        -Dmacos-cocoa-cb=disabled -Dmacos-media-player=disabled) ;;
    esac
    meson_build mpv --default-library=shared "${options[@]}"
    mark mpv
fi

# ---------------------------------------------------------------- result
# The licences of everything inside, shipped beside the library
# (licenses/<name>/). Linux takes libass and the font libraries from the
# system, so they are not here.
rm -rf "$prefix/licenses"
for dir in "$work"/*/; do
    name=$(basename "$dir")
    [ "$name" = gperf ] && continue
    mkdir -p "$prefix/licenses/$name"
    for file in COPYING COPYING.LGPLv2.1 COPYING.LGPLv3 LICENSE LICENSE.md LICENSE.LGPL LICENSE.TXT LICENCE Copyright docs/FTL.TXT; do
        [ -f "$dir/$file" ] && cp "$dir/$file" "$prefix/licenses/$name/"
    done
done
cd "$prefix"
case $os in
android)
    for lib in libmpv libavcodec libavformat libavutil libavfilter libswresample libswscale libavif; do
        [ -f lib/$lib.so ] || { echo "$lib.so was not built" >&2; exit 1; }
        "$STRIP" --strip-unneeded lib/$lib.so
        # An APK only carries lib*.so names: no versioned sonames.
        echo "$lib.so: $(llvm-readelf -d lib/$lib.so | grep -o 'SONAME.*' | head -1)" >&2
    done
    ;;
linux)
    "$STRIP" --strip-unneeded "$(readlink -f lib/libmpv.so)"
    echo "exported: $(nm -D --defined-only lib/libmpv.so | awk '{print $3}' | grep -vc '^mpv_') besides mpv_*" >&2
    ;;
windows)
    "$STRIP" --strip-unneeded bin/libmpv-2.dll
    # Only mpv's API leaves the DLL; lib/mpv.def lets MSVC make an import
    # library (lib /def:mpv.def /machine:x64 /out:mpv.lib).
    exports=$($triple-objdump -p bin/libmpv-2.dll | sed -n '/\[Ordinal\/Name Pointer\] Table/,/^$/p' | awk '/^\t\[/ { print $NF }')
    if echo "$exports" | grep -v '^mpv_'; then
        echo "libmpv-2.dll exports more than mpv_* (above)" >&2
        exit 1
    fi
    { echo "LIBRARY libmpv-2.dll"; echo "EXPORTS"; echo "$exports" | sed 's/^/    /'; } >lib/mpv.def
    echo "needs: $($triple-objdump -p bin/libmpv-2.dll | grep 'DLL Name' | awk '{print $3}' | sort -u | tr '\n' ' ')" >&2
    # libavif the same way: only avif* leaves it, lib/avif.def for MSVC.
    avif=$(cd bin && ls libavif*.dll | head -1)
    [ -n "$avif" ] || { echo "libavif was not built" >&2; exit 1; }
    "$STRIP" --strip-unneeded "bin/$avif"
    exports=$($triple-objdump -p "bin/$avif" | sed -n '/\[Ordinal\/Name Pointer\] Table/,/^$/p' | awk '/^\t\[/ { print $NF }')
    if echo "$exports" | grep -v '^avif'; then
        echo "$avif exports more than avif* (above)" >&2
        exit 1
    fi
    { echo "LIBRARY $avif"; echo "EXPORTS"; echo "$exports" | sed 's/^/    /'; } >lib/avif.def
    echo "$avif needs: $($triple-objdump -p "bin/$avif" | grep 'DLL Name' | awk '{print $3}' | sort -u | tr '\n' ' ')" >&2
    ;;
macos)
    # Found inside the app bundle.
    install_name_tool -id @rpath/libmpv.2.dylib lib/libmpv.2.dylib
    install_name_tool -id @rpath/libavif.16.dylib lib/libavif.16.dylib
    # The edit voids the linker's ad-hoc signature, without which arm64
    # Macs refuse to load the library.
    codesign --force -s - lib/libmpv.2.dylib lib/libavif.16.dylib
    ;;
esac
echo "libmpv for $target in $prefix (include/, lib/)" >&2
