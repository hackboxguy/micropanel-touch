# MicroPanel Touch handover — v6

**Prepared:** 2026-08-18
**Supersedes:** [`handover-note-v5.md`](handover-note-v5.md) as the current
restart point. V5 remains the record of the checksum-safe generator fix and
the first committed-B acceptance.

## Current A/B update state

Stage 2 is hardware-accepted for the **Pi 4 + Luckfox CTP** bench fixture.
The final deliberate fallback test left the appliance on the committed A
release:

- `/etc/incremental-version.txt`: `VERSION=00.22`;
- running selector and command-line root: A / `root=LABEL=MP_ROOT_A`;
- normal selector: `config.txt` has `os_prefix=A/`;
- one-shot selector: `tryboot.txt` has `os_prefix=B/`;
- durable/public update result: `state=fallback`, candidate B, version
  `00.22`, variant `luckfox-ctp`; and
- HMI and privileged broker: active, with no failed units.

`fallback` is the expected outcome of the final intentional interruption. It
does **not** mean B was committed: normal boot remains A. B is a valid,
uncommitted `MP_ROOT_B` candidate reserve.

## Completed Stage 2 bench evidence

| Acceptance case | Evidence and result |
|---|---|
| Checksum-safe A → B happy path | A `00.20` image accepted the `00.21` FAT32 `MP_UPDATE` payload. The Pi verified the full decompressed rootfs SHA-256, ran the post-write filesystem check, relabelled B, booted B once, and committed after the 30-second HMI/broker/data/first-frame health window. A physical power-cycle remained on B. |
| Interrupted write | Power was removed at about 35% of the `00.21` write. The Pi returned to committed A; B was never armed. A fresh retry then completed the A → B case above. |
| Committed B → A update | The committed B system accepted the `00.22` payload into A. It booted A once, committed after the same health window, and a physical power-cycle remained on A. |
| Corrupt-payload refusal before arm | A valid-XZ `00.22` rootfs whose decompressed data differed from the manifest SHA-256 was placed on USB while its manifest and boot archive were left unchanged. The handler reached the end of its stream, recorded `phase=failed-integrity`, displayed the safe failure result, did not reboot, and left normal A / one-shot B selectors intact. |
| Post-arm, pre-commit loss of power | After the valid `00.22` rootfs was restored, A armed B and automatically booted the B candidate to its first UI frame. Power was removed before the 30-second commit window, then restored after 10 seconds. The Pi booted committed A with `state=fallback`, candidate B, proving the attended one-shot recovery path. |

The corrupt-payload case intentionally overwrites only the inactive target;
the valid rootfs was restored before the post-arm test. The final B label was
verified as `MP_ROOT_B` after recovery.

## Repository pins

| Repository | Commit | Purpose |
|---|---|---|
| `micropanel-touch` | `9ebea62d741cf5db7188f35afdab21f89bbb3e64` | Stage 2 update recovery, target filesystem validation, and commit telemetry. |
| `misc-tools` | `bde05af5b336fb3f8c989ea41b31e5795536a237` | Checksum-safe label-neutral payload generator plus success/failure restoration regression coverage. |

The Luckfox hook list must continue to pin the same app revision as the
default hook list. The build host must pull both repository commits before
building a release.

## Generator finding retained

Do not blank an ext4 volume label by raw-writing its superblock field. That
invalidates ext4 metadata checksums and makes the device updater correctly
fail `e2fsck` before candidate arm. The generator uses `e2label <p5> ""` on a
writable loop attachment, streams that valid label-neutral filesystem, and
restores `MP_ROOT_A` before publishing. Its loopback test verifies the
decompressed artifact with `e2fsck -fn` and verifies source-label restoration
after both successful and deliberately failed compression.

`blkid`, `e2fsck`, and `e2label` are already present in `/usr/sbin` on the
minimal image. The root updater explicitly adds that directory to `PATH`; do
not add a duplicate runtime dependency merely because the unprivileged `pi`
login PATH omits it.

## Scope and next engineering step

This is an **unsigned, attended USB** updater acceptance on Pi 4 + Luckfox
CTP only. A candidate that fails before userspace still needs one manual
power-cycle; the owner accepted that residual for Stage 2, and the final
post-arm test above proves the recovery procedure.

The next planned implementation stage is Stage 3 factory reset in
[`pi-in-system-update-plan.md`](pi-in-system-update-plan.md). Continue to
boot-test every published payload on this bench fixture before publication.
The user owns remote pushes and image builds; commit local documentation
changes but do not push them.
