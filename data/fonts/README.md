# Spleen Fonts

These QFNT version 2 assets contain printable ASCII from Spleen at 5x8,
6x12, 8x16, and 12x24. The upstream BDF fonts are from
[fcambus/spleen](https://github.com/fcambus/spleen/tree/5116f8eac7f65da2cfa169626ec43b5d041665fe)
and are preserved in `tools/spleen-source/`. They are distributed under
BSD-2-Clause; see [Spleen-LICENSE.txt](Spleen-LICENSE.txt).

The compact binary format and its limits are specified in
[QL32-API-001](../../docs/specification/QL32-API-001.md). To regenerate the
assets from the preserved upstream BDF files:

```sh
python3 tools/convert_spleen_bdf.py tools/spleen-source data/fonts include/ql32/graphics/spleen_font_data.hpp
```

The generated header embeds built-in faces in the portable firmware. The
`.qfnt` files can be loaded once and shared across channels by handle:

```basic
titleFont = LOADFONT("FFS:/fonts/spleen-12x24.qfnt")
FONT #1,titleFont,0
GFONT #1,titleFont,0
```