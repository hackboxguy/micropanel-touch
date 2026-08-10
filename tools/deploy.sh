#!/bin/sh
# Sprint 0 deploy loop: copy source, build natively on the Pi, optionally run.
# Credentials are deliberately external (SSH agent/key or interactive prompt).
set -eu

usage() {
    cat <<'EOF'
Usage: tools/deploy.sh [--run] [--clean] [-- <micropanel-touch arguments>]

Environment:
  MICROPANEL_TOUCH_TARGET       SSH target (default: pi@192.168.1.124)
  MICROPANEL_TOUCH_REMOTE_ROOT  Remote source root (default: /home/pi/micropanel-touch)
EOF
}

run_after_build=false
clean_build=false
while [ "$#" -gt 0 ]; do
    case "$1" in
        --run) run_after_build=true ;;
        --clean) clean_build=true ;;
        --help) usage; exit 0 ;;
        --) shift; break ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

target=${MICROPANEL_TOUCH_TARGET:-pi@192.168.1.124}
remote_root=${MICROPANEL_TOUCH_REMOTE_ROOT:-/home/pi/micropanel-touch}
repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

ssh "$target" "mkdir -p '$remote_root'"
rsync -az --delete \
    --exclude .git \
    --exclude build \
    --exclude .cache \
    "$repo_root/" "$target:$remote_root/"

if [ "$clean_build" = true ]; then
    ssh "$target" "rm -rf '$remote_root/build'"
fi

ssh "$target" "cmake -S '$remote_root' -B '$remote_root/build' -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON && cmake --build '$remote_root/build'"

if [ "$run_after_build" = true ]; then
    exec ssh -t "$target" "'$remote_root/build/micropanel-touch' $*"
fi
