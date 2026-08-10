# sol-review-v1 — disposition

**Review:** `tmp-docs/sol-review-v1.md` (codex-sol, 2026-08-09) against `micropanel-touch-prd.md` and `micropanel-touch-plan.md`.
**Disposition date:** 2026-08-10.

**Independent verification:** before accepting, the load-bearing factual claims were re-checked against the source: module counts (55 declarations + 59 submenu refs in `config-pios-new.json`), log-marker success semantics with ignored exit status and no-log⇒success (`GenericListScreen.cpp:702-820`), `Back`-by-title exit, `enabled`-gates-registration-only, 127× `$MICROPANEL_HOME` in `config-pios-new.json` with install-time rewriting via `update-config-path.sh`, fsync-less temp+rename in `PersistentStorage.cpp`, non-recursive `git clone` in `generic-package-hook.sh:54`, and unconditional `--expand-root` in `custom-pi-imager.sh:414`. **All checked claims were accurate.** The LVGL threading finding (F-08) matches LVGL's official documentation. Overall verdict: a high-quality review; all 26 findings accepted (some with scope nuance), none rejected.

| ID | Sev | Verdict | Action taken |
|---|---|---|---|
| F-01 | P0 | Accepted | Three-level parity (syntactic/navigational/operational) + generated capability matrix now in PRD §7; matrix built in plan Sprint 4 and drives Sprint 6 runtime-deps/hooks/udev. Nuance: some legacy capabilities (Qt-era media/pattern workflows) may be *deliberately retired* — the matrix is exactly the instrument for making that visible (PRD risk 8). |
| F-02 | P0 | Accepted | Runtime execution-context token expansion (JSON never mutated) + compatibility path map + golden tests asserting fully expanded commands: PRD §7; frozen in the Sprint 2 execution contract. |
| F-03 | P0 | Accepted | Minimal RO image with final partition topology pulled forward to Sprint 1, including the `--expand-root` conflict; every later sprint smoke-tests in the real image shape. PRD Phase 0 exit now says "boots read-only". |
| F-04 | P0 | Accepted | Write-path inventory with per-path dispositions + fsync-complete commit protocol: PRD §6.2; implemented/tested per write category in Sprint 5. Legacy `PersistentStorage` explicitly marked do-not-copy. |
| F-05 | P0 | Accepted | PRD §7.2 documents the real legacy semantics (markers, ignored exit codes, no-log⇒success) and requires an `ActionResult` precedence table + log-fixture golden tests; "exit-code-colored card" wording corrected throughout. |
| F-06 | P0 | Accepted | PRD §7.3 process-lifecycle contract (session/PGID, TERM→KILL, reap, orphan cleanup, crash-during-flash); Sprint 2 exit demo replaced "sane" with a tested no-surviving-descendants gauntlet (cancel/timeout/restart/SIGKILL). |
| F-07 | P0 | Accepted | New PRD §6.6: non-root UI + allowlisted privileged path, argv-not-strings with sanitizing legacy shell adapter, service hardening; frozen in Sprint 2 before ActionRunner code. |
| F-08 | P0 | Accepted (our design error) | `lv_async_call`-from-worker removed; UI-thread-only LVGL rule + event queue drained by `lv_timer` now an architecture rule (PRD §6.5), implemented + stress-tested in Sprint 1. |
| F-09 | P1 | Accepted | Single cancellable `CommandService` for *all* command execution (actions, textbox, item providers, built-ins) with timeout/output-cap/cancellation/stale-result handling: PRD §6.5, Sprint 2 item 2, Sprint 4 item 1. |
| F-10 | P1 | Accepted | Observable legacy semantics (enabled, Back-by-title, `$1` first-occurrence, static-item drop quirk, `depends` bag) enumerated in PRD §7 with preserve/break/bug labeling via golden tests generated before replacement (Sprint 2). |
| F-11 | P1 | Accepted | `--validate-config` CLI with strict structural validation, CI over all 14 configs, release build fails on invalid config or placeholders: PRD §7, Sprint 2 item 4. |
| F-12 | P1 | Accepted | Measure-first touch bring-up (evtest corners under shipping overlay; kernel DT transform is source of truth; app applies residual only): PRD §6.3, Sprint 0 item 5. |
| F-13 | P1 | Accepted | Calibration rescue screen (affine transform, degenerate-point validation, versioned storage, reset, non-touch recovery) scheduled: Sprint 3 item 8 + exit demo. |
| F-14 | P1 | Accepted | Backlight ownership probed (backlight/leds/DRM/gpioinfo); kernel-exported interface preferred; deliberate DT change as fallback; on/off-only possibility documented: PRD §6.3, Sprint 0 item 6, plan §7. |
| F-15 | P1 | Accepted | Connector→card→fbN sysfs mapping tested with HDMI absent/present; "configured backend, not physical glass" honesty + absent-glass behavior in the backend contract: PRD §6.3, Sprint 0 item 4. |
| F-16 | P1 | Accepted | getty/VT/console policy + crash restoration moved to Sprint 0 (item 7); service/device ordering, bounded retry, and a fed `sd_notify` watchdog with health criteria in Sprint 5 item 4. |
| F-17 | P1 | Accepted — **keypad retained** | Optional keypad indev implemented in Sprint 4 item 4 (config GPIO map, debounce, focus rules, CI simulation + HW demo); doubles as non-touch recovery input for F-13. |
| F-18 | P1 | Accepted | Second panel unit ordered in Sprint 0 and run through bring-up acceptance before the quirk model freezes; PRD risk 1 updated. |
| F-19 | P1 | Accepted (verified) | Generic hook confirmed submodule-blind; decision made now: dedicated `micropanel-touch-hook.sh` with `git clone --recursive` + pinned LVGL commit (plan §2, Sprint 0 item 10, Sprint 6 item 2). |
| F-20 | P1 | Accepted | Release artifact spec (`.img.xz`, SHA-256, manifest with pinned inputs incl. base-image SHA-256, build log, SBOM); "reproducible" defined as pinned-input repeatability; verification runs from the published artifact: PRD §6.1, Sprint 6 items 4/6. |
| F-21 | P1 | Accepted | Production access posture (appliance account, no default credentials, SSH policy, unique host keys persisted to `data`) + automated release audit: PRD §6.1, Sprint 6 item 5. |
| F-22 | P1 | Accepted | License/SBOM gate over every installed artifact; unapproved artifacts fail the release build: PRD §6.1, Sprint 6 item 4. |
| F-23 | P1 | Accepted | Release verification matrix (all-config validation/traversal, golden semantics, operational smoke per matrix row, lifecycle/corruption/power-cut/soak, both panels, published-artifact stranger test, zero placeholders): Sprint 6 item 6, with the building blocks landing in Sprints 2–5. |
| F-24 | P2 | Accepted (our error) | Counts corrected everywhere to 55 module declarations + 59 submenu references, CI-generated per config: PRD §7, Sprint 2 item 4. |
| F-25 | P2 | Accepted | Single source of truth: pinned micropanel commit, CI byte-drift check, config revision in the image manifest: plan §2 layout note, Sprint 6 manifest. |
| F-26 | P2 | Accepted | Sleep-during-actions is a decided product rule (flash inhibits full sleep, optional dim, progress always active, deliberate override) with an acceptance test and measured power: Sprint 3 item 7, plan §7. |

**Net effect on the plan's shape:** Sprint 0 grew (measurement-first touch, backlight/fbdev/VT ownership, second panel, vendoring decision), Sprint 1 gained the RO-image vertical slice, Sprint 2 gained the frozen execution contract + validator + golden tests, and Sprint 6 shrank from "first integration" to "packaging, posture and proof" — which is the reviewer's recommendation 6, and the right shape for a committed replacement.
