# MicroPanel Touch handover — v3

**Prepared:** 2026-08-14
**Supersedes:** [`handover-note-v2.md`](handover-note-v2.md) as the current
restart point. Version 2 remains the historical record of the accepted
overlay-root and boot-console work.

## Current implementation state

- The current image profile pins the app commit named in
  `misc-tools/board-configs/micropanel-touch/hooks.txt`. The app includes the
  DHCP-Client, Static-address, and DHCP-server IP Settings modes, plus the
  physical-panel dropdown and button/keypad-spacing fixes.
- **System → Touch Calibration** is a five-target, resistive-panel rescue
  flow. It fits a residual axis-aligned X/Y correction, rejects inconsistent
  or implausible samples, applies a valid result immediately, and persists a
  versioned, geometry- and evdev-range-gated file at
  `/data/micropanel-touch/touch-calibration.conf`. A missing or incompatible
  file safely falls back to the bench default mapping. The initial panel run
  corrected the keypad accuracy; re-running the screen replaces a previous
  correction. Its two-tap **Reset default** control durably removes the file
  and restores the factory mapper immediately; SSH removal followed by a
  service restart remains break-glass recovery.
- The panel-profile seam now names the verified PiScreen ADS7846 portrait and
  landscape profiles plus the Luckfox 3.5-RPi-LCD-CTP ST7796S/GT911 portrait
  profile. A live probe confirmed the GT911 at I²C `0x5d` (ID `911`, firmware
  version `1060`) after unbinding the incompatible PiScreen node. Fresh-image
  acceptance is now complete: the `luckfox-ctp` image booted the ST7796S at
  `/dev/fb0`, loaded `panel_mipi_dbi` at boot, exposed the original
  `/sys/class/backlight/backlight_gpio/brightness`, registered GT911 Type-B
  multitouch, and kept the HMI and broker active with zero HMI restarts.
  `dtoverlay -h goodix` on that image confirms that `addr` is supported and
  defaults to `0x14`; this panel's required `addr=0x5d` is therefore explicit
  and correct. The default image remains PiScreen, while the isolated
  `luckfox-ctp` image variant supplies the vendor MIPI-DBI blob and Goodix boot
  configuration.
- DHCP server is an **eth0-only isolated provisioning mode**. A separate typed
  broker request validates a private server subnet and lease range, then calls
  a fixed-argv handler. It is never a generic root command.
- The image installs dnsmasq but masks its distribution unit. Only the
  dedicated, marker-gated MicroPanel service may run it. Its generated config
  has dynamic eth0 binding, no DNS listener, and explicitly suppresses DHCP
  router and DNS options. It does not provide forwarding or NAT.
- DHCP-server settings persist below root-owned, UI-group-readable `/data`.
  Leases intentionally remain below `/run`; connected clients rediscover after
  a panel reboot. Static-address and DHCP-client remove the marker and stop
  the dedicated service. They remain usable on existing/minimal installations
  because they stop that appliance unit only when it exists.
- DHCP-server mode currently requires a multi-address lease range. A
  single-address pool is not a supported v1 use case; add explicit validation
  only if that deployment becomes necessary.
- The current live-image baseline has been checked for active HMI/broker, no
  failed units, direct p3 `/data`, read-only boot firmware, an inactive
  DHCP-server service before enablement, and a masked generic dnsmasq service.

## Verification completed locally

- Typed broker validation, malformed-request rejection, asynchronous client,
  and real `CommandRunner` cancellation coverage pass.
- Headless UI coverage verifies the three mode labels, server defaults,
  confirmation flow, lease-start control alias, dropdown divider geometry, and
  distinct server-form button spacing.
- Handler policy coverage verifies router/DNS suppression, dynamic binding,
  volatile lease location, and the conditional appliance-service stop that
  preserves Tier-1 portability.
- Calibration coverage verifies the five-target solve, geometry/range gate,
  malformed/degenerate rejection, durable save/load, and the complete
  headless UI flow from System menu through an active result.

## Hardware acceptance still required

Do not enable DHCP Server on the normal LAN. It intentionally changes eth0's
address and terminates normal SSH/LAN access; a second DHCP authority would be
unsafe. Use a directly connected client or isolated switch/VLAN with no other
DHCP server.

1. Run static-address → reboot → verify `manual` profile → DHCP-client →
   reboot → verify `auto` profile through the UI broker.
2. Run the isolated DHCP-server procedure in the board
   `misc-tools/board-configs/micropanel-touch/BUILD.md`:
   lease within range, panel reachability, no default route/DNS option,
   reboot/re-discovery, then clean return to DHCP client.
3. Complete the calibration persistence and fail-safe sequence. The first
   panel session already confirmed improved keypad accuracy. Reboot and
   confirm `journalctl -u micropanel-touch` reports `Loaded persistent touch
   calibration`; then preserve the known-good file, temporarily change its
   `version` value, restart the service, and confirm it logs `Ignoring touch
   calibration` and uses the default mapping. Restore the preserved file and
   restart again before continuing normal use. Also verify that the second
   **Reset default** tap removes the saved file and restores the factory
   mapper; re-run the five targets before continuing normal use.

## Remaining Sprint 2.5 work

1. Complete and record the three hardware acceptance sequences above.
2. Luckfox 3.5-RPi-LCD-CTP acceptance is complete on its dedicated image
   variant: ST7796S framebuffer geometry, GT911 multitouch, portrait
   orientation, calibration flow, DHCP-client/static-IP sanity paths, and a
   clean boot all passed. Retain the expected `0x5d` Goodix address and
   `panel_mipi_dbi` module-load check in regression acceptance.
3. Basic Luckfox display sleep/wake acceptance passed: the fresh image blanks
   after roughly 60 seconds and wakes on touch; service state, the then-current
   successful permission unit, `root:micropanel-touch 0660` ownership, and restored
   brightness `1` were independently verified. **Display → Standby**
   now supplies a staged, persistent 10–180-second user timeout and enable
   checkbox. The next Luckfox image revision deliberately moves its backlight
   to kernel PWM so **Display → Brightness** can persist a 5–100% setting;
   confirm the resulting `backlight_pwm` path, physical dimming, wake restore,
   udev-applied `root:micropanel-touch 0660` ownership, and the explicitly
   disabled analogue-audio disposition on hardware. Still run the
   long-action sleep-inhibit acceptance and record power measurements. PiScreen
   remains intentionally `nullopt` until its kernel-exported control path is
   verified.
4. Continue write-path inventory and later power-cut testing. Service sandbox
   tightening (`ProtectSystem`, write-path and capability restrictions) remains
   part of the Sprint 6 release-hardening pass.
