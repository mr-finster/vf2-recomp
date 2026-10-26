#!/bin/sh
# SDL3 and FreeType for the Android build, from their release tarballs: the same versions and
# SHA-256 checksums as tools/mac/deps.sh pins for the Mac and Deck builds. Only fetched and
# unpacked here: android/app/CMakeLists.txt compiles them with the NDK's toolchain, so the APK
# gets its libSDL3.so and a static FreeType for arm64-v8a from the same pins.
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/deps/android && cd build/deps/android

# shasum on the Mac, sha256sum on Linux: either may drive the Android build.
if command -v sha256sum >/dev/null 2>&1; then
	check() { echo "$2  $1" | sha256sum -c -; }
else
	check() { echo "$2  $1" | shasum -a 256 -c -; }
fi
# A tarball already here is checked too, not trusted: a truncated download fails the sum every time.
fetch() { [ -f "$2" ] || curl -fsSL -o "$2" "$1"; check "$2" "$3"; }
fetch https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz SDL3.tar.gz \
	9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
fetch https://downloads.sourceforge.net/project/freetype/freetype2/2.14.3/freetype-2.14.3.tar.xz freetype.tar.xz \
	36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f

# Unpacked once: CMake reconfigures often, and the sources don't change.
[ -d SDL3-3.4.18 ] || tar xzf SDL3.tar.gz
[ -d freetype-2.14.3 ] || tar xJf freetype.tar.xz
