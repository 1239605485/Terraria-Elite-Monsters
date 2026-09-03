#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"
build_dir="${ORIGINREWRITE_BUILD_DIR:-$project_root/build-android-arm64}"
output_zip="${ORIGINREWRITE_OUTPUT:-$project_root/OriginRewrite-android-arm64.zip}"
cmake_bin="${CMAKE:-cmake}"

ndk_dir="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
echo "package_android_arm64: project_root=$project_root"
echo "package_android_arm64: ANDROID_NDK_HOME=${ANDROID_NDK_HOME:-<empty>}"
echo "package_android_arm64: ANDROID_NDK_ROOT=${ANDROID_NDK_ROOT:-<empty>}"
echo "package_android_arm64: ndk_dir=${ndk_dir:-<empty>}"
echo "package_android_arm64: cmake=$(command -v "${cmake_bin:-cmake}" || true)"
if [[ -z "$ndk_dir" ]]; then
    echo "BUILD_ERROR: ANDROID_NDK_HOME or ANDROID_NDK_ROOT is required" >&2
    exit 2
fi

toolchain="$ndk_dir/build/cmake/android.toolchain.cmake"
if [[ ! -f "$toolchain" ]]; then
    echo "BUILD_ERROR: Android toolchain not found: $toolchain" >&2
    echo "Expected path: $ndk_dir/build/cmake/android.toolchain.cmake" >&2
    echo "NDK directory listing:" >&2
    ls -la "$ndk_dir" >&2 || true
    exit 2
fi

echo "package_android_arm64: android.toolchain=$toolchain"

"$cmake_bin" -S "$project_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-24 \
    -DORIGINREWRITE_BUILD_TESTS=OFF

"$cmake_bin" --build "$build_dir" --config Release --target OriginRewrite -j2

library_path="$(find "$build_dir" -type f \
    -name 'libOriginRewrite.android.arm64.so' -print -quit)"
if [[ -z "$library_path" ]]; then
    echo "BUILD_ERROR: Expected ARM64 library was not produced: libOriginRewrite.android.arm64.so" >&2
    echo "Build files found under: $build_dir" >&2
    find "$build_dir" -maxdepth 4 -type f \( -name '*.a' -o -name '*.so' -o -name 'CMakeError.log' \) -print >&2 || true
    exit 3
fi

stage_dir="$(mktemp -d "${TMPDIR:-/tmp}/originrewrite-package.XXXXXX")"
trap 'rm -rf -- "$stage_dir"' EXIT

mkdir -p "$stage_dir/Resources/lib"
cp "$project_root/Manifest.json" "$stage_dir/Manifest.json"
cp "$project_root/Info.json" "$stage_dir/Info.json"
cp "$project_root/OriginRewrite.json" "$stage_dir/OriginRewrite.json"
cp "$library_path" "$stage_dir/Resources/lib/libOriginRewrite.android.arm64.so"

mkdir -p "$(dirname "$output_zip")"
(cd "$stage_dir" && zip -qr -FS "$output_zip" .)

entry_count() {
    unzip -Z1 "$output_zip" | grep -Fxc -- "$1" || true
}

test "$(entry_count 'Manifest.json')" -eq 1
test "$(entry_count 'Info.json')" -eq 1
test "$(entry_count 'OriginRewrite.json')" -eq 1
test "$(entry_count 'Resources/lib/libOriginRewrite.android.arm64.so')" -eq 1

echo "Installable Android ARM64 package: $output_zip"
unzip -l "$output_zip"
