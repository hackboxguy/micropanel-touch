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

if [ "$(id -u)" -ne 0 ]; then
    echo "Run this script as root (for example: sudo $0)." >&2
    exit 1
fi

config=/boot/firmware/config.txt
cmdline=/boot/firmware/cmdline.txt
overlay='dtoverlay=piscreen,drm=1,rotate=0,xohms=100,invx=1'

[ -f "$config" ] || { echo "Missing $config" >&2; exit 1; }
[ -f "$cmdline" ] || { echo "Missing $cmdline" >&2; exit 1; }

if ! grep -Fqx "$overlay" "$config"; then
    # Reopen [all] explicitly so an image-specific section above cannot make
    # the overlay silently disappear on a Pi 4.
    printf '\n[all]\n%s\n' "$overlay" >> "$config"
fi

if ! grep -Eq '(^|[[:space:]])vt\.global_cursor_default=0([[:space:]]|$)' "$cmdline"; then
    sed -i '1s/$/ vt.global_cursor_default=0/' "$cmdline"
fi

# The graphical app owns fb0 after boot; do not leave an interactive login
# prompt painting over it. Kernel output remains available through SSH/serial.
systemctl mask getty@tty1.service

echo "Configured PiScreen DRM overlay and masked getty@tty1.service."
echo "Reboot is required before /dev/fb0 and ADS7846 input can appear."

if [ "$reboot_after" = true ]; then
    systemctl reboot
fi
