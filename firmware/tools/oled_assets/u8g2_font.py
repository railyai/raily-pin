"""Draws text from U8g2 font blobs, pixel for pixel as U8g2 would.

The decoder of the concept pack (docs/pins/keyring-oled/concept/tools/oled.py,
class U8Font), moved here so the firmware generator owns it. The blobs in
fonts/ come from U8g2's u8g2_fonts.c (tools/extract_u8g2_fonts.py). Only the
generator uses this: the firmware links no font, it draws the pre-rendered
word strips of oled_assets.h.
"""
import os

FONTDIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fonts')


def _s8(v):
    return v - 256 if v > 127 else v


class U8Font:
    def __init__(self, name):
        self.name = name
        with open(os.path.join(FONTDIR, name + '.bin'), 'rb') as f:
            self.f = f.read()
        f = self.f
        self.bw, self.bh, self.bx, self.by, self.bd = f[4], f[5], f[6], f[7], f[8]
        self.b0, self.b1 = f[2], f[3]
        self.ascent_A, self.descent_g = _s8(f[13]), _s8(f[14])
        self.pos_A, self.pos_a, self.pos_u = (f[17] << 8) | f[18], (f[19] << 8) | f[20], (f[21] << 8) | f[22]
        self.cache = {}

    def _find(self, enc):
        f = self.f
        p = 23
        if enc <= 255:
            if enc >= ord('a'):
                p += self.pos_a
            elif enc >= ord('A'):
                p += self.pos_A
            while True:
                if f[p + 1] == 0:
                    return None
                if f[p] == enc:
                    return p + 2
                p += f[p + 1]
        p += self.pos_u
        lut = p
        while True:
            p += (f[lut] << 8) | f[lut + 1]
            e = (f[lut + 2] << 8) | f[lut + 3]
            lut += 4
            if e >= enc:
                break
        while True:
            e = (f[p] << 8) | f[p + 1]
            if e == 0:
                return None
            if e == enc:
                return p + 3
            p += f[p + 2]

    def glyph(self, ch):
        """(w, h, x, y, dx, bits) of one character, or None when the font lacks it."""
        enc = ord(ch)
        if enc in self.cache:
            return self.cache[enc]
        p = self._find(enc)
        if p is None:
            self.cache[enc] = None
            return None
        f = self.f
        st = {'p': p, 'b': 0}

        def ub(n):
            v = f[st['p']] >> st['b']
            nb = st['b'] + n
            if nb >= 8:
                s = 8 - st['b']
                st['p'] += 1
                v |= f[st['p']] << s
                nb -= 8
                if nb >= 8:
                    st['p'] += 1
                    v |= f[st['p']] << (s + 8)
                    nb -= 8
            st['b'] = nb
            return v & ((1 << n) - 1)

        def sb(n):
            return ub(n) - (1 << (n - 1))

        gw, gh = ub(self.bw), ub(self.bh)
        gx, gy, gd = sb(self.bx), sb(self.by), sb(self.bd)
        bits = []
        if gw > 0:
            total = gw * gh
            while len(bits) < total:
                a, b = ub(self.b0), ub(self.b1)
                while True:
                    bits += [0] * a + [1] * b
                    if ub(1) == 0:
                        break
        g = (gw, gh, gx, gy, gd, bits[:gw * gh])
        self.cache[enc] = g
        return g

    def missing(self, s):
        return [c for c in s if self.glyph(c) is None]

    def width(self, s):
        return sum((self.glyph(c) or (0, 0, 0, 0, 0, []))[4] for c in s)

    def draw(self, fb, x, baseline, s, color=1):
        """Draws s on a Pillow '1' image with its origin at (x, baseline)."""
        px = fb.load()
        w, h = fb.size
        for c in s:
            g = self.glyph(c)
            if g is None:  # as U8g2: a missing glyph draws nothing (word strips assert none is missing)
                continue
            gw, gh, gx, gy, gd, bits = g
            top = baseline - (gh + gy)
            for i, v in enumerate(bits):
                if v:
                    xx, yy = x + gx + i % gw, top + i // gw
                    if 0 <= xx < w and 0 <= yy < h:
                        px[xx, yy] = color
            x += gd
        return x


_fonts = {}


def font(name):
    if name not in _fonts:
        _fonts[name] = U8Font(name)
    return _fonts[name]
