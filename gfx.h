#ifndef GFX_H
#define GFX_H

/*
 * gfx.h
 * Shared graphics API for TI-Nspire CX / CX II (Ndless)
 *
 * Provides:
 *   - 320x240 RGB565 framebuffer management
 *   - Primitive drawing (pixel, filled rect, border rect, hline, vline)
 *   - 5x8 bitmap font text rendering
 *   - Modal window with title bar, multi-line body, and an OK button
 *
 * Call gfx_init() once before any drawing, gfx_flip() to push the
 * framebuffer to the screen, and gfx_deinit() when done.
 */

#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  Screen geometry                                                   */
/* ------------------------------------------------------------------ */
#define GFX_W 320
#define GFX_H 240

/* ------------------------------------------------------------------ */
/*  Font geometry                                                     */
/* ------------------------------------------------------------------ */
#define GFX_FONT_W 5
#define GFX_FONT_H 8
#define GFX_FONT_GAP 1
#define GFX_CHAR_W (GFX_FONT_W + GFX_FONT_GAP) /* advance per glyph */

/* ------------------------------------------------------------------ */
/*  Colour palette (RGB565)                                           */
/* ------------------------------------------------------------------ */
#define GFX_COL_BLACK 0x0000u
#define GFX_COL_WHITE 0xFFFFu
#define GFX_COL_DARK_BLUE 0x3186u // 0x0011u
#define GFX_COL_GREEN 0x03E0u
#define GFX_COL_ORANGE 0xFD20u
#define GFX_COL_GREY 0x8C71u
#define GFX_COL_RED 0xF800u
#define GFX_COL_LIGHT_GREY 0xC618u

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                         */
/* ------------------------------------------------------------------ */

/* Initialise LCD into 320x240 RGB565 mode.                            */
void gfx_init(void);

/* Restore the OS LCD mode.                                            */
void gfx_deinit(void);

/* Blit the internal framebuffer to the screen.                        */
void gfx_flip(void);

/* ------------------------------------------------------------------ */
/*  Framebuffer access                                                */
/* ------------------------------------------------------------------ */

/* Direct framebuffer pointer (GFX_W * GFX_H uint16_t values).        */
uint16_t *gfx_framebuffer(void);

/* ------------------------------------------------------------------ */
/*  Primitives                                                        */
/* ------------------------------------------------------------------ */

void gfx_setpixel(int x, int y, uint16_t col);
void gfx_fillrect(int x, int y, int w, int h, uint16_t col);
void gfx_borderrect(int x, int y, int w, int h, uint16_t fill, uint16_t border);
void gfx_hline(int x, int y, int len, uint16_t col);
void gfx_vline(int x, int y, int len, uint16_t col);

/* ------------------------------------------------------------------ */
/*  Text                                                              */
/* ------------------------------------------------------------------ */

/* Draw one character, return x-advance (GFX_CHAR_W).                 */
int gfx_drawchar(int x, int y, char ch, uint16_t fg, uint16_t bg);

/* Draw a zero-terminated string, return x after the last character.   */
int gfx_drawstr(int x, int y, const char *s, uint16_t fg, uint16_t bg);

/* Draw a string clipped to maxw pixels wide; pads remainder with bg. */
void gfx_drawstr_clipped(int x, int y, const char *s, uint16_t fg, uint16_t bg,
                         int maxw);

/* ------------------------------------------------------------------ */
/*  Window / modal dialog                                             */
/* ------------------------------------------------------------------ */

/*
 * gfx_window_alert  -  modal informational dialog
 *
 *   title   : text shown in the title bar (may be NULL)
 *   lines   : array of body-text strings  (may be NULL)
 *   nlines  : number of elements in lines[]
 *   ok_label: label on the dismiss button (NULL => "OK")
 *
 * Drawn centred on screen over whatever is already in the framebuffer
 * (caller's scene stays visible as a backdrop).
 * Blocks until Enter, Space or Escape is pressed, then returns.
 * gfx_init() must have been called first.
 */
void gfx_window_alert(const char *title, const char **lines, int nlines,
                      const char *ok_label);
/*
 * gfx_window_scrolltext - scrollable modal informational dialog
 *
 * title   : text shown in the title bar (may be NULL)
 * lines   : array of body-text strings  (may be NULL)
 * nlines  : number of elements in lines[]
 * ok_label: label on the dismiss button (NULL => "OK")
 *
 * Behaves like gfx_window_alert but supports scrolling with Up/Down keys
 * when the number of lines exceeds the window capacity.
 */
void gfx_window_scrolltext(const char *title, const char **lines, int nlines,
                           const char *ok_label);

#endif /* GFX_H */
