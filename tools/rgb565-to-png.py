#!/usr/bin/env python3
"""Convert a compact little-endian RGB565 frame to a dependency-free PNG."""

import argparse
import struct
import zlib
from pathlib import Path


def png_chunk(kind: bytes, contents: bytes) -> bytes:
    return (struct.pack(">I", len(contents)) + kind + contents +
            struct.pack(">I", zlib.crc32(kind + contents) & 0xFFFFFFFF))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="compact RGB565 little-endian input")
    parser.add_argument("output", type=Path, help="PNG output")
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    arguments = parser.parse_args()
    if arguments.width < 1 or arguments.height < 1:
        raise SystemExit("width and height must be positive")

    source = arguments.input.read_bytes()
    expected = arguments.width * arguments.height * 2
    if len(source) != expected:
        raise SystemExit(f"expected {expected} RGB565 bytes, got {len(source)}")

    rows = bytearray()
    offset = 0
    for _ in range(arguments.height):
        rows.append(0)  # PNG filter: None
        for _ in range(arguments.width):
            pixel = source[offset] | (source[offset + 1] << 8)
            offset += 2
            rows.extend((((pixel >> 11) & 0x1F) * 255 // 31,
                         ((pixel >> 5) & 0x3F) * 255 // 63,
                         (pixel & 0x1F) * 255 // 31))

    png = (b"\x89PNG\r\n\x1a\n" +
           png_chunk(b"IHDR", struct.pack(">IIBBBBB", arguments.width, arguments.height, 8, 2, 0, 0, 0)) +
           png_chunk(b"IDAT", zlib.compress(rows, level=9)) +
           png_chunk(b"IEND", b""))
    arguments.output.write_bytes(png)


if __name__ == "__main__":
    main()
