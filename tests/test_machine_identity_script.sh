#!/bin/sh
set -eu

script=$1
temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM

persistent_directory=$temporary_directory/data/micropanel-touch-system
system_id=$temporary_directory/etc/machine-id
dbus_id=$temporary_directory/var/lib/dbus/machine-id
mkdir -p "$(dirname "$system_id")" "$(dirname "$dbus_id")"

run_script() {
    MICROPANEL_TOUCH_MACHINE_ID_ASSUME_PERSISTENT_DATA=1 \
    MICROPANEL_TOUCH_MACHINE_ID_PERSISTENT_DIR="$persistent_directory" \
    MICROPANEL_TOUCH_MACHINE_ID_SYSTEM_PATH="$system_id" \
    MICROPANEL_TOUCH_MACHINE_ID_DBUS_PATH="$dbus_id" \
    /bin/sh "$script"
}

first_id=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
printf '%s\n' "$first_id" > "$system_id"
: > "$dbus_id"
run_script
test "$(cat "$persistent_directory/machine-id")" = "$first_id"
test "$(cat "$system_id")" = "$first_id"
test "$(cat "$dbus_id")" = "$first_id"
test "$(stat -c %a "$persistent_directory/machine-id")" = 444
test "$(stat -c %a "$persistent_directory")" = 700

# A later boot must retain the persisted identity rather than the temporary
# one systemd generated before the data partition became available.
second_id=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
third_id=cccccccccccccccccccccccccccccccc
chmod 0644 "$persistent_directory/machine-id" "$system_id" "$dbus_id"
printf '%s\n' "$second_id" > "$persistent_directory/machine-id"
printf '%s\n' "$third_id" > "$system_id"
printf '%s\n' "$third_id" > "$dbus_id"
run_script
test "$(cat "$persistent_directory/machine-id")" = "$second_id"
test "$(cat "$system_id")" = "$second_id"
test "$(cat "$dbus_id")" = "$second_id"
test "$(stat -c %a "$persistent_directory/machine-id")" = 444

# A corrupt stored value is replaced by the valid transient first-boot ID.
chmod 0644 "$persistent_directory/machine-id" "$system_id"
printf '%s\n' invalid > "$persistent_directory/machine-id"
printf '%s\n' "$third_id" > "$system_id"
run_script
test "$(cat "$persistent_directory/machine-id")" = "$third_id"
