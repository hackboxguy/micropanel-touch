# Fable review — Stage 2b implementation (.mpupdate bundle + zero-preparation USB)

**Reviewer:** fable (Claude), 2026-08-19
**Scope:** review of Opus's Stage 2b implementation against the approved
design (`fable-ota-usb-simplification-proposal.md`), plan §8 Stage 2b, the
folded-in v5 findings, and handover v9's work order. Includes live bench
verification.

Commits reviewed:

| Repo | Commit | Subject |
|---|---|---|
| micropanel-touch | `83b05ac` | Read single-file update bundles from any USB stick |
| micropanel-touch | `86dafca` | Detach the update fixture's loop devices on exit |
| micropanel-touch | `7c15e30` | Name the cause when a USB source fails (fix-forward) |
| micropanel-touch | `fabf183` | Record the fix-forward bench re-check |
| misc-tools | `629c29d` | Publish signed format=2 .mpupdate update bundles |
| misc-tools | `ea3e673` | Give the imager the hook-list variables it has to expand |
| misc-tools | `9c62623`/`9b377a1` | BUILD.md acceptance records |

**Verification performed for this review:**

- **Host:** all five host test suites pass on the current trees, including
  the new no-root bundle-reader suite (21 adversarial cases — truncations at
  four different offsets, wrong order, oversize members, PAX/type refusals,
  lock behavior — each asserting its *specific* failure class) and the
  extended static contract test (which now also self-derives the hook-list
  `${VAR}` references and checks the imager invocation carries them).
- **Bench (Pi 4 + Luckfox CTP):** state matches handover v10 exactly
  (committed B, `IMAGE_VERSION=00.28`, revision `7c15e30`, unlabelled exFAT
  stick, pinned public key + reserved `update-source.conf` present, no
  failed units), and the installed handler is **byte-identical** to the
  committed source. Live probe: triggering the updater against the
  already-installed `00.28` bundle exercised USB discovery on the unlabelled
  exFAT stick and aborted with `failed-version` after reading only the
  manifest member — inactive slot label untouched, root-only journal carried
  the diagnostic, and the V5-02 stale-mount reclaim was observed working on
  a second run. Two findings from these probes are below (O-01, and O-01's
  explanation of the acceptance session's "unexplained" USB-claim
  observation).

## Overall verdict

**This is excellent work — the strongest single increment in the A/B track
so far.** Stage 2b is implemented faithfully to the approved design, every
v5 minor was closed properly (several structurally rather than point-wise:
the telemetry publisher *refuses* to write without the lock, rather than
just being called after it), the acceptance was run on real hardware with a
fresh version pair including both power cuts, and two defects found during
acceptance were fixed forward and re-checked on a second version pair rather
than hand-waved. Opus also fixed real pre-existing defects my reviews had
missed (the die-in-command-substitution class being overwritten by
`failed-internal` since Stage 2; three compounding imager bugs that made a
failed build report success), justified one deliberate deviation from my
proposal with hardware evidence (the `RM=1` predicate would have refused the
bench's own `RM=0` USB device — discovery by transport is correct), and shipped
the signing groundwork complete with custody tooling.

The findings below are: one **operator action item that outranks all code**
(the unbacked-up signing key), one **medium, time-sensitive design gap in
the OTA groundwork** (the standalone manifest's signature is not published,
which quietly recreates the migration problem the early-signing decision was
meant to prevent), one small live-reproduced defect (O-01), and minor polish.

---

## Priority 0 — back up the release signing key (operator, not code)

Handover v10 says it plainly and it bears repeating as the first action of
the next session: the ed25519 release key at
`/etc/micropanel-touch/release-signing/ed25519-release.key` **has no backup**.
Its public half is baked into every image from `00.25` on. Once Stage 4
enforcement lands, losing this key means no flashed device accepts another
release short of reflashing. Copy it offline (per the BUILD.md custody
rules) before any further release is published.

---

## Findings

### O-01 (minor defect, live-reproduced) — cleanup unmounts the USB source before closing the bundle file descriptors

The success path closes fd 0/3 before unmounting (`exec 0</dev/null 3<&-`,
handler line ~674). The **failure** path does not: `cleanup()` runs
`umount "$source_mount"` while the bundle file is still open on fds 0/3, so
the unmount fails `target is busy` and the `|| true` moves on. Reproduced
twice on the bench via the same-version probe (run outside the broker, the
mount visibly strands in the host namespace; the V5-02 reclaim then recovers
it on the next run — also observed working — but each failing run re-strands
it).

**This almost certainly explains the acceptance session's "unexplained"
finding #2** (the USB device refusing `O_EXCL` opens after a corrupt-payload
refusal, with no visible mount): the corrupt-payload `die` fires at the
digest check, *before* the fd-close line, so cleanup's unmount fails EBUSY
and the mount survives — not in an "orphaned" namespace, but in the
**broker service's** PrivateTmp namespace, which lives as long as the broker
does. A mount there holds the block device (blocking `mkfs`/`wipefs`
host-side) while being invisible in `/proc/mounts`. Replugging recreated the
device node, which is why that cleared it.

**Fix:** in `cleanup()`, close the stream descriptors before the unmount —
`exec 0</dev/null 3<&- 2>/dev/null || true` (fd 3 may not exist yet; guard).
One line, and the acceptance record's open observation can then be re-tested
and closed as diagnosed. (Bench note: I manually unmounted after my probes;
the device is left clean and unchanged, still committed-B `00.28`.)

### O-02 (medium, time-sensitive) — the standalone manifest's signature is not published, undermining the "no migration release" property for the OTA check step

The generator publishes exactly **two** assets: the bundle and the bare
`micropanel-touch-<variant>.manifest`. The `manifest.sig` exists only
*inside* the bundle. But the agreed Stage 4 check flow verifies the
signature **on the tiny manifest fetch, before offering the update** — and
there is no published signature for it to fetch. When Stage 4 arrives, either
(a) a third asset `…manifest.sig` starts being published — but then every
release published between now and Stage 4 lacks it, recreating exactly the
"first signed release" migration awkwardness that signing-from-day-one was
designed to avoid; or (b) the check step instead fetches the first
kilobytes of the *bundle* (manifest + sig members) via an HTTP range/early-
close — workable, but adds a Range-behavior dependency on the GitHub asset
redirect chain and makes the standalone manifest asset pointless.

**Recommendation:** publish `micropanel-touch-<variant>.manifest.sig` as a
third asset **now** (a few lines in the generator; publish order
bundle → sig → manifest-last keeps the fail-closed property), and add it to
the loopback fixture's exactly-N-assets check and to `update-source.conf`
as a third reserved URL when convenient. Cheap today, annoying with every
release that ships without it.

### O-03 (minor) — selector invocation failure is classified `internal`, not `selector`

`running_slot=$("$selector" current-slot)` (handler ~line 558) is unguarded:
if the selector *runs and fails* (as opposed to being absent, which is
checked), `set -e` drops to cleanup and publishes `failed-internal` with the
ERR-trap context, not `failed-selector`. The invalid-output case is handled;
the nonzero-exit case isn't. One `|| die selector …` (mindful of the
command-substitution/`terminal_failure_published` interplay that is already
handled correctly elsewhere).

### O-04 (notes, no action required)

- **Boot member staging bound:** 256 MiB cap staged into `/run` tmpfs.
  Actual member is ~70 MB; on a 2 GB device this is fine, and a hostile
  oversized-but-under-cap member just ENOSPCs into a safe failure. Worth one
  sentence in the plan if the 1 GB RAM wish ever becomes real.
- **Stall threshold:** 300 s default, env-tunable, kills and classifies as
  `failed-stall`. My v5 session once observed a ~3–4 minute apparent slowdown
  on this stick; the acceptance runs were clean, but if a slow medium ever
  false-positives, the tunable is the answer — a `stalled` grace phase in
  telemetry before the kill would be a nice-to-have, not a need.
- **Same-version refusal semantics:** `!=` means downgrades install (any
  different version). That is the plan's recorded position (downgrade policy
  lands with Stage 4 verification) — noting it here so nobody mistakes
  `failed-version` for downgrade protection.
- **Discovery bound:** more than 8 USB filesystems fails closed; fine.

### O-05 (minor process) — version-less payload directories silently replace releases

Documented honestly in v10 ("always pass `--payload-dir=<per-version
directory>`"), but a rule that lives in an operator's memory will eventually
be broken. Cheap enforcement: default `PAYLOAD_DIR` to
`$OUT_DIR/payloads/$VERSION/`, or refuse to publish when the existing
manifest in the target directory carries a *different* version (same-version
republish stays allowed, as today).

---

## Assignment resolution audit (v9 work order → what landed)

| Assigned | Status | Notes |
|---|---|---|
| Generator: format=2 bundle, deterministic, rootfs last, standalone manifest | **Done** | Outer tar is deliberately strict **ustar** (single 512-byte headers — what makes the single-pass reader possible); member-size guard; staged fail-closed publish. See O-02 for the one gap (sig asset). |
| Single-pass pipe-capable reader | **Done, exceeded** | Header checksum/magic/prefix/type validation, exact-byte streaming that never over-reads, bounded staged members, order/allow-list enforcement, end-of-archive enforcement after rootfs. The `stdin` source in the fixture proves the OTA path today. |
| USB discovery, FAT32+exFAT, exactly one bundle | **Done, with a justified deviation** | `RM=1` dropped with hardware evidence (bench stick reports `RM=0`); transport-based predicate + `ro,nosuid,nodev,noexec` + fail-closed on zero/many. Bench-proven on unlabelled FAT32 *and* exFAT. |
| Broker source enum | **Done** | `usb` is the whole client vocabulary; the local-path escape hatch was removed entirely — less surface than the proposal asked for. |
| V5-01 trap re-entry | **Done** | Handler, selector, and generator all disarm traps first. |
| V5-02 stale-mount reclaim | **Done, live-verified** | Lock-gated, busy-safe; plus the (stronger) namespace insight recorded in acceptance finding #1. |
| V5-03 lock-before-publish | **Done, structurally** | `write_update_progress` refuses to write unless the lock is held — the invariant is enforced at the publisher, not by call ordering. |
| V5-04 stall policy | **Done** | Implemented (better than the "document it" option). |
| V5-05 single-source repo URL | **Done** | `MICROPANEL_TOUCH_APP_REPO` + `MICROPANEL_TOUCH_RELEASE_URL_TEMPLATE` in board.conf only; static test fails on reappearance elsewhere. |
| Build-side signing + custody | **Done** | ed25519 via openssl, key outside every checkout, sign-then-verify at publish, public key baked and verifier-checked. See Priority 0 (backup) and O-02 (sig asset). |
| IMAGE_VERSION / update-source.conf / pinned pubkey contract | **Done** | Round-tripped and verifier-enforced; bench-verified on `00.28`. |
| Acceptance incl. power-cut re-runs | **Done** | All nine items on `00.25`/`00.26`; both power cuts close handover v8's pending smokes; fix-forward re-checked on `00.27`/`00.28`. |

## Beyond the assignment (credit where due)

- **Three compounding pre-existing imager bugs** (error() writing to stdout
  inside command substitution; the cleanup trap's last command masking the
  exit status so failed builds reported success; the un-exported hook-list
  variable) — found via a real failed build, fixed, and fenced with a
  self-deriving static test so the next added variable cannot regress it.
- **A pre-existing Stage 2 defect** — `die` inside command substitutions had
  its explicit class overwritten by `failed-internal` since Stage 2; every
  slot-resolution failure was mis-reported to the UI. Found, fixed
  (`terminal_failure_published`), and the fix-forward added journal
  mirroring + an ERR-trap context so `failed-internal` is never again
  undiagnosable.
- **Honest evidence discipline maintained**: the truly unexplained USB-claim
  observation was recorded as unexplained rather than papered over (and O-01
  above now supplies the likely mechanism to close it).

## Suggested priority order

1. **Priority 0** — back up the signing key offline. Before anything else.
2. **O-02** — publish the standalone `manifest.sig` asset before the next
   release is burned; every release without it deepens the future migration.
3. **O-01** — one-line fd-close in cleanup; then re-test the acceptance
   session's USB-claim observation and close it as diagnosed.
4. **O-03, O-05** — small hardening with the next touch of each file.
5. Proceed to **Stage 3 (factory reset)** per the plan — nothing in this
   review blocks it.
