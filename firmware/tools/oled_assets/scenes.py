"""The keyring screen scenes (layout A) and their words.

Each scene is a list of frames in the concept pack's timing
(docs/pins/keyring-oled/concept/tools/screens.py, approved as drawn on
2026-09-28). A frame names the mascot's pose, mouth and offset and draws
two kinds of pieces with Pillow on the 64 x 128 portrait canvas at wake
shift 0:

- `glyphs`, the glyph zone: drawn around ZONE_CENTER, the centre of y
  64-128 when no word shows. When the scene's word shows (the phone sent a
  locale with word strips, spec §14), the zone is the pack's y 64-96 and
  every zone piece moves up WORD_ZONE_DY;
- `overlays`, around the mascot (sparkles, sweat, «z z», speech): they
  stay where they are. `whole` pieces are overlays kept as one sprite
  (the fall's dotted floor would be 19 one-pixel pieces), and `icons`
  are overlays drawn only when the frame's word does not show (the «!»
  in the fall's speech bubble without a word locale).

All of them move with the wake shift, except in a `still` scene. The
generator cuts the drawings into sprites. A frame may drop the status row
(`status=False`) and names its word (by default the scene's). The scene
order is the OledScene list of oled_assets.h; screen_state.h decides which
scene shows, oled_compose.h draws it.
"""
import random

from PIL import Image, ImageDraw

from .pixels import ICONS, bits
from .u8g2_font import font

W, H = 64, 128
MASCOT = (8, 12, 48)          # x, y, size: 8 px margins, below the 8 px status row
ZONE_CENTER = (32, 96)        # the glyph zone, y 64-128 while no word shows
WORD_ZONE_DY = -16            # with a word: the pack's zone y 64-96, centre y 80
PLAIN = (1.0, 1.0)
SQUASH = (1.12, 0.84)         # the press frame's body (the pack's f_press frame 1)
SPEECH_FONT = 'u8g2_font_4x6_t_cyrillic'
FLOOR_Y = 92                  # the fall's dotted floor (the pack's o_floor)


def _sprite(name, k=1):
    rows = bits(ICONS[name])
    im = Image.new('1', (len(rows[0]) * k, len(rows) * k), 0)
    px = im.load()
    for y, row in enumerate(rows):
        for x, v in enumerate(row):
            if v:
                for j in range(k):
                    for i in range(k):
                        px[x * k + i, y * k + j] = 1
    return im


def _put(fb, sprite, x, y):
    px, sp = fb.load(), sprite.load()
    for j in range(sprite.size[1]):
        for i in range(sprite.size[0]):
            if sp[i, j] and 0 <= x + i < fb.size[0] and 0 <= y + j < fb.size[1]:
                px[x + i, y + j] = 1


def _put_c(fb, sprite, cx, cy):
    _put(fb, sprite, int(round(cx - sprite.size[0] / 2)), int(round(cy - sprite.size[1] / 2)))


# ------------------------------------------------------------------ glyph drawers (pack geometry)
def status(linked):
    def draw(fb):
        _put(fb, _sprite('bt'), 0, 0)
        if not linked:
            ImageDraw.Draw(fb).line([(6, 8), (10, 0)], fill=1)
    return draw


def radar(phase):
    def draw(fb):
        cx, cy = ZONE_CENTER[0], ZONE_CENTER[1] + 4
        d = ImageDraw.Draw(fb)
        d.ellipse([cx - 1, cy - 1, cx + 1, cy + 1], fill=1)
        for i in range(3):
            r = 5 + ((i * 6 + phase * 2) % 18)
            d.arc([cx - r, cy - r, cx + r, cy + r], 215, 325, fill=1)
            if r < 14:
                d.arc([cx - r, cy - r, cx + r, cy + r], 35, 145, fill=1)
    return draw


def big_phone(dy=0):
    def draw(fb):
        _put_c(fb, _sprite('phone', 2), ZONE_CENTER[0], ZONE_CENTER[1] + dy)
    return draw


def crossed_phone(fb):
    big_phone()(fb)
    cx, cy = ZONE_CENTER
    ImageDraw.Draw(fb).line([(cx - 13, cy + 13), (cx + 13, cy - 13)], fill=1, width=2)


def look_at_phone(arrow_dx):
    """«→▯», the pack's k21 look-at-the-phone glyph: v1's ack 3 knows no
    count and no «found» vs «nobody», so the result says «see the phone»."""
    def draw(fb):
        cx, cy = ZONE_CENTER
        big_phone()(fb)
        _put_c(fb, _sprite('arrow_r', 2), cx + arrow_dx, cy)
    return draw


def watch_dots(lit):
    """The pack's g_dots: three watch dots, one of them lit big."""
    def draw(fb):
        cx, cy = ZONE_CENTER
        d = ImageDraw.Draw(fb)
        for i, dx in enumerate((-8, 0, 8)):
            x, y = cx + dx, cy
            if i == lit:
                d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=1)
            else:
                d.point((x, y), 1)
    return draw


def sparkle(phase):
    def draw(fb):
        x, y, s = MASCOT
        d = ImageDraw.Draw(fb)
        for i, (px, py) in enumerate([(x - 4, y + 6), (x + s + 3, y + 2), (x + s + 2, y + s - 10), (x - 3, y + s - 14)]):
            if (i + phase) % 2:
                continue
            d.line([(px - 2, py), (px + 2, py)], fill=1)
            d.line([(px, py - 2), (px, py + 2)], fill=1)
    return draw


def sweat(fb):
    x, y, s = MASCOT
    _put(fb, _sprite('sweat'), x + s - 4, y + 6)


def zz(phase):
    """The pack's o_zz: «z z» over the sleeping mascot's shoulder."""
    def draw(fb):
        x, y, s = MASCOT
        _put(fb, _sprite('zz'), x + s - 9, y - 1 - phase % 2)
    return draw


def speech(phase, dy):
    """The pack's o_speech: small letters and dots drift down out of the mouth and vanish."""
    def draw(fb):
        x, y, s = MASCOT
        mx, my = x + s / 2, y + s * 0.62 + dy
        rnd = random.Random(7)
        glyphs = ['a', 'o', '~', '.', 'e', '*']
        f = font(SPEECH_FONT)
        d = ImageDraw.Draw(fb)
        for i in range(6):
            t = ((phase + i * 1.5) % 6) / 6.0
            px = mx + (i % 3 - 1) * 9 * t + rnd.randint(-2, 2)
            py = my + 10 + 34 * t
            if t < 0.15:
                continue
            g = glyphs[i % len(glyphs)]
            if g == '.':
                d.rectangle([px, py, px + 1, py + 1], fill=1)
            elif g == '*':
                d.point([(px, py), (px - 1, py), (px + 1, py), (px, py - 1), (px, py + 1)], 1)
            else:
                f.draw(fb, int(px), int(py), g)
    return draw


# ------------------------------------------------------------------ the fall (the pack's f_fall, k23-lost)
def _box(dy):
    x, y, s = MASCOT
    return x, y + dy, s


def speedlines(dy):
    """The pack's o_speedlines: three strokes above the falling mascot."""
    def draw(fb):
        x, y, s = _box(dy)
        d = ImageDraw.Draw(fb)
        for i, cx in enumerate((x + 12, x + 24, x + 36)):
            top = y - 12 + (i % 2) * 3
            d.line([(cx, top), (cx, top + 6)], fill=1)
    return draw


def floor(fb):
    """The pack's o_floor: a dotted floor under the landing spot."""
    x, _, s = MASCOT
    d = ImageDraw.Draw(fb)
    for fx in range(x - 4, x + s + 5, 3):
        d.point((fx, FLOOR_Y), 1)


def dust(phase):
    """The pack's o_dust: a puff either side on the floor, rising."""
    def draw(fb):
        x, _, s = MASCOT
        d = ImageDraw.Draw(fb)
        r = 3 + phase * 3
        for side in (-1, 1):
            cx = x + s // 2 + side * (s // 2 + r)
            d.point([(cx, FLOOR_Y - 1), (cx + side * 2, FLOOR_Y - 3 - phase), (cx - side, FLOOR_Y - 5 - phase * 2)], 1)
    return draw


def stars(dy, phase):
    """The pack's o_stars: three stars over the head, one of them out at a time."""
    def draw(fb):
        x, y, s = _box(dy)
        d = ImageDraw.Draw(fb)
        for i, (px, py) in enumerate([(x + 6, y - 5), (x + s // 2, y - 8), (x + s - 6, y - 5)]):
            if (i + phase) % 3 == 2:
                continue
            d.line([(px - 2, py), (px + 2, py)], fill=1)
            d.line([(px, py - 2), (px, py + 2)], fill=1)
            d.point([(px - 1, py - 1), (px + 1, py + 1), (px - 1, py + 1), (px + 1, py - 1)], 1)
    return draw


def bubble_box(dy):
    """The pack's o_ouch bubble, up and to the right of the head: x0, y0, x1, y1."""
    x, y, s = _box(dy)
    return x + s - 16, y - 16, x + s + 7, y - 4


def bubble(dy):
    """The bubble without its text. It clears what it covers (a star), as in the pack."""
    def draw(fb):
        bx0, by0, bx1, by1 = bubble_box(dy)
        d = ImageDraw.Draw(fb)
        d.rounded_rectangle([bx0, by0, bx1, by1], radius=3, outline=1, fill=0)
        d.line([(bx0 + 5, by1), (bx0 + 2, by1 + 3)], fill=1)
    return draw


def bubble_text_at(dy):
    """Where the pack writes «ой»: 3 px in from the left edge, baseline 3 px above the bottom."""
    bx0, _, _, by1 = bubble_box(dy)
    return bx0 + 3, by1 - 3


def bubble_mark(dy):
    """Without a word locale the bubble says «!»: no language, the same beat."""
    def draw(fb):
        bx0, _, bx1, _ = bubble_box(dy)
        f = font(BUBBLE_MARK_FONT)
        _, base = bubble_text_at(dy)
        f.draw(fb, int(round((bx0 + bx1) / 2 - f.width('!') / 2)), base, '!')
    return draw


def hand(dy=0):
    """The pack's k20 glyph: a waving hand, twice the icon size."""
    def draw(fb):
        _put_c(fb, _sprite('hand', 2), ZONE_CENTER[0], ZONE_CENTER[1] + dy)
    return draw


def bang(big):
    """The pack's o_bang: «!» by the mascot's top right corner."""
    def draw(fb):
        x, y, s = MASCOT
        bx = x + s - 2
        d = ImageDraw.Draw(fb)
        d.rectangle([bx, y - 2, bx + 1, y + (6 if big else 4)], fill=1)
        d.rectangle([bx, y + (8 if big else 6), bx + 1, y + (9 if big else 7)], fill=1)
    return draw


def status_dot(fb):
    """The pack's status-row notification dot (oled.py status_row, dot=True)."""
    ImageDraw.Draw(fb).rectangle([W - 20, 2, W - 18, 4], fill=1)


# ------------------------------------------------------------------ counts (the pack's k05, a02, k06, a03)
# The digits are the only piece a scene cannot pre-draw: the pin draws the
# count at run time from glyph sprites of these U8g2 fonts, at a frame's
# `number` slot. Everything around them is pre-drawn like any glyph.
DIGITS = 'u8g2_font_logisoso24_tn'     # the pack's DIGITS: the big «0» and «+N»
DIGITS_S = 'u8g2_font_logisoso16_tn'   # the pack's DIGITS_S: the digit on the card
NUMBER_FONTS = (DIGITS_S, DIGITS)      # a number slot's font index
CARD = (40, 26)                         # the pack's g_card: w, h


def card_top(rise):
    cx, cy = ZONE_CENTER
    return int(cy - CARD[1] / 2 + (1 - rise) * 22)


def card(rise):
    """The pack's g_card without the count: the mascot «presents» a card
    that slides up, a person on the left. The small phone on the right is
    `card_phone`: it gives way to a second digit."""
    def draw(fb):
        cx, _ = ZONE_CENTER
        cw, ch = CARD
        top = card_top(rise)
        ImageDraw.Draw(fb).rounded_rectangle([cx - cw / 2, top, cx + cw / 2, top + ch], radius=4, fill=0, outline=1)
        _put(fb, _sprite('person'), int(cx - cw / 2 + 5), top + 9)
    return draw


def card_phone(rise):
    def draw(fb):
        cx, _ = ZONE_CENTER
        phone = _sprite('phone').resize((6, 9))
        _put(fb, phone, int(cx + CARD[0] / 2 - 10), card_top(rise) + 9)
    return draw


def card_number(rise):
    """Where the pack writes «3» on the card: 17 px in, baseline 21 px
    down. Two digits (10-99) fill the card from the person to the right
    edge, centred there, and the small phone gives way."""
    cx, _ = ZONE_CENTER
    left = int(cx - CARD[0] / 2)
    return dict(font=0, x=left + 17, x2=left + 14, base=card_top(rise) + 21, plus=False)


def plus_number():
    """The pack's a03 «+5»: pen at zone centre - 20, baseline centre + 12.
    Two digits move left half a digit, so «+N» stays centred where drawn."""
    cx, cy = ZONE_CENTER
    adv = 15
    return dict(font=1, x=cx - 20, x2=cx - 20 - adv // 2, base=cy + 12, plus=True)


def big_zero(fb):
    """The pack's k06 «0»: pen at zone centre - 7, baseline centre + 12."""
    cx, cy = ZONE_CENTER
    font(DIGITS).draw(fb, int(cx - 7), int(cy + 12), '0')


def big_icon(name, dx=0, dy=0, dot=False, k=2):
    """The pack's g_icon: an icon at k times its size, centred in the glyph
    zone, with a dot at its top right when `dot`."""
    def draw(fb):
        cx, cy = ZONE_CENTER
        icon = _sprite(name, k)
        _put_c(fb, icon, cx + dx, cy + dy)
        if dot:
            ex, ey = int(cx + icon.size[0] / 2 + dx), int(cy - icon.size[1] / 2 + dy)
            ImageDraw.Draw(fb).ellipse([ex - 1, ey - 4, ex + 5, ey + 2], fill=1)
    return draw


def person_dot(fb):
    """The pack's a02 glyph: the person at twice the size, a dot at its
    top right (g_icon with dot=True)."""
    cx, cy = ZONE_CENTER
    icon = _sprite('person', 2)
    _put_c(fb, icon, cx, cy)
    ex, ey = int(cx + icon.size[0] / 2), int(cy - icon.size[1] / 2)
    ImageDraw.Draw(fb).ellipse([ex - 1, ey - 4, ex + 5, ey + 2], fill=1)


# ------------------------------------------------------------------ words
# One or two lines per locale (spec §14: ru, en, es, pt-BR; locale 0 and ar
# draw icons only). ru is the concept pack's; en/es/pt are short, human and
# fit 64 px: gen_oled_assets.py asserts it. No raw identifier ever shows.
WORD_LOCALES = ('ru', 'en', 'es', 'pt')     # screen_state byte 8 values 1-4, in order
WORDS = {
    'watch': {'ru': ('на вахте',), 'en': ('on watch',), 'es': ('de guardia',), 'pt': ('de plantão',)},
    'searching': {'ru': ('ищу рядом',), 'en': ('searching',), 'es': ('buscando',), 'pt': ('procurando',)},
    'see_phone': {'ru': ('в телефоне',), 'en': ('check', 'your phone'), 'es': ('mira el', 'teléfono'),
                  'pt': ('veja o', 'celular')},
    'failed': {'ru': ('не вышло',), 'en': ('no luck',), 'es': ('no salió',), 'pt': ('deu errado',)},
    'no_link': {'ru': ('нет связи',), 'en': ('no link',), 'es': ('sin', 'conexión'), 'pt': ('sem', 'conexão')},
    'open_app': {'ru': ('открой', 'приложение'), 'en': ('open', 'the app'), 'es': ('abre', 'la app'),
                 'pt': ('abra', 'o app')},
    'paused': {'ru': ('на паузе',), 'en': ('paused',), 'es': ('en pausa',), 'pt': ('pausado',)},
    'speaking': {'ru': ('говорю',), 'en': ('talking',), 'es': ('hablando',), 'pt': ('falando',)},
    # The fall: «ой» in the bubble, then «было / больно» in the word row.
    # The bubble already says the interjection, so the row says the rest.
    'ouch': {'ru': ('ой',), 'en': ('ow',), 'es': ('ay',), 'pt': ('ai',)},
    'hurt': {'ru': ('было', 'больно'), 'en': ('that', 'hurt'), 'es': ('me', 'dolió'), 'pt': ('isso', 'doeu')},
    # Someone nearby pressed and found this person (the press-again notice).
    # One line as in the pack where it fits.
    'found_you': {'ru': ('тебя нашли',), 'en': ('found you',), 'es': ('te', 'encontraron'),
                  'pt': ('te acharam',)},
    # The agent's counts (spec §9 K3-K5; the pack's words, «никого» as drawn).
    'people': {'ru': ('есть люди',), 'en': ('people',), 'es': ('hay gente',), 'pt': ('tem gente',)},
    'nobody': {'ru': ('никого',), 'en': ('nobody',), 'es': ('nadie',), 'pt': ('ninguém',)},
    'looking': {'ru': ('смотрят',), 'en': ('looking',), 'es': ('te miran',), 'pt': ('te olham',)},
    # Notifications (spec §9 K6, K7; the pack's words, «отчёт» for its «отчёт готов»).
    'request': {'ru': ('запрос',), 'en': ('request',), 'es': ('solicitud',), 'pt': ('pedido',)},
    'accepted': {'ru': ('принят',), 'en': ('accepted',), 'es': ('aceptado',), 'pt': ('aceito',)},
    'report': {'ru': ('отчёт',), 'en': ('report',), 'es': ('informe',), 'pt': ('relatório',)},
    'talks': {'ru': ('переговоры',), 'en': ('talks',), 'es': ('negociación',), 'pt': ('negociação',)},
    'door': {'ru': ('дверь',), 'en': ('door',), 'es': ('puerta',), 'pt': ('porta',)},
    'question': {'ru': ('вопрос',), 'en': ('question',), 'es': ('pregunta',), 'pt': ('pergunta',)},
}
# Words drawn in a speech bubble instead of the word row: the small font,
# left-aligned where the pack writes «ой», inside the bubble or the
# generator fails. They move with the whole wake shift and never move the
# glyph zone.
BUBBLE_WORDS = {'ouch': 38}   # word -> the dy of the frame that shows it (the bubble follows the head)
BUBBLE_MARK_FONT = 'u8g2_font_5x8_tf'
WORD_FONTS = {  # per locale: the pack's word font, then the smaller one when a line does not fit
    'ru': ('u8g2_font_6x13_t_cyrillic', 'u8g2_font_5x8_t_cyrillic'),
    'en': ('u8g2_font_6x13_tf', 'u8g2_font_5x8_tf'),
    'es': ('u8g2_font_6x13_tf', 'u8g2_font_5x8_tf'),
    'pt': ('u8g2_font_6x13_tf', 'u8g2_font_5x8_tf'),
}
WORD_BASELINES = (110, 124)   # layout A: the word row y 98-128, one or two lines
WORD_MAX_WIDTH = W - 4        # 2 px each side: the wake shift moves words across by up to 2 px


# ------------------------------------------------------------------ scenes
SCENE_WORD = 'scene'   # a frame's default word: its scene's


def F(ms, pose, dx=0, dy=0, squash=False, glyphs=(), overlays=(), mouth=None, scale=None, status=True,
      word=SCENE_WORD, whole=(), icons=(), marks=(), number=None, narrow=()):
    """marks: pieces of the status row (the notification dot); like the row
    they never move with the wake shift, and a frame without the row has
    none. number: where the pin draws the count (a glyph-zone piece), or
    None; narrow: glyph-zone pieces drawn only while the count has one digit."""
    if scale is None:
        scale = SQUASH if squash else PLAIN
    assert status or not marks, 'status marks need the status row'
    return dict(ms=ms, pose=pose, dx=dx, dy=dy, scale=scale, glyphs=list(glyphs), overlays=list(overlays),
                mouth=mouth, status=status, word=word, whole=list(whole), icons=list(icons), marks=list(marks),
                number=number, narrow=list(narrow))


def _fall_frames():
    """The pack's f_fall (k23-lost, approved as drawn on 2026-09-28): drops in
    with speed lines, squashes on the floor with > < eyes and dust, bounces,
    sees stars, says «ой», then «было / больно» with half eyes and a wavy
    mouth. No status row while it falls in from above the screen."""
    fl = [floor]
    return [
        F(70, 'wide', dy=-30, status=False, word=None, overlays=[speedlines(-30)]),
        F(70, 'wide', dy=-4, scale=(0.94, 1.08), status=False, word=None, overlays=[speedlines(-4)]),
        F(70, 'wide', dy=22, scale=(0.9, 1.12), word=None, whole=fl, overlays=[speedlines(22)]),
        F(110, 'squint', dy=44, scale=(1.3, 0.66), word=None, whole=fl, overlays=[dust(0)]),
        F(120, 'squint', dy=30, scale=(0.95, 1.06), word=None, whole=fl, overlays=[dust(1)]),
        F(140, 'squint', dy=39, scale=(1.08, 0.94), word=None, whole=fl, overlays=[stars(39, 0)]),
        F(700, 'squint', dy=38, word='ouch', whole=fl, overlays=[stars(38, 1), bubble(38)], icons=[bubble_mark(38)]),
        F(1400, 'half', dy=38, mouth='wavy', word='hurt', whole=fl, overlays=[stars(38, 2)]),
    ]


def _found_you_frames():
    """The pack's f_foundyou (k20, approved as drawn on 2026-09-28): the
    mascot jumps with wide eyes and «!», then smiles; a hand waves in the
    glyph zone and the status row carries the notification dot."""
    return [
        F(150, 'wide', dy=-3, glyphs=[hand()], overlays=[bang(True)], marks=[status_dot]),
        F(150, 'wide', glyphs=[hand(2)], overlays=[bang(False)], marks=[status_dot]),
        F(700, 'happy', glyphs=[hand()], marks=[status_dot]),
    ]


def _found_frames():
    """The pack's f_found (k05, approved as drawn on 2026-09-28): happy
    eyes, sparkles, a card slides up with the person, the count and the
    phone, then holds."""
    frames = []
    for i, rise in enumerate([0.0, 0.5, 1.0, 1.0]):
        frames.append(F(110 if i < 3 else 600, 'happy', dy=-2 if i == 2 else 0, glyphs=[card(rise)],
                        narrow=[card_phone(rise)], number=card_number(rise), overlays=[sparkle(i)]))
    return frames


def _found_self_frames():
    """The pack's f_found_self (a02): a self-wake, so no digit (spec §15):
    the person with a dot, the status row's dot."""
    return [F(800, 'happy', glyphs=[person_dot], marks=[status_dot]),
            F(400, 'happy', dy=-1, glyphs=[person_dot], overlays=[sparkle(1)], marks=[status_dot])]


def _notify_frames(icon):
    """The pack's f_notify (a06, a07, w01, w02): wide eyes, the kind's glyph
    with a dot, the status row's dot. One still frame."""
    return [F(800, 'wide', glyphs=[big_icon(icon, dot=True)], marks=[status_dot])]


def _report_frames():
    """The pack's f_report (a05): happy, the report bobs beside a sun."""
    return [F(500, 'happy', glyphs=[big_icon('doc', dx=-10, dy=d), big_icon('sun', dx=12, dy=-2, k=1)],
              marks=[status_dot]) for d in (0, -2)]


def _question_frames():
    """The pack's f_question (a04): tilted eyes, a «?» bubble that sways."""
    return [F(400, 'tilt', glyphs=[big_icon('question', dx=d)], marks=[status_dot]) for d in (0, 2)]


def _setup_frames():
    return [F(800, 'open', glyphs=[big_phone()]), F(400, 'up', glyphs=[big_phone(-2)])]


def _talk_frames():
    frames = []
    for i, m in enumerate(('o_small', 'o_big', 'o_mid', 'line')):
        dy = -1 if m == 'o_big' else 0
        frames.append(F(120, 'happy' if i == 3 else 'open', dy=dy, mouth=m, overlays=[speech(i * 1.5, dy)]))
    return frames


# flags: loop = the frames repeat (else the last one holds); glass = the
# outline material whatever the mascot; default_look = the default pebble
# (an unowned pin has no owner's mascot, spec §15); blank = nothing lit;
# still = no wake shift (a one-off drawn to the screen edge, too short to
# burn in). word: the WORDS entry the scene's frames show when the phone
# sent a locale (a frame may name its own). Scenes 0-7 are PR 1's (the
# press); 8-11 come from screen_state (PR 2/3); 12 is the fall (PR 5);
# 13 is «found you» (event_ack 0x11, the press-again notice); 14-17 are the
# agent's counts from a trusted screen_state (found with its digit, found
# on a self-wake, nobody, looking); 18-23 the notifications (notify 2-7).
SCENES = [
    dict(name='off', flags={'blank'}, word=None, frames=[F(1000, 'open')]),
    dict(name='nothing_known', flags={'loop'}, word=None, frames=[F(600, 'open'), F(120, 'blink')]),
    dict(name='pressed', flags=set(), word=None, frames=[F(100, 'closed', dy=4, squash=True)]),
    dict(name='searching', flags={'loop'}, word='searching',
         frames=[F(125, pose, glyphs=[radar(i)]) for i, pose in enumerate(['left', 'left', 'right', 'right'])]),
    dict(name='result', flags={'loop'}, word='see_phone',
         frames=[F(400, 'happy', glyphs=[look_at_phone(dx)], overlays=[sparkle(i)]) for i, dx in enumerate((-18, -15))]),
    dict(name='error', flags=set(), word='failed',
         frames=[F(ms, 'squint', dx=dx, overlays=[sweat]) for dx, ms in ((-2, 90), (2, 90), (0, 800))]),
    dict(name='no_link', flags={'glass'}, word='no_link', frames=[F(3000, 'flat', glyphs=[crossed_phone])]),
    dict(name='set_me_up', flags={'loop', 'default_look'}, word='open_app', frames=_setup_frames()),
    dict(name='watch', flags={'loop'}, word='watch',
         frames=[F(ms, pose, dy=dy, glyphs=[watch_dots(i % 3)])
                 for i, (pose, dy, ms) in enumerate([('open', 0, 400), ('open', 1, 400), ('blink', 1, 120),
                                                    ('open', 0, 400)])]),
    dict(name='paused', flags={'loop'}, word='paused',
         frames=[F(600, 'closed', dy=d, overlays=[zz(i)]) for i, d in enumerate([0, 1, 1])]),
    dict(name='needs_setup', flags={'loop'}, word='open_app', frames=_setup_frames()),
    dict(name='speaking', flags={'loop'}, word='speaking', frames=_talk_frames()),
    dict(name='fell', flags={'still'}, word=None, frames=_fall_frames()),
    dict(name='found_you', flags={'loop'}, word='found_you', frames=_found_you_frames()),
    dict(name='found', flags=set(), word='people', frames=_found_frames()),
    dict(name='found_self', flags={'loop'}, word='people', frames=_found_self_frames()),
    dict(name='nobody', flags={'loop'}, word='nobody',
         frames=[F(700, 'open', glyphs=[big_zero]), F(500, 'half', glyphs=[big_zero])]),
    dict(name='looking', flags={'loop'}, word='looking', frames=[F(800, 'wide', number=plus_number())]),
    dict(name='request', flags=set(), word='request', frames=_notify_frames('envelope')),
    dict(name='accepted', flags=set(), word='accepted', frames=_notify_frames('check')),
    dict(name='report', flags={'loop'}, word='report', frames=_report_frames()),
    dict(name='talks', flags=set(), word='talks', frames=_notify_frames('doc')),
    dict(name='door', flags=set(), word='door', frames=_notify_frames('door')),
    dict(name='question', flags={'loop'}, word='question', frames=_question_frames()),
]


def body_scales():
    """Every body scale the scenes use: plain first and the press squash
    second (their variant numbers predate the fall), then in scene order."""
    out = [PLAIN, SQUASH]
    for sc in SCENES:
        for f in sc['frames']:
            if f['scale'] not in out:
                out.append(f['scale'])
    return out


# Serial `o<letter>` on the bench (docs/pins/firmware.md). `m` is not a
# scene: the demo shows «on watch» in modest mode for it. `f` is no static
# demo: bench_serial.h turns `of` into a real fall (ScreenState::onFall).
DEMO_LETTERS = {'off': 'o', 'nothing_known': 'k', 'pressed': 'p', 'searching': 's', 'result': 'r',
                'error': 'e', 'no_link': 'l', 'set_me_up': 'u', 'watch': 'w', 'paused': 'z',
                'needs_setup': 'n', 'speaking': 't', 'fell': 'f', 'found_you': 'y', 'found': 'g',
                'found_self': 'j', 'nobody': 'b', 'looking': 'v', 'request': 'q', 'accepted': 'a',
                'report': 'd', 'talks': 'h', 'door': 'i', 'question': '?'}
DEMO_MODEST_LETTER = 'm'
