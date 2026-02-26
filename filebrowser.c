/*
 * filebrowser.c
 * Filesystem browser for TI-Nspire CX / CX II (Ndless)
 */

#include <ctype.h>
#include <dirent.h>
#include <keys.h>
#include <libndls.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "filebrowser.h"
#include "gfx.h"

/* ------------------------------------------------------------------ */
/*  Colours used by the browser                                       */
/* ------------------------------------------------------------------ */
#define COL_BG GFX_COL_BLACK
#define COL_FG GFX_COL_WHITE
#define COL_HDR GFX_COL_DARK_BLUE
#define COL_SEL_BG GFX_COL_GREEN
#define COL_SEL_FG GFX_COL_BLACK
#define COL_DIR_FG GFX_COL_ORANGE
#define COL_HINT_FG GFX_COL_GREY

/* ------------------------------------------------------------------ */
/*  Layout                                                            */
/* ------------------------------------------------------------------ */
#define HEADER_H 14
#define HINT_H 10
#define ROW_H 11
#define LIST_Y (HEADER_H + 2)
#define LIST_H (GFX_H - HEADER_H - HINT_H - 4)
#define ROWS_VISIBLE (LIST_H / ROW_H)
#define LIST_X 2
#define LIST_W (GFX_W - 4)

/* ------------------------------------------------------------------ */
/*  Directory entry list                                              */
/* ------------------------------------------------------------------ */
#define MAX_ENTRIES 512
#define MAX_PATH 512
#define MAX_NAME 256

typedef struct {
  char name[MAX_NAME];
  char fullpath[MAX_PATH];
  int is_dir;
} Entry;

static Entry entries[MAX_ENTRIES];
static int num_entries = 0;

static int str_endswith_ci(const char *s, const char *suffix) {
  size_t sl = strlen(s), pl = strlen(suffix);
  if (pl > sl)
    return 0;

  const char *p = s + sl - pl;

  while (*p && *suffix) {
    if (tolower((unsigned char)*p) != tolower((unsigned char)*suffix))
      return 0;

    p++;
    suffix++;
  }

  return 1;
}

static int entry_cmp(const void *a, const void *b) {
  const Entry *ea = (const Entry *)a;
  const Entry *eb = (const Entry *)b;

  if (ea->is_dir != eb->is_dir)
    return eb->is_dir - ea->is_dir;

  const char *na = ea->name, *nb = eb->name;

  while (*na && *nb) {
    int d = tolower((unsigned char)*na) - tolower((unsigned char)*nb);
    if (d)
      return d;

    na++;
    nb++;
  }

  return tolower((unsigned char)*na) - tolower((unsigned char)*nb);
}

static void load_dir(const char *path, int filter_asm) {
  DIR *d = opendir(path);

  num_entries = 0;

  if (!d)
    return;

  /* ".." entry unless at root */
  if (strcmp(path, "/documents") != 0) {
    strncpy(entries[0].name, "..", MAX_NAME - 1);
    entries[0].name[MAX_NAME - 1] = '\0';
    strncpy(entries[0].fullpath, path, MAX_PATH - 1);
    entries[0].fullpath[MAX_PATH - 1] = '\0';

    char *slash = strrchr(entries[0].fullpath, '/');
    if (slash && slash != entries[0].fullpath)
      *slash = '\0';
    else
      strncpy(entries[0].fullpath, "/documents", MAX_PATH - 1);

    entries[0].is_dir = 1;
    num_entries = 1;
  }

  struct dirent *de;

  while ((de = readdir(d)) != NULL && num_entries < MAX_ENTRIES) {
    if (de->d_name[0] == '.')
      continue;

    char fullpath[MAX_PATH];
    snprintf(fullpath, MAX_PATH, "%s/%s", path, de->d_name);

    struct stat st;
    int is_dir = 0;
    if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode))
      is_dir = 1;

    if (!is_dir && filter_asm && !str_endswith_ci(de->d_name, ".asm.tns"))
      continue;

    strncpy(entries[num_entries].name, de->d_name, MAX_NAME - 1);
    entries[num_entries].name[MAX_NAME - 1] = '\0';
    strncpy(entries[num_entries].fullpath, fullpath, MAX_PATH - 1);
    entries[num_entries].fullpath[MAX_PATH - 1] = '\0';
    entries[num_entries].is_dir = is_dir;
    num_entries++;
  }

  closedir(d);

  if (num_entries > 0)
    qsort(entries, num_entries, sizeof(Entry), entry_cmp);
}

/* ------------------------------------------------------------------ */
/*  Rendering                                                         */
/* ------------------------------------------------------------------ */
static void render(const char *cur_path, int cursor, int scroll,
                   int filter_asm) {
  gfx_fillrect(0, 0, GFX_W, GFX_H, COL_BG);

  /* Header */
  gfx_fillrect(0, 0, GFX_W, HEADER_H, COL_HDR);
  const char *disp = cur_path;
  int max_chars = (GFX_W - 4) / GFX_CHAR_W;
  int plen = (int)strlen(cur_path);

  if (plen > max_chars)
    disp = cur_path + plen - max_chars;

  gfx_drawstr(2, 3, disp, COL_FG, COL_HDR);

  /* File rows */
  int i;
  for (i = 0; i < ROWS_VISIBLE; i++) {
    int idx = scroll + i;
    int row_y = LIST_Y + i * ROW_H;

    if (idx < num_entries) {
      int sel = (idx == cursor);
      uint16_t fg =
          sel ? COL_SEL_FG : (entries[idx].is_dir ? COL_DIR_FG : COL_FG);
      uint16_t bg = sel ? COL_SEL_BG : COL_BG;

      gfx_fillrect(LIST_X, row_y, LIST_W, ROW_H - 1, bg);

      char display[MAX_NAME + 3];

      if (entries[idx].is_dir && strcmp(entries[idx].name, "..") != 0) {
        display[0] = '[';
        strncpy(display + 1, entries[idx].name, MAX_NAME);
        display[MAX_NAME] = '\0';
        size_t dl = strlen(display);
        display[dl] = ']';
        display[dl + 1] = '\0';
      } else {
        strncpy(display, entries[idx].name, MAX_NAME + 1);
        display[MAX_NAME + 1] = '\0';
      }

      gfx_drawstr_clipped(LIST_X + 2, row_y + 1, display, fg, bg, LIST_W - 4);
    } else {
      gfx_fillrect(LIST_X, row_y, LIST_W, ROW_H - 1, COL_BG);
    }
  }

  /* Scrollbar */
  if (num_entries > ROWS_VISIBLE) {
    int total = LIST_H;
    int bar_h = total * ROWS_VISIBLE / num_entries;
    if (bar_h < 4)
      bar_h = 4;

    int bar_y =
        LIST_Y + (total - bar_h) * scroll / (num_entries - ROWS_VISIBLE);

    gfx_fillrect(GFX_W - 3, LIST_Y, 2, total, COL_HDR);
    gfx_fillrect(GFX_W - 3, bar_y, 2, bar_h, COL_FG);
  }

  /* Hint bar */
  int hy = GFX_H - HINT_H;

  gfx_fillrect(0, hy, GFX_W, HINT_H, COL_HDR);

  char hint[64];
  snprintf(hint, sizeof(hint), "%sEnter:open  Esc:quit",
           filter_asm ? "Tab:all  " : "Tab:.asm ");

  gfx_drawstr(2, hy + 1, hint, COL_HINT_FG, COL_HDR);

  gfx_flip();
}

/* ------------------------------------------------------------------ */
/*  Key helpers                                                       */
/* ------------------------------------------------------------------ */
static void wait_debounce(const t_key key) {
  while (isKeyPressed(key)) {
    msleep(15);
  }
  msleep(30);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                        */
/* ------------------------------------------------------------------ */
static char result_path[MAX_PATH];

const char *filebrowser_select(void) {
  gfx_init();

  int filter_asm = 1;
  char cur_path[MAX_PATH];
  strncpy(cur_path, "/documents", MAX_PATH - 1);
  cur_path[MAX_PATH - 1] = '\0';

  int cursor = 0, scroll = 0;
  load_dir(cur_path, filter_asm);
  render(cur_path, cursor, scroll, filter_asm);

  while (any_key_pressed()) {
    msleep(20);
  }

  for (;;) {
    if (isKeyPressed(KEY_NSPIRE_UP)) {
      if (cursor > 0) {
        cursor--;

        if (cursor < scroll)
          scroll = cursor;
      }

      render(cur_path, cursor, scroll, filter_asm);
      wait_debounce(KEY_NSPIRE_UP);
    } else if (isKeyPressed(KEY_NSPIRE_DOWN)) {
      if (cursor < num_entries - 1) {
        cursor++;

        if (cursor >= scroll + ROWS_VISIBLE)
          scroll = cursor - ROWS_VISIBLE + 1;
      }

      render(cur_path, cursor, scroll, filter_asm);
      wait_debounce(KEY_NSPIRE_DOWN);
    } else if (isKeyPressed(KEY_NSPIRE_TAB)) {
      filter_asm = !filter_asm;
      cursor = 0;
      scroll = 0;

      load_dir(cur_path, filter_asm);
      render(cur_path, cursor, scroll, filter_asm);
      wait_debounce(KEY_NSPIRE_TAB);
    } else if (isKeyPressed(KEY_NSPIRE_ENTER) ||
               isKeyPressed(KEY_NSPIRE_CLICK)) {
      if (num_entries > 0) {
        if (entries[cursor].is_dir) {
          strncpy(cur_path, entries[cursor].fullpath, MAX_PATH - 1);
          cursor = 0;
          scroll = 0;

          load_dir(cur_path, filter_asm);
          render(cur_path, cursor, scroll, filter_asm);
        } else {
          strncpy(result_path, entries[cursor].fullpath, MAX_PATH - 1);

          result_path[MAX_PATH - 1] = '\0';

          /* Do NOT call gfx_deinit() here: the caller reuses
             the LCD for the result window, then deinits.    */
          return result_path;
        }
      }

      wait_debounce(KEY_NSPIRE_ENTER);
    } else if (isKeyPressed(KEY_NSPIRE_ESC)) {
      gfx_deinit();

      return NULL;
    } else {
      msleep(20);
      idle();
    }
  }
}
