#!/usr/bin/env python3
"""Render the upstream VF2 artwork as an opaque, square iOS icon. Requires Pillow."""
import importlib.util
from pathlib import Path
import random

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("vf2_icon", ROOT / "tools/release/make_icon.py")
art = importlib.util.module_from_spec(spec)
spec.loader.exec_module(art)


def main():
    # Reuse the upstream palette, letter geometry and bevels without changing its Mac/Windows generator.
    c1, c2, base, edge = art.PALETTES["steel_red"]
    image = Image.new("RGB", (art.N, art.N))
    draw = ImageDraw.Draw(image)
    rng = random.Random(7)
    step = 128 * art.SS
    points = {}
    for gy in range(0, art.N + step, step):
        for gx in range(0, art.N + step, step):
            jitter = 0 if gx in (0, art.N) or gy in (0, art.N) else step * 0.32
            points[gx, gy] = (gx + rng.uniform(-jitter, jitter), gy + rng.uniform(-jitter, jitter))
    for gy in range(0, art.N, step):
        for gx in range(0, art.N, step):
            a, b, c, d = points[gx, gy], points[gx + step, gy], points[gx + step, gy + step], points[gx, gy + step]
            for triangle in ((a, b, c), (a, c, d)):
                cx = sum(p[0] for p in triangle) / 3 / art.N
                cy = sum(p[1] for p in triangle) / 3 / art.N
                color = art.mix(c1, c2, min(1, max(0, (cx + cy) / 2 + rng.uniform(-0.08, 0.08))))
                draw.polygon(triangle, fill=art.shade(color, rng.uniform(0.88, 1.12)))
    for quad in art.strokes():
        cx = sum(p[0] for p in quad) / 4
        cy = sum(p[1] for p in quad) / 4
        grown = [(x + (12 if x > cx else -12), y + (12 if y > cy else -12)) for x, y in quad]
        draw.polygon([(art.skew(p)[0] * art.SS, (art.skew(p)[1] + 10) * art.SS) for p in grown], fill=(0, 0, 0))
    for quad in art.strokes():
        art.chisel(draw, quad, base, edge, (-0.55, -0.83))
    image = image.resize((1024, 1024), Image.Resampling.LANCZOS)
    destination = ROOT / "apple/Assets.xcassets/AppIcon.appiconset/AppIcon.png"
    destination.parent.mkdir(parents=True, exist_ok=True)
    image.save(destination)
    print(f"Wrote {destination.relative_to(ROOT)} (1024 x 1024, RGB, no alpha)")


if __name__ == "__main__":
    main()
