# Development control interface

The application exposes no control endpoint unless explicitly started with
`--control-socket /absolute/path`. The endpoint is a local `AF_UNIX` socket
created with mode `0600`; it is for bench verification only and must never be
enabled in a release image.

Each client connection sends one newline-delimited JSON request and then reads
one response before the server closes the connection. Requests are parsed off
the UI thread, but every navigation/capture command is delivered through
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
For a `GenericList`, `activate` uses an unambiguous `list:N` target.

## Widget tree

`{"id":"tree","command":"capture_tree"}` returns a flat preorder under
`widgets`. Each node records `id`, `parent_id`, recognized `type`, screen-space
`x`/`y`/`width`/`height`, and label `text`; `widget_tree_truncated` signals the
256-node safety cap. Every textarea is represented as `"<redacted>"`, and its
internal text label is not traversed, so a future password field cannot leak
through this interface.

## RGB565 framebuffer capture

`{"id":"frame","command":"capture_frame"}` replies with a JSON header,
followed immediately by a raw binary payload before the connection closes:

```text
{"ok":true,"capture":{"format":"rgb565le","width":320,"height":480,"stride_bytes":640,"byte_count":307200}}
<exactly byte_count raw RGB565 little-endian bytes>
```

The reader accepts only the active framebuffer's RGB565 layout, validates its
virtual offsets and memory bounds, and normalizes driver stride to contiguous
rows. Capture clients must use `byte_count`, not EOF, as the payload boundary.

For bench use on the machine that can reach the socket:

```sh
python3 tools/capture-control-frame.py /tmp/micropanel-touch-control.sock root.rgb565
python3 tools/rgb565-to-png.py root.rgb565 root.png --width 320 --height 480
```
