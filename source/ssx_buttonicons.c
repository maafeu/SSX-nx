/* ssx_buttonicons.c -- the game's PlayStation button icons with the Switch's
 * buttons, redrawn in memory as the game reads them from the APK
 * (ssx_assetpatch.c, ssx_assets.c). Nothing is written anywhere.
 *
 * The Android game knows one controller, "PLAYSTATION" (ssx_input.c), so its
 * hints and its Help pages show a PlayStation pad's buttons. Each is its own
 * little texture, common/fe/assets/button_icons/button_ps3_<name>.etc1.pvr: a
 * PVR v3 header and RGBA4444 pixels. Here each becomes the Switch button that
 * does that button's job (ssx_input.c):
 *
 *   icon (size)            drawn as                     how
 *   Cross (36x36)          A, or B with swap_a_b        the ring kept, the
 *   Circle                 B, or A with swap_a_b        symbol inside replaced
 *   Square                 Y                            by the letter, in the
 *   Triangle               X                            symbol's own colour
 *   L1, R1 (44x44)         L, R                         the box kept, its
 *   L2, R2 (44x44)         ZL, ZR                       text replaced
 *   Start, Select (36x36)  +, -                         a ring, as the face
 *                                                       buttons', with + / -
 *
 * The letters and signs are drawn here from strokes (segments and half
 * circles, 4x4 samples a pixel): no font, no image. An icon that is not what
 * this expects (APK 0.0.8430's: its size, format and where its ring or box
 * is) is served unchanged, and debug.log says so. The sticks' icons (L, R in
 * a circle, as the Switch's) and the pictures of a PlayStation pad (Settings
 * > Controls, the loading screens) stay as they are. MIT.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dcr_config.h"
#include "util.h"

#define ICON_DIR "common/fe/assets/button_icons/button_ps3_"
#define ICON_EXT ".etc1.pvr"
#define PVR_HEADER 52

typedef enum { FACE, BOX, ROUND } Kind;

/* The face icons' symbol colour (RGB444), for the + and - drawn in a new ring. */
#define SYMBOL_RGB 0x278u

typedef struct {
  const char *name;
  Kind kind;
  int size;
  const char *text, *text_swapped; /* with swap_a_b */
} Icon;

static const Icon k_icons[] = {
    {"x", FACE, 36, "A", "B"},     {"circle", FACE, 36, "B", "A"}, {"square", FACE, 36, "Y", "Y"},
    {"triangle", FACE, 36, "X", "X"}, {"l1", BOX, 44, "L", "L"},    {"r1", BOX, 44, "R", "R"},
    {"l2", BOX, 44, "ZL", "ZL"},   {"r2", BOX, 44, "ZR", "ZR"},     {"start", ROUND, 36, "+", "+"},
    {"select", ROUND, 36, "-", "-"},
};

static const Icon *icon_for(const char *name) {
  size_t n = strlen(ICON_DIR), e = strlen(ICON_EXT), len = strlen(name);
  if (len <= n + e || strncmp(name, ICON_DIR, n) || strcmp(name + len - e, ICON_EXT))
    return NULL;
  for (size_t i = 0; i < sizeof k_icons / sizeof *k_icons; i++)
    if (strlen(k_icons[i].name) == len - n - e && !strncmp(name + n, k_icons[i].name, len - n - e))
      return &k_icons[i];
  return NULL;
}

int ssx_buttonicons_wants(const char *name) { return icon_for(name) != NULL; }

/* Strokes, in pixels from where the text is centred (y down): 's' a segment
 * (x0,y0)-(x1,y1) with round ends, 'b' one with square ends, 'a' the right
 * half of a circle, centre (x0,y0), radius x1. All are `w` wide. */
typedef struct {
  char kind;
  float x0, y0, x1, y1, w;
} Stroke;

#define MAX_STROKES 16

typedef struct {
  Stroke s[MAX_STROKES];
  int n;
} Shape;

static void add(Shape *sh, char kind, float x0, float y0, float x1, float y1, float w) {
  if (sh->n < MAX_STROKES)
    sh->s[sh->n++] = (Stroke){kind, x0, y0, x1, y1, w};
}

/* The face letters: 11 px tall, 2.5 px strokes, centred on (0, 0). */
static void face_letter(Shape *sh, char ch) {
  const float w = 2.5f, top = -5.5f + w / 2, bot = 5.5f - w / 2;
  switch (ch) {
  case 'A':
    add(sh, 's', 0.0f, top, -3.9f, bot, w);
    add(sh, 's', 0.0f, top, 3.9f, bot, w);
    add(sh, 's', -2.3f, 1.6f, 2.3f, 1.6f, w);
    break;
  case 'X':
    add(sh, 's', -3.6f, top, 3.6f, bot, w);
    add(sh, 's', 3.6f, top, -3.6f, bot, w);
    break;
  case 'Y':
    add(sh, 's', -3.8f, top, 0.0f, 0.3f, w);
    add(sh, 's', 3.8f, top, 0.0f, 0.3f, w);
    add(sh, 's', 0.0f, 0.3f, 0.0f, bot, w);
    break;
  case 'B': {
    const float x0 = -3.2f, mid = -0.35f, r1 = (mid - top) / 2, r2 = (bot - mid) / 2, c1 = 0.3f, c2 = 0.6f;
    add(sh, 's', x0, top, x0, bot, w); /* the stem */
    add(sh, 's', x0, top, c1, top, w);
    add(sh, 's', x0, mid, c2, mid, w);
    add(sh, 's', x0, bot, c2, bot, w);
    add(sh, 'a', c1, top + r1, r1, 0, w); /* the upper bowl */
    add(sh, 'a', c2, mid + r2, r2, 0, w); /* the lower one */
    break;
  }
  }
}

/* A box letter (L, R, Z), its ink from x to x + its width, h tall, centred on
 * y = 0, square-ended strokes w wide. Returns its width. */
static float box_letter(Shape *sh, char ch, float x, float h, float w) {
  const float hw = w / 2, top = -h / 2 + hw, bot = h / 2 - hw;
  switch (ch) {
  case 'L': {
    const float width = 0.62f * h;
    add(sh, 'b', x + hw, top, x + hw, bot, w);
    add(sh, 'b', x + hw, bot, x + width - hw, bot, w);
    return width;
  }
  case 'Z': {
    const float width = 0.66f * h;
    add(sh, 'b', x + hw, top, x + width - hw, top, w);
    add(sh, 'b', x + hw, bot, x + width - hw, bot, w);
    add(sh, 's', x + width - hw - 0.2f, top, x + hw + 0.2f, bot, w);
    return width;
  }
  case 'R': {
    const float width = 0.72f * h, mid = top + 0.55f * (bot - top), r = (mid - top) / 2, cx = x + width - hw - r;
    add(sh, 'b', x + hw, top, x + hw, bot, w); /* the stem */
    add(sh, 'b', x + hw, top, cx, top, w);
    add(sh, 'b', x + hw, mid, cx, mid, w);
    add(sh, 'a', cx, top + r, r, 0, w);                 /* the bowl */
    add(sh, 's', cx - 0.6f, mid, x + width - hw, bot, w); /* the leg */
    return width;
  }
  }
  return 0;
}

/* Is (x, y) inside a stroke? Squared distances: no square roots. */
static int inside(const Shape *sh, float x, float y) {
  for (int i = 0; i < sh->n; i++) {
    const Stroke *s = &sh->s[i];
    const float hw = s->w / 2, hw2 = hw * hw;
    if (s->kind == 'a') {
      float px = x - s->x0, py = y - s->y0, r = s->x1, d2 = px * px + py * py;
      if (px >= 0) {
        float lo = r - hw, hi = r + hw;
        if (d2 >= lo * lo && d2 <= hi * hi)
          return 1;
      } else { /* the half circle's two ends */
        float a = py - r, b = py + r;
        if (px * px + a * a <= hw2 || px * px + b * b <= hw2)
          return 1;
      }
      continue;
    }
    float dx = s->x1 - s->x0, dy = s->y1 - s->y0, px = x - s->x0, py = y - s->y0, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? (px * dx + py * dy) / l2 : 0;
    if (s->kind == 'b') { /* square ends: up to half a width past each end */
      if (l2 <= 0)
        continue;
      float ex = px - t * dx, ey = py - t * dy, over = t < 0 ? t : t > 1 ? t - 1 : 0;
      if (over * over * l2 <= hw2 && ex * ex + ey * ey <= hw2)
        return 1;
      continue;
    }
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float ex = px - t * dx, ey = py - t * dy;
    if (ex * ex + ey * ey <= hw2)
      return 1;
  }
  return 0;
}

/* An annulus, radii lo..hi around (0, 0). */
static int in_ring(float x, float y, float lo, float hi) {
  float d2 = x * x + y * y;
  return d2 >= lo * lo && d2 <= hi * hi;
}

static uint16_t px_at(const uint8_t *p, int i) { return (uint16_t)(p[2 * i] | p[2 * i + 1] << 8); }
static void px_put(uint8_t *p, int i, uint16_t v) { p[2 * i] = (uint8_t)v, p[2 * i + 1] = (uint8_t)(v >> 8); }

/* The box around the pixels at least `amin` opaque. */
static void bbox(const uint8_t *src, int size, unsigned amin, int *x0, int *x1, int *y0, int *y1) {
  *x0 = *y0 = size, *x1 = *y1 = -1;
  for (int i = 0; i < size * size; i++)
    if ((px_at(src, i) & 15) >= amin) {
      int x = i % size, y = i / size;
      *x0 = x < *x0 ? x : *x0, *x1 = x > *x1 ? x : *x1, *y0 = y < *y0 ? y : *y0, *y1 = y > *y1 ? y : *y1;
    }
}

/* The opacity-weighted colour (RGB444) of the pixels in a region with green at
 * least gmin (the brighter symbol or text, not its darker ring or box), or
 * `fallback`. The region: inside radius `r` around (cx, cy) when r > 0, else
 * the rectangle rx0..rx1, ry0..ry1. */
static unsigned symbol_rgb(const uint8_t *src, int size, float cx, float cy, float r, int rx0, int rx1, int ry0,
                           int ry1, unsigned gmin, unsigned fallback) {
  unsigned sr = 0, sg = 0, sb = 0, sa = 0;
  for (int i = 0; i < size * size; i++) {
    int x = i % size, y = i / size;
    float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
    if (r > 0 ? dx * dx + dy * dy >= r * r : (x < rx0 || x > rx1 || y < ry0 || y > ry1))
      continue;
    uint16_t v = px_at(src, i);
    unsigned a = v & 15;
    if (a >= 4 && (v >> 8 & 15) >= gmin)
      sr += (v >> 12 & 15) * a, sg += (v >> 8 & 15) * a, sb += (v >> 4 & 15) * a, sa += a;
  }
  if (!sa)
    return fallback;
  return (sr + sa / 2) / sa << 8 | (sg + sa / 2) / sa << 4 | (sb + sa / 2) / sa;
}

/* The commonest colour of the fully opaque pixels (the ring's or box's). */
static unsigned main_rgb(const uint8_t *src, int size) {
  static uint16_t count[4096];
  memset(count, 0, sizeof count);
  unsigned best = 0;
  for (int i = 0; i < size * size; i++)
    if ((px_at(src, i) & 15) == 15) {
      unsigned c = px_at(src, i) >> 4;
      if (++count[c] > count[best])
        best = c;
    }
  return best;
}

/* Of a pixel whose top-left corner is (px, py) in the shape's coordinates,
 * 4x4 samples: how many fall in the shape, and (with ring_lo > 0) how many
 * of the rest in the ring. */
static void coverage(const Shape *sh, float px, float py, float ring_lo, float ring_hi, int *hits, int *ring) {
  *hits = *ring = 0;
  for (int sy = 0; sy < 4; sy++)
    for (int sx = 0; sx < 4; sx++) {
      float x = px + (sx + 0.5f) / 4, y = py + (sy + 0.5f) / 4;
      if (inside(sh, x, y))
        ++*hits;
      else if (ring_lo > 0 && in_ring(x, y, ring_lo, ring_hi))
        ++*ring;
    }
}

/* The redrawn icon (malloc'd, the same size), or NULL: serve the original. */
uint8_t *ssx_buttonicons_apply(const char *name, const uint8_t *data, uint32_t len, uint32_t *out_len) {
  const Icon *ic = icon_for(name);
  if (!ic)
    return NULL;
  const int size = ic->size;
  static const uint8_t k_rgba4444[8] = {'r', 'g', 'b', 'a', 4, 4, 4, 4};
  uint32_t h = 0, w = 0, meta = 1;
  if (len == (uint32_t)(PVR_HEADER + size * size * 2) && !memcmp(data, "PVR\3", 4)) {
    memcpy(&h, data + 24, 4);
    memcpy(&w, data + 28, 4);
    memcpy(&meta, data + 48, 4);
  }
  if (h != (uint32_t)size || w != (uint32_t)size || meta != 0 || memcmp(data + 8, k_rgba4444, 8)) {
    debugPrintf("[buttonicons] %s: not the expected %dx%d RGBA4444 icon, kept\n", name, size, size);
    return NULL;
  }
  const char *text = dcr_config()->swap_ab ? ic->text_swapped : ic->text;
  const uint8_t *src = data + PVR_HEADER;
  int x0, x1, y0, y1;
  bbox(src, size, 2, &x0, &x1, &y0, &y1);
  Shape sh = {0};
  float cx, cy;                     /* where the shape is centred */
  int cx0, cx1, cy0, cy1;           /* the pixels redrawn: a rectangle ... */
  float clear_r = 0;                /* ... or, when > 0, a disc around (cx, cy) */
  float ring_lo = 0, ring_hi = 0;   /* ROUND: the new ring */
  unsigned rgb, ring_rgb = 0;
  if (ic->kind == FACE) {
    /* The ring: the box around what is drawn. Inside its ~3 px stroke: the
     * symbol, cleared; the letter takes the symbol's colour. */
    cx = (x0 + x1 + 1) / 2.0f, cy = (y0 + y1 + 1) / 2.0f;
    float r = ((x1 - x0 > y1 - y0 ? x1 - x0 : y1 - y0) + 1) / 2.0f;
    if (x1 < 0 || r < 9.0f || r > 14.0f) {
      debugPrintf("[buttonicons] %s: no ring found (radius %.1f), kept\n", name, (double)r);
      return NULL;
    }
    clear_r = r - 2.9f;
    cx0 = 0, cx1 = size - 1, cy0 = 0, cy1 = size - 1;
    rgb = symbol_rgb(src, size, cx, cy, clear_r, 0, 0, 0, 0, 0, main_rgb(src, size));
    face_letter(&sh, text[0]);
  } else if (ic->kind == BOX) {
    /* L1/R1: a box 28x18 at (8, 13); L2/R2: a trigger 30x24 at (7, 10). The
     * text is cleared from inside the box's edges and redrawn, centred where
     * the old text was, in its colour. */
    const int l1 = !strcmp(ic->name, "l1") || !strcmp(ic->name, "r1");
    const int ex0 = l1 ? 8 : 7, ex1 = l1 ? 35 : 36, ey0 = l1 ? 13 : 10, ey1 = l1 ? 30 : 33;
    if (x0 != ex0 || x1 != ex1 || y0 != ey0 || y1 != ey1) {
      debugPrintf("[buttonicons] %s: its box is at %d..%d x %d..%d, not where expected, kept\n", name, x0, x1, y0, y1);
      return NULL;
    }
    cx0 = 12, cx1 = 31, cy0 = l1 ? 17 : 13, cy1 = l1 ? 26 : 28; /* inside the box's edges */
    int tx0, tx1, ty0, ty1; /* the old text: its brighter pixels */
    tx0 = ty0 = size, tx1 = ty1 = -1;
    for (int i = 0; i < size * size; i++) {
      int x = i % size, y = i / size;
      uint16_t v = px_at(src, i);
      if (x >= cx0 && x <= cx1 && y >= cy0 && y <= cy1 && (v & 15) >= 4 && (v >> 8 & 15) >= 7)
        tx0 = x < tx0 ? x : tx0, tx1 = x > tx1 ? x : tx1, ty0 = y < ty0 ? y : ty0, ty1 = y > ty1 ? y : ty1;
    }
    if (tx1 < 0) {
      debugPrintf("[buttonicons] %s: no text in its box, kept\n", name);
      return NULL;
    }
    cx = (tx0 + tx1 + 1) / 2.0f, cy = (ty0 + ty1 + 1) / 2.0f;
    rgb = symbol_rgb(src, size, 0, 0, 0, cx0, cx1, cy0, cy1, 7, main_rgb(src, size));
    /* the letters as tall as the old text (L1/R1's 10 px; L2/R2's 12, here 11
     * to fit two wider letters) */
    const float th = l1 ? 10.0f : 11.0f, sw = l1 ? 2.7f : 2.6f, gap = 1.4f;
    float total = 0;
    Shape probe = {0};
    for (const char *c = text; *c; c++)
      total += box_letter(&probe, *c, 0, th, sw) + (c[1] ? gap : 0);
    float x = -total / 2;
    for (const char *c = text; *c; c++)
      x += box_letter(&sh, *c, x, th, sw) + gap;
  } else {
    /* Start's arrow, Select's box: replaced by a ring as the face buttons'
     * (outer radius 11.5 around (18.5, 18)), in the icon's own colour, with
     * a + or - in the face icons' symbol colour. */
    if (x1 < 0) {
      debugPrintf("[buttonicons] %s: empty, kept\n", name);
      return NULL;
    }
    cx = 18.5f, cy = 18.0f;
    cx0 = 0, cx1 = size - 1, cy0 = 0, cy1 = size - 1;
    ring_rgb = main_rgb(src, size);
    rgb = SYMBOL_RGB;
    ring_lo = 8.6f, ring_hi = 11.4f;
    const float sw = 2.5f, arm = 4.3f;
    add(&sh, 's', -arm, 0, arm, 0, sw);
    if (text[0] == '+')
      add(&sh, 's', 0, -arm, 0, arm, sw);
  }
  uint8_t *out = malloc(len);
  if (!out)
    return NULL;
  memcpy(out, data, len);
  uint8_t *dst = out + PVR_HEADER;
  for (int i = 0; i < size * size; i++) {
    int x = i % size, y = i / size;
    float px = x - cx, py = y - cy; /* the pixel's top-left corner, from the centre */
    if (x < cx0 || x > cx1 || y < cy0 || y > cy1)
      continue;
    if (clear_r > 0 && (px + 0.5f) * (px + 0.5f) + (py + 0.5f) * (py + 0.5f) >= clear_r * clear_r)
      continue;
    int hits, ring;
    coverage(&sh, px, py, ring_lo, ring_hi, &hits, &ring);
    unsigned a = (unsigned)((hits + ring) * 15 + 8) / 16;
    unsigned c = ring > hits ? ring_rgb : rgb; /* a pixel mostly ring takes the ring's colour */
    px_put(dst, i, a ? (uint16_t)(c << 4 | a) : 0);
  }
  static unsigned logged;
  unsigned bit = 1u << (ic - k_icons);
  if (!(logged & bit)) {
    logged |= bit;
    debugPrintf("[buttonicons] %s: drawn as %s (swap_a_b %s)\n", name, text, dcr_config()->swap_ab ? "on" : "off");
  }
  *out_len = len;
  return out;
}
