# MicroPanel Touch handover — v7

**Prepared:** 2026-08-18
**Supersedes:** [`handover-note-v6.md`](handover-note-v6.md) as the current
restart point. V6 remains the record of the deliberately interrupted B
candidate and its `fallback` outcome.

## Current bench state

Fable independently revalidated the accepted Stage 2 path on the Pi 4 +
Luckfox CTP fixture after the v6 power-cut record. Its two same-version
`00.22` updates (A → B, then B → A) both committed normally. The live bench
fixture is now:

- `VERSION=00.22`, running and committed slot **A**;
- normal `config.txt`: `os_prefix=A/`; one-shot `tryboot.txt`:
  `os_prefix=B/`;
- durable and unprivileged public update state: `state=committed`, candidate
  A; and
- A and B labels correct, HMI and privileged broker active, no failed units.

This supersedes v6's live `fallback`/candidate-B snapshot only. That earlier
snapshot remains the evidence that an armed, uncommitted B candidate returns
to committed A after the attended power-cycle procedure.

## Independent Stage 2 revalidation

The reviewer observed the public state file as the unprivileged `pi` user
through `fallback → candidate-armed → committed` for a full update lifecycle.
During the A → B and B → A writes, the inactive p6 (17%) and p5 (21%) roots
were valid ext4 filesystems with **no label**, confirming the checksum-safe
label-neutral artifact and pre-stream superblock clearing prevent a duplicate
committed-root label window. Both candidates passed their 30-second health
window and committed cleanly.

Three harmless failure probes also behaved as designed:

| Probe | Result |
|---|---|
| nonexistent local source | `failed-source` |
| variant-mismatched manifest | `failed-compatibility` |
| p8 (`/dev/mmcblk0p8`) offered as source | policy refusal as `failed-source`, without mounting p8 |

Together with the v6 A → B/B → A commits, mid-write cut, corrupt-rootfs
refusal, and post-arm/pre-commit cut, this is complete Stage 2 hardware
acceptance for the unsigned, attended USB updater on Pi 4 + Luckfox CTP.

## Repository baseline on the bench

| Repository | Commit | Purpose |
|---|---|---|
| `micropanel-touch` | `9ebea62d741cf5db7188f35afdab21f89bbb3e64` | Stage 2 update recovery, target validation, public telemetry, and commit policy. |
| `misc-tools` | `bde05af5b336fb3f8c989ea41b31e5795536a237` | Checksum-safe label-neutral payload generator and source-label restoration coverage. |

## V4 hardening now in source, pending a fresh bench build

The post-acceptance review identified no Stage 2 blocker. The current source
adds the following defense and testability improvements before the next image:

1. payload generation accepts an already label-neutral p5 only as a recovery
   state, restores `MP_ROOT_A`, and `build-image.sh --payload` automatically
   runs the full A/B image verifier after generation;
2. the root handler uses explicit, fixed failure classes rather than inferring
   operator guidance from mutable error text; it also has an exclusive lock
   that cannot overwrite another invocation's progress telemetry;
3. lower-root and allowed-parent overrides plus a reboot-command override
   enable a root-only loopback integration test of the real stream → verify →
   relabel → render → arm path; and
4. the commit helper requires zero HMI restarts from candidate boot through
   readiness and settlement, so a candidate cannot hide early HMI crashes by
   becoming healthy later.

The root-only loopback tests passed on the sudo-capable Arch build host against
disposable images. The updater fixture exercised the real stream, digest,
filesystem check, relabel, boot render, selector arm, and reboot handoff; the
payload/finalizer fixture exercised a deliberately label-neutral source-root
recovery and full post-generation image verification. Neither test touched a
physical disk. These source changes are not yet included in the live `00.22`
bench image and therefore need a fresh-image/payload bench revalidation before
being claimed as hardware evidence.

## Scope and next step

The remaining dominant product risk is that Stage 2 payloads are unsigned; a
physically present USB payload meeting the current checks is authoritative.
Signing and downgrade policy remain Stage 4 work. Pre-PID-1 candidate failure
also still requires the owner-approved manual power-cycle.

Build a fresh Pi 4 + Luckfox CTP A/B image and payload from the V4-hardening
commits, then repeat one normal update and the relevant recovery smoke checks.
After that regression evidence, proceed to Stage 3 factory reset. The user
owns remote pushes and image builds; commit local changes but do not push them.
