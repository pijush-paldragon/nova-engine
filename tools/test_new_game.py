#!/usr/bin/env python3
"""Tests for tools/new_game.py.  Run: python3 -m unittest tools/test_new_game.py -v"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import new_game  # noqa: E402

TEMPLATE = new_game.ENGINE_ROOT / "templates" / "game"
# Generator markers; @NOVA_*@ are CMake configure-time placeholders (e.g. in Info.plist.in) and stay.
MARKER = re.compile(r"@(?!NOVA_)[A-Z_]+@")


def text_files(root: Path):
    for path in root.rglob("*"):
        if path.is_file() and ".git" not in path.parts and new_game.is_text(path):
            yield path


class GenerateTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.engine = self.tmp / "nova-engine"

    def tearDown(self):
        self._tmp.cleanup()

    def gen(self, target, **kw):
        kw.setdefault("name", "My Game")
        kw.setdefault("bundle_id", "com.studio.mygame")
        kw.setdefault("engine_dir", self.engine)
        kw.setdefault("git", False)
        return new_game.generate(Path(target), **kw)

    def test_renders_all_placeholders(self):
        out = self.gen(self.tmp / "MyGame")
        leftovers = [str(p) for p in text_files(out) if MARKER.search(p.read_text(encoding="utf-8"))]
        self.assertEqual(leftovers, [])
        cmake = (out / "CMakeLists.txt").read_text()
        self.assertIn("project(mygame", cmake)
        self.assertIn("BUNDLE_ID com.studio.mygame", cmake)
        self.assertTrue((out / ".gitignore").exists())
        self.assertFalse(any(p.name.endswith(".template") for p in out.rglob("*")))

    def test_landscape_swaps_design_size(self):
        out = self.gen(self.tmp / "Wide", orientation="landscape")
        self.assertIn("{1280, 720}", (out / "src" / "main.cpp").read_text())
        self.assertIn("Orientation::Landscape", (out / "src" / "main.cpp").read_text())
        self.assertIn("sensorLandscape", (out / "platforms/android/app/build.gradle").read_text())
        portrait = self.gen(self.tmp / "Tall")
        self.assertIn("{720, 1280}", (portrait / "src" / "main.cpp").read_text())

    def test_relative_engine_path_sibling(self):
        out = self.gen(self.tmp / "MyGame")
        self.assertIn('"${CMAKE_CURRENT_SOURCE_DIR}/../nova-engine"', (out / "CMakeLists.txt").read_text())
        self.assertIn('"${novaGameRoot}/../nova-engine"', (out / "platforms/android/app/build.gradle").read_text())

    def test_relative_engine_path_nested_and_far(self):
        nested = self.gen(self.tmp / "a" / "b" / "Game")
        self.assertIn("/../../../nova-engine", (nested / "CMakeLists.txt").read_text())
        far_engine = self.tmp / "tools" / "engines" / "nova"
        far = self.gen(self.tmp / "work" / "Game2", engine_dir=far_engine)
        self.assertIn("/../../tools/engines/nova", (far / "CMakeLists.txt").read_text())

    def test_engine_path_with_spaces(self):
        engine = self.tmp / "Game Studio" / "nova-engine"
        out = self.gen(self.tmp / "Game Studio" / "Space Game", engine_dir=engine)
        cmake = (out / "CMakeLists.txt").read_text()
        self.assertIn('set(NOVA_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../nova-engine"', cmake)
        self.assertIn('include("${NOVA_ENGINE_DIR}/cmake/Nova.cmake")', cmake)

    def test_refuses_non_empty_target(self):
        target = self.tmp / "Busy"
        target.mkdir()
        (target / "keep.txt").write_text("mine")
        with self.assertRaises(new_game.GeneratorError):
            self.gen(target)
        self.assertEqual(sorted(p.name for p in target.iterdir()), ["keep.txt"])
        self.assertEqual((target / "keep.txt").read_text(), "mine")

    def test_rejects_bad_bundle_id_and_name(self):
        for bundle in ["com", "1abc.x", "com.x y", "com..x"]:
            with self.subTest(bundle=bundle), self.assertRaises(new_game.GeneratorError):
                self.gen(self.tmp / f"bad{abs(hash(bundle))}", bundle_id=bundle)
        for name in ['A"B', "", "x" * 31, "<tag>", "Cost $5", "a;b", "back\\slash"]:
            with self.subTest(name=name), self.assertRaises(new_game.GeneratorError):
                self.gen(self.tmp / f"badname{abs(hash(name))}", name=name)
        with self.assertRaises(new_game.GeneratorError):
            self.gen(self.tmp / "badtarget", target_name="9lives")
        self.assertEqual([p.name for p in self.tmp.iterdir()], [])  # nothing created

    def test_binary_files_identical_and_gradlew_executable(self):
        out = self.gen(self.tmp / "MyGame")
        font = "assets/fonts/Fredoka.ttf"
        self.assertEqual((out / font).read_bytes(), (TEMPLATE / font).read_bytes())
        self.assertEqual((out / "assets/sprites.png").read_bytes(), (TEMPLATE / "assets/sprites.png").read_bytes())
        self.assertTrue(os.access(out / "platforms/android/gradlew", os.X_OK))

    def test_rollback_on_unreplaced_placeholder(self):
        template = self.tmp / "tpl"
        shutil.copytree(TEMPLATE, template)
        (template / "src" / "bogus.cpp").write_text("// @BOGUS@\n")
        target = self.tmp / "Broken"
        with self.assertRaises(new_game.GeneratorError):
            self.gen(target, template_dir=template)
        self.assertFalse(target.exists())

    def test_apostrophe_name_is_escaped_for_android(self):
        out = self.gen(self.tmp / "Bobs", name="Bob's Game")
        self.assertIn("Bob\\\\'s Game", (out / "platforms/android/app/build.gradle").read_text())
        self.assertIn('"Bob\'s Game"', (out / "src/main.cpp").read_text())

    def test_target_name_derived_from_name(self):
        out = self.gen(self.tmp / "Derived", name="Super Space-Run 2")
        self.assertIn("project(superspacerun2", (out / "CMakeLists.txt").read_text())

    @unittest.skipUnless(shutil.which("git"), "git not installed")
    def test_git_flag(self):
        no_git = self.gen(self.tmp / "NoGit")
        self.assertFalse((no_git / ".git").exists())
        with_git = self.gen(self.tmp / "WithGit", git=True)
        log = subprocess.run(["git", "-C", str(with_git), "log", "--oneline"], capture_output=True, text=True)
        self.assertEqual(len(log.stdout.strip().splitlines()), 1)
        status = subprocess.run(["git", "-C", str(with_git), "status", "--porcelain"], capture_output=True, text=True)
        self.assertEqual(status.stdout.strip(), "")


if __name__ == "__main__":
    unittest.main()
