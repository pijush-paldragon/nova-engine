#!/usr/bin/env python3
"""Tests for tools/pack_atlas.py.  Run: python3 -m unittest tools/test_pack_atlas.py -v"""

import re
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    Image = None

import pack_atlas  # noqa: E402

LINE_RE = re.compile(r"^(\S+) (\d+) (\d+) (\d+) (\d+)$")


def read_atlas(path: Path):
    """Parse the engine's .atlas format (same rules as engine/src/assets/atlas.cpp)."""
    lines = [l for l in path.read_text().splitlines() if l.strip() and not l.startswith("#")]
    image = lines[0].split()
    assert image[0] == "image" and len(image) == 2, lines[0]
    regions = {}
    for line in lines[1:]:
        m = LINE_RE.match(line)
        assert m, f"malformed line: {line!r}"
        regions[m.group(1)] = tuple(int(v) for v in m.groups()[1:])
    return image[1], regions


@unittest.skipUnless(Image, "Pillow not installed")
class PackAtlasTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.src = self.tmp / "art"
        self.src.mkdir()

    def tearDown(self):
        self._tmp.cleanup()

    def sprite(self, rel, size, color, mode="RGBA"):
        path = self.src / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        img = Image.new(mode, size, color)
        # A distinct top-left pixel lets tests check orientation and exact placement.
        img.putpixel((0, 0), (255, 255, 255, 255) if mode == "RGBA" else (255, 255, 255))
        img.save(path)
        return img

    def pack(self, **kw):
        out = self.tmp / "out" / "sprites"
        return pack_atlas.pack(self.src, out, **kw), out

    def test_writes_png_and_atlas_with_exact_pixels(self):
        originals = {"ball": self.sprite("ball.png", (32, 32), (200, 40, 40, 255)),
                     "paddle": self.sprite("paddle.png", (160, 32), (40, 200, 40, 255)),
                     "brick": self.sprite("brick.png", (80, 36), (40, 40, 200, 128))}
        result, out = self.pack()
        png, atlas_file = out.with_suffix(".png"), out.with_suffix(".atlas")
        self.assertTrue(png.exists() and atlas_file.exists())
        image_name, regions = read_atlas(atlas_file)
        self.assertEqual(image_name, "sprites.png")
        self.assertEqual(sorted(regions), ["ball", "brick", "paddle"])
        atlas = Image.open(png).convert("RGBA")
        self.assertEqual(atlas.size, result.size)
        for name, original in originals.items():
            x, y, w, h = regions[name]
            self.assertEqual((w, h), original.size)
            crop = atlas.crop((x, y, x + w, y + h))
            self.assertEqual(list(crop.getdata()), list(original.convert("RGBA").getdata()), name)

    def test_atlas_size_is_smallest_power_of_two_that_fits(self):
        for i in range(5):
            self.sprite(f"s{i}.png", (30, 30), (i * 50, 0, 0, 255))
        result, _ = self.pack(padding=2)
        # Sprites are 34 px apart (2 px padding each). 64x64 holds a 2x2 grid of them (padding may
        # run off the atlas edge, where the GPU clamps), so the 5th sprite needs 128x64 / 64x128.
        self.assertEqual(sorted(result.size), [64, 128])
        single = self.tmp / "one"
        single.mkdir()
        Image.new("RGBA", (10, 10)).save(single / "dot.png")
        r = pack_atlas.pack(single, self.tmp / "one_out" / "dot")
        self.assertEqual(r.size, (16, 16))

    def test_regions_do_not_overlap_including_padding(self):
        for i, size in enumerate([(50, 20), (20, 50), (33, 33), (64, 8), (8, 64), (40, 40), (12, 12)]):
            self.sprite(f"s{i}.png", size, (10 * i, 100, 100, 255))
        result, out = self.pack(padding=3)
        _, regions = read_atlas(out.with_suffix(".atlas"))
        for x, y, w, h in regions.values():  # sprites themselves always lie inside the atlas
            self.assertTrue(x >= 0 and y >= 0 and x + w <= result.size[0] and y + h <= result.size[1])
        boxes = [(x - 3, y - 3, x + w + 3, y + h + 3) for x, y, w, h in regions.values()]
        for i, a in enumerate(boxes):
            for b in boxes[i + 1:]:
                self.assertFalse(a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3], (a, b))

    def test_edges_are_extruded_into_padding(self):
        for i in range(9):
            img = Image.new("RGBA", (16, 16), (20 * i, 0, 255 - 20 * i, 255))
            img.putpixel((0, 0), (255, 255, 0, 255))
            img.save(self.src / f"tile{i}.png")
        _, out = self.pack(padding=2)
        atlas = Image.open(out.with_suffix(".png")).convert("RGBA")
        W, H = atlas.size
        _, regions = read_atlas(out.with_suffix(".atlas"))
        inside = lambda px, py: 0 <= px < W and 0 <= py < H
        checked = 0
        for x, y, w, h in regions.values():
            # Pixels in the padding repeat the sprite's nearest edge pixel (incl. corners).
            for (ox, oy), (ex, ey) in [((x + 5, y - 1), (x + 5, y)), ((x + 5, y + h), (x + 5, y + h - 1)),
                                       ((x - 1, y + 5), (x, y + 5)), ((x + w, y + 5), (x + w - 1, y + 5)),
                                       ((x - 2, y - 2), (x, y)), ((x + w + 1, y + h + 1), (x + w - 1, y + h - 1))]:
                if inside(ox, oy):
                    self.assertEqual(atlas.getpixel((ox, oy)), atlas.getpixel((ex, ey)))
                    checked += 1
        self.assertGreater(checked, 12)

    def test_subfolders_become_name_prefixes(self):
        self.sprite("ui/button.png", (20, 10), (1, 2, 3, 255))
        self.sprite("player/run_1.png", (16, 16), (4, 5, 6, 255))
        _, out = self.pack()
        _, regions = read_atlas(out.with_suffix(".atlas"))
        self.assertEqual(sorted(regions), ["player/run_1", "ui/button"])

    def test_rgb_and_jpeg_inputs_are_converted(self):
        self.sprite("opaque.png", (12, 12), (9, 9, 9), mode="RGB")
        Image.new("RGB", (8, 8), (50, 60, 70)).save(self.src / "photo.jpg")
        _, out = self.pack()
        _, regions = read_atlas(out.with_suffix(".atlas"))
        self.assertEqual(sorted(regions), ["opaque", "photo"])
        self.assertEqual(Image.open(out.with_suffix(".png")).mode, "RGBA")

    def test_scale_resizes_sprites(self):
        self.sprite("big.png", (100, 40), (10, 20, 30, 255))
        _, out = self.pack(scale=0.5)
        _, regions = read_atlas(out.with_suffix(".atlas"))
        self.assertEqual(regions["big"][2:], (50, 20))

    def test_errors_write_nothing(self):
        out = self.tmp / "out" / "sprites"
        cases = {
            "empty": lambda: None,
            "too big": lambda: self.sprite("huge.png", (300, 10), (0, 0, 0, 255)),
            "space": lambda: self.sprite("my sprite.png", (8, 8), (0, 0, 0, 255)),
            "duplicate": lambda: (self.sprite("a.png", (8, 8), (0, 0, 0, 255)),
                                  Image.new("RGB", (8, 8)).save(self.src / "a.jpg")),
        }
        for label, setup in cases.items():
            with self.subTest(label):
                for f in self.src.rglob("*"):
                    if f.is_file():
                        f.unlink()
                setup()
                with self.assertRaises(pack_atlas.PackError):
                    pack_atlas.pack(self.src, out, max_size=256)
                self.assertFalse(out.with_suffix(".png").exists())
                self.assertFalse(out.with_suffix(".atlas").exists())

    def test_does_not_fit_in_max_size(self):
        for i in range(9):
            self.sprite(f"tile{i}.png", (60, 60), (i, i, i, 255))
        with self.assertRaises(pack_atlas.PackError) as ctx:
            pack_atlas.pack(self.src, self.tmp / "o" / "s", max_size=128)
        self.assertIn("128", str(ctx.exception))

    def test_output_is_deterministic(self):
        for i, size in enumerate([(10, 30), (30, 10), (20, 20)]):
            self.sprite(f"s{i}.png", size, (i * 80, 0, 0, 255))
        _, out1 = self.pack()
        first = (out1.with_suffix(".png").read_bytes(), out1.with_suffix(".atlas").read_text())
        _, out2 = self.pack()
        self.assertEqual(first, (out2.with_suffix(".png").read_bytes(), out2.with_suffix(".atlas").read_text()))

    def test_cli_main(self):
        self.sprite("a.png", (8, 8), (1, 1, 1, 255))
        out = self.tmp / "cli" / "game"
        self.assertEqual(pack_atlas.main([str(self.src), "-o", str(out)]), 0)
        self.assertTrue(out.with_suffix(".atlas").exists())
        self.assertEqual(pack_atlas.main([str(self.tmp / "missing"), "-o", str(out)]), 2)


if __name__ == "__main__":
    unittest.main()
