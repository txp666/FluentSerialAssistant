#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 --source DIR --output-dir DIR --version VERSION --architecture x64|arm64" >&2
}

source_dir=""
output_dir=""
version=""
architecture=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --source) source_dir="$2"; shift 2 ;;
        --output-dir) output_dir="$2"; shift 2 ;;
        --version) version="$2"; shift 2 ;;
        --architecture) architecture="$2"; shift 2 ;;
        *) usage; exit 2 ;;
    esac
done

if [[ -z "$source_dir" || -z "$output_dir" || -z "$version" || -z "$architecture" ]]; then
    usage
    exit 2
fi
if [[ ! -d "$source_dir" ]]; then
    echo "Staged application directory does not exist: $source_dir" >&2
    exit 2
fi
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z.-]+)?$ ]]; then
    echo "Invalid release version: $version" >&2
    exit 2
fi

case "$architecture" in
    x64) deb_architecture="amd64" ;;
    arm64) deb_architecture="arm64" ;;
    *) echo "Unsupported DEB architecture: $architecture" >&2; exit 2 ;;
esac

if ! command -v dpkg-deb >/dev/null 2>&1; then
    echo "dpkg-deb is required to create the Debian package." >&2
    exit 2
fi

source_dir="$(cd "$source_dir" && pwd -P)"
mkdir -p "$output_dir"
output_dir="$(cd "$output_dir" && pwd -P)"

package_name="fluent-serial-assistant"
deb_name="FluentSerialAssistant-$version-linux-$architecture.deb"
deb_path="$output_dir/$deb_name"
stage_root="$output_dir/deb-stage"
package_root="$stage_root/$package_name-$version-$deb_architecture"
mkdir -p "$stage_root"
case "$package_root" in
    "$stage_root"/*) ;;
    *) echo "Refusing unsafe DEB staging directory: $package_root" >&2; exit 2 ;;
esac

cleanup() {
    rm -rf -- "$package_root"
}
trap cleanup EXIT
cleanup

install_root="$package_root/opt/FluentSerialAssistant"
mkdir -p \
    "$package_root/DEBIAN" \
    "$install_root" \
    "$package_root/usr/bin" \
    "$package_root/usr/share/applications" \
    "$package_root/usr/share/icons/hicolor/512x512/apps"
cp -a "$source_dir/." "$install_root/"

staged_icon="$source_dir/share/icons/hicolor/512x512/apps/tech.zhangshu.FluentSerialAssistant.png"
if [[ ! -f "$staged_icon" ]]; then
    echo "Staged Linux application icon is missing: $staged_icon" >&2
    exit 2
fi
cp "$staged_icon" "$package_root/usr/share/icons/hicolor/512x512/apps/tech.zhangshu.FluentSerialAssistant.png"
rm -f -- "$install_root/share/icons/hicolor/512x512/apps/tech.zhangshu.FluentSerialAssistant.png"
rm -f -- "$install_root/share/applications/tech.zhangshu.FluentSerialAssistant.desktop"

cat > "$package_root/usr/bin/fluent-serial-assistant" <<'LAUNCHER'
#!/bin/sh
if [ -z "${QT_QPA_PLATFORM:-}" ]; then
    QT_QPA_PLATFORM=xcb
    export QT_QPA_PLATFORM
fi
exec /opt/FluentSerialAssistant/bin/FluentSerialAssistant "$@"
LAUNCHER
chmod 0755 "$package_root/usr/bin/fluent-serial-assistant"

cat > "$package_root/usr/bin/fluentserial-cli" <<'CLI_LAUNCHER'
#!/bin/sh
exec /opt/FluentSerialAssistant/bin/fluentserial-cli "$@"
CLI_LAUNCHER
chmod 0755 "$package_root/usr/bin/fluentserial-cli"

cat > "$package_root/usr/bin/fluentserial-mcp" <<'MCP_LAUNCHER'
#!/bin/sh
exec /opt/FluentSerialAssistant/bin/fluentserial-mcp "$@"
MCP_LAUNCHER
chmod 0755 "$package_root/usr/bin/fluentserial-mcp"

cat > "$package_root/usr/share/applications/tech.zhangshu.FluentSerialAssistant.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Fluent Serial Assistant
Name[zh_CN]=Fluent 串口助手
Comment=Serial terminal assistant
Comment[zh_CN]=Fluent 风格串口调试助手
Exec=fluent-serial-assistant
Terminal=false
Icon=tech.zhangshu.FluentSerialAssistant
StartupWMClass=FluentSerialAssistant
Categories=Development;Utility;
DESKTOP
chmod 0644 "$package_root/usr/share/applications/tech.zhangshu.FluentSerialAssistant.desktop"

installed_size="$(du -sk "$package_root/opt" | awk '{print $1}')"
cat > "$package_root/DEBIAN/control" <<CONTROL
Package: $package_name
Version: $version
Architecture: $deb_architecture
Maintainer: txp <771454616@qq.com>
Installed-Size: $installed_size
Section: utils
Priority: optional
Homepage: https://github.com/txp666/FluentSerialAssistant
Depends: libc6 (>= 2.39), libstdc++6, libgcc-s1, libdbus-1-3, libopengl0, libgl1, libegl1, libfontconfig1, libfreetype6, libglib2.0-0t64, libx11-6, libx11-xcb1, libxext6, libxfixes3, libxi6, libxrender1, libsm6, libice6, libxcb1, libxcb-cursor0, libxcb-icccm4, libxcb-image0, libxcb-keysyms1, libxcb-randr0, libxcb-render0, libxcb-render-util0, libxcb-shape0, libxcb-shm0, libxcb-sync1, libxcb-util1, libxcb-xfixes0, libxcb-xinerama0, libxcb-xkb1, libxkbcommon0, libxkbcommon-x11-0
Description: Modern cross-platform serial terminal assistant
 Fluent Serial Assistant provides serial communication, packet tools,
 plotting, scripting, protocol templates, and session management.
CONTROL
chmod 0644 "$package_root/DEBIAN/control"

rm -f -- "$deb_path"
dpkg-deb --root-owner-group --build "$package_root" "$deb_path"

echo "Created: $deb_path"
