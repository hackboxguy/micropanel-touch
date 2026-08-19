# Proposal: extract the in-system A/B update into a board-agnostic toolkit

**Author:** fable (Claude), 2026-08-18
**Status:** owner-approved direction (2026-08-19, see §6) — no code changed
yet. Written while Stage 2b (bundle/USB) was in flight; committed to the repo
after Stage 2b's hardware acceptance. Implementation follows the
[`fable-review-stage2b.md`](fable-review-stage2b.md) fixes and precedes
Stage 3.
**Question answered:** can other `misc-tools` board configs (`media-mux`,
`pi4-touch-demo`, future headless boards) reuse the in-system A/B update
without dragging in the micropanel-touch HMI?

## 1. The good news: the coupling is thinner than it looks

The A/B system was built inside the micropanel-touch project, but its
security/testing hardening has already parameterized almost every
board-specific value (the V4/V5 env seams exist precisely because the
loopback tests needed them). An inventory of every component:

| Component | Lives in | Board-specific content | Extraction verdict |
|---|---|---|---|
| Partition layout finalizer (`finalize-image-layout.sh`) | misc-tools board-config | data-skeleton call, manifest path, fstab lines, selector install path | **Engine** — already takes the skeleton script and sizes as inputs; parameterize manifest path + fstab extras |
| Slot selector (`micropanel-touch-slot-selector`) | misc-tools board-config | name, template path, `MP_ROOT_*` labels | **Engine** — fully generic three-op protocol; rename + config |
| Payload generator (`make-ab-update-payload.sh`) | misc-tools board-config | artifact name prefix `micropanel-touch-*` | **Engine** — prefix becomes a config value |
| Image verifier (`verify-ab-image-layout.sh`) | misc-tools board-config | app account, skeleton dirs, manifest path | **Engine + per-board assertions file** |
| Data skeleton (`micropanel-touch-data-skeleton.sh`) | misc-tools board-config | everything — it *is* the board's durable-state contract | **Stays per-board** (the finalizer already consumes it via `DATA_SKELETON_SCRIPT`) |
| Update handler (`handlers/micropanel-touch-system-update`) | app repo | manifest/state/runtime paths, payload prefix, `PANEL_VARIANT` semantics — **all already env-overridable** | **Engine** — promote env defaults to a sourced config file |
| Commit service (`micropanel-touch-update-commit`) | app repo | health predicate: HMI unit + broker unit + first-frame marker | **Engine + pluggable health check** (the one real design change) |
| Bundle reader (Stage 2b, landed) | app repo | none by design | **Engine** — born generic; the `stdin` source already proves it |
| Broker op / `SystemUpdateService` / StarterUi screens | app repo | everything | **Stays micropanel-only** — it is the *trigger surface*, not the updater |
| Watchdog drop-in, tryboot marker check, structural slot resolution | various | none (labels/partition numbers are the shared layout contract) | **Engine** |

The key observation: the **update engine never needed a touchscreen** — my
bench sessions drove it entirely over SSH by invoking the root handler with
one argument. The HMI/broker is just one client of a root-only CLI that
already exists.

## 2. Target architecture: `pi-ab-update` toolkit in misc-tools

Home: `misc-tools/packages/pi-ab-update/` (misc-tools is already the
cross-board home — "misc-tools owns image assembly" is the standing division
of responsibility; a standalone repo stays possible later, the layout below
doesn't preclude it).

```
misc-tools/packages/pi-ab-update/
├── ab-slot-selector          # 3-op selector + render ops (renamed engine)
├── ab-system-update          # the update handler engine
├── ab-bundle-read            # Stage 2b single-pass reader (shared verbatim)
├── ab-update-commit          # commit service engine w/ pluggable health
├── ab-finalize-layout.sh     # image-side A/B layout engine
├── ab-make-payload.sh        # payload/bundle generator engine
├── ab-verify-image.sh        # layout verifier engine
├── ab-update-commit.service  # unit template (health config via env file)
└── tests/                    # the existing loopback fixtures, parameterized
```

Every engine script reads one board-authored config (baked read-only into
the image by the board's hook, root-owned):

```
/usr/lib/pi-ab-update/ab-update.conf        # authored at image build
  AB_PRODUCT=media-mux                      # artifact/payload name prefix
  AB_MANIFEST=/opt/media-mux/share/image-manifest.env
  AB_STATE_DIR=/data/media-mux-system
  AB_RUNTIME_DIR=/run/ab-update
  AB_VARIANT_KEY=PANEL_VARIANT              # or BOARD_VARIANT; compared verbatim
  AB_HEALTH_UNITS="media-mux.service"       # all must be active, zero restarts
  AB_HEALTH_HOOK=/usr/lib/media-mux/health  # optional extra predicate (exit 0)
  AB_SETTLE_SECONDS=30
```

- **Health check becomes data + one optional hook.** The generic predicate
  keeps: tryboot-candidate marker, `/data` mounted rw, every listed unit
  active with zero restarts, sustained settle window. micropanel-touch's
  first-frame marker check becomes its `AB_HEALTH_HOOK` — the only piece of
  its health logic that is truly app-specific. A headless board lists its
  service(s) and optionally a "network up" or "stream alive" hook.
- **Slot/label/partition contract stays fixed across boards** (p1/p2 boot,
  p5/p6 roots, p7 factory, p8 data; `MP_ROOT_A/B` labels). The `MP_` prefix
  is cosmetic; renaming to `AB_ROOT_*` is possible during the Stage 2b
  breaking wave if the owner cares, but divergent per-board labels buy
  nothing and fragment the tooling. Recommendation: one fixed label set,
  rename once or never.
- **Trigger surfaces per board class:**
  - *micropanel-touch:* unchanged — UI → typed broker → engine (the broker
    remains the unprivileged-client boundary; it simply execs the renamed
    engine).
  - *headless/other boards:* the engine's root CLI **is** the interface
    (`sudo ab-system-update <source>` over SSH), optionally plus a tiny
    `ab-update-check` wrapper and — later, opt-in — a udev/USB-insert
    auto-trigger (needs an explicit safety gate: marker file on the stick +
    a config flag; unattended reflash-on-insert is a real foot-gun) and the
    Stage 4 OTA timer. No broker, no UI, nothing else required.
- **misc-tools build side:** the `--layout=ab`, `--payload`, preflight, and
  stamp plumbing in `build-image.sh` is already board-generic; the remaining
  micropanel-only piece is the `--app-ref`/`--app-revision` provenance,
  which generalizes to "the board's primary app hook declares one
  `${..._REVISION}` variable" when a second board adopts it.

## 3. The honest prerequisites for an adopting board

Extraction makes the *software* reusable; A/B remains an **appliance
discipline** a board must adopt, not a flag it flips:

1. **Read-only overlayroot root.** The engine's structural slot resolution
   reads the lower-root mount, the health check requires `/data` rw, and the
   whole rollback model assumes slots are immutable (a writable committed
   slot drifts from its payload hash, and every update silently discards
   root filesystem changes). `media-mux`/`pi4-touch-demo` are RW Qt images
   today — each needs the overlayroot + `/data`-persistence conversion
   (the same write-path inventory micropanel did in Sprint 2.5) *before*
   A/B means anything. This is the big per-board cost, not the update code.
2. **16 GB card + the §4 partition budget**, with `AB_ROOT_PARTITION_MB`
   sized for the board's root (a media-heavy image may need larger slots —
   configurable, but slot sizes freeze at first flash).
3. **A durable-state skeleton script** (its `/data` contract) and an image
   manifest with layout/variant/board keys.
4. **Per-board hardware acceptance** of the Stage 0–2 checklist on its own
   bench fixture — the variant philosophy applies to boards exactly as it
   does to panels; micropanel-touch's records don't transfer.

## 4. Migration plan and timing

**Do the extraction after the Stage 2b review fixes and before Stage 3.**
(Stage 2b landed 2026-08-19, so the original "after Stage 2b" gate is met.)

- *After the review fixes:* the [`fable-review-stage2b.md`](fable-review-stage2b.md)
  items (O-01…O-05) touch the same handler/generator files; landing them
  first means the extraction moves final code, not code with known edits
  pending.
- *Before Stage 3:* the factory-reset marker + skeleton-wipe machinery is
  board-parameterized by design and should be written once, in the engine.
- *Not later than OTA:* the OTA source plumbing should be written once, in
  the shared engine, so every adopting board inherits "Check for
  Updates" + signing rather than porting it afterwards.

Steps (mostly mechanical, ~1 focused session):

1. Move the six engine scripts to `misc-tools/packages/pi-ab-update/`,
   rename, replace hardcoded defaults with the sourced config; the existing
   env seams remain (tests keep using them).
2. micropanel-touch becomes the **reference profile**: its board-config
   authors `ab-update.conf`, keeps its data skeleton, its broker execs the
   renamed engine, its first-frame check becomes the health hook. The app
   repo deletes its copies of handler/commit script (the broker/UI/unit
   wiring stays).
3. Port the test suite with the engine (static contract, policy greps,
   root-only loopback fixtures) parameterized by a fixture profile; keep the
   grep-pins pointing at the new paths via the existing preflight wiring.
4. One bench regression on micropanel-touch (normal update + one recovery
   smoke) proves the refactor changed nothing observable. Its acceptance
   record notes "engine extracted, behavior identical".
5. Stage 3 factory reset is then *built in the engine* from day one
   (marker + skeleton-wipe are already board-parameterized by design), with
   the PIN gate remaining a micropanel trigger-surface concern.
6. First real second-board adoption (when wanted) follows §3's checklist as
   its own small project — that, not the extraction, is where the per-board
   effort lives.

## 5. Open points for the owner

1. Toolkit home: `misc-tools/packages/pi-ab-update/` (recommended) vs a new
   standalone repo now.
2. Label rename `MP_ROOT_*` → `AB_ROOT_*` during the Stage 2b breaking wave,
   or keep the existing labels forever (functionally identical).
3. Whether the extraction slot in the sequence is confirmed as
   post-Stage-2b / pre-OTA (recommended), or deferred until a second board
   actually commits to adopting A/B (defensible YAGNI position — the risk is
   OTA then lands micropanel-shaped and the later extraction grows).
4. Headless auto-update-on-USB-insert: wanted at all? If yes, the safety
   gate design (marker file + config flag + LED/console feedback) needs its
   own small spec.
5. Which board is the realistic first adopter (`media-mux` vs
   `pi4-touch-demo`), since its overlayroot/`/data` conversion is the true
   cost driver and should be planned as its own sprint item.

## 6. Sequencing and decisions recorded (owner, 2026-08-19)

The owner approved the following defaults from the Stage 2b review
discussion; revise this section before implementation if any changes:

1. **Sequence:** Stage 2b review fixes
   ([`fable-review-stage2b.md`](fable-review-stage2b.md) O-01…O-05, with the
   signing-key backup done by the owner first) → **this extraction** →
   **Stage 3 factory reset built in the extracted engine** → Stage 4
   OTA + signing in the engine.
2. **Toolkit home:** `misc-tools/packages/pi-ab-update/` (§5.1); a
   standalone repo remains possible later.
3. **Labels:** keep the existing `MP_ROOT_*`/`MP_BOOT_*`/`MICROPANEL_DATA`
   labels (§5.2) — a rename would force a bench reflash for cosmetic
   benefit.
4. **No second adopter board yet** (§5.5): the extraction lands with
   micropanel-touch as the sole reference profile; a first adopter is
   planned as its own item when a board commits, per the §3 prerequisites.
5. **No USB auto-update-on-insert** (§5.4) for now; headless trigger remains
   the root CLI.
