#!/usr/bin/env python3
"""Create a new game folder built on nova-engine (Python standard library only).

Usage:
    python3 <nova-engine>/tools/new_game.py <target-dir> --name "My Game" --bundle-id com.studio.mygame
            [--orientation portrait|landscape] [--target mygame] [--no-git]

The game builds against the live engine source; it finds the engine through a path relative to
the game (by default the nova-engine checkout this script lives in).
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_TEMPLATE = ENGINE_ROOT / "templates" / "game"

# Letters and digits only per segment: iOS forbids '_' in bundle ids, Android forbids '-'.
BUNDLE_ID_RE = re.compile(r"^[A-Za-z][A-Za-z0-9]*(\.[A-Za-z][A-Za-z0-9]*)+$")
TARGET_RE = re.compile(r"^[a-z][a-z0-9_]*$")
# CMake target names already taken by the engine, its libraries or CMake itself.
RESERVED_TARGETS = {"nova", "nova_tests", "main", "test", "all", "help", "install", "clean", "package",
                    "package_source", "edit_cache", "rebuild_cache", "list_install_components", "zero_check",
                    "all_build", "box2d", "entt", "doctest", "freetype", "uninstall"}
RESERVED_TARGET_PREFIXES = ("sdl",)
# Characters that would break a C++ string, CMake, Gradle or XML (apostrophes are escaped instead).
FORBIDDEN_NAME_CHARS = set('"<>&\\$;')
# Generator markers. @NOVA_*@ are CMake configure-time placeholders (Info.plist.in) and stay.
MARKER_RE = re.compile(r"@(?!NOVA_)[A-Z_]+@")

TEXT_SUFFIXES = {".txt", ".cmake", ".json", ".md", ".cpp", ".hpp", ".h", ".gradle", ".properties",
                 ".xml", ".plist", ".in", ".storyboard", ".java", ".atlas", ".template", ".bat"}
TEXT_NAMES = {"gradlew"}
RENAMES = {"gitignore.template": ".gitignore"}


class GeneratorError(Exception):
    pass


def is_text(path: Path) -> bool:
    return path.suffix in TEXT_SUFFIXES or path.name in TEXT_NAMES


def output_name(name: str) -> str:
    if name in RENAMES:
        return RENAMES[name]
    return name[: -len(".template")] if name.endswith(".template") else name


def derive_target(name: str) -> str:
    return re.sub(r"[^a-z0-9]", "", name.lower())


def validate(name: str, bundle_id: str, orientation: str, target_name: str) -> None:
    if not 1 <= len(name) <= 30:
        raise GeneratorError("--name must be 1-30 characters")
    bad = sorted(FORBIDDEN_NAME_CHARS.intersection(name)) + [c for c in name if ord(c) < 32]
    if bad:
        raise GeneratorError(f"--name may not contain {' '.join(repr(c) for c in bad)}")
    if not BUNDLE_ID_RE.match(bundle_id):
        raise GeneratorError(f"--bundle-id '{bundle_id}' must look like com.studio.game (letters, digits, dots)")
    if orientation not in ("portrait", "landscape"):
        raise GeneratorError("--orientation must be portrait or landscape")
    if not TARGET_RE.match(target_name):
        raise GeneratorError(f"target name '{target_name}' must match [a-z][a-z0-9_]* (pass --target)")
    if target_name in RESERVED_TARGETS or target_name.startswith(RESERVED_TARGET_PREFIXES):
        raise GeneratorError(f"target name '{target_name}' is reserved by the engine or CMake (pass --target)")


def placeholders(name, bundle_id, orientation, target_name, engine_rel):
    landscape = orientation == "landscape"
    return {
        "GAME_NAME": name,
        "GAME_NAME_ANDROID": android_resource_string(name),
        "GAME_TARGET": target_name,
        "BUNDLE_ID": bundle_id,
        "SAVE_ORG": bundle_id.rsplit(".", 1)[0],
        "ORIENTATION": "Landscape" if landscape else "Portrait",
        "ANDROID_ORIENTATION": "sensorLandscape" if landscape else "sensorPortrait",
        "DESIGN_W": "1280" if landscape else "720",
        "DESIGN_H": "720" if landscape else "1280",
        "ENGINE_REL_PATH": engine_rel,
    }


def android_resource_string(name: str) -> str:
    r"""Escape a name for a Groovy "..." literal that becomes an Android string resource.

    The resource needs \' for apostrophes and a leading \@ / \? (else it is read as a resource
    reference); inside the Groovy literal each backslash is doubled.
    """
    escaped = name.replace("'", "\\\\'")
    if escaped[:1] in ("@", "?"):
        escaped = "\\\\" + escaped
    return escaped


def render(text: str, values: dict, source: Path) -> str:
    for key, value in values.items():
        text = text.replace(f"@{key}@", value)
    leftover = MARKER_RE.search(text)
    if leftover:
        raise GeneratorError(f"unknown placeholder {leftover.group(0)} in template file {source}")
    return text


def generate(target: Path, name: str, bundle_id: str, orientation: str = "portrait",
             target_name: str | None = None, engine_dir: Path = ENGINE_ROOT, git: bool = True,
             template_dir: Path | None = None) -> Path:
    """Create the game at `target`; returns its absolute path. Raises GeneratorError."""
    target_name = target_name or derive_target(name)
    validate(name, bundle_id, orientation, target_name)
    template_dir = Path(template_dir or DEFAULT_TEMPLATE)
    if not template_dir.is_dir():
        raise GeneratorError(f"template folder not found: {template_dir}")

    # Resolve symlinks: CMake and Gradle compute paths from the physical directories.
    target = Path(os.path.realpath(target))
    engine_dir = Path(os.path.realpath(engine_dir))
    if target == engine_dir or engine_dir in target.parents:
        raise GeneratorError(f"'{target}' is inside the engine repo; create games next to it (e.g. ../MyGame)")
    existed = target.exists()
    if existed and (not target.is_dir() or any(target.iterdir())):
        raise GeneratorError(f"'{target}' already exists and is not an empty folder")

    engine_rel = Path(os.path.relpath(engine_dir, target)).as_posix()
    values = placeholders(name, bundle_id, orientation, target_name, engine_rel)

    try:
        target.mkdir(parents=True, exist_ok=True)
        for source in sorted(template_dir.rglob("*")):
            if source.is_dir() or source.name == ".DS_Store":
                continue
            rel = source.relative_to(template_dir)
            dest = target / rel.parent / output_name(rel.name)
            dest.parent.mkdir(parents=True, exist_ok=True)
            if is_text(source):
                dest.write_text(render(source.read_text(encoding="utf-8"), values, rel), encoding="utf-8")
                shutil.copymode(source, dest)
            else:
                shutil.copy2(source, dest)
    except BaseException:
        if existed:  # it was empty before: put it back that way
            for child in target.iterdir():
                shutil.rmtree(child) if child.is_dir() else child.unlink()
        else:
            shutil.rmtree(target, ignore_errors=True)
        raise

    if git:
        init_git(target)
    return target


def init_git(target: Path) -> None:
    if not shutil.which("git"):
        print("warning: git not found; skipped git init", file=sys.stderr)
        return
    for cmd in (["git", "init", "-q", "-b", "main"], ["git", "add", "-A"],
                ["git", "commit", "-q", "-m", "Initial commit from nova-engine template"]):
        result = subprocess.run(cmd, cwd=target, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"warning: '{' '.join(cmd)}' failed: {result.stderr.strip()}", file=sys.stderr)
            return


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Create a new game built on nova-engine.")
    ap.add_argument("target", help="folder to create, e.g. ../MyGame")
    ap.add_argument("--name", required=True, help='display name, e.g. "My Game"')
    ap.add_argument("--bundle-id", required=True, help="reverse-DNS app id, e.g. com.studio.mygame")
    ap.add_argument("--orientation", default="portrait", choices=["portrait", "landscape"])
    ap.add_argument("--target", dest="target_name", help="CMake/executable name (default: from --name)")
    ap.add_argument("--no-git", action="store_true", help="don't run git init / initial commit")
    args = ap.parse_args(argv)
    try:
        out = generate(Path(args.target), args.name, args.bundle_id, args.orientation, args.target_name,
                       git=not args.no_git)
    except GeneratorError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    target_name = args.target_name or derive_target(args.name)
    print(f"Created {args.name} at {out}\n")
    print("Next steps:")
    print(f"  cd \"{out}\"")
    print("  macOS:   cmake --preset macos-debug && cmake --build --preset macos-debug")
    print(f"           build/macos-debug/{target_name}.app/Contents/MacOS/{target_name}")
    print(f"  iOS:     cmake --preset ios && open build/ios/{target_name}.xcodeproj")
    print("  Android: cd platforms/android && ./gradlew installDebug")
    return 0


if __name__ == "__main__":
    sys.exit(main())
