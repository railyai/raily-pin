// Composes every golden frame of tests/oled_golden/manifest.txt and writes
// the canvases as raw 1 KB files; check_oled_golden.py then compares them
// with the PNGs bit for bit. Also checks the panel rotation and clipping.
//
//   test_oled_compose <manifest.txt> <output dir>   (run_tests.sh does this)
#include <stdio.h>
#include <string.h>
#include "../RailyPinsP1/oled_compose.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

static bool canvasPixel(const uint8_t* canvas, int x, int y) {
  return (canvas[y * (OLED_W / 8) + x / 8] & (0x80 >> (x % 8))) != 0;
}

static bool pagePixel(const uint8_t* pages, int x, int y) {
  return (pages[(y / 8) * OLED_PANEL_W + x] >> (y % 8)) & 1;
}

static int litPixels(const uint8_t* canvas) {
  int n = 0;
  for (int i = 0; i < OLED_CANVAS_BYTES; i++) {
    for (int b = 0; b < 8; b++) n += (canvas[i] >> b) & 1;
  }
  return n;
}

static OledScratch scratch;

static void rotationTests() {
  static uint8_t canvas[OLED_CANVAS_BYTES];
  static uint8_t pages[OLED_PANEL_W * OLED_PANEL_PAGES];
  memset(canvas, 0, sizeof(canvas));
  oledCanvasSet(canvas, 3, 10);   // portrait (u 3, v 10)
  oledCanvasSet(canvas, 63, 127);
  oledToPages(canvas, OLED_ROTATION_R1, pages);
  // U8G2_R1: user (x, y) -> panel (height - 1 - y, x), height 128.
  expect(pagePixel(pages, 127 - 10, 3) && pagePixel(pages, 0, 63), "R1 maps portrait pixels like U8G2_R1");
  int lit = 0;
  for (int i = 0; i < (int)sizeof(pages); i++) lit += __builtin_popcount(pages[i]);
  expect(lit == 2, "R1 lights exactly the composed pixels");
  oledToPages(canvas, OLED_ROTATION_R3, pages);
  // U8G2_R3: user (x, y) -> panel (y, width - 1 - x), width 64.
  expect(pagePixel(pages, 10, 63 - 3) && pagePixel(pages, 127, 0), "R3 maps portrait pixels like U8G2_R3");
  uint8_t tile[8];
  oledTile(canvas, OLED_ROTATION_R3, 1, 7, tile);
  expect(memcmp(tile, pages + 7 * OLED_PANEL_W + 8, 8) == 0, "one tile equals its slice of the full buffer");
}

static void clipTests() {
  static uint8_t canvas[OLED_CANVAS_BYTES];
  ScreenFrame f = {};
  f.scene = OLED_SCENE_RESULT;
  f.linked = true;
  f.shiftX = 127;
  f.shiftY = -128;
  oledCompose(f, canvas, scratch);  // ASan catches any write outside the canvas
  expect(true, "extreme shifts clip inside the canvas");
  f.scene = OLED_SCENE_OFF;
  oledCompose(f, canvas, scratch);
  expect(litPixels(canvas) == 0, "off is a dark canvas");
  f.scene = OLED_SCENE_COUNT;
  oledCompose(f, canvas, scratch);
  expect(litPixels(canvas) == 0, "an unknown scene draws nothing");
  f.scene = OLED_SCENE_SEARCHING;
  f.frame = 200;
  f.shape = 99;
  f.material = 99;
  f.shiftX = 0;
  f.shiftY = 0;
  oledCompose(f, canvas, scratch);
  expect(litPixels(canvas) > 0, "out-of-range frame, shape and material fall back safely");
  f.shape = OLED_SHAPE_CUBE;
  f.material = OLED_MATERIAL_JELLY;
  f.scene = OLED_SCENE_SET_ME_UP;
  f.frame = 0;
  static uint8_t pebble[OLED_CANVAS_BYTES];
  oledCompose(f, canvas, scratch);
  f.shape = OLED_SHAPE_PEBBLE;
  f.material = OLED_MATERIAL_SATIN;
  oledCompose(f, pebble, scratch);
  expect(memcmp(canvas, pebble, sizeof(pebble)) == 0, "set me up always shows the default pebble");
  expect(canvasPixel(pebble, 2, 2), "the status row shows the link icon");
}

// The fall (PR 5): still, drawn from above the screen, no status row while
// it falls in, words only on the frames that have them.
static void fallTests() {
  static uint8_t canvas[OLED_CANVAS_BYTES];
  static uint8_t still[OLED_CANVAS_BYTES];
  ScreenFrame f = {};
  f.scene = OLED_SCENE_FELL;
  f.linked = true;
  f.locale = 1;
  for (uint8_t i = 0; i < kOledScenes[OLED_SCENE_FELL].frameCount; i++) {
    f.frame = i;
    f.shiftX = 0;
    f.shiftY = 0;
    oledCompose(f, still, scratch);
    f.shiftX = -2;
    f.shiftY = 2;
    oledCompose(f, canvas, scratch);
    if (memcmp(canvas, still, sizeof(still)) != 0) {
      expect(false, "the fall ignores the wake shift");
      return;
    }
  }
  expect(true, "the fall ignores the wake shift on every frame");
  f.frame = 0;
  f.shiftX = 0;
  f.shiftY = 0;
  oledCompose(f, canvas, scratch);  // the mascot starts 18 px above the screen: ASan checks the clip
  expect(!canvasPixel(canvas, 2, 2) && litPixels(canvas) > 0, "frame 1 falls in without the status row");
  f.frame = 2;
  oledCompose(f, canvas, scratch);
  expect(canvasPixel(canvas, 2, 2), "frame 3 has the status row back");
  // Frame 7: the bubble says «ой» in ru and «!» without a word locale.
  static uint8_t icons[OLED_CANVAS_BYTES];
  f.frame = 6;
  oledCompose(f, canvas, scratch);
  f.locale = 0;
  oledCompose(f, icons, scratch);
  expect(memcmp(canvas, icons, sizeof(icons)) != 0, "the bubble's text depends on the locale");
  f.locale = 5;  // ar: icons, as locale 0
  oledCompose(f, canvas, scratch);
  expect(memcmp(canvas, icons, sizeof(icons)) == 0, "ar draws the bubble's «!», as no locale");
  f.frame = 0;
  f.locale = 1;
  oledCompose(f, canvas, scratch);
  f.locale = 0;
  oledCompose(f, icons, scratch);
  expect(memcmp(canvas, icons, sizeof(icons)) == 0, "frame 1 has no word in any locale");
}

// The counts: a number only where a frame has a slot, 1-99 only.
static void countTests() {
  static uint8_t canvas[OLED_CANVAS_BYTES];
  static uint8_t none[OLED_CANVAS_BYTES];
  ScreenFrame f = {};
  f.scene = OLED_SCENE_WATCH;
  f.linked = true;
  oledCompose(f, none, scratch);
  f.count = 7;
  oledCompose(f, canvas, scratch);
  expect(memcmp(canvas, none, sizeof(none)) == 0, "a scene without a number slot ignores the count");
  f.scene = OLED_SCENE_LOOKING;
  f.count = 0;
  oledCompose(f, none, scratch);
  f.count = 100;
  oledCompose(f, canvas, scratch);
  expect(memcmp(canvas, none, sizeof(none)) == 0, "a count past 99 draws no number");
  f.count = SCREEN_COUNT_WITHHELD;
  oledCompose(f, canvas, scratch);
  expect(memcmp(canvas, none, sizeof(none)) == 0, "withheld draws no number");
  f.count = 42;
  f.shiftX = 127;
  f.shiftY = -128;
  oledCompose(f, canvas, scratch);  // ASan catches any write outside the canvas
  expect(true, "a number clips inside the canvas at extreme shifts");
}

static int goldenFrames(const char* manifestPath, const char* outDir) {
  FILE* manifest = fopen(manifestPath, "r");
  if (!manifest) {
    printf("FAIL cannot open %s\n", manifestPath);
    return -1;
  }
  static uint8_t canvas[OLED_CANVAS_BYTES];
  char line[256];
  int count = 0;
  while (fgets(line, sizeof(line), manifest)) {
    if (line[0] == '#' || line[0] == '\n') continue;
    char file[128];
    int scene, frame, shape, material, linked, sx, sy, locale, modest, number, dot;
    if (sscanf(line, "%127s %d %d %d %d %d %d %d %d %d %d %d", file, &scene, &frame, &shape, &material, &linked,
               &sx, &sy, &locale, &modest, &number, &dot) != 12) {
      printf("FAIL bad manifest line: %s", line);
      failures++;
      continue;
    }
    ScreenFrame f = {};
    f.scene = (uint8_t)scene;
    f.frame = (uint8_t)frame;
    f.shape = (uint8_t)shape;
    f.material = (uint8_t)material;
    f.linked = linked != 0;
    f.shiftX = (int8_t)sx;
    f.shiftY = (int8_t)sy;
    f.locale = (uint8_t)locale;
    f.modest = modest != 0;
    f.count = (uint8_t)number;
    f.dot = dot != 0;
    f.contrast = ScreenState::CONTRAST_NORMAL;
    oledCompose(f, canvas, scratch);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.bin", outDir, file);
    FILE* out = fopen(path, "wb");
    if (!out || fwrite(canvas, 1, sizeof(canvas), out) != sizeof(canvas)) {
      printf("FAIL cannot write %s\n", path);
      failures++;
    }
    if (out) fclose(out);
    count++;
  }
  fclose(manifest);
  return count;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <manifest.txt> <output dir>\n", argv[0]);
    return 2;
  }
  rotationTests();
  clipTests();
  fallTests();
  countTests();
  int count = goldenFrames(argv[1], argv[2]);
  expect(count > 0, "composed the golden frames");
  printf("composed %d golden frames\n", count);
  return failures ? 1 : 0;
}
