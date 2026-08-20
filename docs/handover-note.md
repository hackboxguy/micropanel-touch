# MicroPanel Touch handover

**Last updated:** 2026-08-19 (opus)

**This is the only handover note.** It is rewritten in place at the end of
every session rather than versioned — fourteen numbered predecessors were
consolidated into this file on 2026-08-19, and they remain in git history if
an earlier bench state is ever needed (`git log --follow -- docs/`). Keeping
one current file removes the failure mode that produced them: a reader
picking up a stale note and acting on a bench state that no longer exists.

**For the exact shell commands** — building an image, cutting an update
payload, publishing a GitHub release, flashing a card, rehearsing an update
without publishing — see
[`build-release-update-commands.md`](build-release-update-commands.md).

Read, in order: this note →
[`pi-in-system-update-plan.md`](pi-in-system-update-plan.md) §8 "Stage 4" (the
decisions, the acceptance checklist, and the acceptance record) →
`misc-tools/packages/pi-ab-update/README.md` ("About update authenticity") →
`misc-tools/board-configs/micropanel-touch/BUILD.md` ("Where releases come
from (OTA)").

## The stable base

**`00.39` is the first stable release**, published at
`https://github.com/hackboxguy/micropanel-touch/releases/tag/00.39` and marked
Latest. It is what `releases/latest/download/` resolves to, so any A/B device
built with the default template finds it.

What makes it the base rather than just the newest build: the menus show only
what actually works (Display, Network and System as square icon tiles;
experimental and unimplemented entries hidden rather than presented as
controls that do nothing), `ab-update` gives the engine a single front door
reachable by name, and the whole signed A/B path — network, USB, health-gated
commit, rollback — has run on hardware.

It was installed on the bench **from the published release over the live
GitHub chain**, not from a flash, which is the strongest form of that claim.

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

## The GitHub delivery chain

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

**The foreign-key USB refusal ran too**, closing the last item: a bundle
whose 64-byte signature member had been replaced with a well-formed signature
from an untrusted key was refused in **0 seconds** as `failed-signature`,
before any of the 1.6 GiB payload was read, with the inactive slot's label and
UUID unchanged afterwards. **Every item on the Stage 4 checklist has now run on
hardware.**

## Bench state

Pi 4 + Luckfox CTP at the address and credentials given in the session.

- **The panel runs `00.39` on slot A, committed** — installed from the
  published GitHub release over the real chain, not from a flash. Slot B holds
  `00.36` as the rollback target. Its release source is the GitHub default, so
  no rehearsal server is needed for it to check for updates.
- **There is one card, and it is in the Pi.** Its slot A was blank when
  `00.39` was installed: an earlier USB update was interrupted by a deliberate
  power cut, which is exactly the designed post-cut state — the engine clears
  the target superblock before streaming, so an interruption leaves the slot
  dirty and unlabelled rather than half-written and bootable. The next update
  reclaimed it with no special case and no manual repair, which is the first
  time the recovery half of that design has been exercised on hardware.
- If a *pristine* reference card is wanted, flash `00.39` fresh: that gives
  slot A populated, slot B reserved and empty, and no update history — at the
  cost of the current rollback slot.
- The device kept the address it had across a reflash. A regenerated machine
  identity does **not** move the DHCP lease — that is keyed on the MAC — so
  the earlier expectation (mine and the v5 review's) that it would change was
  wrong. Still worth confirming rather than assuming.
- The USB stick still holds the older bundle (`00.29`) — deliberately, it is a
  useful offline-test fixture.
- **`00.23`–`00.39` are burned identifiers. Start at `00.40`.**
- Payload directories kept for `00.30`, `00.35`, `00.36` and `00.39` under
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

Three defects, and the common thread matters more than any of them: the
fixtures assert on failure *classes*, and in every case the class was
correct. What was wrong was the sentence a person reads — which nothing in
the suite looks at. The signature refusal printed **"this devices release
key"**, because `''` inside a single-quoted shell string closes and reopens
the quote rather than escaping an apostrophe. The other two were in the curl
diagnostics added for review finding N-03:

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

## The operator front door: `ab-update`

`misc-tools/packages/pi-ab-update/ab-update` is a front door for the engine —
`status`, `check`, `install ota|usb|--file=`, `watch`, `log`, plus single-value
flags for scripts. It follows one rule, enforced by a static test: **it
composes, it never decides.** No version comparison, no compatibility rule, no
health judgement — those stay in the engine, because a second copy would drift
and the copy people run would be the untested one.

It ships from `00.38`, and is reachable by name from `00.39`.
**Caveat for `00.38` specifically:** it was installed
into `/usr/local/sbin`, which Debian keeps out of a non-root PATH, so its
unprivileged queries need the full path on that image. Fixed for `00.39`, where
it lives in `/usr/local/bin` and works by name.

Confirmed on hardware from `00.39`: `ab-update --active-version` runs
unprivileged, by name. A note on why `00.38` cannot simply be patched on the
device: **the root
filesystem is an overlay** — `/media/root-ro` is the read-only slot partition
and the upper layer is tmpfs. Anything written to `/usr` survives only until
the next reboot. Moving the file on a running device therefore demonstrates
the fix without persisting it, and this was briefly recorded here as
persistent, in error — the GitHub update run afterwards showed slot A still
shipping `ab-update` in `sbin`. Only a rebuilt image changes what a slot
holds. See `build-release-update-commands.md` §6.

Two capabilities are genuinely new: `--inactive-version` mounts the other slot
read-only (under the engine's lock) to report **what a rollback would land
on**, and `status` lists the units the commit predicate requires — because "the
install works without the GUI but the commit does not" is the most surprising
fact about operating this from a shell.

## Open, honestly

- **An intermittent fixture failure, unexplained.** `test_ab_layout_integration.sh`
  has failed twice inside a full `run-tests.sh` pass and never once standalone,
  across 15+ runs including six consecutive deliberate attempts to reproduce
  it. I did not fix it because I could not diagnose it. The fixture now runs
  under `set -E` with an ERR trap that reports the failing line and the
  loop-device table, and `run-tests.sh` waits for udev between the loopback
  fixtures — the most plausible mechanism, and free. If it recurs, the next
  occurrence will say where.
- The verifier gap (`ab-verify-image.sh` ran only under `--payload`, so
  flashed images were never checked) is fixed, but note that `00.35` went onto
  the bench card unverified before that landed.
- **The no-scroll menu property is portrait-only.** It is asserted at 320×480;
  at 480×320 a 2×3 grid becomes wider and shorter and has never been checked.
  Tracked in the plan's existing landscape bucket — do not read the property as
  both-orientation coverage.
- **Menu headroom is one tile in System** (Display 2, Network 2, System 1 free
  at 2×3). A seventh entry in any menu fails the geometry assertions rather
  than scrolling out of reach, which is deliberate: raise `rows`, shrink the
  tiles, or regroup.
- **The GitHub asset ceiling is 2 GiB and the bundle is 1.46 GiB** (73%). The
  payload generator warns at 90% and fails at the limit, so this surfaces on
  the build host — but it is a real constraint on how much the rootfs can grow.
- **Remaining plan work** is beyond Stage 4: the factory payload (`MP_FACTORY`),
  the Pi 3 / Pi 5 board matrix, and Sprint 6 release machinery, which now has
  both an `http://` hard gate and the asset-size gate to fold in.

## Review disposition

Fable's reviews are addressed **through v7**. All findings from v4, v5, v6 and
v7 are closed; v5, v6 and v7 each found no defects in the code, only doc-sync
gaps and notes. The per-finding detail lives in the commits and in plan §8
rather than being re-narrated here, so this file stays about the *current*
state.

Two things from those rounds worth carrying forward, because they are habits
rather than fixes:

- **Verify a finding before acting on it.** The journald log-tag finding (v6)
  was confirmed by logging a bracketed tag and reading the journal back as
  JSON, which showed the bracket really is discarded — and the fix was then to
  delete the machinery that only looked useful, not to elaborate it.
- **Corrections run in both directions.** Two context errors in the v5 review
  were corrected from evidence, and two of this project's own claims were
  retracted the same way: that the bench card had been persistently patched
  (it had not — the root is an overlay and on-device edits are tmpfs-volatile),
  and that a reflash would move the DHCP lease (it does not — the lease is
  keyed on the MAC).

## Working agreements

- **The session's last commit is the handover note.**
- **Both test gates pass before an image build that touches what they cover** —
  the engine's `run-tests.sh` and the application's `ctest`.

Both recorded in plan §8.
