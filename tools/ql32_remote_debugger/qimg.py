"""QIMG v2 serialization and conversion helpers for the remote debugger."""

from __future__ import annotations

import struct
import re
from pathlib import Path

VERSION = 2
PIXEL_FORMAT_INDEXED_4 = 4
HEADER_SIZE = 64
MAX_DIMENSION = 2048
MAX_PAYLOAD_BYTES = 1024 * 1024
SPIFFS_MAX_COMPONENT_BYTES = 31


def suggest_device_path(source_path: str, directory: str = "FFS:/") -> str:
    """Suggest a stable QIMG name that fits the ESP32 SPIFFS limit."""
    stem = Path(source_path).stem
    safe = re.sub(r"[^A-Za-z0-9_-]+", "_", stem).strip("_") or "image"
    suffix = ".qimg"
    safe = safe.encode("ascii")[:SPIFFS_MAX_COMPONENT_BYTES - len(suffix)].decode("ascii")
    directory = directory.rstrip("/")
    return f"{directory}/{safe}{suffix}"


def validate_device_path(path: str) -> None:
    """Reject destination components SPIFFS cannot create on the ESP32."""
    match = re.match(r"^([A-Za-z][A-Za-z0-9+.-]*):(.*)$", path.strip())
    if not match or match.group(1).upper() != "FFS":
        return
    components = [part for part in match.group(2).split("/") if part not in ("", ".")]
    for component in components:
        if len(component.encode("utf-8")) > SPIFFS_MAX_COMPONENT_BYTES:
            raise ValueError(
                "FFS (SPIFFS) limits each path component to 31 bytes; "
                f"'{component}' is {len(component.encode('utf-8'))} bytes")


def encode_qimg(width: int, height: int, palette, indices) -> bytes:
    """Encode row-major palette indexes as an uncompressed QIMG v2 file.

    ``palette`` contains RGB triples. It is padded to 16 entries with black;
    ``indices`` contains exactly width*height values in the range 0..15.
    """
    width = int(width)
    height = int(height)
    if not 1 <= width <= MAX_DIMENSION or not 1 <= height <= MAX_DIMENSION:
        raise ValueError(f"dimensions must be from 1 to {MAX_DIMENSION}")
    stride = (width + 1) // 2
    payload_size = stride * height
    if payload_size > MAX_PAYLOAD_BYTES:
        raise ValueError("packed image data exceeds the 1 MiB QIMG limit")

    colors = []
    for color in palette:
        if len(color) != 3:
            raise ValueError("palette entries must be RGB triples")
        rgb = tuple(int(component) for component in color)
        if any(component < 0 or component > 255 for component in rgb):
            raise ValueError("palette components must be in the range 0..255")
        colors.append(rgb)
    if not colors or len(colors) > 16:
        raise ValueError("palette must contain from 1 to 16 colors")
    colors.extend([(0, 0, 0)] * (16 - len(colors)))

    values = bytes(indices)
    if len(values) != width * height:
        raise ValueError("index buffer length does not match image dimensions")
    if any(index > 15 for index in values):
        raise ValueError("pixel indexes must be in the range 0..15")

    packed = bytearray(payload_size)
    for y in range(height):
        source = y * width
        target = y * stride
        for x in range(width):
            value = values[source + x]
            if x & 1:
                packed[target + x // 2] |= value
            else:
                packed[target + x // 2] = value << 4

    header = struct.pack("<4sBBHHHI", b"QIMG", VERSION,
                         PIXEL_FORMAT_INDEXED_4, width, height,
                         stride, payload_size)
    palette_bytes = bytes(component for color in colors for component in color)
    return header + palette_bytes + packed


def convert_image(image, width: int, height: int, *, dither: bool = True):
    """Resize and quantize a Pillow image, returning ``(qimg_bytes, preview)``.

    Alpha is composited onto black because QIMG v2 stores RGB palette colors
    without per-pixel alpha. BASIC BLIT transparency remains index-based.
    """
    try:
        from PIL import Image
    except ImportError as error:  # pragma: no cover - depends on app install
        raise RuntimeError("Install the debugger image dependency: Pillow") from error

    width, height = int(width), int(height)
    if not 1 <= width <= MAX_DIMENSION or not 1 <= height <= MAX_DIMENSION:
        raise ValueError(f"dimensions must be from 1 to {MAX_DIMENSION}")
    stride = (width + 1) // 2
    if stride * height > MAX_PAYLOAD_BYTES:
        raise ValueError("packed image data exceeds the 1 MiB QIMG limit")

    rgba = image.convert("RGBA")
    background = Image.new("RGBA", rgba.size, (0, 0, 0, 255))
    rgb = Image.alpha_composite(background, rgba).convert("RGB")
    resized = rgb.resize((width, height), Image.Resampling.LANCZOS)
    dither_mode = (Image.Dither.FLOYDSTEINBERG if dither
                   else Image.Dither.NONE)
    indexed = resized.quantize(colors=16, method=Image.Quantize.MEDIANCUT,
                               dither=dither_mode)
    raw_palette = indexed.getpalette() or []
    palette = [tuple(raw_palette[offset:offset + 3])
               for offset in range(0, min(len(raw_palette), 48), 3)]
    if not palette:
        palette = [(0, 0, 0)]
    indices = indexed.tobytes()
    data = encode_qimg(width, height, palette, indices)
    preview = indexed.convert("RGB")
    return data, preview
