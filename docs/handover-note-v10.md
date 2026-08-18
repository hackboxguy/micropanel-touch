# MicroPanel Touch handover — v10

**Prepared:** 2026-08-18 (opus)
**Supersedes:** [`handover-note-v9.md`](handover-note-v9.md) as the current
restart point. V9 remains the record of the Stage 2 completion state and the
approved Stage 2b work order.

This note is written for a fresh engineering session. Read, in order:
this note → [`pi-in-system-update-plan.md`](pi-in-system-update-plan.md)
(§6 for the format=2 bundle, §8 Stage 2b and its **implementation record**) →
[`fable-ota-usb-simplification-proposal.md`](fable-ota-usb-simplification-proposal.md)
(the approved design this implements) →
`misc-tools/board-configs/micropanel-touch/BUILD.md` (build/flash/signing/
acceptance procedures). [`micropanel-touch-plan.md`](micropanel-touch-plan.md)
§0 is the wider sprint context; the A/B work is still the active track.

## Where the work stands

**Stage 2b is code complete and has had no hardware exposure yet.** Both
repositories are committed on `main` and not pushed. Everything below is
verified on the build host only.

- **Stages 0–2:** complete and hardware-accepted (records in plan §8).
- **Stage 2b:** implemented in full — format=2 `.mpupdate` bundle generator
  with build-side ed25519 signing, single-pass pipe-capable device reader,
  zero-preparation USB discovery, broker source enum, UI text, the whole
  OTA-forward groundwork checklist, and the five folded-in v5 minors. The
  detailed record, including one deliberate deviation from the proposal and
  one pre-existing defect found and fixed, is plan §8 "Stage 2b implementation
  record".
- **Outstanding:** the Stage 2b bench acceptance list (plan §8). It is the
  only thing between this state and Stage 3.

## The bench is not on this code yet

The Pi 4 + Luckfox CTP fixture still runs the **`00.24` format=1 image**:
committed slot A from `/dev/mmcblk0p5`, `state=committed`,
`candidate_slot=A`, app revision `07b261e6…`. Its attached USB device is a
232.9 GB USB disk with a FAT32 filesystem still labelled `MP_UPDATE` and
carrying the old `00.24` triplet.

That image **cannot be updated by a Stage 2b bundle** and a Stage 2b image
**cannot be updated by the old triplet**: the format bumped to 2, the source
became an enum, and `IMAGE_VERSION` became a required image-manifest key. The
migration is a reflash, which is the recorded prototype-phase posture.

Two bench-relevant facts already confirmed live on that Pi:

- its USB device reports `TRAN=usb` but `RM=0 HOTPLUG=0` — this is why the
  discovery predicate deliberately does not require the removable flag;
- the image has the `exfat` kernel module and `exfatprogs`, so the exFAT half
  of the acceptance needs no new runtime dependency.

## Next actions, in order

1. **Build a fresh version pair** on the build host (both repos' `main` pulled
   first). `00.23`/`00.24` are burned; use `00.25`/`00.26` or later:

   ```sh
   sudo ./build-image.sh --board=micropanel-touch --variant=luckfox-ctp \
     --version=00.25 --layout=ab --app-ref=main
   sudo ./build-image.sh --board=micropanel-touch --variant=luckfox-ctp \
     --version=00.26 --layout=ab --app-ref=main --payload
   ```

   The first build creates the release signing keypair if it does not exist
   and prints a custody notice — read BUILD.md "Release signing key custody"
   before the first one, because the private key must be backed up before
   anything is published with it.

2. **Flash `00.25`**, refresh the temporary known-hosts entry (reflashing
   changes SSH host keys), and run the BUILD.md first-boot acceptance list.

3. **Run the Stage 2b bench acceptance** (plan §8): the `00.26` bundle copied
   as a single file onto an unformatted shop stick, FAT32 **and** exFAT
   variants, A→B and B→A with commit and power-cycle persistence;
   corrupt-byte refusal before arm; **mid-write power cut** and
   **post-arm/pre-commit power cut** (these also close handover v8's pending
   recovery-smoke re-runs on the V4-hardened code); zero- and multiple-bundle
   refusals; and a same-version bundle reporting "already runs that version"
   after only kilobytes. Record the result in plan §8 the same day.

4. Then **Stage 3** (factory reset), then **Stage 4** (OTA + signature
   verification as one stage).

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
