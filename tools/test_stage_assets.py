#!/usr/bin/env python3
"""Tests for cmake/NovaStageAssets.cmake.  Run: python3 -m unittest tools/test_stage_assets.py -v

Uses the nova_pack tool from test_nova_pack's helper (built once, or $NOVA_PACK_TOOL).
"""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_nova_pack  # noqa: E402

ENGINE_ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ENGINE_ROOT / "cmake" / "NovaStageAssets.cmake"


class StageAssetsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tool = test_nova_pack.tool_path()

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.src = self.tmp / "assets"
        (self.src / "sfx").mkdir(parents=True)
        (self.src / "a.png").write_bytes(b"\x89PNG not really")
        (self.src / "sfx" / "b.wav").write_bytes(b"RIFF not really")
        (self.src / ".DS_Store").write_bytes(b"skip me")
        self.dest = self.tmp / "Bundle.app" / "Resources"
        self.dest.mkdir(parents=True)
        (self.dest / "Info.plist").write_text("unrelated")
        self.key = self.tmp / "pack.key"
        self.assertEqual(test_nova_pack.run("keygen", self.key).returncode, 0)

    def tearDown(self):
        self._tmp.cleanup()

    def stage(self, pack, keyfile=None):
        return subprocess.run(
            ["cmake", f"-DPACK={1 if pack else 0}", f"-DSRC={self.src}", f"-DDEST={self.dest}",
             f"-DTOOL={self.tool}", f"-DKEYFILE={keyfile or self.key}", "-P", str(SCRIPT)],
            capture_output=True, text=True)

    def files(self):
        return sorted(p.relative_to(self.dest).as_posix() for p in self.dest.rglob("*") if p.is_file())

    def manifest(self):
        return (self.dest / ".nova-staged").read_text().split()

    def test_loose_mode_copies_tree_and_writes_manifest(self):
        r = self.stage(pack=False)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(self.files(), [".nova-staged", "Info.plist", "a.png", "sfx/b.wav"])
        self.assertEqual(sorted(self.manifest()), ["a.png", "sfx/b.wav"])
        self.assertEqual((self.dest / "a.png").read_bytes(), b"\x89PNG not really")

    def test_pack_mode_writes_only_pack(self):
        r = self.stage(pack=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(self.files(), [".nova-staged", "Info.plist", "game.pak"])
        self.assertEqual(self.manifest(), ["game.pak"])
        v = test_nova_pack.run("verify", self.dest / "game.pak", "--key-file", self.key)
        self.assertEqual(v.returncode, 0, v.stderr)

    def test_stage_switches_modes_cleanly(self):
        self.assertEqual(self.stage(pack=False).returncode, 0)
        self.assertEqual(self.stage(pack=True).returncode, 0)
        self.assertEqual(self.files(), [".nova-staged", "Info.plist", "game.pak"])
        self.assertFalse((self.dest / "sfx").exists())  # emptied folders go too
        self.assertEqual(self.stage(pack=False).returncode, 0)
        self.assertEqual(self.files(), [".nova-staged", "Info.plist", "a.png", "sfx/b.wav"])
        self.assertEqual((self.dest / "Info.plist").read_text(), "unrelated")

    def test_removed_asset_is_unstaged(self):
        self.assertEqual(self.stage(pack=False).returncode, 0)
        (self.src / "a.png").unlink()
        self.assertEqual(self.stage(pack=False).returncode, 0)
        self.assertEqual(self.files(), [".nova-staged", "Info.plist", "sfx/b.wav"])

    def test_tool_failure_is_fatal(self):
        r = self.stage(pack=True, keyfile=self.tmp / "missing.key")
        self.assertNotEqual(r.returncode, 0)
        self.assertFalse((self.dest / "game.pak").exists())


if __name__ == "__main__":
    unittest.main()
