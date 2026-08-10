# Development control interface

The application exposes no control endpoint unless explicitly started with
`--control-socket /absolute/path`. The endpoint is a local `AF_UNIX` socket
created with mode `0600`; it is for bench verification only and must never be
enabled in a release image.

Each client connection sends one newline-delimited JSON request and then reads
one response before the server closes the connection. Requests are parsed off
the UI thread, but every control/capture command is delivered through
`UiEventQueue` and completed by the LVGL thread after a forced layout and
refresh barrier.

## Navigation and state

```json
{"id": 1, "command": "state"}
{"id": 2, "command": "navigate", "target": "network_config"}
{"id": 3, "command": "activate", "target": "netsettings"}
{"id": 4, "command": "back"}
```

Successful replies contain `ok`, `screen`, `menu_path`, and `settled: true`.
`navigate` and `activate` currently address the legacy-config renderer; for a
`GenericList`, `activate` uses an unambiguous `list:N` target. `state`, `back`,
capture, and synthetic tap are also available in the starter UI.

## Synthetic tap

```json
{"id":"network","command":"tap","x":160,"y":76}
```

`tap` injects one press/release pair at integer screen-space coordinates into
a dedicated development-only LVGL pointer device. It therefore exercises the
same LVGL hit testing and widget callbacks as the physical touch device; it
does not shortcut navigation or invoke an action directly. Coordinates outside
the active display are rejected. Its response waits for a queued click's
deferred screen action, layout, and refresh to settle, so `screen` describes
the post-tap UI.

The synthetic pointer exists only when the already opt-in control socket is
enabled. It neither opens nor writes a kernel input node, and is unavailable
in a normal app start or a release image.

## Constrained text input

```json
{"id":"ip","command":"text","field":"ip_address","text":"10.0.0.2"}
```

`text` is intentionally limited to the starter UI's mock IP Settings fields:
`ip_address`, `prefix_length`, and `gateway`. The named field must already be
the visibly focused field (normally selected with a preceding `tap`). The
endpoint accepts 1–63 printable ASCII bytes, then applies each character
through a dedicated LVGL keypad device and group — never by assigning a
textarea's value directly. IP and gateway permit digits/dots; prefix length
permits digits only.

The command is rejected on the Wi-Fi Password screen and for every other
field. The application neither logs nor echoes submitted text, and widget-tree
captures redact every textarea regardless of whether it contains public IP
data or a password.

## Widget tree

`{"id":"tree","command":"capture_tree"}` returns a flat preorder under
`widgets`. Each node records `id`, `parent_id`, recognized `type`, screen-space
`x`/`y`/`width`/`height`, and label `text`; `widget_tree_truncated` signals the
256-node safety cap. Every textarea is represented as `"<redacted>"`, and its
internal text label is not traversed, so neither the starter IP fields nor the
password field can leak through this interface.

## RGB565 framebuffer capture

`{"id":"frame","command":"capture_frame"}` replies with a JSON header,
followed immediately by a raw binary payload before the connection closes:

```text
{"ok":true,"capture":{"format":"rgb565le","width":320,"height":480,"stride_bytes":640,"byte_count":307200}}
<exactly byte_count raw RGB565 little-endian bytes>
```

The LVGL UI thread reads the framebuffer immediately after its own settle
barrier and returns an immutable payload to the socket worker, so a later UI
repaint cannot change a response in flight. The reader accepts only the active
framebuffer's RGB565 layout, validates its virtual offsets and memory bounds,
and normalizes driver stride to contiguous rows. `settled` guarantees LVGL has
laid out and refreshed framebuffer memory; it does not wait for an asynchronous
DRM/fbdev SPI flush or physical panel photons. Capture clients must use
`byte_count`, not EOF, as the payload boundary.

The same UI-thread capture seam is covered by the `headless-ui` CTest without
DRM or a framebuffer device. It renders the real starter UI into an RGB565
memory display, so semantic-tree, pixel-geometry, synthetic-touch, and
password-redaction regressions are host-CI checks; the Pi suite remains the
real-panel validation.

For bench use on the machine that can reach the socket:

```sh
python3 tools/capture-control-frame.py /tmp/micropanel-touch-control.sock root.rgb565
python3 tools/rgb565-to-png.py root.rgb565 root.png --width 320 --height 480
```
