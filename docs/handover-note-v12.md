# MicroPanel Touch handover — v12

**Prepared:** 2026-08-19 (opus)
**Supersedes:** [`handover-note-v11.md`](handover-note-v11.md) as the current
restart point. V11 remains the record of the Stage 2c extraction state.

Read, in order: this note →
[`pi-in-system-update-plan.md`](pi-in-system-update-plan.md) (§7 for factory
reset and its trust residual; §8 for Stage 2b/2c/3 and their acceptance
records) → `misc-tools/packages/pi-ab-update/README.md` (the engine's board
contract) → `misc-tools/board-configs/micropanel-touch/BUILD.md` (build,
flash, signing, acceptance). [`micropanel-touch-plan.md`](micropanel-touch-plan.md)
§0 is the wider sprint context.

## Where the work stands

**Stages 0–3 are complete and hardware-accepted.** Stage 4 is next.

- **Stage 2b** (single-file `.mpupdate` bundle + zero-preparation USB) and its
  review fixes: accepted.
- **Stage 2c** (the update engine extracted to `misc-tools/packages/pi-ab-update`
  as a board-agnostic toolkit): accepted; a `00.29`→`00.30` regression plus a
  mid-write power cut proved it changed nothing observable.
- **Stage 3** (factory reset, built in the engine): accepted 2026-08-19 —
  PIN gate, a reset interrupted by a real power cut that retried and completed,
  and a fresh-device state measured against a baseline taken before the run.
- **Stage 4 — OTA + signature verification as one stage** is the next work,
  built in the engine.

**Fixes that landed after the Stage 3 run and have not been on hardware yet:**
the deferred reboot (the panel briefly showed *"invalid privileged broker
response"* because the engine rebooted before the reply could land), the
cancelled-request marker withdrawal, a journal-duplication fix, and the honest
cancel wording. **The next build should confirm the panel now shows the success
message instead of the broker error** — that is the only outstanding bench
item.

## Bench state

Pi 4 + Luckfox CTP at the address and credentials given in the session.

- **`00.32` on slot A, freshly factory-reset.** Identity regenerated
  (machine-id `8b403a71…`, all three SSH host keys new), **no screen lock**,
  **no update history**, `/data` at the pristine skeleton. Slot B holds the
  remnant of an earlier interrupted write — dirty and unlabelled, which is the
  designed post-cut state, not damage.
- The device boots **with a stale clock** after a reset (see below), so its
  journal timestamps lag until NTP syncs.
- exFAT stick attached carrying the `00.29` bundle.
- **`00.23` through `00.32` are burned identifiers** — including `00.31`, which
  was superseded by `00.32` before it was ever accepted. Start at `00.33`.

## Working agreement added this session

**The session's last commit is the handover note.** The note went stale twice
by being written before the session's final stage landed. A stage's acceptance
record and the handover that points a fresh session at it must not end up on
opposite sides of a session boundary. This is now recorded in plan §8.

## Two things a Stage 4 session must design around

1. **A reset device boots with a stale clock.** The wipe removes the saved
   time-sync state, so an RTC-less Pi comes up in the past until NTP syncs.
   "Check for Updates" does TLS to GitHub, so a freshly reset or
   long-powered-off device can fail certificate validation for that reason
   alone. Give it its own refusal class ("clock not yet synchronized") rather
   than letting it surface as a generic TLS failure — cheap now, confusing to
   debug in the field later.
2. **The factory-reset trust residual** (plan §7): the PIN gate lives entirely
   in the UI, so a compromised HMI account can wipe durable state with one
   broker request. That is deliberate and as strong as achievable while the
   lock verifier is app-account-owned; root-owned lock state is the available
   hardening if the threat model tightens. Stage 4 adds a *network* update
   authority, so it is worth re-reading that paragraph before designing the OTA
   trigger.

## What Stage 4 already has

Almost all of the groundwork shipped with Stage 2b:

- a **pipe-capable single-pass reader** — OTA is `curl | ab-system-update
  stdin`, and the `stdin` source is exercised by the loopback fixture today;
- every release publishes **three assets**: the bundle, the standalone
  manifest, and its detached `manifest.sig`, so the check step can verify the
  tiny manifest before offering anything;
- the **pinned public key** at `/usr/lib/pi-ab-update/update-signing-key.pub`
  and the reserved, root-owned `/usr/lib/pi-ab-update/update-source.conf`
  holding version-less `MANIFEST_URL`, `MANIFEST_SIG_URL` and `BUNDLE_URL`;
- **manifest-first early abort** and a `failed-version` class, so "already up
  to date" costs a few hundred bytes.

What is left is source plumbing plus turning device-side verification on, and
adding `curl` + `ca-certificates` to the runtime deps.

## Operational surface

Engine paths and names (unchanged since v11, repeated because they are what a
bench session reaches for):

| Purpose | Path |
|---|---|
| updater | `/usr/local/sbin/ab-system-update` (broker execs it; `--update-engine` overrides) |
| slot selector | `/usr/local/sbin/ab-slot-selector` |
| commit service | `/usr/local/sbin/ab-update-commit`, unit `ab-update-commit.service` |
| factory reset | `/usr/local/sbin/ab-factory-reset` (request; broker execs it, `--factory-reset-engine` overrides) |
| reset wipe | `/usr/local/sbin/ab-factory-reset-boot`, unit `ab-factory-reset.service` |
| board profile | `/usr/lib/pi-ab-update/ab-update.conf` |
| journal tags | `ab-system-update[micropanel-touch]`, `ab-factory-reset[micropanel-touch]` |
| telemetry | `/run/micropanel-touch-update/{progress,status}` (historical path, kept because the HMI reads it) |

## Running the tests

```sh
packages/pi-ab-update/tests/run-tests.sh          # six suites
sudo packages/pi-ab-update/tests/run-tests.sh     # adds the two loopback fixtures
```

Run it with `sudo` once per engine change; the root-only fixtures self-skip
otherwise and say so. If a run is interrupted, check `losetup -a` before
rerunning — and never pair `losetup -D` with a test run in the same command
line, which races the fixtures' own `losetup --find`.

The application repo keeps only its trigger-surface tests (`ctest`): the broker
wire format, the operations validation, the health hook, and the headless UI
walkthrough.

## Build workflow

```sh
sudo ./build-image.sh --board=micropanel-touch --variant=luckfox-ctp \
  --version=<v> --layout=ab --app-ref=main             # image
sudo ./build-image.sh --board=micropanel-touch --variant=luckfox-ctp \
  --version=<v+1> --layout=ab --app-ref=main --payload  # image + bundle
```

- `--app-ref=main` resolves against **GitHub**: both repos must be pushed
  before a build carries your work. This has bitten twice.
- `--payload-dir` defaults to `<output>/payloads/<version>`, and the generator
  refuses to publish over a different version.
- `--skip-apps` regenerates only a payload for an image that already exists —
  minutes instead of a full rebuild.
- Read the updater back out of the built image and compare its hash to
  `packages/pi-ab-update/ab-system-update` before flashing. That separates "the
  build resolved ref main" from "the fix is in the artifact I am flashing".
- The build compiles the app's tests (`include(CTest)`), so a broken test
  breaks the image build.

## Bench-procedure cautions that stay in force

- **`pgrep -f` / `pkill -f` self-matching** bit four times this session,
  including once in my own tooling, where a wait-loop matched the shell whose
  argv contained the pattern. Kill by exact PID; when polling for a process,
  make sure the pattern cannot match the poller.
- The broker runs with `PrivateTmp=yes`, so the handler's source mount lives in
  a **private mount namespace**. An SSH-side `mountpoint` check proves nothing;
  read `/proc/<handler-pid>/mounts`.
- A shell handler can be exercised on the bench without a full build by
  installing it into the running overlay (tmpfs-backed, so it reverts on
  reboot). Say so explicitly in any record that relies on it.
- Every published payload gets one bench boot acceptance before release; every
  stage ends with its acceptance recorded in the plan the same day.
- Attended-update posture: a pre-PID-1 candidate failure is recovered by one
  manual power-cycle. The UI states an open-ended recovery condition,
  deliberately without a numeric window.
- The unsigned-payload residual remains the dominant product risk **until
  Stage 4 lands**: signing is build-side only today, and physical USB
  possession is update authority.
- Reflashing *and factory reset* both change SSH host keys; refresh the
  temporary known-hosts entry after either.

## Things a fresh session will want to know

- The build host **cannot compile the application**: `libgpiod` and
  `nlohmann-json` are absent and `external/lvgl` is an empty submodule. C++
  changes are syntax-checked per translation unit and the real compile happens
  in the image build — which is exactly how the missing PIN keyboard reached
  the bench. Treat any UI change as unverified until an image builds.
- The exFAT case in the device-side loopback fixture self-skips without
  `mkfs.exfat` (this host lacks it); exFAT is covered on the bench regardless.
- The release signing key at
  `/etc/micropanel-touch/release-signing/ed25519-release.key` is backed up by
  the owner. Its location is a board setting (`AB_RELEASE_KEY_DIR`), because
  its public half is already inside flashed images.
