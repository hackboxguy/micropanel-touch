# Sprint notes

## Sprint 1 — network vertical-slice foundation

- `screens/config-basic.json` now drives a temporary touch menu loader. The
  Network → Info route renders live interface name, IPv4, MAC, link state, and
  carrier state collected every 500 ms by a worker thread.
- The worker posts immutable snapshots to `UiEventQueue`; an LVGL timer drains
  it on the UI thread. No worker calls LVGL.
- Bench acceptance: Network → Info navigation and live `eth0` data were
  confirmed on the panel on 2026-08-10. The starter UI uses the existing dark
  contrast baseline; configurable skins remain Sprint 3 work.

## Sprint 0 — walking skeleton

### Starting bench state — 2026-08-10

- Pi: `pi@192.168.1.124` (`pi4-display-setup`), Raspberry Pi OS Lite / Debian
  trixie, kernel `6.18.34+rpt-rpi-v8`, aarch64.
- Before enablement: no PiScreen overlay, no `/dev/fb*`; only vc4/v3d DRM
  by-path entries were present.
- `cmake`, `ninja`, and `git` were absent. `gcc/g++` was already installed.
- LVGL uses the C library allocator on Linux: its built-in 64 KiB pool cannot
  hold a 480 x 60 line RGB565 partial framebuffer and the UI allocation set.

### Bench result — 2026-08-10

- `tools/enable-piscreen.sh --reboot` installed the required overlay under
  `[all]` and masked `getty@tty1.service`. After boot, `fb0` is
  `ili9486drmfb`, `480x320`, 16 bpp; the overlay line is
  `dtoverlay=piscreen,drm=1,rotate=0,xohms=100,invx=1`.
- Discovery chose the stable SPI entry
  `/dev/dri/by-path/platform-fe204000.spi-cs-0-card`, found connected
  `card1-SPI-1`, and mapped it to `/dev/fb0`. The onboard HDMI connectors were
  disconnected during this run.
- The only compatible input was `/dev/input/event0`, `ADS7846 Touchscreen`,
  with X/Y ranges `0..4095` and pressure `0..255`. A two-second hello-app smoke
  test opened it through the direct evdev path and exited successfully. Raw
  `evtest` evidence confirms `BTN_TOUCH` precedes X/Y/pressure in each contact.
- Manual bench acceptance: the repaired hello app incremented its counter on
  physical taps on 2026-08-10. Touch reports are queued individually for LVGL,
  so a short press and release read together cannot be collapsed into a
  release-only event.
- There is no `/sys/class/backlight` entry and no panel-specific LED. GPIO 22
  reports as an unclaimed input on `gpiochip0`; direct GPIO control remains
  deliberately unsupported. A future brightness feature needs an explicit DT
  ownership/control decision.
- The first LVGL smoke test hung in its assertion handler because the default
  64 KiB built-in allocator could not allocate the 57,600-byte draw buffer.
  Switching Linux builds to the C library allocator fixed the issue.
- Console policy: retain `console=tty1`, serial diagnostics, a masked panel
  getty, and a hidden VT cursor. Removing `console=tty1` left the bench Pi
  unreachable after reboot; restoring it recovered the panel boot messages and
  network reachability. Treat the precise coupling as unresolved and do not
  change the console command line without a serial-console recovery path.
- Service packaging includes a `sysusers.d` account definition and grants the
  runtime unit `video`, `input`, and `render` supplementary groups. Startup
  uses bounded display discovery and systemd restart-on-failure instead of the
  deprecated global udev-settle service.
- Fable-review follow-up: Debug and Release test suites passed on the Pi. The
  installed unit was started and stopped successfully as the generated
  `micropanel-touch` account with the required device-access groups.

### Remaining physical acceptance

- Record raw `evtest /dev/input/event0` values for each corner and the centre.
- Repeat discovery and the tap demo with HDMI physically connected.
- Repeat the same acceptance sequence when the second panel arrives.
- `tools/deploy.sh` intentionally requires SSH key/agent authentication; this
  initial bench deployment used an interactive password and has not changed
  the Pi's SSH configuration.
- The currently tested clone exposes no safe kernel backlight control. Do not
  enable a GPIO 22 backlight overlay automatically: its physical routing must
  first be verified on each supported panel model.

### Required evidence before closing Sprint 0

- Overlay/driver: `fbN` driver, resolution, bpp, SPI DRM card, and connector.
- Touch: `evtest` raw minimum/maximum and taps at all four corners plus center.
- Backlight: sysfs/LED/DRM/GPIO ownership evidence, including GPIO 22.
- Console: getty mask and cursor policy verified after restart.
- Deployment: warm `tools/deploy.sh` round trip, host tests, and panel tap demo.
- Second panel: repeat the above when the ordered unit arrives.
