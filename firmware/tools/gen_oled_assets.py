#!/usr/bin/env python3
"""Builds the keyring OLED assets: RailyPinsP1/oled_assets.h and golden frames.

    python3 hardware/firmware/tools/gen_oled_assets.py [--header PATH] [--golden DIR] [--words]

Without options it rewrites the checked-in header and
hardware/firmware/tests/oled_golden/. The output is deterministic for a
given Pillow (the reference is PILLOW_REFERENCE); check_oled_assets.py
regenerates into a temp dir and fails on any difference. --words prints
the word table (every word strip per locale with its pixel width).

What goes in the header (docs/pins/keyring-oled/implementation-plan.md §2):
- the six filled bodies (48 x 48) from the site's SVG shapes, plain and
  at every scale a scene uses (the press squash, the fall's squash and
  stretch), as row spans: a 48 x 48 bitmap would be 288 B, a filled
  shape's spans are about half that;
- the eye and mouth sprites and, per shape and body, where each sits;
- every glyph of the scenes, cut into connected pieces and de-duplicated
  (radar arcs repeat between frames);
- the word strips: every scene word in ru, en, es and pt, drawn from U8g2
  font blobs (tools/oled_assets/fonts/), no font linked;
- the digit glyphs 0-9 and «+» of two U8g2 fonts and the number slots:
  the one thing drawn at run time is a count (1-99), at a frame's slot;
- the scene table: frames, timing, pose, mouth, offsets, glyph placements,
  each frame's word and number slot.

This file also holds the reference composer: the golden frames are drawn
from the same tables by independent Python, and tests/test_oled_compose.cpp
must reproduce them bit for bit.
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
from oled_assets import pixels as P  # noqa: E402
from oled_assets import scenes as SC  # noqa: E402
from oled_assets import shapes as S  # noqa: E402
from oled_assets.u8g2_font import font  # noqa: E402

FIRMWARE = os.path.dirname(HERE)
HEADER = os.path.join(FIRMWARE, 'RailyPinsP1', 'oled_assets.h')
GOLDEN = os.path.join(FIRMWARE, 'tests', 'oled_golden')
PILLOW_REFERENCE = '11.3.0'
ASSET_BUDGET = 24 * 1024   # raised from 16 KB with the counts (lead, 2026-09-29): const tables live in flash, 600 KB free
MATERIALS = ('satin', 'jelly', 'glass')
BODY = 48
VARIANTS = tuple(SC.body_scales())          # 0 = plain, 1 = the press squash, then the fall's
EYE_SPRITES = ('open', 'blink', 'closed', 'wide', 'happy', 'squint_l', 'squint_r', 'flat', 'half', 'tilt_l',
               'tilt_r')
MOUTH_SPRITES = ('o_small', 'o_mid', 'o_big', 'line', 'wavy')   # frame mouth 1-5; 0 = none
GLYPH_MARGIN = 32
FLAG_BITS = {'loop': 1, 'glass': 2, 'default_look': 4, 'blank': 8, 'still': 16}
PLACE_SHIFTED, PLACE_ZONE, PLACE_ICONS, PLACE_ONE_DIGIT = 1, 2, 4, 8
NUMBER_PLUS = 1                    # a number slot writes «+» before the count
NO_NUMBER = 0xFF
DIGIT_CHARS = '0123456789+'
COUNT_MAX = 99
FRAME_NO_STATUS = 1
WORD_BUBBLE = 1
NO_WORD = 0xFF
WAKE_SHIFTS = ((1, 0), (-1, 1), (2, -1), (-2, 2), (0, -2), (1, 2), (-1, -1), (2, 1))
LOCALE_NAMES = ('none',) + SC.WORD_LOCALES + ('ar',)   # screen_state byte 8


# ------------------------------------------------------------------ tables
def _image_rows(im):
    px = im.load()
    return [[1 if px[x, y] else 0 for x in range(im.size[0])] for y in range(im.size[1])]


def _components(canvas):
    """Splits a drawing into 8-connected pieces: (x, y, rows) each, in scan order."""
    w, h = len(canvas[0]), len(canvas)
    seen = [[False] * w for _ in range(h)]
    out = []
    for y in range(h):
        for x in range(w):
            if not canvas[y][x] or seen[y][x]:
                continue
            stack, pts = [(x, y)], []
            seen[y][x] = True
            while stack:
                cx, cy = stack.pop()
                pts.append((cx, cy))
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < w and 0 <= ny < h and canvas[ny][nx] and not seen[ny][nx]:
                            seen[ny][nx] = True
                            stack.append((nx, ny))
            x0, y0 = min(p[0] for p in pts), min(p[1] for p in pts)
            x1, y1 = max(p[0] for p in pts), max(p[1] for p in pts)
            rows = [[0] * (x1 - x0 + 1) for _ in range(y1 - y0 + 1)]
            for px_, py_ in pts:
                rows[py_ - y0][px_ - x0] = 1
            out.append((x0, y0, rows))
    return out


class Sprites:
    def __init__(self):
        self.items, self.index = [], {}

    def add(self, name, rows, fixed=False):
        """A sprite id; equal bitmaps share one, except `fixed` ones (the eye
        and mouth tables index those by position: the «line» mouth and the
        «blink» eye are the same four pixels)."""
        key = tuple(tuple(r) for r in rows)
        if fixed or key not in self.index:
            self.index.setdefault(key, len(self.items))
            self.items.append((name, rows))
            return len(self.items) - 1
        return self.index[key]


def _eye_slot(shape, sy):
    """The pack's eye placement (oled.py eye_anchor/put_c) for one body."""
    k = BODY / 64.0
    ey = S.EYES_Y_HOLLOW if shape in S.HOLLOW else S.EYES_Y
    cy = (32 + (ey - 32) * sy) * k
    slots = []
    for cx in (25 * k, 39 * k):            # pixel tuning: the site's 27/37 is too tight for 4 px eyes
        patch = Image.new('1', (BODY, BODY), 0)
        ImageDraw.Draw(patch).rectangle([cx - 3, cy - 4, cx + 3, cy + 4], fill=1)
        x0, y0, x1, y1 = patch.getbbox()
        tl = []
        for name in EYE_SPRITES:
            rows = P.bits(P.EYES[name])
            tl.append((int(round(cx - len(rows[0]) / 2)), int(round(cy - len(rows) / 2))))
        slots.append(dict(probe=(int(cx), int(cy)), patch=(x0, y0, x1 - 1, y1 - 1), tl=tl))
    return slots


def _mouth_slot(shape, sy):
    """The pack's mouth placement (oled.py mascot(): between the eyes, 7 px
    below them, or 5 px on the hollow shapes whose eyes sit on the rim)."""
    k = BODY / 64.0
    ey = S.EYES_Y_HOLLOW if shape in S.HOLLOW else S.EYES_Y
    cy = (32 + (ey - 32) * sy) * k
    mx = (25 * k + 39 * k) / 2
    my = cy + (5 if shape in S.HOLLOW else 7)
    probe = (int(min(max(mx, 0), BODY - 1)), int(min(max(my, 0), BODY - 1)))
    tl = []
    for name in MOUTH_SPRITES:
        rows = P.bits(P.MOUTHS[name])
        tl.append((int(round(mx - len(rows[0]) / 2)), int(round(my - len(rows) / 2))))
    return dict(probe=probe, tl=tl)


def word_strip(lines, locale):
    """One word in one locale as (rows, x, y, font, widths): centred in the
    64 px row at the layout A baselines, cropped to its ink. Fails when a
    character is missing or a line does not fit WORD_MAX_WIDTH."""
    for fname in SC.WORD_FONTS[locale]:
        f = font(fname)
        missing = [c for line in lines for c in f.missing(line)]
        if missing:
            raise SystemExit('gen_oled_assets.py: %s has no glyph for %r (%s)' % (fname, ''.join(missing), locale))
        widths = [f.width(line) for line in lines]
        if max(widths) <= SC.WORD_MAX_WIDTH:
            break
    else:
        raise SystemExit('gen_oled_assets.py: %r (%s) is %d px, the word row fits %d' % (
            ' / '.join(lines), locale, max(widths), SC.WORD_MAX_WIDTH))
    if len(lines) > len(SC.WORD_BASELINES):
        raise SystemExit('gen_oled_assets.py: %r has more than %d lines' % (lines, len(SC.WORD_BASELINES)))
    fb = Image.new('1', (SC.W, SC.H), 0)
    for i, line in enumerate(lines):
        f.draw(fb, int(round(SC.W / 2 - widths[i] / 2)), SC.WORD_BASELINES[i], line)
    x0, y0, x1, y1 = fb.getbbox()
    shift = max(abs(s[0]) for s in WAKE_SHIFTS)
    if x0 - shift < 0 or x1 - 1 + shift > SC.W - 1 or y1 > SC.H:
        raise SystemExit('gen_oled_assets.py: %r (%s) would leave the screen at a wake shift' % (lines, locale))
    rows = _image_rows(fb.crop((x0, y0, x1, y1)))
    return rows, x0, y0, fname, widths


def bubble_strip(lines, locale, dy):
    """A speech-bubble word (the fall's «ой»): one line in the small font,
    left-aligned where the pack writes it. Fails when a character is missing
    or the ink leaves the bubble's inside."""
    fname = SC.WORD_FONTS[locale][-1]
    f = font(fname)
    missing = [c for line in lines for c in f.missing(line)]
    if missing:
        raise SystemExit('gen_oled_assets.py: %s has no glyph for %r (%s)' % (fname, ''.join(missing), locale))
    if len(lines) != 1:
        raise SystemExit('gen_oled_assets.py: bubble word %r must be one line' % (lines,))
    x, base = SC.bubble_text_at(dy)
    fb = Image.new('1', (SC.W, SC.H), 0)
    f.draw(fb, x, base, lines[0])
    ix0, iy0, ix1, iy1 = fb.getbbox()
    bx0, by0, bx1, by1 = SC.bubble_box(dy)
    if not (bx0 < ix0 and ix1 - 1 < bx1 and by0 < iy0 and iy1 - 1 < by1):
        raise SystemExit('gen_oled_assets.py: %r (%s) leaves the bubble' % (lines[0], locale))
    return _image_rows(fb.crop((ix0, iy0, ix1, iy1))), ix0, iy0, fname, [f.width(lines[0])]


def body_spans(rows):
    """A body as row spans: per row a count, then x0, x1 (inclusive) per run."""
    out = []
    for row in rows:
        runs, x = [], 0
        while x < len(row):
            if row[x]:
                x0 = x
                while x < len(row) and row[x]:
                    x += 1
                runs.append((x0, x - 1))
            else:
                x += 1
        out.append(len(runs))
        for x0, x1 in runs:
            out.extend((x0, x1))
    return out


def build():
    sprites = Sprites()
    for name in EYE_SPRITES:                # eye sprites first: the eye tables index them
        sprites.add('eye_' + name, P.bits(P.EYES[name]), fixed=True)
    for name in MOUTH_SPRITES:              # then the mouths, at OLED_MOUTH_SPRITE_BASE
        sprites.add('mouth_' + name, P.bits(P.MOUTHS[name]), fixed=True)
    bodies, eye_slots, mouth_slots = [], [], []
    for shape in S.SHAPES:
        bodies.append([_image_rows(S.body_mask(shape, BODY, sx, sy)) for sx, sy in VARIANTS])
        eye_slots.append([_eye_slot(shape, sy) for _, sy in VARIANTS])
        mouth_slots.append([_mouth_slot(shape, sy) for _, sy in VARIANTS])
    pose_names = list(P.POSES)
    poses = [(name, EYE_SPRITES.index(P.POSES[name][0]), EYE_SPRITES.index(P.POSES[name][1]),
              P.POSES[name][2], P.POSES[name][3]) for name in pose_names]
    for _, _, _, lx, ly in poses:
        assert lx % 2 == 0 and ly % 2 == 0, 'look offsets must stay even'
    placements = []

    def place(groups, name):
        # A margin past the right and bottom edges keeps a glyph that runs
        # off the screen whole; the composer clips it. A `whole` group is
        # one sprite (its bounding box), not one per connected piece.
        first = len(placements)
        for draws, flags, whole in groups:
            if not draws:
                continue
            fb = Image.new('1', (SC.W + GLYPH_MARGIN, SC.H + GLYPH_MARGIN), 0)
            for draw in draws:
                draw(fb)
            if whole:
                box = fb.getbbox()
                pieces = [(box[0], box[1], _image_rows(fb.crop(box)))] if box else []
            else:
                pieces = _components(_image_rows(fb))
            for x, y, rows in pieces:
                placements.append((sprites.add('%s.%d' % (name, len(placements) - first + 1), rows), x, y, flags))
        return first, len(placements) - first

    status = [place([([SC.status(False)], 0, False)], 'status_unlinked'),
              place([([SC.status(True)], 0, False)], 'status_linked'),
              place([([SC.status_dot], 0, False)], 'status_dot')]
    word_names = list(SC.WORDS)
    frames, scenes, numbers = [], [], []
    digits = digit_glyphs(sprites)
    for sc in SC.SCENES:
        first_frame = len(frames)
        for i, f in enumerate(sc['frames']):
            first, count = place([(f['glyphs'], PLACE_SHIFTED | PLACE_ZONE, False),
                                  (f['narrow'], PLACE_SHIFTED | PLACE_ZONE | PLACE_ONE_DIGIT, False),
                                  (f['overlays'], PLACE_SHIFTED, False),
                                  (f['whole'], PLACE_SHIFTED, True),
                                  (f['icons'], PLACE_SHIFTED | PLACE_ICONS, False),
                                  (f['marks'], 0, False)],
                                 '%s_f%d' % (sc['name'], i + 1))
            number = NO_NUMBER
            if f['number'] is not None:
                slot = dict(f['number'])
                if slot not in numbers:
                    numbers.append(slot)
                number = numbers.index(slot)
            mouth = MOUTH_SPRITES.index(f['mouth']) + 1 if f['mouth'] else 0
            word = sc['word'] if f['word'] == SC.SCENE_WORD else f['word']
            if word in SC.BUBBLE_WORDS and SC.BUBBLE_WORDS[word] != f['dy']:
                raise SystemExit('gen_oled_assets.py: bubble word %s is drawn for dy %d, frame %s f%d has %d' % (
                    word, SC.BUBBLE_WORDS[word], sc['name'], i + 1, f['dy']))
            frames.append(dict(ms=f['ms'], pose=pose_names.index(f['pose']), variant=VARIANTS.index(f['scale']),
                               dx=f['dx'], dy=f['dy'], mouth=mouth, first=first, count=count,
                               word=word_names.index(word) if word else NO_WORD,
                               flags=0 if f['status'] else FRAME_NO_STATUS, number=number))
        flags = 0
        for flag in sc['flags']:
            flags |= FLAG_BITS[flag]
        scenes.append(dict(name=sc['name'], first=first_frame, count=len(sc['frames']), flags=flags,
                           word=word_names.index(sc['word']) if sc['word'] else NO_WORD))
    words = []
    for name, texts in SC.WORDS.items():
        strips = []
        for locale in SC.WORD_LOCALES:
            if name in SC.BUBBLE_WORDS:
                rows, x, y, fname, widths = bubble_strip(texts[locale], locale, SC.BUBBLE_WORDS[name])
            else:
                rows, x, y, fname, widths = word_strip(texts[locale], locale)
            strips.append(dict(sprite=sprites.add('word_%s_%s' % (name, locale), rows), x=x, y=y, font=fname,
                               lines=texts[locale], widths=widths))
        words.append(dict(name=name, strips=strips, flags=WORD_BUBBLE if name in SC.BUBBLE_WORDS else 0))
    for _, x, y, _ in placements:
        assert 0 <= x < SC.W + GLYPH_MARGIN and 0 <= y < SC.H + GLYPH_MARGIN
    assert len(numbers) < NO_NUMBER, 'number slots are uint8_t below OLED_NO_NUMBER'
    number_errors(numbers, digits, placements, frames, sprites)
    assert len(sprites.items) <= 255, 'sprite ids are uint8_t'
    assert len(frames) <= 255, 'frame indexes are uint8_t'
    assert len(words) < NO_WORD, 'word ids are uint8_t below OLED_NO_WORD'
    assert set(SC.DEMO_LETTERS) == {sc['name'] for sc in SC.SCENES}, 'every scene needs a demo letter'
    letters = list(SC.DEMO_LETTERS.values()) + [SC.DEMO_MODEST_LETTER]
    assert len(set(letters)) == len(letters), 'demo letters must be unique'
    return dict(sprites=sprites.items, bodies=bodies, eye_slots=eye_slots, mouth_slots=mouth_slots, poses=poses,
                placements=placements, status=status, frames=frames, scenes=scenes, words=words, numbers=numbers,
                digits=digits)


def digit_glyphs(sprites):
    """Per number font: the pen advance and, per character of DIGIT_CHARS,
    the sprite cropped to its ink and the ink's offset from the pen and
    the baseline. The fonts are monospaced (logisoso *_tn)."""
    out = []
    for fname in SC.NUMBER_FONTS:
        f = font(fname)
        advances = {f.width(c) for c in DIGIT_CHARS}
        assert len(advances) == 1, '%s must be monospaced' % fname
        glyphs = []
        for c in DIGIT_CHARS:
            fb = Image.new('1', (64, 64), 0)
            f.draw(fb, 8, 48, c)
            x0, y0, x1, y1 = fb.getbbox()
            rows = _image_rows(fb.crop((x0, y0, x1, y1)))
            glyphs.append(dict(sprite=sprites.add('digit_%s_%s' % (fname[10:], 'plus' if c == '+' else c), rows),
                               dx=x0 - 8, dy=y0 - 48))
        out.append(dict(font=fname, advance=advances.pop(), glyphs=glyphs))
    return out


def number_text(slot, count):
    return ('+' if slot['plus'] else '') + str(count)


def number_pieces(data_digits, slot, count):
    """(sprite, x, y) of a count at a slot, wake shift 0, no word."""
    font_ = data_digits[slot['font']]
    pen = slot['x'] if count < 10 else slot['x2']
    out = []
    for c in number_text(slot, count):
        g = font_['glyphs'][DIGIT_CHARS.index(c)]
        out.append((g['sprite'], pen + g['dx'], slot['base'] + g['dy']))
        pen += font_['advance']
    return out


def number_errors(numbers, digits, placements, frames, sprites):
    """Every count 1-99 at every slot stays on the screen at any wake
    shift, with or without a word, and a two-digit count never touches a
    piece the frame keeps (the narrow pieces give way)."""
    shift = max(max(abs(a), abs(b)) for a, b in WAKE_SHIFTS)
    for f in frames:
        if f['number'] == NO_NUMBER:
            continue
        slot = numbers[f['number']]
        kept = [p for p in placements[f['first']:f['first'] + f['count']] if p[3] & PLACE_ZONE
                and not p[3] & PLACE_ONE_DIGIT]
        for count in (1, 9, 10, COUNT_MAX):
            for sprite, x, y in number_pieces(digits, slot, count):
                rows = sprites.items[sprite][1]
                w, h = len(rows[0]), len(rows)
                if x - shift < 0 or x + w - 1 + shift > SC.W - 1:
                    raise SystemExit('gen_oled_assets.py: count %d leaves the screen across' % count)
                if count >= 10:
                    for ps, px, py, _ in kept:
                        prow = sprites.items[ps][1]
                        pw, ph = len(prow[0]), len(prow)
                        inside = [(i, j) for j in range(ph) for i in range(pw) if prow[j][i]
                                  and 0 <= px + i - x < w and 0 <= py + j - y < h]
                        # the card outline surrounds the digits: only lit pixel overlaps count
                        if any(rows[py + j - y][px + i - x] for i, j in inside):
                            raise SystemExit('gen_oled_assets.py: count %d overlaps a kept piece' % count)


def timing_errors(data):
    """Frame times the pin divides by: a looping scene of 0 ms would be a
    division by zero in screen_state.h (it guards, but the table must not
    need it), and every time must fit the header's uint16."""
    errors = []
    for sc in data['scenes']:
        frames = data['frames'][sc['first']:sc['first'] + sc['count']]
        if not 1 <= sc['count'] <= 255:
            errors.append('scene %s has %d frames (1-255)' % (sc['name'], sc['count']))
        for i, f in enumerate(frames):
            if not 1 <= f['ms'] <= 0xFFFF:
                errors.append('scene %s frame %d lasts %d ms (1-65535)' % (sc['name'], i + 1, f['ms']))
        if sc['flags'] & FLAG_BITS['loop'] and sum(f['ms'] for f in frames) == 0:
            errors.append('looping scene %s lasts 0 ms' % sc['name'])
    return errors


# ------------------------------------------------------------------ reference composer
def _material(mask, material):
    """satin: filled, a 1 px engraved ring on the upper-left half; jelly: 50 %
    dither inside a solid rim; glass: rim only. Pixels off the mask count as
    empty, except past the 48 px box in the satin erosion: there they are
    ignored, as by the pack's MinFilter (only the fall's widest squash
    reaches the box edge)."""
    n = BODY

    def at(m, x, y, outside=0):
        return m[y][x] if 0 <= x < n and 0 <= y < n else outside

    def erode(m):
        return [[1 if all(at(m, x + i, y + j, 1) for j in (-1, 0, 1) for i in (-1, 0, 1)) else 0
                 for x in range(n)] for y in range(n)]

    if material == 'satin':
        e1 = erode(mask)
        e2 = erode(e1)
        return [[0 if (e1[y][x] and not e2[y][x] and x + y <= 40) else mask[y][x] for x in range(n)] for y in range(n)]
    out = [[0] * n for _ in range(n)]
    for y in range(n):
        for x in range(n):
            if not mask[y][x]:
                continue
            edge = not (at(mask, x - 1, y) and at(mask, x + 1, y) and at(mask, x, y - 1) and at(mask, x, y + 1))
            if edge or (material == 'jelly' and (x + y) % 2 == 0):
                out[y][x] = 1
    return out


def _blit(dst, rows, x, y, color=1):
    for j, row in enumerate(rows):
        for i, v in enumerate(row):
            if v and 0 <= y + j < len(dst) and 0 <= x + i < len(dst[0]):
                dst[y + j][x + i] = color


def _fill(dst, x0, y0, x1, y1):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if 0 <= x < BODY and 0 <= y < BODY:
                dst[y][x] = 1


def mascot_ref(data, shape, variant, material, pose, mouth=0):
    mask = data['bodies'][shape][variant]
    out = _material(mask, MATERIALS[material])
    _, eye_l, eye_r, lx, ly = data['poses'][pose]
    for eye, sprite in enumerate((eye_l, eye_r)):
        slot = data['eye_slots'][shape][variant][eye]
        px = min(max(slot['probe'][0] + lx, 0), BODY - 1)
        py = min(max(slot['probe'][1] + ly, 0), BODY - 1)
        inside = mask[py][px]
        if MATERIALS[material] == 'jelly' and inside:
            x0, y0, x1, y1 = slot['patch']
            _fill(out, x0 + lx, y0 + ly, x1 + lx, y1 + ly)
        color = 0 if inside and MATERIALS[material] != 'glass' else 1
        tx, ty = slot['tl'][sprite]
        _blit(out, data['sprites'][sprite][1], tx + lx, ty + ly, color)
    if mouth:
        slot = data['mouth_slots'][shape][variant]
        px = min(max(slot['probe'][0] + lx, 0), BODY - 1)
        py = min(max(slot['probe'][1] + ly, 0), BODY - 1)
        inside = mask[py][px]
        rows = data['sprites'][len(EYE_SPRITES) + mouth - 1][1]
        tx, ty = slot['tl'][mouth - 1]
        tx, ty = tx + lx, ty + ly
        if MATERIALS[material] == 'jelly' and inside:   # a solid patch, as for the eyes
            _fill(out, tx - 1, ty - 1, tx + len(rows[0]), ty + len(rows))
        _blit(out, rows, tx, ty, 0 if inside and MATERIALS[material] != 'glass' else 1)
    return out


def compose_ref(data, scene, frame, shape, material, linked, shift, locale=0, modest=False, count=0, dot=False):
    """One 64 x 128 frame as rows of 0/1; the C++ composer must match it."""
    canvas = [[0] * SC.W for _ in range(SC.H)]
    sc = data['scenes'][scene]
    if sc['flags'] & FLAG_BITS['blank']:
        return canvas
    if sc['flags'] & FLAG_BITS['default_look']:
        shape, material = 0, 0
    if sc['flags'] & FLAG_BITS['glass']:
        material = MATERIALS.index('glass')
    if sc['flags'] & FLAG_BITS['still']:
        shift = (0, 0)
    f = data['frames'][sc['first'] + frame]
    words = not modest and 1 <= locale <= len(SC.WORD_LOCALES) and f['word'] != NO_WORD
    bubble = words and data['words'][f['word']]['flags'] & WORD_BUBBLE
    if not modest and not f['flags'] & FRAME_NO_STATUS:
        first, pieces = data['status'][1 if linked else 0]
        for sprite, x, y, _ in data['placements'][first:first + pieces]:
            _blit(canvas, data['sprites'][sprite][1], x, y)
        if dot:   # an unseen notification: the status row's dot (spec §12)
            first, pieces = data['status'][2]
            for sprite, x, y, _ in data['placements'][first:first + pieces]:
                _blit(canvas, data['sprites'][sprite][1], x, y)
    body = mascot_ref(data, shape, f['variant'], material, f['pose'], f['mouth'])
    mx, my, _ = SC.MASCOT
    _blit(canvas, body, mx + shift[0] + f['dx'], my + shift[1] + f['dy'])
    if modest:
        return canvas
    two_digits = 10 <= count <= COUNT_MAX
    for sprite, x, y, flags in data['placements'][f['first']:f['first'] + f['count']]:
        if words and flags & PLACE_ICONS:
            continue
        if two_digits and flags & PLACE_ONE_DIGIT:
            continue
        dx = shift[0] if flags & PLACE_SHIFTED else 0
        dy = shift[1] if flags & PLACE_SHIFTED else 0
        if words and not bubble and flags & PLACE_ZONE:
            dy += SC.WORD_ZONE_DY
        _blit(canvas, data['sprites'][sprite][1], x + dx, y + dy)
    if f['number'] != NO_NUMBER and 1 <= count <= COUNT_MAX:
        # A number is a glyph-zone piece: the wake shift and the word move it.
        zone = SC.WORD_ZONE_DY if words and not bubble else 0
        for sprite, x, y in number_pieces(data['digits'], data['numbers'][f['number']], count):
            _blit(canvas, data['sprites'][sprite][1], x + shift[0], y + shift[1] + zone)
    if words:
        strip = data['words'][f['word']]['strips'][locale - 1]
        _blit(canvas, data['sprites'][strip['sprite']][1], strip['x'] + shift[0],
              strip['y'] + (shift[1] if bubble else 0))
    return canvas


# ------------------------------------------------------------------ header
def _pack(rows):
    out = []
    for row in rows:
        for b in range(0, len(row), 8):
            v = 0
            for i, bit in enumerate(row[b:b + 8]):
                if bit:
                    v |= 0x80 >> i
            out.append(v)
    return out


def _hex_lines(values, indent='    ', per=16):
    lines = []
    for i in range(0, len(values), per):
        lines.append(indent + ', '.join('0x%02x' % v for v in values[i:i + per]) + ',')
    return lines


def _ident(name):
    return name.upper().replace('-', '_')


def asset_bytes(data):
    """Flash the tables take, at the structs' real sizes (padding included)."""
    looks = len(S.SHAPES) * len(VARIANTS)
    body = sum(len(body_spans(rows)) for per_shape in data['bodies'] for rows in per_shape) + 2 * looks
    bits = sum(len(_pack(rows)) for _, rows in data['sprites'])
    tables = (4 * len(data['sprites'])                          # OledSprite
              + looks * 2 * (6 + 2 * len(EYE_SPRITES))          # OledEyeSlot
              + looks * (2 + 2 * len(MOUTH_SPRITES))            # OledMouthSlot
              + 4 * len(data['poses'])                          # OledPose
              + 4 * len(data['placements'])                     # OledPlacement
              + 14 * len(data['frames'])                        # OledFrameDesc
              + 5 * len(data['numbers'])                        # OledNumberSlot
              + 3 * len(DIGIT_CHARS) * len(data['digits']) + len(data['digits'])  # OledDigitGlyph, advances
              + 3 * len(data['scenes'])                         # OledSceneDesc
              + 3 * len(data['words']) * len(SC.WORD_LOCALES)   # OledWordStrip
              + len(data['words'])                              # kOledWordFlags
              + 12 + len(data['scenes']) + 2 + 2 * len(WAKE_SHIFTS))
    return body + bits + tables


def header(data):
    L = []
    w = L.append
    w('// Generated by hardware/firmware/tools/gen_oled_assets.py: do not edit.')
    w('// Pixel data of the keyring OLED (docs/pins/keyring-oled.md, layout A).')
    w('// Bitmaps are row-major, rows padded to whole bytes, the most significant')
    w('// bit is the leftmost pixel. Regenerate after any change to')
    w('// hardware/firmware/tools/oled_assets/ and commit the result;')
    w('// tests/check_oled_assets.py fails on any difference.')
    w('#pragma once')
    w('')
    w('#include <stdint.h>')
    w('')
    w('static const uint8_t OLED_W = %d;' % SC.W)
    w('static const uint8_t OLED_H = %d;' % SC.H)
    w('static const uint8_t OLED_BODY = %d;' % BODY)
    w('static const int8_t OLED_MASCOT_X = %d;' % SC.MASCOT[0])
    w('static const int8_t OLED_MASCOT_Y = %d;' % SC.MASCOT[1])
    w('static const uint16_t OLED_ASSET_BYTES = %d;  // budget %d' % (asset_bytes(data), ASSET_BUDGET))
    w('')
    w('// Ids are plain uint8_t constants, not enums: every field that holds one')
    w('// is a uint8_t, and GCC -Wextra rejects enum/integer mixes in ?: .')
    w('// Scenes:')
    for i, sc in enumerate(data['scenes']):
        w('static const uint8_t OLED_SCENE_%s = %d;' % (_ident(sc['name']), i))
    w('static const uint8_t OLED_SCENE_COUNT = %d;' % len(data['scenes']))
    w('')
    w('// Serial `o<letter>` shows a scene on the bench, in scene order; the')
    w('// modest letter shows «on watch» in modest mode, and the fall\'s letter')
    w('// plays a real fall (bench_serial.h).')
    w('static const char kOledDemoLetters[] = "%s";' % ''.join(SC.DEMO_LETTERS[sc['name']] for sc in data['scenes']))
    w("static const char OLED_DEMO_MODEST_LETTER = '%s';" % SC.DEMO_MODEST_LETTER)
    w('')
    w('// Shapes (the mascot byte\'s high nibble, spec §10):')
    for i, shape in enumerate(S.SHAPES):
        w('static const uint8_t OLED_SHAPE_%s = %d;' % (_ident(shape), i))
    w('static const uint8_t OLED_SHAPE_COUNT = %d;' % len(S.SHAPES))
    w('')
    w('// Materials (its low nibble):')
    for i, m in enumerate(MATERIALS):
        w('static const uint8_t OLED_MATERIAL_%s = %d;' % (_ident(m), i))
    w('static const uint8_t OLED_MATERIAL_COUNT = %d;' % len(MATERIALS))
    w('')
    w('// Words (a frame\'s word strip; OLED_NO_WORD for none):')
    for i, word in enumerate(data['words']):
        w('static const uint8_t OLED_WORD_%s = %d;' % (_ident(word['name']), i))
    w('static const uint8_t OLED_WORD_COUNT = %d;' % len(data['words']))
    w('static const uint8_t OLED_NO_WORD = 0x%02X;' % NO_WORD)
    w('// Word locales: screen_state byte 8 values 1-%d (%s); 0 and ar draw icons only.'
      % (len(SC.WORD_LOCALES), ', '.join(SC.WORD_LOCALES)))
    w('static const uint8_t OLED_WORD_LOCALES = %d;' % len(SC.WORD_LOCALES))
    w('// With a word in the word row the glyph zone is y 64-96 instead of 64-128:')
    w('// zone pieces move up.')
    w('static const int8_t OLED_WORD_ZONE_DY = %d;' % SC.WORD_ZONE_DY)
    w('// A speech-bubble word (the fall\'s «ой»): drawn where the generator put')
    w('// it, moves with the whole wake shift, never moves the glyph zone.')
    w('static const uint8_t OLED_WORD_BUBBLE = %d;' % WORD_BUBBLE)
    w('')
    w('static const uint8_t OLED_SCENE_LOOP = %d;          // frames repeat; otherwise the last one holds' % FLAG_BITS['loop'])
    w('static const uint8_t OLED_SCENE_GLASS = %d;         // outline material whatever the mascot' % FLAG_BITS['glass'])
    w('static const uint8_t OLED_SCENE_DEFAULT_LOOK = %d;  // the default pebble/satin' % FLAG_BITS['default_look'])
    w('static const uint8_t OLED_SCENE_BLANK = %d;         // nothing lit' % FLAG_BITS['blank'])
    w('static const uint8_t OLED_SCENE_STILL = %d;        // no wake shift: a one-off drawn to the edge' % FLAG_BITS['still'])
    w('')
    w('static const uint8_t OLED_FRAME_NO_STATUS = %d;  // this frame draws no status row' % FRAME_NO_STATUS)
    w('')
    w('static const uint8_t OLED_PLACE_SHIFTED = %d;  // moves with the wake shift (burn-in)' % PLACE_SHIFTED)
    w('static const uint8_t OLED_PLACE_ZONE = %d;     // a glyph-zone piece: moves up when a row word shows' % PLACE_ZONE)
    w('static const uint8_t OLED_PLACE_ICONS = %d;    // drawn only when the frame\'s word does not show' % PLACE_ICONS)
    w('static const uint8_t OLED_PLACE_ONE_DIGIT = %d;  // drawn only while the count has one digit' % PLACE_ONE_DIGIT)
    w('')
    w('// Counts: the frame\'s number slot draws ScreenFrame::count (1-%d; 0 draws' % COUNT_MAX)
    w('// no number) from the digit glyphs below. Words: a «count» is the value,')
    w('// a «number» the slot that draws it, a «digit» one glyph (0-9 or «+»).')
    w('static const uint8_t OLED_NO_NUMBER = 0x%02X;' % NO_NUMBER)
    w('static const uint8_t OLED_COUNT_MAX = %d;' % COUNT_MAX)
    w('static const uint8_t OLED_NUMBER_PLUS = %d;  // «+» before the count' % NUMBER_PLUS)
    w('static const uint8_t OLED_DIGIT_PLUS = %d;   // the «+» glyph after 0-9' % DIGIT_CHARS.index('+'))
    w('static const uint8_t OLED_NUMBER_FONTS = %d;' % len(SC.NUMBER_FONTS))
    w('')
    w('struct OledSprite {')
    w('  uint8_t w;')
    w('  uint8_t h;')
    w('  uint16_t offset;  // into kOledSpriteBits')
    w('};')
    w('')
    w('// A glyph piece on the portrait canvas at wake shift 0, without a word.')
    w('// The status row does not move; see OLED_PLACE_* for the rest.')
    w('struct OledPlacement {')
    w('  uint8_t sprite;')
    w('  uint8_t x;')
    w('  uint8_t y;')
    w('  uint8_t flags;')
    w('};')
    w('')
    w('// Where an eye sprite goes inside the 48 x 48 body (top-left), the body')
    w('// pixel that decides dark-on-body vs lit-off-body, and the solid patch')
    w('// that keeps a jelly eye readable. Pose look offsets add to all three.')
    w('struct OledEyeSlot {')
    w('  int8_t probeX;')
    w('  int8_t probeY;')
    w('  int8_t patch[4];  // x0, y0, x1, y1 inclusive')
    w('  int8_t topLeft[%d][2];' % len(EYE_SPRITES))
    w('};')
    w('')
    w('// The same for the mouth; a jelly mouth gets a solid patch one pixel')
    w('// around its sprite.')
    w('struct OledMouthSlot {')
    w('  int8_t probeX;')
    w('  int8_t probeY;')
    w('  int8_t topLeft[%d][2];' % len(MOUTH_SPRITES))
    w('};')
    w('')
    w('struct OledPose {')
    w('  uint8_t eye[2];  // eye sprite index (the first sprites of kOledSprites)')
    w('  int8_t lookX;')
    w('  int8_t lookY;')
    w('};')
    w('')
    w('struct OledFrameDesc {')
    w('  uint16_t ms;')
    w('  uint8_t pose;')
    w('  uint8_t variant;  // body scale: see kOledBodyOffsets')
    w('  int8_t dx;')
    w('  int8_t dy;')
    w('  uint8_t mouth;    // 0 none, n: sprite OLED_MOUTH_SPRITE_BASE + n - 1')
    w('  uint8_t word;     // OLED_WORD_* or OLED_NO_WORD')
    w('  uint8_t flags;    // OLED_FRAME_*')
    w('  uint8_t number;   // index into kOledNumbers or OLED_NO_NUMBER')
    w('  uint16_t firstPlacement;')
    w('  uint16_t placementCount;')
    w('};')
    w('')
    w('// Where a frame draws its count, at wake shift 0 without a word: the pen')
    w('// x of the first glyph for a one-digit and a two-digit count, and the')
    w('// baseline. A number is a glyph-zone piece (shift and word move it).')
    w('struct OledNumberSlot {')
    w('  uint8_t font;  // index into kOledDigitFonts')
    w('  uint8_t x;')
    w('  uint8_t x2;')
    w('  uint8_t base;')
    w('  uint8_t flags;  // OLED_NUMBER_PLUS')
    w('};')
    w('')
    w('// One digit glyph: its sprite (cropped to the ink) and the ink\'s offset')
    w('// from the pen and the baseline.')
    w('struct OledDigitGlyph {')
    w('  uint8_t sprite;')
    w('  int8_t dx;')
    w('  int8_t dy;')
    w('};')
    w('')
    w('struct OledSceneDesc {')
    w('  uint8_t firstFrame;')
    w('  uint8_t frameCount;')
    w('  uint8_t flags;')
    w('};')
    w('')
    w('// One word in one locale: a sprite cropped to its ink and where it goes.')
    w('// A row word moves across with the wake shift, never up or down (the row')
    w('// sits on the bottom edge); a bubble word moves with the whole shift.')
    w('struct OledWordStrip {')
    w('  uint8_t sprite;')
    w('  uint8_t x;')
    w('  uint8_t y;')
    w('};')
    w('')
    w('static const uint8_t OLED_EYE_SPRITE_COUNT = %d;' % len(EYE_SPRITES))
    w('static const uint8_t OLED_MOUTH_SPRITE_BASE = %d;' % len(EYE_SPRITES))
    w('static const uint8_t OLED_MOUTH_COUNT = %d;' % len(MOUTH_SPRITES))
    w('static const uint8_t OLED_BODY_VARIANTS = %d;' % len(VARIANTS))
    w('')
    spans, offsets = [], []
    for si, shape in enumerate(S.SHAPES):
        offsets.append([])
        for vi in range(len(VARIANTS)):
            offsets[-1].append(len(spans))
            spans.extend(body_spans(data['bodies'][si][vi]))
    assert len(spans) <= 0xFFFF, 'body span offsets are uint16_t'
    w('// Body variants (the frame\'s variant): x and y scale about the centre.')
    for vi, (sx, sy) in enumerate(VARIANTS):
        w('//   %d: %.2f x %.2f%s' % (vi, sx, sy, {0: ' (plain)', 1: ' (the press)'}.get(vi, ' (the fall)')))
    w('// Each body is %d rows: a run count, then x0, x1 (inclusive) per run.' % BODY)
    w('static const uint8_t kOledBodySpans[%d] = {' % len(spans))
    L.extend(_hex_lines(spans, indent='  '))
    w('};')
    w('')
    w('static const uint16_t kOledBodyOffsets[%d][%d] = {' % (len(S.SHAPES), len(VARIANTS)))
    for si, shape in enumerate(S.SHAPES):
        w('  {%s},  // %s' % (', '.join(str(o) for o in offsets[si]), shape))
    w('};')
    w('')
    bits, table = [], []
    for name, rows in data['sprites']:
        table.append((name, len(rows[0]), len(rows), len(bits)))
        bits.extend(_pack(rows))
    w('static const uint8_t kOledSpriteBits[%d] = {' % len(bits))
    L.extend(_hex_lines(bits, indent='  '))
    w('};')
    w('')
    w('static const uint8_t OLED_SPRITE_COUNT = %d;' % len(table))
    w('static const OledSprite kOledSprites[%d] = {' % len(table))
    for name, sw, sh, off in table:
        w('  {%d, %d, %d},  // %s' % (sw, sh, off, name))
    w('};')
    w('')
    w('static const OledEyeSlot kOledEyeSlots[%d][%d][2] = {' % (len(S.SHAPES), len(VARIANTS)))
    for si, shape in enumerate(S.SHAPES):
        w('  {  // %s' % shape)
        for vi in range(len(VARIANTS)):
            parts = []
            for slot in data['eye_slots'][si][vi]:
                tl = ', '.join('{%d, %d}' % t for t in slot['tl'])
                parts.append('{%d, %d, {%d, %d, %d, %d}, {%s}}' % (slot['probe'] + slot['patch'] + (tl,)))
            w('    {%s},' % ', '.join(parts))
        w('  },')
    w('};')
    w('')
    w('static const OledMouthSlot kOledMouthSlots[%d][%d] = {' % (len(S.SHAPES), len(VARIANTS)))
    for si, shape in enumerate(S.SHAPES):
        parts = []
        for vi in range(len(VARIANTS)):
            slot = data['mouth_slots'][si][vi]
            tl = ', '.join('{%d, %d}' % t for t in slot['tl'])
            parts.append('{%d, %d, {%s}}' % (slot['probe'] + (tl,)))
        w('  {%s},  // %s' % (', '.join(parts), shape))
    w('};')
    w('')
    w('// Poses (index into kOledPoses):')
    for i, (name, _, _, _, _) in enumerate(data['poses']):
        w('static const uint8_t OLED_POSE_%s = %d;' % (_ident(name), i))
    w('')
    w('static const OledPose kOledPoses[%d] = {' % len(data['poses']))
    for name, el, er, lx, ly in data['poses']:
        w('  {{%d, %d}, %d, %d},  // %s' % (el, er, lx, ly, name))
    w('};')
    w('')
    w('static const OledPlacement kOledPlacements[%d] = {' % len(data['placements']))
    for sprite, x, y, flags in data['placements']:
        w('  {%d, %d, %d, %d},' % (sprite, x, y, flags))
    w('};')
    w('')
    w('// Status row pieces: [0] link lost, [1] linked, [2] the unseen-notification')
    w('// dot (ScreenFrame::dot). {first, count}.')
    w('static const uint16_t kOledStatus[3][2] = {{%d, %d}, {%d, %d}, {%d, %d}};' % (
        data['status'][0] + data['status'][1] + data['status'][2]))
    w('')
    w('static const OledFrameDesc kOledFrames[%d] = {' % len(data['frames']))
    for f in data['frames']:
        word = 'OLED_NO_WORD' if f['word'] == NO_WORD else 'OLED_WORD_%s' % _ident(data['words'][f['word']]['name'])
        number = 'OLED_NO_NUMBER' if f['number'] == NO_NUMBER else str(f['number'])
        w('  {%d, %d, %d, %d, %d, %d, %s, %d, %s, %d, %d},' % (f['ms'], f['pose'], f['variant'], f['dx'], f['dy'],
                                                             f['mouth'], word, f['flags'], number, f['first'],
                                                             f['count']))
    w('};')
    w('')
    w('static const OledNumberSlot kOledNumbers[%d] = {' % max(1, len(data['numbers'])))
    for n in data['numbers']:
        w('  {%d, %d, %d, %d, %d},' % (n['font'], n['x'], n['x2'], n['base'], NUMBER_PLUS if n['plus'] else 0))
    w('};')
    w('')
    w('static const uint8_t kOledDigitAdvance[OLED_NUMBER_FONTS] = {%s};' % ', '.join(
        str(d['advance']) for d in data['digits']))
    w('static const OledDigitGlyph kOledDigitFonts[OLED_NUMBER_FONTS][%d] = {' % len(DIGIT_CHARS))
    for d in data['digits']:
        parts = ['{%d, %d, %d}' % (g['sprite'], g['dx'], g['dy']) for g in d['glyphs']]
        w('  {%s},  // %s, 0-9 and +' % (', '.join(parts), d['font']))
    w('};')
    w('')
    w('static const OledSceneDesc kOledScenes[OLED_SCENE_COUNT] = {')
    for sc in data['scenes']:
        w('  {%d, %d, %d},  // %s' % (sc['first'], sc['count'], sc['flags'], sc['name']))
    w('};')
    w('')
    w('static const OledWordStrip kOledWords[OLED_WORD_COUNT][OLED_WORD_LOCALES] = {')
    for word in data['words']:
        parts = ['{%d, %d, %d}' % (s['sprite'], s['x'], s['y']) for s in word['strips']]
        w('  {%s},  // %s' % (', '.join(parts), word['name']))
    w('};')
    w('')
    w('static const uint8_t kOledWordFlags[OLED_WORD_COUNT] = {%s};' % ', '.join(
        str(word['flags']) for word in data['words']))
    w('')
    w('// Mascot and glyph-zone offset per wake (spec §5: ±1-2 px against burn-in).')
    w('static const uint8_t OLED_WAKE_SHIFTS = %d;' % len(WAKE_SHIFTS))
    w('static const int8_t kOledWakeShift[%d][2] = {%s};' % (
        len(WAKE_SHIFTS), ', '.join('{%d, %d}' % s for s in WAKE_SHIFTS)))
    w('')
    return '\n'.join(L)


# ------------------------------------------------------------------ goldens
def golden_cases(data):
    """(file stem, scene, frame, shape, material, linked, shift, locale, modest, count, dot) per golden frame."""
    names = [sc['name'] for sc in data['scenes']]
    shapes, cases = list(S.SHAPES), []

    def case(stem, scene, frame=0, shape='pebble', material='satin', linked=True, shift=(0, 0), locale=0,
             modest=False, count=0, dot=False):
        cases.append((stem, names.index(scene), frame, shapes.index(shape), MATERIALS.index(material), linked,
                      shift, locale, modest, count, dot))

    demo = {'found': 3, 'looking': 5}   # the pack's drawn counts
    for sc in data['scenes']:
        for fi in range(sc['count']):
            case('%s-f%d-pebble-satin' % (sc['name'], fi + 1), sc['name'], fi, linked=sc['name'] != 'no_link',
                 count=demo.get(sc['name'], 0))
    for shape in S.SHAPES:
        for material in MATERIALS:
            for scene in ('nothing_known', 'pressed'):
                if shape == 'pebble' and material == 'satin':
                    continue
                case('%s-f1-%s-%s' % (scene, shape, material), scene, 0, shape, material)
    case('searching-f2-cube-jelly-shift', 'searching', 1, 'cube', 'jelly', shift=(-2, 2))
    case('result-f2-drop-glass-shift', 'result', 1, 'drop', 'glass', shift=(2, -1))
    case('no_link-f1-fold-satin-shift', 'no_link', 0, 'fold', 'satin', linked=False, shift=(-1, -1))
    case('set_me_up-f2-loop-jelly', 'set_me_up', 1, 'loop', 'jelly', shift=(1, 2))
    case('error-f1-lens-satin-unlinked', 'error', 0, 'lens', 'satin', linked=False)
    # Words: every scene with a word, in every locale; ar and «none» draw none.
    for sc in data['scenes']:
        if sc['word'] == NO_WORD:
            continue
        for li, locale in enumerate(SC.WORD_LOCALES):
            case('%s-f1-pebble-satin-%s' % (sc['name'], locale), sc['name'], 0, linked=sc['name'] != 'no_link',
                 locale=li + 1, count=demo.get(sc['name'], 0))
    case('watch-f1-pebble-satin-ar', 'watch', 0, locale=5)
    case('result-f2-drop-glass-shift-es', 'result', 1, 'drop', 'glass', shift=(-2, 2), locale=3)
    case('searching-f3-cube-satin-shift-pt', 'searching', 2, 'cube', 'satin', shift=(2, -1), locale=4)
    # The agent scenes wear the owner's mascot (set_me_up keeps the default).
    case('watch-f2-cube-jelly-ru', 'watch', 1, 'cube', 'jelly', locale=1)
    case('watch-f3-loop-glass-en', 'watch', 2, 'loop', 'glass', locale=2)
    case('paused-f2-loop-glass-en', 'paused', 1, 'loop', 'glass', locale=2)
    case('paused-f1-drop-satin', 'paused', 0, 'drop', 'satin')
    case('needs_setup-f1-fold-satin-es', 'needs_setup', 0, 'fold', 'satin', locale=3)
    case('needs_setup-f2-lens-jelly-shift', 'needs_setup', 1, 'lens', 'jelly', shift=(-1, 1))
    case('set_me_up-f1-lens-jelly-ru', 'set_me_up', 0, 'lens', 'jelly', locale=1)
    # Speaking: the mouth on every shape and material (hollow shapes put it lower).
    for shape in S.SHAPES:
        for material in MATERIALS:
            if shape == 'pebble' and material == 'satin':
                continue
            case('speaking-f2-%s-%s' % (shape, material), 'speaking', 1, shape, material)
    case('speaking-f4-fold-jelly-pt-shift', 'speaking', 3, 'fold', 'jelly', shift=(2, 1), locale=4)
    # Modest: the mascot and its pose only, no status row, glyphs or words.
    case('watch-f1-pebble-satin-modest', 'watch', 0, modest=True)
    case('result-f1-cube-satin-ru-modest', 'result', 0, 'cube', 'satin', locale=1, modest=True)
    case('speaking-f2-drop-jelly-en-modest', 'speaking', 1, 'drop', 'jelly', locale=2, modest=True)
    case('no_link-f1-lens-satin-modest', 'no_link', 0, 'lens', 'satin', linked=False, modest=True)
    # The fall: every frame in ru and en (icons only above), the word frames
    # in es, pt and ar (icons: «!» in the bubble, no row word), the big
    # squash on every look, a still scene ignoring the wake shift, modest.
    fell = data['scenes'][names.index('fell')]
    for fi in range(fell['count']):
        for li, locale in enumerate(SC.WORD_LOCALES[:2]):
            case('fell-f%d-pebble-satin-%s' % (fi + 1, locale), 'fell', fi, locale=li + 1)
    for fi in (6, 7):
        for li, locale in enumerate(SC.WORD_LOCALES[2:]):
            case('fell-f%d-pebble-satin-%s' % (fi + 1, locale), 'fell', fi, locale=li + 3)
        case('fell-f%d-pebble-satin-ar' % (fi + 1), 'fell', fi, locale=5)
    for shape in S.SHAPES:
        for material in MATERIALS:
            if shape == 'pebble' and material == 'satin':
                continue
            case('fell-f4-%s-%s' % (shape, material), 'fell', 3, shape, material)
    case('fell-f2-loop-satin', 'fell', 1, 'loop', 'satin')
    case('fell-f7-fold-jelly-en', 'fell', 6, 'fold', 'jelly', locale=2)
    case('fell-f8-cube-jelly-ru', 'fell', 7, 'cube', 'jelly', locale=1)
    case('fell-f8-loop-glass-en', 'fell', 7, 'loop', 'glass', locale=2)
    case('fell-f8-drop-glass-es', 'fell', 7, 'drop', 'glass', locale=3)
    case('fell-f8-fold-satin-pt', 'fell', 7, 'fold', 'satin', locale=4)
    case('fell-f3-pebble-satin-unlinked', 'fell', 2, linked=False)
    case('fell-f7-pebble-satin-ru-shift', 'fell', 6, shift=(2, -1), locale=1)
    case('fell-f7-pebble-satin-ru-modest', 'fell', 6, locale=1, modest=True)
    case('fell-f8-lens-satin-en-modest', 'fell', 7, 'lens', 'satin', locale=2, modest=True)
    # «Found you»: the owner's mascot, the wake shift (the status dot stays
    # put, the «!» and the hand move), words, ar icons only, unlinked, modest.
    case('found_you-f1-cube-jelly-shift', 'found_you', 0, 'cube', 'jelly', shift=(-2, 2))
    case('found_you-f2-drop-glass-ru', 'found_you', 1, 'drop', 'glass', locale=1)
    case('found_you-f3-loop-satin-es-shift', 'found_you', 2, 'loop', 'satin', shift=(2, -1), locale=3)
    case('found_you-f1-fold-jelly-pt', 'found_you', 0, 'fold', 'jelly', locale=4)
    case('found_you-f2-pebble-satin-ar', 'found_you', 1, locale=5)
    case('found_you-f3-lens-satin-unlinked-en', 'found_you', 2, 'lens', 'satin', linked=False, locale=2)
    case('found_you-f1-pebble-satin-ru-modest', 'found_you', 0, locale=1, modest=True)
    # The counts: the pack's frames in ru (the design check compares them),
    # one and two digits, the wake shift, no word, modest, other looks.
    for fi in range(4):
        case('found-f%d-pebble-satin-ru-3' % (fi + 1), 'found', fi, locale=1, count=3)
    for fi in range(2):
        case('found_self-f%d-pebble-satin-ru' % (fi + 1), 'found_self', fi, locale=1)
        case('nobody-f%d-pebble-satin-ru' % (fi + 1), 'nobody', fi, locale=1)
    case('looking-f1-pebble-satin-ru-5', 'looking', 0, locale=1, count=5)
    case('found-f4-pebble-satin-ru-1', 'found', 3, locale=1, count=1)
    case('found-f4-pebble-satin-ru-10', 'found', 3, locale=1, count=10)
    case('found-f4-cube-jelly-en-99-shift', 'found', 3, 'cube', 'jelly', shift=(2, -1), locale=2, count=99)
    case('found-f4-lens-glass-47-shift', 'found', 3, 'lens', 'glass', shift=(-2, 2), count=47)
    case('found-f1-drop-satin-es-8', 'found', 0, 'drop', 'satin', locale=3, count=8)
    case('found-f3-fold-satin-pt-12-unlinked', 'found', 2, 'fold', 'satin', linked=False, locale=4, count=12)
    case('found-f4-pebble-satin-ar-7', 'found', 3, locale=5, count=7)
    case('found-f4-pebble-satin-ru-0', 'found', 3, locale=1)
    case('found-f4-loop-satin-ru-3-modest', 'found', 3, 'loop', 'satin', locale=1, modest=True, count=3)
    case('found_self-f2-cube-glass-en-shift', 'found_self', 1, 'cube', 'glass', shift=(-1, 1), locale=2)
    case('found_self-f1-fold-jelly', 'found_self', 0, 'fold', 'jelly')
    case('found_self-f1-drop-satin-es-modest', 'found_self', 0, 'drop', 'satin', locale=3, modest=True)
    case('nobody-f2-lens-jelly-pt-shift', 'nobody', 1, 'lens', 'jelly', shift=(2, 1), locale=4)
    case('nobody-f1-pebble-satin-ar', 'nobody', 0, locale=5)
    case('looking-f1-pebble-satin-ru-1', 'looking', 0, locale=1, count=1)
    case('looking-f1-pebble-satin-ru-99', 'looking', 0, locale=1, count=99)
    case('looking-f1-drop-jelly-en-23-shift', 'looking', 0, 'drop', 'jelly', shift=(-2, 2), locale=2, count=23)
    case('looking-f1-loop-glass-9-shift', 'looking', 0, 'loop', 'glass', shift=(2, -1), count=9)
    case('looking-f1-pebble-satin-es-withheld', 'looking', 0, locale=3)
    case('looking-f1-fold-satin-ru-12-modest', 'looking', 0, 'fold', 'satin', locale=1, modest=True, count=12)
    # Notifications: every frame in ru (the design check compares them with
    # the pack), other looks, shifted, unlinked, modest, ar icons only.
    for name in ('request', 'accepted', 'report', 'talks', 'door', 'question'):
        sc = data['scenes'][names.index(name)]
        for fi in range(sc['count']):
            case('%s-f%d-pebble-satin-ru' % (name, fi + 1), name, fi, locale=1)
    case('request-f1-cube-jelly-en-shift', 'request', 0, 'cube', 'jelly', shift=(-2, 2), locale=2)
    case('accepted-f1-lens-glass-es-unlinked', 'accepted', 0, 'lens', 'glass', linked=False, locale=3)
    case('report-f2-drop-satin-pt-shift', 'report', 1, 'drop', 'satin', shift=(2, -1), locale=4)
    case('talks-f1-loop-jelly-es', 'talks', 0, 'loop', 'jelly', locale=3)
    case('door-f1-fold-satin-ar', 'door', 0, 'fold', 'satin', locale=5)
    case('question-f2-cube-glass-en-shift', 'question', 1, 'cube', 'glass', shift=(1, 2), locale=2)
    case('question-f1-lens-jelly', 'question', 0, 'lens', 'jelly')
    case('request-f1-pebble-satin-ru-modest', 'request', 0, locale=1, modest=True)
    # The unseen-notification dot on other scenes (it never moves), and not
    # in modest mode or without the status row.
    case('watch-f1-pebble-satin-ru-dot', 'watch', 0, locale=1, dot=True)
    case('searching-f2-cube-jelly-shift-dot', 'searching', 1, 'cube', 'jelly', shift=(-2, 2), dot=True)
    case('no_link-f1-pebble-satin-dot', 'no_link', 0, linked=False, dot=True)
    case('watch-f1-pebble-satin-dot-modest', 'watch', 0, modest=True, dot=True)
    case('fell-f1-pebble-satin-dot', 'fell', 0, dot=True)
    return cases


def write_goldens(data, out):
    os.makedirs(out, exist_ok=True)
    lines = ['# file scene frame shape material linked shift_x shift_y locale modest count dot '
             '(written by gen_oled_assets.py)']
    for stem, scene, frame, shape, material, linked, shift, locale, modest, count, dot in golden_cases(data):
        canvas = compose_ref(data, scene, frame, shape, material, linked, shift, locale, modest, count, dot)
        im = Image.new('1', (SC.W, SC.H), 0)
        px = im.load()
        for y in range(SC.H):
            for x in range(SC.W):
                if canvas[y][x]:
                    px[x, y] = 1
        im.save(os.path.join(out, stem + '.png'), optimize=False)
        lines.append('%s.png %d %d %d %d %d %d %d %d %d %d %d' % (stem, scene, frame, shape, material,
                                                                 1 if linked else 0, shift[0], shift[1], locale,
                                                                 1 if modest else 0, count, 1 if dot else 0))
    with open(os.path.join(out, 'manifest.txt'), 'w') as f:
        f.write('\n'.join(lines) + '\n')


def words_table(data):
    """The word strips as a Markdown table: text and pixel width per locale."""
    out = ['| Word | ' + ' | '.join(SC.WORD_LOCALES) + ' |', '| --- |' + ' --- |' * len(SC.WORD_LOCALES)]
    for word in data['words']:
        cells = []
        for s in word['strips']:
            small = '' if s['font'].startswith('u8g2_font_6x13') else ' (5x8)'
            if word['flags'] & WORD_BUBBLE:
                small = ' (5x8, bubble)'
            cells.append(' / '.join('%s %d' % (line, width) for line, width in zip(s['lines'], s['widths'])) + small)
        out.append('| %s | %s |' % (word['name'], ' | '.join(cells)))
    return '\n'.join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--header', default=HEADER)
    ap.add_argument('--golden', default=GOLDEN)
    ap.add_argument('--words', action='store_true', help='print the word table (px widths) and exit')
    args = ap.parse_args()
    import PIL
    if PIL.__version__ != PILLOW_REFERENCE:
        print('gen_oled_assets.py: Pillow %s, the checked-in assets were made with %s; '
              'shapes and arcs may rasterise differently' % (PIL.__version__, PILLOW_REFERENCE), file=sys.stderr)
    data = build()
    if args.words:
        print(words_table(data))
        return
    errors = timing_errors(data)
    if errors:
        raise SystemExit('gen_oled_assets.py: ' + '; '.join(errors))
    size = asset_bytes(data)
    if size > ASSET_BUDGET:
        raise SystemExit('gen_oled_assets.py: assets are %d B, over the %d B budget' % (size, ASSET_BUDGET))
    os.makedirs(os.path.dirname(os.path.abspath(args.header)), exist_ok=True)
    with open(args.header, 'w') as f:
        f.write(header(data))
    write_goldens(data, args.golden)
    print('oled assets: %d B (budget %d B), %d sprites, %d placements, %d frames, %d word strips, %d golden frames' % (
        size, ASSET_BUDGET, len(data['sprites']), len(data['placements']), len(data['frames']),
        len(data['words']) * len(SC.WORD_LOCALES), len(golden_cases(data))))


if __name__ == '__main__':
    main()
