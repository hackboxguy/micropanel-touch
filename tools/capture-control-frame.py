#!/usr/bin/env python3
"""Save a binary capture_frame response from a local micropanel-touch socket."""

import argparse
import json
import socket
from pathlib import Path


def receive_header_and_payload(connection: socket.socket) -> tuple[dict, bytearray]:
    received = bytearray()
    while b"\n" not in received:
        chunk = connection.recv(4096)
        if not chunk:
            raise RuntimeError("control socket closed before its frame header")
        received.extend(chunk)
    header, payload = received.split(b"\n", 1)
    response = json.loads(header)
    if not response.get("ok"):
        raise RuntimeError(response.get("error", "frame capture failed"))
    capture = response.get("capture")
    if not isinstance(capture, dict) or capture.get("format") != "rgb565le":
        raise RuntimeError("control response is not an RGB565 frame capture")
    byte_count = capture.get("byte_count")
    if not isinstance(byte_count, int) or byte_count < 1 or byte_count > 4 * 1024 * 1024:
        raise RuntimeError("control response has an invalid frame byte count")
    while len(payload) < byte_count:
        chunk = connection.recv(min(65536, byte_count - len(payload)))
        if not chunk:
            raise RuntimeError("control socket closed before the full frame payload")
        payload.extend(chunk)
    if len(payload) != byte_count:
        raise RuntimeError("control socket sent bytes after the advertised frame payload")
    return response, payload


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("socket", type=Path, help="absolute path passed to --control-socket")
    parser.add_argument("output", type=Path, help="output .rgb565 path")
    arguments = parser.parse_args()

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.connect(str(arguments.socket))
        connection.sendall(b'{"id":"host-frame","command":"capture_frame"}\n')
        response, payload = receive_header_and_payload(connection)

    arguments.output.write_bytes(payload)
    print(json.dumps({"screen": response["screen"], "capture": response["capture"],
                      "output": str(arguments.output)}))


if __name__ == "__main__":
    main()
