#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Invoked over SSH by deploy-qt-labwc.sh; never launches the interactive app.
set -euo pipefail
revision=${1:?revision required}
jobs=${2:-2}
output=${3:-HDMI-A-2}
upload_name=${4:?upload name required}
[[ "$revision" =~ ^[a-f0-9]{40}$ && "$jobs" =~ ^[1-9][0-9]*$ && "$output" =~ ^[A-Za-z0-9_.:-]+$ && "$upload_name" =~ ^[A-Za-z0-9_-]+$ ]] || exit 2
work="$HOME/.cache/xastir-qt-deploy"
prefix="$HOME/.local/opt/xastir-qt"
mkdir -p "$work" "$prefix/releases"
exec 9>"$work/deployment.lock"
flock -n 9 || { echo 'Another deployment is running.' >&2; exit 1; }
staging="$work/uploads/$upload_name"
[[ -d "$staging" ]] || { echo 'Missing uploaded source.' >&2; exit 1; }
trap 'rm -rf -- "$staging"' EXIT
mkdir -p "$work/source"
# Keep timestamps of unchanged source files so updates can build incrementally.
rsync -rclp --delete "$staging/" "$work/source/"

if ! pkg-config --exists Qt6Widgets Qt6Network Qt6SerialPort Qt6Test sqlite3 shapelib || ! dpkg-query -W -f='${db:Status-Status}' qt6-wayland 2>/dev/null | grep -qx installed; then
    sudo -n apt-get install -y --no-install-recommends build-essential cmake pkg-config python3 \
        qt6-base-dev qt6-serialport-dev qt6-wayland libsqlite3-dev libshp-dev
fi
release="$prefix/releases/${revision:0:12}-$(date -u +%Y%m%dT%H%M%SZ)-$$"
cmake -S "$work/source" -B "$work/build" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=ON -DCMAKE_INSTALL_PREFIX="$release" \
    -DCMAKE_C_FLAGS_RELWITHDEBINFO='-O2 -g' # Keep C fixture assertions active in optimized builds.
cmake --build "$work/build" --parallel "$jobs"
ctest --test-dir "$work/build" --output-on-failure --parallel "$jobs"
if [[ -f "$work/source/tests/deployment/test_labwc_configuration.py" ]]; then
    PYTHONDONTWRITEBYTECODE=1 python3 "$work/source/tests/deployment/test_labwc_configuration.py"
fi
cmake --install "$work/build"
QT_QPA_PLATFORM=offscreen "$release/bin/xastir-qt" --help > "$release/cli-help.txt"
ldd "$release/bin/xastir-qt" > "$release/libraries.txt"
plugin_dir=$(qmake6 -query QT_INSTALL_PLUGINS)
for plugin in "$plugin_dir/platforms/libqwayland-generic.so" "$plugin_dir/wayland-shell-integration/libxdg-shell.so"; do
    [[ -f "$plugin" ]] || { echo "Missing Wayland plugin: $plugin" >&2; exit 1; }
    ldd "$plugin" >> "$release/libraries.txt"
done
if grep -q 'not found' "$release/libraries.txt"; then
    cat "$release/libraries.txt" >&2
    exit 1
fi
printf '%s\n' "$revision" > "$release/REVISION"

# Prepare the window rules/autostart before promoting the tested release.
python3 "$work/source/configure-qt-labwc.py" --output "$output"
if [[ -L "$prefix/current" ]]; then
    old_target=$(readlink "$prefix/current")
    ln -s "$old_target" "$prefix/previous.new.$$"
    mv -Tf "$prefix/previous.new.$$" "$prefix/previous"
fi
ln -s "$release" "$prefix/current.new.$$"
mv -Tf "$prefix/current.new.$$" "$prefix/current"
# Reload compositor configuration; existing applications keep running.
mapfile -t compositor_pids < <(pgrep -u "$(id -u)" -x labwc || true)
if (( ${#compositor_pids[@]} == 1 )); then
    kill -HUP "${compositor_pids[0]}"
fi
printf 'Deployed %s to %s\n' "$revision" "$release"
printf 'Launch command: %s/.local/bin/xastir-qt\n' "$HOME"
