/*
 * Host test harness for the nasm assembler core.
 *
 * Compiles nasm.c for the build machine with the calculator I/O layer
 * (gfx / filebrowser) stubbed out, and exposes a CLI:
 *
 *   nasm_host <input.asm> <output.bin> [zehn]
 *
 * Used by run_tests.sh, which byte-compares the output against the
 * Ndless toolchain's GNU assembler (nspire-as).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main nasm_calc_main
#include "../nasm.c"
#undef main

/* gfx / filebrowser stubs (host) */
GfxTheme g_default_theme;

void gfx_init(void) {}
void gfx_deinit(void) {}
void gfx_window_alert(const char *t, const char **l, int n, const char *o,
                      int s) {
  (void)t;
  (void)l;
  (void)n;
  (void)o;
  (void)s;
}
void gfx_window_scrolltext(const char *t, const char **l, int n,
                           const char *o) {
  (void)t;
  (void)o;
  for (int i = 0; i < n; i++)
    fprintf(stderr, "%s\n", l[i]);
}
int gfx_window_confirm2(const char *t, const char **b, int n, const char *b0,
                        const char *b1) {
  (void)t;
  (void)b;
  (void)n;
  (void)b0;
  (void)b1;
  return 1;
}
int gfx_window_confirm3(const char *t, const char **b, int n, const char *b0,
                        const char *b1, const char *b2) {
  (void)t;
  (void)b;
  (void)n;
  (void)b0;
  (void)b1;
  (void)b2;
  return 2;
}
int gfx_window_exec(GfxWindow *win) {
  (void)win;
  return -1;
}

static GfxWidget *stub_widget(void) { return calloc(1, sizeof(GfxWidget)); }
GfxWidget *widget_create_label(int x, int y, int w, int h, const char *t) {
  (void)x; (void)y; (void)w; (void)h; (void)t;
  return stub_widget();
}
GfxWidget *widget_create_heading(int x, int y, int w, int h, const char *t) {
  (void)x; (void)y; (void)w; (void)h; (void)t;
  return stub_widget();
}
GfxWidget *widget_create_toggle(int x, int y, int w, int h, int *v) {
  (void)x; (void)y; (void)w; (void)h; (void)v;
  return stub_widget();
}
GfxWidget *widget_create_number(int x, int y, int w, int h, int *v, int lo,
                                int hi) {
  (void)x; (void)y; (void)w; (void)h; (void)v; (void)lo; (void)hi;
  return stub_widget();
}
GfxWidget *widget_create_dropdown(int x, int y, int w, int h, int *v,
                                  const char **o, int n) {
  (void)x; (void)y; (void)w; (void)h; (void)v; (void)o; (void)n;
  return stub_widget();
}
GfxWidget *widget_create_text(int x, int y, int w, int h, char *b, int m,
                              const char *p) {
  (void)x; (void)y; (void)w; (void)h; (void)b; (void)m; (void)p;
  return stub_widget();
}
GfxWidget *widget_create_button(int x, int y, int w, int h, const char *l) {
  (void)x; (void)y; (void)w; (void)h; (void)l;
  return stub_widget();
}
const char *filebrowser_select(void) { return NULL; }

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s in.asm out.bin [zehn]\n", argv[0]);
    return 2;
  }

  settings_load();

  int use_zehn = (argc > 3);
  int ret = assembler(argv[1], argv[2], use_zehn);

  clear_errors();

  for (int i = 0; i < num_cached_files; i++)
    free(file_cache[i].data);
  num_cached_files = 0;

  return ret >= 0 ? 0 : 1;
}
