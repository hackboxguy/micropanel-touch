#!/bin/bash
# Build the application for the panel without building an image, and optionally
# push it onto a running one.
#
# Why this exists: a full image build is ~40 minutes, and until now that was the
# only way to see a UI change on hardware. This is ~2 minutes cold and seconds
# incrementally.
#
# Why a qemu chroot rather than a cross-toolchain: the host's aarch64 GCC brings
# its own glibc and its own libstdc++, both newer than the image's, and a binary
# linked against them fails on the device in ways that look like application
# bugs. Building *inside the image's own rootfs* removes the question entirely -
# it is the same compiler, the same headers and the same libraries the release
# uses. The sysroot is the base-stage image, which still carries the -dev
# packages that the apps stage purges.
#
# This is a development loop. It produces nothing a release consumes: releases
# are built by misc-tools/build-image.sh inside the image, from a pushed commit.
set -euo pipefail

source_dir=$(cd "$(dirname "$0")/.." && pwd)
workspace=${MICROPANEL_TOUCH_WORKSPACE:-$HOME/pi-image-workspace}
base_image=${MICROPANEL_TOUCH_BASE_IMAGE:-}
mount_root=${MICROPANEL_TOUCH_ARM_ROOT:-/mnt/armbuild}
build_dir=build-arm64
panel=${MICROPANEL_TOUCH_PANEL:-}
qemu=${QEMU_STATIC:-/usr/bin/qemu-aarch64-static}

usage() {
    cat <<'USAGE'
Usage: tools/cross-build.sh [--deploy USER@HOST] [--clean] [--umount]

  --deploy USER@HOST  copy the built binary, handlers and screens onto a
                      running panel and restart its services
  --clean             discard the aarch64 build directory first
  --umount            tear the build chroot down and exit

Environment:
  MICROPANEL_TOUCH_BASE_IMAGE  base-stage image to use as the build rootfs
                               (default: the newest one in the workspace)
  MICROPANEL_TOUCH_PANEL       default --deploy target
  SSHPASS / sshpass            used for --deploy if key auth is not set up
USAGE
}

deploy_target=$panel
clean=0
umount_only=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --deploy) deploy_target=${2:?--deploy needs USER@HOST}; shift 2 ;;
        --deploy=*) deploy_target=${1#*=}; shift ;;
        --clean) clean=1; shift ;;
        --umount) umount_only=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

teardown() {
    for m in "$mount_root/src" "$mount_root/sys" "$mount_root/proc" "$mount_root/dev" "$mount_root"; do
        mountpoint -q "$m" 2>/dev/null && sudo umount "$m"
    done
    return 0
}

if [ "$umount_only" = 1 ]; then
    teardown
    echo "build chroot torn down"
    exit 0
fi

if [ -z "$base_image" ]; then
    base_image=$(ls -t "$workspace"/base/micropanel-touch*/*.img 2>/dev/null | head -1 || true)
fi
[ -n "$base_image" ] && [ -f "$base_image" ] || {
    echo "ERROR: no base-stage image found; build one with misc-tools/build-image.sh" >&2
    exit 1
}
[ -x "$qemu" ] || { echo "ERROR: $qemu is missing (install qemu-user-static)" >&2; exit 1; }

if ! mountpoint -q "$mount_root" 2>/dev/null; then
    echo "Preparing the build rootfs from $(basename "$base_image")"
    # An overlay, so the cached base image the real pipeline reuses stays
    # byte-for-byte untouched by anything built here.
    upper=$workspace/tmp/arm-upper
    work=$workspace/tmp/arm-work
    sudo mkdir -p "$mount_root" "$upper" "$work"
    lower=$(mktemp -d)
    loop=$(sudo losetup --find --show --partscan --read-only "$base_image")
    for _ in $(seq 1 50); do [ -b "${loop}p2" ] && break; sleep 0.1; done
    sudo mount -o ro "${loop}p2" "$lower"
    sudo mount -t overlay overlay \
        -o "lowerdir=$lower,upperdir=$upper,workdir=$work" "$mount_root"
    sudo cp "$qemu" "$mount_root$qemu"
    sudo mount --bind /dev "$mount_root/dev"
    sudo mount -t proc proc "$mount_root/proc"
    sudo mount -t sysfs sys "$mount_root/sys"
    sudo mkdir -p "$mount_root/src"
    sudo mount --bind "$source_dir" "$mount_root/src"
fi

[ "$clean" = 1 ] && sudo rm -rf "$source_dir/$build_dir"

echo "Building for aarch64 with the image's own toolchain"
sudo chroot "$mount_root" /bin/bash -c "
    set -e
    cd /src
    cmake -S . -B $build_dir -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF >/dev/null
    cmake --build $build_dir --target micropanel-touch -j\$(nproc)
"
sudo chown "$(id -u):$(id -g)" "$source_dir/$build_dir/micropanel-touch"
echo "Built: $build_dir/micropanel-touch"

[ -n "$deploy_target" ] || exit 0

echo "Deploying to $deploy_target"
ssh_cmd=(ssh -o StrictHostKeyChecking=no)
scp_cmd=(scp -o StrictHostKeyChecking=no)
if [ -n "${SSHPASS:-}" ] && command -v sshpass >/dev/null 2>&1; then
    ssh_cmd=(sshpass -e "${ssh_cmd[@]}")
    scp_cmd=(sshpass -e "${scp_cmd[@]}")
fi

# Handlers and screen configs are data: they need no compilation and are worth
# pushing every time, because a stale handler beside a fresh binary is a
# confusing way to spend an evening.
"${scp_cmd[@]}" "$source_dir/$build_dir/micropanel-touch" \
    "$source_dir"/handlers/micropanel-touch-* \
    "$source_dir"/screens/config-basic.json \
    "$deploy_target:/tmp/" >/dev/null

# The panel's root is a read-only lower layer with a tmpfs upper, so this
# survives until the next reboot and no further. That is the right lifetime for
# a test build: nothing here can quietly become what a device ships.
"${ssh_cmd[@]}" "$deploy_target" 'sudo sh -s' <<'REMOTE'
set -eu
prefix=/opt/micropanel-touch
systemctl stop micropanel-touch.service
install -m0755 -o root -g root /tmp/micropanel-touch "$prefix/usr/bin/micropanel-touch"
for h in /tmp/micropanel-touch-*; do
    [ -f "$h" ] || continue
    case "$h" in */micropanel-touch) continue ;; esac
    install -m0755 -o root -g root "$h" "$prefix/usr/bin/$(basename "$h")"
done
install -m0644 -o root -g root /tmp/config-basic.json \
    "$prefix/share/micropanel-touch/screens/config-basic.json"
rm -f /tmp/micropanel-touch /tmp/micropanel-touch-* /tmp/config-basic.json
systemctl restart micropanel-touch-privileged.service
systemctl start micropanel-touch.service
sleep 3
systemctl is-active micropanel-touch.service micropanel-touch-privileged.service
echo "deployed; volatile until the next reboot"
REMOTE
