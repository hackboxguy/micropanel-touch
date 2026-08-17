#!/bin/sh
set -eu

handler=$1

# The real acceptance writes a payload to the inactive partition on the Pi.
# Pin the non-negotiable streaming and selector ordering here for every build.
grep -Fq 'mount -o ro,nosuid,nodev,noexec -- "$source" "$source_mount"' "$handler"
grep -Fq 'xz --memlimit-decompress=64MiB --decompress --stdout -- "$rootfs"' "$handler"
grep -Fq 'tee "$hash_fifo" "$count_fifo"' "$handler"
grep -Fq '[ "$rootfs_sha256" = "${payload[rootfs_sha256]}" ] || die' "$handler"
grep -Fq 'e2label "$target_root" "MP_ROOT_${target_slot}"' "$handler"
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
