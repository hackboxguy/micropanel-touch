# MicroPanel Touch handover — v1

**Prepared:** 2026-08-11  
**Repository state:** `main` at `94df1c1` (`Harden privileged broker response timeout`) before this handover-note commit.

This note is a restart point for the next implementation session. It records
the accepted work and the important boundaries, not credentials or other
secrets.

## Current product state

- Sprint 0 bench enablement is complete on the Pi 4 with the 3.5-inch
  resistive PiScreen panel. Preserve `console=tty1` in the Pi boot cmdline:
  removing it previously left the bench board unreachable after boot.
- The starter Network / Display / System navigation is implemented and
  accepted on the panel. Network Info is live; Wi-Fi scanning is asynchronous
  and accurately shows that the current bench `wlan0` radio is unavailable.
- Network → IP Settings is accepted. It starts in DHCP mode; Static mode shows
  IP address, Gateway, and dotted Netmask fields, with editable defaults
  `192.168.1.1`, `192.168.1.1`, and `255.255.255.0`. The large two-row mode
  chooser has an explicit divider and is usable with a finger.
- The password-demo keyboard, eye reveal control, blinking focus cursor,
  repeat Backspace, menus, icons, themes, sliders, progress demo, and Action
  Runner demo have all been exercised and accepted on the panel. The Action
  Runner demo was tested both to completion and through cancellation.
- Portrait is the accepted shipping orientation. Runtime LVGL rotation was
  visibly slow on this SPI panel; use the boot overlay/profile orientation,
  not runtime rotation, for normal operation. See the PRD orientation section
  for the verified touch mappings.

## Network mutation boundary

The code has a complete but deliberately **opt-in** network-write path:

```text
Starter UI -> NetworkApplyService worker -> typed AF_UNIX broker
           -> root-owned fixed-argv handler -> NetworkManager
```

- The only allowed typed operations are `apply_static_ipv4` and `apply_dhcp`.
  The broker checks `SO_PEERCRED`, rejects unknown JSON fields, revalidates
  input, and never accepts an executable or argv from a client.
- `micropanel-touch-privileged` and both network handlers build and install,
  but no broker systemd unit is installed or enabled on the bench image yet.
- The UI only uses a broker when launched with an absolute
  `--privileged-broker-socket`; normal IP Settings is validation-only.
- The panel acceptance test intentionally used a nonexistent socket, proving
  a clear error card and working Back behavior without changing networking.
  **A real DHCP/static change has not been approved or tested on the live
  bench connection.** Do not start a root broker or apply an address without
  explicit confirmation of interface, values, and recovery path.
- Commit `94df1c1` fixes the prior P1 timeout mismatch: handlers have a
  named 45-second ceiling and clients wait 60 seconds for the terminal broker
  reply. The protocol remains one terminal reply (no client cancellation), so
  shutdown can wait for that bounded interval. A six-second delayed-handler
  regression test proves this behavior.

## Validation status

- Local WSL native builds work after the project dependencies were installed.
  The sandbox cannot run the AF_UNIX listener tests, so use the Pi for the
  complete suite.
- After `94df1c1`, the Pi ran all **27/27 CTest tests successfully**. This
  includes the socket broker, network-apply service, headless UI, handler
  contracts, and the new slow broker-response test.
- The Pi source tree and build directory contain `94df1c1`. The currently
  displayed app need not be restarted for this non-visual broker hardening;
  restart it when beginning a new panel test.

Useful commands (authenticate outside this repository; do not store a
password in scripts or docs):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

For the bench loop, copy the source without a destructive synchronization,
build natively on the Pi, run CTest there, then restart only the known test-app
PID. Avoid `tools/deploy.sh` for this interactive loop because it currently
uses `rsync --delete`.

## Fable review v8 disposition

| Finding | Status |
|---|---|
| P1 client response timeout shorter than valid handler run | Fixed in `94df1c1`, with regression coverage. |
| Root handler discovery based on `argv[0]` | Fixed in `94df1c1`; uses `/proc/self/exe`. |
| NetworkManager polkit can bypass broker for an unprivileged account | Accepted as an image-level requirement. PRD risk 11 and Sprint 2.5 require an appliance account outside `netdev`, a root-only NetworkManager polkit rule, and a capability-matrix entry. |
| ControlServer shutdown and simulated-handler argv check | Already implemented before v8 review. |
| Physical keypad and landscape password-finger evidence | Still open capability-gate evidence; no regression reported. |

## Recommended next work

The plan intentionally moves to **Sprint 2.5: infrastructure consolidation**
before scaling out menus. Begin by inspecting the sibling `misc-tools`
repository and its board-config conventions, then implement the
`micropanel-touch` board configuration and image hook. The first vertical
slice should establish:

1. a dependency-complete Pi OS Lite image that installs this project,
   handlers, screens, sysusers, and the UI/broker service units;
2. the image-level broker/polkit posture above;
3. the panel-profile seam for resistive portrait/landscape and the capacitive
   panel variant;
4. the minimal read-only-root plus writable-data layout; and
5. display sleep/wake through the selected profile's backlight path.

Consult `docs/micropanel-touch-plan.md` Sprint 2.5 and
`docs/micropanel-touch-prd.md` before making image or privilege changes. They
contain the required acceptance demonstrations and are the source of truth
for the sequencing.
