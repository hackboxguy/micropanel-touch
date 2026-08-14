#!/bin/sh
# Run on the Pi as root to select the verified Luckfox 3.5-RPi-LCD-CTP panel.
# This is intentionally a distinct boot profile: ST7796S/GT911 cannot share
# the PiScreen ILI9486/ADS7846 SPI and GPIO claims.
set -eu

reboot_after=false
if [ "${1:-}" = "--reboot" ]; then
    reboot_after=true
elif [ "$#" -ne 0 ]; then
    echo "Usage: $0 [--reboot]" >&2
    exit 2
fi

if [ "$(id -u)" -ne 0 ] && [ "${MICROPANEL_TOUCH_ALLOW_UNPRIVILEGED_TEST:-0}" != "1" ]; then
    echo "Run this script as root (for example: sudo $0)." >&2
    exit 1
fi

config=${MICROPANEL_TOUCH_CONFIG_PATH:-/boot/firmware/config.txt}
cmdline=${MICROPANEL_TOUCH_CMDLINE_PATH:-/boot/firmware/cmdline.txt}
modules_load=${MICROPANEL_TOUCH_MODULES_LOAD_PATH:-/etc/modules-load.d/micropanel-touch-luckfox-ctp.conf}
systemctl_command=${MICROPANEL_TOUCH_SYSTEMCTL_COMMAND:-systemctl}

[ -f "$config" ] || { echo "Missing $config" >&2; exit 1; }
[ -f "$cmdline" ] || { echo "Missing $cmdline" >&2; exit 1; }

# The Luckfox vendor configuration is a DRM MIPI-DBI panel with an ST7796S
# command-sequence firmware blob and a GT911 at 0x5d. GPIO 17 is the GT911
# reset line, so retaining PiScreen's ILI9486 node would prevent touch probe.
sed -i \
    -e '/^# BEGIN micropanel-touch PiScreen$/,/^# END micropanel-touch PiScreen$/d' \
    -e '/^[[:space:]]*dtoverlay=piscreen\(,\|$\)/d' \
    -e '/^# BEGIN micropanel-touch Luckfox CTP$/,/^# END micropanel-touch Luckfox CTP$/d' \
    "$config"
if [ -s "$config" ] && ! tail -c 1 "$config" | grep -q '^$'; then
    printf '\n' >> "$config"
fi
printf '%s\n' \
    '# BEGIN micropanel-touch Luckfox CTP' \
    '[all]' \
    'dtparam=spi=on' \
    'dtparam=i2c_arm=on' \
    'dtparam=i2c_arm_baudrate=50000' \
    'dtparam=audio=off' \
    'dtoverlay=mipi-dbi-spi,spi0-0,speed=48000000' \
    'dtparam=compatible=st7796s\0panel-mipi-dbi-spi' \
    'dtparam=width=320,height=480,width-mm=49,height-mm=79' \
    'dtparam=reset-gpio=27,dc-gpio=22,backlight-pwm=0,backlight-pwm-chan=0,backlight-pwm-gpio=18,backlight-pwm-func=2' \
    'dtoverlay=goodix,addr=0x5d,interrupt=4,reset=17' \
    '# END micropanel-touch Luckfox CTP' >> "$config"

# The vendor-compatible string must remain first because panel-mipi-dbi derives
# its firmware filename from it (st7796s.bin).  On modular kernels this also
# means udev emits spi:st7796s, which has no alias for panel_mipi_dbi.  Load the
# in-tree driver explicitly on this opt-in image variant.
mkdir -p "$(dirname "$modules_load")"
printf '%s\n' 'panel_mipi_dbi' > "$modules_load"

if ! grep -Eq '(^|[[:space:]])vt\.global_cursor_default=0([[:space:]]|$)' "$cmdline"; then
    sed -i '1s/$/ vt.global_cursor_default=0/' "$cmdline"
fi

"$systemctl_command" mask getty@tty1.service

echo "Configured Luckfox ST7796S/GT911 DRM panel profile with PWM backlight, panel_mipi_dbi module load, and masked getty@tty1.service."
echo "Reboot is required before /dev/fb0 and Goodix multitouch can appear."

if [ "$reboot_after" = true ]; then
    systemctl reboot
fi
