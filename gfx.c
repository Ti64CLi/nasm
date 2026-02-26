/*
 * gfx.c
 * Shared graphics implementation for TI-Nspire CX / CX II (Ndless)
 */

#include <keys.h>
#include <libndls.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"

/* --------------------------------------------------------------*/
/*  Framebuffer                                                  */
/* --------------------------------------------------------------*/

static uint16_t fb[GFX_W * GFX_H];

uint16_t *gfx_framebuffer(void) { return fb; }

/* --------------------------------------------------------------*/
/*  Lifecycle                                                    */
/* --------------------------------------------------------------*/

void gfx_init(void) { lcd_init(SCR_320x240_565); }
void gfx_deinit(void) { lcd_init(SCR_TYPE_INVALID); }
void gfx_flip(void) { lcd_blit(fb, SCR_320x240_565); }

/* --------------------------------------------------------------*/
/*  Primitives                                                   */
/* --------------------------------------------------------------*/

void gfx_setpixel(int x, int y, uint16_t col) {
  if ((unsigned)x < GFX_W && (unsigned)y < GFX_H)
    fb[y * GFX_W + x] = col;
}

void gfx_fillrect(int x, int y, int w, int h, uint16_t col) {
  int r, c;
  for (r = y; r < y + h; r++)
    for (c = x; c < x + w; c++)
      gfx_setpixel(c, r, col);
}

void gfx_hline(int x, int y, int len, uint16_t col) {
  int i;
  for (i = 0; i < len; i++)
    gfx_setpixel(x + i, y, col);
}

void gfx_vline(int x, int y, int len, uint16_t col) {
  int i;
  for (i = 0; i < len; i++)
    gfx_setpixel(x, y + i, col);
}

void gfx_borderrect(int x, int y, int w, int h, uint16_t fill,
                    uint16_t border) {
  gfx_fillrect(x, y, w, h, fill);
  gfx_hline(x, y, w, border);         /* top    */
  gfx_hline(x, y + h - 1, w, border); /* bottom */
  gfx_vline(x, y, h, border);         /* left   */
  gfx_vline(x + w - 1, y, h, border); /* right  */
}

/* --------------------------------------------------------------*/
/*  5x8 bitmap font (ASCII 32-126)                               */
/*  Each entry is GFX_FONT_H bytes; each byte is a 5-bit row,    */
/*  MSB = leftmost pixel.                                        */
/* --------------------------------------------------------------*/

static const uint8_t font5x8[][GFX_FONT_H] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ' ' 32 */
    {0x04, 0x04, 0x04, 0x04, 0x00, 0x04, 0x00, 0x00}, /* '!' 33 */
    {0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '"' 34 */
    {0x0A, 0x1F, 0x0A, 0x0A, 0x1F, 0x0A, 0x00, 0x00}, /* '#' 35 */
    {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04, 0x00}, /* '$' 36 */
    {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03, 0x00}, /* '%' 37 */
    {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D, 0x00}, /* '&' 38 */
    {0x0C, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00}, /* ''' 39 */
    {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02, 0x00}, /* '(' 40 */
    {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, 0x00}, /* ')' 41 */
    {0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00, 0x00}, /* '*' 42 */
    {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00, 0x00}, /* '+' 43 */
    {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08, 0x00}, /* ',' 44 */
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x00}, /* '-' 45 */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, /* '.' 46 */
    {0x01, 0x02, 0x02, 0x04, 0x08, 0x10, 0x10, 0x00}, /* '/' 47 */
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E, 0x00}, /* '0' 48 */
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00}, /* '1' 49 */
    {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F, 0x00}, /* '2' 50 */
    {0x1F, 0x02, 0x04, 0x06, 0x01, 0x11, 0x0E, 0x00}, /* '3' 51 */
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02, 0x00}, /* '4' 52 */
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E, 0x00}, /* '5' 53 */
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E, 0x00}, /* '6' 54 */
    {0x1F, 0x01, 0x02, 0x04, 0x04, 0x04, 0x04, 0x00}, /* '7' 55 */
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00}, /* '8' 56 */
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C, 0x00}, /* '9' 57 */
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00, 0x00}, /* ':' 58 */
    {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08, 0x00}, /* ';' 59 */
    {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02, 0x00}, /* '<' 60 */
    {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00}, /* '=' 61 */
    {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08, 0x00}, /* '>' 62 */
    {0x0E, 0x11, 0x02, 0x04, 0x04, 0x00, 0x04, 0x00}, /* '?' 63 */
    {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E, 0x00}, /* '@' 64 */
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00}, /* 'A' 65 */
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E, 0x00}, /* 'B' 66 */
    {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E, 0x00}, /* 'C' 67 */
    {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C, 0x00}, /* 'D' 68 */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F, 0x00}, /* 'E' 69 */
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10, 0x00}, /* 'F' 70 */
    {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F, 0x00}, /* 'G' 71 */
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00}, /* 'H' 72 */
    {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00}, /* 'I' 73 */
    {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C, 0x00}, /* 'J' 74 */
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 0x00}, /* 'K' 75 */
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00}, /* 'L' 76 */
    {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11, 0x00}, /* 'M' 77 */
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11, 0x00}, /* 'N' 78 */
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00}, /* 'O' 79 */
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10, 0x00}, /* 'P' 80 */
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D, 0x00}, /* 'Q' 81 */
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11, 0x00}, /* 'R' 82 */
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E, 0x00}, /* 'S' 83 */
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00}, /* 'T' 84 */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00}, /* 'U' 85 */
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00}, /* 'V' 86 */
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11, 0x00}, /* 'W' 87 */
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11, 0x00}, /* 'X' 88 */
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x00}, /* 'Y' 89 */
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F, 0x00}, /* 'Z' 90 */
    {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E, 0x00}, /* '[' 91 */
    {0x10, 0x08, 0x08, 0x04, 0x02, 0x02, 0x01, 0x00}, /* '\' 92 */
    {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E, 0x00}, /* ']' 93 */
    {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '^' 94 */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00}, /* '_' 95 */
    {0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00}, /* '`' 96 */
    {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F, 0x00}, /* 'a' 97 */
    {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E, 0x00}, /* 'b' 98 */
    {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E, 0x00}, /* 'c' 99 */
    {0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F, 0x00}, /* 'd' 100 */
    {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E, 0x00}, /* 'e' 101 */
    {0x06, 0x08, 0x08, 0x1C, 0x08, 0x08, 0x08, 0x00}, /* 'f' 102 */
    {0x00, 0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x0E}, /* 'g' 103 */
    {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x00}, /* 'h' 104 */
    {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E, 0x00}, /* 'i' 105 */
    {0x02, 0x00, 0x06, 0x02, 0x02, 0x02, 0x12, 0x0C}, /* 'j' 106 */
    {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00}, /* 'k' 107 */
    {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00}, /* 'l' 108 */
    {0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11, 0x00}, /* 'm' 109 */
    {0x00, 0x00, 0x1E, 0x11, 0x11, 0x11, 0x11, 0x00}, /* 'n' 110 */
    {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00}, /* 'o' 111 */
    {0x00, 0x00, 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10}, /* 'p' 112 */
    {0x00, 0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x01}, /* 'q' 113 */
    {0x00, 0x00, 0x16, 0x18, 0x10, 0x10, 0x10, 0x00}, /* 'r' 114 */
    {0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E, 0x00}, /* 's' 115 */
    {0x08, 0x08, 0x1C, 0x08, 0x08, 0x08, 0x06, 0x00}, /* 't' 116 */
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x11, 0x0F, 0x00}, /* 'u' 117 */
    {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04, 0x00}, /* 'v' 118 */
    {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A, 0x00}, /* 'w' 119 */
    {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00}, /* 'x' 120 */
    {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E, 0x00}, /* 'y' 121 */
    {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F, 0x00}, /* 'z' 122 */
    {0x06, 0x04, 0x04, 0x08, 0x04, 0x04, 0x06, 0x00}, /* '{' 123 */
    {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00}, /* '|' 124 */
    {0x0C, 0x04, 0x04, 0x02, 0x04, 0x04, 0x0C, 0x00}, /* '}' 125 */
    {0x00, 0x08, 0x15, 0x02, 0x00, 0x00, 0x00, 0x00}, /* '~' 126 */
};

/* --------------------------------------------------------------*/
/*  Text drawing                                                 */
/* --------------------------------------------------------------*/

int gfx_drawchar(int x, int y, char ch, uint16_t fg, uint16_t bg) {
  int row, col;

  uint8_t idx = (uint8_t)ch;
  if (idx < 32 || idx > 126)
    idx = 63; /* '?' */

  const uint8_t *glyph = font5x8[idx - 32];

  for (row = 0; row < GFX_FONT_H; row++) {
    uint8_t bits = glyph[row];

    for (col = 0; col < GFX_FONT_W; col++)
      gfx_setpixel(x + col, y + row, (bits & (0x10 >> col)) ? fg : bg);

    gfx_setpixel(x + GFX_FONT_W, y + row, bg); /* gap column */
  }

  return GFX_CHAR_W;
}

int gfx_drawstr(int x, int y, const char *s, uint16_t fg, uint16_t bg) {
  while (*s) {
    x += gfx_drawchar(x, y, *s++, fg, bg);
  }

  return x;
}

void gfx_drawstr_clipped(int x, int y, const char *s, uint16_t fg, uint16_t bg,
                         int maxw) {
  int cx = x;

  while (*s && cx + GFX_CHAR_W <= x + maxw) {
    cx += gfx_drawchar(cx, y, *s++, fg, bg);
  }

  if (cx < x + maxw)
    gfx_fillrect(cx, y, x + maxw - cx, GFX_FONT_H, bg);
}

/* --------------------------------------------------------------*/
/*  Modal alert window                                           */
/* --------------------------------------------------------------*/

/*
 * Layout (all sizes in pixels):
 *
 *  +--[ title bar, TITLE_H px ]-------------------------+
 *  |  title text                                        |
 *  +----------------------------------------------------+
 *  |  BODY_PAD px padding                               |
 *  |  line 0                                            |
 *  |  line 1  ...                                       |
 *  |  BODY_PAD px padding                               |
 *  +--[ button row, BTN_ROW_H px ]----------------------+
 *  |  [ ok_label ]                                      |
 *  +----------------------------------------------------+
 *
 *  Window is centred horizontally and vertically.
 *  Minimum width: MIN_W.  Body text is clipped if too long.
 */

#define WIN_MIN_W 160
#define WIN_MAX_W 300
#define WIN_MIN_H 60
#define WIN_MAX_H 200
#define WIN_TITLE_H 13
#define WIN_BODY_PAD 6
#define WIN_BTN_H 14
#define WIN_BORDER 1
#define WIN_H_PAD 8 /* horizontal padding inside body */

void gfx_window_alert(const char *title, const char **lines, int nlines,
                      const char *ok_label) {
  if (!ok_label)
    ok_label = "OK";

  /* Measure required width */
  int title_chars = title ? (int)strlen(title) : 0;
  int max_body_chars = 0;
  int i;

  for (i = 0; i < nlines; i++) {
    int l = lines[i] ? (int)strlen(lines[i]) : 0;
    if (l > max_body_chars)
      max_body_chars = l;
  }
  int ok_chars = (int)strlen(ok_label);

  int content_w = max_body_chars * GFX_CHAR_W;
  if (title_chars * GFX_CHAR_W > content_w)
    content_w = title_chars * GFX_CHAR_W;
  if (ok_chars * GFX_CHAR_W + 8 > content_w)
    content_w = ok_chars * GFX_CHAR_W + 8;

  int win_w = content_w + WIN_H_PAD * 2 + WIN_BORDER * 2;
  if (win_w < WIN_MIN_W)
    win_w = WIN_MIN_W;
  if (win_w > WIN_MAX_W)
    win_w = WIN_MAX_W;

  /* Measure height */
  int body_h = nlines * (GFX_FONT_H + 2);
  int win_h = WIN_TITLE_H + WIN_BODY_PAD + body_h + WIN_BODY_PAD + WIN_BTN_H +
              WIN_BORDER;

  /* Centre on screen */
  int wx = (GFX_W - win_w) / 2;
  int wy = (GFX_H - win_h) / 2;

  /* Draw window frame */
  /* Drop-shadow (2px, dark grey) */
  gfx_fillrect(wx + 3, wy + 3, win_w, win_h, GFX_COL_GREY);

  /* Outer border */
  gfx_borderrect(wx, wy, win_w, win_h, GFX_COL_LIGHT_GREY, GFX_COL_DARK_BLUE);

  /* Title bar */
  gfx_fillrect(wx + WIN_BORDER, wy + WIN_BORDER, win_w - WIN_BORDER * 2,
               WIN_TITLE_H, GFX_COL_DARK_BLUE);
  if (title) {
    gfx_drawstr_clipped(
        wx + WIN_H_PAD, wy + WIN_BORDER + (WIN_TITLE_H - GFX_FONT_H) / 2, title,
        GFX_COL_WHITE, GFX_COL_DARK_BLUE, win_w - WIN_H_PAD * 2);
  }

  /* Body background */
  int body_top = wy + WIN_BORDER + WIN_TITLE_H;
  int body_area_h = win_h - WIN_BORDER - WIN_TITLE_H - WIN_BTN_H - WIN_BORDER;
  gfx_fillrect(wx + WIN_BORDER, body_top, win_w - WIN_BORDER * 2, body_area_h,
               GFX_COL_BLACK);

  /* Body text lines */
  int inner_w = win_w - WIN_BORDER * 2 - WIN_H_PAD * 2;
  for (i = 0; i < nlines; i++) {
    if (!lines[i])
      continue;
    int ty = body_top + WIN_BODY_PAD + i * (GFX_FONT_H + 2);
    gfx_drawstr_clipped(wx + WIN_BORDER + WIN_H_PAD, ty, lines[i],
                        GFX_COL_WHITE, GFX_COL_BLACK, inner_w);
  }

  /* Separator line above button row */
  int sep_y = wy + win_h - WIN_BTN_H - WIN_BORDER;
  gfx_hline(wx + WIN_BORDER, sep_y, win_w - WIN_BORDER * 2, GFX_COL_DARK_BLUE);

  /* Button row background */
  gfx_fillrect(wx + WIN_BORDER, sep_y + 1, win_w - WIN_BORDER * 2,
               WIN_BTN_H - 1, GFX_COL_BLACK);

  /* Draw and redraw OK button (highlighted) */
  int btn_label_w = (int)strlen(ok_label) * GFX_CHAR_W;
  int btn_w = btn_label_w + 8;
  int btn_h = GFX_FONT_H + 4;
  int btn_x = wx + (win_w - btn_w) / 2;
  int btn_y = sep_y + (WIN_BTN_H - btn_h) / 2;

  gfx_borderrect(btn_x, btn_y, btn_w, btn_h, GFX_COL_GREEN, GFX_COL_WHITE);
  gfx_drawstr(btn_x + 4, btn_y + 2, ok_label, GFX_COL_BLACK, GFX_COL_GREEN);

  gfx_flip();

  /* Wait for dismiss key */
  /* drain any held keys first */
  while (any_key_pressed()) {
    msleep(20);
  }

  for (;;) {
    if (isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_SPACE) ||
        isKeyPressed(KEY_NSPIRE_ESC) || isKeyPressed(KEY_NSPIRE_CLICK)) {
      while (any_key_pressed()) {
        msleep(20);
      }
      return;
    }

    msleep(20);
    idle();
  }
}

void gfx_window_scrolltext(const char *title, const char **lines, int nlines,
                           const char *ok_label) {
  if (!ok_label)
    ok_label = "OK";

  /* Measure Content for Adaptive Sizing */
  int title_chars = title ? (int)strlen(title) : 0;
  int max_len = 0;
  for (int i = 0; i < nlines; i++) {
    if (lines[i]) {
      int len = (int)strlen(lines[i]);
      if (len > max_len)
        max_len = len;
    }
  }
  int ok_chars = (int)strlen(ok_label);

  int content_w = max_len * GFX_CHAR_W;
  if (title_chars * GFX_CHAR_W > content_w)
    content_w = title_chars * GFX_CHAR_W;
  if (ok_chars * GFX_CHAR_W + 8 > content_w)
    content_w = ok_chars * GFX_CHAR_W + 8;

  int line_spacing = GFX_FONT_H + 2;

  /* Compute Dynamic Width & Height */
  int win_w = content_w + WIN_H_PAD * 2 + WIN_BORDER * 2;
  if (win_w < WIN_MIN_W)
    win_w = WIN_MIN_W;

  int need_h_scroll = 0;
  if (win_w > WIN_MAX_W) {
    win_w = WIN_MAX_W;
    need_h_scroll = 1;
  }

  int body_h = nlines * line_spacing;
  if (need_h_scroll)
    body_h += 6; /* Reserve bottom space if horizontal scroll is needed */

  int win_h = WIN_TITLE_H + WIN_BODY_PAD * 2 + body_h + WIN_BTN_H + WIN_BORDER;
  int need_v_scroll = 0;

  if (win_h > WIN_MAX_H) {
    win_h = WIN_MAX_H;
    need_v_scroll = 1;

    /* If adding a vertical scrollbar eats into our width, we might now need
     * horizontal scroll */
    if (!need_h_scroll &&
        (content_w > win_w - WIN_BORDER * 2 - WIN_H_PAD * 2 - 6)) {
      need_h_scroll = 1;
    }
  }

  /* Calculate Layout */
  int wx = (GFX_W - win_w) / 2;
  int wy = (GFX_H - win_h) / 2;
  int body_top = wy + WIN_BORDER + WIN_TITLE_H;
  int body_area_h = win_h - WIN_BORDER - WIN_TITLE_H - WIN_BTN_H - WIN_BORDER;
  int sep_y = wy + win_h - WIN_BTN_H - WIN_BORDER;

  /* Subtract scrollbar thickness (6px) only if that specific scrollbar is
   * active */
  int inner_w = win_w - WIN_BORDER * 2 - WIN_H_PAD * 2;
  if (need_v_scroll)
    inner_w -= 6;

  int visible_body_h = body_area_h - WIN_BODY_PAD * 2;
  if (need_h_scroll)
    visible_body_h -= 6;

  int max_visible_y = visible_body_h / line_spacing;
  if (max_visible_y < 1)
    max_visible_y = 1;
  int max_visible_x = inner_w / GFX_CHAR_W;

  int btn_label_w = ok_chars * GFX_CHAR_W;
  int btn_w = btn_label_w + 8;
  int btn_h = GFX_FONT_H + 4;
  int btn_x = wx + (win_w - btn_w) / 2;
  int btn_y = sep_y + (WIN_BTN_H - btn_h) / 2;

  int scroll_y = 0;
  int scroll_x = 0;

  /* Drain held keys */
  while (any_key_pressed()) {
    msleep(20);
  }

  /* Main Window Loop */
  for (;;) {
    gfx_fillrect(wx + 3, wy + 3, win_w, win_h, GFX_COL_GREY);
    gfx_borderrect(wx, wy, win_w, win_h, GFX_COL_LIGHT_GREY, GFX_COL_DARK_BLUE);

    /* Title bar */
    gfx_fillrect(wx + WIN_BORDER, wy + WIN_BORDER, win_w - WIN_BORDER * 2,
                 WIN_TITLE_H, GFX_COL_DARK_BLUE);
    if (title) {
      gfx_drawstr_clipped(
          wx + WIN_H_PAD, wy + WIN_BORDER + (WIN_TITLE_H - GFX_FONT_H) / 2,
          title, GFX_COL_WHITE, GFX_COL_DARK_BLUE, win_w - WIN_H_PAD * 2);
    }

    /* Body Background */
    gfx_fillrect(wx + WIN_BORDER, body_top, win_w - WIN_BORDER * 2, body_area_h,
                 GFX_COL_BLACK);

    /* Draw Visible Lines with Horizontal Offset */
    for (int i = 0; i < max_visible_y; i++) {
      int idx = scroll_y + i;
      if (idx >= nlines)
        break;
      if (!lines[idx])
        continue;

      int ty = body_top + WIN_BODY_PAD + i * line_spacing;
      int len = (int)strlen(lines[idx]);

      const char *disp = "";
      if (len > scroll_x) {
        disp = lines[idx] + scroll_x;
      }

      gfx_drawstr_clipped(wx + WIN_BORDER + WIN_H_PAD, ty, disp, GFX_COL_WHITE,
                          GFX_COL_BLACK, inner_w);
    }

    /* Draw Vertical Scrollbar */
    if (need_v_scroll && nlines > max_visible_y) {
      int sb_x = wx + win_w - WIN_BORDER - 4;
      int sb_y = body_top + WIN_BODY_PAD;
      int sb_total_h = max_visible_y * line_spacing;

      int bar_h = (sb_total_h * max_visible_y) / nlines;
      if (bar_h < 4)
        bar_h = 4;

      int bar_y =
          sb_y + ((sb_total_h - bar_h) * scroll_y) / (nlines - max_visible_y);

      gfx_fillrect(sb_x, sb_y, 2, sb_total_h, GFX_COL_GREY);
      gfx_fillrect(sb_x, bar_y, 2, bar_h, GFX_COL_WHITE);
    }

    /* Draw Horizontal Scrollbar */
    if (need_h_scroll && max_len > max_visible_x) {
      int sb_x = wx + WIN_BORDER + WIN_H_PAD;
      int sb_y = body_top + body_area_h - 4;
      int sb_total_w = inner_w;

      int bar_w = (sb_total_w * max_visible_x) / max_len;
      if (bar_w < 4)
        bar_w = 4;

      int bar_x =
          sb_x + ((sb_total_w - bar_w) * scroll_x) / (max_len - max_visible_x);

      gfx_fillrect(sb_x, sb_y, sb_total_w, 2, GFX_COL_GREY);
      gfx_fillrect(bar_x, sb_y, bar_w, 2, GFX_COL_WHITE);
    }

    /* Draw Button Row */
    gfx_hline(wx + WIN_BORDER, sep_y, win_w - WIN_BORDER * 2,
              GFX_COL_DARK_BLUE);
    gfx_fillrect(wx + WIN_BORDER, sep_y + 1, win_w - WIN_BORDER * 2,
                 WIN_BTN_H - 1, GFX_COL_BLACK);
    gfx_borderrect(btn_x, btn_y, btn_w, btn_h, GFX_COL_GREEN, GFX_COL_WHITE);
    gfx_drawstr(btn_x + 4, btn_y + 2, ok_label, GFX_COL_BLACK, GFX_COL_GREEN);

    gfx_flip();

    /* Input Handling */
    if (isKeyPressed(KEY_NSPIRE_UP) || isKeyPressed(KEY_NSPIRE_8)) {
      if (scroll_y > 0)
        scroll_y--;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_UPRIGHT) || isKeyPressed(KEY_NSPIRE_9)) {
      if (scroll_y > 0)
        scroll_y--;
      if (need_h_scroll && scroll_x < max_len - max_visible_x)
        scroll_x++;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_RIGHT) || isKeyPressed(KEY_NSPIRE_6)) {
      if (need_h_scroll && scroll_x < max_len - max_visible_x)
        scroll_x++;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_RIGHTDOWN) ||
               isKeyPressed(KEY_NSPIRE_3)) {
      if (need_v_scroll && scroll_y < nlines - max_visible_y)
        scroll_y++;
      if (need_h_scroll && scroll_x < max_len - max_visible_x)
        scroll_x++;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_DOWN) || isKeyPressed(KEY_NSPIRE_2)) {
      if (need_v_scroll && scroll_y < nlines - max_visible_y)
        scroll_y++;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_DOWNLEFT) ||
               isKeyPressed(KEY_NSPIRE_1)) {
      if (need_v_scroll && scroll_y < nlines - max_visible_y)
        scroll_y++;
      if (scroll_x > 0)
        scroll_x--;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_LEFT) || isKeyPressed(KEY_NSPIRE_4)) {
      if (scroll_x > 0)
        scroll_x--;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_LEFTUP) || isKeyPressed(KEY_NSPIRE_7)) {
      if (scroll_y > 0)
        scroll_y--;
      if (scroll_x > 0)
        scroll_x--;

      msleep(40);
    } else if (isKeyPressed(KEY_NSPIRE_ENTER) ||
               isKeyPressed(KEY_NSPIRE_SPACE) || isKeyPressed(KEY_NSPIRE_ESC) ||
               isKeyPressed(KEY_NSPIRE_CLICK)) {
      while (any_key_pressed())
        msleep(20);

      break; /* Exit the loop */
    } else {
      msleep(20);
      idle();
    }
  }
}
