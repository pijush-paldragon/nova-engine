#!/usr/bin/env python3
"""Fetch OFL Noto fonts from Google's font repository and optionally subset them.

    python3 tools/fetch_fonts.py --out assets/fonts --subset "こんにちは世界" NotoSansJP
    python3 tools/fetch_fonts.py --out assets/fonts --subset-text-from assets/text NotoSansArabic
    python3 tools/fetch_fonts.py --subset-file Big.ttf --subset "abc" --out-file Small.ttf

Variable fonts are pinned to Regular (wght 400, wdth 100) before subsetting. Subsetting keeps
only the characters you give (plus ASCII when --keep-ascii), which turns a 10 MB CJK font into a
few hundred KB. Needs fontTools for subsetting: pip install --user fonttools
"""

import argparse
import json
import re
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

REPO = "https://raw.githubusercontent.com/google/fonts/main/ofl"
FAMILIES = {
    "NotoSansArabic": ("notosansarabic", "NotoSansArabic[wdth,wght].ttf"),
    "NotoSansJP": ("notosansjp", "NotoSansJP[wght].ttf"),
    "NotoSansDevanagari": ("notosansdevanagari", "NotoSansDevanagari[wdth,wght].ttf"),
    "NotoSansHebrew": ("notosanshebrew", "NotoSansHebrew[wdth,wght].ttf"),
}
ASCII = "".join(chr(c) for c in range(0x20, 0x7F))


def fail(message):
    print(f"error: {message}", file=sys.stderr)
    sys.exit(1)


def need_fonttools():
    try:
        from fontTools import subset  # noqa: F401
        from fontTools.ttLib import TTFont  # noqa: F401
    except ImportError:
        fail("subsetting needs fontTools: pip install --user fonttools")


def download(url, dest):
    # curl uses the system's certificate store (python.org builds of Python ship without one).
    r = subprocess.run(["curl", "-fsSL", url, "-o", str(dest)], capture_output=True, text=True)
    if r.returncode != 0:
        fail(f"download failed: {url}: {r.stderr.strip()}")


def strings_in(folder):
    """Every character used by the string values of the language files in a folder."""
    chars = set()
    for path in Path(folder).glob("*.json"):
        text = "\n".join(l for l in path.read_text(encoding="utf-8").splitlines() if not l.strip().startswith("//"))
        text = re.sub(r",(\s*[}\]])", r"\1", text)

        def walk(v):
            if isinstance(v, str):
                chars.update(v)
            elif isinstance(v, dict):
                for x in v.values():
                    walk(x)
            elif isinstance(v, list):
                for x in v:
                    walk(x)
        data = json.loads(text)
        if isinstance(data, dict):  # skips languages.json (a list of tags)
            walk(data.get("strings", {}))
    return "".join(sorted(chars))


def subset_font(src, dest, chars):
    from fontTools import subset
    from fontTools.ttLib import TTFont
    font = TTFont(src)
    if "fvar" in font:
        from fontTools.varLib import instancer
        axes = {a.axisTag: (400 if a.axisTag == "wght" else 100 if a.axisTag == "wdth" else a.defaultValue)
                for a in font["fvar"].axes}
        font = instancer.instantiateVariableFont(font, axes)
    options = subset.Options()
    options.layout_features = ["*"]  # keep shaping features (joining, ligatures, kerning)
    options.name_IDs = ["*"]
    options.notdef_outline = True
    sub = subset.Subsetter(options)
    sub.populate(text=chars)
    sub.subset(font)
    font.save(dest)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("families", nargs="*", help="font families: " + ", ".join(FAMILIES))
    ap.add_argument("--out", help="folder for fetched fonts (written as <Family>-sub.ttf when subsetting)")
    ap.add_argument("--subset", default="", help="characters to keep")
    ap.add_argument("--subset-text-from", help="keep every character used in this folder's language files")
    ap.add_argument("--keep-ascii", action="store_true", help="also keep printable ASCII")
    ap.add_argument("--subset-file", help="subset this local font instead of downloading")
    ap.add_argument("--out-file", help="output path for --subset-file")
    args = ap.parse_args()

    chars = args.subset + (strings_in(args.subset_text_from) if args.subset_text_from else "")
    if args.keep_ascii:
        chars += ASCII
    if args.subset_file:
        if not args.out_file or not chars:
            fail("--subset-file needs --out-file and characters to keep")
        need_fonttools()
        subset_font(args.subset_file, args.out_file, chars)
        return
    for family in args.families:
        if family not in FAMILIES:
            fail(f"unknown family {family} (known: {', '.join(FAMILIES)})")
    if not args.out or not args.families:
        fail("give --out and at least one family")
    if chars:
        need_fonttools()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for family in args.families:
        folder, name = FAMILIES[family]
        url = f"{REPO}/{folder}/{urllib.request.quote(name)}"
        if chars:
            with tempfile.TemporaryDirectory() as tmp:
                full = Path(tmp) / name
                download(url, full)
                dest = out / f"{family}-sub.ttf"
                subset_font(full, dest, chars)
        else:
            dest = out / f"{family}.ttf"
            download(url, dest)
        print(f"wrote {dest} ({dest.stat().st_size // 1024} KB)")
    download(f"{REPO}/{FAMILIES[args.families[0]][0]}/OFL.txt", out / "OFL-Noto.txt")


if __name__ == "__main__":
    main()
