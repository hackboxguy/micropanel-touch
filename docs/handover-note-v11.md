# MicroPanel Touch handover — v11

**Prepared:** 2026-08-19 (opus)
**Supersedes:** [`handover-note-v10.md`](handover-note-v10.md) as the current
restart point. V10 remains the record of the Stage 2b acceptance and its
fix-forward.

Read, in order: this note →
[`pi-in-system-update-plan.md`](pi-in-system-update-plan.md) (§6 for the
format=2 bundle; §8 for Stage 2b's acceptance, the review-fix record, and
**Stage 2c**, the engine extraction) →
`misc-tools/packages/pi-ab-update/README.md` (the engine's board contract) →
`misc-tools/board-configs/micropanel-touch/BUILD.md` (build, flash, signing,
acceptance). [`micropanel-touch-plan.md`](micropanel-touch-plan.md) §0 is the
wider sprint context.

## Where the work stands

The A/B track has moved twice since v10, and both moves are hardware-accepted.

- **Stages 0–2b:** complete and accepted (records in plan §8).
- **Stage 2b review fixes** ([`fable-review-stage2b.md`](fable-review-stage2b.md)
  O-01/O-02/O-03/O-05): accepted 2026-08-19. O-01 also **diagnosed and closed**
  the acceptance session's unexplained USB-claim observation — a failure path
  unmounted the source while the bundle was still open on fd 0/3, so the
  unmount failed EBUSY, was swallowed, and left a mount holding the device
  inside the broker's `PrivateTmp` namespace. Verified on the bench before and
  after.
- **Stage 2c — the update engine is extracted:** accepted 2026-08-19. The
  updater now lives in `misc-tools/packages/pi-ab-update/` as a board-agnostic
  toolkit. A `00.29`→`00.30` bench regression plus a mid-write power cut proved
  the refactor changed nothing observable.
- **Next: Stage 3 — factory reset, built in the engine** (plan §7/§8 and the
  extraction proposal's step 5). Then Stage 4 (OTA + signature verification as
  one stage), also in the engine.

**Owner decision, 2026-08-19:** factory reset also clears the A/B update state,
so a reset device looks freshly flashed. Wiping `/data` removes `update-state`
anyway; this records that as intended semantics rather than a side effect.

## Bench state

Pi 4 + Luckfox CTP at the address and credentials given in the session.

- committed **slot B running `00.30`**, `state=committed  candidate_slot=B`,
  app revision `f9a17ebd91d5d76ca4692d9e8aee31c9c40a574e`;
- **slot A holds a dirty, unlabelled remnant** of the deliberately interrupted
  write from the Stage 2c recovery smoke. That is the designed post-cut state,
  not damage: the next update overwrites it, and the missing label is what
  makes it safe;
- exFAT stick attached carrying the `00.29` bundle;
- `00.23` through `00.30` are burned identifiers — advance past them.

## What changed operationally (read this before touching the bench)

The engine renamed the surface a bench session touches:

| Was | Now |
|---|---|
| `/opt/micropanel-touch/usr/bin/micropanel-touch-system-update` | `/usr/local/sbin/ab-system-update` |
| `/usr/local/sbin/micropanel-touch-slot-selector` | `/usr/local/sbin/ab-slot-selector` |
| `/usr/local/sbin/micropanel-touch-update-commit` | `/usr/local/sbin/ab-update-commit` |
| `micropanel-touch-update-commit.service` | `ab-update-commit.service` |
| `journalctl -t micropanel-touch-system-update` | `journalctl -t 'ab-system-update[micropanel-touch]'` |
| env seams `MICROPANEL_UPDATE_*` | `AB_*` (see the engine README) |

Unchanged on purpose: `/run/micropanel-touch-update/{progress,status}` (the HMI
reads them, so `AB_RUNTIME_DIR` keeps the historical path),
`/data/micropanel-touch-system/update-state`, the `MP_*` labels, the partition
layout, and the `@MICROPANEL_SLOT@` cmdline placeholder.

The broker execs the engine by absolute path; `--update-engine` overrides it.

## Release signing key

The ed25519 key at `/etc/micropanel-touch/release-signing/ed25519-release.key`
is **backed up by the owner** (confirmed 2026-08-19), which closes v10's open
action. Its public half is baked into every image from `00.25` on. Custody
rules stay in BUILD.md; the location is now a board setting
(`AB_RELEASE_KEY_DIR`) rather than an engine default, precisely because that
public half is already in flashed images.

Since Stage 2b's O-02 fix, a release publishes **three** assets: the bundle,
the standalone manifest, and its detached `manifest.sig` — the last so Stage
4's check step can verify the tiny manifest before offering an update.

## Running the tests

The engine owns its suites now; `ctest` on the app repo no longer carries them.

```sh
packages/pi-ab-update/tests/run-tests.sh          # five suites
sudo packages/pi-ab-update/tests/run-tests.sh     # adds the two loopback fixtures
```

Run it with `sudo` once per engine change: the two root-only fixtures self-skip
without it and say so, and they are where the real device-side behaviour is
exercised. If a run is interrupted, check `losetup -a` before rerunning.

## Build workflow

```sh
sudo ./build-image.sh --board=micropanel-touch --variant=luckfox-ctp \
  --version=<v> --layout=ab --app-ref=main            # image
sudo ./build-image.sh --board=micropanel-touch --variant=luckfox-ctp \
  --version=<v+1> --layout=ab --app-ref=main --payload # image + bundle
```

- `--app-ref=main` resolves against **GitHub**, so both repos must be pushed
  before a build carries your work. This has bitten twice.
- `--payload-dir` now defaults to `<output>/payloads/<version>`; the generator
  also refuses to publish over a different version, so releases can no longer
  silently share a directory.
- To regenerate only a payload for an image that already exists, add
  `--skip-apps` — minutes instead of a full apps rebuild.
- The apps `.stamp` is per output directory, not per version, so building N+1
  invalidates N's cache.
- Worth keeping: read the updater back out of the built image and compare its
  hash to `packages/pi-ab-update/ab-system-update` before flashing. That
  separates "the build resolved ref main" from "the fix is in the artifact I am
  about to flash".

## Bench-procedure cautions that stay in force

- When interrupting a bench update, kill by **exact PID** — a `pkill -f`
  pattern matches your own SSH command line. Its cousin bit three times this
  session: `pgrep -f <pattern>` from an SSH command whose own text contains the
  pattern reports a phantom process.
- The broker runs with `PrivateTmp=yes`, so the handler's source mount lives in
  a **private mount namespace**. An SSH-side `mountpoint` check on
  `/run/micropanel-touch-update/source` proves nothing either way; read
  `/proc/<handler-pid>/mounts` instead.
- Every published payload gets one bench boot acceptance before release; every
  stage ends with its acceptance recorded in the plan the same day.
- Attended-update posture: a pre-PID-1 candidate failure is recovered by one
  manual power-cycle (owner-accepted, bench-proven). The UI states an
  open-ended recovery condition, deliberately without a numeric window.
- The unsigned-payload residual remains the dominant product risk until Stage
  4: signing is build-side only, and physical USB possession is update
  authority today.
- Reflashing changes SSH host keys; refresh only the temporary known-hosts
  entry. Panel variants stay per-image; never runtime-switch boot profiles.

## Things a fresh session will want to know

- The build host **cannot compile the application**: `libgpiod` and
  `nlohmann-json` are absent and `external/lvgl` is an empty submodule. C++
  changes are syntax-checked per translation unit; the real compile happens in
  the image build, so a build failure there is the first thing to check. The
  `00.29` image build retro-validated this path for the `--update-engine`
  change.
- The exFAT case in the device-side loopback fixture self-skips when the host
  has no `mkfs.exfat` (this build host). It runs on a host with `exfatprogs`,
  and exFAT is covered on the bench regardless.
- A handler change can be exercised on the bench without a full build by
  installing it into the running overlay (`/opt/...` and `/usr/local/sbin` are
  tmpfs-backed overlay, so it reverts on reboot). Used for the O-01 before/after
  probes; say so explicitly in any record that relies on it.
