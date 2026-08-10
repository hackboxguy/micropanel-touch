# micropanel-touch — Implementation Plan

**Companion to:** [`micropanel-touch-prd.md`](micropanel-touch-prd.md) (the *what* and *why*; this document is the *how* and *when*).
**Method:** agile, sprint-based. Every sprint ends with a build **deployed and demoed on the real bench Pi 4 + 3.5" panel** — no sprint is "done" on a dev host. Risky/unknown components are deliberately pulled into the earliest sprints so blockers surface while the plan is still cheap to change.
**Date:** 2026-08-09

---

## 0. Current implementation status — 2026-08-10

- **Sprint 0 is complete on the first bench panel.** The PiScreen DRM overlay,
  fbdev/DRM discovery, direct ADS7846 input, the counter demo, service
  hardening, and the on-target build/test loop were all exercised on the Pi 4.
  `console=tty1` remains deliberately preserved; removing it previously made
  the bench Pi unreachable after reboot.
- **Sprint 1 remains in progress.** `config-basic.json` drives the temporary
  Network / Display / System navigation, and Network → Info has been accepted
  on the panel with live interface data delivered through the UI event queue.
  Network → IP Settings now has three numeric fields (address, prefix, gateway)
  and local IPv4 validation. It deliberately does **not** invoke `nmcli` yet:
  applying a network change waits for the privileged command boundary and
  result-card flow. Network → WiFi now performs a read-only asynchronous
  NetworkManager scan and shows access points or the radio state; its scan
  worker delivers results through the same coalescing UI queue. On the bench
  Pi, `wlan0` reports unavailable, and the UI surfaces that state accurately.
  Its command path has a 15-second timeout, cancellation on shutdown,
  process-group kill and reap, plus a bounded output limit; this is the first
  consumer of the Sprint 2 command-execution contract.
  Leaf Back behavior is covered by a toolkit-independent navigation-history
  test, so Info/IP/Wi-Fi return to their parent menu rather than skipping to
  root. The queue now distinguishes ordered events from replaceable snapshots.
  Physical keypad usability still needs explicit acceptance.
- **Native portrait is the accepted bench mode.** The overlay now uses
  `rotate=90`, yielding a 320×480 framebuffer; the verified touch mapping is
  `swapxy=1` with neither `invx` nor `invy`. This is materially more responsive
  than LVGL's `--portrait` runtime rotation because the fbdev path no longer
  converts horizontal dirty stripes into full-height SPI writes. The runtime
  option remains a development aid, not the product path.
- **The overlay default SPI clock is retained.** A 32 MHz `speed=` trial made
  the physical panel drop glyph pixels even though captures of `/dev/fb0` were
  correct; removing the override restored clean text. Do not add a `speed=`
  value without integrity testing on every supported panel variant.
- The first root screen now resolves its layout once the fbdev backend has
  discovered the real framebuffer geometry, avoiding the malformed initial
  menu that was previously corrected only after the first navigation event.
- The Release test suite passed on the Pi after this increment: touch mapper,
  display backend, UI event queue, static-IP validation, and starter config.

---

## 1. Bench target — observed state (checked 2026-08-09)

`ssh pi@192.168.1.124` — key facts from a live inspection, which Sprint 0 must account for:

| Item | State |
|---|---|
| OS | Debian 13 (trixie) Pi OS Lite, kernel `6.18.34+rpt-rpi-v8`, aarch64 |
| Panel overlay | **Absent** — the Pi was re-imaged since the enablement session; no `piscreen` line, no `/dev/fb0`, no ADS7846, no backlight device |
| Toolchain | `gcc/g++ 14.2` present; **cmake, ninja, git absent** |
| Network stack | NetworkManager (`nmcli`) present — this is what the Sprint 1 network screen scripts against; `wlan0` exists (down), `eth0` = 192.168.1.124 |
| Resources | 3.8 GB RAM free, 108 GB free disk — comfortable for on-target builds |

**Credentials policy:** this repo is public — no passwords in any committed file or script. Sprint 0 installs an SSH key (`ssh-copy-id`) and all tooling (`deploy.sh`, docs) assumes key auth thereafter.

---

## 2. Development workflow

**On-target builds for now.** The Pi 4 (quad A72, 3.8 GB RAM) compiles LVGL + app in low single-digit minutes cold and seconds incrementally; that beats maintaining an aarch64 cross toolchain during the exploratory phase. Revisit cross-compilation only if the edit-build-run loop exceeds ~2 minutes warm (decision recorded per sprint retro).

The loop, encoded in `tools/deploy.sh` from Sprint 0:

```
rsync source → pi  →  cmake --build (on pi)  →  restart app (ssh)  →  watch stdout/journal
```

- App runs in foreground over SSH during Sprints 0–2 (fastest feedback); as a systemd unit (`micropanel-touch.service`, restart-on-failure) from Sprint 3 onward, since power management and long-soak behavior need service semantics.
- GitHub Actions CI from Sprint 0: host-side compile of the engine + unit tests (no display needed) so the ActionRunner/JSON core stays testable off-hardware, per the PRD's engine/renderer split.
- **AI feedback loop (from Sprint 2):** the control + capture interface (PRD §6.8) lets claude-code/codex drive the UI over the deploy loop — navigate by module id or synthetic tap, wait for the render-settle barrier, then read back the **widget tree** (primary: text/geometry assertions) and the **framebuffer** (pixel/theme regressions) — on the bench Pi via SSH and headlessly in CI. UI changes get verified by machines reading what was actually rendered, not by eyeballing the panel.

### Repo layout (established in Sprint 0)

```
micropanel-touch/
├── CMakeLists.txt
├── external/lvgl/            # LVGL v9.x, pinned submodule
├── src/
│   ├── core/                 # renderer-agnostic: config loader, nav graph, ActionRunner,
│   │                         #   PersistentStorage, built-in module registry
│   ├── ui/                   # LVGL frontend: screens, widgets, theme engine, keyboard
│   ├── platform/             # DisplayBackend (fbdev/DRM seam), TouchInput (evdev + quirks),
│   │                         #   Backlight/PowerManager
│   └── main.cpp
├── screens/                  # dev-only demo configs; the 14 legacy configs come from a
│                             #   PINNED micropanel commit (CI byte-drift check, revision
│                             #   recorded in the image manifest) — never hand-copied
├── handlers/                 # Tier-1 core action handlers (PRD §6.7): portable,
│                             #   dependency-light glue scripts installed to $PREFIX/usr/bin;
│                             #   domain packs live in their own repos (pack rule)
│   └── themes/               # skin JSON files (§6), traveling with configs
├── tests/                    # host-runnable unit tests (core/ only)
├── tools/deploy.sh           # rsync + remote build + restart
├── micropanel-touch.service.in   # configure_file'd systemd unit (paths from install prefix)
└── docs/
```

### CMake contract (imager integration)

The final SD image is produced by the existing `misc-tools/build-image.sh` pipeline (as micropanel's is today: `sudo ./build-image.sh --board=micropanel --base-profile=qt-bookworm --version=01.20`). A future `--board=micropanel-touch` board-config will build this repo **inside the image chroot via a hook**, so this repo's CMake must be a good citizen of that pipeline from Sprint 0 — mirroring micropanel's own `CMakeLists.txt` pattern:

- **Dependencies are checked, never fetched:** `find_package`/`find_library`/`find_path` with `REQUIRED` for every system dep (nlohmann-json, libdrm, libgpiod, threads, …) so configure **fails loudly** when the chroot lacks a package — the hook's `build-deps` list is then the fix, not a CMake workaround. The only vendored exception is LVGL itself, a pinned git submodule — and **verified (sol-review-v1 F-19): the existing `generic-package-hook.sh` runs a plain `git clone` and never initializes submodules**, so imaging uses a small dedicated `micropanel-touch-hook.sh` (`git clone --recursive`, LVGL commit recorded in the image manifest). Decided now, not discovered in Sprint 6.
- **Everything installs under `CMAKE_INSTALL_PREFIX`** (the hook passes e.g. `/home/pi/micropanel-touch`): binary, `screens/` (including `screens/themes/`), helper scripts, version file.
- **`INSTALL_SYSTEMD_SERVICE=ON` option** installs a `configure_file`d unit (`micropanel-touch.service.in` → `lib/systemd/system/` under the prefix, ExecStart/paths expanded from the prefix), plus a `SYSTEMD_UNITFILE_ARGS`-style cache variable for extra daemon arguments — same knobs micropanel exposes today. Service *enablement* is the hook's job (`systemctl enable <prefix>/lib/systemd/system/micropanel-touch.service`), not CMake's.
- **No root assumptions, no absolute-path writes at build time** — the same tree must build both on the bench Pi (deploy.sh) and in the imager chroot unchanged.

Division of responsibility: this repo = *check deps → build → install (incl. unit)*; misc-tools board-config = *base image, config.txt overlay line, partition/RO scheme, dep install/purge, service enablement*.

---

## 3. Sprint plan

Sprint numbering is by increment, not calendar; each is roughly 1–2 weeks of part-time effort. **Exit demo** = what gets shown working on the physical panel before the sprint closes.

### Sprint 0 — Walking skeleton (zero → pixels → touch → loop)

Goal: the full path *source on host → binary on Pi → pixels on panel → touch events in app* exists and is fast to iterate.

1. Bench re-enablement: append `dtoverlay=piscreen,drm=1,rotate=90,xohms=100,swapxy=1` under `[all]` in `/boot/firmware/config.txt` (mind the [hw-findings §2.3] traps); verify `fb0 = ili9486drmfb 320×480` and ADS7846 event node after reboot. Trixie/6.18 is the same OS family the enablement session validated — but kernel moved 6.12→6.18, so re-verify rather than assume.
2. SSH key auth + `tools/deploy.sh`; install cmake/ninja/git on the Pi.
3. Repo scaffold per §2 layout; LVGL v9 pinned submodule; `lv_conf.h` with `LV_COLOR_DEPTH 16`; CMake skeleton already following the §2 contract (`REQUIRED` dep checks, install target, `INSTALL_SYSTEMD_SERVICE` option with a `.service.in`).
4. Display bring-up through the `DisplayBackend` seam: enumerate `/dev/dri/by-path`, select the SPI card, and **derive the connector→card→`/dev/fbN` mapping via sysfs** — a DRM connector path does not name the framebuffer, and probe order renumbers devices, so the mapping is tested with HDMI absent *and* present (PRD §6.3).
5. Touch bring-up in `TouchInput` — **measure before transforming**: record `evtest` corner + center coordinates under the shipping overlay first. The kernel DT properties (`touchscreen-swapped-x-y`, `invx`) already transform coordinates, so the app applies only range scaling from kernel-reported ABS min/max, the `BTN_TOUCH`-before-coordinates guard, and any *residual* calibration — never a second swap/invert (sol-review-v1 F-12). Raw evidence retained in `docs/sprint-notes.md`.
6. **Determine backlight ownership** (needed by Sprint 3, verified now while we're in the DT weeds): probe `/sys/class/backlight`, `/sys/class/leds`, DRM props, and `gpioinfo` for GPIO 22 — the overlay assigns that line to the display node, so the kernel driver may own it and raw libgpiod access is *not* assumed. If nothing is kernel-exported, a deliberate DT tweak is the fallback; record whether v1 is on/off-only or has brightness levels.
7. **fbcon/VT ownership from day one:** mask the getty on the panel console, decide console/cursor/blanking policy, and define terminal restoration after app crash — not deferred to Sprint 6 (kernel messages painting over the UI is a bug class we close now).
8. Hello screen: title bar, a counter, one 48 px button that increments it.
9. **Order the second, physically different panel unit now** (top product risk retires early, not at release): it goes through this same sprint's display/touch/backlight acceptance before the quirk model freezes.
10. Vendoring decision recorded: LVGL submodule + dedicated image hook (§2 CMake contract).

**Risks burned:** panel enablement reproducibility on current kernel; touch transform correctness (measured, not inferred); backlight ownership; fbdev↔DRM mapping; clone variance (started); the whole build/deploy loop.
**Exit demo:** tap the button, counter increments, correct mapping at four corners + center, on **both** panel units if the second has arrived; `deploy.sh` round-trip < 2 min warm.

### Sprint 1 — Risk-burndown vertical slice: the Network screen

Goal: one *real, useful* screen that deliberately exercises every risky UI capability at once — on-screen keyboard, async work, busy/progress indication, live data — before any engine investment. This is the sprint that tells us whether LVGL-on-SPI delivers the UX the PRD promises.

Driven by **`screens/config-basic.json`** — a deliberately small, legacy-schema-shaped starter config (Network / Display / System menus: `netinfo`, `netsettings`, `wifi`, `brightness`, `system`, a CPU-load `textbox`, plus new touch-only ids `theme_select`/`orientation_select`) — loaded by a throwaway mini-loader; the real engine comes in Sprint 2. This config is the tuning vehicle: basic menus first, orientation/color/button-style experimentation on top of them (Sprint 3), FPGA-class actions only after the spine exists.

1. **Info panel:** IP/MAC/link state per interface (from `ip -j`/`nmcli -t`), live-refreshing at 2 Hz through an `lv_timer` — proves partial-redraw discipline (only changed labels repaint).
2. **Static IP entry:** tap the IP field → numeric keypad (`lv_keyboard` number mode + `lv_textarea` with an IP accept-filter) → apply via `nmcli` → result card. Proves: keyboard on resistive touch, input validation UX.
3. **WiFi join:** "Scan" with an `lv_spinner` busy overlay while `nmcli dev wifi rescan/list` runs async → tap an SSID → full QWERTY with password masking → join with progress feedback → success/failure card. Proves: QWERTY usability at 480×320, and the **one legal concurrency pattern** (PRD §6.5): workers never touch LVGL — LVGL is not thread-safe and `lv_async_call()` is *not* exempt (sol-review-v1 F-08) — they push immutable events into a thread-safe queue drained on the UI thread by an `lv_timer`. This queue is exactly what ActionRunner reuses; a stress test hammers it with repeated scan/progress/cancel bursts.
4. A progress-bar demo row (fake 30 s task with determinate bar + elapsed label) to preview the flashing UX.
5. **RO-image vertical slice** (pulled forward from Sprint 6 — sol-review-v1 F-03): a minimal `misc-tools` board-config that produces an image with the **final partition topology** — read-only root + `/boot/firmware`, tmpfs overlay, `data` partition — booting the Sprint 0 hello app as a service. This forces the `--expand-root` conflict into the open now (the imager currently always expands root, which would swallow the space `data` needs) and gives every later sprint a real RO image to smoke-test in, while daily work stays on the SSH loop.

**Risks burned:** keyboard usability (fingernail/stylus, key size), UI-thread event-queue pattern, redraw performance of realistic screens, `nmcli` scripting, image partition topology + `--expand-root` conflict.
**Exit demo:** on the panel only — read the Pi's MAC, set a static IP on eth0, scan and join a WiFi AP with a typed password. Subjective UX verdict recorded (this is the PRD's "judge the stack" evidence, arriving even earlier than planned). Plus: the minimal RO image boots read-only to the hello service on a freshly flashed card.

### Sprint 2 — Engine spine: JSON navigation + ActionRunner

Goal: the throwaway loader dies; the real renderer-agnostic core arrives (PRD §6.5).

**Before code: freeze the execution contract** (sol-review-v1 recommendation 4) — one short document covering: privilege architecture (PRD §6.6: non-root UI + allowlisted privileged path; argv-not-strings with a legacy shell-adapter boundary), the `ActionResult` precedence table (PRD §7.2 — legacy judges success by *log markers*, not exit codes; no-log ⇒ assumed success; deviations recorded), path/token expansion via execution context (PRD §7: `$MICROPANEL_HOME` ×127 in `config-pios-new.json`, expanded at runtime, JSON never mutated), and the process-lifecycle contract (PRD §7.3: per-job session/process-group, PGID SIGTERM→SIGKILL, guaranteed reap, startup orphan cleanup).

1. Config loader honoring the compatibility contract for `menu` + static `GenericList`; navigation stack preserving the *observable legacy semantics* (PRD §7): `enabled` gates only top-level registration, `Back`-by-title exit, reserved `back` id.
2. **CommandService**: the single cancellable execution service (PRD §6.5) with timeout, output cap, cancellation token and UI-thread delivery — used by *everything* that will ever run a command (`ActionRunner` now; `textbox`/`items_source`/`list_selection` in Sprint 4, so the legacy blocking-`popen()` freeze class can't reappear).
3. **ActionRunner** on top of it: spawn → tail `log_file` → both progress strategies (`parse_progress` scrape; `usb_blaster_duration` estimate capped at 99 % and labeled *estimated*) → result per the precedence table. Host CI: golden tests with representative FPGA/RH850/generic log fixtures, including contradictory markers and nonzero-exit-with-`[SUCCESS]`.
4. **`micropanel-touch --validate-config`**: strict structural validation (duplicate ids, dangling references, cycles, bad types/timeouts) with readable diagnostics, run in CI over all 14 pinned legacy configs; CI also generates the per-config counts (`config-pios-new.json` = 55 module declarations + 59 submenu references) so parity reporting has honest denominators.
5. Progress screen per the PRD UI direction: bar (determinate vs estimated+elapsed), live 3-line log tail, result card colored by `ActionResult` status.
6. Parity checkpoint: `config-pios-new.json` parses and validates cleanly; unimplemented module types render as visible "not yet implemented" placeholders (dev builds only — release builds fail on them) — the placeholder list *is* the remaining-work tracker.
7. **Control + capture interface v1** (PRD §6.8): UDS JSON protocol (navigate/activate/back/text/tap/state), commands routed through `UiEventQueue`, render-settle barrier, widget-tree dump, framebuffer capture; a headless memory-display backend so the same tests run in CI without hardware. Lands in Sprint 2 because every later sprint (theming, orientation, grids, progress screens) wants machine verification from day one.
8. **Tier-1 `handlers/` established** (PRD §6.7): directory + handler contract (§7.2 result markers, standard-interfaces-only rule), installed by CMake with `screens/`; the Sprint 1 network screens' in-C++ `nmcli` calls migrate to Tier-1 handlers via `CommandService` as built-ins land (Sprint 4), unifying config actions and built-ins on one execution path.

**Exit demo:** navigate the real `config-pios-new.json` menu tree on the panel; run a 350 s simulated FPGA flash with live log tail and result card; then the lifecycle gauntlet — cancel, timeout, service restart, and `SIGKILL` of the UI mid-action — each leaving **zero surviving descendant processes** (asserted by test, not eyeballed).

### Sprint 3 — Themes/skins + display power management

Goal: the experience features requested for early experimentation — skins, orientation, menu presentation, display sleep — all configurable, all tunable live on `config-basic.json`.

**Theme engine (§6 below for design):**
1. Skin = one JSON file of design tokens (colors, radii, font sizes, spacing, button/tile presentation); loaded at startup via config key or `--theme`; applied through a single LVGL theme callback so screens contain zero hardcoded colors.
2. Ship three: `dark` (default), `light`, `high-contrast`. A settings screen (`theme_select`) switches skins live (theme re-apply + full redraw — one-shot cost, acceptable).
3. **Configurable orientation** (PRD §8, revised after the portrait-performance finding): orientation ships as a **boot profile** — portrait `rotate=90,swapxy=1` (the accepted bench default) or landscape `rotate=0,invx=1` — because panel-controller rotation is free while LVGL runtime rotation inflates portrait updates to near-full-frame SPI writes. `orientation_select` offers *preview now* (runtime rotation, dev-grade responsiveness) and *apply at next boot* (managed replace-not-append edit of the overlay line via the PRD §6.6 privileged boundary; until that boundary exists the screen previews only). All screens must reflow correctly in both geometries (the no-hardcoded-literals rule gets its enforcement test here). Portrait free-text keyboard constraint (~30 px QWERTY keys at 320 px) resolved with real fingers this sprint: split layout vs. landscape-style keyboard presentation.
4. **Menu presentation engine** (PRD §8) — the "menu button theme" experimentation surface, config- and skin-driven end to end:
   - `layout: "list" | "grid"` + optional `columns` (default 2) per menu module: row-major flowing tiles (a 2×2 is four items at `columns: 2`, a 2×3 is six), scrollable on overflow, tile size derived from `content_width / columns` in either orientation.
   - Per-item `icon` (built-in symbol name or PNG path, resolved config-relative → skin `icons/` directory; enable LVGL's PNG decoder (`LV_USE_LODEPNG`), cache decoded images, ≤64×64 guidance) and per-item `color` accent override.
   - Skin tokens for button/tile radius, icon size, tile label position, pressed-state treatment.
   - `screens/config-basic.json` gains a grid-demo variant (root menu as 2×2 icon tiles with one per-button color override) so both presentations are demonstrated by editing JSON only — no code path per screen.
5. Redraw law enforced here: the theme layer is the only place styles are defined, and it permits no gradients/shadows/screen-wide animations (PRD §8).

**Display sleep / wake-on-touch (§7 below for design):**
6. Inactivity tracking via `lv_display_get_inactive_time()`; after a configurable timeout (default 60 s, `0` = never) → backlight off (path determined in Sprint 0) + rendering paused.
7. Wake on first touch, **swallowing the wake tap** (touch events discarded until release after wake, so waking never activates a button).
8. Config: additive JSON key, e.g. `"power": { "display_sleep_sec": 60 }` in the app config; also exposed on the settings screen.
9. **Sleep-during-actions policy is a product rule, not a vibe** (sol-review-v1 F-26): active flash/destructive jobs inhibit full sleep (optional dim level allowed), progress processing always stays active, and overriding this requires a deliberate user setting. Acceptance test included; measured power for awake/dim/slept recorded.
10. **Touch-calibration rescue screen** (PRD requirement, sol-review-v1 F-13): settings-menu calibration flow — tap targets → affine transform, validated against degenerate points, versioned in `PersistentStorage`, with reset-to-default and a documented non-touch recovery path (SSH now; keypad once Sprint 4 lands).

**Exit demo:** on `config-basic.json`, switch skins live; preview the other orientation at runtime and verify every screen reflows in both geometries (the shipping orientation switch is the boot-profile path); switch the root menu between list rows and a 2×2 icon grid **and** recolor one button, via JSON edits alone; leave it 60 s → backlight off; tap → instant wake, no accidental activation; a running fake flash keeps the screen alive past the timeout; deliberately mis-calibrate touch, then recover via the rescue flow; measure idle power draw awake vs slept (USB power meter) to quantify the battery win.

### Sprint 4 — Module parity + touch-first upgrades

Goal: the shipped configs work end-to-end (PRD Phases 3–4 combined).

1. `textbox` (script output, `refresh_sec`), `action` modules — both running through `CommandService` (the legacy `TextBoxScreen` calls its script synchronously with no timeout; that class of freeze ends here).
2. Dynamic `GenericList`: `items_source`, `items_action` (`$1` — first-occurrence, via the sanitizing legacy adapter), `list_selection`, `prepend_static_items`, and **`items_path`** (study first — flagged unstudied in the PRD). The legacy static-item-dropping quirk gets its preserve/fix decision here.
3. Built-in module registry — the real one, generated from legacy code (14 ids, `MicroPanel.cpp:563-584`; note `cpu_temp` is a `textbox` config entry, *not* a built-in): the Sprint 1 network screen graduates into `netinfo`/`netsettings`/`wifi`; OLED-only built-ins get their no-op/remap decisions in the matrix (`invert_display` → no-op, `brightness` → kernel backlight interface, …).
4. **Optional keypad indev** (PRD carry-over, sol-review-v1 F-17): config-defined GPIO map, debounce, focus/navigation rules so rotate+press still works and serves as the non-touch recovery input; CI keyboard-simulation test + hardware demo with buttons on free GPIOs.
5. Confirmation dialogs for destructive actions; scrollable full log view from the result card.
6. **The capability/parity matrix becomes real** (sol-review-v1 F-01): generated from code + configs — per reachable action: expanded command, owning package/artifact, devices/permissions, writable paths, status (*supported / remapped / deliberately unavailable / retired*). Product-owner pass over the contested entries (media/pattern workflows with Qt-era dependencies). The matrix drives the Sprint 6 runtime-deps/hooks/udev lists.

**Exit demo:** operational, not just navigational — every *supported* matrix row executes its real command successfully on the panel; every non-supported row shows its deliberate on-screen disposition; matrix published in `docs/`.

### Sprint 5 — Performance + production hardening

1. SPI speed trials: measure frame/flush times at the overlay default and at
   candidate `piscreen` `speed=` values. A 32 MHz trial on the first panel
   already failed visual-integrity acceptance (dropped glyph pixels), so it is
   not a candidate shipping value; document results per [hw-findings §8.1–8.2].
2. Real FPGA-flash rehearsal against a target board — the actual field workflow, timed and observed.
3. **Write-path inventory implemented** over the Sprint 1 partition topology (PRD §6.2): every writer in the image dispositioned (tmpfs / bind-to-`data` / persisted / prohibited) — including NetworkManager profiles, SSH host keys, machine identity, journals; `PersistentStorage` rewritten with the durable-commit protocol (temp → `fsync(file)` → rename → `fsync(dir)` — the legacy fsync-less version is not copied); `data` mounted `nofail`, corrupt/absent `data` falls back to tmpfs defaults and is recreated only on a positively identified device.
4. `micropanel-touch.service` finalized: boot-to-app, restart policy, and — if the watchdog is kept — a real `sd_notify` heartbeat with defined healthy/unhealthy criteria (an unfed `WatchdogSec` is just a restart loop); quiet boot + splash; service ordering against `/dev/fb*`/`/dev/input*` device availability with bounded retry.
5. Power-cut + robustness suite: scripted ~100× random-point cuts **per write category** (idle, persistence commit, network update, mid-action), plus corrupt-`data`, full-`data`, and missing-device boots → must always come back to the app.
6. Performance + soak thresholds recorded: boot-to-app time, input latency, redraw/CPU/RSS under a multi-hour soak — numbers, not impressions.

**Exit demo:** flash → boot to app; yank power mid-action and mid-commit repeatedly; device always recovers, persisted state is never truncated. Threshold table in `docs/sprint-notes.md`.

### Sprint 6 — misc-tools board-config + release machinery

Goal: `sudo ./build-image.sh --board=micropanel-touch --version=X.Y` produces the *shipping* release — the Sprint 1 minimal board-config graduates into full release automation. (First integration already happened in Sprint 1; this sprint is packaging, posture and proof.)

1. Finalize `misc-tools/board-configs/micropanel-touch/`: `board.conf` (stock Lite base — plain trixie profile, **no Qt base profile**, far smaller than micropanel's image — with the base-image **SHA-256 filled in**, unlike micropanel's currently empty field), `runtime-deps.txt` **generated from the Sprint 4 capability matrix** (not hand-guessed), `hooks.txt`.
2. Build hook: the dedicated `micropanel-touch-hook.sh` decided in Sprint 0 (`git clone --recursive`, pinned repo tag + LVGL commit + micropanel config revision, all recorded in the manifest — the generic hook is confirmed submodule-blind).
3. Image-level hooks finalized: `piscreen` overlay line, partition/RO scheme (from Sprint 1/5), getty mask, service enablement, udev rules from the matrix.
4. **Release artifacts** (PRD §6.1): `.img.xz` + SHA-256 + version manifest + build log + SBOM with license notices; unapproved artifacts fail the build. Raspberry Pi Imager compatibility check (customization must not break RO or the appliance account).
5. **Production access posture + audit** (PRD §6.1): appliance account, no default credentials, SSH policy applied, unique host keys persisted to `data`; a release-time audit script fails the image on default passwords, password-SSH, shared host keys, wrong ownership — or the PRD §6.8 control interface left enabled.
6. **Release verification matrix** run against the *published* `.img.xz` on a blank card: all 14 configs validate + navigate, every supported matrix row operational, lifecycle/power-cut/soak suites green on **both** named panel models, plus the stranger test (flash-and-go doc + retail hardware + no help).
7. Versioning/changelog, flash-and-go user doc, tested-hardware list. The board-config + manifest serve as the auditable stock-delta list.

Note: items 1–3 land as a PR to **misc-tools**, not this repo — this repo's contribution to imaging is complete once its CMake honors the §2 contract.

**Exit demo:** a freshly flashed card from the pipeline, on a second SD card and ideally a second panel unit, boots to a working device with zero manual steps — the PRD's "stranger test".

---

## 4. Mapping to PRD phases

| PRD phase | Sprint | Note |
|---|---|---|
| 0 image skeleton | 0 (bench enablement + CMake contract), **1 (minimal RO image, final partition topology)**, 6 (release automation) | Daily work stays on the SSH deploy loop, but the real image shape exists from Sprint 1 (sol-review-v1 F-03) so nothing is built on writable-root assumptions. Zero-touch is proven in Sprint 6 from the published artifact. |
| 1 spine / decision point | 1 + 2 | The "judge the stack" evidence arrives in Sprint 1 (riskier slice, earlier), the spine in Sprint 2. |
| 2 performance | 5 | Plus an early informal read in Sprint 1's UX verdict. |
| 3 + 4 parity | 4 | Combined; Sprint 1's network screen pre-pays the hardest built-ins. |
| 5 production image | 5 | |
| 6 release | 6 | |

---

## 5. Risk register → retirement schedule

| Risk | Retired by |
|---|---|
| Touch quirks (BTN_TOUCH ordering, axis mapping) on current kernel | Sprint 0 |
| Keyboard UX on resistive 480×320 (the make-or-break for §7.1 upgrades) | Sprint 1 |
| Async action → UI threading pattern | Sprint 1 (pattern), 2 (ActionRunner) |
| Partial-redraw performance on realistic screens | Sprint 1 (felt), 5 (measured) |
| Backlight control path under `drm=1` | Sprint 0 (probe), 3 (feature) |
| `items_path` unknowns | Sprint 4 (studied at sprint start) |
| Clone variance | Sprint 0/1 (second unit through bring-up acceptance before the quirk model freezes); re-confirmed at release |
| Image partition topology vs. imager `--expand-root` | Sprint 1 (minimal RO image forces the conflict open) |
| RO-rootfs retrofit pain | Avoided: real RO image exists from Sprint 1; app written RO-aware from Sprint 2 |
| Legacy semantic drift (a "clean" reimplementation behaving differently) | Sprint 2 (golden tests generated from legacy code before it's replaced) |
| Operational-parity scope (legacy tools/firmware/services the configs invoke) | Sprint 4 (capability matrix + product-owner pass) |
| Shell-injection / privilege surface | Sprint 2 (execution contract per PRD §6.6, before ActionRunner exists) |
| Shipping a default credential / password SSH | Sprint 6 (posture + automated image audit) |
| Redistribution licensing | Sprint 6 (SBOM + license gate) |

---

## 6. Theme/skin design (Sprint 3)

A skin is data, not code — one JSON file mapping design tokens to values; the LVGL theme callback is the only consumer. Screens reference roles, never colors.

```json
{
  "name": "dark",
  "colors": {
    "bg": "#101418", "surface": "#1c2228", "chrome": "#161b20",
    "text": "#e8edf2", "text_dim": "#8a94a0",
    "accent": "#3d9bf0", "ok": "#2fbf71", "warn": "#e0a638", "error": "#e05252"
  },
  "shape": { "radius": 8, "tile_radius": 12, "border_width": 0 },
  "type":  { "font_body": "montserrat_16", "font_title": "montserrat_20", "font_small": "montserrat_12" },
  "space": { "pad": 8, "row_height": 56, "chrome_height": 40, "touch_min": 48 }
}
```

- Per-section `accent` override (the PRD's optional module key) multiplies on top of the skin.
- Menu presentation is tokenized too: the optional per-module `layout`/`columns` keys select list rows vs a flowing tile grid (2×2 = four items at `columns: 2`); per-item `icon` (symbol name or PNG, resolved config-relative → the skin's `icons/` directory) and `color` overrides ride on the config; the skin styles what the config places (`"buttons": { "radius": 8, "icon_size": 24, "tile_label": "below" }`-class tokens). Orientation is *not* a skin property — it's a boot-profile device setting, so a skin must look right in both geometries.
- Fonts limited to the LVGL built-in Montserrat set for v1; custom-font conversion is a later nicety. The current starter slice accepts `colors.warn` and `type.font_small` as reserved status/progress tokens; neither has a rendered consumer until those components land.
- The current starter slice applies a selected skin for the lifetime of the process. Selection persistence begins with `PersistentStorage`; a malformed skin file falls back to built-in `dark` with a logged warning (never a crash — themes are user-editable experiment surface by design).
- `space` and the illustrative `buttons` token group are planned expansion points, not inputs accepted by the current minimal skin parser. Menu geometry therefore remains in `StarterUi` for this slice. Before release, decide the presentation-key policy: strict structural validation in `--validate-config`/CI is required; runtime handling may remain strict or become warn-and-default for cosmetic keys.

---

## 7. Display sleep design (Sprint 3)

- **Trigger:** `lv_display_get_inactive_time()` polled by a 1 s `lv_timer`; threshold from config (`display_sleep_sec`, default 60, `0` disables). Any evdev activity (touch or optional GPIO keys) resets it.
- **Sleep:** backlight off — via whatever the kernel exports (`/sys/class/backlight/*`, `/sys/class/leds/*`, DRM property; ownership probed in Sprint 0 — GPIO 22 belongs to the display node per the overlay, so raw libgpiod access is not assumed; if nothing is exported, a deliberate DT tweak is the route); additionally pause LVGL timers/refresh so the CPU idles and no SPI traffic flows. The backlight LED is the dominant panel power draw, so this captures most of the energy win; DRM DPMS panel-sleep can be layered later if measurements justify it.
- **Wake:** the evdev reader thread keeps running while slept; first `BTN_TOUCH` down → backlight on + resume refresh, and **all touch events are discarded until that contact releases** — the wake tap must never press whatever is under the finger.
- **Interaction with long actions — product rule, decided:** active flash/destructive jobs inhibit full sleep; an optional dim level is allowed; progress processing always continues; overriding requires a deliberate user setting. Covered by an acceptance test, with measured awake/dim/slept power recorded (sol-review-v1 F-26).

---

## 8. Working agreements

- Every sprint closes with the exit demo run on the physical panel and a short retro note in `docs/sprint-notes.md` (what was learned, decisions taken, measurements) — this becomes the evidence trail the PRD's open questions get closed against.
- `main` stays deployable: `deploy.sh` from a clean checkout of `main` must always produce a running app on the bench Pi.
- Hardware findings that contradict [hw-findings] or this plan are recorded the same day they're observed (the bench Pi being silently re-imaged before this plan is exactly why).
