# MicroPanel Touch handover — v14

**Prepared:** 2026-08-19 (opus)
**Supersedes:** [`handover-note-v13.md`](handover-note-v13.md) as the current
restart point. V13 remains the record of Stage 4 *before* any hardware ran.

Read, in order: this note →
[`pi-in-system-update-plan.md`](pi-in-system-update-plan.md) §8 "Stage 4" (the
decisions, the acceptance checklist, and the acceptance record) →
`misc-tools/packages/pi-ab-update/README.md` ("About update authenticity") →
`misc-tools/board-configs/micropanel-touch/BUILD.md` ("Where releases come
from (OTA)").

## Where the work stands

**Stages 0–3 complete and accepted. Stage 4 is complete and bench-accepted
against a rehearsal server.** Ten items ran on a Pi 4 + Luckfox CTP; the full
table is in plan §8. The headlines:

- A `00.35` → `00.36` network install: 1.67 GB streamed over HTTP straight
  into the inactive slot, verified, armed, booted, and self-committed after
  30 seconds of health.
- **A signed USB release installed on a panel whose clock said January 2016.**
  This is the claim the whole offline deployment rests on, and it is now
  tested rather than argued.
- A signed downgrade both *offered* (`00.36` running, `00.35` offered →
  `available`) and *installed* (`00.36` → `00.29`, booted, committed).
- A TLS failure with an unsynchronized clock reported as `clock`, not
  `network` — same server, same trusted certificate, only the date changed.

## The one thing that has never run

**The real GitHub delivery chain.** Every OTA test used
`ab-serve-release.sh` on the build host via `--release-url-template`. That
exercises neither the default template rendering nor
`releases/latest/download/` → redirect chain → asset CDN → TLS. Until a real
release is published and check+install run against the **default** template,
Stage 4 is *accepted against a rehearsal server* and the plan says so. This is
the top item for the next session, and it needs the owner to publish.

Two smaller gaps, both covered by host fixtures but never run on the Pi: the
stalling-server case and the foreign-key refusal.

## Bench state

Pi 4 + Luckfox CTP at the address and credentials given in the session.

- **The card was freshly flashed with `00.36`** at the end of this session, so
  the panel starts clean: no update history, no accumulated state. Slot A. Its
  release source points at `http://192.168.1.80:8000/@ASSET@`, i.e. the build
  host - not GitHub. Rebuild without `--release-url-template` to get the
  default.
- That image predates two fixes committed after it was built: the corrected
  `update-source.conf` comment and the curl first-line diagnostics. Both land
  in `00.37`. Nothing functional depends on either.
- The USB stick still holds the older bundle (`00.29`) — deliberately, it is a
  useful offline-test fixture.
- **`00.23`–`00.36` are burned identifiers. Start at `00.37`.**
- Payload directories kept for `00.30`, `00.35`, `00.36` under
  `~/pi-image-workspace/out/micropanel-touch-luckfox-ctp-ab/payloads/`. Serve
  any of them with `ab-serve-release.sh <dir> 8000`.

## The build host can now build and test the application

This changed mid-session and matters more than it sounds. Installing
`libgpiod` and `nlohmann-json` (`sudo pacman -S --needed libgpiod
nlohmann-json`) and checking out the LVGL submodule (`git submodule update
--init --recursive`) makes the whole application build on the build host, and
all 42 ctest tests run in about 13 seconds.

Before that, UI changes were compile-checked only by a ~40-minute image build.
The loop paid for itself twice within minutes of existing: it caught a compile
error (the *Update now* button added to `show_network_result`, which has an
identical tail, instead of `show_system_update_result`) and then a real defect
— the offer state was set *before* `show_system_update_result()`, which clears
the screen first, so the button rendered and **did nothing at all**. Only
pressing it finds that. Use this loop.

## What the bench found that no fixture could

Both were in the curl diagnostics added for review finding N-03:

1. A doubled `curl: curl: (7) …` prefix — curl prefixes its own messages.
2. Worse: logging the **last** line of curl's six-line TLS error. The journal
   said "please visit the webpage mentioned above" when curl's first line said
   "certificate is not yet valid" — the one line that names the clock as the
   problem. Every host fixture produces single-line failures, so the
   multi-line case was structurally invisible to them.

**And one fix validated by accident.** During the offline run the commit
service started at a stale `Aug 18 14:13:39` and finished at `Aug 19
20:12:08` — the wall clock moved ~30 hours mid-window, through 2016 and then
an NTP resync — and it correctly reported *30 seconds* of health and
committed. Wall-clock arithmetic would have computed ~110,000 seconds,
judged the deadline expired, and dropped a healthy candidate into fallback.
That bug was found by reasoning, not by a failure; this is the first time the
conditions occurred.

## Open, honestly

- **An intermittent fixture failure, unexplained.** `test_ab_layout_integration.sh`
  has failed twice inside a full `run-tests.sh` pass and never once standalone,
  across 15+ runs including six consecutive deliberate attempts to reproduce
  it. I did not fix it because I could not diagnose it. The fixture now runs
  under `set -E` with an ERR trap that reports the failing line and the
  loop-device table, and `run-tests.sh` waits for udev between the loopback
  fixtures — the most plausible mechanism, and free. If it recurs, the next
  occurrence will say where.
- The verifier gap found this session (`ab-verify-image.sh` ran only under
  `--payload`, so flashed images were never checked) is fixed, but note that
  `00.35` went onto the bench card unverified before that landed.

## Review disposition (fable v5)

The v5 review found **no defects** and closed all five v4 findings. Its four
notes are disposed of as follows:

- **V5-A** (the real GitHub run) — owner-gated, and now spelled out in plan §8
  checklist item 5: what the owner must supply, and the two negatives that
  have never run on hardware (stalling server, foreign-key USB refusal) folded
  into the same session because the marginal cost is minutes.
- **V5-B** (`00.35` flashed unverified) — no action; the gap is fixed and the
  record already says it. Noted so the audit trail connects.
- **V5-C** (host app-test loop as a habit) — done: plan §8 now carries "both
  test gates run before any image build that touches what they cover", so it
  no longer lives only in a handover narrative.
- **V5-D** (`dmesg` in the flaky fixture's trap) — done.

## Two corrections to the v5 review's context

Neither changes a finding, but the next session should not be misled:

- The panel is unreachable because **the card is currently out of it** (moved
  to the build host for the `00.36` reflash), not only because of a new DHCP
  lease. Put the card back and it will boot. That said, the identity *is*
  regenerated by a fresh flash, so the address may well differ - ask the owner
  rather than assuming the old one.
- The bench card's `00.36` points at the build host's rehearsal server, so a
  panel booted from it will report `network` on "Check for updates" unless
  that server is running. That is configuration, not a fault.

## Working agreements

- **The session's last commit is the handover note.**
- **Both test gates pass before an image build that touches what they cover** —
  the engine's `run-tests.sh` and the application's `ctest`.

Both recorded in plan §8.
