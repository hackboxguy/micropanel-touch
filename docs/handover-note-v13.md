# MicroPanel Touch handover — v13

**Prepared:** 2026-08-19 (opus)
**Supersedes:** [`handover-note-v12.md`](handover-note-v12.md) as the current
restart point. V12 remains the record of the post-Stage-3 state.

Read, in order: this note →
[`pi-in-system-update-plan.md`](pi-in-system-update-plan.md) (§8 "Stage 4" for
what was decided and why) → `misc-tools/packages/pi-ab-update/README.md`
("About update authenticity") → `misc-tools/board-configs/micropanel-touch/BUILD.md`
("Where releases come from (OTA)" — including the bench rehearsal procedure).

## Where the work stands

**Stages 0–3 are complete and hardware-accepted. Stage 4 is code-complete and
fully covered by host tests, but has not been on hardware at all.** No image
has been built from it yet.

The reason is mundane and is the first thing to fix: **the image build clones
the application from its remote**, and this session's application commits
(`01c5791`, `b3225ad`) plus the earlier `914ec67`, `ff0887d` are **committed to
main but not pushed**. Nothing can be built until they are.

## What Stage 4 does

**One stage, as planned: signature verification and the network source
together.**

- The **manifest signature is now mandatory on every route, USB included**, and
  is verified *before a single manifest field is parsed*. An unsigned,
  foreign-signed or post-signing-edited release never reaches the parser. This
  needed no migration release: the build side has signed since Stage 2b, so
  every already-published release verifies.
- **Verification is clock-independent by construction.** Raw ed25519 over the
  manifest against a key pinned in the image — no X.509, no certificate, no
  validity window. A panel that has never reached an NTP server and believes it
  is years in the past still verifies and installs a signed release from USB.
  **This is the mechanism the offline deployment depends on**, and a test
  refuses any `x509`/`-CAfile` usage in the engine so it cannot regress
  quietly.
- The **`ota` source** streams `curl | reader` — no staging in RAM or `/tmp`,
  the inactive slot remains the only staging area, and an interrupted download
  leaves the same dirty unlabelled slot an interrupted USB write does.
- **`ab-update-check`** fetches the manifest and its signature *only*, verifies
  before reading any field, and publishes `available` / `up-to-date`. Finding
  out you are current never costs a payload download.
- **Signed downgrades are permitted.** Only an identical version is refused, so
  an older signed release stays installable and rollback stays available. The
  check reports an older offer as `available`.
- **A clock failure is a distinct refusal from a network failure.** A TLS
  authentication failure that coincides with an unsynchronized clock is
  reported as `clock`, and the UI says to set the time *or use the USB route*.
  This closes the F-04 design input from the v3 review.
- The panel's **Software Update** screen now offers both routes and names the
  difference; an offer becomes an **Update now** button, and the offer is
  re-tested when that button is pressed rather than trusted from the button's
  existence.

## The bench sequence that has NOT been run

This is the whole outstanding item. In order:

1. **Push both repos.** Then build `00.35` pointed at this workstation:

   ```sh
   sudo ./build-image.sh --board=micropanel-touch --variant=luckfox-ctp \
       --version=00.35 --layout=ab --app-ref=main \
       --release-url-template=http://<build-host-ip>:8000/@ASSET@
   ```

   Flash it, then build a **second** version as the payload to be offered
   (`00.36 --payload`), and serve that payload directory:

   ```sh
   misc-tools/packages/pi-ab-update/ab-serve-release.sh <payload-dir> 8000
   ```

2. **Check for updates → Update now**, over the LAN. Plain HTTP is a faithful
   rehearsal, not a weakened one: authenticity comes from the pinned key, so an
   unauthenticated server cannot make the panel accept anything it would
   otherwise refuse.
3. **The offline test, which is the one the deployment actually depends on:**
   set the panel's clock deliberately and badly wrong (e.g. `timedatectl
   set-ntp false` then `date -s 2016-01-01`), then do a **signed USB update**.
   It must succeed. If it does not, the offline claim above is wrong and
   everything resting on it needs revisiting.
4. **Negative cases worth one run each:** server unreachable (expect a network
   refusal, not a payload one), and a bundle signed by a foreign key (expect a
   signature refusal).

`00.23`–`00.34` are burned identifiers. **Start at `00.35`.**

## Bench state

Pi 4 + Luckfox CTP at the address and credentials given in the session.
`00.34` on the card, exFAT stick attached. Nothing about the device changed
this session — no image was built.

## What is verified, and what is not

**Verified on this host** (`sudo misc-tools/packages/pi-ab-update/tests/run-tests.sh`,
all nine suites pass):

- The OTA check against a **real local HTTP server**: newer/same/older
  releases, a manifest edited after signing, a foreign signing key, wrong
  variant, wrong board, an unreachable server, and that the check never
  downloads a payload.
- The engine against **real loop partitions**, now including a bundle streamed
  from HTTP, a truncated download, an unreachable server, and a failure raised
  *mid-download*, which must be blamed on neither the transport nor made to
  wait for it.
- That the **real payload generator's** member order matches what the device
  now requires, read back off a generated bundle.
- The broker and privileged-operation suites, extended for the `ota` source and
  the new operation.

**Not verified anywhere yet:** the UI changes. This host cannot build the
application at all — `libgpiod` is not installed and the LVGL submodule is
absent — so `StarterUi.cpp` is compile-checked only by the image build. If you
want the UI and headless-UI suites runnable here, installing `libgpiod` and
`git submodule update --init --recursive` should be enough; that would be worth
doing before the next UI change.

## Two notes for whoever runs this next

- **An intermittent test failure was seen once and not explained.** The layout
  loopback fixture failed once inside a full `run-tests.sh` run, then passed
  standalone and on every re-run since (five or more). It is most likely loop
  device contention right after the handler fixture releases its own, but that
  was not proven. If it recurs, that is the thread to pull.
- **A board overriding `AB_CURL` should point it at a single process.** The
  engine stops a download by signalling that one process; a wrapper script that
  lingers as a parent can leave a child holding the engine's lock. This was
  found by a test fixture doing exactly that, and is now documented in the
  engine README.

## Working agreement (unchanged, still in force)

**The session's last commit is the handover note.** Recorded in plan §8.
