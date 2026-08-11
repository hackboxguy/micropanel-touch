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

## Network-settings broker client

IP Settings starts in DHCP mode. Switching to Static exposes IP address,
Gateway, and dotted Netmask fields; local validation remains the default and
cannot change networking. For an explicitly provisioned root-owned broker, opt
in at app startup with an absolute socket path:

```sh
build/micropanel-touch --privileged-broker-socket /run/micropanel-touch/broker.sock \
    --static-ip-interface eth0
```

This flag only enables the non-root asynchronous client and its result card;
it does not start a broker or install a service. The root-side broker must be
provisioned separately. Applying DHCP or static settings can interrupt SSH if
the selected connection is in use.

For the Pi 4 bench loop, see [Sprint 0 hardware checklist](docs/sprint-0-hardware-checklist.md).
The deploy and bootstrap scripts contain no credentials; use SSH key/agent auth
or an interactive password prompt outside the repository.
