#!/bin/sh
# Run on the Pi as root (normally: sudo tools/enable-piscreen.sh [--reboot]).
# The overlay is intentionally unconditional for the supported SPI class; this
# script does not claim that a write-only panel can be physically detected.
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
systemctl_command=${MICROPANEL_TOUCH_SYSTEMCTL_COMMAND:-systemctl}
# Native portrait avoids LVGL's costly runtime rotation.  `swapxy=1` is the
# verified PiScreen touch mapping for this mode; do not add a `speed=` override
# without panel-specific integrity testing.
overlay='dtoverlay=piscreen,drm=1,rotate=90,xohms=100,swapxy=1'
# The ILI9486 driver's legacy led-gpios property leaves the PiScreen's
# GPIO-22 backlight-enable line unclaimed with the DRM driver. Expose that
# binary enable through the LED class instead of letting the HMI touch GPIO
# directly. This is intentionally standby-only: GPIO 22 is not a PWM output.
backlight_overlay='dtoverlay=gpio-led,gpio=22,label=micropanel-touch-piscreen-backlight,active_low=0'

[ -f "$config" ] || { echo "Missing $config" >&2; exit 1; }
[ -f "$cmdline" ] || { echo "Missing $cmdline" >&2; exit 1; }

# The script owns every PiScreen line: leaving an older landscape profile in
# place and appending this portrait profile would claim the same SPI/GPIO
# resources twice at boot. The marked block makes repeat runs idempotent; the
# unmarked-line rule migrates installations produced by older script versions.
sed -i \
    -e '/^# BEGIN micropanel-touch PiScreen$/,/^# END micropanel-touch PiScreen$/d' \
    -e '/^# BEGIN micropanel-touch PiScreen Backlight$/,/^# END micropanel-touch PiScreen Backlight$/d' \
    -e '/^[[:space:]]*dtoverlay=piscreen\(,\|$\)/d' \
    "$config"
if [ -s "$config" ] && ! tail -c 1 "$config" | grep -q '^$'; then
    printf '\n' >> "$config"
fi
printf '%s\n' \
    '# BEGIN micropanel-touch PiScreen' \
    '[all]' \
    "$overlay" \
    '# END micropanel-touch PiScreen' \
    '# BEGIN micropanel-touch PiScreen Backlight' \
    "$backlight_overlay" \
    '# END micropanel-touch PiScreen Backlight' >> "$config"

if ! grep -Eq '(^|[[:space:]])vt\.global_cursor_default=0([[:space:]]|$)' "$cmdline"; then
    sed -i '1s/$/ vt.global_cursor_default=0/' "$cmdline"
fi

# The graphical app owns fb0 after boot; do not leave an interactive login
# prompt painting over it. Kernel output remains available through SSH/serial.
"$systemctl_command" mask getty@tty1.service

echo "Configured PiScreen DRM overlay and masked getty@tty1.service."
echo "Reboot is required before /dev/fb0 and ADS7846 input can appear."

if [ "$reboot_after" = true ]; then
    systemctl reboot
fi
