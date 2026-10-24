#!/usr/bin/env python3
"""Check a game's translation files against en.json.

    python3 tools/check_strings.py games/mygame/assets/text

For every <lang>.json it reports keys missing or extra versus en.json, plural forms the
language needs but lacks (CLDR rules, as the engine applies them), {placeholders} that differ
from English, and rich-text tags that don't open and close in pairs. With a languages.json it
also checks that the list and the files agree. Exit status 0 when clean, 1 otherwise.
"""

import json
import re
import sys
from pathlib import Path

CATEGORIES = ["zero", "one", "two", "few", "many", "other"]
STYLE_TAGS = {"b", "i", "color", "size", "style"}


def strip_json5(text):
    """Removes // and /* */ comments and trailing commas outside strings (yyjson accepts them)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            while i < n and text[i] != "\n":
                i += 1
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = n if end < 0 else end + 2
        elif c == ",":
            j = i + 1
            # A comma followed (past spaces and comments) by } or ] is a trailing comma.
            k = j
            while k < n:
                if text[k].isspace():
                    k += 1
                elif text.startswith("//", k):
                    while k < n and text[k] != "\n":
                        k += 1
                elif text.startswith("/*", k):
                    end = text.find("*/", k + 2)
                    k = n if end < 0 else end + 2
                else:
                    break
            if k >= n or text[k] not in "}]":
                out.append(c)
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def load(path):
    """JSON with comments and trailing commas, as the engine's loader accepts."""
    return json.loads(strip_json5(path.read_text(encoding="utf-8")))


def plural_category(tag, n):
    """The engine's CLDR rules (engine/src/text/plural.cpp)."""
    lang = re.split(r"[-_]", tag)[0]
    mod10, mod100 = n % 10, n % 100
    if lang in ("ja", "zh", "ko", "th", "id", "ms", "vi", "lo", "my", "km"):
        return "other"
    if lang == "fr" or tag in ("pt-BR", "pt_BR"):
        return "one" if n <= 1 else "other"
    if lang in ("ru", "uk", "be", "pl"):
        if (n == 1) if lang == "pl" else (mod10 == 1 and mod100 != 11):
            return "one"
        if 2 <= mod10 <= 4 and not 12 <= mod100 <= 14:
            return "few"
        return "many"
    if lang in ("cs", "sk"):
        return "one" if n == 1 else "few" if 2 <= n <= 4 else "other"
    if lang == "ar":
        if n <= 2:
            return ["zero", "one", "two"][n]
        return "few" if 3 <= mod100 <= 10 else "many" if 11 <= mod100 <= 99 else "other"
    if lang == "he":
        return "one" if n == 1 else "two" if n == 2 else "other"
    return "one" if n == 1 else "other"


def categories_of(tag):
    found = {plural_category(tag, n) for n in (0, 1, 2, 3, 5, 11, 21, 22, 100, 101, 111)}
    return [c for c in CATEGORIES if c in found]


def flatten(strings, prefix=""):
    """Dotted keys -> str, or dict of plural forms."""
    out = {}
    for key, value in strings.items():
        name = f"{prefix}.{key}" if prefix else key
        if isinstance(value, dict) and value and all(k in CATEGORIES for k in value):
            out[name] = value
        elif isinstance(value, dict):
            out.update(flatten(value, name))
        else:
            out[name] = value
    return out


def texts(value):
    return list(value.values()) if isinstance(value, dict) else [value]


def placeholders(value):
    names = set()
    for t in texts(value):
        if isinstance(t, str):
            names.update(re.findall(r"(?<!\{)\{([A-Za-z0-9_]+)\}(?!\})", t))
    return names


def markup_problems(text):
    """Tags that don't pair up ([[ is a literal bracket)."""
    problems, stack = [], []
    for m in re.finditer(r"\[\[|\[(/?)([a-z]+)(?:=[^\]]*)?\]", text):
        if m.group(0) == "[[" or m.group(2) == "icon" or m.group(2) not in STYLE_TAGS:
            continue
        name = m.group(2)
        if not m.group(1):
            stack.append(name)
        elif stack and stack[-1] == name:
            stack.pop()
        else:
            problems.append(f"unbalanced [/{name}]")
    problems.extend(f"unbalanced [{name}]" for name in stack)
    return problems


def fmt(names):
    return ", ".join("{" + n + "}" for n in sorted(names)) or "none"


def check(folder):
    folder = Path(folder)
    problems = []
    en_path = folder / "en.json"
    if not en_path.exists():
        return [f"{folder}: no en.json"]
    files = {p.stem: p for p in folder.glob("*.json") if p.name != "languages.json"}
    tables = {}
    for lang, path in sorted(files.items()):
        try:
            tables[lang] = flatten(load(path).get("strings", {}))
        except (json.JSONDecodeError, AttributeError) as e:
            problems.append(f"{lang}: cannot read {path.name}: {e}")
    english = tables.get("en", {})

    for lang, table in sorted(tables.items()):
        for key, value in sorted(table.items()):
            for t in texts(value):
                for p in markup_problems(t) if isinstance(t, str) else []:
                    problems.append(f"{lang}: markup {key}: {p}")
        if lang == "en":
            continue
        for key in sorted(english.keys() - table.keys()):
            problems.append(f"{lang}: missing: {key}")
        for key in sorted(table.keys() - english.keys()):
            problems.append(f"{lang}: extra: {key}")
        for key in sorted(english.keys() & table.keys()):
            ours, theirs = table[key], english[key]
            if isinstance(theirs, dict) or isinstance(ours, dict):
                have = set(ours) if isinstance(ours, dict) else {"other"}
                lacking = [c for c in categories_of(lang) if c not in have]
                if lacking:
                    problems.append(f"{lang}: plural {key}: {lang} needs {', '.join(lacking)}")
            if placeholders(ours) != placeholders(theirs):
                problems.append(f"{lang}: placeholders {key}: {fmt(placeholders(theirs))} vs {fmt(placeholders(ours))}")

    manifest = folder / "languages.json"
    if manifest.exists():
        listed = load(manifest)
        for lang in listed:
            if lang not in files:
                problems.append(f"languages.json: {lang} has no {lang}.json")
        for lang in sorted(files):
            if lang not in listed:
                problems.append(f"languages.json: {lang}.json is not listed")
    return problems


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        sys.exit(2)
    problems = check(sys.argv[1])
    for p in problems:
        print(p)
    if problems:
        sys.exit(1)
    print(f"ok: {len(list(Path(sys.argv[1]).glob('*.json')))} files")


if __name__ == "__main__":
    main()
