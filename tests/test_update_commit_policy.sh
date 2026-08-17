#!/bin/sh
set -eu

commit_helper=$1
unit=$2

grep -Fq 'is_tryboot_candidate || exit 0' "$commit_helper"
grep -Fq 'systemctl is-active --quiet "$hmi_unit"' "$commit_helper"
grep -Fq 'systemctl is-active --quiet "$broker_unit"' "$commit_helper"
grep -Fq '[ -f "$frame_marker" ] && [ ! -L "$frame_marker" ]' "$commit_helper"
grep -Fq '"$selector" commit "$current_slot"' "$commit_helper"
grep -Fq 'state=committed' "$commit_helper"
grep -Fq 'TimeoutStartSec=3min' "$unit"
grep -Fq 'After=data.mount micropanel-touch.service micropanel-touch-privileged.service' "$unit"

printf '%s\n' 'update-commit-policy: PASS'
