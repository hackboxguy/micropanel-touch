#!/bin/sh
set -eu

script=$1
temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM
config=$temporary_directory/config.txt
cmdline=$temporary_directory/cmdline.txt

printf '%s\n' '[all]' 'dtoverlay=piscreen,drm=1,rotate=0,xohms=100,invx=1' > "$config"
printf '%s\n' 'console=tty1 root=LABEL=writable rootwait' > "$cmdline"

run_script() {
    MICROPANEL_TOUCH_ALLOW_UNPRIVILEGED_TEST=1 \
    MICROPANEL_TOUCH_CONFIG_PATH="$config" \
    MICROPANEL_TOUCH_CMDLINE_PATH="$cmdline" \
    MICROPANEL_TOUCH_SYSTEMCTL_COMMAND=/bin/true \
    /bin/sh "$script"
}

run_script
grep -Fqx 'dtoverlay=piscreen,drm=1,rotate=90,xohms=100,swapxy=1' "$config"
[ "$(grep -c '^[[:space:]]*dtoverlay=piscreen\(,\|$\)' "$config")" -eq 1 ]
grep -Fqx 'dtoverlay=gpio-led,gpio=22,label=micropanel-touch-piscreen-backlight,active_low=0' \
    "$config"
[ "$(grep -c '^# BEGIN micropanel-touch PiScreen Backlight$' "$config")" -eq 1 ]
grep -Eq '(^|[[:space:]])vt\.global_cursor_default=0([[:space:]]|$)' "$cmdline"
cp "$config" "$temporary_directory/first-config.txt"
cp "$cmdline" "$temporary_directory/first-cmdline.txt"

run_script
cmp "$temporary_directory/first-config.txt" "$config"
cmp "$temporary_directory/first-cmdline.txt" "$cmdline"
