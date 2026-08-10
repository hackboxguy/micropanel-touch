#!/bin/sh
# Sprint 0 deploy loop: copy source, build natively on the Pi, optionally run.
# Credentials are deliberately external (SSH agent/key or interactive prompt).
set -eu

usage() {
    cat <<'EOF'
Usage: tools/deploy.sh [--run] [--release] [--clean] [-- <micropanel-touch arguments>]

Environment:
  MICROPANEL_TOUCH_TARGET       SSH target (default: pi@192.168.1.124)
  MICROPANEL_TOUCH_REMOTE_ROOT  Remote source root (default: /home/pi/micropanel-touch)
EOF
}

run_after_build=false
clean_build=false
build_type=Debug
while [ "$#" -gt 0 ]; do
    case "$1" in
        --run) run_after_build=true ;;
        --release) build_type=Release ;;
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

quote_for_remote() {
    printf "'"
    printf '%s' "$1" | sed "s/'/'\\\\''/g"
    printf "'"
}

remote_root_quoted=$(quote_for_remote "$remote_root")
ssh "$target" "mkdir -p $remote_root_quoted"
rsync -az --delete \
    --exclude .git \
    --exclude build \
    --exclude .cache \
    "$repo_root/" "$target:$remote_root/"

if [ "$clean_build" = true ]; then
    ssh "$target" "rm -rf $(quote_for_remote "$remote_root/build")"
fi

ssh "$target" "cmake -S $remote_root_quoted -B $(quote_for_remote "$remote_root/build") -G Ninja -DCMAKE_BUILD_TYPE=$build_type -DBUILD_TESTING=ON && cmake --build $(quote_for_remote "$remote_root/build")"

if [ "$run_after_build" = true ]; then
    remote_command=$(quote_for_remote "$remote_root/build/micropanel-touch")
    for argument in "$@"; do
        remote_command="$remote_command $(quote_for_remote "$argument")"
    done
    exec ssh -t "$target" "exec $remote_command"
fi
