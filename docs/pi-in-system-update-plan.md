# Pi in-system A/B update plan — micropanel-touch appliance

**Prepared:** 2026-08-15 (fable, from owner decisions of the same date)
**Status:** Stages 0–2c are complete and hardware-accepted on the Pi 4 +
Luckfox CTP bench unit, including the V4-hardened build's normal-update
regression (`00.23`→`00.24`, handover v8) and an independent live
verification of the concurrency lock, mid-write interrupt cleanup, and
interrupted-slot retry (2026-08-18). **Stage 2b** (single-file `.mpupdate`
bundle + zero-preparation USB, per
[`fable-ota-usb-simplification-proposal.md`](fable-ota-usb-simplification-proposal.md))
is **complete and hardware-accepted on 2026-08-19**, as are its review fixes
and **Stage 2c** (the engine extraction into `pi-ab-update`) — see §8 and
[`handover-note-v11.md`](handover-note-v11.md). The approved next work is
**Stage 3** (factory reset, built in the engine), then
**Stage 4.1+4.2 as one stage**
(GitHub OTA together with signing — OTA must not ship unsigned).
**Companion docs:** `misc-tools/board-configs/micropanel-touch/PERSISTENCE.md`
(the /data contract this plan builds on), `docs/micropanel-touch-plan.md`
(sprint context), `misc-tools/board-configs/micropanel-touch/BUILD.md`.

## 1. Goal

The SD card is updated **in-system** (no card removal): the running
appliance writes a new OS image into the inactive slot, reboots into it
once via the Raspberry Pi `tryboot` mechanism, and commits only after the
new system proves healthy. A failed update falls back automatically.
`/data` (the only durable state, per PERSISTENCE.md) is never part of an
update. A menu-triggered **factory reset** returns the device to
fresh-system behavior.

## 2. Owner decisions (recorded 2026-08-15)

| Decision | Value |
|---|---|
| Mechanism | Native Pi `tryboot` + `os_prefix` slot directories; **no U-Boot, no third-party updater**. Small custom updater in the established typed-broker / fixed-argv-handler pattern. |
| Minimum SD card | **16 GB** (breaking partition-layout change accepted). |
| Minimum RAM | **2 GB hard requirement; 1 GB desired.** This forbids RAM-staged payloads — see §5 streaming design. |
| Hardware scope | Pi 4 and Pi 5 are the intended scope; Pi 3 is desired — same mechanism (`tryboot.txt` is firmware-side and model-independent, unlike `autoboot.txt` which is Pi 4/5-only). Verify Pi 3 in Stage 0. **Current Stage 1 manifest allow-list is Pi 4 only**: Pi 5/RP1 and Pi 3 are not accepted until their own hardware evidence exists. **Pre-approved (owner, 2026-08-15): if the Pi 3 `tryboot.txt` spike is shaky, drop Pi 3 from A/B scope and standardize on the `autoboot.txt` dual-boot-partition layout for Pi 4 / CM4 / Pi 5** — one selector backend, aligned with the §11 secure-boot path. |
| Delivery | **Stage 1: USB stick / local file.** Later: HTTPS pull with `curl` from a GitHub-hosted artifact, streamed (see §5). |
| Signing | **Deferred** — hash-only integrity first; detached-signature verification added in a later stage without changing the payload flow. |
| Breakage tolerance | Prototype phase, single bench unit: fresh-reflash on layout or format changes is acceptable. No migration tooling required yet. |
| Sequencing | **Minimal foundation first** (partition layout + A/B scaffold + dependencies), then resume micropanel-touch menu/feature work; update-system refinement (HTTP, signing, factory image) proceeds in parallel. |
| Factory reset | v1 = **data reset** (wipe `/data`, PIN-gated when screen lock is enabled, marker + early-boot wipe). Partition space is **reserved now** for a later true factory-image restore. |
| Post-Stage-2 sequencing | **Owner, 2026-08-18:** single-file `.mpupdate` bundle + zero-preparation USB (Stage 2b) lands before Stage 3 factory reset; GitHub OTA is not urgent and is combined with signing into one later stage (4.1+4.2) — OTA does not ship unsigned. Stage 2b bakes in the OTA-forward groundwork so OTA needs no format or reader rework. |

## 3. Assumptions and open points (proposed defaults — override before Stage 1 if wrong)

1. **GitHub Releases assets, not LFS**, for the HTTP stage —
   **confirmed by owner 2026-08-15**. LFS free bandwidth is 1 GB/month
   (already bitten once in `media-files`); Release assets are quota-free
   plain HTTPS and work with streaming `curl`. The HTTP flow is
   user-triggered: a menu action fetches the latest release's small
   manifest for this image type (variant + boards), compares `version`
   against the running slot's manifest, reports "up to date" or offers
   the update, and only then streams the payload (§5).
2. **Slot B ships empty** in the initial image (small flashable image;
   the first update populates B). Fallback protection therefore begins
   after the first successful update — acceptable for the prototype.
3. **Slot sizes are frozen at first flash** (the least reversible number
   in this design). Proposed split for a "16 GB" card (~14.8 GiB usable):
   bootA 256 MiB + bootB-reserve 256 MiB, rootA 5 GiB, rootB 5 GiB,
   factory-reserve 2 GiB, `/data` = remainder (~2.2 GiB, up from today's
   512 MiB).
4. Panel **variants stay per-image** (recorded owner decision, v13/v16):
   payloads carry `PANEL_VARIANT` and the updater refuses a mismatch. The
   base PiScreen image and payload both use `piscreen`; the Luckfox hook
   deliberately replaces that with `luckfox-ctp`.
5. Bench acceptance initially on the single Pi 4 unit; Pi 3 / Pi 5 runs
   happen when hardware is on the bench (Pi 5 note: RP1 changes the
   Luckfox PWM/SPI overlay parameters — each board × panel combination
   needs its own acceptance, as per the existing variant philosophy).

## 4. Partition layout (breaking change, Stage 1)

MBR (Pi 3 compatibility rules out GPT; firmware reads only p1):

| Part | Type | Label | Size | Content |
|---|---|---|---|---|
| p1 | FAT32 primary | `MP_BOOT_A` | 256 MiB | Shared firmware + `config.txt` + `tryboot.txt` staging + `A/` and `B/` `os_prefix` directories (each: kernels incl. `kernel8.img` **and** `kernel_2712.img`, initramfs, overlays, `cmdline.txt`) |
| p2 | FAT32 primary | `MP_BOOT_B` | 256 MiB | **Reserved, empty** on SD builds — becomes the second signed-`boot.img` partition in the CM4 secure-boot variant (§11) so that migration renumbers nothing |
| p3 | extended | — | rest | container |
| p4 | *(unused primary slot kept free)* | — | — | — |
| p5 | logical ext4 | `MP_ROOT_A` | 5 GiB | Slot A root (read-only, overlayed as today) |
| p6 | logical ext4 | `MP_ROOT_B` | 5 GiB | Slot B root (empty at first flash) |
| p7 | logical | `MP_FACTORY` | 2 GiB | **Reserved, empty** — later holds the compressed signed factory payload for true factory restore |
| p8 | logical | `MICROPANEL_DATA` | remainder | unchanged `/data` contract per PERSISTENCE.md |

(Roots as logical partitions are unproblematic — the firmware reads only
the FAT primaries; Linux mounts logicals identically. If Stage 0 shows
any firmware oddity with this arrangement, the fallback is roots as
primaries p2/p3 and boot-B reserved space folded into the extended
container instead — same labels, same contract.)

Consequences baked into Stage 1:

- A/B mounts use **LABEL=** references (`/data` already has
  `MICROPANEL_DATA`; the bind mount and every A/B fstab line follow). The
  retained legacy single-slot image intentionally continues to mount `/data`
  by its authored `PARTUUID=`.
- Each slot's `cmdline.txt` names its own root (`root=LABEL=MP_ROOT_A`
  vs `root=LABEL=MP_ROOT_B`, keeping slot images independent of
  partition numbering) plus the existing `overlayroot=tmpfs:recurse=0`
  and console arguments.
- An A/B rootfs has **no** fstab `/` entry. Overlayroot obtains its root from
  the kernel command line; a payload installed into B must never cause fstab
  processing to identify or fsck inactive A.
- Slot selection goes through a **single small selector abstraction**
  (script/module with exactly three operations: `current-slot`,
  `arm-candidate`, `commit`) so the CM4 secure-boot backend (§11) can
  replace the mechanism without touching the updater, commit service, or
  UI. The SD backend keeps a complete normal `config.txt` and a complete
  `tryboot.txt` on p1; `tryboot.txt` is the configuration read for the
  one-shot boot, not an additive fragment. Normal and candidate selections
  both use `os_prefix=<slot>/`, so the updater writes only that target
  directory. The Pi 4 + Luckfox CTP normal-`os_prefix=A/` isolation test passed
  on 2026-08-17 with the real p8 skeleton and an A-only cmdline marker proof;
  the selector has no flat-A compatibility path.
  Each selector update is temp-write plus rename while p1 is temporarily
  remounted `rw`. The tryboot flag itself is one-shot and cleared by the
  firmware on reset — that is the automatic-fallback property.

## 5. Update flow (the streaming rule)

**Hard rule: no payload staging in `/tmp` or anywhere in RAM.** `/tmp` is
tmpfs; a ~1.5 GB payload there breaks the 2 GB floor and kills the 1 GB
wish. The inactive slot partition **is** the staging area — it is safe to
fill with unverified bytes because nothing boots it until verification
passes and tryboot is armed.

```
source (USB file | HTTPS) → xz -d (stream) → /dev/mmcblk0p<inactive>
                          ↘ sha256 computed on the fly
manifest check (version, variant, board-support, sha256 of uncompressed image)
  → only then: write boot files into <slot>/ on p1, write tryboot.txt-staged
    config, arm tryboot, reboot "0 tryboot"
```

- The payload encoder fixes its LZMA2 dictionary at 64 MiB. Its decoder needs
  65 MiB for that format, so the device handler caps `xz -d` at 80 MiB: enough
  for the supported artifact with bounded headroom, and well within 1 GB RAM.
- Verification is **hash-then-arm**: stream once, compare the computed
  digest against the manifest before any boot-selection change. (When
  signing lands, the *manifest* becomes the signed object; the streaming
  path does not change.)
- The release artifact has a deliberately blank primary ext4 volume-label
  field. The updater also clears the inactive partition's first MiB before
  streaming, so an interrupted transfer leaves a dirty **unlabelled** target,
  never a duplicate label for the committed slot. The target is resolved
  structurally from the running MMC root and the fixed layout (A=p5, B=p6),
  rather than by an inactive-slot label that a failed transfer could destroy.
  The streamed rootfs digest is the digest of those label-neutral artifact
  bytes. Immediately after that comparison succeeds, the updater runs
  `e2label <inactive-device> MP_ROOT_<target>` before it writes any selector.
  This makes one slot-neutral rootfs artifact usable in either slot and
  prevents duplicate labels. Because relabeling changes the ext4 superblock,
  a later raw-partition hash must not be compared to `rootfs_sha256`; any
  at-rest verification must use a label-aware scheme defined with the updater.
- USB source: the handler itself enumerates USB-transport block devices,
  mounts each candidate FAT32/exFAT filesystem read-only
  (`ro,nosuid,nodev,noexec`), requires exactly one `*.mpupdate` bundle across
  all of them, and unmounts. No automount daemon is added. (Stage 2b replaced
  the fixed `/dev/disk/by-label/MP_UPDATE` source; see §8 Stage 2b.)
- HTTP source (later stage): `curl --fail --location` streaming the
  Release asset; resume (`--continue-at`) is a refinement, not a
  requirement — a failed transfer just leaves a dirty inactive slot,
  which the next attempt overwrites.
- Boot-file update on the shared FAT touches only the inactive slot's
  directory. `boot.tar` carries a release-specific, slot-neutral
  `cmdline.txt.template`, never a slot-bound `cmdline.txt`. The updater always
  renders `cmdline.txt` from that template, replacing its `root=` token with
  `root=LABEL=MP_ROOT_<target>` and preserving the release's explicit
  `overlayroot=tmpfs:recurse=0`. Update results are therefore deterministic
  and a routine release can evolve cmdline parameters. Shared firmware blobs
  (`start*.elf` on Pi 3/4) are updated rarely and deliberately, never as part
  of a routine payload.

### Health check and commit

A `micropanel-touch-update-commit` oneshot (same idiom as the machine-id
service) runs on every boot: if this boot is a tryboot candidate slot,
require **HMI active + broker active + `/data` mounted rw + first frame
rendered + zero HMI restarts from candidate boot through a sustained N-second
window** (N=30 default), then swap the roles in
`config.txt` and mark the slot manifest committed. Anything less: do
nothing — the next reset falls back automatically.

The durable update state remains root-only. The commit service publishes a
bounded, world-readable runtime summary (`committed`, `candidate-armed`, or
`fallback`) for the HMI; a normal boot that sees an armed but unbooted
candidate records `fallback` before exposing it. During the deliberate
multi-minute update window the broker is serial and other broker operations
wait; this is an accepted appliance policy because the device is about to
reboot and the UI tells the operator not to interrupt power. The UI update
worker joins its one terminal broker reply on service shutdown; that reply has
a 31-minute ceiling for a 30-minute handler. A systemd service-stop timeout is
therefore the final shutdown backstop for an in-flight update. This is accepted
for the attended appliance path: the handler writes only the inactive slot
until arm, and successful updates reboot immediately afterward.

**Enable the hardware watchdog** (`RuntimeWatchdogSec=` in
`/etc/systemd/system.conf`, Stage 1): tryboot fallback only happens on a
*reset*; without a watchdog a hung candidate slot is a brick until power
cycle. This ships with the layout, not with the updater, so every A/B
image has it from day one. This systemd watchdog starts only after PID 1;
it does not by itself cover a candidate that dies before PID 1. The separate
Stage 0 broken-cmdline test below recorded this boundary. The owner has
accepted the manual-power-cycle residual for attended Stage 2 updates; the
operator and release requirements are recorded with that result below.

### Slot identity at runtime

Derived from `/proc/cmdline` root= (`MP_ROOT_A`→A, `MP_ROOT_B`→B) via
the selector abstraction; each slot's
`image-manifest.env` (already produced by the image hook) carries version,
variant, and — new — `SLOT_COMPATIBLE_BOARDS`.

## 6. Payload format (v1, unsigned)

One directory (USB) or one Release (HTTP) containing:

```
micropanel-touch-<version>-<variant>.rootfs.img.xz   # label-neutral rootfs; relabel after stream
micropanel-touch-<version>-<variant>.boot.tar        # os_prefix contents plus slot-neutral cmdline.txt.template
micropanel-touch-<version>-<variant>.manifest        # key=value, SettingsFile grammar
```

Manifest keys: `version`, `variant`, `boards`, `rootfs_sha256`
(uncompressed pre-relabel artifact), `rootfs_bytes`, `boot_sha256`,
`format=1`. The manifest
uses the existing `SettingsFile` strict grammar so the parser is already
written and tested. Signing later = `manifest.sig` (ed25519 via the
already-shipped OpenSSL) + a pinned public key in the image; downgrade
policy (refuse or warn on `version` regression) decided when signing
lands.

`misc-tools/build-image.sh` gains a payload output target that derives all
three artifacts from the same build that produces the flashable image —
one build, two outputs, no drift.

**Format v2 (Stage 2b, implemented 2026-08-18):** the triplet is a build
intermediate; the published artifact is a single streamable `.mpupdate` bundle
plus a standalone copy of the tiny manifest for cheap update checks. Inner
artifacts, hashes, and the manifest grammar are unchanged; the manifest gains
`format=2`. Design and rationale:
[`fable-ota-usb-simplification-proposal.md`](fable-ota-usb-simplification-proposal.md).
The format=1 triplet remains the accepted Stage 2 record and is now rejected
by the device.

The outer container is **ustar**, not pax. `--format=posix` can emit extended
(`x`) header members, which would force the single-pass reader to understand a
second header grammar for no benefit; ustar keeps every member header exactly
one 512-byte block. Its 8 GiB per-member ceiling is far above any slot artifact
and the generator checks it rather than emit a truncated size field. Members
are, in this exact order:

```
micropanel-touch-<variant>.mpupdate
  1. manifest         # SettingsFile grammar + format=2  (mandatory, first)
  2. manifest.sig     # ed25519 over member 1            (optional to the
                      #   device until Stage 4; always published)
  3. boot.tar         # unchanged inner artifact, boot_sha256 in manifest
  4. rootfs.img.xz    # unchanged, LAST — streamed to the inactive slot
```

Published asset names are version-less
(`micropanel-touch-<variant>.mpupdate` / `.manifest`); the version lives inside
the manifest so the Stage 4 `releases/latest/download/` URLs are stable.

## 7. Factory reset (v1: data reset)

- **Trigger:** System-menu entry → PIN required when screen lock is
  configured+enabled (a reset that bypassed the PIN would make the lock
  decorative) → explicit two-step confirm, in the established
  confirm-pattern.
- **Mechanism:** typed broker operation → fixed-argv root handler writes a
  root-owned marker `/data/micropanel-touch-system/factory-reset-requested`
  and reboots. An **early-boot oneshot** (ordered before every `/data`
  consumer, after `data.mount`) sees the marker, recreates the pristine
  `/data` skeleton (exactly what `finalize-image-layout.sh` authors —
  factor that skeleton into a shared script so image build and reset
  cannot drift), and clears the marker. Power-cut mid-reset simply
  retries next boot — the marker survives.
- **Effect:** settings, calibration, screen lock, NM profiles, DHCP-server
  state, SSH host keys, and machine identity are all recreated fresh on
  the following boot by the existing first-boot services. (New machine-id
  and SSH host keys are the *intended* fresh-system semantics — recorded
  here deliberately.)
- **Update state is cleared too** (owner, 2026-08-19): the durable
  `update-state` record lives under `/data`, so the wipe removes it and a reset
  device reports no update history — it looks freshly flashed. Recorded as
  intended semantics rather than a side effect. The *slots* are untouched: the
  device keeps running whichever slot it was on.
- **Trust residual (recorded 2026-08-19).** The PIN gate lives entirely in the
  UI: the broker's factory-reset operation carries nothing and requires nothing
  beyond being the HMI account. Against the *physical* attacker the screen lock
  exists for, that is the right design and it is bench-proven — a wrong PIN is
  refused before the broker is ever asked. But it means a **compromised HMI
  account can wipe the durable state** (identity, host keys, settings) with one
  broker request: no PIN, no physical media — unlike an update, which at least
  requires attached USB. Verifying the PIN broker-side would add almost nothing
  today, because the lock verifier file is owned by the app account, so the
  same compromised HMI could rewrite it before asking. UI-side gating is
  therefore as strong as this is achievable without moving lock state to root
  ownership, which is the available hardening if the threat model ever
  tightens. This mirrors the screen lock's own honest framing: a casual
  physical throttle, not a defense against an attacker who already has the
  app account.
- **Offline devices must stay updatable forever, so update authenticity never
  depends on the clock** (recorded 2026-08-19, owner use case). Some
  deployments never reach an NTP server. Payload authenticity is therefore a
  **pinned raw ed25519 key over the manifest bytes**, not an X.509 chain: there
  is no `notBefore`/`notAfter` to validate, so signature verification works on a
  device whose clock says 1970. Had signing used a certificate chain, a
  permanently offline appliance would eventually become unable to update at all
  when the certificate expired. The USB path does no TLS at any point, so an
  offline device updates from USB indefinitely with full signature enforcement.
  Only the Stage 4 *network* path needs a roughly correct clock, and a device
  that never goes online simply never uses it. **Do not add an expiry or a
  timestamp check to the payload signature**; that would reintroduce exactly the
  failure this avoids.
- **Elapsed time inside the update engine is monotonic, never wall clock**
  (fixed 2026-08-19). An RTC-less Pi boots in the past and jumps forward when
  NTP syncs — and a factory reset enlarges that jump, because the wipe removes
  the saved clock state. With wall-clock arithmetic a jump mid-operation looks
  like minutes of elapsed time that never happened: the stall detector would
  abort a healthy multi-gigabyte write, and the commit service's readiness
  deadline would expire early and drop a healthy candidate into fallback. Both
  now read `/proc/uptime`, which is monotonic and needs no time source at all.
- **A reset device boots with a stale clock.** The wipe removes saved clock
  state along with everything else, so an RTC-less Pi comes up in the past
  until NTP syncs — journals from a reset boot carry the previous day's
  timestamps. Harmless here, but it is a **Stage 4 input**: "Check for Updates"
  does TLS to GitHub, and a freshly reset or long-powered-off device can fail
  certificate validation for this reason alone. Stage 4's check flow must
  classify that as its own refusal ("clock not yet synchronized") rather than a
  generic TLS failure.
- **Not in v1:** reverting the OS slots. The reserved `MP_FACTORY`
  partition later holds a compressed factory payload; "full factory
  restore" then = data reset + updater writes factory payload into the
  inactive slot + tryboot into it. The layout supports this without
  another breaking change — that is the entire reason p5 exists now.

## 8. Staged implementation

Each stage ends with a bench acceptance; no stage starts before the
previous stage's acceptance is recorded (project convention).

**The session's last commit is the handover note** (convention added
2026-08-19, after the note went stale twice). A stage's acceptance record and
the handover that points a fresh session at it must not end up on opposite
sides of a session boundary: writing the note *before* the last stage lands
produces a note that describes a bench state nobody has any more.

### Stage 0 — bench spike (no repo changes; ~1 day)

Hand-partition a 16 GB card per §4; install the current image into slot A
by hand; clone it to slot B with one changed marker file; prove on the
bench Pi 4:

1. normal boot lands in A;
2. `reboot "0 tryboot"` boots B exactly once;
3. commit (rewrite `config.txt`) makes B the default;
4. a deliberately broken B (corrupt cmdline) + watchdog → automatic
   fallback to A without human touch;
5. same sequence on Pi 3 and Pi 5 when hardware is present (Pi 3 verifies
   the `tryboot.txt` firmware support claim — the one open hardware
   question in this plan).

Findings go into this document. **Nothing else proceeds until 1–4 pass.**

#### Stage 0 bench record — 2026-08-16 (Pi 4 + Luckfox CTP): selector and post-PID-1 watchdog passed; broken-cmdline item remains open

**Fixture.** A removable Transcend 128 GB card (119.1 GiB visible) was
hand-partitioned to the §4 MBR layout: 256 MiB `MP_BOOT_A`, 256 MiB
`MP_BOOT_B`, 5 GiB each for `MP_ROOT_A`/`MP_ROOT_B`, 2 GiB `MP_FACTORY`,
and a 106.6 GiB `MICROPANEL_DATA` p8.  The source was the verified
`00.12` Luckfox image, SHA-256
`9b10bfebaf835e92d55bbd5bf799906cc6888219df99451cfa1dbef4534fe2a5`.
Its 4 GiB root was copied into both slots (about 53 seconds per USB write),
then checked and expanded to the 5 GiB slot size.  The source data partition
was also copied and expanded to p8: its small but essential first-boot
directory skeleton is required; a blank ext4 data filesystem did not boot
the appliance.

The Pi 4 had EEPROM bootloader `d76c460359ec31b2fadf3b48e44599673095326f`
(2026-01-09) and firmware `288930ab4712b99596f32732664aaaeb881ef1e0`
(2026-05-21).  Both support file-level `tryboot`.  The 256 MiB shared boot
filesystem used 155 MiB after normal-A compatibility files were added.

**Selector finding.** The initial combined experiment (blank p8 plus normal
`os_prefix=A/`) black-screened and never reached SSH.  After preserving the
source data skeleton, normal A was deliberately made a flat, source-compatible
boot root (`config.txt` + root `cmdline.txt` selecting `MP_ROOT_A`), while
`tryboot.txt` selects `os_prefix=B/`.  This is the accepted Stage 0 fixture;
the two variables were changed together, so this result does **not** isolate
normal `os_prefix` as the cause.  The B `os_prefix` path itself booted
successfully under `reboot '0 tryboot'`.

**Observed acceptance.**

1. An ordinary boot reached A: `/proc/cmdline` named `root=LABEL=MP_ROOT_A`
   and `micropanel.stage0_slot=A`; the durable A marker, Luckfox HMI, broker,
   and machine-ID service were active; `/data` was p8; no units had failed.
2. `reboot '0 tryboot'` reached B exactly once: command line and marker named
   B, the firmware tryboot device-tree value was non-zero, all three services
   were active, and `/data` remained shared on p8.
3. Atomically replacing the small FAT selectors made `config.txt` select B
   and `tryboot.txt` select A.  An ordinary reboot then stayed on B with a
   zero tryboot flag and healthy services.
4. For the watchdog test, B was returned to the one-shot candidate and
   configured with `RuntimeWatchdogSec=20s`.  PID 1 held `/dev/watchdog0`
   (Broadcom BCM2835 watchdog, 20-second timeout).  A user-space attempt to
   stop PID 1 was ignored by the kernel, so it was not a valid fault trigger.
   With panic auto-reboot explicitly `0`, a controlled SysRq kernel panic in
   candidate B stopped the feeder; SSH was down for about 26 seconds before
   the hardware watchdog reset the Pi.  It then returned automatically to
   normal A with a zero tryboot flag and all appliance services active.  The
   temporary watchdog config, service, and enablement link were removed from
   inactive B after the check.

The test in item 4 was a valuable **post-PID-1** watchdog result, but it was
not the stated corrupt-cmdline test. A bad cmdline can fail before systemd
opens `/dev/watchdog0`; that gap was tested on the built card below. The
result did not reset automatically. The owner subsequently accepted the
explicit residual risk for attended Stage 2 updates: a manual power-cycle is
required to consume tryboot's one-shot fallback after a pre-systemd failure.

The known-good recovery card and the source image were never overwritten.
Pi 3 and Pi 5 remain separate future hardware checks; the Pi 3 result still
decides whether this file-level backend remains the cross-model choice.

#### Stage 1 selector isolation record — 2026-08-17 (Pi 4 + Luckfox CTP): passed

With the built card's real `MICROPANEL_DATA` p8 mounted, normal `config.txt`
was rendered with `os_prefix=A/`. The Pi booted with root A, p8 `/data`, and
all appliance services active. An A-only, single-line kernel cmdline marker
then appeared in `/proc/cmdline` after reboot, proving firmware used
`A/cmdline.txt`; the original one-line cmdline was restored and a final clean
normal-`os_prefix=A/` reboot passed. A first marker attempt accidentally made
`cmdline.txt` two lines and therefore required card-side recovery; that was a
test-input error, not evidence against `os_prefix`.

#### Stage 1 pre-PID-1 watchdog record — 2026-08-17 (Pi 4 + Luckfox CTP): manual reset required

On the freshly built `00.14` A/B card, the selector armed B as the one-shot
candidate. Its otherwise valid B cmdline was changed only to
`root=LABEL=MP_ROOT_MISSING` while retaining `rootwait`, producing a kernel
root-device wait before PID 1. Normal `config.txt` continued to select A; B's
original cmdline and `tryboot.txt` were backed up first. `reboot "0 tryboot"`
made the Pi unreachable for more than 60 seconds: `RuntimeWatchdogSec=20s`
did not reset it back to A. A single manual power-cycle then returned it to
healthy A with all appliance services active, proving the one-shot fallback
was consumed by that external reset. B's cmdline and selector were restored
byte-for-byte and p1 was remounted read-only.

**Owner decision — 2026-08-17, clarified 2026-08-18: accepted for attended
Stage 2.** A candidate that fails before systemd is recovered by one manual
power-cycle; the one-shot fallback is bench-proven to return to the previously
committed slot after that cycle. The Stage 2 update UI states an open-ended
recovery condition rather than a numerical window, because USB throughput and
candidate startup are media-dependent: if the candidate has not returned,
remove and reapply power; the uncommitted candidate will be abandoned. Every
published payload must complete a bench boot acceptance before publication. An
image-contained, per-slot initramfs watchdog remains the next hardening
candidate if soak or use exposes this failure class; EEPROM watchdog
provisioning remains deferred to the CM4 secure-boot phase.

#### Stage 1 populated-slot selector record — 2026-08-17 (Pi 4 + Luckfox CTP): passed

The freshly built `00.14` A/B image was exercised on the 119.1 GiB Transcend
bench card (Raspberry Pi 4 Model B Rev 1.5). While A was healthy, its read-only
lower root p5 was copied to idle p6, then p6 was immediately relabelled
`MP_ROOT_B` and passed `e2fsck -pf`; p5 remained `MP_ROOT_A`. This is the
manual form of the updater's required write → relabel → arm sequence.

From A, `arm-candidate B` rendered normal `config.txt` for `os_prefix=A/` and
`tryboot.txt` for `os_prefix=B/`. `reboot '0 tryboot'` booted B exactly once:
the command line named `root=LABEL=MP_ROOT_B`, p8 mounted at `/data`, and the
HMI, privileged, and machine-ID services were active. `commit B` was then
performed from B; a normal reboot remained on B with the same health checks.
Finally, from B, `arm-candidate A` and `reboot '0 tryboot'` booted A once with
healthy services and p8 `/data`; A was deliberately not committed. The final
ordinary reboot returned to committed B (`config.txt` B, `tryboot.txt` A).

This is the required populated-B Stage 1 acceptance. It does not alter the
separate pre-PID-1 recovery decision above.

### Stage 1 — layout + scaffold foundation (breaking; the "minimal possible foundation")

`misc-tools` changes:

- `build-image.sh`/`finalize-image-layout.sh`: A/B layout behind a flag
  (`AB_LAYOUT=1` in board.conf or `--layout=ab`), producing §4 exactly;
  LABEL-based fstab/bind mounts; per-slot `os_prefix` population;
  `config.txt` slot selector; watchdog enabled; `/data` skeleton factored
  into the shared script (§7); slot manifest extensions.
- Keep the current single-slot layout buildable during transition
  (default unchanged until Stage 2 lands, then flip the default).
- Runtime deps: `xz-utils` (curl added in Stage 4).

App changes: none required beyond reading slot identity for display.
Acceptance: before each bench flash, run the static A/B contract check and
the root-only loopback finalizer/verifier integration test. A fresh A/B image
then boots slot A with all existing acceptance checks green (BUILD.md list),
followed by the documented **manual** slot-B population and selector
switch/one-shot-repeat on the *built* card. The latter includes the actual
corrupt-cmdline candidate result and the explicit owner decision on its
pre-PID-1 recovery policy.

**Exit of Stage 1 = the foundation the owner asked for.** Menu-function
development resumes after this point; Stages 2+ proceed in parallel.

### Stage 2 — updater v1 (USB/local file, unsigned)

- New typed broker operation `apply_system_update` (source path as the
  only client-supplied value; same strict JSON/unknown-field discipline
  as `apply_dhcp_server`), mapped to a fixed-argv root handler
  implementing §5: mount source ro → stream `xz -d` to inactive slot with
  on-the-fly SHA-256 → manifest checks (variant/board/hash) → relabel the
  inactive ext4 slot → install slot-relative boot files and render the target
  cmdline from `cmdline.txt.template` → arm tryboot → reboot.
- `micropanel-touch-update-commit` health/commit service per §5.
- Minimal UI: **System → Software Update** — shows running
  version/slot/commit state, "Check USB stick", result via the existing
  ActionRunner progress contract (streaming `dd` progress maps naturally
  onto it), and the owner-approved pre-PID-1 recovery instruction: if the
  candidate has not returned, remove and reapply power to abandon it and boot
  the previously committed slot. This intentionally has no fixed displayed
  duration; the instruction is a recovery condition, not a throughput promise.
- Handler policy test (grep-pinned, like the dnsmasq handler) covering:
  streams-not-stages, hash-before-arm, variant refusal.
- Acceptance: build payload vN+1, update A→B via USB stick, commit;
  update again B→A; pull power mid-write (must remain on old slot);
  corrupt a payload byte (must refuse before arming); pull power after
  arm but before commit (must fall back). Before publication, boot-test each
  published payload once on the Pi 4 + Luckfox CTP bench fixture.

#### Stage 2 USB A→B update + normal-reboot persistence — 2026-08-17: passed

On the Pi 4 + Luckfox CTP bench fixture, a freshly flashed `00.15` A/B image
on slot A accepted a FAT32 USB volume labelled `MP_UPDATE` containing the
three `00.17` payload artifacts. The **System → Software Update → Check USB
stick** path streamed the verified 5 GiB rootfs into inactive p6, then rebooted
once into `00.17` on slot B (`root=LABEL=MP_ROOT_B`).

The candidate HMI, privileged broker, `/data`, and first-frame marker remained
healthy for the configured 30 seconds. `micropanel-touch-update-commit`
recorded `state=committed`, selected B for normal boot (`config.txt` contains
`os_prefix=B/`), and retained A as the one-shot fallback (`tryboot.txt`
contains `os_prefix=A/`). A subsequent ordinary reboot returned to `00.17` on
slot B with HMI and broker active, first-frame evidence present, and no failed
units.

This accepts the USB A→B happy path and its normal-reboot persistence. The
later Stage 2 completion record below covers the B→A repeat, mid-write
power-loss, corrupted-payload refusal, and post-arm/pre-commit fallback.

#### Stage 2 checksum-safe payload, power-loss, and committed-B record — 2026-08-18: passed as scoped

The first generator implementation blanked `s_volume_name` by directly
overwriting sixteen raw ext4 superblock bytes. That left the ext4 metadata
checksum stale, so the device-side post-write `e2fsck -pf` correctly rejected
the target before candidate arm. `misc-tools` commit
`bde05af5b336fb3f8c989ea41b31e5795536a237` replaces that raw edit with
`e2label`: it authors a checksum-valid blank-label source image for streaming,
then restores `MP_ROOT_A` before publishing the payload. Its loopback
integration test verifies both `e2fsck -fn` on the decompressed artifact and
source-label restoration after successful and deliberately failed compression.

The bench fixture booted a fresh `00.20` Luckfox CTP A/B image on A, with app
revision `9ebea62d741cf5db7188f35afdab21f89bbb3e64`. Its `00.21` USB payload
had the expected three files, `MP_UPDATE` FAT label, 5,368,709,120-byte
uncompressed rootfs, a blank sixteen-byte ext4 volume-label field, and a full
on-device decompressed SHA-256 match to its manifest
(`fd8cb6b395f3627080e516916764e301ba5554a63e819045a0f16cea147a6b86`).

Power was removed at approximately 35% of the write. On restart, the Pi
returned to committed A; the incomplete B target was not armed. A fresh
`00.21` retry then completed through **System → Software Update → Check USB
stick**, booted B once, and passed the 30-second HMI/broker/data/first-frame
acceptance window. The commit service recorded `state=committed`, rendered
normal `config.txt` for `os_prefix=B/`, and left `tryboot.txt` selecting A.
After a physical power-cycle, the Pi still booted B from
`root=LABEL=MP_ROOT_B` (`/dev/mmcblk0p6`), reported `VERSION=00.21`, retained
the expected app revision, had HMI and privileged services active, and had no
failed units.

This closes the mid-write power-loss item in the Stage 2 acceptance list and
reconfirms A→B persistence with the checksum-safe generator. The remaining
hardware evidence at that point was the B→A update, corrupt-payload refusal
before arm, and a power cut after arm but before candidate commit; all three
are recorded as passed below.

#### Stage 2 completion — B→A, integrity refusal, and attended fallback — 2026-08-18: passed

The committed `00.21` B system accepted a `00.22` Luckfox CTP USB payload into
A. The handler verified the streamed payload, checked and relabelled A,
booted A once, and committed it after the 30-second HMI/broker/data/first-frame
health window. A subsequent physical power-cycle still selected A
(`root=LABEL=MP_ROOT_A`, normal `config.txt` `os_prefix=A/`, one-shot
`tryboot.txt` `os_prefix=B/`).

For the integrity refusal case, the USB kept the original `00.22` manifest and
boot archive but used a valid-XZ rootfs with one decompressed byte changed.
Its resulting SHA-256 differed from the manifest. The handler wrote only the
inactive B target, then refused the digest at the hash-before-`e2fsck`,
relabel, boot-file, and selector-arm boundary. It reported
`phase=failed-integrity` and the UI returned the safe pre-candidate failure
result; it did not reboot. A remained running with normal A and one-shot B
selectors, and the previously committed A state remained authoritative.

The published valid rootfs was then restored to USB for the final case. A
valid `00.22` B candidate reached its first UI frame after the automatic
tryboot reboot, at which point power was deliberately removed before the
30-second commit interval. After ten seconds without power, the Pi returned to
A. The durable and public records both showed `state=fallback`, candidate B,
version `00.22`; p6 was labelled `MP_ROOT_B`, while normal boot remained A.
The HMI and privileged broker were active and no units had failed. This
completes every Stage 2 hardware acceptance item on the Pi 4 + Luckfox CTP
fixture. The updater remains unsigned and attended; the owner-approved manual
power-cycle recovery for a pre-PID-1 candidate remains the documented limit.

### Stage 2b — single-file `.mpupdate` bundle + zero-preparation USB (inserted 2026-08-18)

Owner-approved insertion before Stage 3: the update artifact becomes one
file an average Windows user can copy to a shop-fresh USB stick, and the
same bundle/reader later powers OTA with no rework. Full design:
[`fable-ota-usb-simplification-proposal.md`](fable-ota-usb-simplification-proposal.md).

1. **Generator:** emit the format=2 `.mpupdate` bundle (deterministic outer
   tar, fixed member order, rootfs last) and publish the standalone manifest
   beside it; the format=1 triplet becomes a build intermediate.
2. **Bundle reader:** one **single-pass, pipe-capable** reader
   (`micropanel-touch-bundle-read` idiom): parse tar headers sequentially,
   enforce the exact member order/allow-list, route manifest → bounded
   memory, `boot.tar` → root-only staging + hash check, `rootfs.img.xz` →
   the existing `xz -d | tee | dd` pipeline. Grep-pinned policy tests plus
   the root-only loopback fixture extended to feed the handler a bundle
   through a pipe (the OTA path in miniature).
3. **USB discovery:** the handler enumerates USB media, mounts candidates
   `ro,nosuid,nodev,noexec` (FAT32 **and** exFAT), and requires exactly one
   `*.mpupdate`; the broker request becomes a source enum (`usb`), removing
   the last client-supplied path. The `MP_UPDATE` label flow and triplet path
   retire once the bench migrates (prototype-phase break, announced in
   BUILD.md). *Implementation deviation, 2026-08-18:* the predicate is USB
   transport plus a mountable FAT filesystem, **without** the originally
   proposed `RM=1` removable flag — the bench USB device and many real sticks
   and USB SSDs report `RM=0`, so that flag would have failed closed on the
   intended hardware.
4. **Fold in the open v5 handler minors** (same files, same touch):
   V5-01 re-entrant cleanup trap, V5-02 stale-mount reclaim under lock
   ownership, V5-03 lock-before-first-publishing-die.
5. **OTA-forward groundwork (the no-rework checklist — done now, used later):**
   - reader is pipe-capable from day one, so OTA is literally
     `curl | reader`;
   - `format=2` defines the optional `manifest.sig` member now (absent =
     accepted until signing lands; present = ignored until then), so
     signing needs no format bump;
   - **build-side signing starts now**: generate the ed25519 keypair,
     document custody, and publish `manifest.sig` inside every bundle from
     the first format=2 release — when device-side verification lands
     (Stage 4), all previously published releases are already signed and no
     migration release is needed (this also front-loads the §11 CM4
     requirement that signing exist before any OTP fuse);
   - version-less published asset names
     (`micropanel-touch-<variant>.mpupdate` / `.manifest`, version inside
     the manifest) so GitHub `releases/latest/download/` URLs are stable
     later;
   - manifest-first early-abort implemented and tested now (wrong
     variant/board/same-version aborts after KBs);
   - same-version semantics decided now: the check step reports
     "up to date" and stops; forced reinstall stays a bench/SSH operation;
     the downgrade rule lands with signature verification;
   - one pinned source of truth for the app/release repo URL (also closes
     v5's V5-05 duplication) and a reserved root-owned config location for
     the future OTA URL template;
   - stall policy decided (v5's V5-04): either N-minute write-stall
     detection in the reader loop or an explicit doc note that the
     30-minute ceiling is the answer.

**Acceptance (one bench pass, fresh version pair):** bundle on an
unformatted shop stick (FAT32 and exFAT variants) A→B and B→A with commit
and power-cycle persistence; corrupt-byte refusal before arm; **mid-write
power cut and post-arm/pre-commit power cut** (these also close handover
v8's pending recovery-smoke re-runs on the V4-hardened code); zero- and
multiple-bundle refusal classes; the BUILD.md user instruction reduced to
the one-sentence copy-file flow.

#### Stage 2b bench acceptance — 2026-08-19 (Pi 4 + Luckfox CTP): passed, complete

Every item of the Stage 2b acceptance list passed on the bench fixture, on a
fresh `00.25`/`00.26` version pair built from `micropanel-touch`
`86dafcadd7b82d02072251d2ba3a8ef4b7451e2c`. The card was flashed with `00.25`
and the `00.26` bundle was the published `micropanel-touch-luckfox-ctp.mpupdate`
(1,666,549,760 bytes), signed and verified against the public key baked into
the running image.

**Zero-preparation USB, both filesystems, both directions.** A→B from an
unlabelled **FAT32** partition on a 232.9 GB stick, committed after the
30-second health window and retained across a physical power-cycle
(`tryboot` flag `0` on the following boot, proving the firmware selected B from
the normal `config.txt` rather than a leftover one-shot). B→A from the same
stick with the `00.25` bundle, committed. A→B again from an unlabelled
**exFAT** partition, committed. The handler mounted every source
`ro,nosuid,nodev,noexec` and enumerated it by USB transport: the stick appeared
as `sda1`, `sdb1` and `sda1` again across runs, so no fixed device path was
ever relied on.

**The removable-flag deviation was necessary, not merely defensible.** The
bench device reports `TRAN=usb RM=0 HOTPLUG=0`. The proposal's `RM=1` predicate
would have refused the only USB device on the fixture.

**Refusal classes, all distinct and all non-destructive.** No media →
`failed-source`. Media with no bundle → `failed-payload`. Two bundles →
`failed-payload`, refusing rather than choosing; p6 still held 177,979 non-zero
bytes in its first MiB, proving the count was decided before either file was
opened. Already-installed version → `failed-version` after roughly 234 bytes of
a 1.67 GB file, with the inactive slot's superblock untouched — the property
OTA depends on. One changed decompressed byte in an otherwise perfect bundle
(identical manifest, valid ed25519 signature, valid XZ, correct length) →
`failed-integrity` after the full 5 GiB write, with p6 left **unlabelled**, the
inactive `B/` boot tree still carrying its old `root=LABEL=MP_ROOT_B` cmdline,
no arm and no reboot.

**Both power cuts.** Mid-write (~30%): returned unattended to committed A,
p6 dirty and unlabelled beside an intact `MP_ROOT_A`, durable state still
`committed`, nothing armed, and no stranded mount or lock in the tmpfs runtime
directory despite the handler being killed with no chance to run cleanup. The
retry then overwrote the dirty slot with no manual cleanup step. Post-arm and
pre-commit (candidate mid-boot, past firmware, before the first frame):
returned to committed B with durable and public state both `fallback`,
`candidate_slot=A`, the one-shot consumed, and the abandoned slot left complete
and labelled — only the selector decides it is not in charge. These two also
close handover v8's pending recovery-smoke re-runs on the V4-hardened code.

**Image contract.** `IMAGE_VERSION` round-tripped: written by the finalizer,
read by the handler for its early abort, and correct on each freshly installed
slot. The pinned `update-signing-key.pub` (`root:root 0644`) and the reserved
`update-source.conf` with version-less GitHub URLs are present in the image and
verified by the image verifier.

##### Findings from the acceptance session

1. **`PrivateTmp=yes` on the broker gives the handler a private mount
   namespace.** Its source mount is therefore invisible from an ordinary SSH
   shell, so host-side `mountpoint` checks on `/run/micropanel-touch-update/source`
   prove nothing either way. Two consequences: a stranded mount cannot leak
   into the host namespace, because the namespace dies with the handler's
   process tree — which is a stronger guarantee than V5-02's reclaim provides,
   making that reclaim belt-and-braces on the appliance rather than the fix for
   an observed production hazard; and any future bench check of the source
   mount must read `/proc/<handler-pid>/mounts`, not `/proc/mounts`.
2. **A USB device can be left exclusively claimed after a failed run.** After
   the corrupt-payload refusal, `/dev/sda` and `/dev/sda1` both refused `O_EXCL`
   opens — blocking `mkfs`, `wipefs` and `BLKRRPART` — with no mount in the host
   namespace, no process holding a descriptor, no device-mapper holder, zero
   in-flight I/O and a clean kernel log. Restarting `systemd-udevd` did not
   clear it; replugging the stick did. The most consistent explanation is a
   mount surviving inside an orphaned mount namespace, since a mount holds the
   claim without any process descriptor and is invisible to `/proc/mounts`.
   **Impact is low**: the updater only ever mounts, which needs no exclusive
   open, so a retry works; only formatting tools are affected. Recorded as an
   unexplained bench observation rather than a diagnosed defect.
3. **Two defects fixed after the acceptance runs (2026-08-19).** The final
   `mount_source_device` and the scan loop's `unmount_source` were unguarded, so
   a failure there reached `cleanup` and published the generic
   `failed-internal`; a user who pulls a stick out of Windows before the copy
   flushes hits exactly that, and "the update stopped safely before candidate
   boot" is a poor answer for a half-copied file. Both now carry explicit
   `source` classes. Separately, a `failed-internal` was undiagnosable after the
   fact: the broker never relays handler output and nothing captured its stderr.
   The handler now mirrors diagnostics to the root-only journal via `logger` and
   records the failing command through an `ERR` trap under `set -E`, so cleanup
   can report `line N: <command>`. Four truncated-bundle cases were added to the
   reader test — cut inside the first header, inside the manifest, inside the
   boot archive and inside the rootfs — and all four now report a specific
   class; the suite fails if any case reports `failed-internal`.
   **These changes landed after the hardware runs above**, so they required
   their own bench pass — recorded immediately below.

#### Stage 2b review fixes (O-01/O-02/O-03/O-05) — 2026-08-19: accepted

Fixes for [`fable-review-stage2b.md`](fable-review-stage2b.md). The signing-key
backup (its Priority 0) was done by the owner. O-04 was notes only.

**O-01 — cleanup unmounted the USB source while the bundle was still open, and
this closes the acceptance session's unexplained finding #2.** Every failure
path reaches `cleanup()` with the bundle open on fds 0/3; an open file keeps
its filesystem busy, so the unmount failed `target is busy` and `|| true`
swallowed it. The surviving mount holds an exclusive claim on the USB device
while being invisible in the host `/proc/mounts`, which is exactly what blocked
host-side `mkfs`/`wipefs` after the corrupt-payload refusal until the stick was
replugged. Cleanup now closes fd 0 and fd 3 before unmounting, and first stops
the streaming subshell if it is still alive — a signal during the write leaves
that child holding the bundle too, so closing only the parent's descriptors
would not have been enough.

*Bench evidence (Pi 4 + Luckfox CTP, committed B `00.28`).* Baseline with the
installed handler: the device was `O_EXCL`-free before the probe; one
same-version run printed `umount: … target is busy`, left
`/run/micropanel-touch-update/source` a mountpoint with one `/dev/sda` entry in
`/proc/mounts`, and the device then refused `O_EXCL`. With the fixed handler
installed into the volatile overlay, the identical probe left no mountpoint, no
`/proc/mounts` entry and an `O_EXCL`-free device — repeated three times, with
the committed state, slot labels and services untouched. The image copy was
restored afterwards; the built-in fix ships with the next image.
**The acceptance record's finding #2 is therefore diagnosed and closed:** it
was this, not an orphaned mount namespace. A host regression test reproduces it
(a corrupt bundle delivered from USB media) and was verified to fail with the
fix reverted.

**O-02 — the standalone manifest's signature is now published.** Stage 4's
check step verifies the tiny manifest before offering an update, so it needs a
signature it can fetch beside it; publishing that only when Stage 4 lands would
leave every release in between unverifiable by that step, recreating the
migration gap that signing-from-day-one exists to avoid. The generator now
publishes three assets — bundle, `manifest.sig`, manifest last, so a reader that
finds the manifest can rely on the other two being complete. The image's
reserved `update-source.conf` gains `MANIFEST_SIG_URL`, and the image verifier
requires it.

**O-03 — a selector that runs and exits non-zero is a selector failure.** The
absent-selector and invalid-output cases were handled; the nonzero-exit case
fell through to the generic class. Now `die selector`.

**O-05 — releases can no longer silently share a payload directory.** Two
independent guards, because the previous rule lived only in an operator's
memory: `--payload-dir` defaults to `<output>/payloads/<version>`, and the
generator refuses to publish when the target directory already holds a
*different* version (same-version republish stays allowed and is exercised by
the fixture).

*Host verification:* all suites pass, including the new O-01 USB-failure
regression, the fixture's exactly-three-assets and detached-signature
verification checks, the O-05 refuse/allow pair, and new grep pins for each
fix. Note for future fixture work: a republish legitimately changes artifact
digests (`e2label` bumps the superblock write time), so the O-05 republish runs
last, after the assertions that compare extracted copies.

#### Stage 2b fix-forward re-check — 2026-08-19 (Pi 4 + Luckfox CTP): passed

The two post-acceptance fixes were built into `00.27` (image) and `00.28`
(bundle), both pinning `micropanel-touch`
`7c15e30780fb9c07ff00b36a992198f220063e42`. Before flashing, the handler was
read back out of the built image and confirmed byte-identical to the committed
source, which closes the gap between "the build resolved ref `main`" and "the
fix is in the artifact about to be flashed".

On a freshly flashed `00.27` card, with the whole first-boot acceptance list
green:

- **No USB media** → `failed-source`, and the root-only journal carried
  `no USB filesystem is available for a system update`. On the previous build
  that line existed only on the handler's discarded stderr.
- **Media present, no bundle** → `failed-payload` with
  `no update bundle was found on the USB media`; the inactive slot, selectors
  and (absent) durable state were all untouched. The media/payload distinction
  held, so the panel does not blame the stick for a contents problem.
- **Normal `00.27`→`00.28` update** from the same unlabelled exFAT stick
  streamed, verified, armed, booted B and committed after the 30-second health
  window. The guarded discovery path left the happy path unchanged, and the
  successful run logged **nothing** to the journal — the new diagnostics emit
  only on failure.

Neither refusal reported the generic `failed-internal`, which was the point of
the fix. `00.27` and `00.28` are now burned bench identifiers.

#### Stage 2b implementation record — 2026-08-18: code complete

All seven Stage 2b task groups are implemented in `micropanel-touch`
(handler, broker, UI, tests) and `misc-tools` (generator, signing, builder,
finalizer, verifier, tests). Nothing here has been on the Pi yet: the bench
fixture still runs the `00.24` format=1 image, and the acceptance list above
is the outstanding work. It needs a fresh image/bundle version pair, because
`00.23` and `00.24` are burned bench identifiers.

**Bundle reader.** One single-pass, pipe-capable reader inside the handler:
it reads each 512-byte ustar header, validates the header checksum, the
`ustar` magic, an empty prefix field, and a regular-file type flag, then reads
*exactly* the member's byte count (1 MiB / 512 B / 1 B `dd` steps with
`iflag=fullblock`) and skips the padding. It never over-reads, so a pipe stays
positioned for the next header. Member names are an allow-list and their order
is the format; anything after `rootfs.img.xz` other than end-of-archive is
refused. Bash assigns `/dev/null` to an asynchronous list's standard input
unless the list carries an explicit redirection, so the streaming stage reads
a named descriptor that shares its open file description with the foreground
reads — the one non-obvious detail in an otherwise linear reader.

**Manifest-first early abort.** Wrong variant, unsupported board, and
already-installed version are all decided from member 1, i.e. after a few
kilobytes, whether the source is a stick or (later) a network stream. The
same-version case is its own public failure class, `failed-version`, so the UI
can say "this panel already runs that version" instead of reporting an error.
Forced reinstall stays a bench/SSH operation; the downgrade rule still lands
with signature verification.

**USB discovery — one deliberate deviation from the proposal.** The proposal
specified `lsblk` `TRAN=usb` **+ `RM=1`**. The bench USB device reports
`RM=0 HOTPLUG=0`, and so do USB SSDs and a good number of ordinary sticks, so
requiring the removable flag would have failed closed on the very hardware
this stage is meant to serve. The implemented predicate is USB transport plus
a mountable `vfat`/`exfat` filesystem (partition or superfloppy), capped at
eight candidates, mounted `ro,nosuid,nodev,noexec`. Zero bundles and more than
one bundle are separate refusals (`failed-payload`), and "no USB filesystem at
all" stays `failed-source`.

**Source enum.** The broker request field is now `source` with the single
accepted value `usb`; the old `source_path` string and the
`/data/micropanel-touch-system/updates/` local escape hatch are both gone. The
handler additionally accepts `stdin`, which the typed broker cannot request:
it is the identical reader fed by a pipe, used by the loopback fixture and by
Stage 4 as `curl | handler`.

**Signing (build side only).** `misc-tools` generates an ed25519 keypair at
`/etc/micropanel-touch/release-signing/` on first use, signs the manifest into
every published bundle, and re-verifies the signature before publishing.
Custody rules are in BUILD.md. The public key is baked into every A/B image at
`/usr/lib/micropanel-touch/update-signing-key.pub`, and the image also carries
a reserved, unused `/usr/lib/micropanel-touch/update-source.conf` with the
version-less OTA URLs. The device ignores both until Stage 4.

**Image contract additions.** `image-manifest.env` gains `IMAGE_VERSION`
(required — the updater refuses to run without it, since it is what the
same-version abort compares against). The image verifier checks it along with
the pinned public key and the reserved OTA config.

**Folded-in v5 minors.**

| Id | Resolution |
|---|---|
| V5-01 | `cleanup()` in the update handler and `restore_boot_state()` in the selector now `trap - EXIT HUP INT TERM` as their first statement; the payload generator's `cleanup()` had the same shape and got the same fix. The commit helper turned out to have no traps at all, so it needed no change. Pinned by ordering assertions in both repos' policy tests. |
| V5-02 | A stranded source mountpoint is reclaimed — but only by the update-lock owner, and only when it is not busy; a busy one is still `failed-source`. Reclaim runs *before* `install -d`, because a stranded read-only mount makes the mountpoint itself unwritable. Covered by the loopback fixture. |
| V5-03 | The lock is acquired immediately after argv/tool validation, before anything can publish, and `write_update_progress` refuses to write at all unless the lock is held. A pre-lock refusal now publishes nothing and the concurrent-invocation case leaves the owner's telemetry untouched — both asserted in the reader test. |
| V5-04 | Decided: N-minute write-stall detection in the streaming loop (`MICROPANEL_UPDATE_STALL_SECONDS`, default 300) rather than relying on the 30-minute broker ceiling for the one unbounded phase. Its own failure class, `failed-stall`. Every other phase keeps the existing ceiling; recorded in BUILD.md. |
| V5-05 | `MICROPANEL_TOUCH_APP_REPO` in `board.conf` is now the only place naming the application/release repository (the builder resolves `--app-ref` against it; both hook lists expand it). `MICROPANEL_TOUCH_RELEASE_URL_TEMPLATE` sits beside it as the reserved OTA URL source. The static contract test fails if the URL reappears anywhere else. |

**One defect found and fixed while implementing.** A `die` inside a command
substitution — `resolve_target_root`, `detect_board` — published its explicit
failure class from the subshell, but the parent's `failure_reported` flag never
saw it, so `cleanup()` immediately overwrote the class with `failed-internal`.
Every slot-resolution and board-detection failure had therefore been reported
to the UI as a generic internal error since Stage 2. `cleanup()` now consults
the published file, which is the only state that crosses the subshell boundary,
before overwriting an explicit class. This was pre-existing, not introduced by
Stage 2b.

**Verification so far (host only, no hardware).**

- `test_update_bundle_reader.sh`: 17 cases, no root — empty bundle, manifest
  not first, missing boot member, out-of-order signature, unknown member name,
  directory member, oversize manifest, `format=1`, wrong variant, unsupported
  board, same version, boot-digest mismatch, slot-bound cmdline in the boot
  archive, signed and unsigned bundles both reaching slot resolution,
  concurrent-invocation refusal with the owner's telemetry intact, and a
  pre-lock refusal publishing nothing.
- `test_system_update_handler_integration.sh` (root, loopback): full A→B
  candidate arm through a **pipe**, one-changed-byte integrity refusal leaving
  the target unlabelled, no-USB / no-bundle / two-bundle refusals, full A→B
  through a real unlabelled **FAT32** USB filesystem, and stale-mountpoint
  reclaim. The exFAT case is present but skipped on this build host, which has
  no `mkfs.exfat`; the bench Pi has `exfatprogs` and the `exfat` kernel module,
  so exFAT is a bench item.
- `misc-tools` static contract + finalizer/verifier/generator loopback
  integration, including member order, ustar magic, signature verification of
  the published bundle, and "exactly two published assets".
- Cross-repo agreement was checked directly: a bundle produced by the real
  generator was fed to the real device reader, which accepted every
  format-level check and stopped only at slot resolution, as expected off
  hardware.
- Not run here: the full C++ test suite. This checkout cannot configure —
  `libgpiod` and `nlohmann-json` are absent from the build host and
  `external/lvgl` is an empty submodule. The changed C++ translation units were
  syntax-checked individually with `-Wall -Wextra`; the real compile happens in
  the image build.

### Stage 2c — extract the update engine into `pi-ab-update` (inserted 2026-08-19)

Owner-approved insertion between the Stage 2b review fixes and Stage 3, per
[`fable-ab-update-extraction-proposal.md`](fable-ab-update-extraction-proposal.md)
§6. The A/B update was built inside micropanel-touch; now that it is
hardware-accepted, it moves to `misc-tools/packages/pi-ab-update/` so other
boards can adopt it — and, more immediately, so Stage 3's factory reset and
Stage 4's OTA are written once in the engine rather than ported out of a
micropanel-shaped implementation afterwards.

The engine keeps: the streaming installer and its bundle reader, the selector,
the commit service, the layout finalizer, the payload generator, the image
verifier, the release-key custody helper, and all five host suites. What stays
with the board is what genuinely belongs to it: its durable-state skeleton, its
image assertions, its trigger surface (broker + UI), and one health hook.

**The board contract.** Everything product-specific arrives from a single
board-authored `/usr/lib/pi-ab-update/ab-update.conf` (product name, manifest
path and variant key, state and runtime directories, health units, health
hook, settle window). The engine *parses* that file strictly and never sources
it: this handler has never evaluated a file as shell code, and keeping that
absolute is worth more than the four lines it costs. Precedence is
environment > config > default, so the existing test seams still work; they are
renamed `AB_*`, since a shared toolkit carrying `MICROPANEL_*` variables would
not really be extracted.

**The one real design change** is the health predicate, exactly as the proposal
anticipated. It is now data plus one optional hook: every unit in
`AB_HEALTH_UNITS` must be active with no restarts, `/data` must be mounted rw,
this must be a tryboot candidate boot, and `AB_HEALTH_HOOK` must exit 0.
micropanel-touch's first-frame marker check became that hook — the only part of
its health logic that was ever app-specific. The engine refuses to commit at
all when the unit list is empty, because a predicate that asserts nothing is a
misconfiguration rather than a permissive default. The restart check also
tightened slightly on the way: it now covers every listed unit, where before it
counted only the HMI's.

**Deliberately not renamed.** The `MP_*` labels, the p1/p2/p5/p6/p7/p8 layout
and the `@MICROPANEL_SLOT@` cmdline placeholder stay as fixed cross-board
format constants (§6 decision 3). Renaming the labels would force a reflash and
renaming the placeholder would silently invalidate every published bundle, both
for cosmetic gain. The historical release-key directory is kept the same way,
as a board setting rather than an engine default, because that key's public
half is already inside flashed images.

**One deviation from the proposal's file list.** The single-pass bundle reader
stays inside `ab-system-update` rather than becoming a separate
`ab-bundle-read`. It routes members directly into the streaming
`xz -d | tee | dd` pipeline and shares the handler's failure classes and
progress publishing; splitting it would add a process boundary in the most
safety-critical path to satisfy a file listing. It is board-agnostic either
way, which was the actual requirement, and the `stdin` source still proves it
is pipe-capable for Stage 4.

The broker now execs `/usr/local/sbin/ab-system-update` (overridable with
`--update-engine`) because the engine ships with the image rather than from the
application's prefix. The engine installs itself — updater, selector, commit
service, its unit and the `WantedBy` symlink, plus the board config — from the
image finalizer, so an adopting board ships no copy of any of it.

**Acceptance:** all five host suites pass from the engine's own home via
`packages/pi-ab-update/tests/run-tests.sh`, and one bench regression on
micropanel-touch (a normal update plus one recovery smoke) proves the refactor
changed nothing observable.

#### Stage 2c bench regression — 2026-08-19 (Pi 4 + Luckfox CTP): passed

A fresh `00.29` card, updated to `00.30`, on app revision
`f9a17ebd91d5d76ca4692d9e8aee31c9c40a574e`. The regression is deliberately
small because the claim is that nothing observable changed.

**The engine really is what runs.** The flashed image carries
`/usr/local/sbin/ab-{system-update,slot-selector,update-commit,data-skeleton}`,
`/lib/systemd/system/ab-update-commit.service` with its `WantedBy` symlink, and
`/usr/lib/pi-ab-update/{ab-update.conf,update-signing-key.pub,update-source.conf,boot-selector-config.base}`
— with all four previously micropanel-named copies gone, so there is no
ambiguity about which updater the device runs. The commit service is ordered
after `micropanel-touch.service` and `micropanel-touch-privileged.service`
through a drop-in the finalizer *generated from* `AB_HEALTH_UNITS`; the shared
unit names no product. Both installed engine scripts are byte-identical to
their committed sources. The health hook runs standalone and exits 0 once a
frame has been rendered.

**Normal update.** The broker exec'd `/usr/local/sbin/ab-system-update usb`,
which read its product identity from the board config, discovered the
unlabelled exFAT stick, streamed and verified `00.30`, armed, and rebooted; the
extracted commit service then logged `[SUCCESS] committed candidate slot B
after 30 seconds of health` — the rewritten predicate (every unit in
`AB_HEALTH_UNITS` active with no restarts, plus the hook) reaching the same
decision the hardcoded one did. Durable and public state agreed, selectors
flipped, both slot labels correct, engine journal silent on success.

**Recovery smoke (mid-write power cut at ~30%).** Returned unattended to
committed B `00.30`; p5 left dirty (177,796 non-zero bytes in its first MiB)
and **unlabelled** beside an intact `MP_ROOT_B`; durable state still
`committed/B`; nothing armed and the tryboot flag zero. The tmpfs runtime
directory held only `status` — no stranded mount, no lock, no progress file —
and the USB device accepted an `O_EXCL` open afterwards. That last point is
**O-01 verified on hardware in its natural setting**: the handler was killed
mid-write with no chance to run cleanup, and the device still came back free.

**Not re-run**, deliberately: the rest of the Stage 2b acceptance matrix. Those
paths are unchanged code, exercised by the seven host suites that now run from
`packages/pi-ab-update/tests/run-tests.sh`, including both root loopback
fixtures on the pushed build checkout.

### Stage 3 — factory reset (v1)

Per §7: marker handler + early-boot wipe + PIN-gated menu entry +
skeleton-script sharing. Acceptance: reset with lock enabled requires
PIN; after reset the device behaves as first boot (new identity, DHCP,
default settings, no lock); power cut mid-reset retries and completes.

#### Stage 3 bench acceptance — 2026-08-19 (Pi 4 + Luckfox CTP): passed

On a freshly flashed `00.32` card (app revision
`ff3d10879585e75688f7258aebb823ad96dc2ec0`), with the pre-reset baseline
recorded first so "behaves as first boot" could be compared rather than
judged: machine-id `32472020…`, three SSH host-key digests, the pristine
skeleton, and no update history.

**PIN gate.** With the screen lock enabled, Factory Reset demanded the PIN. A
deliberately wrong PIN produced *"Incorrect PIN. Nothing was erased."*, the
button stayed unarmed, and — the part that matters — the engine's journal was
**empty**: the refusal happened in the UI and the broker was never asked, so no
privileged surface was touched at all. No marker, identity unchanged, no
reboot.

**Reset with a power cut, and the retry.** Correct PIN, two deliberate presses,
reboot. Power was pulled *during* that boot and restored after 20 seconds. The
following boot ran the reset to completion: the marker survived the cut exactly
as designed (it is written before the reboot and cleared only at the very end),
and the wipe is idempotent, so the interruption simply cost one boot. This is
the plan's "power cut mid-reset retries and completes" item.

**Fresh-device state, measured against the baseline.** machine-id
`32472020…` → `8b403a71…`; all three SSH host keys new; `screen-lock.conf`
gone, so the PIN no longer exists; `/data` back to exactly the skeleton with
correct owners and modes; no update history; `lost+found` preserved. The
NetworkManager re-seed ran — on this image its source directory is empty, so it
correctly restored nothing; that path is exercised with a real profile file,
modes preserved, by the host fixture.

##### Two defects found by this acceptance, both fixed

1. **The PIN field had no keyboard** (found before the run could start): the
   reset's PIN prompt created the input but no `lv_keyboard`, so it could not
   be typed into. It now uses the same NUMBER-mode keyboard and the same
   geometry as the other PIN forms — the keyboard owns the bottom of the
   display, so the buttons have to sit above it. Its check key submits (the
   two-press confirm remains the real guard) and its cancel key abandons the
   reset and returns home rather than falling through to the screen-lock
   settings page the other forms return to. Fixed in `00.32`, which is the
   image this acceptance ran on.
2. **A false error on success.** The panel briefly showed *"invalid privileged
   broker response"* before rebooting. The engine wrote the marker and called
   `reboot` immediately; systemd tore down the broker mid-reply, so the client
   reported a failure for an operation that had already succeeded. Confirmed by
   evidence rather than inference: the broker journal shows no rejection, and
   the reset demonstrably ran. The request now schedules the reboot a couple of
   seconds later (`AB_REBOOT_DELAY_SECONDS`, 0 for a synchronous reboot in
   fixtures) so the reply lands first. The fixture asserts the request returns
   immediately, that the reboot has *not* fired at that point, and that it
   fires afterwards.

Also fixed: the early-boot service was logging every line twice, once via
stdout and once via `logger`. systemd already captures a unit's stdout; the
request CLI keeps `logger` because *its* output is discarded by the broker.

*Observed during the run and worth knowing when reading these logs:* the reset
boot's journal carries the **previous day's** timestamps. The wipe removes the
saved clock state, so an RTC-less Pi boots in the past until NTP syncs. It is
not a fault, and it is a named Stage 4 design input (§7).

**These two fixes landed after the run above.** Everything the acceptance
asserts still holds — the keyboard fix was already in `00.32`, and the reboot
timing changes only *when* the reboot happens, not whether the reset does. The
next build should re-check that the panel now shows the success message instead
of the broker error.

#### Stage 3 implementation record — 2026-08-19

Built in the `pi-ab-update` engine from day one, as the extraction proposal's
step 5 intended, so a second board inherits it rather than porting it.

**Two scripts, deliberately.** `ab-factory-reset` writes one root-owned marker
into `AB_STATE_DIR` and reboots — it wipes nothing itself.
`ab-factory-reset-boot` runs early on the next boot, ordered after the durable
mount and before anything that reads it, and does the work. Splitting them is
what makes the reset safe to interrupt: every step is idempotent and the marker
is cleared *last*, so a power cut at any point simply repeats the whole reset
next boot instead of leaving half a device.

**Ordering is generated, not hardcoded.** The shared unit names no product; the
finalizer writes a `Before=` drop-in from `AB_RESET_BEFORE`, exactly as it does
for the commit service's `After=`. For micropanel-touch that is the machine-ID
service (which restores the durable identity before journald restarts), the HMI
and the broker.

**What comes back.** The wipe removes every child of the data mount except
`lost+found`, then re-runs the *same* `ab-data-skeleton` the image build runs,
so a reset device and a freshly flashed one cannot drift. `AB_RESET_SEED`
restores what the skeleton cannot know about — for this board the shipped
NetworkManager profiles, whose pristine copies survive only in the read-only
lower root because the running system bind-mounts `/data` over them. The engine
also guarantees `AB_STATE_DIR` itself, rather than assuming a board's skeleton
creates it.

**The update state goes too** (owner, 2026-08-19): it lives under `/data`, so
the wipe removes it and a reset device reports no update history. The slots are
untouched — the device keeps running whichever slot it was on.

**Safety refusals.** The wipe refuses a data mount that is not a mount point of
its own, is not writable, or whose configured state directory lives elsewhere.
That is not paranoia about a typo: it is what a failed durable-partition mount
looks like, and wiping that directory would destroy the running root.

**Trigger surface (micropanel-touch only).** A typed broker operation carrying
*nothing* — the bare operation name is the whole request, so there is no field
for a client to influence the one operation that erases the device; the broker
rejects any request with extra fields. The UI adds a System-menu entry that
requires the screen-lock PIN when the lock is enabled and configured, then two
deliberate presses. The PIN is re-checked on the confirming press too, so a
correct first press never leaves an armed button for whoever picks the panel up
next.

**Host verification.** A new root loopback fixture drives the real pair against
a real filesystem: marker mode and reboot, that the request wipes nothing, the
interrupted-reset retry, the full wipe (settings, calibration, identity, update
state, user network profiles), skeleton and shipped-profile restoration with
modes preserved, `lost+found` untouched, marker cleared last, and the
not-a-mount-point refusal. Broker tests cover the bare-request accept and the
extra-field reject. The engine gate now runs eight suites.

**Not yet done:** the bench acceptance (PIN gate, first-boot behaviour after a
reset, and a power cut mid-reset).

### Stage 4 — refinements (parallel with feature work, in this order)

1. **OTA pull + signature verification — one stage, never split**
   (reordered 2026-08-18; supersedes the earlier separate 4.1/4.2):
   "Check for Updates" fetches the tiny standalone manifest from the pinned
   `releases/latest/download/` URL template (plain `curl --fail --location`,
   no GitHub API, quota-free assets; LFS remains rejected), verifies its
   `manifest.sig` against the pinned public key, compares
   version/variant/boards, and reports "up to date" or offers the update;
   "Update now" streams the bundle through the **same Stage 2b reader**
   (`curl | reader`, no local download — the inactive slot remains the only
   staging area), re-verifies the embedded manifest + signature, and the
   accepted Stage 2 pipeline does the rest. Device-side refusal of
   unsigned/invalid payloads and the downgrade policy land here; the
   attended-recovery UI rule carries over. Runtime deps gain `curl` +
   `ca-certificates`. The build side has been signing since Stage 2b, so no
   migration release is needed.
2. **Factory payload**: populate `MP_FACTORY` at flash time; "full
   factory restore" menu path per §7; PERSISTENCE.md and capability
   matrix rows for the updater and reset.
3. **Board matrix**: Pi 3 / Pi 5 bring-up and per-(board × panel)
   acceptance; `boards` manifest enforcement proven on hardware.

## 9. Risks and honesty notes

- **`config.txt` rewrite on FAT is the single non-atomic-ish moment.**
  Temp-write + rename, tiny file, p1 otherwise `ro`. Residual risk
  accepted for the prototype; a duplicated selector file + firmware-side
  fallback can be revisited if power-cut soak ever trips it.
- **`config.txt` is shared, not slot-relative.** A candidate initially uses
  the running release's selector template; after commit, the new template is
  also what the fallback slot sees. A release that changes its selector
  template (`dtoverlay`, `dtparam`, or equivalent shared boot setting) is a
  special release class: call it out in release notes and re-test fallback
  before relying on the update. Routine slot payloads must not change it.
- **Pre-PID-1 candidate failures are not covered by `RuntimeWatchdogSec`.**
  The `00.14` broken-root test requires one manual power-cycle, after which
  tryboot returns to A. The owner has accepted that residual for attended
  Stage 2 updates; the update UI and release checklist make it explicit.
  An image-contained per-slot initramfs watchdog is the next hardening
  candidate if evidence warrants it; EEPROM watchdog provisioning is deferred
  to CM4 secure boot.
- **Slot sizes are effectively permanent** once devices ship; 5 GiB per
  slot is deliberate headroom over today's ~4 GiB root.
- **Rollback reads newer `/data`**: the `SettingsFile` unknown-key
  rejection means an older app falls back to defaults on files a newer
  app wrote. That is the *intended* rollback semantic — record it in
  PERSISTENCE.md (additive-keys-only remains the compatibility policy).
- **Shared bootloader blobs are not A/B'd** (Pi 3/4 `start*.elf` on p1;
  Pi 4/5 EEPROM entirely outside this design). EEPROM auto-update stays
  disabled in the image; firmware-blob bumps are rare, deliberate, and
  called out in release notes.
- **Pi 3 support is unverified until Stage 0.5**; the design does not
  depend on it (Pi 4/5 are the must-haves).
- **First-flash fallback gap** (slot B empty until the first update) —
  assumption 3; revisit if the prototype phase ends before Stage 4.3.
- The updater writes to a raw partition as root from a network/USB
  source: the fixed-argv handler + hash-before-arm + variant refusal is
  the security boundary until signing lands. Signing is the release
  gate, not optional polish — Sprint 6 inherits it alongside the
  existing firmware-license and sandboxing gates.

## 10. What this plan deliberately does not do

- No delta/binary-diff updates (full-image only — simplicity beats
  bandwidth at this fleet size).
- No update server / device management; pull-only.
- No `/data` migration tooling (prototype phase; reflash accepted).
- No runtime panel-variant switching (existing owner decision stands;
  payloads are variant-bound).

## 11. Forward path: CM4 + eMMC + secure boot (owner intent, 2026-08-15)

A later product variant is a Pi 4 Compute Module with 16 GB eMMC and
Raspberry Pi secure boot (customer key in OTP). The plan is deliberately
shaped so this is **moderate, localized rework — not a redesign**:

**What carries over unchanged:** the streaming updater and hash-then-arm
flow, health/commit service, watchdog, factory reset, `/data` contract,
payload pipeline and GitHub Releases delivery, all UI. eMMC is still
`/dev/mmcblk0`; the 16 GB budget of §4 applies as-is; `boot.img` loads
fully to RAM but is tens of MiB (fine at 1–2 GB).

**What changes (all behind existing seams):**

1. **Slot-selection backend.** Secure boot loads a *signed `boot.img`*
   (one FAT image containing firmware/kernel/initramfs) per boot
   partition, selected by `autoboot.txt` `boot_partition=` with a
   `[tryboot]` alternate — the documented secure A/B pattern.
   `autoboot.txt` itself is unsigned but can only choose between two
   *signed* images, which is acceptable. This is a second backend for the
   §4 selector abstraction; p2 (`MP_BOOT_B`, reserved now precisely for
   this) becomes the second boot partition, and **no other partition
   moves** — roots, factory, and `/data` keep their positions and labels,
   so fstab, the updater, and the reset skeleton are untouched.
2. **Per-slot boot artifact.** `os_prefix` directory → one signed
   `boot.img` file per boot partition. The updater writes one file
   instead of syncing a directory (simpler); the build pipeline gains a
   make-`boot.img`-and-sign step.
3. **Rootfs verification depth (the one genuinely new work item).** The
   firmware chain verifies only `boot.img`. Extending trust to the root
   filesystem means **dm-verity**, with each release's root hash baked
   into that release's signed initramfs — which pairs naturally with this
   appliance's never-written read-only root and the existing overlayroot.
   The A/B slot pairing already updates boot artifact + rootfs together,
   which is exactly the coupling dm-verity requires.

**Hard consequences to plan around:**

- **Signing (Stage 4.2) becomes a prerequisite, not a refinement**, and
  key custody becomes irreversible-mistake territory: the OTP burn is
  permanent, so the signing process must exist, be exercised, and have a
  key-loss story *before* the first CM4 is fused. Development CM4s should
  be fused late; keep unfused units for bring-up.
- EEPROM/bootloader configuration itself is signed under secure boot;
  EEPROM update handling (already deliberately out of A/B scope, §9)
  gains a signed-config step in the CM4 variant.
- **Stage 0 gains a decision point:** `autoboot.txt` is Pi 4/5/CM4-only.
  If the Pi 3 `tryboot.txt` spike result is shaky *and* the CM4 path is
  firm, the simplest system drops Pi 3 and uses the `autoboot.txt`
  dual-boot-partition layout everywhere — one selector backend instead of
  two. Decide at the end of Stage 0 with the spike evidence in hand.
