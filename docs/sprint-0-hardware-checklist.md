# Sprint 0 hardware checklist

The PiScreen overlay owns the configured SPI display backend. It cannot prove
that physical glass is connected; the application detects the configured DRM
card, its connected DRM connector, and the associated framebuffer node.

Run these checks on each named supported panel before the touch quirk model is
frozen. Record the output in `sprint-notes.md`.

1. Install tooling once: `ssh pi@192.168.1.124 'cd /home/pi/micropanel-touch && ./tools/bootstrap-pi.sh'`.
2. Enable the panel and quiet console once: `sudo ./tools/enable-piscreen.sh --reboot`.
3. After reconnecting, run `build/micropanel-touch --probe`. Confirm a stable
   SPI `/dev/dri/by-path/*spi*-card` entry maps through a `cardN-SPI-*`
   connector to the actual `/dev/fbN`.
4. Record `evtest /dev/input/eventN` corner + center taps with the shipping
   overlay. The application uses those kernel-transformed ranges directly and
   must not add swap/invert a second time.
5. Record `ls -l /sys/class/backlight /sys/class/leds`, `gpioinfo`, and the
   probe output. Inspect GPIO 22 specifically, but do not assume it is safe to
   drive with libgpiod: choose a kernel-exported control or make an explicit DT
   change if v1 needs software backlight control.
6. Run `tools/deploy.sh --run -- --run-seconds 20`, tap all four corners and
   the counter button, then repeat with HDMI attached. The expected outcome is
   one clean increment per press, no `(0,0)` initial press, and a correct
   DRM-to-fb mapping in both probe orders.

The console policy for this sprint is: mask `getty@tty1`, hide the VT cursor,
and use SSH/serial for diagnostics. The app does not take root or alter boot
state; the setup script performs the explicitly privileged boot configuration.
