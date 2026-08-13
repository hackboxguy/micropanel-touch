#!/bin/sh
set -eu

script=$1
temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM
config=$temporary_directory/config.txt
cmdline=$temporary_directory/cmdline.txt

printf '%s\n' \
    '[all]' \
    'dtoverlay=piscreen,drm=1,rotate=90,xohms=100,swapxy=1' \
    '# BEGIN micropanel-touch Luckfox CTP' \
    '[all]' \
    'dtoverlay=goodix,addr=0x14' \
    '# END micropanel-touch Luckfox CTP' > "$config"
printf '%s\n' 'console=tty1 root=LABEL=writable rootwait' > "$cmdline"

run_script() {
    MICROPANEL_TOUCH_ALLOW_UNPRIVILEGED_TEST=1 \
    MICROPANEL_TOUCH_CONFIG_PATH="$config" \
    MICROPANEL_TOUCH_CMDLINE_PATH="$cmdline" \
    MICROPANEL_TOUCH_SYSTEMCTL_COMMAND=/bin/true \
    /bin/sh "$script"
}

run_script
! grep -q '^[[:space:]]*dtoverlay=piscreen\(,\|$\)' "$config"
grep -Fqx 'dtoverlay=mipi-dbi-spi,spi0-0,speed=48000000' "$config"
grep -Fqx 'dtparam=compatible=st7796s\0panel-mipi-dbi-spi' "$config"
grep -Fqx 'dtoverlay=goodix,addr=0x5d,interrupt=4,reset=17' "$config"
[ "$(grep -c '^dtoverlay=goodix,addr=0x5d,interrupt=4,reset=17$' "$config")" -eq 1 ]
grep -Eq '(^|[[:space:]])vt\.global_cursor_default=0([[:space:]]|$)' "$cmdline"
cp "$config" "$temporary_directory/first-config.txt"
cp "$cmdline" "$temporary_directory/first-cmdline.txt"

run_script
cmp "$temporary_directory/first-config.txt" "$config"
cmp "$temporary_directory/first-cmdline.txt" "$cmdline"
