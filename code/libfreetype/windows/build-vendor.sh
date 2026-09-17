#!/bin/sh
# Build minimal static FreeType for Windows vendors (MinGW and/or MSVC).
#
# MinGW (MSYS2):
#   bash code/libfreetype/windows/build-vendor.sh mingw
#   Prerequisites: mingw-w64-{i686,x86_64}-{gcc,cmake,make}, curl, tar
#
# MSVC (from a "x64 Native Tools" / VS Developer Command Prompt, or Git Bash
# with cmake that can drive MSBuild — same layout as libcurl's vs2017/):
#   bash code/libfreetype/windows/build-vendor.sh msvc
#   Output: windows/vs2017/lib{32,64}/freetype_a[_debug].lib
#
# Both:
#   bash code/libfreetype/windows/build-vendor.sh all
set -e

TARGET="${1:-mingw}"

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
VENDOR_TMP="${TMPDIR:-/tmp}/q3e-vendor"
FT_TAG=VER-2-13-3
FT_DIR=freetype-${FT_TAG}
FT_WIN="$ROOT/code/libfreetype/windows"
FT_CMAKE_OPTS="
  -DBUILD_SHARED_LIBS=OFF
  -DFT_DISABLE_ZLIB=TRUE
  -DFT_DISABLE_BZIP2=TRUE
  -DFT_DISABLE_PNG=TRUE
  -DFT_DISABLE_HARFBUZZ=TRUE
  -DFT_DISABLE_BROTLI=TRUE
"

mkdir -p "$VENDOR_TMP"
cd "$VENDOR_TMP"

if [ ! -f "freetype-${FT_TAG}.tar.gz" ]; then
  curl -L --connect-timeout 30 --max-time 180 -o "freetype-${FT_TAG}.tar.gz" \
    "https://github.com/freetype/freetype/archive/refs/tags/${FT_TAG}.tar.gz"
fi

ensure_src() {
  if [ ! -d "$FT_DIR" ]; then
    tar xzf "freetype-${FT_TAG}.tar.gz"
  fi
}

install_headers_from() {
  prefix=$1
  mkdir -p "$FT_WIN/include"
  rm -rf "$FT_WIN/include/"*
  cp -a "$prefix/include/." "$FT_WIN/include/"
}

build_mingw() {
  ensure_src
  rm -rf out64 out32
  mkdir -p out64 out32

  build_ft_mingw() {
    prefix=$1
    toolchain_path=$2
    export PATH="${toolchain_path}:/usr/bin"
    rm -rf "$VENDOR_TMP/ft-build"
    mkdir -p "$VENDOR_TMP/ft-build"
    cd "$VENDOR_TMP/ft-build"
    # shellcheck disable=SC2086
    cmake "../${FT_DIR}" \
      -G "MinGW Makefiles" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$prefix" \
      $FT_CMAKE_OPTS
    cmake --build . -j"$(nproc)"
    cmake --install .
    cd "$VENDOR_TMP"
  }

  build_ft_mingw "$VENDOR_TMP/out64" /mingw64/bin
  build_ft_mingw "$VENDOR_TMP/out32" /mingw32/bin

  mkdir -p "$FT_WIN/mingw/lib32" "$FT_WIN/mingw/lib64"
  install_headers_from "$VENDOR_TMP/out64"
  cp -f "$VENDOR_TMP/out32/lib/libfreetype.a" "$FT_WIN/mingw/lib32/libfreetype.a"
  cp -f "$VENDOR_TMP/out64/lib/libfreetype.a" "$FT_WIN/mingw/lib64/libfreetype.a"
  echo "MinGW installed:"
  ls -la "$FT_WIN/mingw/lib32/libfreetype.a" "$FT_WIN/mingw/lib64/libfreetype.a"
}

# MSVC: use default CMake generator (whatever VS is on PATH). Libs land in
# vs2017/ to match code/libcurl/windows/vs2017 layout used by msvc2017/*.vcxproj.
build_msvc() {
  ensure_src
  mkdir -p "$FT_WIN/vs2017/lib32" "$FT_WIN/vs2017/lib64"

  build_ft_msvc() {
    arch=$1
    prefix=$2
    libdir=$3
    rm -rf "$VENDOR_TMP/ft-msvc-$arch" "$prefix"
    mkdir -p "$VENDOR_TMP/ft-msvc-$arch" "$prefix"
    cd "$VENDOR_TMP/ft-msvc-$arch"
    # Force static CRT (/MT|/MTd) to match msvc2017 RuntimeLibrary=MultiThreaded*
    # shellcheck disable=SC2086
    cmake "../${FT_DIR}" -A "$arch" \
      -DCMAKE_INSTALL_PREFIX="$prefix" \
      -DCMAKE_POLICY_DEFAULT_CMP0091=NEW \
      -DCMAKE_MSVC_RUNTIME_LIBRARY='MultiThreaded$<$<CONFIG:Debug>:Debug>' \
      -DCMAKE_C_FLAGS_RELEASE='/MT /O2 /Ob2 /DNDEBUG' \
      -DCMAKE_C_FLAGS_DEBUG='/MTd /Zi /Ob0 /Od /RTC1' \
      $FT_CMAKE_OPTS
    cmake --build . --config Release --parallel
    cmake --install . --config Release
    cmake --build . --config Debug --parallel
    dbg=$(find . -path '*/Debug/*' -name 'freetype*.lib' | head -n 1)
    if [ -z "$dbg" ]; then
      echo "Debug freetype lib not found for $arch" >&2
      exit 1
    fi
    cp -f "$prefix/lib/freetype.lib" "$libdir/freetype_a.lib"
    cp -f "$dbg" "$libdir/freetype_a_debug.lib"
    cd "$VENDOR_TMP"
  }

  build_ft_msvc x64 "$VENDOR_TMP/out64-msvc" "$FT_WIN/vs2017/lib64"
  build_ft_msvc Win32 "$VENDOR_TMP/out32-msvc" "$FT_WIN/vs2017/lib32"
  install_headers_from "$VENDOR_TMP/out64-msvc"
  echo "MSVC (vs2017 layout) installed:"
  ls -la "$FT_WIN/vs2017/lib32" "$FT_WIN/vs2017/lib64"
}

case "$TARGET" in
  mingw) build_mingw ;;
  msvc) build_msvc ;;
  all) build_mingw; build_msvc ;;
  *)
    echo "Usage: $0 [mingw|msvc|all]" >&2
    exit 1
    ;;
esac

echo "Headers:"
ls "$FT_WIN/include"
