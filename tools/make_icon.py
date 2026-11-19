"""Draws the HOME-menu icon (launcher/icon.jpg, and installer/icon.jpg for the web installer).

Snowy peaks, a tree line, a carved line in the snow and a rider in the air against the sun: no text, no logo and
no picture from anywhere else. Everything is drawn here from shapes, so the icon is this repository's own work.

    python tools/make_icon.py              # writes launcher/icon.jpg and installer/icon.jpg
    python tools/make_icon.py out.jpg ...  # writes the given files instead

Needs Pillow. The icon is drawn at 1024x1024 and saved at 256x256 as a baseline JPEG with 4:2:0 chroma, as the
HOME menu takes it (the limit for a NACP icon is 128 KiB). MIT.
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

S = 1024  # drawn at 4x
SIZE = 256


def hexc(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def vgrad(stops):
    """A vertical gradient over the whole canvas, from [(y, '#rrggbb'), ...]."""
    stops = [(y, hexc(c)) for y, c in stops]
    im = Image.new('RGB', (S, S))
    d = ImageDraw.Draw(im)
    for y in range(S):
        if y <= stops[0][0]:
            c = stops[0][1]
        elif y >= stops[-1][0]:
            c = stops[-1][1]
        else:
            for (y0, c0), (y1, c1) in zip(stops, stops[1:]):
                if y0 <= y <= y1:
                    c = lerp(c0, c1, (y - y0) / (y1 - y0))
                    break
        d.line([(0, y), (S, y)], fill=c)
    return im


def fill(base, poly, stops):
    """Fills a polygon with a vertical gradient."""
    m = Image.new('L', (S, S), 0)
    ImageDraw.Draw(m).polygon(poly, fill=255)
    base.paste(vgrad(stops), (0, 0), m)


def glow(base, cx, cy, r, color, alpha):
    g = Image.new('L', (S, S), 0)
    d = ImageDraw.Draw(g)
    for i in range(r, 0, -4):
        d.ellipse([cx - i, cy - i, cx + i, cy + i], fill=round(alpha * (1 - i / r) ** 1.6))
    base.paste(Image.new('RGB', (S, S), hexc(color)), (0, 0), g.filter(ImageFilter.GaussianBlur(8)))


def bezier(p, n=200):
    out = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        out.append((u**3 * p[0][0] + 3 * u * u * t * p[1][0] + 3 * u * t * t * p[2][0] + t**3 * p[3][0],
                    u**3 * p[0][1] + 3 * u * u * t * p[1][1] + 3 * u * t * t * p[2][1] + t**3 * p[3][1]))
    return out


def ribbon(d, pts, w0, w1, color):
    """A line that widens from w0 to w1 along pts (nearer = wider)."""
    left, right = [], []
    for i, (x, y) in enumerate(pts):
        a = pts[max(i - 1, 0)]
        b = pts[min(i + 1, len(pts) - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        n = math.hypot(dx, dy) or 1
        w = (w0 + (w1 - w0) * i / (len(pts) - 1)) / 2
        left.append((x - dy / n * w, y + dx / n * w))
        right.append((x + dy / n * w, y - dx / n * w))
    d.polygon(left + right[::-1], fill=color)


def rider(d, cx, cy, k, color, board):
    """A snowboarder mid-jump, nose down to the left, in thick strokes."""
    def P(x, y):
        return (cx + x * k, cy + y * k)

    def stroke(pts, w, c=color):
        pts = [P(*p) for p in pts]
        d.line(pts, fill=c, width=round(w * k), joint='curve')
        r = w * k / 2
        for x, y in pts:
            d.ellipse([x - r, y - r, x + r, y + r], fill=c)

    stroke([(-34, 40), (-48, 4), (-8, -16)], 22)       # front leg, knee bent up
    stroke([(30, 27), (38, -8), (-8, -16)], 22)        # back leg
    stroke([(-6, -14), (-28, -70)], 32)                # torso, leaning forward
    stroke([(-26, -64), (-62, -50), (-96, -64)], 14)   # front arm, out
    stroke([(-20, -60), (16, -42), (42, -12)], 14)     # back arm, to the board
    hx, hy = P(-40, -94)
    r = 18 * k
    d.ellipse([hx - r, hy - r, hx + r, hy + r], fill=color)  # head
    stroke([(-82, 50), (78, 16)], 16, board)           # the board, over the feet


def draw():
    im = vgrad([(0, '#0b2552'), (380, '#2c69b8'), (600, '#9cc8ec'), (690, '#e9f2f8')])
    glow(im, 760, 310, 340, '#ffffff', 175)
    d = ImageDraw.Draw(im)

    # far range
    far = [(0, 650), (70, 590), (130, 620), (210, 545), (290, 610), (360, 575), (430, 630), (600, 600),
           (700, 560), (760, 590), (850, 530), (930, 585), (1024, 560), (1024, S), (0, S)]
    fill(im, far, [(530, '#b9cfe8'), (700, '#dce9f6')])

    # second peak, on the right, behind the main one
    p2 = [(600, 560), (700, 440), (760, 395), (810, 330), (860, 380), (920, 420), (1024, 500), (1024, S), (600, S)]
    fill(im, p2, [(330, '#f4f8fd'), (700, '#d3e3f4')])
    p2s = [(810, 330), (860, 380), (920, 420), (1024, 500), (1024, S), (840, S), (830, 520), (845, 430)]
    fill(im, p2s, [(330, '#a9c4e6'), (700, '#86a8d6')])

    # main peak: the lit face, then the shadow face
    apex = (470, 160)
    main = [(-20, 660), (60, 590), (170, 500), (260, 420), (330, 345), (400, 255), apex,
            (540, 235), (590, 300), (660, 360), (740, 430), (860, 520), (1040, 640), (1040, S), (-20, S)]
    fill(im, main, [(160, '#ffffff'), (760, '#dbe8f6')])
    shadow = [apex, (540, 235), (590, 300), (660, 360), (740, 430), (860, 520), (1040, 640), (1040, S),
              (610, S), (585, 760), (560, 640), (528, 540), (540, 450), (505, 360), (515, 270)]
    fill(im, shadow, [(160, '#a3c0e6'), (760, '#7a9dd0')])
    for g in ([(540, 235), (562, 330), (548, 420), (556, 330)],      # gullies on the shadow side
              [(590, 300), (615, 400), (600, 470), (606, 395)],
              [(660, 360), (690, 450), (676, 520), (680, 445)],
              [(740, 430), (772, 520), (756, 590), (762, 515)],
              [(860, 520), (880, 600), (868, 650), (872, 598)]):
        d.polygon(g, fill=hexc('#6e91c6'))
    for crease in ([(400, 255), (380, 330), (350, 400), (362, 330)],   # creases on the lit side
                   [(330, 345), (300, 430), (262, 490), (290, 420)]):
        d.polygon(crease, fill=hexc('#c9dbef'))

    # tree line along the near ridge
    def ridge(x):
        return 760 - 110 * (max(x, 0) / S) ** 1.4 + 18 * math.sin(x / 70)
    fill(im, [(x, ridge(x)) for x in range(0, S + 1, 8)] + [(S, S), (0, S)], [(620, '#2a5276'), (900, '#1c3a57')])
    rnd = random.Random(7)
    for x in range(-10, S + 20, 22):
        h = rnd.randint(46, 80)
        y = ridge(x) + rnd.randint(-4, 10)
        w = h * 0.36
        d.polygon([(x, y - h), (x + w, y), (x - w, y)], fill=hexc('#163049'))

    # near snow slope and a carved line in it
    def slope(x):
        return 880 - 190 * (x / S) ** 1.25
    fill(im, [(x, slope(x)) for x in range(0, S + 1, 8)] + [(S, S), (0, S)], [(690, '#ffffff'), (1024, '#d2e3f5')])
    trail = bezier([(990, 712), (760, 780), (860, 900), (330, 1060)])
    ribbon(d, trail, 6, 44, hexc('#8fb6e3'))
    ribbon(d, [(x - 4, y - 6) for x, y in trail], 2, 12, hexc('#ffffff'))

    # a rider in the air, against the sun
    rider(d, 755, 330, 1.3, hexc('#0e1f38'), hexc('#f26b1d'))
    return im.resize((SIZE, SIZE), Image.LANCZOS)


def main(argv):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    outs = argv or [os.path.join(root, 'launcher', 'icon.jpg'), os.path.join(root, 'installer', 'icon.jpg')]
    img = draw()
    for out in outs:
        img.save(out, 'JPEG', quality=92, progressive=False, subsampling=2)
        size = os.path.getsize(out)
        if size > 128 * 1024:
            sys.exit(f'{out}: {size} bytes, over the 128 KiB a NACP icon may have')
        print(f'{os.path.relpath(out, root)}: {SIZE}x{SIZE}, {size} bytes')


if __name__ == '__main__':
    main(sys.argv[1:])
