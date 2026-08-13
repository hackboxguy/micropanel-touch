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

## Persistence implementation awaiting hardware acceptance

The previously accepted image has a third ext4 partition labelled
`MICROPANEL_DATA`, but its plain `overlayroot=tmpfs` argument recursively
overlayed `/data`; its visible writes were volatile. The follow-up image
configuration changes that argument to `overlayroot=tmpfs:recurse=0`, so only
the root is overlaid and `/data` is expected to be the direct `p3` ext4 mount.
The overlay-sensitive root-maintenance units recognise both command-line forms
so this change does not reintroduce boot warnings.

The same follow-up keeps NetworkManager connection keyfiles on `p3` and bind
mounts them at `/etc/NetworkManager/system-connections` before NetworkManager
starts. The application now inspects its configured data filesystem at startup
and rejects overlay/tmpfs storage in favour of its explicit runtime fallback,
so an incorrectly assembled image cannot silently claim durable application
storage.

SSH host keys now have a separate `/data` seed store: a service creates the
seed once and restores it into the volatile root before SSH starts. This is
intended to prevent host-key churn, but it must be verified after flashing the
new image. `machine-id` is deliberately not claimed persistent: it is consumed
too early in boot for a late `/data` service to be a correct solution. Record
its behaviour across the same two boots and design its early-boot disposition
if it changes.

None of these changes completes the Sprint 2.5 persistence requirement until
a fresh card proves write → reboot → power-cycle survival on the bench Pi.

## Remaining Sprint 2.5 work

1. Flash the follow-up image and prove `/data`, application state, and an
   NetworkManager profile survive reboot and power-cycle; record SSH host-key
   and machine-id results.
2. Run the approved real network-mutation test through the broker.
3. Implement and validate the panel-profile/capacitive-panel seam.
4. Implement display sleep/wake using a verified backlight control path.
5. Begin the remaining write-path inventory and later power-cut test suite;
   release access hardening remains a Sprint 6 requirement.

For image build and current bench verification commands, see
`misc-tools/board-configs/micropanel-touch/BUILD.md`.
