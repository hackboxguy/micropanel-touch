# MicroPanel Touch handover

**Last updated:** 2026-08-20 (opus)

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
[`micropanel-touch-plan.md`](micropanel-touch-plan.md) §0.0 (the base-features
milestone: the owner's decisions and the work order this session ran to) →
`misc-tools/board-configs/micropanel-touch/BUILD.md` ("The image diet —
measured") → [`pi-in-system-update-plan.md`](pi-in-system-update-plan.md) §8
if you need the A/B acceptance history.

## What this session did

The base-features milestone, in the handover's order. Four of the five feature
slices have landed as code and tests. **None of the feature work has been on a
panel yet** — see "What is not verified" below, which is the most important
section of this note.

1. **The decisions are recorded.** Plan §0.0 carries the priority pivot
   (legacy-config parity deferred, images stay fragmented per use case), the
   four owner decisions, and the work order. PRD risk 7 changed from an open
   Phase-4 question to a recorded decision; the update plan's §11 inherits the
   other half of it.
2. **The image diet, measured and landed** — the headline result of the
   session.
3. **The no-scroll property now runs in both geometries.** The 480×320 boot
   profile itself is still open.
4. **(a) Wi-Fi join, (b) System Stats, (c) About, (d) Reboot/shutdown** are
   implemented, tested, and committed.
5. **(e) iperf3 diagnostics is not started.**

## The image diet: 1.46 GiB → 0.21 GiB

The bundle was 73 % of two independent 2 GiB ceilings. It is now **10.7 %** —
229,580,800 bytes. Two changes produced it, and the smaller-looking one
returned more than expected:

- **The payload was carrying the slot's freed blocks.** `ab-make-payload.sh`
  streams the whole 5 GiB slot partition through `xz`, so every block the
  build wrote and then deleted travelled into the release compressing like the
  data it used to be. The A/B finalizer now zeros the slot's free space before
  sealing. That alone took the compressed rootfs from 1.40 GiB to 0.52 GiB,
  **while removing nothing from the running system**.
- **Stock Pi OS Lite, declined in part.** The measurement that shaped this is
  worth carrying forward because it is not what one would guess: a *pristine*
  Lite rootfs is 1860 MiB and this appliance's was 1958 MiB. Almost none of
  the weight was ours. Rootfs 1958 → 764 MiB.

Full numbers, the per-entry reasoning, and the option measured but *not* taken
(the Python 3 stack, ~180 MiB, which takes cloud-init and netplan with it) are
in BUILD.md. **Plan §7's open question is answered there too: the 2 GiB
`MP_FACTORY` reservation stands**, for asymmetry rather than headroom.

The gate is the resulting size (`SLIM_MAX_ROOT_MB`), not the removal list,
because a base-image bump can silently stop matching a pinned kernel version
in `slim-remove.txt`. A missed removal then fails the build rather than
quietly producing a fatter release.

## Bench state

Pi 4 + Luckfox CTP at the address and credentials given in the session.

- **The panel runs `00.41` on slot B, committed.** It was installed over the
  A/B chain from the local payload — not flashed — so the dieted bundle went
  through the real update path as well as delivering the features. Slot A
  retains `00.39` as the rollback target, which is a better one than the
  `00.36` it replaced. The full acceptance table is in plan §0.0.1.
- **The Wi-Fi radio is currently on, and will not stay that way.** It was
  enabled by hand so the panel can be tested now (`sudo nmcli radio wifi on`);
  the next reboot puts it back to disabled. The fix is committed but has not
  been through an image build — see below.
- `00.40` is the diet image and its payload, built from the **published**
  `00.39` application revision — it is the size measurement, with no feature
  changes in it.
- **The panel runs `00.43` on slot B, committed** — `00.42` plus the three
  defects the owner found on the panel (untappable network rows, the summary
  wrapping onto the first row, and missing glyphs drawing as filled boxes).
  Radio enabled on its own, no failed units, scan returns networks on both
  bands. `00.42` on slot A is the rollback. Its application revision
  `c316625` is **local** until the owner pushes.
- Previously: **`00.42` on slot A** — the diet, the four feature
  slices, and the Wi-Fi radio fix. Built from the *pushed* remote (`82663e0`),
  installed over the A/B chain, and confirmed on a fresh boot with nothing
  touched by hand: `nmcli radio wifi` reports **enabled**, `wlan0` is
  **disconnected** rather than unavailable, and a scan returns eight access
  points. `00.41` on slot B is the rollback.
- `00.41` was the previous step: identical application behaviour, but the radio
  boots off. Built, layout-verified, and its payload signed and verified
  (230,195,200 bytes — 10.7 % of the 2 GiB ceilings). Its manifest records
  application revision `d6881ab`, which is a **local** commit: it was built
  from a bare mirror served over HTTP on the build host, because that commit
  has not been pushed. It becomes findable the moment the owner pushes.
- **`00.23`–`00.43` are burned identifiers. Start at `00.44`.**
- Payload directories under
  `~/pi-image-workspace/out/micropanel-touch-luckfox-ctp-ab/payloads/` for
  `00.30`, `00.35`, `00.36`, `00.39`, `00.40` and `00.41`. Serve any of them
  with `ab-serve-release.sh <dir> 8000`.
- The USB stick still holds the older `00.29` bundle — deliberately, as an
  offline-test fixture.

## The one defect the bench found

Stock Pi OS Lite ships `/var/lib/NetworkManager/NetworkManager.state` with
`WirelessEnabled=false`. On this image that file is in the **read-only lower
root**, so `nmcli radio wifi on` writes to the tmpfs upper layer and is
forgotten at the next boot — `phy0` comes up soft-blocked every time. The
panel's Network → Wi-Fi screen therefore reports the radio state instead of a
network list, on every boot, **with no on-screen way out of it**: the join
path this session added is correct and completely unreachable.

No fixture could have caught it. The file is stock content the build never
touched, and the symptom only exists on a real boot of a read-only root.

The app hook now bakes `WirelessEnabled=true` and the static contract asserts
the line. The file is **not** package-owned (`dpkg -S` finds no match), so the
imager's runtime-package re-assert cannot restore the stock value after the
hook writes it — checked before trusting the fix, and confirmed again by
reading `WirelessEnabled=true` out of the finished `00.42` image.

The owner reproduced the defect independently the same day: after restarting
from the Power screen, the Wi-Fi screen reported the radio unavailable again.

**The radio fix is confirmed on hardware** (`00.42`, fresh boot, no shell
involved). **A second thing will bite a Wi-Fi test, and it is not fixed.** The panel's
regulatory domain is `00` (world), because no Wi-Fi country has ever been set.
Under the world domain 5 GHz channels are conventionally passive-scan-only, so
a 5 GHz access point is visible but not joinable. **Test with a 2.4 GHz
network.** This is deliberately left open rather than guessed at in the build:
which country the panel transmits under is a deployment and regulatory
decision. BUILD.md carries the options. What *is* confirmed is that the diet
left `wpasupplicant` in place, so WPA association has what it needs.

## What the panel found that nothing else could

Three defects, all reported by the owner from the screen, all of which the full
suite passed at the time:

- **The Wi-Fi rows were visible but untappable**, because the 50 ms event drain
  rebuilt them unconditionally and deleted the button under the finger before
  the release could land. The general lesson is worth keeping: *a synthetic tap
  is faster than a refresh, and a person is not*. Any fixture that presses and
  releases in one pass is blind to this whole class of defect.
- **A wrapped summary label drew on top of the first row.** Headless geometry
  checks assert that controls fit and do not scroll; they say nothing about two
  things occupying the same pixels.
- **Missing glyphs drew as filled boxes**, in fourteen strings — nine of them
  older than this milestone, including every software-update progress message.
  There is now a test that asks LVGL, on every screen in both geometries,
  whether it can draw every character; it is the only reason the pre-existing
  nine were found.

## What is not verified — read this before claiming anything works

**Two of the four gate screens have now been seen; the other two have not.**
The owner rendered the Wi-Fi network list on the panel, and restarted the panel
from System → Power — which is worth noting for a specific reason: the accepted
`reboot` action is the one path no fixture can ever run, because invoking it
would restart whatever is running the test. Every test only exercises the
handler's refusals, so the bench closed the structurally-unreachable half
first.

**System Stats and About have not been read on the panel, and no Wi-Fi network
has been joined.** The milestone is described as *polish*, and the polish half is judged
on the physical screen; that judgement has not happened. Everything below is
asserted by fixtures, by on-device shell checks, or by nothing else:

- **The Wi-Fi list renders on the panel** — the owner saw it, with the radio
  manually enabled. But nothing has been *joined*: the keyfile the handler
  writes is exercised against a stand-in `nmcli`, so what is tested is the
  file's contents, its mode, and that the secret never reaches an argument
  vector — not that NetworkManager accepts it.
- **Restart is confirmed on the panel** — the owner used System → Power to
  reboot it. Shut down has not been pressed, and neither has the arm-the-other
  case (arm Restart, then press Shut down) that the headless test covers.
  The handler's refusals were also run on the device (`""`, `poweroff`,
  `halt`, `REBOOT` all rejected with rc 64 and the contract message).
- System Stats and About were checked against the running device's real files
  and agree with `uptime`, `free -m`, the thermal zone and `ab-update status`
  field for field — but through the readers on the build host, not through the
  screens.

What the diet *is* verified on: `00.41` boots clean with no failed units, the
committed slot boots in **16.6 s** (no regression against the ~18 s recorded
before the diet — the 44.6 s first boot was the candidate's 30-second commit
window, by design), the runtime tools all survived, and the onboard Wi-Fi
firmware still loads (`brcmfmac43455-sdio`, `7.45.265`), so the firmware trim
did not take the radio with it.
- **The landscape boot profile does not exist.** Both `tools/enable-*.sh`
  hard-code portrait. The no-scroll property is asserted at 480×320 in a
  headless display, which says the screens *would* fit — it says nothing about
  a real panel in landscape, and the landscape touch mapping is unverified.

**One structural obstacle worth knowing about.** The working agreement is that
sessions commit to `main` and the owner pushes; the image build clones from
the remote. A commit that has not been pushed therefore cannot reach an image
at all, which is why none of this could be bench-tested as it was written.
`build-image.sh --app-repo=URL` now closes that gap. It is for testing only —
a release build must use the pinned repository, because the manifest records
the revision either way but only the pinned repository makes it findable.

To rebuild from local commits, serve a bare mirror on the build host and point
the build at it:

```sh
MIRROR=~/pi-image-workspace/tmp/git-mirror
rm -rf "$MIRROR" && mkdir -p "$MIRROR"
git clone --bare /path/to/micropanel-touch "$MIRROR/micropanel-touch.git"
git -C "$MIRROR/micropanel-touch.git" update-server-info
(cd "$MIRROR" && python3 -m http.server 8765 --bind 127.0.0.1 &)

cd /path/to/misc-tools
sudo ./build-image.sh --board=micropanel-touch --variant=luckfox-ctp \
    --version=<VER> --layout=ab --payload \
    --app-repo=http://127.0.0.1:8765/micropanel-touch.git --app-ref=main
```

The chroot shares the host's network namespace, so `127.0.0.1` reaches the
build host's own loopback. The submodule still comes from GitHub.

## How to test `00.41` on the panel

Flash it:

```sh
sudo ./build-image.sh --board=micropanel-touch --variant=luckfox-ctp \
    --flash=/dev/sdX --version=00.41 --layout=ab
```

or write the built image directly — it is at
`~/pi-image-workspace/out/micropanel-touch-luckfox-ctp-ab/2025-10-01-raspios-trixie-arm64-lite-micropanel-touch-luckfox-ctp-ab-00.41.img`.
A fresh flash gives slot A populated, slot B reserved and empty, and no update
history.

The alternative that needs no card swap is an A/B install of the payload,
which also exercises the dieted bundle end to end:

```sh
scp ~/pi-image-workspace/out/micropanel-touch-luckfox-ctp-ab/payloads/00.41/\
micropanel-touch-luckfox-ctp.mpupdate pi@<panel>:/tmp/
ssh pi@<panel> 'sudo ab-update install --file=/tmp/micropanel-touch-luckfox-ctp.mpupdate'
```

That writes into slot B — **which currently holds the `00.36` rollback
target** — boots the candidate once, and commits after 30 seconds of health.
If it is unhealthy it falls back to `00.39` on its own.

**What to look at, in the order the gate cares about** (plan §0.0, decision
4). Every one of these is a first look at a screen no human has seen:

1. **System → System Stats.** Do the five rows update, and do they look
   plausible against `uptime` and `vcgencmd measure_temp` over SSH? The CPU
   row should say "measuring…" for the first half-second and then a
   percentage.
2. **System → About.** Does it agree with `ab-update status` over SSH? If the
   two disagree, About is wrong by construction — it is supposed to read the
   same files.
3. **System → Power.** Restart, twice, and check that one press only arms.
   Then arm Restart and press Shut down: it must not shut the panel down.
4. **Network → WiFi.** Does the scan list render as tappable rows, and does
   joining a real hotspot work? Watch `journalctl -f` on the panel for the
   passphrase — it must not appear.
5. **Geometry.** Every new screen is asserted to fit 480×320 headlessly, but
   the panel boots portrait, so what you can judge today is the portrait look.

## Working agreements

- **Commit to `main`, do not push.** The owner pushes and publishes releases.
- **Both test gates before an image build that touches what they cover** — the
  engine's `misc-tools/packages/pi-ab-update/tests/run-tests.sh` (13 suites)
  and the application's ctest (**51 tests**, ~14 seconds). Both were green
  before `00.41`.
- **Every published payload gets one bench boot acceptance before release**,
  and every slice's acceptance is recorded in plan §0.0 the same day.
- **The session's last commit is the handover-note update.**

## Open, carried forward

- **The intermittent fixture failure is still unexplained.**
  `test_ab_layout_integration.sh` has failed twice inside a full `run-tests.sh`
  pass and never once standalone. It did not recur this session across several
  full passes and several standalone runs. The ERR trap that reports the
  failing line and the loop-device table is still in place, so the next
  occurrence will say where.
- **Menu headroom.** System is now a *full* 2×4 grid — eight tiles, no free
  slot. The next System entry needs `rows: 5` or a regroup; the geometry test
  will fail rather than scroll, which is deliberate. Network has two free
  slots at 2×3, which is where iperf3 goes.
- **Slice (e), iperf3, is not started.** Owner decision 2 is both roles:
  client bandwidth test, bounded-duration UDP flood, and server mode, with the
  DHCP-server-style double-confirm on the flood and server modes. Nothing has
  been written for it, and `iperf3` is not in `runtime-deps.txt` yet.
- **The landscape boot profile**, as above.
- The verifier gap and the DHCP-lease/overlay corrections from earlier
  sessions stand as recorded; nothing this session contradicted them.

## Review disposition

Fable's reviews are addressed **through v7**; nothing new arrived this
session. The two habits worth carrying forward are unchanged, and both earned
their keep again here:

- **Verify a finding before acting on it.** The runtime-dependency guard
  reported `libssl3` as removed by the purge cascade. It had not been: apt
  resolves that name to `libssl3t64`, and dpkg only ever hears the latter. The
  fix was to make the guard resolve `Provides`, not to change the dependency
  list — which is what a reader who trusted the message would have done.
- **Corrections run in both directions.** The expected landing zone for the
  diet was 0.7–1.0 GiB, on the assumption that the weight was ours to trim.
  Measuring a pristine Lite rootfs showed it was not, and the free-space pass
  — which removes nothing — turned out to be worth 40 % of the win on its own.
