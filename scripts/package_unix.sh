#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 --build-dir DIR --output-dir DIR --platform macos|linux --architecture ARCH [--version VERSION] [--configuration CONFIG]" >&2
}

build_dir=""
output_dir="dist"
platform=""
architecture=""
version=""
configuration="Release"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) build_dir="$2"; shift 2 ;;
        --output-dir) output_dir="$2"; shift 2 ;;
        --platform) platform="$2"; shift 2 ;;
        --architecture) architecture="$2"; shift 2 ;;
        --version) version="$2"; shift 2 ;;
        --configuration) configuration="$2"; shift 2 ;;
        *) usage; exit 2 ;;
    esac
done

if [[ -z "$build_dir" || -z "$platform" || -z "$architecture" ]]; then
    usage
    exit 2
fi
if [[ "$platform" != "macos" && "$platform" != "linux" ]]; then
    echo "Unsupported platform: $platform" >&2
    exit 2
fi
if [[ ! "$architecture" =~ ^[a-zA-Z0-9_-]+$ ]]; then
    echo "Invalid architecture: $architecture" >&2
    exit 2
fi

script_dir="$(cd "$(dirname "$0")" && pwd -P)"
build_dir="$(cd "$build_dir" && pwd -P)"
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd -P)"

version_file="$build_dir/FluentSerialAssistant-version.txt"
if [[ ! -f "$version_file" ]]; then
    echo "Version file is missing. Configure the CMake build first: $version_file" >&2
    exit 2
fi
configured_version="$(tr -d '[:space:]' < "$version_file")"
if [[ -z "$version" ]]; then
    version="$configured_version"
fi
if [[ "$version" != "$configured_version" ]]; then
    echo "Requested version '$version' does not match CMake project version '$configured_version'." >&2
    exit 2
fi
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z.-]+)?$ ]]; then
    echo "Invalid release version: $version" >&2
    exit 2
fi

package_name="FluentSerialAssistant-$version-$platform-$architecture"
stage_root="$output_dir/stage"
stage_dir="$stage_root/$package_name"
mkdir -p "$stage_root"
case "$stage_dir" in
    "$stage_root"/*) ;;
    *) echo "Refusing unsafe stage directory: $stage_dir" >&2; exit 2 ;;
esac

cleanup() {
    rm -rf -- "$stage_dir"
}
trap cleanup EXIT
cleanup

echo "Installing $package_name into the staging directory..."
cmake --install "$build_dir" --config "$configuration" --component Runtime --prefix "$stage_dir"

if [[ "$platform" == "macos" ]]; then
    app_bundle="$stage_dir/FluentSerialAssistant.app"
    executable="$app_bundle/Contents/MacOS/FluentSerialAssistant"
    if [[ ! -x "$executable" ]]; then
        echo "Installed macOS executable is missing: $executable" >&2
        exit 2
    fi
    if [[ ! -f "$app_bundle/Contents/PlugIns/platforms/libqcocoa.dylib" ]]; then
        echo "Required Qt Cocoa platform plugin was not deployed." >&2
        exit 2
    fi

    echo "Applying an ad-hoc signature to the macOS bundle..."
    codesign --force --deep --sign - "$app_bundle"
    codesign --verify --deep --strict "$app_bundle"

    entry_count="$(find "$stage_dir" -mindepth 1 -maxdepth 1 -print | wc -l | tr -d '[:space:]')"
    if [[ "$entry_count" != "1" ]]; then
        echo "The macOS image staging directory must contain only FluentSerialAssistant.app." >&2
        exit 2
    fi

    archive_path="$output_dir/$package_name.dmg"
    rm -f -- "$archive_path"
    echo "Creating DMG..."
    hdiutil create \
        -volname "Fluent Serial Assistant" \
        -srcfolder "$stage_dir" \
        -fs HFS+ \
        -format UDZO \
        -imagekey zlib-level=9 \
        -ov \
        "$archive_path"
    hdiutil verify "$archive_path"
else
    executable="$stage_dir/bin/FluentSerialAssistant"
    if [[ ! -x "$executable" ]]; then
        echo "Installed Linux executable is missing: $executable" >&2
        exit 2
    fi
    if ! find "$stage_dir" -type f -name 'libQt6Core.so*' -print -quit | grep -q .; then
        echo "Qt Core runtime library was not deployed." >&2
        exit 2
    fi
    if [[ ! -f "$stage_dir/plugins/platforms/libqxcb.so" ]]; then
        echo "Qt xcb platform plugin was not deployed." >&2
        exit 2
    fi
    if ldd "$executable" | grep -F "not found"; then
        echo "The staged Linux executable has unresolved runtime dependencies." >&2
        exit 2
    fi

    "$script_dir/create_deb_package.sh" \
        --source "$stage_dir" \
        --output-dir "$output_dir" \
        --version "$version" \
        --architecture "$architecture"
    archive_path="$output_dir/FluentSerialAssistant-$version-linux-$architecture.deb"
fi

if command -v sha256sum >/dev/null 2>&1; then
    archive_hash="$(sha256sum "$archive_path" | awk '{print $1}')"
else
    archive_hash="$(shasum -a 256 "$archive_path" | awk '{print $1}')"
fi
printf '%s  %s\n' "$archive_hash" "$(basename "$archive_path")" > "$archive_path.sha256"

echo "Created: $archive_path"
echo "Created: $archive_path.sha256"
