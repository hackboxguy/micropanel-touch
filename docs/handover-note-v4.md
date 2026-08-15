# MicroPanel Touch handover — v4

**Prepared:** 2026-08-15  
**Supersedes:** [`handover-note-v3.md`](handover-note-v3.md) as the current
restart point. V3 remains the historical record of the initial image,
networking, calibration, and panel-variant work.

## Exact repository state

At handover, both working trees are clean apart from this new handover file.
The following commits are local on `main` and must be pushed before an image
build that is intended to include them:

| Repository | Commit | Purpose |
|---|---|---|
| `micropanel-touch` | `ecd5dc4e0bdd854f90cbd41dc0548b1cd7d7d72e` | Unifies the Wi-Fi/PIN reveal control and adds eyes to New PIN, Confirm PIN, unlock PIN, and the current PIN used to disable screen lock. |
| `misc-tools` | `880cd44` | Pins both default and `luckfox-ctp` hooks to `ecd5dc4`; records the display-power capability matrix and precise PiScreen GPIO wording. |

The `misc-tools/board-configs/micropanel-touch/hooks.txt` and
`hooks-luckfox-ctp.txt` pins must always move with the app commit. Do not
reset or rewrite history: the user pushes these local `main` commits.

Local verification was run from `/tmp/micropanel-touch-v15-build`:

```sh
cmake --build /tmp/micropanel-touch-v15-build -j4
ctest --test-dir /tmp/micropanel-touch-v15-build --output-on-failure
```

All **41/41** tests passed. The Unix-domain-socket tests require execution
outside Codex's filesystem sandbox; this is a sandbox restriction, not a
test failure.

## Accepted panel behavior

### Default resistive PiScreen (ILI9486 + ADS7846)

- The normal board image uses only:

  ```ini
  dtoverlay=piscreen,drm=1,rotate=90,xohms=100,swapxy=1
  ```

  It must **not** add a GPIO-22 `gpio-led` overlay. The prior additive
  overlay caused the ILI9486 probe to fail and produced a white screen.
- GPIO 22 belongs to the panel overlay's pin group. A watched, three-second
  `gpioset ... 22=0` bench probe on 2026-08-15 produced **no visible
  backlight change**. This accepted clone is fixed-on.
- Consequently **Display → Standby** and **Display → Brightness** are
  unavailable on this panel. Do not add raw GPIO, software-PWM, or a second
  overlay. DRM blanking is explicitly deferred and needs power measurements
  before reconsideration.
- Screen Lock is functional manually. Its automatic lock is deliberately
  coupled to a successful standby transition, so it cannot auto-lock on this
  fixed-on panel. Whether to add a distinct, display-on idle-lock timer is a
  pending product decision; do not silently change that semantic.
- `System → Touch Calibration` remains the five-target resistive rescue flow;
  Reset default is a two-tap durable reset.

### Luckfox `--variant=luckfox-ctp` (ST7796S + GT911)

- This is a separate image variant, never a runtime profile switch. The
  PiScreen and Luckfox boot profiles have mutually exclusive SPI/GPIO claims.
- It uses the vendor-pinned MIPI-DBI command sequence, Goodix GT911 at I2C
  `0x5d`, and the kernel PWM backlight
  `/sys/class/backlight/backlight_pwm/brightness` on GPIO 18.
- Luckfox alone supports persistent 10–180-second standby and live 5–100%
  brightness. Its managed profile disables competing analogue audio and
  applies the narrow udev permission at backlight-node creation.

`misc-tools/board-configs/micropanel-touch/capability-matrix.csv` is now the
machine-readable record of both panels' standby/brightness status.

## Reliability and security work completed

- The image has RO root and RO `/boot/firmware`, with persistent state on the
  separate `/data` ext4 partition. NetworkManager profiles, host keys, screen
  lock, calibration, and HMI settings use the documented data paths.
- Early boot restores a device-specific machine ID from
  `/data/micropanel-touch-system/machine-id` before consumers start; it then
  restarts journald in the correct ordering. A post-power-cycle live check
  confirmed runtime ID equals persisted ID, HMI and privileged broker are
  active, and HMI restart count is zero.
- Screen-lock PINs are 4–10 digits; storage uses PBKDF2-HMAC-SHA-256 with a
  random salt and an atomic, restricted file. Failed-unlock attempts have a
  session-memory exponential delay limiter. PINs are always redacted from the
  development control interface, including while the user has tapped an eye
  to reveal them visually.
- Backlight write failures use a three-failure circuit breaker: the third
  failure logs once, retries are suppressed, and input activity or applying
  standby settings re-arms it. This prevents 1 Hz journal flooding.
- User hardware reports already confirmed that static IPv4 and DHCP-client
  mode survive reboot and power-cycle on the resistive image. The isolated,
  eth0-only DHCP-server mode remains a destructive network test and must be
  exercised only on an isolated link/VLAN; it changes eth0 addressing and
  can sever normal SSH access.

## Last live Pi evidence

The bench Pi is the user-managed Pi 4 at `192.168.1.124`; use credentials
provided in the active session, not this repository. Reflashing deliberately
changes the SSH host key, so refresh only the temporary known-host entry after
confirming a reflash occurred.

The most recently inspected flashed manifest was
`befb88dfe0a9f1aea4c3ccfcfadbd1bc4ee2689a`, before the later PIN-eye and V17
commits. It showed the PiScreen overlay, ILI9486 `/dev/fb0`, ADS7846 input,
`/data` on the ext4 data partition, active HMI and privileged broker, zero
HMI restarts, no failed units, and the expected overlay-root/RO-boot policy.
Build and flash the current pins above before treating the new eye controls as
hardware accepted.

## Sprint 2.5 status

There are no remaining Sprint 2.5 implementation questions. Remaining bench
acceptance evidence is:

1. On the resistive panel after flashing `ecd5dc4`: screen-lock setup/unlock/
   disable eye controls and attempt limiter; calibration reset/recalibration
   persistence and malformed-file fallback.
2. Network sequence on a safe test link: static → reboot → DHCP-client →
   reboot, plus the isolated DHCP-server procedure in the board `BUILD.md`.
3. On Luckfox: long-action standby inhibit/wake and awake/sleep power
   measurements. These are Luckfox-only because PiScreen is fixed-on.
4. Record the Sprint 2.5 exit demo on both supported panel models.

## Next planned engineering work — A/B Stage 0

The owner-approved next work item is the in-system A/B update foundation.
The authoritative plan is
[`pi-in-system-update-plan.md`](pi-in-system-update-plan.md), added in commit
`90745c7`. **Stage 0 is a bench spike with no repository changes.**

It requires a **spare 16 GB or larger SD card**. Do not overwrite the current
known-good card. Hand-partition the spare per plan §4, put the current image
in slot A, clone it into slot B with a marker change, and prove on the Pi 4:

1. ordinary boot selects A;
2. `reboot "0 tryboot"` selects B exactly once;
3. rewriting the slot selector commits B as the default;
4. a deliberately broken B plus the watchdog automatically falls back to A.

Record exact commands, firmware behavior, boot logs, and timings in the A/B
plan. Do not start Stage 1 or edit `misc-tools` until all four pass. Pi 3 and
Pi 5 are later hardware checks; the Stage 0 Pi 3 result decides whether the
future selector stays `tryboot.txt` across models or drops Pi 3 in favor of
the Pi 4/5/CM4 `autoboot.txt` backend already pre-approved by the owner.

Stage 1, only after this spike, is the breaking A/B partition-layout and
watchdog scaffold behind an explicit build flag. The current normal
single-slot build must remain usable during that transition.

## Later engineering work — Tier-2 packs

All future app-style functionality (FPGA/MCU/ESP32 flashing, CAN/UART
debug, Domoticz buttons/status, audio/media playback) is added as Tier-2
packs per the owner-approved
[`micropanel-touch-pack-spec.md`](micropanel-touch-pack-spec.md) (format
v1). Its **§10 core-enabler table is the gate**: the fragment loader,
`status`/`refresh_sec` item types, pack handler resolution, build-time
closure check, and pack event socket are core work that must land before
the first pack builds. Per the owner's sequencing decision, that work —
and the menu-function fan-out generally — resumes **after** A/B Stage 1;
the recommended first pack is `fpga-flash` (Sprint 5's flash-rehearsal
item), with an audio pack as the natural second. The spec also records
the media/DSI forward path (§13: SPI display handover vs DSI DRM plane
lease, and the Pi 5 no-H.264-hardware caveat).

## Operating constraints for the next session

- Commit local changes to the respective repository's `main`; do not push.
  The user performs pushes and image builds.
- Preserve unrelated work in dirty trees. Do not use `git reset --hard` or
  overwrite the active boot card without explicit confirmation.
- Resistive PiScreen and Luckfox are separate image variants. Never attempt
  live swapping of their boot profile on a read-only appliance image.
- Treat network/DHCP-server and A/B card work as potentially disruptive;
  confirm exact hardware/card scope before any destructive test.
