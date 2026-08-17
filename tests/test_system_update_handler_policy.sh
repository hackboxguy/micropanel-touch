#!/bin/sh
set -eu

handler=$1

# The real acceptance writes a payload to the inactive partition on the Pi.
# Pin the non-negotiable streaming and selector ordering here for every build.
grep -Fq 'mount -o ro,nosuid,nodev,noexec -- "$source" "$source_mount"' "$handler"
grep -Fq 'update_usb_source=/dev/disk/by-label/MP_UPDATE' "$handler"
grep -Fq 'target_root=$(resolve_target_root "$running_slot")' "$handler"
grep -Fq 'dd if=/dev/zero of="$target_root" bs=1M count=1 conv=fsync status=none' "$handler"
grep -Fq '            ./) continue ;;' "$handler"
grep -Fq '            ..|../*|*/..|*/../*|*//*|cmdline.txt|cmdline.txt/)' "$handler"
grep -Fq 'xz --memlimit-decompress=80MiB --decompress --stdout -- "$rootfs"' "$handler"
grep -Fq 'tee "$hash_fifo" "$count_fifo"' "$handler"
grep -Fq '[ "$rootfs_sha256" = "${payload[rootfs_sha256]}" ] || die' "$handler"
grep -Fq 'e2label "$target_root" "MP_ROOT_${target_slot}"' "$handler"
grep -Fq 'boot cmdline template must contain exactly one line' "$handler"
grep -Fq 'write_update_progress "failed-${class}" 0' "$handler"
grep -Fq 'write_update_progress failed-internal 0' "$handler"
grep -Fq 'cmdline.txt.template' "$handler"
grep -Fq '"$selector" arm-candidate "$target_slot"' "$handler"
grep -Fq '/usr/sbin/reboot "0 tryboot"' "$handler"

hash_line=$(grep -nF '[ "$rootfs_sha256" = "${payload[rootfs_sha256]}" ] || die' "$handler" | cut -d: -f1)
label_line=$(grep -nF 'e2label "$target_root" "MP_ROOT_${target_slot}"' "$handler" | cut -d: -f1)
arm_line=$(grep -nF '"$selector" arm-candidate "$target_slot"' "$handler" | cut -d: -f1)
[ "$hash_line" -lt "$label_line" ]
[ "$label_line" -lt "$arm_line" ]

# The rootfs transfer cannot regress to a RAM-backed staging path.
! grep -Eq '(^|[[:space:]])(/tmp|\$TMPDIR|\${TMPDIR)' "$handler"
printf '%s\n' 'system-update-handler-policy: PASS'
