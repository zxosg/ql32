# Indexed image format study

**Date:** 2026-11-15  
**Status:** QIMG v2 implemented and physically accepted on Olimex; desktop GUI runtime verification pending

## Recommendation

Use QIMG v2 with an embedded 16-color RGB palette. QIMG v1 compatibility is
not required because QL/32 has not been released. The Python remote debugger
now converts common image inputs, previews/resizes the indexed result, exports
QIMG, and uploads it through YMODEM. TGA remains a possible converter input;
its full file format is not a firmware decoding target.

QIMG v2 should keep the existing compact, uncompressed, packed 4-bit pixel
layout and make the palette part of the versioned header:

- Preserve the existing 16-byte QIMG header fields.
- Add 16 RGB triplets (48 bytes), giving v2 a fixed 64-byte header.
- Store palette components as 8-bit R, G, B values in index order 0 through 15.
- Store pixels high-nibble first, with rows top-to-bottom and row stride
  `ceil(width / 2)`, as in v1.
- Validate version, dimensions, palette, payload length, and the existing
  decoded-pixel limit before allocating or reading image data.
- v1 files are rejected; the v2 loader always uses its embedded RGB palette.

The debugger converter uses Pillow to open supported raster formats, resize,
quantize to 16 colors, and emit this fixed QIMG v2 layout. Pillow-supported TGA,
PNG, BMP, GIF, PCX, and other inputs can be used; the on-device loader only
needs QIMG. Alpha is composited onto black because QIMG v2 stores RGB palette
colors without alpha. BASIC `BLIT` can still skip a chosen source palette
index as transparent.

## TGA assessment

TGA is a plausible compact source format, and its uncompressed color-mapped
image type (type 1) is far simpler than PNG or GIF decoding. However, a TGA
reader still has to interpret an 18-byte header, optional image ID, color-map
first index/length/entry depth, image index depth, dimensions, and horizontal
and vertical origin flags. TGA 2.0 adds optional extension, developer, and
footer data. Its common indexed images use 8-bit indices and 24- or 32-bit
palette entries, so they are typically larger than QIMG's packed 4-bit data.
Type 9 adds RLE and is outside the simplest subset.

If direct TGA loading is ever needed, constrain it explicitly to type 1,
indexed images with a bounded palette, supported entry/index depths, no
interleaving, and well-defined origin handling. That subset is still more
decoder work than QIMG v2 and duplicates the conversion step's job. Prefer
TGA as a host-side converter input.

## Palette and display semantics

An embedded palette defines each file's color identity, but it does not
guarantee exact colors on hardware with fewer available display colors. QL/32
currently exposes a shared display palette. The v2 loader retains source RGB
values; `BLIT` maps those colors to the active display palette using
deterministic nearest-color matching, without recoloring other windows. Exact
output is possible only when the active display can represent the source
colors. The language reference documents this mapping.

## Comparison

| Format | Palette handling | Firmware decoder work | Assessment |
|---|---|---|---|
| QIMG v2 | Fixed 16-entry RGB palette in versioned header | Lowest; fixed layout, no compression, existing packed pixels | **Selected and implemented**; no v1 compatibility |
| TGA type 1 | Color-map table with configurable first index, entry depth, and image index depth | Moderate for a strict subset; header, palette, optional ID, and origin handling | Good converter input; no reason to duplicate this parsing on-device |
| Indexed BMP, 4 bpp, BI_RGB | RGBQUAD table in each file | Low to moderate; headers/table, DWORD row padding, and row direction | Viable standard alternative, not selected for firmware |
| PNG, indexed | Required PLTE; optional tRNS | High; chunks, DEFLATE, scanline filters, CRCs, and color modes | Strong converter input; too much firmware decoder for this use |
| GIF | Global or per-image local color table; transparency extension | High; LZW, sub-block framing, interlace, animation/container behavior | More than a static BASIC bitmap needs |
| ILBM/LBM | CMAP chunk | Moderate; IFF chunks, big-endian sizes/padding, bitplane reconstruction, optional ByteRun1 and masks | Historically relevant, but unlike current chunky indexed rows |
| PCX | Palette in header or trailing VGA palette | Moderate; RLE, plane/bit-depth conventions, padded scanlines, palette variants | Possible converter input; decoder has historical variants |

## References

- [Truevision TGA 2.0 specification](https://raw.githubusercontent.com/DEAKSoftware/Truevision-TGA/master/truevision-tga.pdf): header, color-map and image-type fields, image origins, and optional TGA 2.0 structures.
- [Library of Congress: TGA format description](https://wwws.loc.gov/preservation/digital/formats/fdd/fdd000179.shtml): indexed-color, RGB, and transparency support across TGA versions.
- [W3C PNG Specification](https://www.w3.org/TR/png/): indexed-color PNG requires PLTE; PNG also defines compressed IDAT data, filters, chunks, and optional transparency.
- [Microsoft Bitmap Storage](https://learn.microsoft.com/en-us/windows/win32/gdi/bitmap-storage) and [Bitmap Header Types](https://learn.microsoft.com/en-us/windows/win32/gdi/bitmap-header-types): indexed color tables, bit depth, scanline alignment, and row orientation.
- [GIF89a specification](https://giflib.sourceforge.net/gifstandard/GIF89a.html): global/local color tables and image data rules.
- [EA IFF 85 standard](https://amiga.net.au/files/Tech_Amiga/Commodore_EA_IFF_85_Standard.pdf): FORM/chunk structure and ILBM CMAP/BMHD/BODY raster layout.
- [ZSoft PCX format notes, reproducing the original documentation](https://www.fileformat.info/format/pcx/corion.htm): 128-byte header, 16-color palette, RLE, and optional trailing VGA palette.
