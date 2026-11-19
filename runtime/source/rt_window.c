/* rt_window.c -- the one window: libnx's default NWindow, at the rendering
 * size config.ini chose.
 *
 * Every port has exactly one window. Its size starts at RT_SCREEN_W x
 * RT_SCREEN_H and config.ini [display] resolution may change it before the
 * engine starts. The vi layer scales it to the screen: 1080p is shown as is
 * docked and downscaled on the 720p panel. The EGL layer (gl_mesa.c) calls
 * dcr_window_prepare right before it makes the window surface; a game with an
 * ANativeWindow of its own gets the same window through android_ndk.c. While
 * the on-screen boot log owns the window (the null renderer, or setup before
 * EGL), its geometry is left alone.
 *
 * THE RENDERING SIZE CAN CHANGE INSIDE BIGGER BUFFERS
 * ---------------------------------------------------
 * Mesa's Switch EGL makes a window surface's buffers once, at the window's
 * size then. For a window that has to follow docking (resolution = auto: 1080
 * docked, 720 in handheld, while the game runs) the buffers are made at the
 * biggest size (dcr_window_set_buffer_size, before the surface), the game
 * draws the current size into them, and the window's crop shows only that
 * part: the vi layer scales the crop to the screen as it would a window of
 * that size. dcr_window_resize changes the rendering size and the crop
 * between two frames. GL draws a W x H picture from the bottom-left corner of
 * its framebuffer, and a window buffer's rows go top to bottom, so the crop is
 * the bottom-left W x H of the buffer. MIT.
 */
#include <switch.h>

#include "rt_settings.h"
#include "rt_window.h"
#include "util.h"

static int g_win_w = RT_SCREEN_W, g_win_h = RT_SCREEN_H; /* what is drawn */
static int g_buf_w, g_buf_h; /* the buffers, when bigger than that (0: the same) */
static int g_crop_top;       /* the crop at the buffer's top instead (a test) */
static int g_made;           /* the window has its buffer size (prepare ran) */

void dcr_window_size(int *w, int *h) {
  if (w)
    *w = g_win_w;
  if (h)
    *h = g_win_h;
}

void dcr_window_set_size(int w, int h) {
  if (w > 0 && h > 0 && !g_made) {
    g_win_w = w;
    g_win_h = h;
  }
}

void dcr_window_set_buffer_size(int w, int h) {
  if (!g_made && w > 0 && h > 0)
    g_buf_w = w, g_buf_h = h;
}

void dcr_window_buffer_size(int *w, int *h) {
  if (w)
    *w = g_buf_w > g_win_w ? g_buf_w : g_win_w;
  if (h)
    *h = g_buf_h > g_win_h ? g_buf_h : g_win_h;
}

void dcr_window_crop_at_top(int on) { g_crop_top = on; }

static void set_crop(NWindow *w) {
  u32 bw = 0, bh = 0;
  if (R_FAILED(nwindowGetDimensions(w, &bw, &bh)) || !bw || !bh)
    return;
  const s32 cw = g_win_w < (int)bw ? g_win_w : (s32)bw, ch = g_win_h < (int)bh ? g_win_h : (s32)bh;
  if (g_crop_top)
    nwindowSetCrop(w, 0, 0, cw, ch);
  else
    nwindowSetCrop(w, 0, (s32)bh - ch, cw, (s32)bh);
}

void rt_window_set_geom(NWindow *w, u32 width, u32 height) {
  if (!nwindowIsValid(w) || log_console_active())
    return; /* the on-screen boot log owns the window */
  u32 bw = width, bh = height;
  if (g_buf_w > (int)bw && g_buf_h > (int)bh)
    bw = (u32)g_buf_w, bh = (u32)g_buf_h;
  nwindowSetDimensions(w, bw, bh);
  set_crop(w);
  nwindowSetSwapInterval(w, 1);
  g_made = 1;
}

void dcr_window_prepare(void) { rt_window_set_geom(nwindowGetDefault(), (u32)g_win_w, (u32)g_win_h); }

int dcr_window_resize(int w, int h) {
  int bw, bh;
  dcr_window_buffer_size(&bw, &bh);
  if (w <= 0 || h <= 0 || w > bw || h > bh || (w == g_win_w && h == g_win_h))
    return 0;
  g_win_w = w, g_win_h = h;
  NWindow *nw = nwindowGetDefault();
  if (g_made && nwindowIsValid(nw) && !log_console_active())
    set_crop(nw);
  return 1;
}
