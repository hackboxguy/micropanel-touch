# micropanel-touch

`micropanel-touch` is the LVGL/direct-evdev replacement for the OLED
micropanel UI. The product is ultimately a read-only, flash-and-go Pi OS image;
the application is developed and tested independently first.

## Sprint 0 build

LVGL is a pinned submodule. Initialise it before configuring:

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The initial binary has two useful hardware modes:

```sh
build/micropanel-touch --probe
build/micropanel-touch --run-seconds 20
build/micropanel-touch --theme high-contrast --run-seconds 20
```

`screens/config-basic.json` selects the startup skin with its optional
`"theme"` key (`dark`, `light`, or `high-contrast`). `--theme` overrides that
choice and also accepts a JSON skin path. A malformed or missing requested skin
falls back to the packaged dark skin; live selection is available at
Display → Theme.

`--probe` reports the stable DRM-by-path → connector → framebuffer mapping,
kernel-exported backlight/LED candidates, and ADS7846-compatible touch devices.
It deliberately does not claim that a write-only SPI panel can be physically
hot-plug detected.

## Touch calibration

The shipped ADS7846 mapping is the default. If a clone has a small residual
offset (for example, a keypad `.` lands on a neighbouring key), use
**System → Touch Calibration** and tap the centres of its five numbered
targets. The app fits separate X/Y corrections, rejects inconsistent samples,
applies a successful result immediately, and stores it atomically at
`/data/micropanel-touch/touch-calibration.conf`. The file is ignored if its
version, panel geometry, or reported driver range does not match at a later
boot. Re-running the screen replaces a previous correction. **Reset default**
uses a second confirmation tap, removes the saved correction durably, and
restores the factory mapping immediately. SSH removal of the file followed by
a service restart remains the break-glass recovery route.

Calibration is deliberately a rescue path, not a first-boot requirement; it
does not replace the orientation transforms supplied by the device-tree
overlay.

## Network-settings broker client

IP Settings offers **DHCP-Client**, **Static-Address**, and **DHCP-Server**.
Static exposes IP address, Gateway, and dotted Netmask fields. DHCP Server is
an appliance-only, eth0-bound isolated-link mode with server IP, Netmask, and
lease-range fields; it defaults to `192.168.50.1/24` and does not provide
routing, NAT, or DNS. Its first Apply asks for a second confirmation because
it disconnects eth0 from the normal LAN. Local validation remains the default
and cannot change networking. For an explicitly provisioned root-owned broker,
opt in at app startup with an absolute socket path:

```sh
build/micropanel-touch --privileged-broker-socket /run/micropanel-touch/broker.sock \
    --static-ip-interface eth0
```

This flag only enables the non-root asynchronous client and its result card;
it does not start a broker or install a service. The root-side broker must be
provisioned separately. Applying any mode can interrupt SSH; DHCP Server must
be tested only with a directly connected client or an isolated network that
has no other DHCP authority. Its lease database is deliberately volatile: a
client re-discovers a lease after a panel reboot rather than relying on lease
continuity.

Production-image installs configure the HMI, broker, and conditionally-active
DHCP-server service together with
`-DINSTALL_SYSTEMD_SERVICE=ON`: the UI runs as `micropanel-touch`, while the
root broker owns `/run/micropanel-touch/broker.sock` and authorizes only that
account. The image supplies `dnsmasq`, masks its generic distro service, and
lets only the broker-managed eth0 unit start it. The UI receives explicit
persistent, fallback, and runtime roots from its unit. If the image's `data`
partition cannot be mounted, it keeps running with action state in its private
volatile runtime directory instead of writing to the read-only root filesystem.

For the Pi 4 bench loop, see [Sprint 0 hardware checklist](docs/sprint-0-hardware-checklist.md).
The deploy and bootstrap scripts contain no credentials; use SSH key/agent auth
or an interactive password prompt outside the repository.
