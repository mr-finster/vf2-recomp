#!/bin/bash
# Called by the dependency targets in VF2Recomp.xcodeproj.
set -euo pipefail

# Keep configure and make mutually exclusive even when multiple Xcode processes
# use the same DerivedData. -k keeps the lock inode stable for waiting processes.
if [[ "${1:-}" != --locked ]]; then
    mkdir -p "$VF2_DEPS_DIR"
    exec /usr/bin/lockf -k "$VF2_DEPS_DIR/.build.lock" /bin/bash "$0" --locked
fi

export PATH="/opt/homebrew/bin:/usr/local/bin:/Applications/CMake.app/Contents/bin:$PATH"
cmake_bin="${VF2_CMAKE_EXECUTABLE:-$(command -v cmake || true)}"
if [[ ! -x "$cmake_bin" ]]; then
    echo "error: CMake 3.24 or later is required. Install it with brew install cmake, then build again."
    exit 1
fi

case "$PLATFORM_NAME" in
    iphoneos|iphonesimulator)
        system_name=iOS
        deployment_target="$IPHONEOS_DEPLOYMENT_TARGET"
        ;;
    macosx)
        system_name=Darwin
        deployment_target="$MACOSX_DEPLOYMENT_TARGET"
        ;;
    *) echo "error: Unsupported SDK: $PLATFORM_NAME"; exit 1 ;;
esac

sdk_path="$(xcrun --sdk "$PLATFORM_NAME" --show-sdk-path)"
architectures="$(echo "$ARCHS" | xargs | tr ' ' ';')"
mkdir -p "$VF2_DEPS_DIR"
echo "VF2: Preparing SDL / FreeType ($PLATFORM_NAME, $architectures, $CONFIGURATION)"

# Xcode and a developer's shell can export flags for a different SDK. Keep the
# dependency toolchain tied to the selected Run destination instead.
if ! env -u CFLAGS -u CXXFLAGS -u CPPFLAGS -u OBJCFLAGS -u OBJCXXFLAGS -u LDFLAGS \
    "$cmake_bin" -S "$SRCROOT/apple" -B "$VF2_DEPS_DIR" -G "Unix Makefiles" \
    -DCMAKE_MAKE_PROGRAM=/usr/bin/make \
    -DCMAKE_SYSTEM_NAME="$system_name" \
    -DCMAKE_OSX_SYSROOT="$sdk_path" \
    -DCMAKE_OSX_ARCHITECTURES="$architectures" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
    -DCMAKE_BUILD_TYPE="$CONFIGURATION" \
    -DDISABLE_FORCE_DEBUG_POSTFIX=ON \
    -DCMAKE_C_COMPILER="$(xcrun --find clang)" \
    -DCMAKE_CXX_COMPILER="$(xcrun --find clang++)" \
    -DCMAKE_OBJC_COMPILER="$(xcrun --find clang)" \
    -DCMAKE_OBJCXX_COMPILER="$(xcrun --find clang++)" \
    -DCMAKE_C_FLAGS= -DCMAKE_CXX_FLAGS= -DCMAKE_OBJC_FLAGS= -DCMAKE_OBJCXX_FLAGS= \
    -DCMAKE_EXE_LINKER_FLAGS= -DCMAKE_SHARED_LINKER_FLAGS= \
    > "$VF2_DEPS_DIR/configure.log" 2>&1; then
    cat "$VF2_DEPS_DIR/configure.log"
    exit 1
fi

"$cmake_bin" --build "$VF2_DEPS_DIR" --target SDL3-static freetype \
    --parallel "${VF2_BUILD_JOBS:-$(sysctl -n hw.ncpu)}"
