from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.convert_spleen_bdf import FONT_SIZES, GLYPH, HEADER, parse_bdf


def main() -> None:
    source_dir = ROOT / "tools" / "spleen-source"
    for width, height in FONT_SIZES:
        name = f"spleen-{width}x{height}"
        encoded = parse_bdf(source_dir / f"{name}.bdf", width, height)
        (magic, version, header_size, max_advance, line_height, ascent,
         first, count, flags, reserved, index_offset, bitmap_offset,
         total_size) = HEADER.unpack_from(encoded)
        assert magic == b"QFNT" and version == 2
        assert header_size == HEADER.size and reserved == 0 and flags == 0
        assert (max_advance, line_height) == (width, height)
        assert 0 < ascent <= line_height and first == 32 and count == 95
        assert index_offset == HEADER.size
        assert bitmap_offset == index_offset + count * GLYPH.size
        assert total_size == len(encoded) <= 8192

        previous_end = bitmap_offset
        advances = set()
        saw_bearing = False
        for glyph_index in range(count):
            record = GLYPH.unpack_from(encoded, index_offset + glyph_index * GLYPH.size)
            character, glyph_width, glyph_height, advance, x_offset, y_offset, flags, offset, length = record
            assert character == first + glyph_index
            assert glyph_width <= 16 and glyph_height <= 24
            assert advance > 0 and advance <= width and flags == 0
            saw_bearing |= x_offset != 0 or y_offset != 0
            assert length == ((glyph_width + 7) // 8) * glyph_height
            assert previous_end <= offset <= offset + length <= total_size
            previous_end = offset + length
            advances.add(advance)
        assert previous_end == total_size
        assert advances == {width}
        assert saw_bearing


if __name__ == "__main__":
    main()