/* ssx_buttonicons.c -- the game's PlayStation face-button icons with the
 * Switch's letters, redrawn in memory as the game reads them from the APK
 * (ssx_assetpatch.c, ssx_assets.c). Nothing is written anywhere.
 *
 * The Android game knows one controller, "PLAYSTATION" (ssx_input.c), so its
 * hints -- the menus' button bar, the training, the races -- show Cross,
 * Circle, Square and Triangle. Each is its own little texture,
 * common/fe/assets/button_icons/button_ps3_<name>.etc1.pvr: a PVR v3 header
 * and 36x36 RGBA4444 pixels, a ring with the symbol inside. Here the ring is
 * kept and the symbol is replaced by the letter of the Switch button that
 * does that button's job (ssx_input.c), drawn in the symbol's own colour:
 *
 *                 swap_a_b = false (by function)   true (by position)
 *   Cross               A                                B
 *   Circle              B                                A
 *   Square              Y                                Y
 *   Triangle            X                                X
 *
 * The letters are drawn here from strokes (segments and half circles, 4x4
 * samples a pixel): no font, no image. An icon that is not what this expects
 * (APK 0.0.8430's) is served unchanged, and debug.log says so. The pictures
 * of a PlayStation pad (Settings > Controls, the loading screens) stay as
 * they are. MIT.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dcr_config.h"
#include "util.h"

#define ICON_DIR "common/fe/assets/button_icons/button_ps3_"
#define ICON_EXT ".etc1.pvr"
#define PVR_HEADER 52
#define ICON_SIZE 36

/* The Switch letter for a button_ps3_<name> icon, or 0: not one of the four. */
static char letter_for(const char *name) {
  size_t n = strlen(ICON_DIR), e = strlen(ICON_EXT), len = strlen(name);
  if (len <= n + e || strncmp(name, ICON_DIR, n) || strcmp(name + len - e, ICON_EXT))
    return 0;
  const char *b = name + n;
  size_t bl = len - n - e;
  const int swap = dcr_config()->swap_ab;
  if (bl == 1 && b[0] == 'x')
    return swap ? 'B' : 'A';
  if (bl == 6 && !strncmp(b, "circle", 6))
    return swap ? 'A' : 'B';
  if (bl == 6 && !strncmp(b, "square", 6))
    return 'Y';
  if (bl == 8 && !strncmp(b, "triangle", 8))
    return 'X';
  return 0;
}

int ssx_buttonicons_wants(const char *name) { return letter_for(name) != 0; }

/* A letter: up to six strokes, in pixels from the icon's centre (y down).
 * kind 's': a segment (x0,y0)-(x1,y1); 'a': the right half of a circle,
 * centre (x0,y0), radius x1. The strokes are STROKE wide, round-ended. */
typedef struct {
  char kind;
  float x0, y0, x1, y1;
} Stroke;

#define STROKE 2.5f
#define TOP (-5.5f + STROKE / 2) /* the strokes' centre lines: 11 px tall letters */
#define BOT (5.5f - STROKE / 2)

static int strokes_for(char ch, Stroke *s) {
  int n = 0;
  switch (ch) {
  case 'A':
    s[n++] = (Stroke){'s', 0.0f, TOP, -3.9f, BOT};
    s[n++] = (Stroke){'s', 0.0f, TOP, 3.9f, BOT};
    s[n++] = (Stroke){'s', -2.3f, 1.6f, 2.3f, 1.6f};
    break;
  case 'X':
    s[n++] = (Stroke){'s', -3.6f, TOP, 3.6f, BOT};
    s[n++] = (Stroke){'s', 3.6f, TOP, -3.6f, BOT};
    break;
  case 'Y':
    s[n++] = (Stroke){'s', -3.8f, TOP, 0.0f, 0.3f};
    s[n++] = (Stroke){'s', 3.8f, TOP, 0.0f, 0.3f};
    s[n++] = (Stroke){'s', 0.0f, 0.3f, 0.0f, BOT};
    break;
  case 'B': {
    const float x0 = -3.2f, mid = -0.35f, r1 = (mid - TOP) / 2, r2 = (BOT - mid) / 2, c1 = 0.3f, c2 = 0.6f;
    s[n++] = (Stroke){'s', x0, TOP, x0, BOT}; /* the stem */
    s[n++] = (Stroke){'s', x0, TOP, c1, TOP};
    s[n++] = (Stroke){'s', x0, mid, c2, mid};
    s[n++] = (Stroke){'s', x0, BOT, c2, BOT};
    s[n++] = (Stroke){'a', c1, TOP + r1, r1, 0};    /* the upper bowl */
    s[n++] = (Stroke){'a', c2, mid + r2, r2, 0};    /* the lower one */
    break;
  }
  }
  return n;
}

/* Is (x, y) inside a stroke? Squared distances: no square roots. */
static int inside(const Stroke *s, int n, float x, float y) {
  const float hw = STROKE / 2, hw2 = hw * hw;
  for (int i = 0; i < n; i++) {
    if (s[i].kind == 's') {
      float dx = s[i].x1 - s[i].x0, dy = s[i].y1 - s[i].y0, px = x - s[i].x0, py = y - s[i].y0;
      float t = (px * dx + py * dy) / (dx * dx + dy * dy);
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      float ex = px - t * dx, ey = py - t * dy;
      if (ex * ex + ey * ey <= hw2)
        return 1;
    } else {
      float cx = s[i].x0, cy = s[i].y0, r = s[i].x1, px = x - cx, py = y - cy, d2 = px * px + py * py;
      if (px >= 0) {
        float lo = r - hw, hi = r + hw;
        if (d2 >= lo * lo && d2 <= hi * hi)
          return 1;
      } else { /* the half circle's two ends */
        float a = py - r, b = py + r;
        if (px * px + a * a <= hw2 || px * px + b * b <= hw2)
          return 1;
      }
    }
  }
  return 0;
}

static uint16_t px_at(const uint8_t *p, int i) { return (uint16_t)(p[2 * i] | p[2 * i + 1] << 8); }

/* The redrawn icon (malloc'd, the same size), or NULL: serve the original. */
uint8_t *ssx_buttonicons_apply(const char *name, const uint8_t *data, uint32_t len, uint32_t *out_len) {
  const char ch = letter_for(name);
  if (!ch)
    return NULL;
  static const uint8_t k_rgba4444[8] = {'r', 'g', 'b', 'a', 4, 4, 4, 4};
  uint32_t h = 0, w = 0, meta = 1;
  if (len == PVR_HEADER + ICON_SIZE * ICON_SIZE * 2 && !memcmp(data, "PVR\3", 4)) {
    memcpy(&h, data + 24, 4);
    memcpy(&w, data + 28, 4);
    memcpy(&meta, data + 48, 4);
  }
  if (h != ICON_SIZE || w != ICON_SIZE || meta != 0 || memcmp(data + 8, k_rgba4444, 8)) {
    debugPrintf("[buttonicons] %s: not the expected 36x36 RGBA4444 icon, kept\n", name);
    return NULL;
  }
  const uint8_t *src = data + PVR_HEADER;
  /* The ring: the box around what is drawn, its centre and radius. */
  int x0 = ICON_SIZE, x1 = -1, y0 = ICON_SIZE, y1 = -1;
  for (int i = 0; i < ICON_SIZE * ICON_SIZE; i++)
    if ((px_at(src, i) & 15) >= 2) {
      int x = i % ICON_SIZE, y = i / ICON_SIZE;
      x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1, y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
    }
  const float cx = (x0 + x1 + 1) / 2.0f, cy = (y0 + y1 + 1) / 2.0f;
  const float r = ((x1 - x0 > y1 - y0 ? x1 - x0 : y1 - y0) + 1) / 2.0f;
  if (x1 < 0 || r < 9.0f || r > 14.0f) {
    debugPrintf("[buttonicons] %s: no ring found (radius %.1f), kept\n", name, (double)r);
    return NULL;
  }
  /* Inside the ring (its stroke is ~3 px): the symbol, cleared. Its colour,
   * weighted by its opacity, is the letter's. */
  const float clear = r - 2.9f, clear2 = clear * clear;
  unsigned sr = 0, sg = 0, sb = 0, sa = 0;
  for (int i = 0; i < ICON_SIZE * ICON_SIZE; i++) {
    float dx = i % ICON_SIZE + 0.5f - cx, dy = i / ICON_SIZE + 0.5f - cy;
    uint16_t v = px_at(src, i);
    unsigned a = v & 15;
    if (dx * dx + dy * dy < clear2 && a >= 4)
      sr += (v >> 12 & 15) * a, sg += (v >> 8 & 15) * a, sb += (v >> 4 & 15) * a, sa += a;
  }
  unsigned rgb = 0;
  if (sa)
    rgb = (sr + sa / 2) / sa << 8 | (sg + sa / 2) / sa << 4 | (sb + sa / 2) / sa;
  else /* no symbol pixels: the ring's colour, from its brightest-opacity pixel */
    for (int i = 0, best = -1; i < ICON_SIZE * ICON_SIZE; i++)
      if ((int)(px_at(src, i) & 15) > best)
        best = px_at(src, i) & 15, rgb = px_at(src, i) >> 4;
  Stroke s[6];
  const int ns = strokes_for(ch, s);
  uint8_t *out = malloc(len);
  if (!out)
    return NULL;
  memcpy(out, data, len);
  uint8_t *dst = out + PVR_HEADER;
  for (int i = 0; i < ICON_SIZE * ICON_SIZE; i++) {
    float px = i % ICON_SIZE - cx, py = i / ICON_SIZE - cy; /* the pixel's top-left corner */
    float dx = px + 0.5f, dy = py + 0.5f;
    if (dx * dx + dy * dy >= clear2)
      continue;
    int hits = 0;
    for (int sy = 0; sy < 4; sy++)
      for (int sx = 0; sx < 4; sx++)
        hits += inside(s, ns, px + (sx + 0.5f) / 4, py + (sy + 0.5f) / 4);
    unsigned a = (unsigned)(hits * 15 + 8) / 16;
    uint16_t v = a ? (uint16_t)(rgb << 4 | a) : 0;
    dst[2 * i] = (uint8_t)v, dst[2 * i + 1] = (uint8_t)(v >> 8);
  }
  static unsigned logged;
  if (!(logged & 1u << (ch - 'A'))) {
    logged |= 1u << (ch - 'A');
    debugPrintf("[buttonicons] %s: drawn as %c (swap_a_b %s)\n", name, ch, dcr_config()->swap_ab ? "on" : "off");
  }
  *out_len = len;
  return out;
}
