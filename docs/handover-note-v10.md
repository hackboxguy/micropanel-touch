# MicroPanel Touch handover — v10

**Prepared:** 2026-08-18 (opus)
**Supersedes:** [`handover-note-v9.md`](handover-note-v9.md) as the current
restart point. V9 remains the record of the Stage 2 completion state and the
approved Stage 2b work order.

This note is written for a fresh engineering session. Read, in order:
this note → [`pi-in-system-update-plan.md`](pi-in-system-update-plan.md)
(§6 for the format=2 bundle, §8 Stage 2b, its **bench acceptance record** and
the implementation record beneath it) →
[`fable-ota-usb-simplification-proposal.md`](fable-ota-usb-simplification-proposal.md)
(the approved design this implements) →
`misc-tools/board-configs/micropanel-touch/BUILD.md` (build/flash/signing/
acceptance procedures). [`micropanel-touch-plan.md`](micropanel-touch-plan.md)
§0 is the wider sprint context; the A/B work is still the active track.

## Where the work stands

**Stage 2b is complete and hardware-accepted (2026-08-19).** All nine
acceptance items passed on the Pi 4 + Luckfox CTP fixture with a fresh
`00.25`/`00.26` version pair. The full record, including three findings from
the session, is in plan §8 "Stage 2b bench acceptance".

- **Stages 0–2b:** complete and hardware-accepted (records in plan §8).
- **Bench state at handover:** committed **slot B running `00.26`**, durable
  state `state=fallback  candidate_slot=A  version=00.25` — the deliberate
  post-arm/pre-commit power-cut result, left in place as evidence. Slot A holds
  a complete, labelled `00.25` that simply is not selected. The attached stick
  is **exFAT** with the `00.25` bundle on it.
- **Outstanding:** two defects were fixed *after* the hardware runs (unguarded
  discovery mounts publishing the generic `failed-internal`, and
  `failed-internal` being undiagnosable). Per the project rule, **the next
  build must re-check the source-refusal path on the bench** before its payload
  is published. Nothing else in the acceptance depends on those changes.
- **Next stage:** Stage 3 (factory reset), then Stage 4 (OTA + signature
  verification as one stage).

## Burned version identifiers

`00.23`, `00.24`, `00.25` and `00.26` are all burned bench identifiers. The
next build must advance past them.

## Release signing key — the one irreversible thing here

The build host now holds a real ed25519 release key at
`/etc/micropanel-touch/release-signing/ed25519-release.key` (root, `0600`),
created automatically by the first A/B build. Its public half is baked into
`00.25` and `00.26` and every image after them.

**It has not been backed up.** Losing it means no already-flashed device will
accept a future signed release once Stage 4 lands, and recovery is a reflash.
Back it up offline before publishing anything else. Custody rules are in
BUILD.md "Release signing key custody".

## Next actions, in order

1. **Fix-forward build.** The two post-acceptance handler fixes need a build
   and a bench re-check of the source-refusal path (tap Check USB stick with no
   stick attached, and with a stick holding no bundle — both must report their
   specific message, not "the update stopped safely before candidate boot").
   Use a version past `00.26`.

2. **Stage 3 — factory reset** (plan §7/§8).

3. **Stage 4 — OTA + signature verification as one stage.** Most of the
   groundwork is already on the device: the pinned public key at
   `/usr/lib/micropanel-touch/update-signing-key.pub`, the reserved
   `/usr/lib/micropanel-touch/update-source.conf` holding version-less
   `MANIFEST_URL`/`BUNDLE_URL`, a reader that is already pipe-capable
   (`curl | handler`, proven by the `stdin` source in the loopback fixture),
   and every published bundle already carrying `manifest.sig`. Stage 4 is
   source plumbing plus turning verification on.

## Build workflow consequence to remember

Published asset names are **version-less** by design, so two releases cannot
share a payload directory — the second silently replaces the first. Always pass
`--payload-dir=<per-version directory>`; the releases used here live in
`~/pi-image-workspace/releases/00.25/` and `releases/00.26/`.

Also: the apps `.stamp` is per output directory, not per version, so building
version N+1 invalidates version N's cache. Rebuilding an earlier version does a
full apps stage rather than reusing it.

## Bench-procedure cautions that stay in force

- When interrupting a bench update, kill processes **by exact PID** — a
  `pkill -f` pattern can match your own SSH command line and kill your session.
- Every published payload gets one bench boot acceptance before release; every
  stage ends with its acceptance recorded in the plan the same day.
- Attended-update posture: pre-PID-1 candidate failure is recovered by one
  manual power-cycle (owner-accepted, bench-proven); the UI states an
  open-ended recovery condition, deliberately without a numeric window.
- The unsigned-payload residual is still the dominant product risk until
  Stage 4. Stage 2b signs on the build side only; **device-side enforcement is
  Stage 4** and physical USB possession remains update authority today.
- Reflashing changes SSH host keys; refresh only the temporary known-hosts
  entry after confirming a reflash. Panel variants remain separate images;
  never runtime-switch boot profiles. Do not reuse bench version identifiers.

## Things a fresh session will want to know

- The build host used for this work **cannot compile the application**:
  `libgpiod` and `nlohmann-json` are not installed and `external/lvgl` is an
  empty submodule. The C++ changes were syntax-checked per translation unit;
  the real compile happens inside the image build, so a build failure there is
  the first thing to check if `00.25` does not come out.
- `sudo tests/test_system_update_handler_integration.sh` and
  `misc-tools/.../test_ab_layout_integration.sh` both need root and loop
  devices, and both leave nothing behind — but if a run is interrupted, check
  `losetup -a` before rerunning.
- The exFAT case in the device-side loopback fixture skips itself when the host
  has no `mkfs.exfat`. On a host with `exfatprogs` it runs, and it is worth
  running once there in addition to the bench.
