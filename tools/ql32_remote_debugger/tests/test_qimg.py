from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from tools.ql32_remote_debugger.qimg import (
    SPIFFS_MAX_COMPONENT_BYTES,
    convert_image,
    encode_qimg,
    suggest_device_path,
    validate_device_path,
)


class QimgTests(unittest.TestCase):
    def test_v2_header_palette_and_high_nibble_first_rows(self):
        palette = [(i, 255 - i, i // 2) for i in range(16)]
        data = encode_qimg(3, 2, palette, [1, 2, 3, 4, 5, 6])
        self.assertEqual(data[:4], b"QIMG")
        self.assertEqual(data[4:6], bytes((2, 4)))
        self.assertEqual(struct.unpack_from("<HHHI", data, 6), (3, 2, 2, 4))
        self.assertEqual(data[16:64], bytes(c for rgb in palette for c in rgb))
        self.assertEqual(data[64:], bytes((0x12, 0x30, 0x45, 0x60)))

    def test_palette_is_padded_to_sixteen_colors(self):
        data = encode_qimg(1, 1, [(255, 4, 2)], [0])
        self.assertEqual(len(data), 65)
        self.assertEqual(data[16:19], bytes((255, 4, 2)))
        self.assertEqual(data[19:64], bytes(45))
        self.assertEqual(data[64], 0)

    def test_suggested_uuid_filename_fits_spiffs(self):
        path = suggest_device_path(
            "/tmp/4e641ee9-babf-4076-b960-6d4c88222382.png")
        filename = path.rsplit("/", 1)[-1]
        self.assertEqual(path, "FFS:/4e641ee9-babf-4076-b960-6d.qimg")
        self.assertLessEqual(len(filename.encode("utf-8")),
                             SPIFFS_MAX_COMPONENT_BYTES)

    def test_rejects_long_spiffs_components_but_allows_sdc_names(self):
        validate_device_path("FFS:/" + "a" * 26 + ".qimg")
        with self.assertRaisesRegex(ValueError, "limits each path component to 31 bytes"):
            validate_device_path("FFS:/" + "a" * 27 + ".qimg")
        validate_device_path("SDC:/" + "a" * 50 + ".qimg")

    def test_rejects_bad_dimensions_indexes_and_payload_size(self):
        with self.assertRaises(ValueError):
            encode_qimg(0, 1, [(0, 0, 0)], [0])
        with self.assertRaises(ValueError):
            encode_qimg(2, 1, [(0, 0, 0)], [0])
        with self.assertRaises(ValueError):
            encode_qimg(1, 1, [(0, 0, 0)], [16])
        with self.assertRaises(ValueError):
            encode_qimg(2048, 2048, [(0, 0, 0)], [0] * (2048 * 2048))

    def test_pillow_image_conversion_when_dependency_is_installed(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow is installed by the debugger requirements")
        image = Image.new("RGBA", (4, 2), (255, 0, 0, 255))
        image.putpixel((0, 0), (0, 0, 255, 0))
        data, preview = convert_image(image, 3, 2, dither=False)
        self.assertEqual((preview.width, preview.height), (3, 2))
        self.assertEqual(data[:6], b"QIMG\x02\x04")
        self.assertEqual(len(data), 64 + 4)


if __name__ == "__main__":
    unittest.main()
