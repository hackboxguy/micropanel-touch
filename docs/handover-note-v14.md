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

**Stages 0–3 complete and accepted. Stage 4 is complete and accepted** —
including against GitHub proper, which closed the last open checklist item on
the same day. The full table is in plan §8. The headlines:

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

## The GitHub chain — done

Release **`00.36`** is published on `hackboxguy/micropanel-touch` (three
version-less assets, 1.55 GiB bundle). A `00.37` image built with the
**default** template was flashed, and the panel ran check + install against
GitHub: a 1.4-second check moving 298 bytes, then 1.55 GiB from the asset CDN
into slot B, verified, armed, booted, committed. Its connection table during
the transfer showed both legs — `github.com` and the asset host it redirects
to — which is what makes `--location` and `--proto-redir` load-bearing.

The stalling-server case also ran on hardware: check bounded at 60s, bundle
fetch at 61s, published as `failed-network` while the reader's own view was
"update bundle is empty", and **the inactive slot left untouched** so
rollback survived.

**Capacity, worth tracking:** the bundle is 1.55 GiB against GitHub's 2 GiB
per-asset limit — about 450 MiB of headroom, which will be hit without
warning as the rootfs grows. When it is, GitHub releases stop being a viable
channel and the failure appears at upload time, not before.

**Still never run on hardware:** a foreign-key USB refusal. Covered by host
fixtures; what is untested is the device. It needs a stick carrying a bundle
signed by a throwaway key.

## Bench state

Pi 4 + Luckfox CTP at the address and credentials given in the session.

- **The panel runs `00.36` on slot B, committed**, reached by a real GitHub
  OTA update from a flashed `00.37`. Slot A still holds `00.37`, so rollback
  is available and intact — two deliberate stalled-download failures did not
  disturb it.
- **Its release source is the GitHub default**, because `00.37` was built
  without `--release-url-template`. No rehearsal server is needed for it to
  check for updates; it will find `00.36` and report *up to date*.
- The device kept the address it had before the reflash. A regenerated machine
  identity does **not** move the DHCP lease — that is keyed on the MAC — so
  the earlier expectation (mine and the v5 review's) that it would change was
  wrong. Still worth confirming rather than assuming.
- `00.36` predates the curl first-line diagnostics fix, so its journal shows
  the doubled `curl: curl:` prefix and the unhelpful last line of multi-line
  TLS errors. Cosmetic; fixed from `00.37` onward.
- The USB stick still holds the older bundle (`00.29`) — deliberately, it is a
  useful offline-test fixture.
- **`00.23`–`00.37` are burned identifiers. Start at `00.38`.**
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
