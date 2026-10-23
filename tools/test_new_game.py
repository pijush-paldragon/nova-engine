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

    def test_gitignore_protects_pack_key(self):
        out = self.gen(self.tmp / "MyGame")
        self.assertIn(".nova/", (out / ".gitignore").read_text().splitlines())

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
        self.assertIn('set(_nova_engine "${CMAKE_CURRENT_SOURCE_DIR}/../nova-engine")', cmake)
        self.assertIn('include("${_nova_engine}/cmake/Nova.cmake")', cmake)

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

    def test_symlinked_target_resolves_to_engine(self):
        real_engine = self.tmp / "real" / "nova-engine"
        (real_engine / "cmake").mkdir(parents=True)
        (self.tmp / "real" / "games").mkdir()
        os.symlink(self.tmp / "real" / "games", self.tmp / "shortcut")  # shortcut at a different depth
        out = self.gen(self.tmp / "shortcut" / "Game", engine_dir=real_engine)
        rel = re.search(r'CMAKE_CURRENT_SOURCE_DIR\}/([^"]+)"', (out / "CMakeLists.txt").read_text()).group(1)
        # CMake and Gradle see the physical (symlink-resolved) source dir.
        self.assertEqual(Path(os.path.realpath(Path(os.path.realpath(out)) / rel)), Path(os.path.realpath(real_engine)))

    def test_rejects_reserved_target_names(self):
        for name in ["Nova", "Test", "Box2D", "All", "Install", "SDL3", "Main"]:
            with self.subTest(name=name), self.assertRaises(new_game.GeneratorError):
                self.gen(self.tmp / f"r{name}", name=name)

    def test_leading_at_or_question_mark_escaped_for_android(self):
        out = self.gen(self.tmp / "AtHome", name="@Home")
        self.assertIn('"\\\\@Home"', (out / "platforms/android/app/build.gradle").read_text())
        out2 = self.gen(self.tmp / "Quiz", name="?Quiz", target_name="quiz")
        self.assertIn('"\\\\?Quiz"', (out2 / "platforms/android/app/build.gradle").read_text())

    def test_refuses_target_inside_engine(self):
        self.engine.mkdir()
        with self.assertRaises(new_game.GeneratorError):
            self.gen(self.engine / "MyGame")
        self.assertFalse((self.engine / "MyGame").exists())

    def test_rejects_underscore_in_bundle_id(self):
        with self.assertRaises(new_game.GeneratorError):
            self.gen(self.tmp / "Under", bundle_id="com.studio.my_game")

    def test_ios_app_icon_catalog_present(self):
        out = self.gen(self.tmp / "Iconic")
        icon = out / "platforms/ios/Assets.xcassets/AppIcon.appiconset"
        self.assertTrue((icon / "Contents.json").exists())
        self.assertTrue((icon / "AppIcon-1024.png").exists())

    @unittest.skipUnless(shutil.which("git"), "git not installed")
    def test_git_flag(self):
        no_git = self.gen(self.tmp / "NoGit")
        self.assertFalse((no_git / ".git").exists())
        with_git = self.gen(self.tmp / "WithGit", git=True)
        log = subprocess.run(["git", "-C", str(with_git), "log", "--oneline"], capture_output=True, text=True)
        self.assertEqual(len(log.stdout.strip().splitlines()), 1)
        status = subprocess.run(["git", "-C", str(with_git), "status", "--porcelain"], capture_output=True, text=True)
        self.assertEqual(status.stdout.strip(), "")


@unittest.skipUnless(shutil.which("cmake"), "cmake not installed")
class EngineLookupTests(unittest.TestCase):
    """The generated CMakeLists must re-resolve the engine on every configure (-D > env > default)."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        for tag in ("A", "B", "C"):  # stand-in engines: just report which one was included
            cmake_dir = self.tmp / f"engine{tag}" / "cmake"
            cmake_dir.mkdir(parents=True)
            (cmake_dir / "Nova.cmake").write_text(
                f'message(STATUS "NOVA_ENGINE_USED={tag}")\n'
                "function(nova_add_game)\nendfunction()\nfunction(nova_add_game_tests)\nendfunction()\n")
        self.game = new_game.generate(self.tmp / "Game", name="Lookup", bundle_id="com.t.lookup",
                                      engine_dir=self.tmp / "engineA", git=False)

    def tearDown(self):
        self._tmp.cleanup()

    def configure(self, env_engine=None, extra=()):
        env = dict(os.environ)
        env.pop("NOVA_ENGINE_DIR", None)
        if env_engine:
            env["NOVA_ENGINE_DIR"] = str(self.tmp / env_engine)
        r = subprocess.run(["cmake", "-S", str(self.game), "-B", str(self.tmp / "build"), *extra],
                           capture_output=True, text=True, env=env)
        m = re.search(r"NOVA_ENGINE_USED=(\w)", r.stdout)
        return m.group(1) if m else r.stdout + r.stderr

    def test_default_then_env_then_override_then_back(self):
        self.assertEqual(self.configure(), "A")                       # relative default
        self.assertEqual(self.configure(env_engine="engineB"), "B")   # env honoured on reconfigure
        self.assertEqual(self.configure(env_engine="engineB",
                                        extra=[f"-DNOVA_ENGINE_DIR={self.tmp / 'engineC'}"]), "C")  # -D wins
        self.assertEqual(self.configure(extra=["-DNOVA_ENGINE_DIR="]), "A")  # clearing returns to default


class CMakeHelperTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cmake"), "cmake not installed")
    def test_nova_add_game_tests_rejects_unknown_arguments(self):
        with tempfile.TemporaryDirectory() as tmp:
            script = Path(tmp) / "check.cmake"
            script.write_text(f'set(NOVA_BUILD_GAME_TESTS OFF)\nset(NOVA_ENGINE_DIR "{new_game.ENGINE_ROOT}")\n'
                              f'include("{new_game.ENGINE_ROOT}/cmake/NovaGame.cmake")\n'
                              "nova_add_game_tests(demo SOURCE tests/a.cpp)\n")
            r = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("unknown arguments", r.stdout + r.stderr)


if __name__ == "__main__":
    unittest.main()
