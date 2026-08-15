# MicroPanel Touch pack specification (Tier-2 domain packs)

**Prepared:** 2026-08-15 (fable, with owner direction)
**Status:** format v1 — normative for all new packs; §10 lists the core
enablers that must land before the first pack builds.
**Audience:** primarily AI coding sessions implementing a new pack
(FPGA/MCU flasher, CAN or UART debugger, ESP32 flasher, Domoticz
buttons/status, …). Follow this document exactly; where it is silent,
follow the PRD §6.7 packaging rules and the conventions visible in the
existing Tier-1 handlers.

**Companion documents:** `docs/micropanel-touch-prd.md` §6.7 (Tier-1/
Tier-2 boundary, license gate), `docs/action-execution-contract.md`
(handler result contract), `misc-tools/board-configs/micropanel-touch/
PERSISTENCE.md` (durable-state contract),
`docs/pi-in-system-update-plan.md` (A/B interaction).

---

## 1. What a pack is (and is not)

A **pack** is a separately-versioned repository providing one domain of
functionality (screens + the handlers those screens invoke + optional
background bridge worker + device-access grants), composed into the
appliance **at image build time** by a `misc-tools` hook line.

Non-negotiable properties, inherited from the PRD:

- **Packs never enter the micropanel-touch repo.** License isolation
  (GPL flashers, proprietary bitstreams) and privacy (private repos) are
  the reason Tier-2 exists. Private pack repos follow the misc-tools
  rule: host-side `SOURCES` + `file://${REPOBINS}`, never in-chroot
  clones.
- **There is no runtime plugin loading.** Installing or removing a pack
  is an image rebuild. The RO-root/A-B design depends on this.
- **The dangling-reference ban:** a pack's screens may reference only
  (a) the pack's own handlers and (b) Tier-1 handlers shipped by
  micropanel-touch itself. The image build fails on any unresolved
  reference (§9 closure check).
- **Screens and handlers travel as one unit.** A pack that ships a menu
  without its handlers, or handlers without capability-matrix rows, is
  invalid.

### 1.1 Built-in or pack? The litmus test (owner decision, 2026-08-15)

> **Does the feature manage or observe *the appliance itself*? →
> built-in core screen. Does it use the appliance to work on
> *something else* (external hardware, external services, external
> networks)? → pack.**

Self-management and self-observation are core: networking and Wi-Fi
settings, software update (A/B), factory reset, reboot, system
status/CPU load/temperature/memory, software version, screen lock,
display settings, log collection. These are needed on every image
flavor and are largely legacy-micropanel parity items.

Tiebreakers when the short rule feels ambiguous — **built-in** if the
feature needs a typed broker operation or root, belongs to the
trust/recovery base, is needed by every image flavor, or handles
secrets requiring core redaction discipline; **pack** if it is
deployment- or domain-specific, brings extra dependencies or license
baggage, needs device-specific hardware grants, or could be removed
without weakening the appliance's core promise.

Worked examples: Wi-Fi *join* is built-in (broker op + secret handling
+ parity) even though scanning feels similar to diagnostics; iperf/
network diagnostics is a pack despite being "networking" (it tests an
*external* network, is deployment-specific, and adds a dependency);
media playback is a pack despite touching the system's display/audio
(it consumes external content); A/B update is built-in on every
tiebreaker (trust base, root, universal, self-hosting).

## 2. Pack identity and naming

- Pack id: kebab-case, globally unique, stable forever —
  e.g. `fpga-flash`, `can-debug`, `esp32-flash`, `domoticz-panel`.
- Handler names: `<pack-id>-<action>` (e.g. `fpga-flash-program`,
  `can-debug-capture`). No collisions with Tier-1 names
  (`micropanel-touch-*` is reserved for Tier-1).
- Menu module ids: `pack.<pack-id>.<screen>` (e.g.
  `pack.can-debug.live_view`). The `pack.` prefix is reserved; core
  modules never use it.
- Dedicated system account (when a bridge worker exists):
  `mp-pack-<pack-id>` via a shipped `sysusers.d` fragment.

## 3. Canonical repository layout

```
<pack-repo>/
  pack.conf                    # §4 manifest (required)
  menu-fragment.json           # §5 (required)
  handlers/                    # §6 (required if fragment invokes actions)
    <pack-id>-<action>...
  capability-rows.csv          # §8 (required)
  deps.txt                     # apt runtime deps, one per line (optional)
  udev/                        # *.rules device grants (optional, §7)
  systemd/                     # bridge-worker units (optional, §7.3)
  sysusers.d/                  # account for the bridge worker (with systemd/)
  pack-hook.sh                 # §9 image hook (required)
  tests/
    test_pack_policy.sh        # §11 (required)
    ...
  README.md                    # what it does, licenses of everything shipped
```

## 4. `pack.conf` manifest (required)

Strict `key=value` grammar (identical to the app's `SettingsFile` rules:
no unknown keys, no duplicates). Keys:

```
format=1
pack=<pack-id>
version=<pack version string>
core_min=<oldest micropanel-touch version the fragment schema needs>
summary=<one line>
license=<SPDX id or "proprietary"; the §6.7 license gate applies>
grants=<comma list: none | udev | broker-op | bridge>
listens=none                   # none | diagnostic-session; see §7.4
```

`listens=none` is the norm. `listens=diagnostic-session` declares the
narrowly-permitted exception of §7.4 (a user-initiated, session-scoped
diagnostic listener such as an iperf server); any other listening
behavior is prohibited.

`grants` must name every privilege mechanism the pack uses; the image
build cross-checks it against what the pack actually ships (§9). A pack
requesting `broker-op` cannot be built until the corresponding typed
operation exists in core (§7.2) — that is a reviewed core change, never
a pack-side addition.

## 5. Menu fragment (`menu-fragment.json`)

One JSON file declaring the pack's home-screen entry and its screens.
The core config (`config-basic.json` / `config-pios-new.json`) is
**never edited by a pack**; the loader discovers fragments at startup
from the packs directory (§10 enabler) and merges them.

```json
{
  "format": 1,
  "pack": "domoticz-panel",
  "home": {
    "id": "pack.domoticz-panel.root",
    "title": "Domoticz",
    "icon": "network",
    "accent": "#397c63",
    "order": 50
  },
  "modules": [
    {
      "id": "pack.domoticz-panel.root",
      "title": "Domoticz",
      "submenus": [
        {"id": "pack.domoticz-panel.lights", "title": "Lights"},
        {"id": "back", "title": "Back"}
      ]
    },
    {
      "id": "pack.domoticz-panel.lights",
      "title": "Lights",
      "items": [
        {
          "type": "action",
          "title": "Desk lamp ON",
          "handler": "domoticz-panel/domoticz-panel-switch",
          "args": ["desk-lamp", "on"],
          "confirm": false
        },
        {
          "type": "status",
          "title": "Desk lamp",
          "state_key": "desk-lamp",
          "refresh_sec": 10,
          "handler": "domoticz-panel/domoticz-panel-read-state",
          "args": ["desk-lamp"]
        }
      ]
    }
  ]
}
```

Rules:

- Field vocabulary follows the core `StarterConfig` schema (module
  `id`/`title`/`icon`/`accent`/`submenus`, presentation options as the
  core grows them). A fragment using unknown fields fails validation —
  additive schema changes are core work, versioned by `format`.
- `handler` references: `"<pack-id>/<handler-name>"` resolves to this
  pack's `handlers/` directory; a bare name resolves to Tier-1. A
  fragment may only use its **own** pack prefix (closure check §9).
- `order` positions the home tile deterministically (ascending; ties
  broken by pack id). Core tiles occupy orders < 50 by convention.
- Destructive actions (flashing, erasing) must set `"confirm": true` —
  the engine renders the standard two-step confirmation.
- `status` items are the **Level-1 inbound path** (§7.4): the named
  read-state handler runs every `refresh_sec` through `CommandService`
  and the item renders its bounded stdout. `state_key` additionally
  subscribes the item to Level-2 push events when a bridge exists.

## 6. Handlers

Handlers are executables invoked by the action engine **as the
appliance account**, with fixed argv from the fragment (`args`), never
through a shell, under the existing execution contract
(`docs/action-execution-contract.md`): bounded runtime with SIGTERM →
grace → SIGKILL cancellation, bounded output, `[SUCCESS]`/`[ERROR]`
terminal markers, incremental progress markers for long jobs (the
flashing progress UI consumes these — the Tier-1 `simulated-flash`
handler is the reference implementation).

Additional pack rules:

- Handlers may use only their own pack's files, Tier-1 tools, and the
  `deps.txt` packages. No network listeners, no daemons — a handler is
  run-to-completion. Long-lived behavior belongs to a bridge worker
  (§7.3).
- Handlers requiring hardware access get it via udev grants (§7.1),
  never via setuid, sudo, or a root helper.
- Secrets (API keys, e.g. Domoticz credentials) live in the pack's
  `/data` directory (§7.5) with `0600`, written by a settings screen or
  provisioning step — never in the fragment JSON, image, or repo.

## 7. Privilege, devices, and external integration

### 7.1 Device access (default path): udev grants

Ship `udev/*.rules` granting the appliance account (or the pack's
bridge account) access to **specifically matched devices** — USB JTAG
probes by VID:PID, a serial bootloader by interface attributes, an SPI
node, `gpiochip` lines via a group. This follows the backlight
precedent: narrow, attribute-matched, created-on-add. Broad rules
(`KERNEL=="ttyUSB*"` without VID:PID match) fail review.

### 7.2 Root operations (exception path): typed broker operations

The broker never accepts an executable, argv, or "run this" request.
If a pack genuinely needs root (e.g. `ip link set can0 up type can
bitrate …` needs `CAP_NET_ADMIN`), the operation must be added to
**core** as a new typed, schema-validated broker operation with a
fixed-argv Tier-1-style handler — a reviewed core change with its own
tests, after which the pack may invoke it. Declare `grants=broker-op`
and document the operation in the pack README. Expect this to be rare;
`can-debug` link configuration is the known first candidate.

### 7.3 Bridge workers (long-lived integration processes)

A pack needing a persistent connection to an external system (MQTT
subscription, periodic REST polling beyond per-item `refresh_sec`,
serial monitor) or a long-lived local job that outlives the action
contract's bounded runtime (e.g. a media playback pipeline) ships a
**bridge worker**: an unprivileged daemon under its own `mp-pack-<id>`
account, installed and enabled by the pack hook as a systemd unit
(`After=network-online.target` as appropriate, `Restart=on-failure`,
and the standard hardening set: `NoNewPrivileges`, `PrivateTmp`,
`ProtectSystem=strict` with only its own `/data` path writable).

A bridge worker may expose one **pack-private control socket** (an
`AF_UNIX` socket under `/run/micropanel-touch-packs/<pack-id>/`, owned
by the pack account, group-limited so the appliance account can write).
The pack's own handlers then act as short-lived clients of that socket
("play <url>", "stop") — keeping the UI-side action within the bounded
handler contract while the daemon does the long-running work. The
control protocol stays entirely inside the pack's trust domain (both
ends unprivileged), must be strictly parsed like every socket protocol
in this system, and is never reachable from the network.

### 7.4 Inbound notifications from external systems (the Domoticz case)

**Hard rule: packs never open listening network sockets.** All external
integration is **client-originated from the appliance outward**
(HTTP(S) polling, MQTT subscribe over an outbound connection, SSE).
This keeps the appliance's network attack surface at zero listeners and
avoids inventing auth/TLS termination on a 320×480 panel. A
webhook-listener pack is a future, explicitly-reviewed exception — not
available under format v1.

One precise carve-out: **client-negotiated media-transport receive
ports** (RTP/RTCP UDP ports opened as part of an *appliance-initiated*
RTSP/SDP session, e.g. a gstreamer stream client) are permitted — they
are solicited, session-scoped, and closed with the session. What stays
banned is any unsolicited service port: HTTP servers, webhooks, fixed
RTP listeners waiting for unannounced senders, discovery responders.

A second carve-out for diagnostic tooling (owner decision, 2026-08-15):
a pack declaring `listens=diagnostic-session` (§4) may run a
**user-initiated, session-scoped diagnostic listener** — the iperf
server of a network-test pack is the motivating case. Conditions, all
mandatory and pinned by the pack's policy test: the listener starts
only from an explicit user action on the pack's screen; it stops
automatically when that screen is left *and* on a hard timeout; the
screen visibly indicates the open port while it listens; the port is
unprivileged and fixed (declared in the pack README and capability
rows). The appliance's steady-state posture remains **zero idle
listeners**.

Two delivery levels into the UI:

- **Level 1 — polling (available first):** a `status` item's read-state
  handler runs every `refresh_sec` (§5). For Domoticz this is a
  `curl`-based handler querying the REST API. Simple, stateless, good
  to ~5–10 s freshness. This is the v1 answer for "button state
  changed in Domoticz — show it on the panel."
- **Level 2 — push via the pack event socket (core enabler, §10):** the
  HMI owns one local `AF_UNIX` datagram socket
  (`/run/micropanel-touch-ui/pack-events.sock`, group
  `micropanel-touch-events`, `0660`). A bridge worker (e.g. an MQTT
  client subscribed to Domoticz's topics) writes one JSON object per
  datagram:

  ```json
  {"pack": "domoticz-panel", "key": "desk-lamp", "value": "on", "ts": 1765900000}
  ```

  Constraints, enforced by the core reader: ≤ 4 KiB per datagram,
  strict schema (exactly these fields, string `value`), unknown pack
  ids dropped, per-pack rate limit with drop-and-log on flood. Events
  are **state, never commands**: they update `state_key`-bound widgets
  through the existing coalescing UI queue (`push_latest` keyed by
  pack+key) and can trigger nothing else. Outbound actions (panel
  button → Domoticz) remain ordinary handlers; the socket is one-way
  into the UI.

### 7.5 Persistence

Pack durable state lives in `/data/micropanel-touch-packs/<pack-id>/`
(created by the image finalizer/pack hook; owner = the account that
writes it; `0750` or stricter). Config files use the `SettingsFile`
grammar; writes use the durable-replace protocol. Additive-keys-only
across versions (A/B rollback reads newer files — unknown keys must
degrade to defaults, per the update plan). Volatile scratch goes under
`/run`. Add every path to the pack's capability rows; PERSISTENCE.md
gains one line per pack at image-integration time.

## 8. Capability matrix rows (`capability-rows.csv`)

Same columns as the board's `capability-matrix.csv`:

```
capability,status,owner,privileged_path,devices,writable_paths
fpga-flash-program,supported,fpga-flash,none,usb:0403:6014 via udev,/data/micropanel-touch-packs/fpga-flash
```

One row per user-reachable action. `status` uses the established
vocabulary (`supported`/`pending`/`unavailable-hardware`/…). The image
build appends these rows to the merged matrix; a fragment action
without a row fails the build.

## 9. Image integration (`pack-hook.sh` and misc-tools)

- Enabling a pack = one hook line in the board's `hooks.txt` (or a
  variant hook list), pointing at the pack repo and pinned revision —
  the existing Tier-2 slots. Pin bumps trigger apps-stage rebuilds via
  the stamp system automatically.
- `pack-hook.sh` runs in the chroot after the core hook and must be
  idempotent (re-run-safe, like `luckfox-ctp-hook.sh`). It installs to
  `/opt/micropanel-touch-packs/<pack-id>/` (fragment at its root,
  handlers under `handlers/`), plus udev rules, sysusers, systemd units
  (enable via absolute path), and appends its rows to the merged
  capability matrix and its identity to `image-manifest.env`
  (`PACK_<ID>_VERSION=…`).
- **Build-time closure check** (misc-tools, §10 enabler): after all
  hooks run, validate every fragment — schema version, handler
  references resolve within the allowed set, matrix rows exist for
  every action, `pack.conf` grants match shipped artifacts (udev files
  present iff `udev` granted; units present iff `bridge`; no unit
  `ListenStream`/port binds ever), and fragment ids are collision-free.
  Any failure fails the image build.

## 10. Core enablers — status at spec time

Packs cannot build until the following core work lands. AI sessions:
check this table first; implement missing enablers in core/misc-tools
**before** starting a pack that needs them.

| Enabler | Where | Status 2026-08-15 |
|---|---|---|
| Fragment discovery + merge + validation in the config loader | micropanel-touch `StarterConfig` | **Not implemented** (loads a single file today) |
| `action`/`status` item types with `refresh_sec` through `CommandService` | micropanel-touch (Sprint 4 item 1) | **Not implemented** in the starter UI |
| Pack handler resolution (`<pack-id>/<name>`) in the execution context | micropanel-touch | **Not implemented** |
| Build-time closure check | misc-tools | **Not implemented** |
| Pack event socket (Level 2) + `state_key` binding | micropanel-touch | **Not implemented** (Level 1 polling needs only Sprint 4 machinery) |
| `/data/micropanel-touch-packs/` skeleton in finalizer + factory reset | misc-tools | **Not implemented** |
| Confirmation dialog for `"confirm": true` | micropanel-touch (Sprint 4 item 5) | **Not implemented** |
| Tier-1 udev-grant, broker-op, bridge-unit patterns | both | **Exist** (backlight rule, DHCP-server op, SSH/machine-id services are the reference implementations) |
| Handler execution contract + progress UI | micropanel-touch | **Exists** (`action-execution-contract.md`, ActionRunner) |

Recommended landing order: fragment loader → item types/`refresh_sec` →
closure check → first real pack (`fpga-flash`, per Sprint 5's rehearsal
item) → pack event socket when the first push-integration pack
(Domoticz) is scheduled.

## 11. Required pack tests

- `tests/test_pack_policy.sh` — grep-pinned invariants (the established
  handler-policy pattern): no `sudo`/`setuid`, no `nc -l`/listen calls,
  handlers use fixed argv, udev rules carry specific matches, marker
  strings present.
- Handler contract tests via the core `test_handler_contract.sh`
  pattern (argument-validation error paths at minimum).
- Fragment validates against the schema (once the closure checker
  exists, run it in the pack's CI too).
- Bridge workers: unit-test the event formatting and the reconnect
  path; never require live external services in tests.

## 12. AI-session checklist: creating a new pack

1. Read this spec top to bottom; check §10 — if a needed enabler is
   missing, implement it in core first (separate commits/pins).
2. Create the pack repo with the §3 layout; fill `pack.conf` (§4);
   choose ids per §2.
3. Write handlers against `action-execution-contract.md`; model long
   jobs on the Tier-1 `simulated-flash` handler (progress markers,
   cancellation).
4. Declare privileges honestly: udev rules for devices; if root is
   unavoidable, stop and add the typed broker operation in core per
   §7.2 before continuing.
5. Write `menu-fragment.json` (§5); every action gets a matrix row
   (§8); destructive actions get `"confirm": true`.
6. External integration: Level 1 polling handler first; add a bridge
   worker + Level 2 events only when freshness demands it (§7.4). No
   listeners, ever.
7. Write `pack-hook.sh` (idempotent, §9) and the §11 tests.
8. misc-tools: add the hook line + pinned revision to the target
   board/variant; build with `--dry-run`, then build the image; verify
   the closure check passes and the merged capability matrix carries
   the pack's rows.
9. Bench acceptance: menu appears at the declared order; each action
   runs and reports through the result card; destructive confirm
   works; matrix/manifest entries verified on the flashed image; add
   the pack's acceptance steps to the board BUILD.md.
10. Record licenses in the pack README (the §6.7 license gate applies
    to every shipped binary/bitstream — the Luckfox firmware precedent
    shows the required provenance format).

## 13. Explicitly out of scope for format v1

- Runtime pack install/removal (image rebuild only, by design).
- Pack-to-pack dependencies or shared pack libraries.
- Listening network services of any kind (§7.4, with its RTP carve-out).
- Per-pack update channels (packs update with the image via A/B).
- UI plugins/custom widgets beyond the declared fragment vocabulary —
  new widget types are core work, versioned via `format`.
- **Video rendering to the panel.** Audio-only media packs (internet
  radio, URL/RTP audio via a gstreamer/ffmpeg bridge worker, §7.3) are
  fully supported under v1. Video needs a format-v2 core enabler whose
  shape depends on the display class:
  - **SPI panels:** an exclusive **display-handover protocol** (HMI
    pauses LVGL rendering — the display-sleep pause machinery is the
    foundation — player writes the framebuffer, HMI resumes with a full
    invalidate on stop while retaining touch). Link budget caps this at
    roughly 10–15 fps at 320×480 over 48 MHz SPI — camera-preview
    grade, not smooth playback.
  - **DSI/KMS panels (the intended media class):** a **DRM plane
    lease** — the HMI runs LVGL's DRM backend as display master on the
    primary plane and leases a hardware overlay plane to the pack's
    player (lease fd passed over the pack control socket). Zero-copy
    decoded frames (`v4l2h264dec → kmssink` on Pi 4/CM4; note Pi 5 has
    **no** H.264 hardware decoder — HEVC via `rpivid` or software
    H.264 there) render under a still-live, composited LVGL UI, so
    transport controls stay on screen and the player never touches UI
    memory. This supersedes handover wherever KMS planes exist.
  Variant capability note for audio: the `luckfox-ctp` image disables
  analogue audio (PWM-backlight trade-off) and blacklists
  `snd_bcm2835`, so audio packs there require USB or I²S audio and must
  say so in their capability rows; the PiScreen image retains the
  analogue jack.
