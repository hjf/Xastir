#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build committed HEAD natively on the target and update its Labwc autostart.
set -euo pipefail
host=hjf@direwolf
output=HDMI-A-2
jobs=2
usage() {
    echo "Usage: $0 [--host user@host] [--output HDMI-A-2] [--jobs 2]"
    echo 'Deploys committed HEAD; tests run offscreen. Activation is at the next Labwc login.'
}
while (( $# )); do
    case "$1" in
        --host|--output|--jobs)
            (( $# >= 2 )) || { usage >&2; exit 2; }
            case "$1" in --host) host=$2;; --output) output=$2;; --jobs) jobs=$2;; esac
            shift 2;;
        --help|-h) usage; exit 0;;
        *) usage >&2; exit 2;;
    esac
done
[[ "$host" =~ ^[A-Za-z0-9_.@:-]+$ && "$host" != -* && "$output" =~ ^[A-Za-z0-9_.:-]+$ && "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'Invalid host, output, or job count.' >&2; exit 2; }
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(git -C "$script_dir" rev-parse --show-toplevel)
revision=$(git -C "$repo" rev-parse HEAD)
upload_name="${revision:0:12}-$(date -u +%Y%m%dT%H%M%SZ)-$$"
upload=".cache/xastir-qt-deploy/uploads/$upload_name"
# Ignore unrelated local configuration/generated files; only committed source is shipped.
if ! git -C "$repo" diff --quiet HEAD -- CMakeLists.txt qt src tests/core; then
    echo 'Commit application source changes before deploying committed HEAD.' >&2
    exit 1
fi
snapshot=$(mktemp -d)
trap 'rm -rf -- "$snapshot"' EXIT
git -C "$repo" archive HEAD | tar -x -C "$snapshot"
ssh_command=(ssh -F "${XASTIR_DEPLOY_SSH_CONFIG:-/dev/null}" -o BatchMode=yes -o ConnectTimeout=10)
printf -v remote_shell '%q ' "${ssh_command[@]}"
"${ssh_command[@]}" "$host" "mkdir -p $upload"
rsync -az --delete -e "$remote_shell" "$snapshot/" "$host:$upload/"
rsync -az -e "$remote_shell" "$script_dir/configure-qt-labwc.py" "$host:$upload/configure-qt-labwc.py"
printf -v remote_command 'bash -s -- %q %q %q %q' "$revision" "$jobs" "$output" "$upload_name"
"${ssh_command[@]}" "$host" "$remote_command" < "$script_dir/deploy-qt-remote.sh"
