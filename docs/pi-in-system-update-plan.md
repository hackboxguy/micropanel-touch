# Pi in-system A/B update plan — micropanel-touch appliance

**Prepared:** 2026-08-15 (fable, from owner decisions of the same date)
**Status:** Stage 1 exit acceptance is complete on the Pi 4 + Luckfox CTP
bench unit, including populated-slot switching and the explicit attended
pre-PID-1 manual-power-cycle recovery decision. Stage 2 has passed the USB
A→B update, mid-write power-loss recovery, candidate commit, and post-commit
physical power-cycle checks. The B→A repeat, corrupt-payload refusal, and
post-arm/pre-commit recovery checks remain before Stage 2 is fully accepted.
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
- USB source: the handler itself mounts only the fixed
  `/dev/disk/by-label/MP_UPDATE` source read-only, streams, and unmounts. No
  automount daemon is added.
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
rendered + no HMI restart during a sustained N-second window** (N=30 default),
then swap the roles in
`config.txt` and mark the slot manifest committed. Anything less: do
nothing — the next reset falls back automatically.

The durable update state remains root-only. The commit service publishes a
bounded, world-readable runtime summary (`committed`, `candidate-armed`, or
`fallback`) for the HMI; a normal boot that sees an armed but unbooted
candidate records `fallback` before exposing it. During the deliberate
multi-minute update window the broker is serial and other broker operations
wait; this is an accepted appliance policy because the device is about to
reboot and the UI tells the operator not to interrupt power.

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
- **Not in v1:** reverting the OS slots. The reserved `MP_FACTORY`
  partition later holds a compressed factory payload; "full factory
  restore" then = data reset + updater writes factory payload into the
  inactive slot + tryboot into it. The layout supports this without
  another breaking change — that is the entire reason p5 exists now.

## 8. Staged implementation

Each stage ends with a bench acceptance; no stage starts before the
previous stage's acceptance is recorded (project convention).

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

**Owner decision — 2026-08-17: accepted for attended Stage 2.** A candidate
that fails before systemd is recovered by one manual power-cycle; the
one-shot fallback is bench-proven to return to the previously committed slot
after that cycle. The Stage 2 update UI must state its recovery window and
tell the operator: if the device has not returned by then, remove and
reapply power; the uncommitted candidate will be abandoned. Every published
payload must complete a bench boot acceptance before publication. An
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
  candidate has not returned, remove
  and reapply power to abandon it and boot the previously committed slot.
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

### Stage 3 — factory reset (v1)

Per §7: marker handler + early-boot wipe + PIN-gated menu entry +
skeleton-script sharing. Acceptance: reset with lock enabled requires
PIN; after reset the device behaves as first boot (new identity, DHCP,
default settings, no lock); power cut mid-reset retries and completes.

### Stage 4 — refinements (parallel with feature work, in this order)

1. **HTTPS pull**: `curl` streaming from a GitHub Release asset (URL
   template pinned in image config; LFS explicitly rejected for quota).
   Reuses the entire Stage 2 pipeline — only the source changes.
2. **Signing**: build-side ed25519 detached signature over the manifest;
   pinned public key in the image; handler refuses unsigned/invalid;
   downgrade policy decided and implemented here; document key custody.
3. **Factory payload**: populate `MP_FACTORY` at flash time; "full
   factory restore" menu path per §7; PERSISTENCE.md and capability
   matrix rows for the updater and reset.
4. **Board matrix**: Pi 3 / Pi 5 bring-up and per-(board × panel)
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
