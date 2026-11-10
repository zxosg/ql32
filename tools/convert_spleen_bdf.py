#!/usr/bin/env python3
"""Convert printable-ASCII Spleen BDF strikes to indexed QFNT v2 assets."""

import argparse
import struct
from pathlib import Path


FONT_SIZES = ((5, 8), (6, 12), (8, 16), (12, 24))
FIRST_CHARACTER = 32
GLYPH_COUNT = 95
HEADER = struct.Struct("<4sBBBBBBBBHIII")
GLYPH = struct.Struct("<BBBBbbBIH")
MAXIMUM_FILE_SIZE = 8192


def parse_bdf(path: Path, expected_width: int, expected_height: int) -> bytes:
    lines = path.read_text(encoding="ascii").splitlines()
    ascent = None
    descent = None
    glyphs = {}
    index = 0
    while index < len(lines):
        parts = lines[index].split()
        if len(parts) == 2 and parts[0] == "FONT_ASCENT":
            ascent = int(parts[1])
        elif len(parts) == 2 and parts[0] == "FONT_DESCENT":
            descent = int(parts[1])
        if not parts or parts[0] != "STARTCHAR":
            index += 1
            continue

        encoding = None
        advance = None
        box = None
        bitmap = []
        index += 1
        while index < len(lines) and lines[index] != "ENDCHAR":
            parts = lines[index].split()
            if len(parts) >= 2 and parts[0] == "ENCODING":
                encoding = int(parts[1])
            elif len(parts) >= 3 and parts[0] == "DWIDTH":
                advance = int(parts[1])
            elif len(parts) == 5 and parts[0] == "BBX":
                box = tuple(map(int, parts[1:]))
            elif parts and parts[0] == "BITMAP":
                index += 1
                while index < len(lines) and lines[index] != "ENDCHAR":
                    bitmap.append(lines[index])
                    index += 1
                break
            index += 1
        if encoding is not None and FIRST_CHARACTER <= encoding < FIRST_CHARACTER + GLYPH_COUNT:
            if box is None or advance is None or ascent is None:
                raise ValueError(f"{path}: missing BDF metrics for character {encoding}")
            glyphs[encoding] = (box, advance, bitmap)
        index += 1

    if ascent is None or descent is None:
        raise ValueError(f"{path}: missing FONT_ASCENT or FONT_DESCENT")
    if ascent + descent != expected_height:
        raise ValueError(f"{path}: ascent plus descent does not match native height")
    missing = sorted(set(range(FIRST_CHARACTER, FIRST_CHARACTER + GLYPH_COUNT)) -
                     glyphs.keys())
    if missing:
        raise ValueError(f"{path}: missing ASCII glyphs {missing}")

    encoded_glyphs = []
    max_advance = 0
    for character in range(FIRST_CHARACTER, FIRST_CHARACTER + GLYPH_COUNT):
        (width, height, x_offset, y_offset), advance, bitmap = glyphs[character]
        if (width < 0 or height < 0 or width > 16 or height > 24 or
                not -128 <= x_offset <= 127 or not -128 <= y_offset <= 127 or
                not 0 <= advance <= 255 or len(bitmap) != height):
            raise ValueError(f"{path}: invalid bounding box for character {character}")
        row_bytes = (width + 7) // 8
        packed = bytearray(row_bytes * height)
        for glyph_y, line in enumerate(bitmap):
            source = bytes.fromhex(line)
            if len(source) * 8 < width:
                raise ValueError(f"{path}: short bitmap row for character {character}")
            for glyph_x in range(width):
                if source[glyph_x // 8] & (0x80 >> (glyph_x % 8)):
                    packed[glyph_y * row_bytes + glyph_x // 8] |= 0x80 >> (glyph_x % 8)
        encoded_glyphs.append((character, width, height, advance, x_offset,
                               y_offset, bytes(packed)))
        max_advance = max(max_advance, advance)

    if max_advance != expected_width:
        raise ValueError(f"{path}: maximum advance does not match native width")

    header_size = HEADER.size
    index_offset = header_size
    bitmap_offset = index_offset + GLYPH_COUNT * GLYPH.size
    bitmap_data = bytearray()
    directory = bytearray()
    for character, width, height, advance, x_offset, y_offset, bitmap in encoded_glyphs:
        offset = bitmap_offset + len(bitmap_data)
        directory.extend(GLYPH.pack(character, width, height, advance, x_offset,
                                    y_offset, 0, offset, len(bitmap)))
        bitmap_data.extend(bitmap)

    total_size = bitmap_offset + len(bitmap_data)
    if total_size > MAXIMUM_FILE_SIZE:
        raise ValueError(f"{path}: generated QFNT exceeds {MAXIMUM_FILE_SIZE} bytes")
    header = HEADER.pack(b"QFNT", 2, header_size, expected_width,
                         expected_height, ascent, FIRST_CHARACTER,
                         GLYPH_COUNT, 0, 0, index_offset,
                         bitmap_offset, total_size)
    return header + directory + bitmap_data


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("bdf_dir", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("header", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    args.header.parent.mkdir(parents=True, exist_ok=True)

    generated = []
    for width, height in FONT_SIZES:
        name = f"spleen-{width}x{height}"
        encoded = parse_bdf(args.bdf_dir / f"{name}.bdf", width, height)
        (args.output_dir / f"{name}.qfnt").write_bytes(encoded)
        declaration = [f"inline constexpr std::uint8_t {name.replace('-', '_')}[] = {{"]
        for start in range(0, len(encoded), 12):
            row = encoded[start:start + 12]
            declaration.append("    " + ", ".join(f"0x{byte:02x}" for byte in row) + ",")
        declaration.append("};")
        generated.append("\n".join(declaration))

    args.header.write_text(
        "#pragma once\n\n#include <cstdint>\n\n"
        "namespace ql32::graphics::font_data {\n\n" +
        "\n\n".join(generated) +
        "\n\n} // namespace ql32::graphics::font_data\n",
        encoding="ascii",
    )


if __name__ == "__main__":
    main()