#!/usr/bin/env python3
"""Tests for tools/check_strings.py.  Run: python3 -m unittest tools/test_check_strings.py -v"""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parent / "check_strings.py"

EN = {"language": "en", "strings": {
    "menu": {"play": "Play", "best": "Best: {score}", "rich": "[b]Buy[/b] for {price}"},
    "hud": {"lives": {"one": "{count} life", "other": "{count} lives"}},
}}


def run(files):
    with tempfile.TemporaryDirectory() as tmp:
        for name, content in files.items():
            text = content if isinstance(content, str) else json.dumps(content, ensure_ascii=False)
            (Path(tmp) / name).write_text(text, encoding="utf-8")
        r = subprocess.run([sys.executable, str(TOOL), tmp], capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr


def de(**overrides):
    strings = {"menu": {"play": "Spielen", "best": "Rekord: {score}", "rich": "[b]Kaufen[/b] für {price}"},
               "hud": {"lives": {"one": "{count} Leben", "other": "{count} Leben"}}}
    strings.update(overrides)
    return {"language": "de", "strings": strings}


class CheckStringsTests(unittest.TestCase):
    def test_clean_set_passes(self):
        # Comments and trailing commas are allowed, as in the engine's loader.
        commented = '{\n  // German\n  "language": "de",\n  "strings": ' + json.dumps(de()["strings"]) + ',\n}\n'
        code, out = run({"en.json": EN, "de.json": commented,
                         "languages.json": '["en", "de"]'})
        self.assertEqual(code, 0, out)
        self.assertIn("ok", out)

    def test_comments_and_commas_outside_strings_only(self):
        en = '{ "language": "en", /* block */ "strings": { "a": "x,]", // trailing\n "b": "http://y" , }, }'
        de = '{ "language": "de", "strings": { "a": "x,] de", "b": "http://z" } }'
        code, out = run({"en.json": en, "de.json": de})
        self.assertEqual(code, 0, out)

    def test_reports_missing_and_extra(self):
        d = de()
        del d["strings"]["menu"]["play"]
        d["strings"]["menu"]["quit"] = "Beenden"
        code, out = run({"en.json": EN, "de.json": d})
        self.assertEqual(code, 1)
        self.assertIn("de: missing: menu.play", out)
        self.assertIn("de: extra: menu.quit", out)

    def test_reports_plural_categories_for_russian_and_arabic(self):
        ru = {"language": "ru", "strings": dict(de()["strings"], hud={"lives": {"one": "{count} жизнь", "other": "{count} жизней"}})}
        ar = {"language": "ar", "strings": dict(de()["strings"], hud={"lives": {"other": "{count} روح"}})}
        ja = {"language": "ja", "strings": dict(de()["strings"], hud={"lives": {"other": "残機 {count}"}})}
        code, out = run({"en.json": EN, "ru.json": ru, "ar.json": ar, "ja.json": ja})
        self.assertEqual(code, 1)
        self.assertIn("ru: plural hud.lives: ru needs few, many", out)
        self.assertIn("ar: plural hud.lives: ar needs zero, one, two, few, many", out)
        self.assertNotIn("ja:", out)

    def test_reports_placeholder_mismatch(self):
        code, out = run({"en.json": EN, "de.json": de(menu={"play": "Spielen", "best": "Rekord: {punkte}",
                                                             "rich": "[b]Kaufen[/b] für {price}"})})
        self.assertEqual(code, 1)
        self.assertIn("de: placeholders menu.best: {score} vs {punkte}", out)

    def test_reports_unbalanced_markup(self):
        code, out = run({"en.json": EN, "de.json": de(menu={"play": "Spielen", "best": "Rekord: {score}",
                                                             "rich": "[b]Kaufen für {price}"})})
        self.assertEqual(code, 1)
        self.assertIn("de: markup menu.rich: unbalanced [b]", out)

    def test_reports_language_manifest_mismatch(self):
        code, out = run({"en.json": EN, "de.json": de(), "languages.json": '["en", "fr"]'})
        self.assertEqual(code, 1)
        self.assertIn("languages.json: fr has no fr.json", out)
        self.assertIn("languages.json: de.json is not listed", out)


if __name__ == "__main__":
    unittest.main()
