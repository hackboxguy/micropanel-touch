# MicroPanel Touch handover — v2

**Prepared:** 2026-08-13
**Supersedes:** [`handover-note-v1.md`](handover-note-v1.md) as the current
implementation restart point. The v1 note remains the historical record of
the pre-image state.

## Validated appliance-image slice

- `misc-tools/build-image.sh --board=micropanel-touch` builds a pinned Pi OS
  Lite appliance image with the PiScreen portrait profile, the HMI, the privileged broker,
  package-owned handlers, sysusers, the NetworkManager polkit rule, and both
  enabled systemd units.
- On the bench Pi 4 with the 3.5-inch resistive panel, both
  `micropanel-touch.service` and `micropanel-touch-privileged.service` are
  active. The broker socket is owned by `micropanel-touch` and has mode
  `0600`.
- The boot console and the HMI share the PiScreen framebuffer. The accepted
  image keeps the required `console=tty1` but prevents boot-status output,
  cloud-init SSH-key disclosure, and sdm first-boot output from repainting the
  panel. The menu remains visible after boot and normal touch navigation works.
- Latest bench timings: HMI started at roughly 12 seconds; userspace completed
  at roughly 18 seconds. The accepted boot had no failed systemd units.
- Real static-IP/DHCP mutation remains untested by design. Do not test it
  until the target interface, replacement values, and recovery method are
  explicitly agreed.

## Persistence blocker

The image correctly has a third ext4 partition labelled `MICROPANEL_DATA`, but
the current `overlayroot=tmpfs` configuration recursively overlays the visible
`/data` path. Application writes to `/data` therefore go to tmpfs rather than
the ext4 partition and do not yet survive a reboot.

Do not describe the Sprint 2.5 persistent-data requirement as complete. The
next implementation task is to expose the data partition as a persistent
mount after overlayroot is active, then prove persistence across reboot and
power-cycle tests.

## Remaining Sprint 2.5 work

1. Correct the `/data` mount layout and add a persistence acceptance check.
2. Run the approved real network-mutation test through the broker.
3. Implement and validate the panel-profile/capacitive-panel seam.
4. Implement display sleep/wake using a verified backlight control path.
5. Begin the write-path inventory and later power-cut test suite; release
   access hardening remains a Sprint 6 requirement.

For image build and current bench verification commands, see
`misc-tools/board-configs/micropanel-touch/BUILD.md`.
