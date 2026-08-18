# Proposal: single-file update bundle for USB and GitHub OTA

**Author:** fable (Claude), 2026-08-18
**Status:** proposal for owner/codex review — no code changed.
**Context:** the current expert-only USB preparation flow (a Linux-host
`wipefs`/`parted`/`mkfs.fat -n MP_UPDATE` sequence plus a three-file copy,
kept outside this repo as bench notes), `pi-in-system-update-plan.md` §5/§6
(payload format v1, Stage 4.1 HTTPS pull), Stage 2 as accepted in
`handover-note-v8.md`.

## 1. The problem, concretely

The Stage 2 payload is a **triplet** (`.manifest`, `.boot.tar`,
`.rootfs.img.xz`) on a **FAT32** stick **labelled `MP_UPDATE`** with **exact
filenames**. The current preparation steps are `wipefs`/`parted`/`mkfs.fat`
— a Linux expert flow. For an average Windows user this is worse than it
looks:

- **Windows cannot natively format sticks >32 GB as FAT32** — Explorer only
  offers exFAT/NTFS there; FAT32 needs diskpart tricks or third-party tools.
- Setting a **volume label** and copying **three files without renaming** are
  each easy to get wrong, and the handler (correctly) fails closed on any
  deviation.
- For OTA, a triplet is also the wrong shape: three URLs per release, a
  partial-publish/partial-download consistency problem, and a
  multi-file "which file do I click" story on the Releases page.

## 2. Core recommendation — one `.mpupdate` bundle file

Wrap the existing triplet in **one uncompressed POSIX tar** with a fixed,
streaming-friendly member order, published as a single file:

```
micropanel-touch-<variant>.mpupdate        (outer tar, deterministic flags)
  1. manifest              # existing SettingsFile grammar + format=2
  2. manifest.sig          # OPTIONAL — reserved now for Stage 4.2 ed25519
  3. boot.tar              # unchanged inner artifact, boot_sha256 in manifest
  4. rootfs.img.xz         # unchanged, LAST — streamed to the inactive slot
```

Design properties:

- **Nothing inside changes.** The inner artifacts, their hashes, the
  hash-before-arm ordering, streaming rule, failure classes, commit service —
  all Stage 2 machinery survives untouched. Only the *front end* that locates
  the three artifacts changes, which is exactly the seam plan §5 promised
  ("only the source changes").
- **Order = streamability.** Manifest first means an OTA stream can validate
  version/variant/board and abort after a few KB (cheap "already up to date"
  even mid-download). Rootfs last means the multi-GB tail flows straight into
  the existing `xz -d | tee | dd` pipeline with no staging. `boot.tar`
  (~70 MB) is staged in the root-only private dir and hash-checked before
  use (tmpfs is fine at the 2 GB floor; use `/data` staging if the 1 GB wish
  ever matters).
- **Extension `.mpupdate`, not `.tar`** — Windows 11's Explorer now opens
  tars; a custom extension stops helpful extraction/renaming and gives the
  release asset an unambiguous identity.
- The generator already produces deterministic tars; the bundle is one more
  `tar --format=posix --sort=none` step over files it already makes, with
  the same staged-publish discipline (bundle replaces the triplet as the
  published artifact; the triplet can remain a build intermediate).
- **Prior art:** this converges on what SWUpdate (`.swu` cpio bundle) and
  RAUC (`.raucb`) do — single self-describing streamable container — which
  validates the shape without adopting a third-party updater (owner decision
  stands).

### The one new component: a sequential bundle reader

USB (seekable file) could use three `tar -xOf` passes, but OTA (pipe) needs
single-pass — so build **one** single-pass reader used by both:
`micropanel-touch-bundle-read` — read 512-byte tar header, parse name+size,
enforce the exact member order/allow-list, `dd` exactly `size` bytes to the
member's sink (manifest → memory-bounded file, boot.tar → staging,
rootfs.img.xz → the existing xz/tee/dd pipeline), skip padding, repeat.
~100 lines of shell (`dd iflag=fullblock`) or small C; grep-pinned policy
tests plus the existing root-only loopback fixture extended to feed the
handler a bundle through a pipe. One code path for USB and OTA means one
acceptance matrix.

## 3. USB flow: accept the stick as it comes from the shop

With one file, the remaining pain is the FAT32/label requirement — drop it:

- **Discovery instead of label:** the root handler enumerates removable USB
  block devices (`lsblk` TRAN=usb + RM=1), mounts candidate first partitions
  `ro,nosuid,nodev,noexec`, and requires **exactly one `*.mpupdate`**
  across them (fail closed on zero or multiple, existing failure-class
  telemetry). The client still supplies no path — the broker request becomes
  an enum (`source=usb`), which is *less* client input than today's
  fixed-path string.
- **Accept FAT32 and exFAT** (the two filesystems Windows ships sticks
  with; trixie's kernel has native exfat). No formatting, no label, no
  partitioning. Keep the `MP_UPDATE` by-label path as a bench fast-path if
  desired, or delete it — prototype-phase breakage is allowed and one path
  is simpler.

**End-user instruction after this change, in full:** *"Copy the
`micropanel-touch-<variant>.mpupdate` file onto a USB stick, plug it into
the panel, tap System → Software Update → Check USB stick."*

## 4. GitHub OTA ("Check for Updates")

Publish **two assets per release**, both with **version-less names** so the
stable `releases/latest/download/<asset>` redirect works with plain `curl`
— no GitHub API, no JSON parsing in a root handler, no rate limits, and
release assets are quota-free (the owner's recorded reason for choosing
Releases over LFS):

```
micropanel-touch-<variant>.manifest    # tiny: powers "Check for Updates"
micropanel-touch-<variant>.mpupdate    # the bundle: powers "Update now"
```

(The real version lives *inside* the manifest — content is authoritative,
names are stable. Optionally also attach versioned copies for humans
browsing the Releases page.)

Flow, all through the existing typed-broker pattern with **no client-supplied
URL ever** (URL template pinned in root-owned image config):

1. **Check:** new broker op `check_system_update` → handler
   `curl --fail --location --max-filesize <small>` the manifest asset →
   validate with the existing strict parser → compare `version`/`variant`/
   `boards` against the running manifest → return a sanitized
   "up to date" / "update available: <version>" result. UI shows current vs
   available and asks for explicit confirmation (attended-update rule and
   the recovery instruction stay).
2. **Update:** `apply_system_update source=github` → handler streams the
   bundle URL through the same bundle reader → embedded manifest is
   cross-checked against what step 1 offered (refuse mismatch) → everything
   downstream is the accepted Stage 2 path (hash → e2fsck → relabel → boot
   render → arm → reboot; interrupted transfer leaves the usual dirty
   unlabelled slot, retry overwrites).
3. `curl` joins runtime-deps at this stage, exactly as plan §8 Stage 4
   already schedules.

### Security sequencing — the one strong recommendation

Pull **Stage 4.2 signing forward to land *with* OTA, not after it.**
Attended-USB-unsigned was an explicit owner-accepted posture; a network
source is a different threat class (TLS to github.com is transport
integrity only — anyone with write access to the repo/release, or a future
URL-template mistake, becomes an update authority). The bundle format
reserves the `manifest.sig` slot now precisely so signing is an additive
member + a verify step before any use of the manifest — no format break,
and the check flow verifies the small manifest's signature *before* offering
the update. Downgrade/same-version policy lands with the signature, per the
plan's existing note.

### OTA dataflow and memory budget (no local download, ever)

The bundle is **never downloaded to storage or RAM first**. There are two
independent guards, and the big transfer only starts after the first one:

```
"Check for Updates"                      "Update now" (after user confirms)
────────────────────                     ─────────────────────────────────
curl <manifest asset>  (~300 bytes)      curl <bundle asset> ──┐ (streamed)
  → strict parse                                               ▼
  → compare version/variant/boards       [1] manifest (first KBs, in memory)
    against running image                      → must match what Check offered
  → "up to date"  → STOP (nothing            → wrong/same version → ABORT
    else is ever downloaded)                   (only a few KB were transferred)
  → "update available: <ver>"            [2] boot.tar (~70 MB) → root-only
    → show to user, ask to proceed             staging file → sha256 check
                                         [3] rootfs.img.xz → xz -d → dd →
                                               inactive partition (direct,
                                               sha256 computed on the fly)
                                         hash pass → e2fsck → relabel →
                                         boot render → arm → reboot
```

- The **rootfs never exists anywhere except the inactive partition** — the
  network pipe feeds the exact same `xz -d | tee | dd` path the USB update
  uses today; the inactive slot *is* the staging area (the plan's existing
  streaming rule, unchanged).
- **RAM budget during OTA:** xz decode ≤ 80 MiB (existing limit) + curl
  buffers + the manifest (KBs). The only sizeable staged item is `boot.tar`
  (~70 MB): tmpfs is fine at the 2 GB floor; stage it under `/data` instead
  if the 1 GB wish matters. Nothing else is stored.
- **Interrupted download = today's interrupted USB write:** a dirty,
  unlabelled inactive slot; nothing armed; the next attempt overwrites it.
  `curl --continue-at` resume stays the optional refinement the plan already
  notes.
- The in-stream manifest check ([1]) is deliberately redundant with the
  Check step: it protects against the release changing between Check and
  Update, and against a forced update ever bypassing the comparison.

### Which recommendations depend on which

The three recommendations are **independent — you do not need all three**:

| Goal | Needs |
|---|---|
| Simpler USB (one file to copy; stick still FAT32+`MP_UPDATE`, prepared once and reused) | Rec 1 only (bundle) |
| Zero-preparation USB (any shop stick, no format/label) | Rec 1 + Rec 2 (discovery) |
| GitHub OTA | Rec 1 + Rec 3 (Rec 2 not required) |

If OTA is the priority: implement Rec 1 (bundle + reader), then Rec 3 on
top of the same reader; Rec 2 is an optional USB-UX improvement that can
land any time later, since it touches only source discovery.

## 5. What stays deliberately unchanged

- Payload *contents* and all inner hashes; the manifest grammar/parser.
- The streaming rule, structural slot resolution, label-neutral rootfs,
  pre-stream superblock clear, lock, telemetry, commit service, watchdog.
- Variant binding (`PANEL_VARIANT`) and the Pi-4-only board allow-list.
- The attended recovery model and every recorded owner decision.

## 6. Suggested implementation order

1. **Generator:** emit the `.mpupdate` bundle (+ keep standalone manifest as
   a published asset); static/integration tests extended (member order,
   deterministic bytes, manifest-first early-abort).
2. **Handler:** bundle reader + USB discovery (FAT32/exFAT, exactly one
   bundle); retire the triplet path once the bench migrates (breaking is
   allowed; announce in BUILD.md and bump `format=2`).
3. **Re-run the Stage 2 acceptance set once against a bundle on USB:**
   corrupt-byte refusal, mid-write cut, post-arm/pre-commit cut — the
   existing checklist, one pass, on a fresh version pair (this also covers
   handover v8's pending power-cut smoke re-runs).
4. **OTA check+update** behind the pinned URL template (Stage 4.1) together
   with **manifest signing** (Stage 4.2) and the downgrade rule.
5. BUILD.md: replace the USB preparation section with the one-sentence user
   instruction; keep the expert triplet notes only as build-side reference.

## 7. Open points for the owner

- USB back-compat: keep the `MP_UPDATE` label fast-path or single
  discovery path only? (Recommendation: single path.)
- exFAT: confirm on the bench that the image kernel mounts exfat read-only
  (expected on trixie; zero new runtime deps).
- OTA asset naming: fixed names (recommended, stable URLs) vs versioned
  names + manifest-first URL construction.
- Same-version behavior for OTA "Check": report "up to date" (recommended)
  vs allow forced reinstall from the UI.
- Sequencing sign-off: OTA (4.1) + signing (4.2) as one stage, per §4 above.
