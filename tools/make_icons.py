"""Иконки программы установки по умолчанию (src/setup/setup.ico, uninst.ico).

Иконки описаны векторно в координатах 0..1 и растеризуются со сглаживанием;
результат — ICO с размерами 16, 24, 32, 48 (BMP) и 256 (PNG), 32 бита.

    python tools/make_icons.py [директория для предпросмотра]
"""
import math
import os
import struct
import sys
import zlib

SIZES = [16, 24, 32, 48, 256]


def hexc(s, a=255):
    s = s.lstrip('#')
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16), a)


def lerp(c1, c2, t):
    t = max(0.0, min(1.0, t))
    return tuple(c1[i] + (c2[i] - c1[i]) * t for i in range(4))


class Shape:
    def __init__(self, fill, stroke=None, width=0.0):
        self.fill = fill            # цвет или функция (x, y) -> цвет
        self.stroke = stroke
        self.width = width

    def color(self, x, y):
        return self.fill(x, y) if callable(self.fill) else self.fill


class Polygon(Shape):
    def __init__(self, pts, fill, stroke=None, width=0.0):
        super().__init__(fill, stroke, width)
        self.pts = pts
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        w = width
        self.box = (min(xs) - w, min(ys) - w, max(xs) + w, max(ys) + w)

    def inside(self, x, y):
        c = False
        n = len(self.pts)
        for i in range(n):
            x1, y1 = self.pts[i]
            x2, y2 = self.pts[(i + 1) % n]
            if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
                c = not c
        return c

    def edge(self, x, y):
        best = 9.0
        n = len(self.pts)
        for i in range(n):
            x1, y1 = self.pts[i]
            x2, y2 = self.pts[(i + 1) % n]
            dx, dy = x2 - x1, y2 - y1
            t = ((x - x1) * dx + (y - y1) * dy) / (dx * dx + dy * dy)
            t = max(0.0, min(1.0, t))
            px, py = x1 + t * dx - x, y1 + t * dy - y
            best = min(best, px * px + py * py)
        return math.sqrt(best)

    def sample(self, x, y):
        if not (self.box[0] <= x <= self.box[2] and self.box[1] <= y <= self.box[3]):
            return None
        ins = self.inside(x, y)
        if self.stroke and self.width and self.edge(x, y) < self.width / 2:
            return self.stroke
        return self.color(x, y) if ins else None


class Ellipse(Shape):
    def __init__(self, cx, cy, rx, ry, fill, stroke=None, width=0.0, hole=0.0):
        super().__init__(fill, stroke, width)
        self.cx, self.cy, self.rx, self.ry, self.hole = cx, cy, rx, ry, hole
        w = width
        self.box = (cx - rx - w, cy - ry - w, cx + rx + w, cy + ry + w)

    def sample(self, x, y):
        if not (self.box[0] <= x <= self.box[2] and self.box[1] <= y <= self.box[3]):
            return None
        nx, ny = (x - self.cx) / self.rx, (y - self.cy) / self.ry
        d = math.sqrt(nx * nx + ny * ny)
        r = min(self.rx, self.ry)
        if self.stroke and self.width and abs(d - 1) * r < self.width / 2:
            return self.stroke
        if self.hole and d < self.hole:
            if self.stroke and self.width and abs(d - self.hole) * r < self.width / 2:
                return self.stroke
            return None
        return self.color(x, y) if d <= 1 else None


def render(shapes, size, ss):
    """Растр size x size: список строк RGBA (0..255)."""
    rows = []
    step = 1.0 / (size * ss)
    for py in range(size):
        row = []
        for px in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                y = (py * ss + sy + 0.5) * step
                for sx in range(ss):
                    x = (px * ss + sx + 0.5) * step
                    for sh in reversed(shapes):
                        c = sh.sample(x, y)
                        if c is not None:
                            a = c[3] / 255.0
                            acc[0] += c[0] * a
                            acc[1] += c[1] * a
                            acc[2] += c[2] * a
                            acc[3] += a
                            break
            n = ss * ss
            a = acc[3] / n
            if a > 0:
                row.append((round(acc[0] / acc[3]), round(acc[1] / acc[3]), round(acc[2] / acc[3]), round(a * 255)))
            else:
                row.append((0, 0, 0, 0))
        rows.append(row)
    return rows


# ---------------------------------------------------------------------------
# Рисунок: открытая картонная коробка, в которую опускается компакт-диск.
# ---------------------------------------------------------------------------

def box_shapes(stroke_w):
    outline = hexc('#5A3A14')
    front_top, front_bot = hexc('#EDBE76'), hexc('#C88F45')
    side_top, side_bot = hexc('#BE843D'), hexc('#8F5B22')
    sw = stroke_w

    def grad(c1, c2, y1, y2):
        return lambda x, y: lerp(c1, c2, (y - y1) / (y2 - y1))

    def cd_fill(x, y):
        # Металлический диск с радужным отливом по углу.
        ang = math.atan2(y - 0.34, x - 0.50)
        base = lerp(hexc('#F4F6F9'), hexc('#AEB7C2'), (y - 0.08) / 0.52)
        hue = (ang / (2 * math.pi)) % 1.0
        r = 0.5 + 0.5 * math.cos(2 * math.pi * hue)
        g = 0.5 + 0.5 * math.cos(2 * math.pi * (hue - 1 / 3))
        b = 0.5 + 0.5 * math.cos(2 * math.pi * (hue - 2 / 3))
        tint = (155 + 100 * r, 155 + 100 * g, 155 + 100 * b, 255)
        k = 0.28 * max(0.0, math.cos(2 * ang))
        return lerp(base, tint, k)

    return [
        # Створки сзади
        Polygon([(0.40, 0.40), (0.88, 0.50), (0.93, 0.30), (0.47, 0.20)], hexc('#E7B76E'), outline, sw),
        Polygon([(0.12, 0.49), (0.40, 0.40), (0.29, 0.25), (0.03, 0.33)], hexc('#D9A55B'), outline, sw),
        # Внутренность коробки
        Polygon([(0.12, 0.49), (0.40, 0.40), (0.88, 0.50), (0.62, 0.60)], hexc('#5C3B17'), outline, sw),
        # Диск
        Ellipse(0.50, 0.34, 0.27, 0.27, cd_fill, hexc('#6E7884'), sw, hole=0.36),
        Ellipse(0.50, 0.34, 0.27 * 0.36, 0.27 * 0.36, hexc('#E4E9EF'), hexc('#8C96A1'), sw * 0.6, hole=0.42),
        # Передняя и боковая стенки
        Polygon([(0.12, 0.49), (0.62, 0.60), (0.62, 0.95), (0.12, 0.83)],
                grad(front_top, front_bot, 0.49, 0.95), outline, sw),
        Polygon([(0.62, 0.60), (0.88, 0.50), (0.88, 0.83), (0.62, 0.95)],
                grad(side_top, side_bot, 0.50, 0.95), outline, sw),
        # Полоса скотча на передней стенке
        Polygon([(0.33, 0.54), (0.41, 0.556), (0.41, 0.90), (0.33, 0.88)], hexc('#F6DDA8'), None, 0),
    ]


def badge_shapes(stroke_w):
    sw = stroke_w
    cx, cy, r = 0.74, 0.74, 0.235
    red = lambda x, y: lerp(hexc('#F0563F'), hexc('#B0180C'), (y - (cy - r)) / (2 * r))
    shapes = [Ellipse(cx, cy, r, r, red, hexc('#6E0B03'), sw)]
    # Белый крест из двух повёрнутых прямоугольников.
    L, T = r * 0.55, r * 0.17
    for a in (math.pi / 4, -math.pi / 4):
        ca, sa = math.cos(a), math.sin(a)
        pts = [(cx + ca * dx - sa * dy, cy + sa * dx + ca * dy) for dx, dy in ((-L, -T), (L, -T), (L, T), (-L, T))]
        shapes.append(Polygon(pts, hexc('#FFFFFF')))
    return shapes


def icon_images(uninstall):
    imgs = {}
    for size in SIZES:
        # Обводка — около одного пикселя на малых размерах.
        sw = max(1.0 / size, 0.012) * (1.15 if size <= 24 else 1.0)
        shapes = box_shapes(sw)
        if uninstall:
            shapes += badge_shapes(sw)
        ss = 3 if size == 256 else 6
        imgs[size] = render(shapes, size, ss)
    return imgs


# ---------------------------------------------------------------------------
# Запись PNG и ICO
# ---------------------------------------------------------------------------

def png_bytes(rows):
    h, w = len(rows), len(rows[0])
    raw = b''.join(b'\x00' + bytes(v for px in row for v in px) for row in rows)

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def dib_bytes(rows):
    h, w = len(rows), len(rows[0])
    hdr = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    pix = b''.join(bytes((b, g, r, a)) for row in reversed(rows) for (r, g, b, a) in row)
    mask_row = ((w + 31) // 32) * 4
    return hdr + pix + b'\x00' * (mask_row * h)


def write_ico(path, imgs):
    datas = [(s, png_bytes(imgs[s]) if s >= 256 else dib_bytes(imgs[s])) for s in sorted(imgs)]
    out = struct.pack('<HHH', 0, 1, len(datas))
    offset = 6 + 16 * len(datas)
    body = b''
    for s, d in datas:
        out += struct.pack('<BBBBHHII', s % 256, s % 256, 0, 0, 1, 32, len(d), offset + len(body))
        body += d
    open(path, 'wb').write(out + body)


def preview(path, sets):
    """Все размеры обеих иконок на светлом и тёмном фоне (малые увеличены в 4 раза)."""
    scale = {16: 4, 24: 4, 32: 4, 48: 2, 256: 1}
    pad = 16
    cell = 256 + pad
    width = pad + cell * len(SIZES) + cell
    height = (cell * 2) * len(sets) + pad
    canvas = [[(0, 0, 0, 255)] * width for _ in range(height)]
    for si, imgs in enumerate(sets):
        for bi, bg in enumerate(((240, 240, 240), (40, 44, 52))):
            oy = pad + (si * 2 + bi) * cell
            for y in range(oy - pad // 2, oy + 256 + pad // 2):
                for x in range(width):
                    canvas[y][x] = bg + (255,)
            ox = pad
            for s in SIZES + [32]:
                k = scale[s] if ox < pad + cell * len(SIZES) else 1
                img = imgs[s]
                for y in range(s * k):
                    for x in range(s * k):
                        r, g, b, a = img[y // k][x // k]
                        cy, cx = oy + y, ox + x
                        br = canvas[cy][cx]
                        t = a / 255
                        canvas[cy][cx] = (round(r * t + br[0] * (1 - t)), round(g * t + br[1] * (1 - t)),
                                          round(b * t + br[2] * (1 - t)), 255)
                ox += cell
    open(path, 'wb').write(png_bytes(canvas))


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    setup = icon_images(False)
    uninst = icon_images(True)
    write_ico(os.path.join(root, 'src', 'setup', 'setup.ico'), setup)
    write_ico(os.path.join(root, 'src', 'setup', 'uninst.ico'), uninst)
    if len(sys.argv) > 1:
        preview(os.path.join(sys.argv[1], 'icons_preview.png'), [setup, uninst])


main()
