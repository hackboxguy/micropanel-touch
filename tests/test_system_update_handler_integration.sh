#!/bin/bash
# Exercise the real Stage 2 handler against disposable loop partitions. This
# verifies stream -> hash -> e2fsck -> relabel -> boot render -> selector arm
# without a Pi or a real reboot. Run as root; ordinary ctest users skip it.
set -euo pipefail

handler=${1:?handler path is required}

if [ "$(id -u)" -ne 0 ]; then
    echo 'SKIP: system update handler integration requires root loop/mount access'
    exit 77
fi

for tool in truncate sfdisk losetup mkfs.vfat mkfs.ext4 mount umount mountpoint \
            install dd xz sha256sum stat tar awk grep sync e2label findmnt lsblk \
            blockdev flock sleep seq; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "ERROR: missing test tool: $tool" >&2
        exit 1
    }
done

work=$(mktemp -d)
image="$work/handler-fixture.img"
loop=""
boot_mount="$work/boot"
lower_root_mount="$work/lower-root"
target_root_mount="$work/target-root"

unmount_if_mounted() {
    local directory=$1
    [ -n "$directory" ] && mountpoint -q "$directory" && umount "$directory"
}

cleanup() {
    local status=$?
    unmount_if_mounted "$target_root_mount" || true
    unmount_if_mounted "$lower_root_mount" || true
    unmount_if_mounted "$boot_mount" || true
    [ -z "$loop" ] || losetup -d "$loop" 2>/dev/null || true
    rm -rf "$work"
    exit "$status"
}
trap cleanup EXIT HUP INT TERM

# p5/p6 are logical partitions inside p4, matching the production A/B
# contract closely enough for lsblk PKNAME/PARTN resolution to be real.
truncate -s 320M "$image"
sfdisk "$image" >/dev/null <<EOF
label: dos
unit: sectors

${image}1 : start=2048, size=65536, type=c, bootable
${image}4 : start=67584, size=587776, type=5
${image}5 : start=69632, size=196608, type=83
# Each logical partition needs an EBR sector immediately before it.  Leave a
# 1 MiB gap after p5 so sfdisk can place the p6 EBR without overlapping p5.
${image}6 : start=268288, size=196608, type=83
EOF

loop=$(losetup --find --show --partscan "$image")
for _ in $(seq 1 20); do
    [ -b "${loop}p1" ] && [ -b "${loop}p5" ] && [ -b "${loop}p6" ] && break
    sleep 1
done
[ -b "${loop}p1" ] && [ -b "${loop}p5" ] && [ -b "${loop}p6" ] || {
    echo 'ERROR: handler fixture loop partitions did not appear' >&2
    exit 1
}

mkfs.vfat -F32 -n MP_BOOT_A "${loop}p1" >/dev/null
mkfs.ext4 -F -L MP_ROOT_A "${loop}p5" >/dev/null
mkfs.ext4 -F -L MP_ROOT_B "${loop}p6" >/dev/null
install -d "$boot_mount" "$lower_root_mount" "$target_root_mount"
mount "${loop}p1" "$boot_mount"
mount "${loop}p5" "$lower_root_mount"
printf '%s\n' 'handler integration root marker' > "$lower_root_mount/payload-marker"
sync

install -d "$boot_mount/A" "$boot_mount/B"
printf '%s\n' 'old inactive boot tree' > "$boot_mount/B/obsolete"

updates="$work/updates"
release="$updates/release"
prefix=micropanel-touch-fixture-luckfox-ctp
install -d "$release" "$work/payload-boot"
printf '%s\n' \
    'console=tty1 root=LABEL=@MICROPANEL_SLOT@ rootfstype=ext4 rootwait overlayroot=tmpfs:recurse=0' \
    > "$work/payload-boot/cmdline.txt.template"
printf '%s\n' 'fixture kernel' > "$work/payload-boot/kernel8.img"
tar --format=posix --sort=name --owner=0 --group=0 --numeric-owner --mtime=@0 \
    -C "$work/payload-boot" -cf "$release/$prefix.boot.tar" .

# The source rootfs must be label-neutral exactly as a published payload is.
rootfs_raw="$work/rootfs.img"
dd if="${loop}p5" of="$rootfs_raw" bs=1M status=none
e2label "$rootfs_raw" ""
rootfs_bytes=$(stat -c %s "$rootfs_raw")
rootfs_sha256=$(sha256sum "$rootfs_raw" | awk '{print $1}')
xz --threads=0 --check=crc64 --lzma2=dict=16MiB --stdout "$rootfs_raw" > "$release/$prefix.rootfs.img.xz"
boot_sha256=$(sha256sum "$release/$prefix.boot.tar" | awk '{print $1}')
printf '%s\n' \
    'version=fixture' \
    'variant=luckfox-ctp' \
    'boards=pi4' \
    "rootfs_sha256=$rootfs_sha256" \
    "rootfs_bytes=$rootfs_bytes" \
    "boot_sha256=$boot_sha256" \
    'format=1' > "$release/$prefix.manifest"

image_manifest="$work/image-manifest.env"
printf '%s\n' \
    'IMAGE_LAYOUT=ab' \
    'PANEL_VARIANT=luckfox-ctp' \
    'SLOT_COMPATIBLE_BOARDS=pi4' > "$image_manifest"

selector="$work/selector"
selector_log="$work/selector.log"
printf '%s\n' \
    '#!/bin/sh' \
    'case "$1" in' \
    '  current-slot) printf "%s\\n" A ;;' \
    '  arm-candidate) [ "$2" = B ] && printf "%s %s\\n" "$1" "$2" > "$SELECTOR_LOG" ;;' \
    '  *) exit 64 ;;' \
    'esac' > "$selector"
chmod 0755 "$selector"

reboot_command="$work/reboot"
reboot_log="$work/reboot.log"
printf '%s\n' \
    '#!/bin/sh' \
    'printf "%s\\n" "$*" > "$REBOOT_LOG"' > "$reboot_command"
chmod 0755 "$reboot_command"

state_dir="$work/state"
runtime_dir="$work/runtime"
SELECTOR_LOG="$selector_log" \
REBOOT_LOG="$reboot_log" \
MICROPANEL_IMAGE_MANIFEST="$image_manifest" \
MICROPANEL_SLOT_SELECTOR="$selector" \
MICROPANEL_BOOT_DIR="$boot_mount" \
MICROPANEL_UPDATE_STATE_DIR="$state_dir" \
MICROPANEL_UPDATE_RUNTIME_DIR="$runtime_dir" \
MICROPANEL_LOWER_ROOT_MOUNT="$lower_root_mount" \
MICROPANEL_ROOT_PARENT_PATTERN='^loop[0-9]+$' \
MICROPANEL_LOCAL_UPDATE_ROOT="$updates" \
MICROPANEL_UPDATE_REBOOT_COMMAND="$reboot_command" \
MICROPANEL_BOARD=pi4 \
    /bin/bash "$handler" "$release"

[ "$(e2label "${loop}p6")" = MP_ROOT_B ]
mount -o ro "${loop}p6" "$target_root_mount"
grep -Fqx 'handler integration root marker' "$target_root_mount/payload-marker"
unmount_if_mounted "$target_root_mount"
grep -Fqx 'root=LABEL=MP_ROOT_B' <(tr ' ' '\n' < "$boot_mount/B/cmdline.txt")
test ! -e "$boot_mount/B/cmdline.txt.template"
test ! -e "$boot_mount/B/obsolete"
grep -Fqx 'state=candidate-armed' "$state_dir/update-state"
grep -Fqx 'candidate_slot=B' "$state_dir/update-state"
grep -Fqx 'arm-candidate B' "$selector_log"
grep -Fqx '0 tryboot' "$reboot_log"
grep -Fqx 'phase=arming' "$runtime_dir/progress"
grep -Fqx 'progress=100' "$runtime_dir/progress"

echo 'system-update-handler-integration: PASS'
