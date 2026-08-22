# MicroPanel Touch handover

**Last updated:** 2026-08-22 (opus)

**This is the only handover note.** It is rewritten in place at the end of
every session rather than versioned — fourteen numbered predecessors were
consolidated into this file on 2026-08-19, and they remain in git history if
an earlier bench state is ever needed (`git log --follow -- docs/`). Keeping
one current file removes the failure mode that produced them: a reader
picking up a stale note and acting on a bench state that no longer exists.

**For the exact shell commands** — building an image, cutting an update
payload, publishing a GitHub release, flashing a card, rehearsing an update
without publishing, and the two-minute deploy loop — see
[`build-release-update-commands.md`](build-release-update-commands.md).

Read, in order: this note →
[`micropanel-touch-plan.md`](micropanel-touch-plan.md) §0.0 (the base-features
milestone: the owner's five decisions, the work order, and the per-image bench
acceptance records) → `misc-tools/board-configs/micropanel-touch/BUILD.md`
("The image diet — measured") →
[`pi-in-system-update-plan.md`](pi-in-system-update-plan.md) §8 if you need the
A/B acceptance history.

## Where the milestone stands

**All five base-1.0 slices are implemented, and `00.47` is published, installed
and bench-checked.** The gate (decision 4) is WiFi join, System Stats, About,
reboot/shutdown, and the iPerf bandwidth test.

| Slice | State |
|---|---|
| (a) WiFi hotspot join | Accepted on hardware, credential redaction audited on the device |
| (b) System Stats | Implemented and tested; **the screen has never been looked at** |
| (c) About / version | Implemented and tested; **the screen has never been looked at** |
| (d) Reboot + shutdown | Reboot accepted on hardware; **shutdown has not been pressed** |
| (e) iPerf3 diagnostics | Accepted on hardware, including a **cross-panel run** — see below |

What remains before calling base 1.0 done: the two unseen screens, shutdown,
and the owner's decision on whether the cross-panel iPerf run already satisfies
the two-panel acceptance topology.

## Bench state

Pi 4 + Luckfox CTP at the address and credentials given in the session. **Do
not put either in a committed file — this repository is public.**

- **The panel runs `00.49`, committed**, installed by the owner over the air
  and factory-reset afterwards as the acceptance test. App revision
  `154679a`; the engine is `pi-ab-update` `95ab3f7`.
- `00.47` was the first release the panel fetched itself from its own Software
  Update screen; `00.49` went the same way and was then factory-reset as its
  acceptance test.
- **`00.23`–`00.49` are burned identifiers. Start at `00.50`.**
- Payload directories under
  `~/pi-image-workspace/out/micropanel-touch-luckfox-ctp-ab/payloads/` for
  `00.44`–`00.49`. Serve any of them with `ab-serve-release.sh <dir> 8000`.
- The USB stick still holds the older `00.29` bundle — deliberately, as an
  offline-test fixture.
- **A factory reset wipes the WiFi credential and regenerates the SSH host
  keys** — both live on `/data`, and both are the reset working. In practice:
  the network has to be re-joined from the panel afterwards, and
  `ssh-keygen -R <panel>` is needed before reconnecting over SSH.
- Three GitHub releases now exist (`00.47`, `00.48`, `00.49`); `00.49` is
  latest. `00.48` was installed once and silently discarded — see the
  factory-reset defects below — so no device ever ran it.
- Until `00.47` was published the device kept offering `00.39`, which was not
  a bug: 00.39 was genuinely the newest *published* release while the bench
  ran newer images.

The full `00.47` check table is in plan §0.0.1. The short version: no failed
units, the listening set exactly as decision 5 records it, rootfs 765 MiB, the
WiFi keyfile intact across the update at `0600` on `/data`, every network test
run, and `ab-update check` answering `up to date (00.47)`.

## What this session did

Twenty-two commits, in four groups.

1. **The network-testing slice, completed** (`74f1609`…`6ed8bf6`). Network →
   Status became a live per-interface list; Network → Testing arrived as
   pick-a-link-then-test: ping, internet, speed, neighbours, port check, and
   both iPerf roles with mDNS discovery. Every command binds the interface it
   was given, because this panel holds an address on `eth0` *and* `wlan0` on
   one subnet and an unbound test measures whichever link the route table
   prefers and reports it as the answer.
2. **UX work, all of it driven by the owner watching the panel**
   (`ac42998`…`027915b`). Scrollable output, selectable discovered servers, a
   Stop button, tests that outlive their screen, an iPerf run that answers in
   figures with the log a press away, and a progress bar that actually moves.
3. **Fable's v9 review, addressed** (`56078df`, `ca4e154`).
4. **`00.47` built, published and accepted** (`31ac21e`, `47e9c2f`).

**The whole slice added a large feature surface with zero new privileged
surface** — every diagnostic runs unprivileged through one auditable handler,
`handlers/micropanel-touch-net-test`, and the broker was not touched. Keep it
that way.

## What the panel found that nothing else could

This is the section worth reading twice. Every item below passed the full
suite at the time it was wrong.

- **iperf3 full-buffers its stdout into a pipe.** An eight-second run
  delivered *every* progress line at the eighth second, so the bar was created
  and jumped to 100 % in the same instant the summary replaced it. `--forceflush`
  is the fix, and the policy test now asserts it **on the command line**, not
  merely mentioned in a comment — the first version of that guard passed while
  the flag was gone, because it matched the comment explaining it.
- **mawk re-batches a stream even with `fflush()` after every print.** Fixing
  iperf3 changed nothing, because the awk filter downstream held the lines: a
  producer printing one line a second had all four delivered at the fourth.
  The parse moved into the shell. *Anything in a streaming path must be
  measured with timestamps, not inspected for output.*
- **`set -e` plus a kill of an already-dead child exits the handler.** With
  avahi unreachable the address publisher exits at once, and the `kill` meant
  only to tidy up took the whole handler with it — so the iPerf server never
  started and the screen showed nothing at all. Every cleanup `kill`/`rm` now
  ends in `|| true`. Fable's v9 F-2 pointed near this; only running it with
  the bus broken showed how much worse it was.
- **A bare mDNS service publish resolves to a different address per
  interface.** The bench panel answered `.197` over eth0 and `.198` over wlan0
  while iperf3 listened on one of them, so a peer that heard the announcement
  over the other link discovered a server and dialled nothing. The server now
  publishes an address record and points the service at it with `-H`.
- **A panel discovers its own server on its own LAN addresses.** Filtering
  loopback was not enough; every address the panel answers to is filtered now.
- **`lv_obj_get_scroll_bottom()` is negative when the content is shorter than
  the view.** The auto-pin scrolled by its negation and pushed the only line
  of output down past the bottom edge, where it was clipped — which reads as
  text hidden behind the buttons.
- **`clear_screen()` was throwing away state that belongs to the test, not to
  the screen** — the log and the verdict, discarded the moment a background
  test's screen was left.
- **Attaching to a finished run by test+interface alone answered "run it now"
  with the last run's report.** Pressing Start after changing TCP to UDP
  showed the old TCP report and started nothing. The screen now knows *why* it
  was opened; the same fault was live for ping and the port check.

- **Pressing Factory Reset found two defects in an hour**, neither reachable
  any other way. **The first: a reset orphaned the mount it wiped under.** The running system
  bind-mounts `/data/NetworkManager/system-connections` onto `/etc`; the wipe
  deletes that directory and the skeleton makes a new one, so a mount
  established earlier in the same boot keeps pointing at the old, unlinked
  inode. `findmnt` says it plainly — `…/system-connections//deleted`,
  `ino=20 links=0` — and an unlinked directory cannot have files created in
  it, so NetworkManager could not save a profile *at all*, not even as root.
  The panel scanned, listed every access point, accepted the password and
  never joined, with nothing in any log about it. **Every symptom pointed at
  WiFi and none of the cause was in WiFi.** Fixed in `misc-tools`
  (`73f6d8f`): the reset re-establishes whatever the wipe orphaned, and the
  fstab entry is now ordered after the reset so it is not established wrongly
  to begin with.
- **The second was found by the first fix appearing not to work.** The owner
  installed the fixed image, reset immediately, and got the old version back —
  with the defect the update had just fixed. A tryboot candidate gets exactly
  one boot and the reset's own reboot spends it, while the wipe erases the
  record that a candidate was being tried, so the commit service finds nothing
  and the device falls back. **Nothing detected a fault**: the health gate
  never ran, and the update was not rejected but forgotten. The reset request
  now refuses while the state is `candidate-armed`, reporting it with exit 75
  (`EX_TEMPFAIL`) rather than a message — the broker never forwards handler
  output, so a code is the only thing that can select the sentence the panel
  shows. Both fixes are accepted on hardware in `00.49`.
- **A reboot hides both of them.** It re-establishes the mount and it ends any
  trial, so any test that reboots before checking cannot tell a fixed image
  from a broken one. The acceptance test is: reset, then join Wi-Fi *without*
  rebooting.

And one about method, from this session, worth keeping:

> I told the owner the progress bar "moves from the first second". I had
> verified that the handler *emitted* progress lines — not *when they
> arrived*. The owner said they saw no bar, and they were right. **Verifying
> that output exists is not verifying that it is timely.**

## What is not verified — read this before claiming anything works

- **System Stats and About have never been read on the panel.** Their inputs
  were checked on the device — the image manifest carries the pinned app and
  LVGL revisions, and `/run/micropanel-touch-update/check` now says
  `state=up-to-date version=00.47`, which is what About reads — but nobody has
  looked at either screen. These are two of the four gate screens.
- **Shutdown has not been pressed**, nor the arm-the-other case (arm Restart,
  then press Shut down) that the headless test covers. Reboot *is* accepted on
  hardware — the one path no fixture can ever run, because invoking it would
  restart whatever runs the test.
- **Forget has not been pressed**, and neither has a join to an open network.
- **The landscape boot profile does not exist.** Both `tools/enable-*.sh`
  hard-code portrait. The no-scroll property is asserted at 480×320 in a
  headless display, which says the screens *would* fit; it says nothing about
  a real panel in landscape, and the landscape touch mapping is unverified.
- **The iPerf figures measured on this panel alone are meaningless** — around
  9–20 Gbit/s, which is the local stack, because one panel was wearing both
  roles. The owner's cross-panel run is the real one: panel 1 served on eth0,
  the second panel discovered `.197` and connected. Whether that satisfies
  decision 2's two-panel acceptance topology is the owner's call.
- **The regulatory domain is `00` (world)**, because no WiFi country has ever
  been set. 5 GHz channels are conventionally passive-scan-only under it, so a
  5 GHz access point is visible but not joinable. **Test with 2.4 GHz.** Left
  open deliberately: which country the panel transmits under is a deployment
  and regulatory decision, and BUILD.md carries the options.

## The fast loop: `tools/cross-build.sh`

*Full instructions in
[`build-release-update-commands.md`](build-release-update-commands.md) §0.5.*

```sh
SSHPASS=<panel password> tools/cross-build.sh --deploy pi@<panel>
```

~2 minutes cold, seconds incrementally, against ~40 minutes for a full image
build. It builds inside a **qemu chroot on the base-stage image**, not with a
host cross-toolchain: the host's aarch64 GCC brings its own glibc and
libstdc++, both newer than the image's, and a binary linked against them fails
on the device in ways that look like application bugs. (That route was tried
and abandoned. No toolchain file is kept for it — a build file that does not
build is worse than this note.)

Handlers and screen configs are **data**: pushed on every deploy, because a
stale handler beside a fresh binary is a confusing way to spend an evening.

**The deploy lives in the panel's tmpfs upper layer and dies at the next
reboot** — including a reboot from System → Power. That is the right lifetime
for a test build, and it has a sharp edge: the panel silently reverts to the
installed image, which reads as a regression in whatever you just changed. The
deploy prints the image version, the binary hash and the **uptime** for exactly
this reason. *If something you just deployed seems to have vanished, read the
uptime before reading the code.*

## Releasing

The whole chain is proven now, so follow it rather than improvising:

1. Both test gates green: the engine's
   `misc-tools/packages/pi-ab-update/tests/run-tests.sh` (10 suites) and the
   application's ctest (**56 tests**, ~15 s).
2. Push. `build-image.sh` clones from GitHub, so an unpushed commit cannot
   reach an image.
3. Build pinned to the commit, not to a branch:
   `--app-revision=<40-char sha> --payload`.
4. Verify the manifest signature locally before publishing. **The signing key
   on the build host must match the key pinned in the running image** —
   compare `/etc/micropanel-touch/release-signing/ed25519-release.key.pub`
   against `/usr/lib/pi-ab-update/update-signing-key.pub` on the panel. They
   matched for `00.47` (`676aa6d0…`). If they ever do not, every device will
   refuse the release, which is the system working correctly.
5. `gh release create` — a normal published release, because the device fetches
   `releases/latest/download/`, which skips drafts and pre-releases.
6. Fetch the manifest and signature back **from the release URL** and verify
   them there before touching a device.

A base-stage rebuild is triggered by `runtime-deps.txt` changing, not only by
app code — that is why `00.47` took ~40 minutes rather than ~20, and it is
correct: `nc` is a *declared* dependency from `00.47` onward rather than
something stock Pi OS happened to ship.

## Working agreements

- **Commit to `main`.** The owner has historically pushed; this session the
  owner asked for pushes so the build could see the code, and they were made
  and reported each time. Ask if it is not obvious.
- **Both test gates before an image build that touches what they cover.**
- **Every published payload gets one bench acceptance before release**, and
  every slice's acceptance is recorded in plan §0.0.1 the same day.
- **The session's last commit is the handover-note update.**
- **Findings get closed the way W-1 was closed**: measure the behaviour on the
  bench, tabulate what each case actually does, then fix — not fix, then
  assert.
- **Break a new assertion before trusting it.** Every property added this
  session was checked by making the code wrong and watching the test fail.
  Two of them did not fire on the first attempt, and both times the *test* was
  the thing that was wrong.

## Open, carried forward

- **The stable release** — work item 4 of the milestone. Blocked only on the
  unverified screens above and the owner's call on the iPerf acceptance.
- **The landscape boot profile**, as above.
- **Menu headroom.** System is a *full* 2×4 grid — eight tiles, no free slot.
  The next System entry needs `rows: 5` or a regroup; the geometry test will
  fail rather than scroll, which is deliberate.
- **The intermittent fixture failure is still unexplained.**
  `test_ab_layout_integration.sh` has failed twice inside a full
  `run-tests.sh` pass and never once standalone; it has not recurred. The ERR
  trap that reports the failing line and the loop-device table is still in
  place, so the next occurrence will say where.
- **An IPv6-only mDNS peer is invisible to discovery.** The legacy panel was
  seen announcing `_iperf3._tcp` over IPv6 only at one point; the whole
  feature is IPv4 end to end. Flagged, not fixed — it widens the slice.
- **The iperf3 column layout is pinned to 3.18** in a handler comment beside
  the parse, asserted by the policy test. A package bump that shifts a column
  must come past a person: the verdict would stay correct (it comes from the
  exit status) but the figures would go quietly wrong.
- The verifier gap and the DHCP-lease/overlay corrections from earlier
  sessions stand as recorded; nothing this session contradicted them.

## Review disposition

**Fable's reviews are addressed through v9.** v9 gated the `00.47` build and
raised one posture item plus three minor findings; all are closed.

- **F-1 — avahi-daemon is an always-on network service.** Now decision 5 in
  plan §0.0, **confirmed by the owner** on 2026-08-21: the image runs an mDNS
  responder, deliberately, because zero-typing discovery only works if the
  responder is up before the other panel looks. The exposure is recorded as
  measured rather than described — `udp/5353` both families, avahi's two
  ephemeral query sockets, `tcp/22`, and nothing else — and re-confirmed on
  `00.47`. The Sprint 6 release audit gains a listening-services assertion so
  the *next* dependency that ships a daemon is caught at review.
- **F-2 — the fallback announce could claim success with avahi down.** Fixed,
  and it was worse than described (see the `set -e` item above). Verified with
  the bus made unreachable: `Not announced - dial <address>:<port> directly`,
  and the server still listens.
- **F-3 — the server has no confirm, against the kickoff wording.** Not drift:
  the owner amended that decision on the bench. Recorded in decision 2 — the
  flood keeps its double-confirm, the server keeps the warning and loses the
  confirm.
- **F-4 — nits.** The speed test stages through `mktemp` in the HMI's own
  runtime directory (mode 700) instead of a fixed name in shared `/tmp`; the
  external endpoints the internet and speed checks depend on are named in the
  plan with what an air-gapped network sees from each; the iperf3 version is
  pinned as above.

Two habits from earlier reviews earned their keep again and are worth keeping:

- **Verify a finding before acting on it.** A runtime-dependency guard once
  reported `libssl3` as removed when apt had merely resolved it to
  `libssl3t64`. The fix was to make the guard resolve `Provides` — not to
  change the dependency list, which is what a reader who trusted the message
  would have done.
- **Corrections run in both directions.** The diet was expected to land at
  0.7–1.0 GiB on the assumption that the weight was ours to trim. Measuring a
  pristine Lite rootfs showed it was not, and the free-space pass — which
  removes nothing — turned out to be worth 40 % of the win on its own.
