#!/usr/bin/env python3
"""Tests for tools/fetch_fonts.py.  Run: python3 -m unittest tools/test_fetch_fonts.py -v"""

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parent / "fetch_fonts.py"
FREDOKA = Path(__file__).resolve().parent.parent / "games/brickbreaker/assets/fonts/Fredoka.ttf"
HAS_FONTTOOLS = importlib.util.find_spec("fontTools") is not None


class FetchFontsTests(unittest.TestCase):
    def test_strings_in_skips_language_list(self):
        spec = importlib.util.spec_from_file_location("fetch_fonts", TOOL)
        tool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(tool)
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "languages.json").write_text('["en", "ja"]', encoding="utf-8")
            (Path(tmp) / "ja.json").write_text('{\n // c\n "strings": {"a": {"b": "日本"}},\n}', encoding="utf-8")
            self.assertEqual(tool.strings_in(tmp), "日本")

    def test_missing_fonttools_message(self):
        # Simulate a Python without fontTools by blocking the import.
        code = ("import sys, runpy; sys.modules['fontTools'] = None; "
                f"sys.argv = ['fetch_fonts.py', '--out', '/tmp/x', '--subset', 'abc', 'NotoSansArabic']; "
                f"runpy.run_path({str(TOOL)!r}, run_name='__main__')")
        r = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True)
        self.assertEqual(r.returncode, 1)
        self.assertIn("pip install --user fonttools", r.stderr)

    @unittest.skipUnless(HAS_FONTTOOLS, "fontTools not installed")
    def test_subset_keeps_only_requested_chars(self):
        from fontTools.ttLib import TTFont
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "sub.ttf"
            r = subprocess.run([sys.executable, str(TOOL), "--subset-file", str(FREDOKA), "--subset", "Hello",
                                "--out-file", str(out)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            cmap = TTFont(out).getBestCmap()
            self.assertEqual(sorted(chr(c) for c in cmap), sorted(set("Hello")))
            self.assertLess(out.stat().st_size, FREDOKA.stat().st_size)

    def test_unknown_family_is_an_error(self):
        r = subprocess.run([sys.executable, str(TOOL), "--out", "/tmp/x", "NoSuchFont"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 1)
        self.assertIn("unknown family", r.stderr)


if __name__ == "__main__":
    unittest.main()
