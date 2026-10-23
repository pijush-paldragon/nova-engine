#!/usr/bin/env python3
"""Tests for the nova_pack host tool.  Run: python3 -m unittest tools/test_nova_pack.py -v

The tool is built once into a temp folder (or taken from $NOVA_PACK_TOOL if that points at a binary).
"""

import math
import os
import re
import shutil
import struct
import subprocess
import tempfile
import time
import unittest
import wave
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parent.parent
_TOOL = None
_TOOL_DIR = None


def tool_path() -> Path:
    """Build tools/nova_pack once per test run and return the binary."""
    global _TOOL, _TOOL_DIR
    if _TOOL:
        return _TOOL
    env_tool = os.environ.get("NOVA_PACK_TOOL")
    if env_tool and Path(env_tool).is_file():
        _TOOL = Path(env_tool)
        return _TOOL
    _TOOL_DIR = tempfile.mkdtemp(prefix="nova_pack_build_")
    build = Path(_TOOL_DIR)
    subprocess.run(["cmake", "-S", str(ENGINE_ROOT / "tools" / "nova_pack"), "-B", str(build),
                    "-DCMAKE_BUILD_TYPE=Release"], check=True, capture_output=True)
    subprocess.run(["cmake", "--build", str(build), "-j"], check=True, capture_output=True)
    _TOOL = build / "nova_pack"
    return _TOOL


def run(*args, env=None):
    full_env = dict(os.environ)
    full_env.pop("NOVA_PACK_KEY", None)
    if env:
        full_env.update(env)
    return subprocess.run([str(tool_path()), *map(str, args)], capture_output=True, text=True, env=full_env)


def write_sine_wav(path: Path, seconds=0.25):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(22050)
        # A periodic tone (period 50 samples), like real game audio it compresses well.
        w.writeframes(b"".join(struct.pack("<h", int(12000 * math.sin(2 * math.pi * i / 50)))
                               for i in range(int(22050 * seconds))))


class NovaPackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        tool_path()

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.assets = self.tmp / "assets"
        self.assets.mkdir()
        (self.assets / "a.png").write_bytes(os.urandom(2048))
        write_sine_wav(self.assets / "sfx" / "b.wav")
        (self.assets / "fonts").mkdir()
        shutil.copy(ENGINE_ROOT / "games/brickbreaker/assets/fonts/Fredoka.ttf", self.assets / "fonts" / "c.ttf")
        (self.assets / "README.txt").write_text("hello pack\n")
        (self.assets / ".DS_Store").write_bytes(b"ignored")
        self.key = self.tmp / "pack.key"
        self.assertEqual(run("keygen", self.key).returncode, 0)

    def tearDown(self):
        self._tmp.cleanup()

    def pack(self, out=None, assets=None, key=None):
        out = out or self.tmp / "out" / "game.pak"
        r = run("pack", assets or self.assets, out, "--key-file", key or self.key)
        self.assertEqual(r.returncode, 0, r.stderr)
        return out

    def listing(self, pak, key=None):
        r = run("list", pak, "--key-file", key or self.key)
        self.assertEqual(r.returncode, 0, r.stderr)
        return [line.split("\t") for line in r.stdout.splitlines()]

    def test_keygen_writes_64_hex_and_refuses_overwrite(self):
        self.assertRegex(self.key.read_text(), r"\A[0-9a-f]{64}\n\Z")
        r = run("keygen", self.key)
        self.assertEqual(r.returncode, 1)
        self.assertIn("error:", r.stderr)
        nested = self.tmp / "game" / ".nova" / "pack.key"
        self.assertEqual(run("keygen", nested).returncode, 0)  # creates parent folders
        self.assertNotEqual(nested.read_text(), self.key.read_text())

    def test_pack_verify_list_round_trip(self):
        pak = self.pack()
        self.assertEqual(run("verify", pak, "--key-file", self.key).returncode, 0)
        rows = self.listing(pak)
        self.assertEqual([r[0] for r in rows], ["README.txt", "a.png", "fonts/c.ttf", "sfx/b.wav"])
        kinds = {r[0]: r[3] for r in rows}
        self.assertEqual(kinds["a.png"], "raw")
        self.assertEqual(kinds["sfx/b.wav"], "zstd")
        self.assertEqual(kinds["fonts/c.ttf"], "zstd")
        sizes = {r[0]: (int(r[1]), int(r[2])) for r in rows}
        self.assertEqual(sizes["a.png"][0], 2048)
        self.assertLess(sizes["sfx/b.wav"][1], sizes["sfx/b.wav"][0])

    def test_pack_is_deterministic(self):
        a = self.pack(self.tmp / "o1" / "game.pak").read_bytes()
        b = self.pack(self.tmp / "o2" / "game.pak").read_bytes()
        self.assertEqual(a, b)

    def test_header_and_names_not_visible(self):
        pak = self.pack().read_bytes()
        self.assertEqual(pak[:8], b"NOVAPAK\0")
        for secret in (b"sfx/b.wav", b"README", b"Fredoka", b"hello pack", b"fonts/"):
            self.assertNotIn(secret, pak)

    def test_tamper_detected_by_verify(self):
        original = self.pack().read_bytes()
        for offset in (70, len(original) - 5, 20):
            with self.subTest(offset=offset):
                bad = bytearray(original)
                bad[offset] ^= 0x01
                path = self.tmp / f"bad{offset}.pak"
                path.write_bytes(bytes(bad))
                self.assertEqual(run("verify", path, "--key-file", self.key).returncode, 1)

    def test_verify_rejects_overflowing_header_lengths(self):
        bad = bytearray(self.pack().read_bytes())
        bad[16:24] = struct.pack("<Q", len(bad))
        bad[24:32] = struct.pack("<Q", 2**64 - 16)
        path = self.tmp / "hostile.pak"
        path.write_bytes(bytes(bad))
        r = run("verify", path, "--key-file", self.key)
        self.assertEqual(r.returncode, 1)
        self.assertIn("out of range", r.stderr)

    def test_wrong_key_fails_and_env_key_takes_precedence(self):
        pak = self.pack()
        other = self.tmp / "other.key"
        run("keygen", other)
        self.assertEqual(run("verify", pak, "--key-file", other).returncode, 1)
        right = self.key.read_text().strip()
        self.assertEqual(run("verify", pak, "--key-file", other, env={"NOVA_PACK_KEY": right}).returncode, 0)

    def test_pack_paths_with_spaces_and_unicode(self):
        (self.assets / "ui").mkdir()
        (self.assets / "ui" / "my button.png").write_bytes(b"\x89PNG fake")
        (self.assets / "über").mkdir()
        (self.assets / "über" / "ß.txt").write_text("unicode")
        pak = self.pack()
        paths = [r[0] for r in self.listing(pak)]
        self.assertIn("ui/my button.png", paths)
        self.assertIn("über/ß.txt", paths)
        self.assertEqual(run("verify", pak, "--key-file", self.key).returncode, 0)

    def test_large_incompressible_file_round_trips(self):
        big = 16 * 1024 * 1024
        (self.assets / "big.bin").write_bytes(os.urandom(big))
        start = time.monotonic()
        pak = self.pack()
        self.assertLess(time.monotonic() - start, 10)
        row = next(r for r in self.listing(pak) if r[0] == "big.bin")
        self.assertEqual((int(row[1]), int(row[2]), row[3]), (big, big, "raw"))
        self.assertEqual(run("verify", pak, "--key-file", self.key).returncode, 0)

    def test_concurrent_packs_to_same_output_are_valid(self):
        out = self.tmp / "shared" / "game.pak"
        out.parent.mkdir()
        cmd = [str(tool_path()), "pack", str(self.assets), str(out), "--key-file", str(self.key)]
        env = {k: v for k, v in os.environ.items() if k != "NOVA_PACK_KEY"}
        procs = [subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env) for _ in range(2)]
        for p in procs:
            _, err = p.communicate()
            self.assertEqual(p.returncode, 0, err)
        self.assertEqual(run("verify", out, "--key-file", self.key).returncode, 0)
        self.assertEqual([p.name for p in out.parent.iterdir()], ["game.pak"])  # no temp files left

    def test_embed_key_splits_key(self):
        cpp = self.tmp / "key.cpp"
        self.assertEqual(run("embed-key", self.key, cpp).returncode, 0)
        text = cpp.read_text()
        key_hex = self.key.read_text().strip()
        key_bytes = bytes.fromhex(key_hex)
        as_c = ", ".join(f"0x{b:02x}" for b in key_bytes)
        self.assertNotIn(as_c, text)
        self.assertNotIn(key_hex, text)
        main = self.tmp / "main.cpp"
        main.write_text('#include <cstdint>\n#include <cstdio>\n'
                        'namespace nova::detail { void packKey(std::uint8_t out[32]); }\n'
                        'int main() { std::uint8_t k[32]; nova::detail::packKey(k);'
                        ' for (auto b : k) std::printf("%02x", b); }\n')
        exe = self.tmp / "keytest"
        subprocess.run(["c++", "-std=c++17", str(cpp), str(main), "-o", str(exe)], check=True)
        self.assertEqual(subprocess.run([str(exe)], capture_output=True, text=True).stdout, key_hex)
        again = self.tmp / "key2.cpp"
        run("embed-key", self.key, again)
        self.assertNotEqual(again.read_text(), text)  # fresh random shares each time

    def test_missing_assets_dir_errors_and_leaves_no_output(self):
        out = self.tmp / "none" / "game.pak"
        r = run("pack", self.tmp / "missing", out, "--key-file", self.key)
        self.assertEqual(r.returncode, 1)
        self.assertFalse(out.exists())
        self.assertFalse(any(out.parent.glob("*.tmp.*")) if out.parent.exists() else False)


def tearDownModule():
    if _TOOL_DIR:
        shutil.rmtree(_TOOL_DIR, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
