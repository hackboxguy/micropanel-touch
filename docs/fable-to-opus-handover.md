# Fable → Opus handover: the base-features milestone

**Prepared:** 2026-08-20 (fable, from owner decisions of the same date)
**Audience:** a fresh implementation session. This note is the kickoff: it
records the owner's priority pivot and decisions, and lays out the work
order. The plan and PRD remain the authoritative documents — folding these
decisions into them is this session's first task, not optional.

**Read, in order:** this note →
[`handover-note.md`](handover-note.md) (current state; `00.39` stable base) →
[`pi-in-system-update-plan.md`](pi-in-system-update-plan.md) §7–§9 →
[`micropanel-touch-prd.md`](micropanel-touch-prd.md) §6.6/§6.7/§7.1 →
[`build-release-update-commands.md`](build-release-update-commands.md) →
`misc-tools/packages/pi-ab-update/README.md`.

Bench Pi + credentials come from the active session, never this repo. The
card runs the published `00.39` from the live GitHub chain (slot A), with
`00.36` on slot B as rollback. **Identifiers `00.23`–`00.39` are burned;
version numbering starts at `00.40`.**

## The owner's priority pivot (record in plan §0/§2 + a PRD note first)

**Legacy-config parity — Sprint 4's compatibility half — is deferred.**
micropanel-touch's next milestone is a **polished standalone lab tool** with
refined base system features. The config contract, the parity machinery, and
the 14 pinned legacy configs all remain in place as the seam for a later
milestone; nothing is deleted, but nothing new is built against it now.

**Images stay fragmented per use case.** No jumbo image: the base lab tool
now; fpga/mcu-flash, camera/gstreamer monitoring, serial-terminal, media and
IT-diagnostic packs later as image-level pack stacks per PRD §6.7 — each
only if it fits the 2 GiB ceilings below. Keep the number of image flavors
small: every flavor is its own release with its own bench acceptance.

## Owner decisions, recorded 2026-08-20

1. **WiFi credentials:** joining a hotspot stores the credential as a
   NetworkManager keyfile on `/data` (root-owned, `0600`) —
   plaintext-equivalent at rest, accepted for this lab tool, recoverable by
   factory reset. Record in PRD risk 7. **Encrypted-at-rest credentials are
   deliberately deferred to the CM4/eMMC/secure-boot milestone** (plan §11),
   where a hardware-backed secret store makes it worth doing properly.
2. **iperf topology:** the panel implements **both roles** — iperf3 server
   mode and client mode (TCP bandwidth test; bounded-duration UDP flood).
   The owner will prepare two micropanel-touch setups with one configured as
   server, and a plain Pi 4 as the cable-linked iperf client peer for
   acceptance. Flood and server modes get the DHCP-server-style
   disruptive-action treatment: double-confirm, clearly worded, safe wording
   about shared LANs.
3. **Power controls:** both **reboot and shutdown**, each behind a confirm
   dialog, as typed broker operations.
4. **Milestone gate ("base 1.0"):** WiFi join, System Stats, About/version,
   reboot/shutdown, and the iperf bandwidth test gate the next stable
   release. Flood test and hostname display are fast-follows, not gates.

## Work order

Each slice lands with tests and its bench acceptance recorded in the plan
before the next starts.

### 0. Record the decisions above

Plan §0/§2 and the PRD note. Ten minutes; do it before code so the
authoritative documents never trail this note.

### 1. Image diet, measured

The bundle is 1.46 GiB against **two independent 2 GiB ceilings** (GitHub
release asset; the `MP_FACTORY` partition) — 73 % of both, and suspiciously
heavy for a Pi OS Lite appliance. Inventory the built image's rootfs on the
build host (`du -x` depth 2 + `dpkg-query -W --showformat` installed-size
ranking), trim the top offenders (apt caches/lists, docs/locales, firmware
packages nothing uses, leftovers), and record before/after rootfs and bundle
sizes in BUILD.md. Expected landing zone: a base bundle around 0.7–1.0 GiB,
restoring roughly half of both ceilings.

**Then answer plan §7's open question** — whether the 2 GiB `MP_FACTORY`
reservation stands — with the measured numbers, and record it. (With a
dieted base, keeping 2 GiB is expected to be generous; the factory payload
is the same signed bundle artifact.)

### 2. Landscape carry-over

Close the long-pending landscape bucket before the feature fan-out: the
480×320 boot profile's reflow, its bench-verified touch mapping, and the
no-scroll assertions running in both geometries. New base-feature screens
then land on both orientations instead of being retrofitted.

### 3. Base features, in slices

Use the established patterns throughout: typed broker operation where
privileged (client supplies an enum or nothing — never a path, URL, or
command), Tier-1 handler, UI card, headless test, bench acceptance. Every
new screen obeys the redraw law and stays inside the tested no-scroll grids
(raise `rows` or regroup if a menu grows).

- **(a) WiFi hotspot join** — the existing scan and password-keyboard
  screens graduate from mock to real: select SSID → keyboard → typed broker
  op → NM keyfile on `/data` per decision 1 → result card. The secret must
  never appear in logs, events, broker replies, or control captures — the
  existing redaction tests must be extended to cover the *real* path, not
  just the demo screen. Include forget/disconnect.
- **(b) System Stats, wired for real** — CPU load, CPU temperature, memory,
  uptime, live at the 2–4 Hz refresh discipline with change-guarded labels;
  un-hide the existing entries.
- **(c) About/version screen** — running version, slot, app revision,
  update state; read from the same published files `ab-update status`
  reads. Un-hide/replace the hidden System Stats placeholder as needed.
- **(d) Reboot and shutdown** — typed broker ops, confirm dialog each.
- **(e) iperf3 diagnostics** — client bandwidth test, bounded-duration UDP
  flood, and server mode per decision 2, with the disruptive-action
  double-confirm. Results through the existing progress/result-card flow.

### 4. Release

When the gate features (decision 4) are accepted: publish the next stable
release through the documented flow, boot-tested from the published asset —
the `00.39` precedent is the bar.

## Working rules (unchanged, restated)

- Commit to `main`, do not push; the owner pushes and publishes releases.
- Both test gates before any image build: the engine's `run-tests.sh` and
  the app's ctest suite on the build host.
- Every published payload gets one bench boot acceptance before release;
  every slice's acceptance is recorded in the plan the same day.
- The session's **last commit is the handover note update**.
- One caveat the fixtures cannot cover: this milestone is *polish*, and the
  polish half is judged on the physical panel. Plan for the owner's eyes on
  the screen at each slice's exit, as in the early sprints.
