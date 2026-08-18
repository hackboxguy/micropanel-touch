# MicroPanel Touch handover — v9

**Prepared:** 2026-08-18 (fable)
**Supersedes:** [`handover-note-v8.md`](handover-note-v8.md) as the current
restart point. V8 remains the record of the V4-hardening build (`00.23`) and
its `00.23`→`00.24` hardware regression.

This note is written for a fresh engineering session. Read, in order:
this note → [`pi-in-system-update-plan.md`](pi-in-system-update-plan.md)
(§8 has the reordered stages) →
[`fable-ota-usb-simplification-proposal.md`](fable-ota-usb-simplification-proposal.md)
(the approved Stage 2b design) →
`misc-tools/board-configs/micropanel-touch/BUILD.md` (build/flash/acceptance
procedures). [`micropanel-touch-plan.md`](micropanel-touch-plan.md) §0 is the
wider sprint context; the A/B work is currently the active track.

## Current bench state (live-verified 2026-08-18, after the v8 snapshot)

The Pi 4 + Luckfox CTP fixture (`192.168.1.124`-class bench device; use the
address and credentials provided in the active session, never this repo):

- `VERSION=00.24`, running and **committed slot A** from `/dev/mmcblk0p5`
  (`root=LABEL=MP_ROOT_A`);
- normal `config.txt`: `os_prefix=A/`; one-shot `tryboot.txt`: `os_prefix=B/`;
- durable and public update state: `state=committed`, `candidate_slot=A`;
- `MICROPANEL_TOUCH_REVISION=07b261e645ef0f9498b9e2362c83eed1b2f5b034`
  (installed handler verified byte-identical to that revision's source);
- A/B/data labels correct, HMI and privileged broker active, no failed
  units; a USB stick with the `00.24` triplet payload is attached.

This **supersedes v8's committed-B snapshot**: an independent review session
ran a mid-write TERM interrupt (clean recovery: `failed-internal` published,
source unmounted, target left unlabelled, durable state untouched) followed
by a successful retry that booted candidate A once and committed it. That
session also verified the concurrency lock twice (second invocation refused
without touching the owner's progress telemetry).

Bench-procedure caution from that session: when interrupting a bench update,
kill processes **by exact PID** — a `pkill -f` pattern can match your own
SSH command line and kill your session instead of the handler.

## Where the A/B work stands

- **Stages 0–2: complete and hardware-accepted** (records in the plan §8):
  layout, selector, watchdog, populated-slot switching, USB updater with
  hash-before-arm, health-gated commit service, public status telemetry,
  fixed failure classes, structural target resolution, label-neutral
  payloads, concurrency lock, and the V4-hardened `00.23`→`00.24` regression.
- **Still pending from v8:** the power-cut recovery-smoke re-runs on the
  hardened code (mid-write cut; post-arm/pre-commit cut). These are
  deliberately **folded into Stage 2b acceptance** — do not run them
  separately first.
- **Build provenance:** builds require exactly one of
  `--app-ref=<branch|tag>` (resolved once to a SHA before preflight) or
  `--app-revision=<40-char SHA>`; the SHA is verified at checkout, recorded
  in the image manifest, and re-checked by the A/B finalizer and image
  verifier. Bench identifiers `00.23`/`00.24` are burned — the next build
  must advance its version.
- Repository heads at handover: `micropanel-touch` `cfdc03d` (proposal doc),
  `misc-tools` `e0e7816`. Both repos' `main` must be pulled on the build
  host before building. The user owns pushes and image builds; commit local
  changes to `main`, do not push.

## Approved next work order (owner decision, 2026-08-18)

1. **Stage 2b — single-file `.mpupdate` bundle + zero-preparation USB.**
   The full task list, the OTA-forward groundwork checklist, and the
   acceptance list are in plan §8 (Stage 2b) and the proposal doc. The
   intent in one line: an average Windows user copies **one file** to a
   shop-fresh FAT32/exFAT stick — no formatting, no label, no triplet — and
   the single-pass bundle reader built here is byte-for-byte the future OTA
   reader. OTA is **not urgent**, but Stage 2b deliberately front-loads
   everything OTA needs (pipe-capable reader, reserved `manifest.sig`
   member, version-less asset names, manifest-first early abort, build-side
   signing from the first format=2 release) so Stage 4 OTA is source-plumbing
   only, with no format or reader rework.
2. **Stage 3 — factory reset** (unchanged, plan §7/§8).
3. **Stage 4 — OTA + signature verification as one stage** (plan §8 item 1;
   never ship OTA unsigned), then factory payload, then the Pi 3/Pi 5 board
   matrix.

Open minor findings to fold into Stage 2b (from the 2026-08-18 review; all
small, none blocking):

| Id | Item | Where |
|---|---|---|
| V5-01 | `cleanup()` signal traps re-enter via EXIT (observed as a harmless double-`umount` message); `trap - EXIT HUP INT TERM` at cleanup start — same pattern applies to the selector and commit helper | update handler + selector + commit helper |
| V5-02 | A SIGKILL'd handler can strand the USB source mountpoint; reclaim a stale non-busy mount when the update lock is held | update handler |
| V5-03 | Acquire the update lock before any `die` that publishes telemetry, so only the lock owner ever writes the progress file | update handler |
| V5-04 | Stream-stall policy: either N-minute write-stall detection or an explicit doc note that the 30-minute broker ceiling is the answer | Stage 2b design decision |
| V5-05 | App/release repo URL is duplicated (builder + both hook lists); one source of truth, which also becomes the reserved OTA URL-template location | misc-tools |

## Working rules that stay in force

- Every published payload gets one bench boot acceptance before release;
  every stage ends with its acceptance recorded in the plan the same day.
- Attended-update posture: pre-PID-1 candidate failure is recovered by one
  manual power-cycle (owner-accepted, bench-proven); the UI states an
  open-ended recovery condition, deliberately without a numeric window.
- The unsigned-payload residual is the dominant product risk until Stage 4;
  physical USB possession is currently update authority. Stage 2b's
  build-side signing narrows the eventual cutover but device-side
  enforcement waits for Stage 4.
- Reflashing changes SSH host keys; refresh only the temporary known-hosts
  entry after confirming a reflash. Panel variants remain separate images;
  never runtime-switch boot profiles. Do not reuse bench version
  identifiers.
