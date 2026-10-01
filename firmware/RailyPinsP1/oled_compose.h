#pragma once

#include <stdint.h>
#include <string.h>
#include "oled_assets.h"
#include "screen_state.h"

// Pure composer: ScreenFrame -> 64 x 128 portrait canvas (1 KB, row-major,
// 8 bytes a row, the most significant bit is the leftmost pixel), then the
// canvas -> U8g2's SSD1306 full-buffer page layout. No Arduino, no heap:
// host tests compare every scene with tests/oled_golden/ bit for bit, and
// the Python reference in tools/gen_oled_assets.py draws those goldens from
// the same tables.

static const uint16_t OLED_CANVAS_BYTES = OLED_W / 8 * OLED_H;
static const uint8_t OLED_PANEL_W = 128;  // the SSD1306's own axes
static const uint8_t OLED_PANEL_PAGES = 8;

// The glass's long side runs along the keyring (spec §1); which end is up is
// checked on the bench. Same mappings as U8G2_R1 / U8G2_R3 (u8g2_setup.c):
// R1 portrait (u, v) -> panel (127 - v, u); R3 -> panel (v, 63 - u).
static const uint8_t OLED_ROTATION_R1 = 1;
static const uint8_t OLED_ROTATION_R3 = 3;

// 48 rows of 48 bits, bit x = pixel x. The caller owns it (static in the
// renderer), so composing needs no loop-task stack for the body.
struct OledScratch {
  uint64_t mask[OLED_BODY];
  uint64_t body[OLED_BODY];
  uint64_t eroded[OLED_BODY];
};

static const uint64_t OLED_ROW_BITS = (1ULL << OLED_BODY) - 1;

static inline void oledCanvasSet(uint8_t* canvas, int x, int y) {
  if (x < 0 || y < 0 || x >= OLED_W || y >= OLED_H) return;
  canvas[y * (OLED_W / 8) + x / 8] |= (uint8_t)(0x80 >> (x % 8));
}

static inline bool oledSpritePixel(const OledSprite& s, int x, int y) {
  const uint8_t* row = kOledSpriteBits + s.offset + y * ((s.w + 7) / 8);
  return (row[x / 8] & (0x80 >> (x % 8))) != 0;
}

static inline void oledBlitCanvas(uint8_t* canvas, uint8_t sprite, int x, int y) {
  const OledSprite& s = kOledSprites[sprite];
  for (int j = 0; j < s.h; j++) {
    for (int i = 0; i < s.w; i++) {
      if (oledSpritePixel(s, i, j)) oledCanvasSet(canvas, x + i, y + j);
    }
  }
}

static inline void oledBodyPut(uint64_t* rows, int x, int y, bool lit) {
  if (x < 0 || y < 0 || x >= OLED_BODY || y >= OLED_BODY) return;
  if (lit) {
    rows[y] |= 1ULL << x;
  } else {
    rows[y] &= ~(1ULL << x);
  }
}

// A row's 1 x 3 minimum. Pixels past the 48 px box are ignored, as by
// Pillow's MinFilter in the pack (only the fall's widest squash reaches it).
static inline uint64_t oledRowMin(uint64_t r) {
  return r & ((r << 1) | 1ULL) & ((r >> 1) | (1ULL << (OLED_BODY - 1))) & OLED_ROW_BITS;
}

// 3 x 3 minimum; rows past the box are ignored too.
static inline void oledErode(const uint64_t* in, uint64_t* out) {
  uint64_t above = OLED_ROW_BITS;
  uint64_t here = oledRowMin(in[0]);
  for (int y = 0; y < OLED_BODY; y++) {
    uint64_t below = y + 1 < OLED_BODY ? oledRowMin(in[y + 1]) : OLED_ROW_BITS;
    out[y] = above & here & below;
    above = here;
    here = below;
  }
}

// satin: filled, a 1 px engraved ring kept on the upper-left half (the
// site's glossy edge); jelly: 50 % dither inside a solid rim; glass: rim.
static inline void oledMaterial(OledScratch& s, uint8_t material) {
  const uint64_t* m = s.mask;
  if (material == OLED_MATERIAL_SATIN) {
    oledErode(m, s.eroded);
    for (int y = 0; y < OLED_BODY; y++) {
      const uint64_t* e = s.eroded;
      uint64_t h0 = y > 0 ? oledRowMin(e[y - 1]) : OLED_ROW_BITS;
      uint64_t h1 = oledRowMin(e[y]);
      uint64_t h2 = y + 1 < OLED_BODY ? oledRowMin(e[y + 1]) : OLED_ROW_BITS;
      uint64_t ring = e[y] & ~(h0 & h1 & h2);
      int limit = 40 - y;  // engraved where x + y <= 40
      uint64_t upperLeft = limit < 0 ? 0 : ((1ULL << (limit + 1)) - 1) & OLED_ROW_BITS;
      s.body[y] = m[y] & ~(ring & upperLeft);
    }
    return;
  }
  for (int y = 0; y < OLED_BODY; y++) {
    uint64_t up = y > 0 ? m[y - 1] : 0;
    uint64_t down = y + 1 < OLED_BODY ? m[y + 1] : 0;
    uint64_t edge = m[y] & ~(m[y] & (m[y] << 1) & (m[y] >> 1) & up & down);
    uint64_t checker = (y % 2 == 0) ? 0x555555555555ULL : 0xAAAAAAAAAAAAULL;  // (x + y) even
    s.body[y] = material == OLED_MATERIAL_JELLY ? (edge | (m[y] & checker)) : edge;
  }
}

static inline void oledMascot(OledScratch& s, uint8_t shape, uint8_t variant, uint8_t material, uint8_t pose,
                              uint8_t mouth = 0) {
  // The body's row spans: per row a run count, then x0, x1 (inclusive).
  const uint8_t* src = kOledBodySpans + kOledBodyOffsets[shape][variant];
  for (int y = 0; y < OLED_BODY; y++) {
    uint64_t row = 0;
    for (uint8_t runs = *src++; runs > 0; runs--) {
      uint8_t x0 = *src++;
      uint8_t x1 = *src++;
      row |= ((1ULL << (x1 - x0 + 1)) - 1) << x0;
    }
    s.mask[y] = row & OLED_ROW_BITS;
  }
  oledMaterial(s, material);
  const OledPose& p = kOledPoses[pose];
  for (int eye = 0; eye < 2; eye++) {
    const OledEyeSlot& slot = kOledEyeSlots[shape][variant][eye];
    int px = slot.probeX + p.lookX;
    int py = slot.probeY + p.lookY;
    px = px < 0 ? 0 : (px >= OLED_BODY ? OLED_BODY - 1 : px);
    py = py < 0 ? 0 : (py >= OLED_BODY ? OLED_BODY - 1 : py);
    bool inside = (s.mask[py] >> px) & 1;
    if (material == OLED_MATERIAL_JELLY && inside) {  // a solid patch so the dither does not eat the eye
      for (int y = slot.patch[1]; y <= slot.patch[3]; y++) {
        for (int x = slot.patch[0]; x <= slot.patch[2]; x++) oledBodyPut(s.body, x + p.lookX, y + p.lookY, true);
      }
    }
    // Dark eyes on a lit body, lit eyes on glass or off the body.
    bool lit = !(inside && material != OLED_MATERIAL_GLASS);
    uint8_t sprite = p.eye[eye];
    const OledSprite& sp = kOledSprites[sprite];
    int x0 = slot.topLeft[sprite][0] + p.lookX;
    int y0 = slot.topLeft[sprite][1] + p.lookY;
    for (int j = 0; j < sp.h; j++) {
      for (int i = 0; i < sp.w; i++) {
        if (oledSpritePixel(sp, i, j)) oledBodyPut(s.body, x0 + i, y0 + j, lit);
      }
    }
  }
  if (mouth == 0 || mouth > OLED_MOUTH_COUNT) return;
  // The mouth, like the eyes: dark on a lit body, lit on glass or off it.
  const OledMouthSlot& ms = kOledMouthSlots[shape][variant];
  int px = ms.probeX + p.lookX;
  int py = ms.probeY + p.lookY;
  px = px < 0 ? 0 : (px >= OLED_BODY ? OLED_BODY - 1 : px);
  py = py < 0 ? 0 : (py >= OLED_BODY ? OLED_BODY - 1 : py);
  bool inside = (s.mask[py] >> px) & 1;
  const OledSprite& sp = kOledSprites[OLED_MOUTH_SPRITE_BASE + mouth - 1];
  int x0 = ms.topLeft[mouth - 1][0] + p.lookX;
  int y0 = ms.topLeft[mouth - 1][1] + p.lookY;
  if (material == OLED_MATERIAL_JELLY && inside) {  // a solid patch one pixel around it
    for (int y = y0 - 1; y <= y0 + sp.h; y++) {
      for (int x = x0 - 1; x <= x0 + sp.w; x++) oledBodyPut(s.body, x, y, true);
    }
  }
  bool lit = !(inside && material != OLED_MATERIAL_GLASS);
  for (int j = 0; j < sp.h; j++) {
    for (int i = 0; i < sp.w; i++) {
      if (oledSpritePixel(sp, i, j)) oledBodyPut(s.body, x0 + i, y0 + j, lit);
    }
  }
}

static inline void oledCompose(const ScreenFrame& f, uint8_t* canvas, OledScratch& s) {
  memset(canvas, 0, OLED_CANVAS_BYTES);
  if (f.scene >= OLED_SCENE_COUNT) return;
  const OledSceneDesc& scene = kOledScenes[f.scene];
  if ((scene.flags & OLED_SCENE_BLANK) || scene.frameCount == 0) return;
  uint8_t shape = f.shape < OLED_SHAPE_COUNT ? f.shape : OLED_SHAPE_PEBBLE;
  uint8_t material = f.material < OLED_MATERIAL_COUNT ? f.material : OLED_MATERIAL_SATIN;
  if (scene.flags & OLED_SCENE_DEFAULT_LOOK) {
    shape = OLED_SHAPE_PEBBLE;
    material = OLED_MATERIAL_SATIN;
  }
  if (scene.flags & OLED_SCENE_GLASS) material = OLED_MATERIAL_GLASS;
  uint8_t frameIndex = f.frame < scene.frameCount ? f.frame : scene.frameCount - 1;
  const OledFrameDesc& frame = kOledFrames[scene.firstFrame + frameIndex];
  // A still scene (the fall) runs to the screen edge and lasts seconds:
  // no wake shift.
  const int shiftX = (scene.flags & OLED_SCENE_STILL) ? 0 : f.shiftX;
  const int shiftY = (scene.flags & OLED_SCENE_STILL) ? 0 : f.shiftY;

  // Words only for a frame that has one, in a locale with strips (ru, en,
  // es, pt); modest mode (spec §15) draws the mascot and its pose only: no
  // status row, no glyphs, no words.
  const bool words = !f.modest && f.locale >= 1 && f.locale <= OLED_WORD_LOCALES && frame.word < OLED_WORD_COUNT;
  const bool bubble = words && (kOledWordFlags[frame.word] & OLED_WORD_BUBBLE);
  if (!f.modest && !(frame.flags & OLED_FRAME_NO_STATUS)) {
    const uint16_t* status = kOledStatus[f.linked ? 1 : 0];
    for (uint16_t i = 0; i < status[1]; i++) {
      const OledPlacement& p = kOledPlacements[status[0] + i];
      oledBlitCanvas(canvas, p.sprite, p.x, p.y);  // the status row never shifts
    }
    if (f.dot) {  // an unseen notification (spec §12)
      const uint16_t* dot = kOledStatus[2];
      for (uint16_t i = 0; i < dot[1]; i++) {
        const OledPlacement& p = kOledPlacements[dot[0] + i];
        oledBlitCanvas(canvas, p.sprite, p.x, p.y);
      }
    }
  }

  oledMascot(s, shape, frame.variant, material, frame.pose, frame.mouth);
  int mx = OLED_MASCOT_X + shiftX + frame.dx;
  int my = OLED_MASCOT_Y + shiftY + frame.dy;
  for (int y = 0; y < OLED_BODY; y++) {
    uint64_t row = s.body[y];
    for (int x = 0; row != 0 && x < OLED_BODY; x++, row >>= 1) {
      if (row & 1) oledCanvasSet(canvas, mx + x, my + y);
    }
  }
  if (f.modest) return;

  // A count of 1-99 at the frame's number slot; 0 (none, or withheld)
  // draws no number. Two digits take the room of the one-digit pieces.
  const bool number = frame.number != OLED_NO_NUMBER && f.count >= 1 && f.count <= OLED_COUNT_MAX;
  const bool twoDigits = number && f.count >= 10;
  for (uint16_t i = 0; i < frame.placementCount; i++) {
    const OledPlacement& p = kOledPlacements[frame.firstPlacement + i];
    if (words && (p.flags & OLED_PLACE_ICONS)) continue;  // the word says it instead
    if (twoDigits && (p.flags & OLED_PLACE_ONE_DIGIT)) continue;
    int dx = (p.flags & OLED_PLACE_SHIFTED) ? shiftX : 0;
    int dy = (p.flags & OLED_PLACE_SHIFTED) ? shiftY : 0;
    if (words && !bubble && (p.flags & OLED_PLACE_ZONE)) dy += OLED_WORD_ZONE_DY;
    oledBlitCanvas(canvas, p.sprite, p.x + dx, p.y + dy);
  }
  if (number) {
    // A glyph-zone piece: it moves with the wake shift and up for a row word.
    const OledNumberSlot& slot = kOledNumbers[frame.number];
    const OledDigitGlyph* glyphs = kOledDigitFonts[slot.font];
    const int dy = shiftY + (words && !bubble ? OLED_WORD_ZONE_DY : 0);
    int pen = twoDigits ? slot.x2 : slot.x;
    uint8_t chars[3];
    uint8_t n = 0;
    if (slot.flags & OLED_NUMBER_PLUS) chars[n++] = OLED_DIGIT_PLUS;
    if (twoDigits) chars[n++] = (uint8_t)(f.count / 10);
    chars[n++] = (uint8_t)(f.count % 10);
    for (uint8_t i = 0; i < n; i++) {
      const OledDigitGlyph& g = glyphs[chars[i]];
      oledBlitCanvas(canvas, g.sprite, pen + g.dx + shiftX, slot.base + g.dy + dy);
      pen += kOledDigitAdvance[slot.font];
    }
  }
  if (words) {
    // A row word moves across with the wake shift, never up or down (the
    // row sits on the edge); a bubble word moves with its bubble.
    const OledWordStrip& w = kOledWords[frame.word][f.locale - 1];
    oledBlitCanvas(canvas, w.sprite, w.x + shiftX, w.y + (bubble ? shiftY : 0));
  }
}

// One 8 x 8 tile of the panel in U8g2's full-buffer layout: byte i is
// panel column 8 * tileX + i, bit n is panel row 8 * tileY + n.
static inline void oledTile(const uint8_t* canvas, uint8_t rotation, uint8_t tileX, uint8_t tileY, uint8_t out[8]) {
  for (int i = 0; i < 8; i++) {
    int panelX = tileX * 8 + i;
    uint8_t byte = 0;
    for (int bit = 0; bit < 8; bit++) {
      int panelY = tileY * 8 + bit;
      int u = rotation == OLED_ROTATION_R1 ? panelY : (OLED_W - 1 - panelY);
      int v = rotation == OLED_ROTATION_R1 ? (OLED_PANEL_W - 1 - panelX) : panelX;
      if (canvas[v * (OLED_W / 8) + u / 8] & (0x80 >> (u % 8))) byte |= (uint8_t)(1 << bit);
    }
    out[i] = byte;
  }
}

// The whole canvas as U8g2's 1 KB buffer (8 pages x 128 columns).
static inline void oledToPages(const uint8_t* canvas, uint8_t rotation, uint8_t* buffer) {
  for (uint8_t ty = 0; ty < OLED_PANEL_PAGES; ty++) {
    for (uint8_t tx = 0; tx < OLED_PANEL_W / 8; tx++) {
      oledTile(canvas, rotation, tx, ty, buffer + ty * OLED_PANEL_W + tx * 8);
    }
  }
}
