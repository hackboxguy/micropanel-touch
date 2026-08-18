# MicroPanel Touch handover — v8

**Prepared:** 2026-08-18
**Supersedes:** [`handover-note-v7.md`](handover-note-v7.md) as the current
restart point. V7 remains the record of the initial V4 source hardening and
its privileged loopback coverage.

## Current bench state

A fresh Pi 4 + Luckfox CTP A/B card (`00.23`) was built after the V4 hardening
and flashed from scratch. The builder resolved `--app-ref=main` to the exact
application commit `07b261e645ef0f9498b9e2362c83eed1b2f5b034`; the installed
image manifest and updater checksum confirmed that revision on the Pi.

The attached `00.24` USB payload then completed the normal Stage 2 path:

1. the `00.23` system started on committed slot A;
2. it wrote and booted candidate B, then committed B after the 30-second
   health window; and
3. a physical power-cycle returned to committed B unchanged.

The live fixture is now:

- `VERSION=00.24`, running and committed slot **B** from
  `/dev/mmcblk0p6` (`MP_ROOT_B`);
- normal `config.txt`: `os_prefix=B/`; one-shot `tryboot.txt`:
  `os_prefix=A/`;
- durable and public update state: `state=committed`, candidate B;
- `MICROPANEL_TOUCH_REVISION=07b261e645ef0f9498b9e2362c83eed1b2f5b034`;
  and
- correct A/B/data labels, active HMI and privileged broker, and no failed
  units.

`00.23` and `00.24` were reissued bench identifiers after the earlier
hook-pin omission was discovered. Do not reuse either identifier for a later,
different artifact; the next build must advance its version.

## V4 regression evidence

The running image contains the reviewed V4 handler and commit-policy changes:

- fixed failure classes (`valid_failure_class`), including `failed-image`;
- non-blocking `flock` serialization for concurrent update handlers;
- the configurable lower-root/reboot seams used by the root-only loopback
  fixture; and
- the zero-HMI-restart rule throughout candidate readiness and settlement.

The completed `00.23` → `00.24` update proves that these changes coexist with
the real Pi 4 + Luckfox CTP normal update, candidate commit, and committed-slot
power-cycle path. The previous Stage 2 acceptance evidence still covers
mid-write cut recovery, corrupt-rootfs refusal before arm, and the
post-arm/pre-commit fallback procedure.

## Build provenance and release guard

The Arch builder used `misc-tools`
`bf855e4f491fe0bc3bdab8a5bfa43f8d2bf192a3` and the application revision above.
MicroPanel Touch builds now require exactly one of:

- `--app-ref=main` (or another safe branch/tag), resolved once to a full SHA
  before preflight; or
- `--app-revision=<40-character SHA>` for a deliberate exact rebuild.

The resolved SHA is expanded into the SDM hook, checked after checkout,
written to the installed image manifest, and verified by both A/B finalization
and payload validation. An omitted or conflicting application source now
fails before an expensive image stage, preventing the stale-hook failure that
produced the earlier `00.23` artifact.

## Remaining scope and next step

Stage 2 remains an unsigned, attended USB updater; signing and downgrade
policy are Stage 4 work. Pre-PID-1 candidate failure continues to use the
owner-approved attended power-cycle procedure.

To fully re-run V4 hardware evidence, repeat the recovery smoke cases on a
newer version pair: one mid-write interruption, one corrupt-rootfs refusal,
and one post-arm/pre-commit interruption. Then proceed to Stage 3 factory
reset. The user owns remote pushes and image builds; commit local changes but
do not push them.
