# micropanel-touch — PRD

**Status:** draft for review
**Date:** 2026-08-09
**Companion document:** [`hardware-enablement-findings.md`](hardware-enablement-findings.md) — the bench-verified hardware record from the enablement session, cited below as **[hw-findings]**. Its product/planning sections are superseded by this PRD; its hardware evidence (§2–§6) remains authoritative.
**Basis:** stakeholder interview conducted 2026-08-09 (answers summarized in §2) + [hw-findings] bench evidence.

---

## 1. The reframe: the product is the SD image

The original plan treated this as "port micropanel to a specific SPI panel." The actual goals are:

- The custom `pi-hmi-hat` PCB is a **bottleneck to be eliminated**, and
- end users must be able to **self-source every piece of hardware** on the open market.

Which means: **the deliverable is not an app, it is a redistributable, flash-and-go SD card image.** The app is one component inside it. Everything the old product delivered via a controlled hardware channel (known display, known buttons, known wiring) must now be delivered by software running on hardware you never saw. That single sentence drives every requirement below.

The core decision this project drives: **retire pi-hmi-hat and make the SD image the shipped product.** That decision is already made ("committed replacement", not a feasibility study) — so this PRD plans for production quality, not for a prototype verdict.

---

## 2. Interview outcomes (the requirements source)

| Question | Answer | Consequence |
|---|---|---|
| Why replace pi-hmi-hat? | Custom PCB is a bottleneck **and** users must self-source HW | Product = SD image; image build pipeline is a first-class deliverable (§6.1) |
| Is an on-device screen required? | Yes — required | Headless/phone-web variant is out of scope for v1 (kept as future seam, §6.5) |
| Which displays? | 3.5" SPI resistive family now; **architect so more classes are cheap later** | Display/touch behind a thin HAL with a detection seam (§6.3), UI resolution-independent (§8) |
| Setup burden on the user? | **Zero-touch ideal** — flash, connect, boot, works | Image ships pre-configured for the supported panel family; no calibration ritual, no config editing (§6.3) |
| Fate of the OLED variant? | **Config compatibility only** — fresh codebase is fine, but it must load the existing `screens/*.json` | This codebase; the JSON schema is the binding contract, but interaction patterns are free to improve (§7) |
| Field updates? | Reflash the SD card | No OTA machinery in v1; versioned image releases; persistent user data must survive… nothing (reflash wipes it) — keep persistence minimal (§6.4) |
| Success criterion? | **Committed replacement** for the shipping field tool | Production quality bar: power-cut safety, boot behavior, error handling are requirements, not polish |
| Maintainers? | General embedded C/C++ (not Qt-tied) | Confirms the LVGL/C++ decision — see §4 |
| Base OS? | Raspberry Pi OS Lite | No Buildroot for v1; robustness must be achieved *on* Pi OS Lite (§6.2) |
| Power-off in the field? | **Users will just cut power** | Read-only rootfs from day 1 — an architecture choice, not a hardening pass (§6.2) |

---

## 3. Carried over from [hw-findings] unchanged

These were established with bench evidence and none of the interview answers disturb them:

1. **Hardware enablement** — one line in `config.txt` (`dtoverlay=piscreen,drm=1,rotate=90,xohms=100,swapxy=1`), the `piscreen`-overlay-with-`drm=1` rationale, the pinout, the traps ([hw-findings] §2). This line ships baked into the image.
2. **Qt is rejected** for the on-device UI. The evdevtouch/mtdev NaN failure, the input-grab leaks and the double-activation issue ([hw-findings] §4) stand. (The `QProcess` self-kill bug found during that session has since been fixed in `qt-demo-launcher` independently of this project.)
3. **No coexistence with the Himax/FPD-Link image** ([hw-findings] §3). Reinforced by the interview: the OLED product needs only *config* compatibility, so there is no reason to share an image.
4. **Performance envelope** — ~10 fps full-frame ceiling at 24 MHz; partial redraw is the architecture; progress UIs refresh at 2–4 Hz ([hw-findings] §6).
5. **One process owns display and input, forever** ([hw-findings] §7.4). No spawned UI children, ever.
6. **Extract the `ActionRunner`** from micropanel's `GenericListScreen` into a display-agnostic, unit-testable component ([hw-findings] §7.3).
7. **UI/UX direction** — persistent chrome bar, tile/list content area, and especially the progress screen (determinate vs. explicitly-estimated progress, live 3-line log tail, and a result card colored by the action's final status — where "status" follows the §7.2 result-semantics contract, *not* the raw exit code) ([hw-findings] §7.5).
8. **Input plan** — pointer indev from evdev with the `BTN_TOUCH`-before-coordinates guard; optional keypad indev so a rotate+press flow still works if buttons are fitted ([hw-findings] §7.6).

---

## 4. Technology choice, re-validated

[hw-findings] chose **C++ + LVGL on `/dev/fb0`, reading evdev directly**, while ranking team familiarity last and flagging that as a risk. The interview resolved the risk: the maintainers are **general embedded C/C++**, not Qt-invested. LVGL (plain C API used from C++) is squarely inside their competence, is MIT-licensed, and is the single most-documented toolkit for exactly this display class. The choice stands, now without the caveat.

LVGL also covers the touch-first interaction upgrades this product wants (§7.1): it ships production-quality widgets for on-screen keyboards (`lv_keyboard` with text/number modes), text areas with password masking, lists, sliders, switches, message boxes, arcs/spinners and bars, plus flex/grid layout, anti-aliased fonts, and a themeable style system — all rendering through the dirty-rectangle engine that the SPI budget requires.

Resolved open questions from [hw-findings] §8.5:

- **LVGL version:** v9.x (current stable line), pinned by commit.
- **License:** MIT — no posture issue.
- **Build integration:** vendored in-tree (git submodule or CMake `FetchContent`, pinned) in this repo's CMake project. Installed into the image by a misc-tools chroot hook running plain `cmake --install` (§6.1) — no Buildroot packaging, no `.deb`.

Fallback remains Python+SDL2 if LVGL velocity disappoints, per [hw-findings] §5.2 — but the decision point (§9, Phase 1) is where that gets judged, once, and not revisited afterwards.

---

## 5. Product definition

**`micropanel-touch`** — a fresh codebase (this repository) that:

1. Loads existing `micropanel/screens/*.json` configs **verbatim** (contract in §7).
2. Renders a touch-first menu/action UI on a retail 3.5" SPI resistive panel, with **configurable orientation** — portrait 320×480 (the current bench-verified default profile) or landscape 480×320 — selected per **boot profile** with an on-device runtime preview (§8).
3. Runs shell actions asynchronously with live progress, log tail, and result reporting — parity with the OLED build's *action semantics*, while the interaction layer is free to exceed the OLED experience (§7.1).
4. Ships only inside **the image**: a versioned, reproducible Raspberry Pi OS Lite derivative that boots straight into the app and survives power cuts indefinitely.

Out of scope for v1: remote/web UI (seam preserved, §6.5), multi-language, DSI/HDMI display classes (seam preserved, §6.3). *(OTA updates were out of scope when this was written; they shipped in `00.36`–`00.39` — see [`pi-in-system-update-plan.md`](pi-in-system-update-plan.md).)*

**Milestone note, 2026-08-20 (owner priority pivot).** The next milestone is
**base 1.0: a polished standalone lab tool** — refined base system features
(WiFi join, system stats, about/version, power controls, iperf3 diagnostics)
on the image that already exists. **The §7 legacy-config parity work is
deferred**, not cancelled: the config contract, `--validate-config`, the
parity machinery and the 14 pinned configs remain in place and tested as the
seam a later milestone builds on, but nothing new is built against them now.
The plan's §0.0 carries the decisions and the work order.

---

## 6. Architecture

### 6.1 The image build pipeline is a first-class deliverable

Because "reflash the SD card" is the only update channel, every release *is* an image. Requirements:

- **Reproducible:** one command turns a tagged source tree into a flashable image — via the **existing house pipeline, `misc-tools/build-image.sh`**, the same machinery that already builds the shipping micropanel image (`sudo ./build-image.sh --board=micropanel --base-profile=qt-bookworm --version=01.20`). This product adds a `board-configs/micropanel-touch/` entry to misc-tools: a `board.conf` (base Lite image, runtime-deps, hook list — no Qt profile needed), and a hook that clones this repo inside the image chroot and runs the standard CMake configure/build/install, exactly as micropanel's hook does today.
- **Division of responsibility:** *this repo* owns "check dependencies (fail loudly if missing) → build → `cmake --install` everything, including the systemd unit, under `CMAKE_INSTALL_PREFIX`" — mirroring micropanel's CMake (`INSTALL_SYSTEMD_SERVICE` option, `configure_file`d `.service.in`, screens/themes installed under the prefix). *misc-tools* owns image assembly: the `config.txt` overlay line, partition/read-only scheme, build-dep install/purge, runtime-dep reassertion, and service enablement.
- **Auditable delta:** the image differs from stock Lite by an enumerable list (config.txt line, app + configs + scripts, systemd units, partition/fstab changes, disabled services). The board-config in misc-tools *is* that list, like [hw-findings]'s "exactly one line added" discipline, extended to the whole image.
- **Imager-friendly:** publish `.img.xz` compatible with Raspberry Pi Imager. Imager's OS-customization (hostname, WiFi, username) must not break the read-only scheme or the appliance account model — verify in Phase 5.
- **Release artifact spec:** each release = `.img.xz` + SHA-256 + version manifest (pinned base-image URL **and SHA-256** — micropanel's board.conf currently leaves that field empty; this board must not — plus this repo's tag, the LVGL commit, the pinned micropanel config revision, and dependency package versions) + build log + SBOM with license notices. "Reproducible" here means **pinned-input repeatability** (same manifest → same content), not bit-for-bit determinism. Release verification flashes the *published compressed artifact*, not the working `.img`.
- **Production access posture:** the inherited pipeline enables password SSH and the Qt profile carries a fixed default password — a redistributable WiFi-capable image must not. Policy: an appliance service account distinct from any interactive user; no known/default credentials; SSH disabled by default or key-only via Imager provisioning; unique host keys (first-boot regeneration, persisted to `data` so RO root doesn't clone them). A release-time image audit fails on default passwords, password-SSH contrary to policy, shared host keys, or wrong install ownership. The development-mode override (bench units) is documented and visibly distinct.
- **License gate:** the image redistributes far more than LVGL — OS packages, flashing tools, firmware/bitstreams, media, fonts. Every installed artifact gets a redistribution-rights/notice/source-offer review recorded in the SBOM; unapproved artifacts fail the release build.

### 6.2 Power-cut safety: read-only by design

Users will cut power with no ritual. On Pi OS Lite the proven shape is:

- **Rootfs read-only** with an **overlayfs upper in tmpfs** (the mechanism behind `raspi-config`'s Overlay FS option; we bake it in rather than toggling it). `/boot/firmware` mounted read-only.
- **A third, writable `data` partition** (ext4, small) for the few things that must persist: `PersistentStorage` state (§6.4), optionally rotated logs. Mounted with conservative options (`nofail`, bounded fsck); corruption of `data` must never prevent boot or app start — on mount/read failure, fall back to tmpfs defaults and recreate on a positively identified device (never format by guessed name).
- **A complete write-path inventory is a design artifact**, not an afterthought: every writer in the image — app state, NetworkManager profiles, SSH host keys, machine identity, journals, DHCP leases, clock state, USB mount metadata — gets an explicit disposition: *tmpfs/volatile*, *bind-mounted into `data`*, *deliberately persisted*, or *prohibited*. The durable-commit protocol for anything persisted is: write temp → `fsync(file)` → atomic rename → `fsync(parent dir)`. The legacy `PersistentStorage` does temp+rename **without any fsync** (`PersistentStorage.cpp:164-205`) — that implementation must not be copied. Power-cut testing covers each write category, not just idle operation.
- **Action logs** (`log_file` in the configs) default to tmpfs. The existing collect-logs-to-USB pattern (micropanel's `scripts/collect-logs-to-usb.sh`) is the export path, surfaced as a menu action.
- **Consequence for development:** the image boots in RO mode for users; a documented switch (kernel cmdline toggle) flips a bench unit to RW for development. Building the app *against* RO assumptions from Phase 1 avoids a painful retrofit.

Boot time on Pi OS Lite (~15–25 s to app) is accepted as a trade-off of the OS choice. Mitigations that are cheap and safe — disabling unneeded services, no network-wait, `boot_delay=0`, quiet console with an early splash on the panel (fbcon is already on `fb0`, so even a one-shot service that blits a logo works) — go into Phase 5. If field feedback later says boot time is a real problem, that is the trigger to revisit Buildroot, not before.

### 6.3 Display seam: exactly one class now, cheap to add more

Zero-touch and "design for more displays later" combine into this v1 design:

- **The default image ships with the `piscreen` overlay enabled unconditionally.** SPI panels are write-only and cannot be probed, so the default's "auto-detect" means: pre-configure the PiScreen class and make that configuration harmless when nothing is attached. The verified Luckfox 3.5-RPi-LCD-CTP is selected at image-build time with `--variant=luckfox-ctp`, not by runtime guessing: its ST7796S MIPI-DBI/GT911 boot profile has mutually exclusive SPI0/GPIO claims. This is the only honest zero-touch model for SPI panels.
- **The app selects its display at startup, by evidence, through one small `DisplayBackend` seam:** enumerate DRM connectors via stable `/dev/dri/by-path` names ([hw-findings] §2.5), pick the connected one by priority (SPI panel → future DSI → future HDMI), and hand LVGL the right fbdev/DRM target plus the display's geometry. v1 implements only the SPI/fbdev backend; the seam is the enumeration + geometry handoff, not speculative abstraction.
- **Touch selection mirrors it:** enumerate `/dev/input/event*`, pick by capability signature (ABS_X/Y + BTN_TOUCH, no MT axes ⇒ resistive class), apply a per-driver quirk entry (ADS7846: range scaling from kernel-reported ABS min/max plus the `BTN_TOUCH`-ordering guard). **The kernel already applies the DT touchscreen transforms** (`touchscreen-swapped-x-y`, `invx`) — the app applies only the *residual* calibration measured on the bench, never a second swap/invert on top.
- **Panel profiles, and the capacitive variant** *(added 2026-08-11)*: display + touch selection is unified into a named **panel profile** = {boot configuration, geometry, touch driver signature + quirks, backlight control path, orientation/calibration mapping}. v1 ships the resistive ADS7846 PiScreen profile and the named capacitive Luckfox 3.5-RPi-LCD-CTP profile (ST7796S SPI + GT911 I²C). The latter uses a vendor-pinned MIPI-DBI command blob and a `goodix,addr=0x5d,interrupt=4,reset=17` overlay; it is selected by the image's `luckfox-ctp` variant because a write-only SPI panel cannot be safely identified at runtime. The evdev capability signature distinguishes touch behavior (Type-B MT axes ⇒ capacitive); LVGL consumes one contact either way, so screens and actions are identical. The GT911 needs no ADS7846 `BTN_TOUCH` ordering guard and normally needs no user calibration. Adding a future capacitive board remains a data/profile addition, not a UI rewrite, but requires its own image boot configuration and hardware acceptance.
- **Two honesty limits of this design** (accepted, documented): (a) a DRM connector path does not name `/dev/fbN` — the connector→card→framebuffer mapping is derived via sysfs and must be tested with HDMI absent *and* present, since probe order renumbers devices; (b) the SPI connector reporting "connected" is configuration, not physical detection — a write-only panel cannot be probed, so v1 detects the *configured backend*, and defined behavior for "framebuffer exists but glass is absent" is part of the backend contract.
- **Backlight ownership:** the overlay owns panel/backlight GPIOs, so the HMI must use only a kernel-exported interface and never fight that ownership through raw GPIO. The Luckfox-only boot profile deliberately selects `mipi-dbi-spi` PWM backlight support on GPIO 18, exposing `backlight_pwm/brightness` with discrete levels. It powers both standby and the 5–100% brightness UI; the user percentage is mapped to the closest supported level. A variant-only udev rule grants access solely when that `brightness` node is created, avoiding a panel-probe timing race. The bench PiScreen's 2026-08-15 three-second libgpiod low drive of GPIO 22 produced no visible change: its backlight is fixed-on, so it enables neither standby nor percentage brightness. The managed PWM profile explicitly disables the competing analogue headphone-audio route; this is a recorded hardware trade-off.
- **No first-boot calibration wizard** (zero-touch). Ship the bench-derived mapping as default. The implemented **System → Touch Calibration** rescue hatch takes five numbered target taps, fits a residual axis-aligned X/Y correction, rejects inconsistent or implausible results, applies a valid result immediately, and atomically stores the versioned correction in the `data` partition. It validates the saved panel geometry and evdev ranges at boot, falling back safely to the default mapping when the file is absent or incompatible. Re-running the flow replaces a prior correction; its two-tap **Reset default** control durably removes the saved file and restores the factory mapper immediately. SSH removal plus service restart remains break-glass recovery.

**Clone variance is the #1 product risk** (§10) — the supported-hardware list published to users must name specific, purchasable models that were actually tested.

### 6.4 Persistence, answered

[hw-findings] §8.6 left `PersistentStorage`/`ModuleDependency` parity open. Answer: keep the *feature* (some screens legitimately remember state, e.g. selected image/pattern, and §7.1's WiFi credentials need a home), back it with the `data` partition, and treat loss of that partition as a non-event (defaults apply). Reflash-as-update already means users tolerate state loss.

### 6.5 The engine/renderer split stays

The [hw-findings] §7.4 architecture (JSON + `ActionRunner` + navigation engine, renderer-agnostic core, LVGL frontend) is retained unchanged. It is what makes both future display classes (§6.3) and a future remote web UI possible without re-litigating this PRD. No web UI work in v1.

Two architecture rules added after solution review (sol-review-v1):

- **The UI thread is the sole caller of every LVGL API.** LVGL is not thread-safe, and `lv_async_call()` is *not* on its short exempt list. Workers (ActionRunner, command providers, network scans) push immutable events into a thread-safe queue; an LVGL timer or eventfd drain consumes them on the UI thread. This is the one concurrency pattern in the codebase, stress-tested in CI.
- **One cancellable command-execution service for *everything* that runs external commands** — not just `ActionRunner` actions but also `textbox` scripts, `items_source`/`items_action`/`list_selection` providers and built-ins. The legacy code runs several of these through blocking `popen()` with no timeout; in this product every invocation has a timeout, an output-size bound, a cancellation token, and UI-thread event delivery, with defined stale-result handling when the user navigates away mid-command.

### 6.6 Privilege and execution security model

The shipped configs invoke reboot, edit `/boot/firmware/config.txt`, reconfigure networks, mount media and drive flash hardware — and the legacy engine builds those commands by unescaped string substitution (`$1` ← item title) executed via `/bin/sh -c`, where titles can come from filenames, SSIDs or command output. On a redistributable, WiFi-capable image this is a real security boundary, so it gets a real design (frozen **before** ActionRunner implementation):

- **Non-root UI process** as the default posture; hardware/system access via udev group permissions where sufficient, and a narrow, allowlisted privileged path (sudo rules for specific commands, or a small broker / per-action systemd units) for reboot, boot-config edits, network changes, mounts and flash devices.
- **Argv, not strings:** internally actions are executable + argument vector; dynamic values are validated before substitution. `/bin/sh -c` survives only inside an explicit legacy-compatibility adapter for the shipped configs' shell one-liners, with dynamic-value sanitization at that boundary.
- **Service hardening** to the extent the action set allows: `NoNewPrivileges`, filesystem protections, device allowlists, capability bounding — with each relaxation traceable to a named capability in the parity matrix (§7).
- The threat model and the chosen privilege architecture are documented in `docs/` as a Phase-1/2 deliverable.

### 6.7 Action handlers: two tiers, and the pack rule

The scripts and tools that JSON actions invoke are packaged in exactly two ways:

- **Tier 1 — core handlers, in this repo.** A `handlers/` directory holding the *generic, dependency-light, hardware-agnostic* glue: network info/static-IP/DHCP/WiFi wrappers, system/CPU/RAM/temperature collectors, log-collection-to-USB, brightness. CMake installs them under the prefix (`$PREFIX/usr/bin`) together with `screens/` and `themes/`, and configs reference them only through the execution context (`$MICROPANEL_HOME/usr/bin/…`, §7) — never absolute paths. Constraints that keep this tier portable: handlers may use only standard Linux interfaces (`/sys`, iproute2, NetworkManager, coreutils); anything board-specific lives behind the platform seam, not in a handler; every handler speaks the §7.2 result contract (markers, progress, exit codes). **Consequence: one `cmake --install` yields a self-sufficient base device** (menus + network config + monitoring) on any supported SBC, with no imager involved — this is the portability foundation for future boards (Pi Zero 2, Orange Pi) and display classes.
The isolated eth0 DHCP-server capability is the explicit appliance exception: its typed broker handler is packaged here for protocol review, but it requires the board image's dnsmasq unit and root-owned `/data` state, so it is unavailable in a bare portable install.

- **Tier 2 — domain packs, never in this repo.** FPGA flashers and bitstreams (xc3sprog, openFPGALoader, sp6bins, rh850-flash-tools), CAN/UART/I2C tooling, network-analyzer extras: separate repos/artifacts with their own licenses, release cadence, size, and (sometimes) proprietary status — all reasons they must not enter this public repo (§6.1 license gate). They are composed **at image level** by the misc-tools board-config, driven by the capability matrix (§7). Image flavors (base / FPGA field tool / protocol debugger) are just different pack stacks over the same app.
- **The pack rule (dangling-reference ban):** a JSON screen config may only reference handlers guaranteed by *its own package* or by Tier 1. Each domain pack therefore ships its **own menu-fragment config together with its handlers** — screens and handlers always travel as one unit, and the capability matrix verifies the closure at image build. This is the F-01 operational-parity lesson enforced at the packaging layer.

**Image-flavor policy, 2026-08-20 (owner).** There is **no jumbo image**. The
base lab tool ships on its own; fpga/mcu-flash, camera/gstreamer monitoring,
serial-terminal, media and IT-diagnostic packs arrive later as *image-level
pack stacks* over the same app, exactly as this section describes. Two hard
admission criteria apply to every flavor: it must fit **both** 2 GiB ceilings
(the GitHub release asset limit and the reserved `MP_FACTORY` partition), and
it must carry its own bench acceptance — so the number of flavors stays
deliberately small.

Migration note: built-in screens that currently shell out from C++ (e.g. the Sprint 1 `nmcli` calls) converge onto Tier-1 handlers invoked through `CommandService`, so config-defined actions and C++ built-ins share one execution path.

### 6.8 Automated UI verification: control + capture interface

UI/UX work (layout, text sizing, themes, orientation) needs a machine-usable feedback loop so automated agents (claude-code/codex) can navigate the UI and verify what was actually rendered — catching regressions without a human staring at the panel.

- **Control endpoint on the engine:** a line-delimited JSON protocol over a **Unix domain socket** by default (e.g. `/run/micropanel-touch/control.sock`), with an optional `--control-port` loopback TCP mode for bench use (remote access via SSH forwarding — the socket itself never binds beyond localhost). Commands: navigate to a module id, activate an item, back, text entry, synthetic tap at coordinates (injected through the normal indev path so hit-testing is exercised), and state queries (current screen id, nav stack).
- **Thread rule holds:** socket commands are posted through the `UiEventQueue` and executed on the UI thread — the §6.5 "UI thread is the sole LVGL caller" invariant is not negotiable for test tooling either.
- **Two capture forms, both behind a render-settle barrier** (command completes only when no dirty areas remain and the flush has finished, making captures deterministic):
  1. **Semantic dump** — the serialized widget tree (widget type, geometry, text content, key style values) as JSON. This is the primary regression instrument: assertions on text and layout survive font antialiasing and theme tweaks that make raw pixel diffs brittle.
  2. **Pixel capture** — the rendered buffer (or `/dev/fb0` readback) as raw RGB565 + geometry header; PNG conversion happens host-side. Used for theming/rendering regressions and for a human/AI to *look at* the screen.
- **Headless CI backend:** the same engine renders into a memory display on the build host, so tree/pixel tests run in GitHub Actions with no hardware; bench runs re-validate the real panel path. This is the engine/renderer split (§6.5) paying rent.
- **Production posture:** compiled in but **disabled by default in release images**; enabling it is a documented dev-mode action, and the §6.1 release audit fails an image that ships with it enabled. It executes UI-level commands only — never arbitrary shell — and is not a user-facing remote API (the §6.5 web-UI seam remains separate future work, though it may later reuse the same engine surface).

---

## 7. Config compatibility contract (the binding interface)

The existing `micropanel/screens/*.json` files (14 configs, pinned to a specific micropanel commit recorded in the image manifest — never unversioned copies) are the contract between the OLED product and this one. "Compatibility" is claimed at three independently tested levels, and only the third counts as replacement parity:

1. **Syntactic** — the config parses; known types/keys recognized, unknown keys ignored.
2. **Navigational** — every intended node and edge resolves, with the *observable legacy semantics* below preserved (or intentionally broken, on record).
3. **Operational** — the command actually runs: its binary/script/firmware exists in the image, with the required devices, permissions, services and writable paths. This level is driven by a **generated capability/parity matrix** — for every reachable action/built-in: the exact command after expansion, owning package/artifact, device+permission needs, writable paths, and a status of *supported / remapped / deliberately unavailable (with on-screen explanation) / retired*. The matrix is machine-readable and is the source of truth for the misc-tools runtime-deps, hooks and udev rules. A release ships no placeholders and no late "command not found" — every legacy capability is either operational or intentionally, visibly dispositioned. (Note: rejecting Qt for the on-device UI does not by itself retire Qt-era *external* workflows in the configs — media/pattern entries that depend on them get explicit matrix decisions.)

Precisely:

- **Must load verbatim:** every module type and key inventoried in [hw-findings] §7.1 — `menu`/`submenus` (with reserved `back`), `GenericList` with `list_items` (`title`, `action`, `async`, `timeout`, `log_file`, `progress_title`, `result_pattern`, `result_prefix`, `usb_blaster_duration`, `parse_progress`) and dynamic sources (`items_source`, `items_action` with `$1`, `list_selection`, `prepend_static_items`, `items_path`), `textbox` (`depends.script_path`, `refresh_sec`, `display_title`), `action`, and the type-less built-in ids used by the shipped configs (`netinfo`, `system`, `cpu_temp`, …).
- **Action-semantics parity, not interaction parity:** an action defined in JSON produces the same process, arguments, progress interpretation and result extraction as the OLED build (result semantics per §7.2). *How the user navigates, enters data and sees results is explicitly free to differ* — see §7.1.
- **Execution context, not file mutation:** the configs are full of environment-specific tokens — `$MICROPANEL_HOME` appears **127 times in `config-pios-new.json` alone**, and older configs carry hard-coded `/home/pi/micropanel`-era paths. The legacy image resolves these by *rewriting the JSON at install time* (`update-config-path.sh`). This product instead expands tokens at runtime through an explicit execution context (`MICROPANEL_HOME`, data dir, log dir) so source JSON is never mutated; remaining hard-coded legacy paths are served by a documented compatibility layout/symlink map. Golden tests assert the **fully expanded command line**, not merely successful parsing.
- **Observable legacy semantics are contract too** (verified against the legacy source; each labeled *preserve*, *safe intentional break*, or *legacy bug not relied on*, with golden tests generated before the legacy engine is replaced):
  - `enabled: false` gates only *top-level registration* — the module is still constructed and reachable from other menus (`MicroPanel.cpp:372-464`).
  - `GenericList` exits on an item **titled** `Back`/`back`/`BACK`, independent of the reserved `back` id (`GenericListScreen.cpp:262-270`).
  - `$1` substitution replaces only the first occurrence, unescaped (`GenericListScreen.cpp:377-384`).
  - Dynamic lists preserve only static items titled `Back`/`Stop-Playback`, silently dropping others (likely a legacy bug — decide, don't inherit accidentally).
  - `depends` is a heterogeneous property bag; the legacy checker only validates values beginning with `/`.
- **Unknown keys are ignored** (forward compatibility), and new touch-only keys are **optional and additive** — module-level `layout`, `columns`, `rows`, `accent`, `input`; item-level `icon`, `color`, `enabled` on `submenus[]`/`list_items[]` entries (§8) — so one JSON file can drive both products for as long as the OLED variant lives. Ignoring unknown keys is *not* the same as accepting broken structure — see validation below.
- **Structural validation is strict:** duplicate ids, dangling submenu references, cycles, wrong field types, invalid timeouts and empty menus are diagnosed with human-readable errors by a host-runnable `micropanel-touch --validate-config`, run in CI over all 14 configs; a release image build **fails** on invalid shipped config rather than rendering placeholders.
- **`items_path` (10 uses) must be studied and implemented** — it was flagged unstudied in [hw-findings] §8.7 and is inside the contract, not outside it.
- Built-ins that are meaningless on this hardware (e.g. `invert_display` for the OLED) map to a no-op or a touch-appropriate equivalent (brightness → the kernel-exposed backlight interface, §6.3), decided case-by-case in Phase 4 and recorded in the parity matrix. The actual type-less built-in registry in the legacy code is 14 ids (`MicroPanel.cpp:563-584`: `hello`, `counter`, `brightness`, `network`, `system`, `textbox`, `internet`, `wifi`, `ping`, `netinfo`, `netsettings`, `speedtest`, `throughputserver`, `throughputclient`) — the matrix is generated from code + configs, not from examples.

The parity test, with honest denominators: `config-pios-new.json` contains **55 module declarations and 59 submenu references** (not "114 modules"); counts are CI-generated per config so coverage reporting can't drift. All 14 shipped configs validate, load with zero edits, reach navigational parity, and every *supported* workflow passes its operational matrix row.

### 7.1 Touch-first upgrades: encouraged divergence

micropanel's interaction patterns were shaped by a 128×64 OLED and five buttons — those constraints are gone, and faithfully reproducing their workarounds would waste the new hardware. Where the OLED build contorted, this build should do the natural touch thing. Divergence of this kind is a goal, not a compatibility violation, as long as the underlying action semantics (§7) are preserved. Known cases:

- **Free-text entry via on-screen keyboard.** The OLED build enters IP addresses by rotate/press digit-scrolling (`IPSelectorScreen`) and cannot enter WiFi passwords at all. Here, a built-in keyboard (LVGL `lv_keyboard` + `lv_textarea`) handles both: numeric mode for IP/netmask entry, full QWERTY with password masking for WiFi credentials. Resistive single-touch is fine for this — tap typing, no swipe — with ≥40 px keys (a 10-column QWERTY at 480 px wide gives 48 px keys). Proven as a **framework capability gate** (plan, Sprint 2): masked password entry demonstrated on the physical panel, with the secret provably absent from logs/events, *before* the real Wi-Fi join action is wired.
- **Direct-manipulation controls.** Continuous values — backlight brightness, audio volume — get horizontal *and* vertical `lv_slider` controls: ≥40 px hit areas, themed by the skin, with the §8 redraw law applied to drag feedback (a dragged slider invalidates only its own track/knob region — measured on the SPI bus, not assumed). Also a **framework capability gate** (plan, Sprint 2) before any real volume/brightness action binds to them.
- **Direct list interaction.** Long `GenericList`s scroll by drag and select by tap; no cursor walking. Type-ahead filtering via the keyboard becomes possible for very long dynamic lists (e.g. image files on USB).
- **Richer built-ins.** Screens like `netinfo`/`system`/`cpu_temp` were line-budgeted to 8×8 text; here they can use proper layout, units, and live-updating values at the 2–4 Hz refresh discipline.
- **Richer menu presentation.** Menus may render as icon-tile grids (2×2, 2×3, …) with per-button colors and images, driven entirely by the additive keys in §8 — pure presentation on top of unchanged action semantics, and the OLED build ignores every one of those keys.
- **Confirmation and error surfaces.** Destructive actions (flash, reset) get a real confirm dialog; failures get the result card plus a scrollable log view instead of a truncated one-liner.

Each upgrade lands in the phase that owns the underlying module (§9); the keyboard lands with the WiFi/IP settings screens in Phase 4. New JSON keys these upgrades need (e.g. an `input: "text" | "ip" | "password"` hint on a list item) follow the additive-optional rule from §7.

### 7.2 Action result semantics (corrected by sol-review-v1)

The legacy engine does **not** judge success by exit code. Verified behavior (`GenericListScreen.cpp:702-820`): it reaps the child, *ignores the exit status*, and decides success by scanning the log for `[SUCCESS]` or RH850 verification phrases while requiring the absence of recognized error strings; **with no `log_file` configured, success is assumed unconditionally**. `result_pattern` only extracts display text; progress parsing accepts the last numeric `%` found anywhere in the log. The legacy child also opens stdout and stderr on the same path in `w` mode, giving overlapping/truncated log semantics that must not be copied.

v1 therefore defines an explicit **`ActionResult` precedence table** — spawn failure → timeout/cancel → killed-by-signal → log success/error markers → exit code → no-log default — documented alongside the config schema, with every intentional deviation from legacy behavior recorded. Output capture is ordered and non-corrupting (single pipe, combined stream) with size/retention limits. Fixture-based golden tests use representative FPGA, RH850 and generic logs, including contradictory markers and nonzero-exit-with-`[SUCCESS]` cases. Parity is claimed only when the table and the tests agree.

### 7.3 Process lifecycle contract

Legacy behavior: fork `/bin/sh -c`, remember only the shell PID, signal only that PID — descendants of the shell can outlive cancellation. This product specifies: one job at a time; each job in its own session/process group; PGID-wide `SIGTERM` with a grace period, then PGID-wide `SIGKILL`; guaranteed reap (no zombies); orphan cleanup at app startup; defined behavior for app crash/service restart during an irreversible flash phase (a systemd transient scope per action is an acceptable implementation if it simplifies cgroup ownership). "Sane child handling" is not a demo adjective — tests assert *no surviving descendants* after cancel, timeout, service restart, and `SIGKILL` of the UI.

---

## 8. UI direction

The layout language from [hw-findings] §7.5 stands (40 px chrome, tile grid for menus, 56 px rows for long lists, 48 px minimum touch targets, the progress-screen design). Additions:

- **Layout in relative terms** — LVGL flex/grid with percentage/`LV_SIZE_CONTENT` sizing and a small set of scale tokens (chrome height, row height, font pair) derived from the resolution the `DisplayBackend` reports. Not full DPI independence; just no hardcoded 480/320 literals outside the theme.
- **Orientation is a boot-profile choice, executed by the panel controller, not by LVGL.** *(Revised 2026-08-10 — bench evidence overturned the earlier runtime-rotation design.)* LVGL software rotation turns its logical horizontal refresh stripes into native-space columns, which the fbdev damage path inflates to near-full-frame SPI writes — measured ~6–8× slower portrait with a visible raster. The shipping mechanism is therefore the DT overlay's `rotate=` parameter (MADCTL scan direction — zero runtime cost). Verified profiles: **portrait `rotate=90,xohms=100,swapxy=1` → 320×480 (current default)** and landscape `rotate=0,xohms=100,invx=1` → 480×320. Touch mappings are per-profile and bench-verified, never derived. LVGL runtime rotation survives only as a **preview/development aid** with its documented sluggishness. The `orientation_select` screen accordingly offers *preview now* (runtime rotation) and *apply at next boot* (rewrites the managed overlay line through the §6.6 privileged boundary; requires that boundary to exist). **Its menu entry is disabled as of 2026-08-20** (`"enabled": false` in the Display submenu) because that boundary does not exist yet: the screen previews only, and a tile that cannot apply what it appears to offer is worse than no tile. Re-enabling is one boolean once the overlay-rewrite path lands. Layouts must reflow correctly in both geometries via the relative-layout rule above. **Known constraint:** a full QWERTY at 320 px wide yields ~30 px keys, below the 48 px touch minimum — numeric/IP keypads are fine in portrait, but free-text entry needs a portrait-specific answer (taller split layout, or a landscape-style keyboard presentation); decided with real fingers during the theming sprint.
- **Menu presentation is config + skin data, never code:**
  - **Per-menu layout** via the optional additive `layout` key: `"list"` (default single-column rows) or `"grid"`, with optional `columns` (default 2; 1–4 supported so the narrow portrait panel keeps 48 px touch targets). The top-level module list uses the same keys in an optional top-level `root` object. Grid items flow row-major into square-ish tiles sized from `content_width / columns`, with an optional `rows` key setting the tile height as `content_height / rows` — 2×2 and 2×3 arrangements are simply item count × column count, in either orientation. **A menu that does not fit its panel is a defect, not a scroll** (revised 2026-08-20): a tile scrolled out of reach is indistinguishable to a user from a button that does nothing, so the headless UI suite asserts every button of every visited menu lies fully inside the panel, with the expected tile count. Overflow therefore fails the build rather than degrading silently; growing a menu past its grid means raising `rows`, shrinking the tiles, or regrouping the items. *Evidence is portrait-only* (320×480) — the landscape profile shares the "landscape checks pending" bucket with the rest of the Sprint 2 capability gates.
  - **Per-item availability** via the optional additive `enabled` key on a `submenus[]` entry, which **defaults to true** — deliberately the opposite of a *module's* `enabled`, which defaults to false: a module opts in to appearing on the root screen, whereas an entry someone has written into a `submenus` array is plainly meant to show unless switched off. A disabled entry is **hidden, not greyed** (a control that cannot act is worse in front of a user than one they never see) and stays in the JSON as the record of a feature that exists but is not yet wired up.
  - **Per-item appearance** on `submenus[]`/`list_items[]` entries: `icon` — a named built-in symbol (e.g. `"wifi"`, `"settings"`) or an image file path, resolved config-relative first and then against the active skin's icon directory; and `color` — a per-button hex accent override, layered over the module `accent` and the skin.
  - **Skin tokens** style whatever the config places: button/tile corner radius, icon size, tile label position (below/beside the icon), pressed-state treatment.
  - **Image discipline:** PNG icons decoded through LVGL's built-in decoder and cached after first decode; ≤64×64 recommended for tiles. A tapped tile invalidates only itself — comfortably inside the SPI budget. Full-tile photographic backgrounds violate the redraw law above and are rejected by design, not by review.
- **Redraw discipline as theme law:** flat fills, no gradients/shadows/screen-wide animation — the [hw-findings] §6 rules encoded once in the LVGL theme so screens can't accidentally violate the SPI budget. The on-screen keyboard obeys the same law: it appears instantly (one ~full-screen redraw, ≈100 ms at 24 MHz, acceptable as a one-shot) rather than sliding in (which would dirty its whole area every animation frame).
- Visual tone: modern-flat, high-contrast (field use, possibly sunlight), large type. Dark theme default (glare and perceived polish favor it); accent color per top-level section via the optional `accent` key.

---

## 9. Phasing

| Phase | Deliverable | Exit criterion |
|---|---|---|
| 0 | **Image pipeline skeleton**: `misc-tools` `--board=micropanel-touch` board-config producing a bootable Lite image with the overlay line, the **final partition topology (RO root + `data`) in minimal form**, + a hello-LVGL binary autostarting | One `build-image.sh` command → flashable image; flashed card boots **read-only** to pixels on the panel with no manual steps |
| 1 | **Spine prototype** (the decision point): JSON loader, `menu` + static `GenericList`, extracted `ActionRunner`, progress screen with log tail + result card | `config-pios-new.json` loads verbatim; one real async action (FPGA flash or a 350 s simulation) runs with live progress on the panel; UI feel judged acceptable |
| 2 | **Performance measurement**: real frame times at 24/32/48 MHz SPI, chosen default; scroll/animation policy validated | Numbers in the repo replacing [hw-findings] §8.1–8.2 arithmetic |
| 3 | **Module parity**: `textbox`, `action`, dynamic `GenericList` incl. `items_path` | All shipped configs load; every `type`-ed module works |
| 4 | **Built-ins parity + touch upgrades**: type-less ids resolve; no-op/remap table for OLED-specific ones; on-screen keyboard for IP + WiFi credential entry (§7.1) | Every id reachable from shipped configs resolves; parity table published; WiFi join + static-IP set performed entirely on-device |
| 5 | **Production image**: RO rootfs + overlay + `data` partition, power-cut test (scripted 100× cut-during-write soak), boot tuning + splash, Imager compatibility check, supported-hardware list with ≥2 purchasable panel models tested | Flash → cut power at random points ×100 → always boots to app; documented HW list |
| 6 | **Release machinery**: versioning, image changelog, the enumerable stock-delta list, user-facing flash-and-go doc | A stranger with the doc + Imager + listed HW builds a working device |

**Sequencing note (2026-08-11):** the plan (`micropanel-touch-plan.md`) inserts an **infrastructure-consolidation sprint (2.5)** between the engine spine and the menu fan-out, pulling the dependency-complete `misc-tools` image, the RO-rootfs shape, the panel-profile display HAL (incl. the **capacitive** 3.5" variant), and **display power management** earlier than these phase rows imply. The rationale: build the production runway before implementing dozens of menu items, so each item lands on the real image layout rather than being retrofitted. Phase rows above describe deliverables; the plan owns the order.

Phase 1 remains the single judge-the-stack moment; Phase 5 is where "committed replacement" earns its name.

---

## 10. Risks & open questions

1. **Clone variance (top risk).** "3.5 inch RPi LCD" clones vary in controller (ili9486 vs 9488), SPI speed tolerance, and touch axis orientation. Mitigation: a second, physically different unit is purchased and run through the *initial* bring-up acceptance (plan Sprint 0/1) so the quirk model freezes against two data points — the tested-models list at release (Phase 5) merely re-confirms it.
2. **SPI speed above 24 MHz still unmeasured** — Phase 2. Frame-time claims remain arithmetic until then.
3. **Pi OS Lite churn.** trixie vs bookworm already behave differently (runtime-overlay behavior, [hw-findings] §3). Pin the base image release per micropanel-touch release; re-validate on each base bump.
4. **Boot time acceptance.** ~15–25 s on Lite is assumed tolerable for the field workflow. Validate with a real user in Phase 1; Buildroot remains the escape hatch only if this fails.
5. **Pi 4 supply/EOL horizon** — the image pins to Pi 4 today; Pi 5 support (different firmware/overlay landscape) is a known future cost, not in v1.
6. **Battery/power hardware is user-supplied and unspecified** (powerbank assumed). Undervoltage behavior on cheap powerbanks under FPGA-flashing load is untested; worth one bench soak in Phase 5.
7. **WiFi credential handling** (new with §7.1): entered passwords persist in the `data` partition on an otherwise read-only device that users may hand around. **Decided by the owner, 2026-08-20:** joining a hotspot stores the credential as a NetworkManager keyfile on `/data`, root-owned and mode `0600` — plaintext-equivalent at rest. That is **accepted for this lab tool**: the same physical access that reads the keyfile also reads the card, and a factory reset (wiping `/data`) is the documented recovery. The credential is never allowed into logs, UI events, broker replies or control captures — that boundary is tested, and the tests cover the real join path, not only the demo screen. The same acceptance covers the credential's life *in memory*: the LVGL text area's heap after it is cleared, the `std::string` copies between the screen and the broker, and the anonymous `memfd` the handler reads it from are none of them zeroized. Within this threat model that is not worth the machinery — the account that would read them is the account that typed the secret — but it is an accepted limit rather than an oversight. **Encrypted-at-rest credentials are deliberately deferred to the CM4/eMMC/secure-boot milestone** ([`pi-in-system-update-plan.md`](pi-in-system-update-plan.md) §11), where a hardware-backed secret store makes it worth doing properly; doing it now would be a software secret protecting a software secret.
8. **Operational-parity scope creep.** The legacy image ships toolchains, firmware, media, Qt-era helpers and services the configs invoke (F-01 of sol-review-v1). The capability matrix (§7) is the instrument that turns this from an open-ended porting risk into an enumerated set of *supported / remapped / retired* decisions — but the decisions themselves (especially media/pattern workflows) need a product owner call early in Phase 3/4.
9. **Privilege model vs. functionality tension** (§6.6): least-privilege may silently break actions that assumed root. Every privileged capability is traceable in the matrix; breakage surfaces in operational-parity tests, not in the field.
10. **Capacitive-panel driver variance** *(added 2026-08-11)*: retail "3.5 inch capacitive RPi LCD" boards use different controllers (Goodix GT911, FT5x06/FT6236, …) on different DT overlays, and some route the touch IRQ/I²C differently from the resistive sibling. Mitigation: the panel-profile model (§6.3) makes each a data entry; the tested-models list names the specific capacitive board(s) actually verified, exactly as for resistive. Bring the capacitive unit through Sprint 2.5 acceptance before adding capacitive menu work.
11. **Network-mutation path tightness** *(added 2026-08-11, from fable-review-v8)*: the root broker is the only *root* network path, but stock Pi OS polkit lets an unprivileged UI-account compromise reconfigure NetworkManager directly, bypassing the broker's typed boundary. Mitigation: an image-level polkit rule restricting NM modification to root, appliance account not in `netdev`, recorded in the capability matrix (plan Sprint 2.5).
