#!/usr/bin/env python3
"""Pack a folder of images into a sprite atlas for Nova: <out>.png + <out>.atlas.

Usage:
    python3 <nova-engine>/tools/pack_atlas.py <images-folder> [-o assets/sprites]
            [--padding 2] [--max-size 2048] [--scale 1.0]

Every image in the folder (subfolders too) becomes one region, named after its path without the
extension: art/ui/button.png -> "ui/button". In game code:

    nova::Atlas atlas = assets().atlas("sprites");    // assets/sprites.atlas + sprites.png
    nova::SpriteRegion button = atlas.region("ui/button");

The atlas image is the smallest power-of-two size (up to --max-size) that holds every sprite.
Each sprite gets --padding pixels of its own edge colour around it, so smooth (linear) scaling
never bleeds a neighbour's pixels into it. Needs Pillow (pip3 install pillow).
"""

import argparse
import os
import re
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # reported by main()
    Image = None

IMAGE_SUFFIXES = {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tga", ".webp"}
NAME_RE = re.compile(r"^[A-Za-z0-9_.\-/]+$")


class PackError(Exception):
    pass


@dataclass
class PackResult:
    size: tuple                                  # (width, height) of the atlas image
    regions: dict = field(default_factory=dict)  # name -> (x, y, w, h)
    png_path: Path = None
    atlas_path: Path = None

    @property
    def fill(self) -> float:
        used = sum(w * h for _, _, w, h in self.regions.values())
        return used / (self.size[0] * self.size[1])


# ---------------------------------------------------------------------------- packing (MaxRects)


def _maxrects(cells, bin_w, bin_h):
    """Place (name, w, h) cells in a bin with MaxRects best-short-side-fit.

    Returns {name: (x, y)} or None if they don't all fit.
    """
    free = [(0, 0, bin_w, bin_h)]
    placed = {}
    for name, w, h in cells:
        best = None
        for fx, fy, fw, fh in free:
            if w <= fw and h <= fh:
                short, long_ = min(fw - w, fh - h), max(fw - w, fh - h)
                score = (short, long_, fy, fx)
                if best is None or score < best[0]:
                    best = (score, fx, fy)
        if best is None:
            return None
        _, x, y = best
        placed[name] = (x, y)
        free = _split_free(free, (x, y, w, h))
    return placed


def _split_free(free, used):
    ux, uy, uw, uh = used
    result = []
    for fx, fy, fw, fh in free:
        if ux >= fx + fw or ux + uw <= fx or uy >= fy + fh or uy + uh <= fy:
            result.append((fx, fy, fw, fh))
            continue
        if ux > fx:
            result.append((fx, fy, ux - fx, fh))
        if ux + uw < fx + fw:
            result.append((ux + uw, fy, fx + fw - (ux + uw), fh))
        if uy > fy:
            result.append((fx, fy, fw, uy - fy))
        if uy + uh < fy + fh:
            result.append((fx, uy + uh, fw, fy + fh - (uy + uh)))
    # Drop rectangles contained in another one.
    pruned = []
    for i, a in enumerate(result):
        contained = any(i != j and a[0] >= b[0] and a[1] >= b[1] and a[0] + a[2] <= b[0] + b[2]
                        and a[1] + a[3] <= b[1] + b[3] and (a != b or j < i) for j, b in enumerate(result))
        if not contained:
            pruned.append(a)
    return pruned


def _candidate_sizes(max_size):
    sides = []
    side = 1
    while side <= max_size:
        sides.append(side)
        side *= 2
    # Smallest area first; for equal area prefer square, then wider than tall.
    return sorted(((w, h) for w in sides for h in sides),
                  key=lambda wh: (wh[0] * wh[1], abs(wh[0] - wh[1]), -wh[0]))


def _layout(sprites, padding, max_size):
    """Return ((W, H), {name: (x, y)}) for the smallest power-of-two atlas that fits."""
    # Each sprite occupies a cell of padding on every side. The bin is enlarged by `padding` on
    # every side too, so a cell's outer padding may hang off the atlas edge (the GPU clamps
    # there); sprite pixels themselves always stay inside.
    cells = [(name, img.width + 2 * padding, img.height + 2 * padding) for name, img in sprites]
    cells.sort(key=lambda c: (-max(c[1], c[2]), -(c[1] * c[2]), c[0]))
    need = sum(w * h for _, w, h in cells)
    for w, h in _candidate_sizes(max_size):
        if (w + 2 * padding) * (h + 2 * padding) < need:
            continue
        if any(img.width > w or img.height > h for _, img in sprites):
            continue
        placed = _maxrects(cells, w + 2 * padding, h + 2 * padding)
        if placed is not None:
            # Convert cell positions (virtual bin) to sprite positions in the atlas.
            return (w, h), {name: (x, y) for name, (x, y) in placed.items()}
    total = sum(img.width * img.height for _, img in sprites)
    raise PackError(f"{len(sprites)} sprites ({total} px total) don't fit in a {max_size}x{max_size} atlas; "
                    "split them into several atlases, use --scale, or raise --max-size")


def _extruded(img, padding):
    """The sprite with `padding` pixels of its edge pixels repeated around it."""
    if padding == 0:
        return img
    w, h = img.size
    out = Image.new("RGBA", (w + 2 * padding, h + 2 * padding))
    out.paste(img, (padding, padding))
    out.paste(img.crop((0, 0, w, 1)).resize((w, padding), Image.NEAREST), (padding, 0))
    out.paste(img.crop((0, h - 1, w, h)).resize((w, padding), Image.NEAREST), (padding, h + padding))
    out.paste(img.crop((0, 0, 1, h)).resize((padding, h), Image.NEAREST), (0, padding))
    out.paste(img.crop((w - 1, 0, w, h)).resize((padding, h), Image.NEAREST), (w + padding, padding))
    for (cx, cy), (px, py) in [((0, 0), (0, 0)), ((w - 1, 0), (w + padding, 0)),
                               ((0, h - 1), (0, h + padding)), ((w - 1, h - 1), (w + padding, h + padding))]:
        out.paste(Image.new("RGBA", (padding, padding), img.getpixel((cx, cy))), (px, py))
    return out


# ---------------------------------------------------------------------------- inputs


def _collect(src: Path, skip: set, scale: float):
    sprites = {}
    for path in sorted(src.rglob("*")):
        if not path.is_file() or path.name.startswith(".") or path.suffix.lower() not in IMAGE_SUFFIXES:
            continue
        if path.resolve() in skip:
            continue  # a previous output written inside the input folder
        name = path.relative_to(src).with_suffix("").as_posix()
        if not NAME_RE.match(name):
            raise PackError(f"'{path.relative_to(src)}': rename it using only letters, digits and _ . - "
                            "(region names can't contain spaces)")
        if name in sprites:
            raise PackError(f"two images are both named '{name}' (e.g. {name}.png and {name}.jpg)")
        try:
            with Image.open(path) as opened:
                img = opened.convert("RGBA")
        except OSError as e:
            raise PackError(f"can't read '{path}': {e}") from None
        if scale != 1.0:
            size = (max(1, round(img.width * scale)), max(1, round(img.height * scale)))
            img = img.resize(size, Image.LANCZOS)
        sprites[name] = img
    if not sprites:
        raise PackError(f"no images ({', '.join(sorted(IMAGE_SUFFIXES))}) found in '{src}'")
    return sorted(sprites.items())


def pack(src, out, padding: int = 2, max_size: int = 2048, scale: float = 1.0) -> PackResult:
    """Pack every image under `src` into `out`.png + `out`.atlas. Raises PackError; on error
    nothing is written."""
    src, out = Path(src), Path(out)
    if not src.is_dir():
        raise PackError(f"'{src}' is not a folder")
    if padding < 0 or max_size < 1 or scale <= 0:
        raise PackError("--padding must be >= 0, --max-size >= 1 and --scale > 0")
    png_path, atlas_path = out.with_name(out.name + ".png"), out.with_name(out.name + ".atlas")

    sprites = _collect(src, {png_path.resolve()}, scale)
    too_big = [n for n, img in sprites if img.width > max_size or img.height > max_size]
    if too_big:
        raise PackError(f"larger than --max-size {max_size}: {', '.join(too_big)}")
    (w, h), positions = _layout(sprites, padding, max_size)

    atlas = Image.new("RGBA", (w, h))
    regions = {}
    for name, img in sprites:
        cx, cy = positions[name]
        # Cell (cx, cy) is in the padding-enlarged bin; the sprite starts at the same offset in the atlas.
        atlas.paste(_extruded(img, padding), (cx - padding, cy - padding))
        regions[name] = (cx, cy, img.width, img.height)

    lines = [f"# Generated by tools/pack_atlas.py from {src.name}/ — don't edit; re-run the tool.",
             "# name x y w h", f"image {png_path.name}"]
    lines += [f"{name} {x} {y} {rw} {rh}" for name, (x, y, rw, rh) in sorted(regions.items())]

    out.parent.mkdir(parents=True, exist_ok=True)
    # Write both files to temporaries first so a failure never leaves a half-updated pair.
    tmp_png = tempfile.NamedTemporaryFile(dir=out.parent, suffix=".png", delete=False)
    tmp_png.close()
    try:
        atlas.save(tmp_png.name, optimize=True)
        tmp_atlas = Path(tmp_png.name).with_suffix(".atlas.tmp")
        tmp_atlas.write_text("\n".join(lines) + "\n")
        os.replace(tmp_png.name, png_path)
        os.replace(tmp_atlas, atlas_path)
    finally:
        for leftover in (Path(tmp_png.name), Path(tmp_png.name).with_suffix(".atlas.tmp")):
            if leftover.exists():
                leftover.unlink()
    return PackResult((w, h), regions, png_path, atlas_path)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Pack a folder of images into a Nova sprite atlas (.png + .atlas).")
    ap.add_argument("folder", help="folder of images (subfolders become name prefixes)")
    ap.add_argument("-o", "--out", default="sprites",
                    help="output path without extension (default: ./sprites → sprites.png + sprites.atlas)")
    ap.add_argument("--padding", type=int, default=2, help="pixels of extruded edge around each sprite (default 2)")
    ap.add_argument("--max-size", type=int, default=2048, help="largest atlas side in pixels (default 2048)")
    ap.add_argument("--scale", type=float, default=1.0, help="resize every sprite first, e.g. 0.5 (default 1.0)")
    args = ap.parse_args(argv)
    if Image is None:
        print("error: Pillow is required: pip3 install pillow", file=sys.stderr)
        return 2
    try:
        result = pack(Path(args.folder), Path(args.out), args.padding, args.max_size, args.scale)
    except PackError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    w, h = result.size
    print(f"Packed {len(result.regions)} sprites into {w}x{h} ({result.fill:.0%} used)")
    print(f"  {result.png_path}\n  {result.atlas_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
