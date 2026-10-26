#!/usr/bin/env python3
"""The app icon: "VF2" in chiselled low-poly letters on a faceted tile, drawn from scratch (no art
from the game), as vf1-recomp's "VF1". Writes build/icon/icon_<variant>.png at 1024x1024 (the
macOS icon grid: an 824px rounded tile with room for the shadow) and choices.png, a sheet of them.
--final <variant> writes the icons the packages use: assets/icon/icon.png, VF2.icns (the Mac app,
by iconutil) and VF2.ico (the Windows exe). --android <variant> writes the Android app's icons
(the launcher's own and the adaptive icon's two layers, see android()) into the android/ project.

    tools/release/make_icon.py [variant ...]      variants: see PALETTES
    tools/release/make_icon.py --final steel_red
    tools/release/make_icon.py --android steel_red
"""
import math, os, random, sys
from PIL import Image, ImageDraw, ImageFilter

SS = 4                     # supersampling
N = 1024 * SS
ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
OUT = os.path.join(ROOT, 'build', 'icon')            # the variants to choose from
FINAL = os.path.join(ROOT, 'assets', 'icon')         # the one the packages use

PALETTES = {
    # tile (two corners), letter base colour, letter edge colour
    'gold':  ((18, 22, 70), (70, 20, 90), (255, 196, 40), (200, 60, 10)),
    'red':   ((10, 10, 18), (40, 40, 60), (235, 40, 40), (120, 0, 20)),
    'steel': ((15, 60, 160), (10, 20, 60), (230, 235, 245), (90, 110, 150)),
    # the game's own scheme: the title's blue, the yellow lettering with its warm red-orange edge
    'title': ((20, 70, 210), (8, 20, 90), (255, 214, 30), (210, 50, 10)),
    'title_deep': ((10, 40, 170), (4, 10, 60), (255, 220, 40), (170, 30, 0)),
    # vf1-recomp's icon is 'title'; VF2's: steel letters on red, or gold on red
    'steel_red': ((190, 20, 30), (60, 0, 12), (232, 236, 246), (70, 85, 120)),
    'gold_red': ((170, 15, 25), (45, 0, 10), (255, 214, 30), (150, 40, 0)),
}

def shade(c, k):
    return tuple(max(0, min(255, int(v * k))) for v in c)

def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def _skew(p, s=0.22):       # italic, leaning forward
    x, y = p
    return (x + (512 - y) * s, y)

_FIT = None
def skew(p):
    """Slanted, then scaled and centred to sit inside the tile with a margin."""
    global _FIT
    if _FIT is None:
        pts = [_skew(q) for st in strokes() for q in st]
        x0 = min(x for x, _ in pts); x1 = max(x for x, _ in pts)
        y0 = min(y for _, y in pts); y1 = max(y for _, y in pts)
        k = min(640 / (x1 - x0), 420 / (y1 - y0))
        _FIT = (k, 512 - (x0 + x1) / 2 * k, 500 - (y0 + y1) / 2 * k)
    k, ox, oy = _FIT
    x, y = _skew(p)
    return (x * k + ox, y * k + oy)

def strokes():
    """The letters as quads (in a 1024 design space): each stroke is chiselled separately."""
    q = []
    # V: two arms meeting at the bottom
    q.append([(120, 300), (215, 300), (300, 640), (250, 740)])
    q.append([(380, 300), (475, 300), (350, 740), (250, 740)][::1])
    # F: stem, top arm, middle arm
    q.append([(500, 300), (585, 300), (585, 740), (500, 740)])
    q.append([(585, 300), (745, 300), (725, 375), (585, 375)])
    q.append([(585, 480), (695, 480), (675, 550), (585, 550)])
    # 2: a hook down at the top's left (so it isn't a Z), the top arm (its corner cut), the right
    # stem's upper half, the diagonal down to the left, the foot
    q.append([(770, 375), (845, 375), (845, 405), (770, 450)])
    q.append([(770, 300), (925, 300), (960, 375), (770, 375)])
    q.append([(880, 375), (960, 375), (960, 470), (880, 500)])
    q.append([(880, 500), (960, 470), (855, 665), (770, 665)])
    q.append([(770, 665), (960, 665), (960, 740), (770, 740)])
    return q
    q.append([(770, 375), (845, 375), (845, 405), (770, 450)])
    q.append([(770, 300), (925, 300), (960, 345), (960, 375)][:0] or [(770, 300), (925, 300), (960, 375), (770, 375)])
    q.append([(880, 375), (960, 375), (960, 470), (880, 500)])
    q.append([(880, 500), (960, 470), (855, 665), (770, 665)])
    q.append([(770, 665), (960, 665), (960, 740), (770, 740)])
    return q

def chisel(d, quad, base, edge, light):
    """A bevelled block: the top face (two triangles) and four sloped edges, each flat-shaded."""
    cx = sum(p[0] for p in quad) / 4
    cy = sum(p[1] for p in quad) / 4
    inner = [(cx + (x - cx) * 0.62, cy + (y - cy) * 0.62) for x, y in quad]
    # the inner face shrinks toward the middle more across the stroke than along it
    faces = []
    for i in range(4):
        a, b = quad[i], quad[(i + 1) % 4]
        ia, ib = inner[i], inner[(i + 1) % 4]
        # the facet's outward direction decides its brightness
        nx, ny = (b[1] - a[1]), -(b[0] - a[0])
        ln = math.hypot(nx, ny) or 1
        lit = (nx * light[0] + ny * light[1]) / ln
        faces.append(([a, b, ib, ia], mix(shade(edge, 0.55), shade(base, 1.15), (lit + 1) / 2)))
    faces.append(([inner[0], inner[1], inner[2]], shade(base, 1.08)))
    faces.append(([inner[0], inner[2], inner[3]], shade(base, 0.92)))
    for poly, col in faces:
        d.polygon([(skew(p)[0] * SS, skew(p)[1] * SS) for p in poly], fill=col)

def tile(img, c1, c2, m=100, radius=185, shadow=True):
    """A rounded tile of flat-shaded triangles, darker toward one corner. The margin, the corner
    radius and the shadow are the desktop icon's by default; Android's adaptive icon wants the
    background filling its whole canvas (m=0, radius=0, no shadow)."""
    mask = Image.new('L', (N, N), 0)
    if radius:
        ImageDraw.Draw(mask).rounded_rectangle((m * SS, m * SS, N - m * SS, N - m * SS), radius=radius * SS, fill=255)
    else:
        ImageDraw.Draw(mask).rectangle((m * SS, m * SS, N - m * SS, N - m * SS), fill=255)
    layer = Image.new('RGB', (N, N))
    d = ImageDraw.Draw(layer)
    rnd = random.Random(7)
    step = 128 * SS
    pts = {}
    for gy in range(0, N + step, step):
        for gx in range(0, N + step, step):
            j = 0 if gx in (0, N) or gy in (0, N) else step * 0.32
            pts[gx, gy] = (gx + rnd.uniform(-j, j), gy + rnd.uniform(-j, j))
    for gy in range(0, N, step):
        for gx in range(0, N, step):
            a, b, c, e = pts[gx, gy], pts[gx + step, gy], pts[gx + step, gy + step], pts[gx, gy + step]
            for tri in ((a, b, c), (a, c, e)):
                cx = sum(p[0] for p in tri) / 3 / N
                cy = sum(p[1] for p in tri) / 3 / N
                col = mix(c1, c2, min(1, max(0, (cx + cy) / 2 + rnd.uniform(-0.08, 0.08))))
                d.polygon(tri, fill=shade(col, rnd.uniform(0.88, 1.12)))
    if shadow:
        # a soft drop shadow under the tile
        shadow_layer = Image.new('L', (N, N), 0)
        ImageDraw.Draw(shadow_layer).rounded_rectangle(
            (m * SS, (m + 14) * SS, N - m * SS, N - m * SS + 14 * SS), radius=(radius or 185) * SS, fill=110)
        shadow_layer = shadow_layer.filter(ImageFilter.GaussianBlur(18 * SS))
        img.paste((0, 0, 0, 255), (0, 0), shadow_layer)
    img.paste(layer, (0, 0), mask)

def letters(img, base, edge):
    """The "VF2" letters on the image (a 1024 design space), each stroke chiselled, with a dark
    outline (the same shapes, grown) for contrast on the tile."""
    d = ImageDraw.Draw(img)
    for q in strokes():
        cx = sum(p[0] for p in q) / 4; cy = sum(p[1] for p in q) / 4
        grown = [(cx + (x - cx) * 1.0 + (12 if x > cx else -12), cy + (y - cy) * 1.0 + (12 if y > cy else -12)) for x, y in q]
        d.polygon([(skew(p)[0] * SS, (skew(p)[1] + 10) * SS) for p in grown], fill=(0, 0, 0, 170))
    for q in strokes():
        chisel(d, q, base, edge, (-0.55, -0.83))

def render(name, tile_on=True, letters_on=True, **tile_kw):
    """The 1024x1024 icon of a variant, its tile, its letters, or either alone (Android's adaptive
    icon is the two layers apart)."""
    c1, c2, base, edge = PALETTES[name]
    img = Image.new('RGBA', (N, N), (0, 0, 0, 0))
    if tile_on:
        tile(img, c1, c2, **tile_kw)
    if letters_on:
        letters(img, base, edge)
    return img.resize((1024, 1024), Image.LANCZOS)

def make(name):
    img = render(name)
    os.makedirs(OUT, exist_ok=True)
    img.save(os.path.join(OUT, f'icon_{name}.png'))
    return img

def final(name):
    """The packages' icons from one variant: icon.png, VF2.icns (every size of the iconset) and VF2.ico."""
    import subprocess, tempfile
    img = make(name)
    os.makedirs(FINAL, exist_ok=True)
    img.save(os.path.join(FINAL, 'icon.png'))
    with tempfile.TemporaryDirectory() as t:
        iconset = os.path.join(t, 'VF2.iconset')
        os.makedirs(iconset)
        for px in (16, 32, 128, 256, 512):
            img.resize((px, px), Image.LANCZOS).save(os.path.join(iconset, f'icon_{px}x{px}.png'))
            img.resize((px * 2, px * 2), Image.LANCZOS).save(os.path.join(iconset, f'icon_{px}x{px}@2x.png'))
        subprocess.run(['iconutil', '-c', 'icns', iconset, '-o', os.path.join(FINAL, 'VF2.icns')], check=True)
    img.save(os.path.join(FINAL, 'VF2.ico'), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])

def android(name):
    """The Android app's icons, from the same variant as the desktop's: the launcher's own (the tile
    and the letters, as the desktop icon) and the adaptive icon's two layers. Its background fills
    the whole 108dp canvas (no margin, no corner, no shadow: the launcher's own shape is the mask
    then) and its foreground is the letters alone at 94% of that canvas, which puts them inside the
    66dp safe zone every launcher's mask keeps: the letters are 62.5% of the design space, so 59%
    of the canvas, 63.4dp of 108.

    Writes android/app/src/main/res/mipmap-*/: ic_launcher.png (48/72/96/144/192 px) and the
    adaptive icon's ic_launcher_foreground.png / ic_launcher_background.png (108dp: 108/162/216/324/
    432 px), which mipmap-anydpi-v26/ic_launcher.xml names.
    """
    res = os.path.join(ROOT, 'android', 'app', 'src', 'main', 'res')
    full = render(name)
    back = render(name, letters_on=False, m=0, radius=0, shadow=False)
    fore = render(name, tile_on=False)
    for density, px in (('mdpi', 48), ('hdpi', 72), ('xhdpi', 96), ('xxhdpi', 144), ('xxxhdpi', 192)):
        dp = int(px * 2.25)     # the adaptive icon's 108dp canvas at this density
        k = int(dp * 0.94)      # the letters at 94% of it (63.4dp of the 108)
        out = os.path.join(res, f'mipmap-{density}')
        os.makedirs(out, exist_ok=True)
        full.resize((px, px), Image.LANCZOS).save(os.path.join(out, 'ic_launcher.png'))
        back.resize((dp, dp), Image.LANCZOS).save(os.path.join(out, 'ic_launcher_background.png'))
        f = fore.resize((k, k), Image.LANCZOS)
        canvas = Image.new('RGBA', (dp, dp), (0, 0, 0, 0))
        canvas.paste(f, ((dp - k) // 2, (dp - k) // 2), f)
        canvas.save(os.path.join(out, 'ic_launcher_foreground.png'))
    print(f'android icons for {name}: {os.path.relpath(res, ROOT)}/mipmap-*')

if __name__ == '__main__':
    if sys.argv[1:2] == ['--final']:
        final(sys.argv[2] if len(sys.argv) > 2 else 'steel_red')
        sys.exit(0)
    if sys.argv[1:2] == ['--android']:
        android(sys.argv[2] if len(sys.argv) > 2 else 'steel_red')
        sys.exit(0)
    names = sys.argv[1:] or list(PALETTES)
    ims = [make(n) for n in names]
    sheet = Image.new('RGB', (360 * len(ims) + 20, 400), (236, 236, 240))
    for i, im in enumerate(ims):
        sheet.paste(im.resize((340, 340), Image.LANCZOS), (20 + i * 360, 30), im.resize((340, 340), Image.LANCZOS))
    sheet.save(os.path.join(OUT, 'choices.png'))
