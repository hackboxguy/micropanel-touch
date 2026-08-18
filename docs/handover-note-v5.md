# MicroPanel Touch handover — v5

**Prepared:** 2026-08-18  
**Supersedes:** [`handover-note-v4.md`](handover-note-v4.md) as the current
restart point. V4 remains the record of the previous panel and Sprint 2.5
work.

## Current A/B update state

The Pi 4 + Luckfox CTP Stage 2 USB update path has a successful, committed
`00.20` → `00.21` acceptance run. The normal system is now slot **B**:

- kernel command line: `root=LABEL=MP_ROOT_B` (`/dev/mmcblk0p6`);
- normal selector: `os_prefix=B/`; one-shot fallback selector:
  `os_prefix=A/`;
- update runtime state before the final reboot: `state=committed`;
- app revision: `9ebea62d741cf5db7188f35afdab21f89bbb3e64`;
- variant/layout: `luckfox-ctp` / `ab`;
- post-commit physical power-cycle: still B, `VERSION=00.21`, HMI and
  privileged broker active, no failed units.

The source image was `00.20` on A. Its `00.21` USB payload was checked on the
Pi before update: the three expected files were present on the FAT volume
labelled `MP_UPDATE`; the decompressed rootfs SHA-256 matched the manifest; its
root volume-label bytes were zero. The handler then completed its post-write
filesystem check and target relabel before it armed tryboot.

## Repository pins

| Repository | Commit | Purpose |
|---|---|---|
| `micropanel-touch` | `9ebea62d741cf5db7188f35afdab21f89bbb3e64` | Stage 2 update recovery, target filesystem validation, and commit telemetry. |
| `misc-tools` | `bde05af5b336fb3f8c989ea41b31e5795536a237` | Checksum-safe label-neutral payload generator plus success/failure restoration regression coverage. |

The Luckfox hook list must continue to pin the same app revision as the
default hook list. The build host must pull both repository commits before
building a release.

## Important generator finding

Do not blank an ext4 volume label by raw-writing its superblock field. That
invalidates ext4 metadata checksums and makes the device updater correctly
fail `e2fsck` before candidate arm. The generator now attaches the completed
image read/write only long enough to run `e2label <p5> ""`, streams the valid
label-neutral filesystem, and restores `MP_ROOT_A` before publishing any
artifact. Its root-only loopback test verifies:

1. the decompressed payload passes `e2fsck -fn` with an empty label;
2. normal payload generation restores the source label; and
3. a forced compressor failure restores the source label through cleanup.

## Accepted recovery evidence

- A power cut at approximately 35% of the `00.21` write returned to committed
  A without arming the incomplete B target.
- The fresh retry booted B once and the 30-second HMI/broker/data/first-frame
  health window committed it.
- The later physical power-cycle remained on B, proving normal-selector
  persistence.
- The attended pre-PID-1 limitation remains accepted: a candidate that fails
  before systemd needs one manual power-cycle to consume tryboot and return to
  the previously committed slot.

## Remaining Stage 2 acceptance

1. Update committed B → A with a later payload and confirm normal reboot
   persistence.
2. Corrupt a payload byte and confirm refusal before selector arm.
3. Cut power after tryboot arm but before candidate commit, then confirm
   fallback to the previously committed slot.

`blkid`, `e2fsck`, and `e2label` are already present in `/usr/sbin` on the
minimal image. The root updater explicitly adds that directory to `PATH`; do
not add a duplicate runtime dependency merely because the unprivileged `pi`
login PATH omits it.

## Next engineering step

Complete the three remaining Stage 2 acceptance cases above. Keep the known
good recovery card available for every destructive test. Once they pass,
proceed to the Stage 3 factory-reset implementation in
[`pi-in-system-update-plan.md`](pi-in-system-update-plan.md). The user owns
remote pushes and image builds; commit local documentation changes but do not
push them.
