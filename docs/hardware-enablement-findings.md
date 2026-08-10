# micropanel SPI-display variant — PRD

**Status:** draft for review
**Date:** 2026-08-09
**Target hardware:** Raspberry Pi 4 Model B Rev 1.5 + Waveshare 3.5inch Resistive Touch Display (B), 480×320 IPS, SPI
**Author's note:** every hardware claim below was measured on the bench Pi at `192.168.1.124` during the enablement session. Claims that were *not* verified are called out explicitly in §8.

---

## 1. Purpose

Build a handheld, battery-powered FPGA-flashing device whose UI is a menu navigator equivalent to the existing `micropanel`, but rendering to a 480×320 SPI touch panel instead of a 128×64 SSD1306 OLED, and driven by **the same JSON configuration format** (`micropanel/screens/config-pios-new.json`).

Two things are explicitly *not* in scope: reusing the existing Qt applications (`qt-demo-launcher`, `touch-gallery`, `disp-tester`), and running the SPI panel alongside the FPD-Link/Himax HDMI display. §4 and §5 explain why both were ruled out on evidence.

---

## 2. Hardware enablement — settled

### 2.1 The entire display configuration is one line

On a fresh Raspberry Pi OS Lite image, append to `/boot/firmware/config.txt`:

```
dtoverlay=piscreen,drm=1,rotate=90,xohms=100,swapxy=1
```

Nothing else. No `LCD-show`, no `fbcp`, no packages, no X. Verified by diffing against the pristine image `config.txt`: exactly one active line added, `apt` history empty.

Resulting state after reboot:

| | |
|---|---|
| `/dev/fb0` | `ili9486drmfb`, 320×480, 16 bpp RGB565, stride 640 |
| DRM | own card, connector `SPI-1` reports `connected` |
| Touch | `ADS7846 Touchscreen` on an `/dev/input/event*` node |
| Console | `fbcon` auto-attaches; login prompt renders on the panel |

### 2.2 Why `piscreen` and not a Waveshare overlay

Raspberry Pi OS ships **no** `waveshare35*` overlay. The bundled OzzMaker `piscreen` overlay has an identical pinout — verified by decompiling `piscreen.dtbo`:

| Signal | GPIO | Header pin |
|---|---|---|
| LCD chip select | 8 (CE0) | 24 |
| Touch chip select | 7 (CE1) | 26 |
| LCD DC/RS | 24 | 18 |
| LCD RESET | 25 | 22 |
| Touch PENIRQ | 17 | 11 |
| Backlight (`led-gpios`) | 22 | 15 |
| SPI0 SCLK/MOSI/MISO | 11/10/9 | 23/19/21 |

`drm=1` is the load-bearing parameter: it rewrites the node's `compatible` to `waveshare,rpi-lcd-35`, binding the in-tree DRM driver `drivers/gpu/drm/tiny/ili9486.c` instead of the legacy staging fbtft driver. That driver understands this board's **16-bit-wide SPI register quirk** (the on-board shift register), which the generic `panel-mipi-dbi` / `mipi-dbi-spi` path does not. It also yields a real KMS device rather than a deprecated fbdev-only setup.

### 2.3 Non-obvious traps

- **Syntax.** `config.txt` uses `=` and commas. The space-separated form (`dtoverlay piscreen drm=1 …`) is the *runtime CLI* syntax; in `config.txt` it is silently ignored and looks identical to a wiring fault.
- **Section placement.** The line must be under `[all]`. The stock trixie `config.txt` ends with `[all]`, so plain appending works — but appending after a `[pi5]`/`[cm4]` filter without reopening `[all]` silently disables it on a Pi 4.
- **`dtparam=spi=on` is not required.** The overlay enables `spi0` itself.
- **Native product mode is portrait.** `rotate=90` gives 320×480; `rotate=0`
  remains the landscape alternative at 480×320.
- **`swapxy=1` is required for the verified portrait mapping.** The override is
  an *inverted* boolean (`touchscreen-swapped-x-y!`), so it clears the overlay's
  landscape-default swap. Do not carry `invx=1` into the portrait profile.
- **Omit `speed=` unless that exact panel passes visual-integrity testing.** The
  driver default is stable on the bench panel; an explicit 32 MHz trial dropped
  physical glyph pixels despite correct framebuffer contents.

### 2.4 Touch controller

The panel's XPT2046 is register-compatible with the TI ADS7846; the in-tree `ti,ads7846` driver is correct and binds automatically. Measured behaviour:

- Reports `ABS_X`, `ABS_Y` (0–4095) and `ABS_PRESSURE` (0–255), plus `BTN_TOUCH`.
- **No multi-touch axes** — `abs` capability mask is `0x1000003` (bits 0, 1, 24 only). This single fact drives most of §4.
- Two deliberate physical taps produce exactly **two clean DOWN/UP cycles** with sane coordinates and pressure. The panel does not bounce.
- `BTN_TOUCH=1` is asserted **one packet before** the first coordinate packet. Any consumer that opens a contact on `BTN_TOUCH` alone will report every touch as starting at (0,0).
- Correct orientation for `rotate=0` is the overlay's built-in `touchscreen-swapped-x-y` **plus** `touchscreen-inverted-x`. Determined empirically with an on-screen tracking test; an earlier inference from corner-tap ordering was wrong and was corrected by live testing.

### 2.5 Device enumeration side-effects

- With no HDMI attached, `vc4` creates **no** `/dev/fb*` at all, so the SPI panel reliably becomes `fb0` and the console lands on it.
- Adding the panel **renumbers DRM cards**: `vc4` moved from `card1` to `card2` because SPI probes earlier. Anything hardcoding a card number breaks. Use the stable `/dev/dri/by-path/platform-gpu-card` (vc4) and `platform-fe204000.spi-cs-0-card` (panel).

---

## 3. Why not coexist with the existing Himax/HDMI setup

Tested directly on the micropanel image (bookworm, kernel `6.12.100-v8+`) by adding the overlay line and rebooting. Result: **the display did not come up, the DS18B20 1-wire sensor broke, and only the touchscreen worked** — the worst of both worlds, and silent unless you read `dmesg`:

```
pinctrl-bcm2835: pin gpio8  already requested by fe204000.spi; cannot claim for onewire@8
w1-gpio onewire@8: Error applying setting, reverse things back
pinctrl-bcm2835: pin gpio25 already requested by button@19; cannot claim for spi0.0
ili9486 spi0.0: Error applying setting, reverse things back
```

Five GPIO collisions between the panel and the micropanel `config.txt`:

| GPIO | Panel needs it for | micropanel claims it for |
|---|---|---|
| 8 | SPI0 CE0 / LCD chip select | `w1-gpio` DS18B20 |
| 17 | XPT2046 PENIRQ | `himax-touch` interrupt (via the HH983 serializer) |
| 22 | `led-gpios` backlight | `gpio-key` KEY_LEFT |
| 24 | LCD DC/RS | `gpio-key` KEY_UP |
| 25 | LCD RESET | `gpio-key` KEY_DOWN |

GPIO 17 is the hard blocker: the Himax touch interrupt is tunnelled back through the FPD-Link chain and lands on header pin 11, which is also PENIRQ. Two drivers, one interrupt line, no software resolution.

**A dual-mode SD card is feasible** (detect the HH983 serializer at `0x18` on i²c1; if absent, bring up the panel), and a working prototype was demonstrated. But it needs two workarounds, and the decision recorded here is to **not pursue it** — a dedicated image is simpler and the handheld is a distinct product. The findings are retained because they may matter later:

- **Runtime `dtoverlay` silently fails on kernel 6.12 for any overlay carrying a pinctrl group.** `fw_devlink` parks the device forever on `wait for supplier /soc/gpio@7e200000/<x>_pins`, because the GPIO controller probed long ago. Visible in `/sys/kernel/debug/devices_deferred`. This hits `piscreen`, `gpio-key` and `w1-gpio` alike, so the obvious "defer the conflicting overlays and apply whichever set matches" design cannot work there. It *did* work on trixie / kernel 6.18 — do not assume portability between the two images.
- The shape that does work: leave `config.txt` untouched, and in panel mode **unbind** the conflicting devices (`echo onewire@8 > /sys/bus/platform/drivers/w1-gpio/unbind`, same for `button@18`/`button@19` via `gpio-keys`) then apply a **pinctrl-free** copy of the overlay. Verified end-to-end; KEY_LEFT/KEY_RIGHT/KEY_ENTER survive.

---

## 4. Why not Qt — the decisive evidence

This is the single most important input to the technology choice, because the team's existing UI investment is Qt.

### 4.1 Qt5's evdevtouch is broken against this touch controller

Debian's Qt5 builds `evdevtouch` with **mtdev**, so `QEvdevTouchScreenHandler` always takes the multi-touch path and reads its coordinate ranges from `EVIOCGABS(ABS_MT_POSITION_X/Y)`. The ADS7846 has no such axes (§2.4), so those ioctls return a zero-width range. Qt then divides by zero and every touch point becomes NaN. Qt says so itself with `QT_LOGGING_RULES=qt.qpa.input=true`:

```
evdevtouch: /dev/input/eventN: Protocol type B (mtdev) (multi), filtered=no
evdevtouch: /dev/input/eventN: min X: 0 max X: 0
evdevtouch: /dev/input/eventN: min Y: 0 max Y: 0
```

and the journal fills with:

```
QGuiApplicationPrivate::processMouseEvent: Got NaN in mouse position
```

The panel appears completely dead. The Himax panel is a genuine MT device, which is why this has never surfaced on the FPD-Link rig.

### 4.2 The workarounds all leak

| Workaround | Outcome |
|---|---|
| `QT_QPA_FB_NO_LIBINPUT=0` (use libinput) | Fixes NaN; buttons respond; apps spawn. But libinput takes an **exclusive `EVIOCGRAB`** — no other process can read the device — and the double-fire below persists. |
| uinput shim republishing the panel as MT protocol-B | Fixes NaN through the production evdevtouch path (`min X: 0 max X: 4095`). Double-fire persists. |
| Same shim presenting as an absolute mouse (`BTN_LEFT`) | A cursor appears and a single tap does not register at all. |

### 4.3 One touch produces two button activations

Measured: the shim emitted **exactly one contact** while the launcher logged **two spawn attempts**. A USB mouse produces one. `qt-demo-launcher` connects to `QPushButton::pressed` (not `clicked`) at `main.cpp:1118`.

### 4.4 A latent bug in qt-demo-launcher turns that into total failure

`main.cpp:794` — the `QProcess::finished` lambda references the *member* rather than the sender:

```cpp
connect(m_runningProcess, &QProcess::finished, [this, program, appId](...) {
    this->show();
    if (m_runningProcess) { m_runningProcess->deleteLater(); m_runningProcess = nullptr; }
});
```

`launchAppAsync()` also `deleteLater()`s the previous process on every launch, and `~QProcess()` **kills** a still-running child. So when activation #2 kills child #1, child #1's `finished` fires and deletes `m_runningProcess` — which by then is child #2. Nothing ever stays open.

> **Recommendation to the wider team, independent of this PRD:** capture the `QProcess*` and guard with `if (m_runningProcess == proc)`. This is a latent bug on the Himax rig too — it only needs two activations to fire.

### 4.5 Conclusion

The framework's input stack — not the display, not the driver, not the panel — is what made Qt unusable here, and it cost the bulk of the enablement session. **Owning the input path is worth more than framework familiarity on this hardware.** Any toolkit where we read `/dev/input/event*` ourselves makes this entire class of problem disappear.

---

## 5. Technology decision

**Chosen: C++ with LVGL, rendering to `/dev/fb0`, reading evdev directly.**

### 5.1 Decision criteria, in priority order

1. **Own the input path** — §4 is the lesson; no framework may sit between us and the ADS7846.
2. **Partial redraw is mandatory** — §6 shows a ~10 fps ceiling for full-screen repaints.
3. **Battery and boot time** — a handheld field tool; every MB of RAM and second of boot is a cost.
4. **Keeps the JSON schema and action semantics** — §7.
5. Team familiarity — real, but ranked below the above.

### 5.2 Options considered

| Option | Verdict |
|---|---|
| **LVGL (C/C++)** | **Chosen.** Purpose-built for this display class: native dirty-rectangle rendering, an evdev input driver we configure, `LV_COLOR_DEPTH 16` matching RGB565 with no conversion, small RAM, ~1–2 s to first paint, no display server. |
| Python + SDL2/pygame | Viable and fastest to iterate; fine performance for menus. Rejected as the primary because of boot time, memory, and packaging on a battery device — but it is the fallback if LVGL development velocity disappoints. |
| Qt5 Widgets + linuxfb | Reuses team skills and existing code, but requires fixing §4.1 *and* §4.4 before it renders a single reliable tap, and carries the heaviest dependency footprint. Rejected. |
| Web UI on the panel (`cog`/WPE on DRM) | Most expressive UI authoring, and superficially attractive. Rejected for the panel: browser engines repaint large regions, which is exactly what a 10 fps SPI bus punishes, plus 200–400 MB RAM and slow boot. **Retained for the *remote* interface** — see §7.4. |

### 5.3 Honest cost of this choice

We lose reuse of `touch-gallery`, `disp-tester` and the Qt UI code. This is a deliberate trade: this handheld is a new product line, its UI is a menu plus progress reporting rather than image/pattern rendering, and the existing Qt apps were written for a 2560×1440 panel (the launcher's own config specifies a `2560×1440` window). If preserving that reuse turns out to matter more than hardware fit, the decision should be revisited — the alternative is Qt5 Widgets with libinput plus the `main.cpp:794` fix.

---

## 6. Performance envelope

A full 480×320 RGB565 frame is **307,200 bytes**. At the overlay's default 24 MHz that is roughly **100 ms of SPI traffic per full-screen repaint** — a ~10 fps ceiling, CPU-driven, drawing battery the whole time.

Design rules that follow:

- Partial redraw is the architecture, not an optimisation.
- No gradients, shadows or full-width animations that dirty the whole screen.
- Refresh progress indicators at **2–4 Hz**, not 60.
- `LV_COLOR_DEPTH 16` so no pixel conversion happens before the flush.
- Use LVGL's fbdev backend; the DRM backend buys nothing here.
- **Do not promote 32 MHz.** The first-panel trial produced dropped glyph
  pixels on the physical display even though `/dev/fb0` captures were correct;
  the driver default restored clean output. Higher clocks require per-panel
  visual-integrity testing before any performance comparison is meaningful.

---

## 7. Migration plan

### 7.1 What micropanel is (as studied)

A flat registry of `modules` keyed by `id`, forming a navigation graph. Module types observed across `screens/*.json`:

| type | count | behaviour |
|---|---|---|
| `menu` | 65 | list of `submenus` (`id`/`title`); `back` is a reserved id |
| `GenericList` | 85 | list of `list_items`, each with a shell `action`; optionally dynamic via `items_source` / `items_action` (`$1` template) / `list_selection` / `prepend_static_items` / `items_path` |
| *(no type)* | 136 | built-in C++ `ScreenModule` bound by `id` (`netinfo`, `system`, `cpu_temp`, …) |
| `textbox` | 19 | runs `depends.script_path` every `refresh_sec`, displays under `display_title` |
| `action` | 11 | built-in action by name (e.g. `invert_display`) |

`list_items` keys: `title`, `action`, `async`, `timeout`, `log_file`, `progress_title`, `result_pattern`, `result_prefix`, `usb_blaster_duration`, `parse_progress`.

Input primitive is **rotate(±1) + press**: `MultiInputDevice` auto-detects `button@N` / `rotary@N` and calls `onRotation(direction)`. The 5 GPIO buttons collapse to a linear cursor over a list. Display is abstracted behind `BaseDisplayDevice` (`drawText(x,y)`, `drawProgressBar`, `setInverted`, `setBrightness`) with an 8×8 font — text/OLED-centric, and not worth carrying forward.

The crown jewel is the async action runner inside `GenericListScreen.cpp`: spawn → tail `log_file` → two independent progress strategies (`parse_progress` scrapes a percentage from the log; `usb_blaster_duration` estimates from elapsed/known duration, capped at 99%) → scrape a result line via `result_pattern` and present it with `result_prefix`.

### 7.2 What transfers unchanged

**The JSON schema.** Nothing in it is OLED-specific — it is a navigation graph plus a shell-action model. The display primitives live in `DisplayDevice`, which the config never sees. The config files are the contract, and they are preserved verbatim.

New keys will be **optional and additive** (`icon`, `accent`, `layout`), and unknown keys must be ignored, so a single config file can still serve both the OLED box and the panel.

### 7.3 What must change

**`ScreenModule::run()` is a blocking per-screen loop** (`enter()` → loop `update()`/`handleInput()` → `exit()`). That is correct for an I²C OLED and wrong for LVGL, which wants one event loop with timers and a screen-object tree. Same lifecycle, no nested loops:

```
enter()        → build an lv_obj screen, lv_scr_load_anim()
update()       → an lv_timer_t at 2–4 Hz
exit()         → lv_obj_del(screen)
handleInput()  → LVGL event callbacks
```

**Extract the action runner from the screen class.** Today progress parsing, log tailing and result scraping live inside `GenericListScreen`. Pull them into a display-agnostic `ActionRunner` that emits `progress(percent, estimated?)`, `result(text)` and `done(exit_code)`. It is the most reusable code in the project, has nothing to do with pixels, and becomes unit-testable.

### 7.4 Target architecture

```
  JSON screen/menu definitions ──┐
                                 ├──► core engine (ActionRunner, module registry,
  action handlers (shell) ───────┘     navigation graph, device logic)
                                        │
                         ┌──────────────┴──────────────┐
                         │                             │
                 panel renderer (LVGL,          web UI (remote, optional,
                 ONE process, never forks)      later — reuses the same
                                                engine over HTTP)
```

Three properties to design for, all of them lessons from §3 and §4:

1. **One process owns the display and input, always.** Screens are modules inside it, not separate executables. micropanel's Qt sibling spawns child apps that each re-open `/dev/fb0` and re-discover input devices — the source of the framebuffer-handover document, the input-grab contention and the `QProcess` self-kill bug. Do not rebuild that model.
2. **Long-running work never blocks the UI thread.** FPGA flashes run to ~350 s (`timeout: 350` in the config); the `ActionRunner` owns them and the UI subscribes.
3. **The JSON model stays renderer-agnostic**, which keeps the renderer choice reversible and makes a remote web UI nearly free later. The existing tools already expose TCP control ports (`--port 8081`, `8082`), so this fits the house style.

### 7.5 UI/UX direction

```
┌────────────────────────────────────────────┐
│ ‹  Firmware ▸ FPGA            ⏻ 42°C  ▮▮▮ │  40px  persistent chrome
├────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐        │
│  │   Detect     │  │   Update     │        │  240px content:
│  │              │  │              │        │  tiles for `menu`,
│  └──────────────┘  └──────────────┘        │  56px rows for long
│  ┌──────────────┐  ┌──────────────┐        │  `GenericList`s
│  │   Backup     │  │   Verify     │        │
│  └──────────────┘  └──────────────┘        │
├────────────────────────────────────────────┤
│                                     40px   │  contextual actions
└────────────────────────────────────────────┘
```

Touch-target minimum 48 px; no hover, no gestures, no multi-touch, no cursor. Resistive means a stylus works and pressure is available.

The **progress screen** is where the extra area pays for itself, because everything already goes to `log_file` and there was nowhere to show it:

- Determinate bar when `parse_progress` is set.
- When only `usb_blaster_duration` is known, show the bar **plus an explicit "estimated" label and elapsed/remaining** — a time-based estimate must not masquerade as real progress.
- A live 3-line tail of `log_file`. On a 350-second flash this is the difference between confidence and "is it stuck?".
- On completion, a result card coloured by exit code, headlined with `result_prefix` + the `result_pattern` match.

### 7.6 Input plan

Feed LVGL a **pointer indev** from `/dev/input/event*`, mapping the raw 0–4095 range to 0–479 / 0–319 (the kernel reports raw values and does not scale). Handle the `BTN_TOUCH`-before-coordinates ordering from §2.4 by opening a contact only once coordinates *and* non-zero pressure have arrived.

Also register a **keypad indev** so the rotate/press model still works if physical buttons are fitted. On a fresh Lite image with only the piscreen line, GPIO 5, 6, 12, 13, 16, 19, 20, 21, 26 and 27 are free; note that 24/25 are now the LCD's DC/RESET, so micropanel's old KEY_UP/KEY_DOWN pins are gone.

### 7.7 Phasing

| Phase | Deliverable | Exit criterion |
|---|---|---|
| 0 | Fresh Lite image + the one config line | Panel renders, touch reports sane coordinates |
| 1 | **Spine prototype**: JSON loader, `menu` + static `GenericList`, `ActionRunner`, progress screen | `config-pios-new.json` loads verbatim; one async action runs with live progress, log tail and result card on the real panel |
| 2 | Measured refresh rate + SPI speed tuning | Documented frame times at the stable default and only visually clean candidate clocks; chosen default |
| 3 | Remaining module types: `textbox`, `action`, dynamic `GenericList` (`items_source`/`items_action`/`list_selection`) | Feature parity with the OLED build for the configs in `screens/` |
| 4 | Built-in modules (`netinfo`, `system`, `cpu_temp`, …) | The `type`-less module ids in the shipped configs all resolve |
| 5 | Optional UI keys (`icon`, `accent`, `layout`), theming, chrome | One config file drives both OLED and panel builds |

Phase 1 is the decision point: it exercises the whole spine — JSON → navigation → async shell action → live progress → result — and makes the feel and the real refresh rate judgeable before committing to the full port.

---

## 8. Open questions and unverified claims

1. **SPI speed above 24 MHz is untested.** §6 assumes linear scaling. Needs measurement before the default changes.
2. **Actual achievable frame rate is not yet measured** — the ~10 fps figure is arithmetic from bus bandwidth, not a benchmark.
3. **Why one touch yields two Qt button activations was never root-caused.** It reproduced under both libinput and evdevtouch with a correct single contact, so it is above the driver. It does not block this PRD (we leave Qt behind) but it would matter if the fallback path in §5.2 is ever taken.
4. **The dual-mode SD card's Himax branch was never exercised on real hardware** — the FPD-Link display was not attached during testing, so only the "panel" branch is proven.
5. **LVGL version, licence posture and build integration** (Buildroot `br-wrapper` package vs. CMake in-tree) are not yet decided.
6. **Persistence.** micropanel has `PersistentStorage` and `ModuleDependency`; whether the panel variant needs them at parity is not yet assessed.
7. **`items_path` appears in the schema (10 uses) but was not studied.**

---

## 9. Appendix — reference commands

```sh
# Panel state
cat /sys/class/graphics/fb0/name /sys/class/graphics/fb0/virtual_size
for d in /sys/class/drm/card*/; do echo "$(basename $d) $(cat $d/status 2>/dev/null)"; done

# Touch device and its capabilities (0x1000003 = X, Y, PRESSURE only)
grep -A4 ADS7846 /proc/bus/input/devices
cat /sys/class/input/eventN/device/capabilities/abs

# Is the pen-down IRQ sane? (a storm here means wrong pendown-gpio polarity)
grep -i ads7846 /proc/interrupts

# GPIO ownership — the first thing to check on any conflict
sudo cat /sys/kernel/debug/gpio

# Why a device never appeared after a runtime overlay
cat /sys/kernel/debug/devices_deferred

# What Qt thinks of a touch device (the NaN smoking gun)
QT_LOGGING_RULES=qt.qpa.input=true <app>
```
