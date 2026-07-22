/*
 * nasm.c
 * ARM assembler for TI-Nspire CX / CX II (Ndless)
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytebuf.h"
#include "filebrowser.h"
#include "gfx.h"
#include "optab.h"
#include "settings.h"
#include "util.h"

#define ZEHN_SIGNATURE 0x6e68655a /* "Zehn" in little-endian */
#define ZEHN_VERSION 1

enum Zehn_flag_type {
  NDLESS_VERSION_MIN = 0,
  NDLESS_VERSION_MAX = 1,
  NDLESS_REVISION_MIN = 2,
  NDLESS_REVISION_MAX = 3,
  RUNS_ON_COLOR = 4,
  RUNS_ON_CLICKPAD = 5,
  RUNS_ON_TOUCHPAD = 6,
  RUNS_ON_32MB = 7,
  EXECUTABLE_NAME = 8,
  EXECUTABLE_AUTHOR = 9,
  EXECUTABLE_VERSION = 10,
  EXECUTABLE_NOTICE = 11,
  RUNS_ON_HWW = 12,  /* CX II 240x320 screen support */
  USES_LCD_BLIT = 13 /* Bypasses legacy screen compatibility mode */
};

typedef struct {
  uint32_t signature;
  uint32_t version;
  uint32_t file_size;
  uint32_t reloc_count;
  uint32_t flag_count;
  uint32_t extra_size;
  uint32_t alloc_size;
  uint32_t entry_offset;
} __attribute__((packed)) Zehn_header;

static uint32_t make_zehn_flag(uint8_t type, uint32_t data) {
  return ((data & 0xFFFFFF) << 8) | type;
}

#define MAX_ERR_LINES 512

static char *g_err_lines[MAX_ERR_LINES + 1];
static int g_num_err_lines = 0;

static void clear_errors(void) {
  for (int i = 0; i < g_num_err_lines; i++) {
    free(g_err_lines[i]);
  }

  g_num_err_lines = 0;
}

static void add_error(const char *fmt, ...) {
  char buf[1024];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  printf("%s\n", buf);

  if (g_num_err_lines < MAX_ERR_LINES) {
    g_err_lines[g_num_err_lines++] = strdup_s(buf);
  } else if (g_num_err_lines == MAX_ERR_LINES) {
    g_err_lines[g_num_err_lines++] =
        strdup_s("... [Too many errors, output truncated]");
  }
}

#define MAX_FILES 256
#define MAX_PATH 1024

typedef struct {
  char path[MAX_PATH];
  unsigned char *data;
  size_t size;
} FileEntry;

static FileEntry file_cache[MAX_FILES];
static int num_cached_files = 0;

static char g_sourcefile[MAX_PATH] = "";
static char g_sourcefolder[MAX_PATH] = "";

static void set_sourcepath(const char *path) {
  int bs;

  strncpy(g_sourcefile, path, MAX_PATH - 1);

  g_sourcefile[MAX_PATH - 1] = '\0';
  bs = (int)strlen(path) - 1;

  while (bs >= 0 && path[bs] != '/')
    bs--;

  strncpy(g_sourcefolder, path, bs + 1 < MAX_PATH ? bs + 1 : MAX_PATH - 1);
  g_sourcefolder[bs + 1 < MAX_PATH ? bs + 1 : MAX_PATH - 1] = '\0';
}

static const char *get_sourcepath(void) { return g_sourcefile; }

static void abspath(const char *curpath, const char *path, char *out,
                    size_t maxout) {
  char tmp[MAX_PATH];

  if (startswith(path, "./")) {
    abspath(curpath, path + 2, out, maxout);
  } else if (!curpath || curpath[0] == '\0') {
    strncpy(out, path, maxout - 1);

    out[maxout - 1] = '\0';
  } else if (startswith(path, "../")) {
    /* go up one level */
    strncpy(tmp, curpath, MAX_PATH - 1);

    tmp[MAX_PATH - 1] = '\0';

    /* strip trailing slash */
    size_t tlen = strlen(tmp);
    if (tlen > 0 && tmp[tlen - 1] == '/') {
      tmp[tlen - 1] = '\0';
      tlen--;
    }

    /* find last slash */
    int bs = (int)tlen - 1;
    while (bs >= 0 && tmp[bs] != '/')
      bs--;

    if (bs >= 0)
      tmp[bs + 1] = '\0';
    else
      tmp[0] = '\0';

    abspath(tmp, path + 3, out, maxout);
  } else if (path[0] == '/') {
    strncpy(out, path, maxout - 1);

    out[maxout - 1] = '\0';
  } else {
    snprintf(out, maxout, "%s%s", curpath, path);
  }
}

static void change_sourcepath(const char *path) {
  char abs[MAX_PATH];

  abspath(g_sourcefolder, path, abs, MAX_PATH);
  set_sourcepath(abs);
}

/* add_file: returns size, sets absp. Returns -1 on error. */
static long add_file(const char *path, char *absp, size_t absp_size) {
  char abs[MAX_PATH];
  int i;

  abspath(g_sourcefolder, path, abs, MAX_PATH);
  strncpy(absp, abs, absp_size - 1);

  absp[absp_size - 1] = '\0';

  /* check cache */
  for (i = 0; i < num_cached_files; i++) {
    if (strcmp(file_cache[i].path, abs) == 0)
      return (long)file_cache[i].size;
  }

  if (num_cached_files >= MAX_FILES) {
    fprintf(stderr, "add_file: file cache full\n");

    return -1;
  }

  FILE *f = fopen(abs, "rb");
  if (!f) {
    fprintf(stderr, "add_file: cannot open '%s'\n", abs);

    return -1;
  }

  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);

  if (sz < 0) {
    fclose(f);

    return -1;
  }

  unsigned char *buf = malloc(sz + 1);
  if (!buf) {
    fclose(f);

    return -1;
  }

  if (fread(buf, 1, sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);

    return -1;
  }

  fclose(f);

  buf[sz] = '\0';

  strncpy(file_cache[num_cached_files].path, abs, MAX_PATH - 1);

  file_cache[num_cached_files].path[MAX_PATH - 1] = '\0';
  file_cache[num_cached_files].data = buf;
  file_cache[num_cached_files].size = (size_t)sz;
  num_cached_files++;

  return sz;
}

static const unsigned char *filecontents(const char *path, size_t *outsz) {
  char abs[MAX_PATH];
  int i;

  abspath(g_sourcefolder, path, abs, MAX_PATH);

  for (i = 0; i < num_cached_files; i++) {
    if (strcmp(file_cache[i].path, abs) == 0) {
      if (outsz)
        *outsz = file_cache[i].size;
      return file_cache[i].data;
    }
  }

  if (outsz)
    *outsz = 0;

  return NULL;
}

static long filesize(const char *path) {
  size_t sz;

  const unsigned char *d = filecontents(path, &sz);
  if (!d)
    return -1;

  return (long)sz;
}

/* Read source code lines from current sourcefile */
/* Returns lines as array of strdup'd strings. *nlines set. Caller frees. */
static char **get_sourcecode(int *nlines) {
  FILE *f;
  char line[4096];
  char **lines = NULL;
  int n = 0;
  *nlines = 0;

  if (g_sourcefile[0] == '\0')
    return NULL;

  f = fopen(g_sourcefile, "r");
  if (!f) {
    fprintf(stderr, "get_sourcecode: cannot open '%s'\n", g_sourcefile);

    return NULL;
  }

  while (fgets(line, sizeof(line), f)) {
    /* strip trailing newline */
    size_t l = strlen(line);
    while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) {
      line[--l] = '\0';
    }

    lines = realloc(lines, (n + 1) * sizeof(char *));
    lines[n++] = strdup_s(line);
  }

  fclose(f);
  *nlines = n;

  return lines;
}

static int is_valid_imval(const char *s);
static int eval_label_expression(const char *s, long long *out);

static int is_valid_imval(const char *s) {
  if (!s || strlen(s) < 2)
    return 0;

  if (s[0] != '#')
    return 0;

  /* strip sign */
  char *t = strdup_s(s);
  if ((t[1] == '-' || t[1] == '+') && strlen(t) >= 3) {
    memmove(t + 1, t + 2, strlen(t + 1));
  }

  int res = 0;

  if (strcmp(t, "#0") == 0)
    res = 1;
  else if (t[1] == '\'' && t[strlen(t) - 1] == '\'' && strlen(t) == 4)
    res = 1;
  else if (startswith(t, "#0x") && strlen(t) >= 4 && str_isxdigit(t + 3))
    res = 1;
  else if (startswith(t, "#0b") && strlen(t) >= 4 && str_isbindigit(t + 3))
    res = 1;
  else if (startswith(t, "#0") && strlen(t) >= 3 && str_isoctdigit(t + 2))
    res = 1;
  else if (t[1] != '0' && str_isdigit_all(t + 1))
    res = 1;
  else {
    /* named constant (EQU) or label expression */
    long long lv;

    if (eval_label_expression(t + 1, &lv))
      res = 1;
  }

  free(t);

  return res;
}

static long long imval_to_int(const char *s) {
  char *t = strdup_s(s);
  int sign = 1;

  if (t[1] == '-') {
    sign = -1;

    memmove(t + 1, t + 2, strlen(t + 1));
  } else if (t[1] == '+') {
    memmove(t + 1, t + 2, strlen(t + 1));
  }

  long long val = 0;

  if (strcmp(t, "#0") == 0)
    val = 0;
  else if (t[1] == '\'' && strlen(t) == 4)
    val = (unsigned char)t[2];
  else if (startswith(t, "#0x"))
    val = strtoll(t + 3, NULL, 16);
  else if (startswith(t, "#0b"))
    val = strtoll(t + 3, NULL, 2);
  else if (startswith(t, "#0") && strlen(t) > 2)
    val = strtoll(t + 2, NULL, 8);
  else {
    long long lv;

    if (eval_label_expression(t + 1, &lv))
      val = lv;
    else
      val = strtoll(t + 1, NULL, 10);
  }

  free(t);

  return sign * val;
}

static int is_valid_numeric_literal(const char *s) {
  char tmp[256];

  snprintf(tmp, sizeof(tmp), "#%s", s);

  return is_valid_imval(tmp);
}

static long long numeric_literal_to_int(const char *s) {
  char tmp[256];

  snprintf(tmp, sizeof(tmp), "#%s", s);

  return imval_to_int(tmp);
}

static int is_expressable_imval(const char *s) {
  long long val = imval_to_int(s);

  int i;
  for (i = 0; i < 32; i += 2) {
    if (rotateleft32((unsigned int)val, i) < 256)
      return 1;
  }

  return 0;
}

static unsigned int encode_imval(const char *s) {
  long long imval = imval_to_int(s);
  unsigned int op2field = 0;

  int i;
  for (i = 0; i < 32; i += 2) {
    unsigned int rot = rotateleft32((unsigned int)imval, i);
    if (rot < 256) {
      op2field = ((i / 2) << 8) | rot;

      break;
    }
  }

  return op2field;
}

/* int_to_signrotimv: returns 1 on success, sets sign/rot/imv; 0 on failure */
static int int_to_signrotimv(long long n, int *sign, int *rot,
                             unsigned int *imv) {
  int i;
  for (i = 0; i < 32; i += 2) {
    unsigned int rv = rotateleft32((unsigned int)n, i);
    if (rv < 256) {
      *sign = 1;
      *rot = i / 2;
      *imv = rv;

      return 1;
    }

    rv = rotateleft32((unsigned int)(-n), i);
    if (rv < 256) {
      *sign = -1;
      *rot = i / 2;
      *imv = rv;

      return 1;
    }
  }

  return 0;
}

/* Label dictionary */
#define MAX_LABELS 4096

typedef struct {
  char name[128];
  long long addr;
} LabelEntry;

static LabelEntry labeldict[MAX_LABELS];
static int num_labels = 0;

static int label_lookup(const char *name, long long *addr) {
  int i;
  for (i = 0; i < num_labels; i++) {
    if (strcmp(labeldict[i].name, name) == 0) {
      *addr = labeldict[i].addr;

      return 1;
    }
  }

  return 0;
}

static int label_insert(const char *name, long long addr) {
  if (num_labels >= MAX_LABELS)
    return -1;

  strncpy(labeldict[num_labels].name, name, 127);

  labeldict[num_labels].name[127] = '\0';
  labeldict[num_labels].addr = addr;
  num_labels++;

  return 0;
}

/* _check_aropexpr: checks s is aropexpr, returns "" or error */
static void _check_aropexpr(const char *s, char *errbuf, size_t errmax) {
  char tmp[1024];

  strncpy(tmp, s, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  char *t = strtrim(tmp);
  if (!*t) {
    strncpy(errbuf, "invalid expression: expected nonempty string", errmax);

    return;
  }
  if (t[0] != '+' && t[0] != '-') {
    strncpy(errbuf, "invalid expression: expected \"+\" or \"-\"", errmax);

    return;
  }

  t++;

  /* find next + or - */
  int i = (int)strlen(t);
  const char *pp = strchr(t, '+');
  const char *pm = strchr(t, '-');

  if (pp)
    i = (int)(pp - t);
  if (pm && (int)(pm - t) < i)
    i = (int)(pm - t);

  char num[256];
  char rest[1024];

  strncpy(num, t, i < 256 ? i : 255);
  num[i < 256 ? i : 255] = '\0';

  char *ntrimmed = strtrim(num);

  strncpy(rest, t + i, sizeof(rest) - 1);
  rest[sizeof(rest) - 1] = '\0';

  char *rtrimmed = strtrim(rest);
  char imvcheck[260];

  snprintf(imvcheck, sizeof(imvcheck), "#%s", ntrimmed);

  if (!is_valid_imval(imvcheck)) {
    strncpy(errbuf, "invalid expression: expected numeric immediate value",
            errmax);

    return;
  }

  if (!*rtrimmed) {
    errbuf[0] = '\0';

    return;
  }

  _check_aropexpr(rtrimmed, errbuf, errmax);
}

static long long _aropexpr_to_int(const char *s) {
  char tmp[1024];

  strncpy(tmp, s, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  char *t = strtrim(tmp);
  int sign = 1;

  if (t[0] == '-')
    sign = -1;

  t++;

  int i = (int)strlen(t);
  const char *pp = strchr(t, '+');
  const char *pm = strchr(t, '-');

  if (pp)
    i = (int)(pp - t);
  if (pm && (int)(pm - t) < i)
    i = (int)(pm - t);

  char num[256];
  char rest[1024];

  strncpy(num, t, i < 256 ? i : 255);
  num[i < 256 ? i : 255] = '\0';

  char *ntrimmed = strtrim(num);

  strncpy(rest, t + i, sizeof(rest) - 1);
  rest[sizeof(rest) - 1] = '\0';

  char *rtrimmed = strtrim(rest);
  char imvarg[260];

  snprintf(imvarg, sizeof(imvarg), "#%s", ntrimmed);

  long long numint = imval_to_int(imvarg);

  if (!*rtrimmed)
    return sign * numint;

  return sign * numint + _aropexpr_to_int(rtrimmed);
}

/* check_pcrelative_expression */
static void check_pcrelative_expression(const char *s, char *errbuf,
                                        size_t errmax) {
  char label[256];
  char rest[1024];

  int i;
  for (i = 0; s[i]; i++) {
    if (!my_isalnum(s[i]) && s[i] != '_')
      break;
  }

  strncpy(label, s, i < 256 ? i : 255);
  label[i < 256 ? i : 255] = '\0';

  strncpy(rest, s + i, sizeof(rest) - 1);
  rest[sizeof(rest) - 1] = '\0';

  long long dummy;
  if (!label_lookup(label, &dummy)) {
    strncpy(errbuf, "invalid pc relative expression: undefined label", errmax);

    return;
  }

  if (!*strtrim(rest)) {
    errbuf[0] = '\0';

    return;
  }

  _check_aropexpr(strtrim(rest), errbuf, errmax);
}

static long long pcrelative_expression_to_int(const char *s,
                                              long long address) {
  char label[256];
  char rest[1024];

  int i;
  for (i = 0; s[i]; i++) {
    if (!my_isalnum(s[i]) && s[i] != '_')
      break;
  }

  strncpy(label, s, i < 256 ? i : 255);
  label[i < 256 ? i : 255] = '\0';

  strncpy(rest, s + i, sizeof(rest) - 1);
  rest[sizeof(rest) - 1] = '\0';

  long long laddr;
  label_lookup(label, &laddr);

  long long offset = laddr - (address + 8);

  char *rtrimmed = strtrim(rest);
  if (!*rtrimmed)
    return offset;

  return offset + _aropexpr_to_int(rtrimmed);
}

/* Evaluate a label expression: a defined label (address label or EQU
 * constant) optionally followed by +/- arithmetic, e.g. "buffer",
 * "buffer+4". Plain numeric literals are not accepted here: that split
 * keeps this evaluator from recursing into is_valid_imval, which calls
 * back into it for named constants. Returns 1 on success. */
static int eval_label_expression(const char *s, long long *out) {
  char tmp[256];

  strncpy(tmp, s, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  char *t = strtrim(tmp);

  if (!isalpha((unsigned char)t[0]) && t[0] != '_')
    return 0;

  int i;
  for (i = 0; t[i]; i++)
    if (!my_isalnum(t[i]) && t[i] != '_')
      break;

  char label[128];

  strncpy(label, t, i < 128 ? i : 127);
  label[i < 128 ? i : 127] = '\0';

  long long laddr;
  if (!label_lookup(label, &laddr))
    return 0;

  char *rest = strtrim(t + i);
  if (!*rest) {
    *out = laddr;

    return 1;
  }

  char err[80];

  err[0] = '\0';

  _check_aropexpr(rest, err, sizeof(err));
  if (err[0])
    return 0;

  *out = laddr + _aropexpr_to_int(rest);

  return 1;
}

/* Evaluate an absolute expression: a numeric literal or a label
 * expression. Returns 1 on success. */
static int eval_abs_expression(const char *s, long long *out) {
  char tmp[256];

  strncpy(tmp, s, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  char *t = strtrim(tmp);

  if (is_valid_numeric_literal(t)) {
    *out = numeric_literal_to_int(t);

    return 1;
  }

  return eval_label_expression(t, out);
}

/* Literal pools for the LDR rX,=expr pseudo-instruction.
 *
 * Stage 2 collects the pending literals (deduplicated by expression
 * text) and places them at each LTORG directive; an automatic pool is
 * appended at the end of the program. Each literal gets a reserved
 * label __litN so the regular PC-relative LDR machinery handles reach
 * checking and encoding. */
#define MAX_LITERALS 512
#define MAX_LITPOOLS 64

typedef struct {
  char expr[128];
  long long addr;
} Literal;

typedef struct {
  long long addr; /* address of the LTORG directive */
  int first, count;
  int pad;
} LitPool;

static Literal g_literals[MAX_LITERALS];
static int g_num_literals = 0;
static int g_pending_start = 0; /* first literal not yet placed in a pool */
static LitPool g_pools[MAX_LITPOOLS];
static int g_num_pools = 0;

static void literals_reset(void) {
  g_num_literals = 0;
  g_pending_start = 0;
  g_num_pools = 0;
}

/* Place all pending literals at an LTORG located at `address`.
 * Returns the directive's size in bytes, or -1 on overflow. */
static int litpool_place(long long address) {
  int count = g_num_literals - g_pending_start;

  if (count == 0)
    return 0;

  if (g_num_pools >= MAX_LITPOOLS)
    return -1;

  int pad = (int)((4 - (address % 4)) % 4);

  int k;
  for (k = 0; k < count; k++) {
    char lname[16];

    g_literals[g_pending_start + k].addr = address + pad + 4 * k;

    snprintf(lname, sizeof(lname), "__lit%d", g_pending_start + k);
    label_insert(lname, g_literals[g_pending_start + k].addr);
  }

  g_pools[g_num_pools].addr = address;
  g_pools[g_num_pools].first = g_pending_start;
  g_pools[g_num_pools].count = count;
  g_pools[g_num_pools].pad = pad;
  g_num_pools++;

  g_pending_start = g_num_literals;

  return pad + 4 * count;
}

static const LitPool *litpool_at(long long address) {
  int p;
  for (p = 0; p < g_num_pools; p++)
    if (g_pools[p].addr == address)
      return &g_pools[p];

  return NULL;
}

static int get_directive_size(const char *name, const char *operands,
                              long long address) {
  char u[16];

  strupper(name, u, sizeof(u));

  if (strcmp(u, "DCD") == 0 || strcmp(u, "DCDU") == 0) {
    int padding = 0;

    if (strcmp(u, "DCD") == 0)
      padding = (int)((4 - (address % 4)) % 4);

    int n;
    char **parts = split_by_comma(operands, &n);

    free_parts(parts, n);
    free(parts);

    return padding + 4 * n;
  }

  if (strcmp(u, "DCW") == 0 || strcmp(u, "DCWU") == 0) {
    int padding = 0;

    if (strcmp(u, "DCW") == 0)
      padding = (int)(address % 2);

    int n;
    char **parts = split_by_comma(operands, &n);

    free_parts(parts, n);
    free(parts);

    return padding + 2 * n;
  }

  if (strcmp(u, "ALIGN") == 0) {
    int n;
    char **parts = split_by_comma(operands, &n);
    long long alignment = 4, offset = 0;

    if (n >= 1 && parts[0][0])
      alignment = is_valid_numeric_literal(parts[0])
                      ? numeric_literal_to_int(parts[0])
                      : -1;
    if (n >= 2)
      offset = numeric_literal_to_int(parts[1]);

    free_parts(parts, n);
    free(parts);

    /* Sizes are computed before operand validation runs; a bad alignment
     * must not reach the modulo below (division by zero). */
    if (alignment <= 0)
      return -1;

    return (int)((alignment - ((address + alignment - offset) % alignment)) %
                 alignment);
  }

  if (strcmp(u, "DCB") == 0) {
    int n;
    char **parts = split_by_comma(operands, &n);
    int size = 0, i;

    for (i = 0; i < n; i++) {
      char *p = strtrim(parts[i]);

      if (p[0] == '"') {
        size += (int)strlen(p) - 2;
      } else
        size++;
    }

    free_parts(parts, n);
    free(parts);

    return size;
  }

  if (strcmp(u, "INCBIN") == 0) {
    long sz = filesize(operands);

    return (int)sz;
  }

  if (strcmp(u, "LTORG") == 0)
    return litpool_place(address);

  if (strcmp(u, "EQU") == 0)
    return 0;

  return -1;
}

static int get_instruction_size(const char *name, const char *operands,
                                long long address) {
  (void)name;
  (void)operands;
  (void)address;

  return 4;
}

static int get_size(const char *name, const char *operands, long long address) {
  if (!is_opname(name))
    return -1;

  if (is_directive(name))
    return get_directive_size(name, operands, address);

  return get_instruction_size(name, operands, address);
}

static void check_op2(const char *op2, char *errbuf, size_t errmax) {
  int n;
  char **parts = split_by_comma(op2, &n);

  errbuf[0] = '\0';

  if (n == 1) {
    if (is_reg(parts[0])) {
      free_parts(parts, n);
      free(parts);

      return;
    }

    if (!is_valid_imval(parts[0])) {
      strncpy(errbuf,
              "Invalid op2 (must be of the form \"reg\" or \"reg, shift\" or "
              "\"immediate value\")",
              errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    if (!is_expressable_imval(parts[0])) {
      strncpy(errbuf, "This immediate value cannot be encoded as op2", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (n != 2) {
    strncpy(errbuf,
            "Invalid op2 (must be of the form \"reg\" or \"reg, shift\" or "
            "\"immediate value\")",
            errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (!is_reg(parts[0])) {
    strncpy(errbuf,
            "Invalid op2 (must be of the form \"reg\" or \"reg, shift\" or "
            "\"immediate value\")",
            errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  /* insert space before # if missing */
  char shift1[512];

  strncpy(shift1, parts[1], sizeof(shift1) - 1);
  shift1[sizeof(shift1) - 1] = '\0';

  char *hp = strchr(shift1, '#');
  if (hp && hp > shift1 && !isspace((unsigned char)*(hp - 1))) {
    /* insert space before # */
    memmove(hp + 1, hp, strlen(hp) + 1);

    *hp = ' ';
  }

  /* split shift1 by whitespace */
  char sw[2][256];
  int swn = 0;
  char *sp = strtrim(shift1);

  char *tok = strtok(sp, " \t");
  while (tok && swn < 2) {
    strncpy(sw[swn++], tok, 255);

    sw[swn - 1][255] = '\0';
    tok = strtok(NULL, " \t");
  }

  if (swn == 1) {
    char u[8];

    strupper(sw[0], u, sizeof(u));

    if (strcmp(u, "RRX") != 0) {
      strncpy(errbuf, "Invalid op2", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (swn > 2) {
    strncpy(errbuf, "Invalid op2", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (!is_shiftname(sw[0])) {
    strncpy(errbuf,
            "Invalid op2 (must be of the form \"reg\" or \"reg, shift\" or "
            "\"immediate value\")",
            errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (is_reg(sw[1])) {
    if (get_reg_num(sw[1]) == 15) {
      strncpy(errbuf, "PC may not be used here", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (!is_valid_imval(sw[1])) {
    strncpy(errbuf, "Invalid op2", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  long long amount = imval_to_int(sw[1]);
  char su[8];

  strupper(sw[0], su, sizeof(su));

  if (amount >= 0 && amount <= 31) {
    free_parts(parts, n);
    free(parts);

    return;
  }

  if (amount == 32 && (strcmp(su, "LSR") == 0 || strcmp(su, "ASR") == 0)) {
    free_parts(parts, n);
    free(parts);

    return;
  }

  strncpy(errbuf,
          "Invalid immediate shift amount. Must be 0 <= amount <= 31 (or 32 "
          "for special LSR, ASR)",
          errmax);

  free_parts(parts, n);
  free(parts);
}

/* encode_op2: returns iflag, sets op2field */
static int encode_op2(const char *op2str, unsigned int *op2field) {
  int n;
  char **parts = split_by_comma(op2str, &n);
  int iflag = 0;

  *op2field = 0;

  if (n == 1) {
    if (is_reg(parts[0])) {
      iflag = 0;

      int reg = get_reg_num(parts[0]);

      *op2field = (0 << 4) | reg; /* LSL by 0, shiftfield=0 */
    } else {
      iflag = 1;
      *op2field = encode_imval(parts[0]);
    }

    free_parts(parts, n);
    free(parts);

    return iflag;
  }

  /* n==2 */
  iflag = 0;

  int reg = get_reg_num(parts[0]);
  char shift1[512];

  strncpy(shift1, parts[1], sizeof(shift1) - 1);
  shift1[sizeof(shift1) - 1] = '\0';

  char *hp = strchr(shift1, '#');
  if (hp && hp > shift1 && !isspace((unsigned char)*(hp - 1))) {
    memmove(hp + 1, hp, strlen(hp) + 1);

    *hp = ' ';
  }

  char sw[2][256];
  int swn = 0;
  char *sp = strtrim(shift1);

  char *tok = strtok(sp, " \t");
  while (tok && swn < 2) {
    strncpy(sw[swn++], tok, 255);

    sw[swn - 1][255] = '\0';
    tok = strtok(NULL, " \t");
  }

  const char *shifttype = "LSL";
  int shiftby = 0;
  int shiftbyreg = 0;

  if (swn == 1) { /* RRX */
    shifttype = "ROR";
    shiftby = 0;
    shiftbyreg = 0;
  } else {
    shifttype = sw[0];

    if (is_reg(sw[1])) {
      shiftby = get_reg_num(sw[1]);
      shiftbyreg = 1;
    } else {
      shiftby = (int)imval_to_int(sw[1]);
      shiftbyreg = 0;

      if (shiftby == 0)
        shifttype = "LSL";

      char su[8];

      strupper(shifttype, su, sizeof(su));

      if ((strcmp(su, "LSR") == 0 || strcmp(su, "ASR") == 0) && shiftby == 32)
        shiftby = 0;
    }
  }

  char su[8];

  strupper(shifttype, su, sizeof(su));

  int stnum = 0;

  if (strcmp(su, "LSL") == 0 || strcmp(su, "ASL") == 0)
    stnum = 0;
  else if (strcmp(su, "LSR") == 0)
    stnum = 1;
  else if (strcmp(su, "ASR") == 0)
    stnum = 2;
  else if (strcmp(su, "ROR") == 0)
    stnum = 3;

  int shiftfield = (stnum << 1) | shiftbyreg;

  if (shiftbyreg)
    shiftfield = (shiftby << 4) | shiftfield;
  else
    shiftfield = (shiftby << 3) | shiftfield;

  *op2field = ((unsigned int)shiftfield << 4) | (unsigned int)reg;

  free_parts(parts, n);
  free(parts);

  return iflag;
}

static void check_dataprocop(const char *name, const char *operands,
                             char *errbuf, size_t errmax) {
  int n;
  char **parts = split_by_comma(operands, &n);

  errbuf[0] = '\0';

  if (n < 2) {
    strncpy(errbuf, "Expected more operands", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (!is_reg(parts[0])) {
    strncpy(errbuf, "Expected a register as first operand", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  int idx = 1;
  char u[8];

  strupper(name, u, sizeof(u));

  if (strcmp(u, "AND") == 0 || strcmp(u, "EOR") == 0 || strcmp(u, "SUB") == 0 ||
      strcmp(u, "RSB") == 0 || strcmp(u, "ADD") == 0 || strcmp(u, "ADC") == 0 ||
      strcmp(u, "SBC") == 0 || strcmp(u, "RSC") == 0 || strcmp(u, "ORR") == 0 ||
      strcmp(u, "BIC") == 0) {
    if (!is_reg(parts[idx])) {
      strncpy(errbuf, "Expected a register as second operand", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    idx++;
  }

  if (idx >= n) {
    strncpy(errbuf, "Expected more operands", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  /* reconstruct remaining as op2 */
  char op2[1024] = "";

  int i;
  for (i = idx; i < n; i++) {
    if (i > idx)
      strncat(op2, ",", sizeof(op2) - strlen(op2) - 1);
    strncat(op2, parts[i], sizeof(op2) - strlen(op2) - 1);
  }

  check_op2(op2, errbuf, errmax);

  free_parts(parts, n);
  free(parts);
}

static void encode_dataprocop(const char *name, const char *flags,
                              const char *condcode, const char *operands,
                              ByteBuf *out) {
  int n;
  char **parts = split_by_comma(operands, &n);
  int sflag = (flags[0] == 'S' && flags[1] == '\0');
  char u[8];

  strupper(name, u, sizeof(u));

  int dest = 0, op1 = 0;
  unsigned int op2 = 0;
  int iflag = 0;

  if (is_dataproc_fullop(u)) {
    dest = get_reg_num(parts[0]);
    op1 = get_reg_num(parts[1]);

    char op2str[1024] = "";

    int i;
    for (i = 2; i < n; i++) {
      if (i > 2)
        strncat(op2str, ",", sizeof(op2str) - strlen(op2str) - 1);
      strncat(op2str, parts[i], sizeof(op2str) - strlen(op2str) - 1);
    }

    iflag = encode_op2(op2str, &op2);
  } else if (is_dataproc_testop(u)) {
    dest = 0;
    op1 = get_reg_num(parts[0]);

    char op2str[1024] = "";

    int i;
    for (i = 1; i < n; i++) {
      if (i > 1)
        strncat(op2str, ",", sizeof(op2str) - strlen(op2str) - 1);
      strncat(op2str, parts[i], sizeof(op2str) - strlen(op2str) - 1);
    }

    iflag = encode_op2(op2str, &op2);
    sflag = 1;
  } else { /* movop */
    dest = get_reg_num(parts[0]);
    op1 = 0;

    char op2str[1024] = "";

    int i;
    for (i = 1; i < n; i++) {
      if (i > 1)
        strncat(op2str, ",", sizeof(op2str) - strlen(op2str) - 1);
      strncat(op2str, parts[i], sizeof(op2str) - strlen(op2str) - 1);
    }

    iflag = encode_op2(op2str, &op2);
  }

  int ccval = get_condcode_value(condcode);
  int dpn = get_dataprocop_num(u);
  unsigned char buf[4];
  int offs[] = {28, 25, 21, 20, 16, 12, 0};
  int lens[] = {4, 1, 4, 1, 4, 4, 12};
  unsigned int vals[] = {(unsigned int)ccval,
                         (unsigned int)iflag,
                         (unsigned int)dpn,
                         (unsigned int)sflag,
                         (unsigned int)op1,
                         (unsigned int)dest,
                         op2};

  encode_32bit_arr(offs, lens, vals, 7, buf);
  bigendian_to_littleendian(buf, 4);
  bb_append(out, buf, 4);

  free_parts(parts, n);
  free(parts);
}

static void check_branchop(const char *name, const char *operands,
                           long long address, char *errbuf, size_t errmax) {
  char u[8];

  strupper(name, u, sizeof(u));

  errbuf[0] = '\0';

  /* strip leading whitespace */
  char ops[1024];

  strncpy(ops, operands, sizeof(ops) - 1);
  ops[sizeof(ops) - 1] = '\0';

  char *op = strtrim(ops);

  /* split by comma */
  int n;
  char **parts = split_by_comma(op, &n);

  if (n != 1) {
    snprintf(errbuf, errmax, "Invalid number of operands: expected 1, got %d",
             n);

    free_parts(parts, n);
    free(parts);

    return;
  }

  char *single = strtrim(parts[0]);

  if (strcmp(u, "BX") == 0) {
    if (!is_reg(single)) {
      strncpy(errbuf, "Invalid Operand: Expected register", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    if (get_reg_num(single) == 15) {
      strncpy(errbuf, "PC not allowed here (causes undefined behaviour)",
              errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }
  } else {
    check_pcrelative_expression(single, errbuf, errmax);

    if (errbuf[0]) {
      free_parts(parts, n);
      free(parts);

      return;
    }

    long long offset = pcrelative_expression_to_int(single, address);
    if (offset % 4 != 0) {
      strncpy(errbuf, "Offset must be aligned to four bytes", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    offset >>= 2;
    if (offset < -(1 << 23) || offset > (1 << 23) - 1) {
      strncpy(errbuf, "Branch target too far away", errmax);
    }
  }

  free_parts(parts, n);
  free(parts);
}

static void encode_branchop(const char *name, const char *condcode,
                            const char *operands, long long address,
                            ByteBuf *out) {
  char u[8];
  char ops[1024];

  strupper(name, u, sizeof(u));
  strncpy(ops, operands, sizeof(ops) - 1);
  ops[sizeof(ops) - 1] = '\0';

  char *op = strtrim(ops);
  unsigned char buf[4];

  if (strcmp(u, "BX") == 0) {
    int rn = get_reg_num(op);
    int ccval = get_condcode_value(condcode);
    int offs[] = {28, 4, 0};
    int lens[] = {4, 24, 4};
    unsigned int vals[] = {(unsigned int)ccval, 0x12FFF1, (unsigned int)rn};

    encode_32bit_arr(offs, lens, vals, 3, buf);
    bigendian_to_littleendian(buf, 4);
    bb_append(out, buf, 4);
  } else {
    long long offset = pcrelative_expression_to_int(op, address);

    offset >>= 2;
    if (offset < 0)
      offset += (1 << 24);

    int ccval = get_condcode_value(condcode);
    int lflag = (strcmp(u, "BL") == 0) ? 1 : 0;
    int offs[] = {28, 25, 24, 0};
    int lens[] = {4, 3, 1, 24};
    unsigned int vals[] = {(unsigned int)ccval, 0x5, (unsigned int)lflag,
                           (unsigned int)(offset & 0xFFFFFF)};

    encode_32bit_arr(offs, lens, vals, 4, buf);
    bigendian_to_littleendian(buf, 4);
    bb_append(out, buf, 4);
  }
}

static void check_psrtransop(const char *name, const char *operands,
                             char *errbuf, size_t errmax) {
  errbuf[0] = '\0';

  int n;
  char **parts = split_by_comma(operands, &n);
  char u[8];

  strupper(name, u, sizeof(u));

  if (n != 2) {
    snprintf(errbuf, errmax, "Invalid number of operands: expected 2, got %d",
             n);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (strcmp(u, "MRS") == 0) {
    if (!is_reg(parts[0])) {
      strncpy(errbuf, "Invalid operand: expected register", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    if (get_reg_num(parts[0]) == 15) {
      strncpy(errbuf, "PC is not allowed here", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    char p1u[16];

    strupper(parts[1], p1u, sizeof(p1u));

    if (strcmp(p1u, "SPSR") != 0 && strcmp(p1u, "SPSR_ALL") != 0 &&
        strcmp(p1u, "CPSR") != 0 && strcmp(p1u, "CPSR_ALL") != 0) {
      strncpy(errbuf, "Invalid operand: expected psr", errmax);
    }
  } else {
    if (!is_psr(parts[0])) {
      strncpy(errbuf, "Invalid operand: expected psr", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    char p0u[16];

    strupper(parts[0], p0u, sizeof(p0u));

    if (!endswith(p0u, "FLG")) {
      if (!is_reg(parts[1])) {
        strncpy(errbuf, "Invalid operand: expected register", errmax);

        free_parts(parts, n);
        free(parts);

        return;
      }

      if (get_reg_num(parts[1]) == 15) {
        strncpy(errbuf, "PC is not allowed here", errmax);
      }
    } else {
      if (is_reg(parts[1])) {
        if (get_reg_num(parts[1]) == 15)
          strncpy(errbuf, "PC is not allowed here", errmax);
      } else {
        if (!is_valid_imval(parts[1])) {
          strncpy(errbuf,
                  "Invalid operand: expected register or immediate value",
                  errmax);

          free_parts(parts, n);
          free(parts);

          return;
        }
        if (!is_expressable_imval(parts[1]))
          strncpy(errbuf, "This immediate value cannot be encoded", errmax);
      }
    }
  }

  free_parts(parts, n);
  free(parts);
}

static void encode_psrtransop(const char *name, const char *condcode,
                              const char *operands, ByteBuf *out) {
  int n;
  char **parts = split_by_comma(operands, &n);
  char u[8];

  strupper(name, u, sizeof(u));

  unsigned char buf[4];

  if (strcmp(u, "MRS") == 0) {
    int rd = get_reg_num(parts[0]);
    char p1u[16];

    strupper(parts[1], p1u, sizeof(p1u));

    int spsrflag = (p1u[0] == 'S') ? 1 : 0;
    int ccval = get_condcode_value(condcode);
    int offs[] = {28, 23, 22, 16, 12};
    int lens[] = {4, 5, 1, 6, 4};
    unsigned int vals[] = {(unsigned int)ccval, 0x2, (unsigned int)spsrflag,
                           0xF, (unsigned int)rd};

    encode_32bit_arr(offs, lens, vals, 5, buf);
    bigendian_to_littleendian(buf, 4);
    bb_append(out, buf, 4);
  } else {
    char p0u[16];

    strupper(parts[0], p0u, sizeof(p0u));

    int spsrflag = (p0u[0] == 'S') ? 1 : 0;
    int allflag = endswith(p0u, "FLG") ? 0 : 1;
    int iflag;
    unsigned int op2field;

    if (is_reg(parts[1])) {
      iflag = 0;
      op2field = (unsigned int)get_reg_num(parts[1]);
    } else {
      iflag = 1;
      op2field = encode_imval(parts[1]);
    }

    int ccval = get_condcode_value(condcode);
    int offs[] = {28, 25, 23, 22, 17, 16, 12, 0};
    int lens[] = {4, 1, 2, 1, 5, 1, 4, 12};
    unsigned int vals[] = {(unsigned int)ccval,
                           (unsigned int)iflag,
                           0x2,
                           (unsigned int)spsrflag,
                           0x14,
                           (unsigned int)allflag,
                           0xF,
                           op2field};

    encode_32bit_arr(offs, lens, vals, 8, buf);
    bigendian_to_littleendian(buf, 4);
    bb_append(out, buf, 4);
  }

  free_parts(parts, n);
  free(parts);
}

/* swiop */
static void check_swiop(const char *name, const char *operands, char *errbuf,
                        size_t errmax) {
  (void)name;
  errbuf[0] = '\0';

  int n;
  char **parts = split_by_comma(operands, &n);

  if (n != 1) {
    snprintf(errbuf, errmax, "Invalid number of operands: expected 1, got %d",
             n);

    free_parts(parts, n);
    free(parts);

    return;
  }

  /* accept both "#imm" (nAssembler style) and bare "imm" (ARM style) */
  char *op = strtrim(parts[0]);
  if (!(op[0] == '#' ? is_valid_imval(op) : is_valid_numeric_literal(op))) {
    strncpy(errbuf, "Invalid operand: expected immediate value", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  long long com =
      (op[0] == '#') ? imval_to_int(op) : numeric_literal_to_int(op);
  if (com > (1 << 24) - 1)
    strncpy(errbuf, "Operand greater than 2^24-1", errmax);
  else if (com < -(1 << 23))
    strncpy(errbuf, "Operand lower than -2^23", errmax);

  free_parts(parts, n);
  free(parts);
}

static void encode_swiop(const char *name, const char *condcode,
                         const char *operands, ByteBuf *out) {
  (void)name;

  char ops[1024];

  strncpy(ops, operands, sizeof(ops) - 1);
  ops[sizeof(ops) - 1] = '\0';

  char *op = strtrim(ops);
  int ccval = get_condcode_value(condcode);
  long long com =
      (op[0] == '#') ? imval_to_int(op) : numeric_literal_to_int(op);
  unsigned char buf[4];
  int offs[] = {28, 24, 0};
  int lens[] = {4, 4, 24};
  unsigned int vals[] = {(unsigned int)ccval, 0xF,
                         (unsigned int)(com & 0xFFFFFF)};

  encode_32bit_arr(offs, lens, vals, 3, buf);
  bigendian_to_littleendian(buf, 4);
  bb_append(out, buf, 4);
}

static void check_miscarithmeticop(const char *name, const char *operands,
                                   char *errbuf, size_t errmax) {
  (void)name;
  errbuf[0] = '\0';

  int n;
  char **parts = split_by_comma(operands, &n);

  if (n != 2) {
    snprintf(errbuf, errmax, "Invalid number of operands: expected 2, got %d",
             n);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (!is_reg(parts[0]) || !is_reg(parts[1])) {
    strncpy(errbuf, "Invalid operand: expected register", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (get_reg_num(parts[0]) == 15 || get_reg_num(parts[1]) == 15)
    strncpy(errbuf, "PC is not allowed here", errmax);
  free_parts(parts, n);
  free(parts);
}

static void encode_miscarithmeticop(const char *name, const char *condcode,
                                    const char *operands, ByteBuf *out) {
  (void)name;

  int n;
  char **parts = split_by_comma(operands, &n);
  int rd = get_reg_num(parts[0]), rm = get_reg_num(parts[1]);
  int ccval = get_condcode_value(condcode);
  unsigned char buf[4];
  int offs[] = {28, 16, 12, 4, 0};
  int lens[] = {4, 12, 4, 8, 4};
  unsigned int vals[] = {(unsigned int)ccval, 0x16F, (unsigned int)rd, 0xF1,
                         (unsigned int)rm};

  encode_32bit_arr(offs, lens, vals, 5, buf);
  bigendian_to_littleendian(buf, 4);
  bb_append(out, buf, 4);

  free_parts(parts, n);
  free(parts);
}

static void check_coprocregtransop(const char *name, const char *operands,
                                   char *errbuf, size_t errmax) {
  (void)name;
  errbuf[0] = '\0';

  int n;
  char **parts = split_by_comma(operands, &n);

  if (n != 5 && n != 6) {
    snprintf(errbuf, errmax,
             "Invalid number of operands. Expected 5 or 6, got %d", n);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (!is_coproc(parts[0])) {
    strncpy(errbuf, "Expected coprocessor (e.g. p15)", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (!is_valid_numeric_literal(parts[1])) {
    strncpy(errbuf, "Expected numeric literal", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  long long v = numeric_literal_to_int(parts[1]);
  if (v < 0 || v > 7) {
    strncpy(errbuf, "Must be in range 0 to 7", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (!is_reg(parts[2])) {
    strncpy(errbuf, "Expected register", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (!is_coprocreg(parts[3]) || !is_coprocreg(parts[4])) {
    strncpy(errbuf, "Expected coprocessor register (e.g. c0)", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (n == 6) {
    if (!is_valid_numeric_literal(parts[5])) {
      strncpy(errbuf, "Expected numeric literal", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    v = numeric_literal_to_int(parts[5]);
    if (v < 0 || v > 7)
      strncpy(errbuf, "Must be in range 0 to 7", errmax);
  }

  free_parts(parts, n);
  free(parts);
}

static void encode_coprocregtransop(const char *name, const char *condcode,
                                    const char *operands, ByteBuf *out) {
  int n;
  char **parts = split_by_comma(operands, &n);
  char u[8];

  strupper(name, u, sizeof(u));

  int cpnum = atoi(parts[0] + 1);
  int cpopc = (int)numeric_literal_to_int(parts[1]);
  int rd = get_reg_num(parts[2]);
  int crn = atoi(parts[3] + 1);
  int crm = atoi(parts[4] + 1);
  int cp = (n == 6) ? (int)numeric_literal_to_int(parts[5]) : 0;
  int lflag = (strcmp(u, "MRC") == 0) ? 1 : 0;
  int ccval = get_condcode_value(condcode);
  unsigned char buf[4];
  int offs[] = {28, 24, 21, 20, 16, 12, 8, 5, 4, 0};
  int lens[] = {4, 4, 3, 1, 4, 4, 4, 3, 1, 4};
  unsigned int vals[] = {(unsigned int)ccval,
                         0xE,
                         (unsigned int)cpopc,
                         (unsigned int)lflag,
                         (unsigned int)crn,
                         (unsigned int)rd,
                         (unsigned int)cpnum,
                         (unsigned int)cp,
                         0x1,
                         (unsigned int)crm};

  encode_32bit_arr(offs, lens, vals, 10, buf);
  bigendian_to_littleendian(buf, 4);
  bb_append(out, buf, 4);

  free_parts(parts, n);
  free(parts);
}

static void check_mulop(const char *name, const char *operands, char *errbuf,
                        size_t errmax) {
  errbuf[0] = '\0';

  char u[8];

  strupper(name, u, sizeof(u));

  int n;
  char **parts = split_by_comma(operands, &n);

  int expected = (strcmp(u, "MLA") == 0) ? 4 : 3;
  if (n != expected) {
    snprintf(errbuf, errmax, "Expected %d operands, got %d", expected, n);

    free_parts(parts, n);
    free(parts);

    return;
  }

  int i;
  for (i = 0; i < n; i++)
    if (!is_reg(parts[i])) {
      strncpy(errbuf, "Expected a register", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

  int regs[4];

  for (i = 0; i < n; i++)
    regs[i] = get_reg_num(parts[i]);

  for (i = 0; i < n; i++)
    if (regs[i] == 15) {
      strncpy(errbuf, "PC is not allowed here", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

  if (regs[0] == regs[1])
    strncpy(errbuf, "Rd must be different from Rm", errmax);

  free_parts(parts, n);
  free(parts);
}

static void encode_mulop(const char *name, const char *flags,
                         const char *condcode, const char *operands,
                         ByteBuf *out) {
  char u[8];

  strupper(name, u, sizeof(u));

  int n;
  char **parts = split_by_comma(operands, &n);
  int sflag = (flags[0] == 'S' && flags[1] == '\0');
  int rd = get_reg_num(parts[0]), rm = get_reg_num(parts[1]),
      rs = get_reg_num(parts[2]);
  int rn = 0, aflag = 0;

  if (strcmp(u, "MLA") == 0) {
    rn = get_reg_num(parts[3]);
    aflag = 1;
  }

  int ccval = get_condcode_value(condcode);
  unsigned char buf[4];
  int offs[] = {28, 21, 20, 16, 12, 8, 4, 0};
  int lens[] = {4, 1, 1, 4, 4, 4, 4, 4};
  unsigned int vals[] = {(unsigned int)ccval,
                         (unsigned int)aflag,
                         (unsigned int)sflag,
                         (unsigned int)rd,
                         (unsigned int)rn,
                         (unsigned int)rs,
                         0x9,
                         (unsigned int)rm};

  encode_32bit_arr(offs, lens, vals, 8, buf);
  bigendian_to_littleendian(buf, 4);
  bb_append(out, buf, 4);

  free_parts(parts, n);
  free(parts);
}

static void check_longmulop(const char *name, const char *operands,
                            char *errbuf, size_t errmax) {
  (void)name;
  errbuf[0] = '\0';

  int n;
  char **parts = split_by_comma(operands, &n);

  if (n != 4) {
    snprintf(errbuf, errmax, "Expected 4 operands, got %d", n);

    free_parts(parts, n);
    free(parts);

    return;
  }

  int i;
  for (i = 0; i < 4; i++)
    if (!is_reg(parts[i])) {
      strncpy(errbuf, "Expected a register", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

  int regs[4];

  for (i = 0; i < 4; i++)
    regs[i] = get_reg_num(parts[i]);

  for (i = 0; i < 4; i++)
    if (regs[i] == 15) {
      strncpy(errbuf, "PC is not allowed here", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

  if (regs[0] == regs[1] || regs[0] == regs[2] || regs[1] == regs[2])
    strncpy(errbuf, "RdHi, RdLo and Rm must all be different registers",
            errmax);

  free_parts(parts, n);
  free(parts);
}

static void encode_longmulop(const char *name, const char *flags,
                             const char *condcode, const char *operands,
                             ByteBuf *out) {
  int n;
  char **parts = split_by_comma(operands, &n);
  int rdlo = get_reg_num(parts[0]), rdhi = get_reg_num(parts[1]),
      rm = get_reg_num(parts[2]), rs = get_reg_num(parts[3]);
  int sflag = (flags[0] == 'S' && flags[1] == '\0');
  char u[8];

  strupper(name, u, sizeof(u));

  int signedflag = (u[0] == 'S') ? 1 : 0;
  int aflag = (u[3] == 'A') ? 1 : 0;
  int ccval = get_condcode_value(condcode);
  unsigned char buf[4];
  int offs[] = {28, 23, 22, 21, 20, 16, 12, 8, 4, 0};
  int lens[] = {4, 5, 1, 1, 1, 4, 4, 4, 4, 4};
  unsigned int vals[] = {(unsigned int)ccval,
                         0x1,
                         (unsigned int)signedflag,
                         (unsigned int)aflag,
                         (unsigned int)sflag,
                         (unsigned int)rdhi,
                         (unsigned int)rdlo,
                         (unsigned int)rs,
                         0x9,
                         (unsigned int)rm};

  encode_32bit_arr(offs, lens, vals, 10, buf);
  bigendian_to_littleendian(buf, 4);
  bb_append(out, buf, 4);

  free_parts(parts, n);
  free(parts);
}

static void is_valid_addresspart(int hsflag, const char *addresspart_in,
                                 int tflag, long long address, char *errbuf,
                                 size_t errmax) {
  errbuf[0] = '\0';

  char addrpart[2048];

  strncpy(addrpart, addresspart_in, sizeof(addrpart) - 1);
  addrpart[sizeof(addrpart) - 1] = '\0';

  if (!addrpart[0]) {
    strncpy(errbuf, "Address part is missing", errmax);

    return;
  }

  if (addrpart[0] != '[') {
    /* must be a label */
    char *label = strtrim(addrpart);
    long long laddr;

    if (!label_lookup(label, &laddr)) {
      strncpy(errbuf, "Expected bracket or label", errmax);

      return;
    }

    long long offset = laddr - address - 8;
    char tmp[256];

    snprintf(tmp, sizeof(tmp), "[PC, #%lld]", offset);
    strncpy(addrpart, tmp, sizeof(addrpart) - 1);
  }

  int writeback = 0;

  size_t alen = strlen(addrpart);
  if (alen > 0 && addrpart[alen - 1] == '!') {
    writeback = 1;
    addrpart[alen - 1] = '\0';
    alen--;

    char *t = strtrim(addrpart);

    memmove(addrpart, t, strlen(t) + 1);
  }

  alen = strlen(addrpart);

  int preindexed = 0;

  if (alen > 0 && addrpart[alen - 1] == ']') {
    preindexed = 1;
    addrpart[alen - 1] = '\0';
    alen--;

    char *t = strtrim(addrpart);

    memmove(addrpart, t, strlen(t) + 1);
  } else {
    if (writeback) {
      strncpy(errbuf, "! is only allowed for preindexed addressing", errmax);

      return;
    }
  }

  /* strip leading [ */
  if (addrpart[0] == '[') {
    memmove(addrpart, addrpart + 1, strlen(addrpart));

    char *t = strtrim(addrpart);

    memmove(addrpart, t, strlen(t) + 1);
  }

  int n;
  char **parts = split_by_comma(addrpart, &n);

  if (n < 1 || n > 3 || (hsflag && n > 2)) {
    strncpy(errbuf, "Invalid addresspart", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (!preindexed) {
    char *p0 = parts[0];
    size_t p0l = strlen(p0);
    if (p0l == 0 || p0[p0l - 1] != ']') {
      strncpy(errbuf, "Expected closing ]", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    p0[p0l - 1] = '\0';

    char *t = strtrim(p0);

    memmove(p0, t, strlen(t) + 1);
  }

  if (!is_reg(parts[0])) {
    strncpy(errbuf, "Expected register as base", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (writeback && get_reg_num(parts[0]) == 15) {
    strncpy(errbuf,
            "Write-back should not be used when PC is the base register",
            errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (preindexed && tflag) {
    strncpy(errbuf, "T-flag is not allowed when pre-indexing is used", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (n == 1) {
    free_parts(parts, n);
    free(parts);

    return;
  }

  if (is_valid_imval(parts[1])) {
    long long nv = imval_to_int(parts[1]);
    long long limit = hsflag ? 0xFF : 0xFFF;

    if (nv > limit) {
      snprintf(errbuf, errmax, "Offset too high (max. %lld)", limit);

      free_parts(parts, n);
      free(parts);

      return;
    }

    if (nv < -limit) {
      snprintf(errbuf, errmax, "Offset too low (min. %lld)", -limit);

      free_parts(parts, n);
      free(parts);

      return;
    }

    if (n > 2) {
      strncpy(errbuf, "Too many operands", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    free_parts(parts, n);
    free(parts);

    return;
  } else {
    size_t p1l = strlen(parts[1]);
    if (p1l < 2) {
      strncpy(errbuf, "Invalid offset", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    char *p1 = parts[1];
    if (p1[0] == '+' || p1[0] == '-') {
      memmove(p1, p1 + 1, strlen(p1));
    }

    if (!is_reg(p1)) {
      strncpy(errbuf, "Invalid offset: must be register or immediate value",
              errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    if (get_reg_num(p1) == 15) {
      strncpy(errbuf, "PC is not allowed as offset", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    if (!preindexed && get_reg_num(parts[0]) == get_reg_num(p1)) {
      strncpy(errbuf,
              "Manual says: post-indexed with Rm = Rn should not be used",
              errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    if (n == 2) {
      free_parts(parts, n);
      free(parts);

      return;
    }

    if (hsflag) {
      strncpy(errbuf, "Expected less operands", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    char *shift = strtrim(parts[2]);
    if (strlen(shift) < 3) {
      strncpy(errbuf, "Invalid shift expression", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    char shu[8];

    strupper(shift, shu, sizeof(shu));

    if (strcmp(shu, "RRX") == 0) {
      free_parts(parts, n);
      free(parts);

      return;
    }

    /* split shift */
    char sw[2][256];
    int swn = 0;

    char *tok = strtok(shift, " \t");
    while (tok && swn < 2) {
      strncpy(sw[swn++], tok, 255);
      sw[swn - 1][255] = '\0';

      tok = strtok(NULL, " \t");
    }

    if (swn == 1 && strchr(sw[0], '#')) {
      char *hp = strchr(sw[0], '#');

      strncpy(sw[1], hp, 255);
      sw[1][255] = '\0';
      *hp = '\0';

      swn = 2;
    }

    if (swn != 2) {
      strncpy(errbuf, "Invalid shift expression", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    if (!is_shiftname(sw[0])) {
      strncpy(errbuf, "Invalid shift name", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    if (is_reg(sw[1])) {
      strncpy(errbuf,
              "Register specified shift amount is not allowed in data transfer "
              "instructions",
              errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    if (!is_valid_imval(sw[1])) {
      strncpy(errbuf, "Invalid shift amount", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    long long sv = imval_to_int(sw[1]);
    char sw0u[8];

    strupper(sw[0], sw0u, sizeof(sw0u));

    if (sv >= 0 && sv <= 31) {
      free_parts(parts, n);
      free(parts);

      return;
    }

    if (sv == 32 && (strcmp(sw0u, "LSR") == 0 || strcmp(sw0u, "ASR") == 0)) {
      free_parts(parts, n);
      free(parts);

      return;
    }

    strncpy(errbuf,
            "Invalid immediate shift amount. Must be 0 <= amount <= 31 (or 32 "
            "for special LSR, ASR)",
            errmax);
  }

  free_parts(parts, n);
  free(parts);
}

static void check_singledatatransop(const char *flags, const char *operands,
                                    long long address, char *errbuf,
                                    size_t errmax) {
  errbuf[0] = '\0';

  int tflag = (strchr(flags, 'T') != NULL);
  char *parts[2];

  int n = split_by_comma_maxsplit(operands, 1, parts, 2);
  if (n != 2) {
    strncpy(errbuf, "Expected more operands", errmax);

    if (n > 0)
      free(parts[0]);

    return;
  }

  if (!is_reg(parts[0])) {
    strncpy(errbuf, "Expected register", errmax);

    free(parts[0]);
    free(parts[1]);

    return;
  }

  is_valid_addresspart(0, parts[1], tflag, address, errbuf, errmax);

  free(parts[0]);
  free(parts[1]);
}

static void check_halfsigneddatatransop(const char *operands, long long address,
                                        char *errbuf, size_t errmax) {
  errbuf[0] = '\0';

  char *parts[2];

  int n = split_by_comma_maxsplit(operands, 1, parts, 2);
  if (n != 2) {
    strncpy(errbuf, "Expected more operands", errmax);

    if (n > 0)
      free(parts[0]);

    return;
  }

  if (!is_reg(parts[0])) {
    strncpy(errbuf, "Expected register", errmax);

    free(parts[0]);
    free(parts[1]);

    return;
  }

  is_valid_addresspart(1, parts[1], 0, address, errbuf, errmax);

  free(parts[0]);
  free(parts[1]);
}

/* parse_datatrans: common parsing for single and half-signed datatrans */
typedef struct {
  int writeback, preindexed, loadflag, upflag, iflag, rd, rn;
  unsigned int offset;
} DatatransResult;

static DatatransResult
parse_datatrans(const char *name, const char *operands_in, long long address) {
  DatatransResult r = {0};
  char ops[2048];

  strncpy(ops, operands_in, sizeof(ops) - 1);
  ops[sizeof(ops) - 1] = '\0';

  /* check if label addressing (no '[') */
  if (!strchr(ops, '[')) {
    char *parts[2];

    int n = split_by_comma_maxsplit(ops, 1, parts, 2);
    if (n >= 2) {
      char *label = strtrim(parts[1]);
      long long laddr;

      label_lookup(label, &laddr);

      long long offset = laddr - address - 8;
      char tmp[256];

      snprintf(tmp, sizeof(tmp), "%s,[PC, #%lld]", parts[0], offset);
      strncpy(ops, tmp, sizeof(ops) - 1);

      free(parts[0]);
      free(parts[1]);
    }
  }

  r.writeback = (ops[strlen(ops) - 1] == '!');
  if (r.writeback) {
    ops[strlen(ops) - 1] = '\0';

    char *t = strtrim(ops);

    memmove(ops, t, strlen(t) + 1);
  }

  r.preindexed = (ops[strlen(ops) - 1] == ']');
  if (r.preindexed) {
    ops[strlen(ops) - 1] = '\0';

    char *t = strtrim(ops);

    memmove(ops, t, strlen(t) + 1);
  }

  char u[8];

  strupper(name, u, sizeof(u));

  r.loadflag = (strcmp(u, "LDR") == 0);

  /* split by comma, but only the first */
  char *parts[8];
  int n = split_by_comma_maxsplit(ops, 1, parts, 2);
  if (n < 2) {
    /* unreachable: the syntax check guarantees two parts */
    if (n == 1)
      free(parts[0]);

    return r;
  }
  /* parts[0] is rd, parts[1] is address part without closing ] */
  r.rd = get_reg_num(parts[0]);
  /* strip leading [ from parts[1] */
  char *ap = parts[1];
  if (ap[0] == '[') {
    memmove(ap, ap + 1, strlen(ap));

    char *t = strtrim(ap);

    memmove(ap, t, strlen(t) + 1);
  }

  if (ap[strlen(ap) - 1] == ']') {
    ap[strlen(ap) - 1] = '\0';

    char *t = strtrim(ap);

    memmove(ap, t, strlen(t) + 1);
  }

  /* now split ap by comma */
  char *subparts[4];
  int sn = split_by_comma_maxsplit(ap, 3, subparts, 4);

  /* post-indexed: the closing ] sits right after the base register */
  size_t s0l = strlen(subparts[0]);
  if (s0l > 0 && subparts[0][s0l - 1] == ']')
    subparts[0][s0l - 1] = '\0';

  r.rn = get_reg_num(strtrim(subparts[0]));
  r.offset = 0;
  r.upflag = 1;
  r.iflag = 0;

  if (sn > 1) {
    if (is_valid_imval(subparts[1])) {
      r.iflag = 0;

      long long ov = imval_to_int(subparts[1]);

      r.upflag = (ov >= 0);

      if (ov < 0)
        ov = -ov;

      r.offset = (unsigned int)ov;
    } else {
      r.iflag = 1;
      r.upflag = 1;

      char *ostr = subparts[1];
      if (ostr[0] == '-') {
        r.upflag = 0;

        memmove(ostr, ostr + 1, strlen(ostr));
      } else if (ostr[0] == '+') {
        memmove(ostr, ostr + 1, strlen(ostr));
      }

      int rm = get_reg_num(ostr);
      int shiftfield = 0;

      if (sn >= 3) {
        /* shift */
        char *shiftstr = subparts[2];
        char sw[2][256];
        int swn = 0;

        char *tok = strtok(shiftstr, " \t");
        while (tok && swn < 2) {
          strncpy(sw[swn++], tok, 255);
          sw[swn - 1][255] = '\0';

          tok = strtok(NULL, " \t");
        }

        const char *shifttype = "LSL";
        int shiftby = 0;

        if (swn == 1) {
          shifttype = "ROR";
          shiftby = 0;
        } else {
          shifttype = sw[0];
          shiftby = (int)imval_to_int(sw[1]);

          if (shiftby == 0)
            shifttype = "LSL";

          char su[8];

          strupper(shifttype, su, sizeof(su));

          if ((strcmp(su, "LSR") == 0 || strcmp(su, "ASR") == 0) &&
              shiftby == 32)
            shiftby = 0;
        }

        char su[8];

        strupper(shifttype, su, sizeof(su));
        int stnum = 0;

        if (strcmp(su, "LSL") == 0 || strcmp(su, "ASL") == 0)
          stnum = 0;
        else if (strcmp(su, "LSR") == 0)
          stnum = 1;
        else if (strcmp(su, "ASR") == 0)
          stnum = 2;
        else if (strcmp(su, "ROR") == 0)
          stnum = 3;

        shiftfield = (shiftby << 3) | (stnum << 1);
      }

      r.offset = ((unsigned int)shiftfield << 4) | (unsigned int)rm;
    }
  }

  free(parts[0]);
  free(parts[1]);

  int i;
  for (i = 0; i < sn; i++)
    free(subparts[i]);

  return r;
}

static void encode_singledatatransop(const char *name, const char *flags,
                                     const char *condcode, const char *operands,
                                     long long address, ByteBuf *out) {
  DatatransResult r = parse_datatrans(name, operands, address);

  if (strchr(flags, 'T'))
    r.writeback = 1;

  int byteflag = (strchr(flags, 'B') != NULL);
  int ccval = get_condcode_value(condcode);
  unsigned char buf[4];
  int offs[] = {28, 26, 25, 24, 23, 22, 21, 20, 16, 12, 0};
  int lens[] = {4, 2, 1, 1, 1, 1, 1, 1, 4, 4, 12};
  unsigned int vals[] = {(unsigned int)ccval,
                         0x1,
                         (unsigned int)r.iflag,
                         (unsigned int)r.preindexed,
                         (unsigned int)r.upflag,
                         (unsigned int)byteflag,
                         (unsigned int)r.writeback,
                         (unsigned int)r.loadflag,
                         (unsigned int)r.rn,
                         (unsigned int)r.rd,
                         r.offset};

  encode_32bit_arr(offs, lens, vals, 11, buf);
  bigendian_to_littleendian(buf, 4);
  bb_append(out, buf, 4);
}

static void encode_halfsigneddatatransop(const char *name, const char *flags,
                                         const char *condcode,
                                         const char *operands,
                                         long long address, ByteBuf *out) {
  DatatransResult r = parse_datatrans(name, operands, address);
  int hflag = (strchr(flags, 'H') != NULL);
  int sflag = (strchr(flags, 'S') != NULL);
  int ccval = get_condcode_value(condcode);
  int not_iflag = r.iflag ? 0 : 1;
  unsigned char buf[4];
  int offs[] = {28, 24, 23, 22, 21, 20, 16, 12, 8, 7, 6, 5, 4, 0};
  int lens[] = {4, 1, 1, 1, 1, 1, 4, 4, 4, 1, 1, 1, 1, 4};
  unsigned int vals[] = {(unsigned int)ccval,
                         (unsigned int)r.preindexed,
                         (unsigned int)r.upflag,
                         (unsigned int)not_iflag,
                         (unsigned int)r.writeback,
                         (unsigned int)r.loadflag,
                         (unsigned int)r.rn,
                         (unsigned int)r.rd,
                         (r.offset >> 4) & 0xF,
                         0x1,
                         (unsigned int)sflag,
                         (unsigned int)hflag,
                         0x1,
                         r.offset & 0xF};

  encode_32bit_arr(offs, lens, vals, 14, buf);
  bigendian_to_littleendian(buf, 4);
  bb_append(out, buf, 4);
}

/* swapop */
static void check_swapop(const char *operands, char *errbuf, size_t errmax) {
  errbuf[0] = '\0';

  int n;
  char **parts = split_by_comma(operands, &n);

  if (n != 3) {
    snprintf(errbuf, errmax, "Expected 3 operands, got %d", n);

    free_parts(parts, n);
    free(parts);

    return;
  }
  if (strlen(parts[2]) < 4 || parts[2][0] != '[' ||
      parts[2][strlen(parts[2]) - 1] != ']') {
    strncpy(errbuf, "Missing brackets around third operand of swap instruction",
            errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  char tmp[256];
  strncpy(tmp, parts[2] + 1, sizeof(tmp) - 1);

  tmp[sizeof(tmp) - 1] = '\0';
  tmp[strlen(tmp) - 1] = '\0';

  char *inner = strtrim(tmp);

  memmove(parts[2], inner, strlen(inner) + 1);

  int i;
  for (i = 0; i < n; i++)
    if (!is_reg(parts[i])) {
      strncpy(errbuf, "Only registers are allowed here", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

  for (i = 0; i < n; i++)
    if (get_reg_num(parts[i]) == 15) {
      strncpy(errbuf, "PC is not allowed here", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

  free_parts(parts, n);
  free(parts);
}

static void encode_swapop(const char *name, const char *flags,
                          const char *condcode, const char *operands,
                          ByteBuf *out) {
  (void)name;

  int n;
  char **parts = split_by_comma(operands, &n);
  /* strip brackets from parts[2] */
  char *p2 = parts[2];

  size_t p2l = strlen(p2);
  if (p2l >= 2 && p2[0] == '[' && p2[p2l - 1] == ']') {
    p2[p2l - 1] = '\0';

    memmove(p2, p2 + 1, strlen(p2) + 1);

    char *t = strtrim(p2);

    memmove(p2, t, strlen(t) + 1);
  }

  int r0 = get_reg_num(parts[0]), r1 = get_reg_num(parts[1]),
      r2 = get_reg_num(parts[2]);
  int byteflag = (flags[0] == 'B') ? 1 : 0;
  int ccval = get_condcode_value(condcode);
  unsigned char buf[4];
  int offs[] = {28, 23, 22, 16, 12, 4, 0};
  int lens[] = {4, 5, 1, 4, 4, 4, 4};
  unsigned int vals[] = {
      (unsigned int)ccval, 0x2, (unsigned int)byteflag, (unsigned int)r2,
      (unsigned int)r0,    0x9, (unsigned int)r1};

  encode_32bit_arr(offs, lens, vals, 7, buf);
  bigendian_to_littleendian(buf, 4);
  bb_append(out, buf, 4);

  free_parts(parts, n);
  free(parts);
}

/* blockdatatransop */
static void check_blockdatatransop(const char *name, const char *operands,
                                   char *errbuf, size_t errmax) {
  (void)name;
  errbuf[0] = '\0';

  char *parts[2];

  int n = split_by_comma_maxsplit(operands, 1, parts, 2);
  if (n < 2) {
    strncpy(errbuf, "Too few operands", errmax);

    if (n > 0)
      free(parts[0]);

    return;
  }

  char *p0 = parts[0];
  int writeback = 0;

  if (p0[strlen(p0) - 1] == '!') {
    writeback = 1;
    p0[strlen(p0) - 1] = '\0';

    char *t = strtrim(p0);

    memmove(p0, t, strlen(t) + 1);
  }

  if (!is_reg(p0)) {
    strncpy(errbuf, "Expected register", errmax);

    free(parts[0]);
    free(parts[1]);

    return;
  }

  int base = get_reg_num(p0);
  if (base == 15) {
    strncpy(errbuf, "PC is not allowed here", errmax);

    free(parts[0]);
    free(parts[1]);

    return;
  }

  char *p1 = parts[1];
  if (strlen(p1) < 2) {
    strncpy(errbuf, "Invalid operand", errmax);

    free(parts[0]);
    free(parts[1]);

    return;
  }

  int sbit = 0;

  if (p1[strlen(p1) - 1] == '^') {
    sbit = 1;
    p1[strlen(p1) - 1] = '\0';

    char *t = strtrim(p1);

    memmove(p1, t, strlen(t) + 1);
  }

  if (p1[0] != '{' || p1[strlen(p1) - 1] != '}') {
    strncpy(errbuf, "Missing {} around register list", errmax);

    free(parts[0]);
    free(parts[1]);

    return;
  }

  p1[strlen(p1) - 1] = '\0';

  memmove(p1, p1 + 1, strlen(p1));

  char *t = strtrim(p1);

  memmove(p1, t, strlen(t) + 1);

  int rn;
  char **regparts = split_by_comma(p1, &rn);

  if (rn < 1) {
    strncpy(errbuf, "Invalid register list", errmax);

    free_parts(regparts, rn);
    free(regparts);
    free(parts[0]);
    free(parts[1]);

    return;
  }

  int reglist[16];
  int regcount = 0;

  int i;
  for (i = 0; i < rn; i++) {
    char *rp = strtrim(regparts[i]);
    if (strchr(rp, '-')) {
      char *dash = strchr(rp, '-');
      char left[32], right[32];
      size_t ll = dash - rp;

      strncpy(left, rp, ll < 32 ? ll : 31);
      left[ll < 32 ? ll : 31] = '\0';

      strncpy(right, dash + 1, 31);
      right[31] = '\0';

      char *lt = strtrim(left), *rt = strtrim(right);
      if (!is_reg(lt) || !is_reg(rt)) {
        strncpy(errbuf, "Expected register", errmax);

        free_parts(regparts, rn);
        free(regparts);
        free(parts[0]);
        free(parts[1]);

        return;
      }

      int start = get_reg_num(lt), end = get_reg_num(rt);
      if (start >= end) {
        strncpy(errbuf, "Registers must be specified in ascending order",
                errmax);

        free_parts(regparts, rn);
        free(regparts);
        free(parts[0]);
        free(parts[1]);

        return;
      }

      int j;
      for (j = start; j <= end && regcount < 16; j++)
        reglist[regcount++] = j;
    } else {
      if (!is_reg(rp)) {
        strncpy(errbuf, "Expected register", errmax);

        free_parts(regparts, rn);
        free(regparts);
        free(parts[0]);
        free(parts[1]);

        return;
      }

      if (regcount < 16)
        reglist[regcount++] = get_reg_num(rp);
    }
  }

  for (i = 0; i < regcount - 1; i++)
    if (reglist[i] >= reglist[i + 1]) {
      strncpy(errbuf, "Registers must be specified in ascending order", errmax);

      free_parts(regparts, rn);
      free(regparts);
      free(parts[0]);
      free(parts[1]);

      return;
    }

  int pc_in = 0;

  for (i = 0; i < regcount; i++)
    if (reglist[i] == 15)
      pc_in = 1;

  char nu[8];

  strupper(name, nu, sizeof(nu));

  if (sbit && writeback &&
      (strcmp(nu, "STM") == 0 || (strcmp(nu, "LDM") == 0 && !pc_in))) {
    strncpy(errbuf,
            "Writeback may not be used combined with user bank transfer",
            errmax);
  }

  if (writeback && strcmp(nu, "LDM") == 0) {
    for (i = 0; i < regcount; i++)
      if (reglist[i] == base) {
        strncpy(errbuf,
                "Attention: Writeback is useless here because the loaded value "
                "will overwrite it",
                errmax);

        break;
      }
  }

  free_parts(regparts, rn);
  free(regparts);
  free(parts[0]);
  free(parts[1]);
}

static void encode_blockdatatransop(const char *name, const char *flags,
                                    const char *condcode,
                                    const char *operands_in, ByteBuf *out) {
  char ops[2048];

  strncpy(ops, operands_in, sizeof(ops) - 1);
  ops[sizeof(ops) - 1] = '\0';

  int n;
  char **parts = split_by_comma(ops, &n);
  int writeback = 0;

  if (parts[0][strlen(parts[0]) - 1] == '!') {
    writeback = 1;
    parts[0][strlen(parts[0]) - 1] = '\0';

    char *t = strtrim(parts[0]);

    memmove(parts[0], t, strlen(t) + 1);
  }

  int base = get_reg_num(parts[0]);
  int sbit = 0;

  if (parts[n - 1][strlen(parts[n - 1]) - 1] == '^') {
    sbit = 1;
    parts[n - 1][strlen(parts[n - 1]) - 1] = '\0';

    char *t = strtrim(parts[n - 1]);

    memmove(parts[n - 1], t, strlen(t) + 1);
  }

  /* strip { from parts[1] and } from parts[n-1] */
  char *p1 = parts[1];
  if (p1[0] == '{') {
    memmove(p1, p1 + 1, strlen(p1));

    char *t = strtrim(p1);

    memmove(p1, t, strlen(t) + 1);
  }

  char *plast = parts[n - 1];
  size_t pl = strlen(plast);
  if (pl > 0 && plast[pl - 1] == '}') {
    plast[pl - 1] = '\0';

    char *t = strtrim(plast);

    memmove(plast, t, strlen(t) + 1);
  }

  unsigned int regfield = 0;

  int i;
  for (i = 1; i < n; i++) {
    char *rp = strtrim(parts[i]);
    if (strchr(rp, '-')) {
      char *dash = strchr(rp, '-');
      char left[32], right[32];
      size_t ll = dash - rp;

      strncpy(left, rp, ll < 32 ? ll : 31);
      left[ll < 32 ? ll : 31] = '\0';

      strncpy(right, dash + 1, 31);
      right[31] = '\0';

      int start = get_reg_num(strtrim(left)), end = get_reg_num(strtrim(right));

      int j;
      for (j = start; j <= end; j++)
        regfield |= (1U << j);
    } else {
      regfield |= (1U << get_reg_num(rp));
    }
  }

  char nu[8];

  strupper(name, nu, sizeof(nu));

  int lflag = (strcmp(nu, "LDM") == 0);
  char fu[4];

  strupper(flags, fu, sizeof(fu));

  int uflag, pflag;

  /* addrmodedict */
  if (strcmp(fu, "IB") == 0) {
    uflag = 1;
    pflag = 1;
  } else if (strcmp(fu, "IA") == 0) {
    uflag = 1;
    pflag = 0;
  } else if (strcmp(fu, "DB") == 0) {
    uflag = 0;
    pflag = 1;
  } else if (strcmp(fu, "DA") == 0) {
    uflag = 0;
    pflag = 0;
  } else if (strcmp(fu, "ED") == 0) {
    uflag = lflag;
    pflag = lflag;
  } else if (strcmp(fu, "FD") == 0) {
    uflag = lflag;
    pflag = !lflag;
  } else if (strcmp(fu, "EA") == 0) {
    uflag = !lflag;
    pflag = lflag;
  } else {
    uflag = !lflag;
    pflag = !lflag;
  } /* FA */

  int ccval = get_condcode_value(condcode);
  unsigned char buf[4];
  int offs[] = {28, 25, 24, 23, 22, 21, 20, 16, 0};
  int lens[] = {4, 3, 1, 1, 1, 1, 1, 4, 16};
  unsigned int vals[] = {(unsigned int)ccval,
                         0x4,
                         (unsigned int)pflag,
                         (unsigned int)uflag,
                         (unsigned int)sbit,
                         (unsigned int)writeback,
                         (unsigned int)lflag,
                         (unsigned int)base,
                         regfield};

  encode_32bit_arr(offs, lens, vals, 9, buf);
  bigendian_to_littleendian(buf, 4);
  bb_append(out, buf, 4);

  free_parts(parts, n);
  free(parts);
}

/* pseudoinstruction (ADR) */
static void check_pseudoinstruction(const char *name, const char *operands,
                                    long long address, char *errbuf,
                                    size_t errmax) {
  errbuf[0] = '\0';

  char u[8];

  strupper(name, u, sizeof(u));

  if (strcmp(u, "ADR") == 0) {
    char *parts[2];

    int n = split_by_comma_maxsplit(operands, 1, parts, 2);
    if (n < 2) {
      strncpy(errbuf, "Expected more operands", errmax);

      if (n > 0)
        free(parts[0]);

      return;
    }

    if (!is_reg(parts[0])) {
      strncpy(errbuf, "Invalid operand: expected register", errmax);

      free(parts[0]);
      free(parts[1]);

      return;
    }

    check_pcrelative_expression(strtrim(parts[1]), errbuf, errmax);

    if (errbuf[0]) {
      free(parts[0]);
      free(parts[1]);

      return;
    }

    long long offs = pcrelative_expression_to_int(strtrim(parts[1]), address);
    int sign, rot;
    unsigned int imv;

    if (!int_to_signrotimv(offs, &sign, &rot, &imv))
      strncpy(errbuf, "Invalid offset: cannot be encoded", errmax);

    free(parts[0]);
    free(parts[1]);
  } else if (strcmp(u, "PUSH") == 0 || strcmp(u, "POP") == 0) {
    char ops[1024];

    strncpy(ops, operands, sizeof(ops) - 1);
    ops[sizeof(ops) - 1] = '\0';

    char *op = strtrim(ops);
    size_t ol = strlen(op);

    if (ol < 3 || op[0] != '{' || op[ol - 1] != '}')
      strncpy(errbuf, "Expected register list in {}", errmax);
  } else if (strcmp(u, "NOP") == 0) {
    char ops[1024];

    strncpy(ops, operands, sizeof(ops) - 1);
    ops[sizeof(ops) - 1] = '\0';

    if (strtrim(ops)[0])
      strncpy(errbuf, "NOP takes no operands", errmax);
  } else
    strncpy(errbuf, "Unknown pseudoinstruction (bug)", errmax);
}

static void get_replacement(const char *name, const char *operands,
                            long long address, char *newop, size_t newopmax,
                            char *newflags, size_t newflagsmax,
                            char *newoperands, size_t newopmax2) {
  char u[8];

  strupper(name, u, sizeof(u));

  newflags[0] = '\0';

  if (strcmp(u, "ADR") == 0) {
    char *parts[2];

    split_by_comma_maxsplit(operands, 1, parts, 2);

    char *reg = strtrim(parts[0]);
    char *expr = strtrim(parts[1]);
    long long offs = pcrelative_expression_to_int(expr, address);
    int sign = 1, rot = 0;
    unsigned int imv = 0;

    int_to_signrotimv(offs, &sign, &rot, &imv);

    strncpy(newop, sign == 1 ? "ADD" : "SUB", newopmax - 1);
    newop[newopmax - 1] = '\0';

    long long absoffs = offs < 0 ? -offs : offs;

    snprintf(newoperands, newopmax2, "%s, PC, #%lld", reg, absoffs);

    free(parts[0]);
    free(parts[1]);
  } else if (strcmp(u, "PUSH") == 0 || strcmp(u, "POP") == 0) {
    int is_push = (u[0] == 'P' && u[1] == 'U');

    /* single plain register: use the STR/LDR encoding, as the ARM ARM
     * prefers (and as GNU as emits) */
    char inner[64];

    strncpy(inner, operands, sizeof(inner) - 1);
    inner[sizeof(inner) - 1] = '\0';

    char *t = strtrim(inner);
    size_t tl = strlen(t);
    int single = 0;

    if (tl >= 3 && t[0] == '{' && t[tl - 1] == '}') {
      t[tl - 1] = '\0';
      t = strtrim(t + 1);

      single = !strchr(t, ',') && !strchr(t, '-') && is_reg(t);
    }

    if (single) {
      strncpy(newop, is_push ? "STR" : "LDR", newopmax - 1);
      newop[newopmax - 1] = '\0';

      if (is_push)
        snprintf(newoperands, newopmax2, "%s,[SP,#-4]!", t);
      else
        snprintf(newoperands, newopmax2, "%s,[SP],#4", t);
    } else {
      /* PUSH {list} -> STMFD SP!,{list} ; POP {list} -> LDMFD SP!,{list} */
      strncpy(newop, is_push ? "STM" : "LDM", newopmax - 1);
      newop[newopmax - 1] = '\0';

      strncpy(newflags, "FD", newflagsmax - 1);
      newflags[newflagsmax - 1] = '\0';

      snprintf(newoperands, newopmax2, "SP!,%s", operands);
    }
  } else if (strcmp(u, "NOP") == 0) {
    /* NOP -> MOV R0,R0 */
    strncpy(newop, "MOV", newopmax - 1);
    newop[newopmax - 1] = '\0';

    strncpy(newoperands, "R0,R0", newopmax2 - 1);
    newoperands[newopmax2 - 1] = '\0';
  }
}

/* directive check/encode */
static void check_directive(const char *name, const char *operands,
                            long long address, char *errbuf, size_t errmax) {
  errbuf[0] = '\0';

  char u[16];

  strupper(name, u, sizeof(u));

  if (strcmp(u, "EQU") == 0) {
    /* the value itself is validated in the label-building stage */
    if (!operands[0])
      strncpy(errbuf, "Missing EQU value", errmax);

    return;
  }

  if (strcmp(u, "LTORG") == 0) {
    if (operands[0]) {
      strncpy(errbuf, "LTORG takes no operands", errmax);

      return;
    }

    /* validate the literal expressions placed in this pool */
    const LitPool *pool = litpool_at(address);

    if (pool) {
      int k;
      for (k = 0; k < pool->count; k++) {
        const char *e = g_literals[pool->first + k].expr;
        long long v;

        if (!eval_abs_expression(e, &v)) {
          snprintf(errbuf, errmax, "Invalid literal expression \"=%.80s\"", e);

          return;
        }
      }
    }

    return;
  }

  if (strcmp(u, "DCD") == 0 || strcmp(u, "DCDU") == 0) {
    int n;
    char **parts = split_by_comma(operands, &n);

    if (n == 0) {
      strncpy(
          errbuf,
          "Missing operands after DCD: expected at least one immediate value",
          errmax);

      free(parts);

      return;
    }

    int i;
    for (i = 0; i < n; i++) {
      if (!is_valid_numeric_literal(parts[i])) {
        strncpy(errbuf, "Invalid numeric literal", errmax);

        free_parts(parts, n);
        free(parts);

        return;
      }

      long long v = numeric_literal_to_int(parts[i]);
      if (v > (long long)(0xFFFFFFFFUL)) {
        strncpy(errbuf,
                "Numeric literal outside of 32bit range: greater than 2^32-1",
                errmax);

        free_parts(parts, n);
        free(parts);

        return;
      }

      if (v < -2147483648LL) {
        strncpy(errbuf,
                "Numeric literal outside of 32bit range: lower than -2^31",
                errmax);

        free_parts(parts, n);
        free(parts);

        return;
      }
    }

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (strcmp(u, "DCW") == 0 || strcmp(u, "DCWU") == 0) {
    int n;
    char **parts = split_by_comma(operands, &n);

    if (n == 0) {
      strncpy(
          errbuf,
          "Missing operands after DCW: expected at least one immediate value",
          errmax);

      free(parts);

      return;
    }

    int i;
    for (i = 0; i < n; i++) {
      if (!is_valid_numeric_literal(parts[i])) {
        strncpy(errbuf, "Invalid numeric literal", errmax);

        free_parts(parts, n);
        free(parts);

        return;
      }

      long long v = numeric_literal_to_int(parts[i]);
      if (v > 65535) {
        strncpy(errbuf,
                "Numeric literal outside of 16bit range: greater than 2^16-1",
                errmax);

        free_parts(parts, n);
        free(parts);

        return;
      }

      if (v < -32768) {
        strncpy(errbuf,
                "Numeric literal outside of 16bit range: lower than -2^15",
                errmax);

        free_parts(parts, n);
        free(parts);

        return;
      }
    }

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (strcmp(u, "ALIGN") == 0) {
    int n;
    char **parts = split_by_comma(operands, &n);

    if (n == 0 || parts[0][0] == '\0') {
      free_parts(parts, n);
      free(parts);

      return;
    }

    if (n > 2) {
      strncpy(errbuf, "Only two arguments are allowed: alignment, offset",
              errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    if (!is_valid_numeric_literal(parts[0])) {
      strncpy(errbuf, "Invalid numeric literal", errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    long long alignment = numeric_literal_to_int(parts[0]);
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
      strncpy(errbuf, "Only powers of two are allowed as alignment boundaries",
              errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    if (n == 2 && !is_valid_numeric_literal(parts[1])) {
      strncpy(errbuf, "Invalid numeric literal", errmax);
    }

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (strcmp(u, "DCB") == 0) {
    int n;
    char **parts = split_by_comma(operands, &n);

    if (n == 0 || parts[0][0] == '\0') {
      strncpy(errbuf,
              "Missing operands after DCB: expected at least one numeric or "
              "string literal",
              errmax);

      free_parts(parts, n);
      free(parts);

      return;
    }

    int i;
    for (i = 0; i < n; i++) {
      char *op = strtrim(parts[i]);
      if (strlen(op) == 0) {
        strncpy(errbuf, "Unexpected comma", errmax);

        free_parts(parts, n);
        free(parts);

        return;
      }

      if (op[0] == '"') {
        if (strlen(op) < 3) {
          strncpy(errbuf, "Invalid string literal: empty", errmax);

          free_parts(parts, n);
          free(parts);

          return;
        }

        if (op[strlen(op) - 1] != '"') {
          strncpy(errbuf, "Invalid string literal: not terminated", errmax);

          free_parts(parts, n);
          free(parts);

          return;
        }
      } else if (is_valid_numeric_literal(op)) {
        long long v = numeric_literal_to_int(op);
        if (v < -128) {
          strncpy(errbuf,
                  "Numeric literal outside of 8bit range: lower than -2^7",
                  errmax);

          free_parts(parts, n);
          free(parts);

          return;
        }

        if (v > 255) {
          strncpy(errbuf,
                  "Numeric literal outside of 8bit range: greater than 2^8-1",
                  errmax);

          free_parts(parts, n);
          free(parts);

          return;
        }
      } else {
        strncpy(errbuf, "Expected numeric or string literal", errmax);

        free_parts(parts, n);
        free(parts);

        return;
      }
    }

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (strcmp(u, "INCBIN") == 0) {
    size_t sz;

    const unsigned char *d = filecontents(operands, &sz);
    if (!d)
      snprintf(errbuf, errmax, "Could not open file \"%.400s\"", operands);

    return;
  }

  strncpy(errbuf, "Invalid name (failed in check_directive) (report as bug)",
          errmax);
}

static void encode_directive(const char *name, const char *operands,
                             long long address, ByteBuf *out) {
  char u[16];

  strupper(name, u, sizeof(u));

  if (strcmp(u, "DCD") == 0 || strcmp(u, "DCDU") == 0) {
    int n;
    char **parts = split_by_comma(operands, &n);

    if (strcmp(u, "DCD") == 0) {
      int pad = (int)((4 - (address % 4)) % 4);

      bb_append_zeros(out, pad);
    }

    int i;
    for (i = 0; i < n; i++) {
      long long v = numeric_literal_to_int(parts[i]);
      unsigned char buf[4];
      int offs[] = {0};
      int lens[] = {32};
      unsigned int vals[] = {(unsigned int)(v & 0xFFFFFFFF)};

      encode_32bit_arr(offs, lens, vals, 1, buf);
      bigendian_to_littleendian(buf, 4);
      bb_append(out, buf, 4);
    }

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (strcmp(u, "DCW") == 0 || strcmp(u, "DCWU") == 0) {
    int n;
    char **parts = split_by_comma(operands, &n);

    if (strcmp(u, "DCW") == 0) {
      int pad = (int)(address % 2);

      bb_append_zeros(out, pad);
    }

    int i;
    for (i = 0; i < n; i++) {
      long long v = numeric_literal_to_int(parts[i]);
      unsigned char buf[2];
      int offs[] = {0};
      int lens[] = {16};
      unsigned int vals[] = {(unsigned int)(v & 0xFFFF)};

      encode_16bit_arr(offs, lens, vals, 1, buf);
      bigendian_to_littleendian_16bit(buf, 2);
      bb_append(out, buf, 2);
    }

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (strcmp(u, "ALIGN") == 0) {
    int n;
    char **parts = split_by_comma(operands, &n);
    long long alignment = 4, offset = 0;

    if (n >= 1 && parts[0][0])
      alignment = numeric_literal_to_int(parts[0]);
    if (n >= 2)
      offset = numeric_literal_to_int(parts[1]);

    int padsize =
        (int)((alignment - ((address + alignment - offset) % alignment)) %
              alignment);

    bb_append_zeros(out, padsize);

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (strcmp(u, "DCB") == 0) {
    int n;
    char **parts = split_by_comma(operands, &n);

    int i;
    for (i = 0; i < n; i++) {
      char *op = strtrim(parts[i]);
      if (op[0] == '"') {
        op++;
        op[strlen(op) - 1] = '\0';

        while (*op) {
          bb_push(out, (unsigned char)*op);

          op++;
        }
      } else {
        unsigned char b = (unsigned char)(numeric_literal_to_int(op) & 0xFF);
        bb_push(out, b);
      }
    }

    free_parts(parts, n);
    free(parts);

    return;
  }

  if (strcmp(u, "INCBIN") == 0) {
    size_t sz;

    const unsigned char *d = filecontents(operands, &sz);
    if (d)
      bb_append(out, d, sz);

    return;
  }

  if (strcmp(u, "LTORG") == 0) {
    const LitPool *pool = litpool_at(address);

    if (!pool)
      return;

    bb_append_zeros(out, pool->pad);

    int k;
    for (k = 0; k < pool->count; k++) {
      long long v = 0;

      eval_abs_expression(g_literals[pool->first + k].expr, &v);

      unsigned char b[4];

      b[0] = v & 0xFF;
      b[1] = (v >> 8) & 0xFF;
      b[2] = (v >> 16) & 0xFF;
      b[3] = (v >> 24) & 0xFF;

      bb_append(out, b, 4);
    }

    return;
  }
}

/* The long, per-line strings are heap-allocated: with fixed 4 KB buffers a
 * Sourceline weighed ~17 KB, which OOMed the calculator on large sources. */
typedef struct {
  char *line;       /* original source text */
  char *notcomment; /* line with the ;-comment stripped */
  char *operation;  /* opcode + operands (label removed) */
  char *operands;   /* operand string */
  char label[256];
  char opname[64];
  char flags[8];
  char condcode[4];
  long long address;
  ByteBuf hexcode;
  int length;
  char errmsg[512];
  char *srcfile; /* strdup'd path of the file this line came from */
  int srcline;   /* 1-based line number within srcfile */
} Sourceline;

/* Replace *dst with a copy of src (src may alias *dst or point into it). */
static void set_str(char **dst, const char *src) {
  char *d = strdup_s(src ? src : "");

  free(*dst);
  *dst = d;
}

static void sl_init(Sourceline *sl, const char *line) {
  sl->line = strdup_s(line);
  sl->notcomment = strdup_s("");
  sl->operation = strdup_s("");
  sl->operands = strdup_s("");
  sl->label[0] = '\0';
  sl->opname[0] = '\0';
  sl->flags[0] = '\0';
  sl->condcode[0] = '\0';
  sl->address = -1;

  bb_init(&sl->hexcode);

  sl->length = -1;
  sl->errmsg[0] = '\0';
  sl->srcfile = NULL;
  sl->srcline = 0;
}

static void sl_free(Sourceline *sl) {
  free(sl->line);
  free(sl->notcomment);
  free(sl->operation);
  free(sl->operands);
  free(sl->srcfile);

  bb_free(&sl->hexcode);
}

static int sl_parse_comments(Sourceline *sl) {
  /* find the first ';' that is not inside a "..." string or a 'c' literal */
  int sci = -1;
  int inquote = 0;

  int i;
  for (i = 0; sl->line[i]; i++) {
    if (sl->line[i] == '"')
      inquote = !inquote;

    if (!inquote && sl->line[i] == '\'' && sl->line[i + 1] &&
        sl->line[i + 2] == '\'') {
      i += 2;

      continue;
    }

    if (sl->line[i] == ';' && !inquote) {
      sci = i;

      break;
    }
  }

  set_str(&sl->notcomment, sl->line);

  if (sci >= 0)
    sl->notcomment[sci] = '\0';

  /* rstrip */
  size_t l = strlen(sl->notcomment);
  while (l > 0 && isspace((unsigned char)sl->notcomment[l - 1])) {
    sl->notcomment[--l] = '\0';
  }

  return 0;
}

static int sl_parse_labelpart(Sourceline *sl) {
  if (!sl->notcomment[0] || isspace((unsigned char)sl->notcomment[0])) {
    char tmp[4096];

    strncpy(tmp, sl->notcomment, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    set_str(&sl->operation, strtrim(tmp));

    return 0;
  }

  char left[256], right[4096];

  int n = split_whitespace(sl->notcomment, left, sizeof(left), right,
                           sizeof(right));
  if (n == 0)
    return 0;

  strncpy(sl->label, left, sizeof(sl->label) - 1);
  sl->label[sizeof(sl->label) - 1] = '\0';

  if (n == 2)
    set_str(&sl->operation, strtrim(right));

  if (!is_valid_label(sl->label)) {
    snprintf(sl->errmsg, sizeof(sl->errmsg),
             "This label contains illegal characters (label:%s)", sl->label);

    return -1;
  }

  if (is_private_label(sl->label)) {
    snprintf(sl->errmsg, sizeof(sl->errmsg),
             "This label name is reserved (label:%s)", sl->label);

    return -1;
  }

  return 0;
}

static void sl_parse_s_suffix(Sourceline *sl) {
  const char *soplist[] = {"ADC",   "ADD",   "RSB",   "RSC", "SBC",
                           "SUB",   "AND",   "BIC",   "EOR", "ORR",
                           "MOV",   "MVN",   "MUL",   "MLA", "UMULL",
                           "SMULL", "UMLAL", "SMLAL", NULL};

  int i;
  for (i = 0; soplist[i]; i++) {
    if (startswith(sl->opname, soplist[i])) {
      size_t oplen = strlen(soplist[i]);

      /* check if ends with condcode + 'S' */
      size_t onlen = strlen(sl->opname);
      if (onlen == oplen + 3 && sl->opname[onlen - 1] == 'S') {
        char cc[3];
        strncpy(cc, sl->opname + oplen, 2);
        cc[2] = '\0';

        if (is_condcode(cc)) {
          sl->opname[onlen - 1] = '\0';
          sl->flags[0] = 'S';
          sl->flags[1] = '\0';
          break;
        }
      }

      if (onlen == oplen + 1 && sl->opname[onlen - 1] == 'S') {
        sl->opname[onlen - 1] = '\0';
        sl->flags[0] = 'S';
        sl->flags[1] = '\0';

        break;
      }
    }
  }
}

static void sl_parse_tbhs_suffixes(Sourceline *sl) {
  const char *tbhsoplist[] = {"LDR", "STR", "SWP", NULL};

  int i;
  for (i = 0; tbhsoplist[i]; i++) {
    if (!startswith(sl->opname, tbhsoplist[i]))
      continue;

    size_t oplen = strlen(tbhsoplist[i]);
    const char *tail = sl->opname + oplen;

    if (!tail[0])
      return;

    /* NAME = prefix + suffix (e.g. LDRSB) */
    if (is_tbhs_suffix(tail)) {
      strncpy(sl->flags, tail, sizeof(sl->flags) - 1);
      sl->flags[sizeof(sl->flags) - 1] = '\0';
      sl->opname[oplen] = '\0';

      return;
    }

    /* NAME = prefix + condcode + suffix (e.g. LDRVSB = LDR VS B).
     * Keep the condcode in the opname; sl_parse_condition_code strips
     * it afterwards. */
    if (strlen(tail) >= 2) {
      char cc[3];

      cc[0] = tail[0];
      cc[1] = tail[1];
      cc[2] = '\0';

      if (is_condcode(cc)) {
        const char *rest = tail + 2;

        if (rest[0] && is_tbhs_suffix(rest)) {
          strncpy(sl->flags, rest, sizeof(sl->flags) - 1);
          sl->flags[sizeof(sl->flags) - 1] = '\0';
          sl->opname[oplen + 2] = '\0';
        }
      }
    }

    return;
  }
}

static void sl_parse_addrmode_suffixes(Sourceline *sl) {
  const char *addrsuffoplist[] = {"LDM", "STM", NULL};
  const char *addrmodelist[] = {"FD", "ED", "FA", "EA", "IA",
                                "IB", "DA", "DB", NULL};

  int i, j;
  for (i = 0; addrsuffoplist[i]; i++) {
    if (startswith(sl->opname, addrsuffoplist[i])) {
      size_t onlen = strlen(sl->opname), oplen = strlen(addrsuffoplist[i]);
      /* check for condcode + addrmode suffix */
      if (onlen == oplen + 4) {
        char cc[3];
        strncpy(cc, sl->opname + oplen, 2);
        cc[2] = '\0';

        if (is_condcode(cc)) {
          char am[3];
          strncpy(am, sl->opname + oplen + 2, 2);
          am[2] = '\0';

          for (j = 0; addrmodelist[j]; j++) {
            if (strcmp(am, addrmodelist[j]) == 0) {
              strncpy(sl->flags, am, sizeof(sl->flags) - 1);
              sl->opname[oplen + 2] = '\0'; /* keep condcode temporarily */
              sl->opname[onlen - 2] = '\0';
              break;
            }
          }
        }
      } else if (onlen >= oplen + 2) {
        char am[3];

        strncpy(am, sl->opname + onlen - 2, 2);
        am[2] = '\0';

        char amu[4];

        strupper(am, amu, sizeof(amu));

        for (j = 0; addrmodelist[j]; j++) {
          if (strcmp(amu, addrmodelist[j]) == 0) {
            strncpy(sl->flags, amu, sizeof(sl->flags) - 1);
            sl->opname[onlen - 2] = '\0';

            break;
          }
        }
      }

      break;
    }
  }
}

static void sl_parse_condition_code(Sourceline *sl) {
  char fullname[128];

  snprintf(fullname, sizeof(fullname), "%s%s", sl->opname, sl->flags);

  if (is_opname(fullname))
    return;

  size_t onlen = strlen(sl->opname);
  if (onlen >= 2 && is_condcode(sl->opname + onlen - 2)) {
    strncpy(sl->condcode, sl->opname + onlen - 2, sizeof(sl->condcode) - 1);
    sl->opname[onlen - 2] = '\0';

    /* The suffix parsers ran before the condition code was stripped; with
     * UAL-style ordering (LDRBEQ, LDMFDEQ, SUBSEQ, ...) the size/mode
     * suffixes sit before the condition code, so strip them again now. */
    sl_parse_s_suffix(sl);
    sl_parse_tbhs_suffixes(sl);
    sl_parse_addrmode_suffixes(sl);
  }
}

static int sl_check_operation(Sourceline *sl) {
  char fullname[128];

  snprintf(fullname, sizeof(fullname), "%s%s", sl->opname, sl->flags);

  if (!is_opname(fullname)) {
    snprintf(sl->errmsg, sizeof(sl->errmsg),
             "This instruction (name:%s, flags:%s) is unknown", sl->opname,
             sl->flags);

    return -1;
  }

  if (is_conditionable(fullname)) {
    if (sl->condcode[0] && !is_condcode(sl->condcode)) {
      snprintf(sl->errmsg, sizeof(sl->errmsg),
               "Condition code is unknown (condition code: %s)", sl->condcode);

      return -1;
    }
  } else if (sl->condcode[0]) {
    snprintf(sl->errmsg, sizeof(sl->errmsg),
             "Condition codes are not allowed for this instruction (name:%s, "
             "flags:%s, cond:%s)",
             sl->opname, sl->flags, sl->condcode);

    return -1;
  }

  return 0;
}

static int sl_parse_namepart(Sourceline *sl) {
  char left[64], right[4096];

  int n =
      split_whitespace(sl->operation, left, sizeof(left), right, sizeof(right));
  if (n == 0)
    return 0;

  strupper(left, sl->opname, sizeof(sl->opname));

  if (n == 2)
    set_str(&sl->operands, strtrim(right));

  char opname_orig[64];

  strncpy(opname_orig, sl->opname, sizeof(opname_orig) - 1);
  opname_orig[sizeof(opname_orig) - 1] = '\0';

  sl_parse_s_suffix(sl);
  sl_parse_tbhs_suffixes(sl);
  sl_parse_addrmode_suffixes(sl);
  sl_parse_condition_code(sl);

  {
    char fn[128];

    snprintf(fn, sizeof(fn), "%s%s", sl->opname, sl->flags);

    if (sl->opname[0] && !is_opname(fn)) {
      /* Retry with the condition code stripped first: in names like
       * LDRHS or LDRGT the condition code's last letter looks like a
       * size suffix and misleads the suffix parsers. */
      strncpy(sl->opname, opname_orig, sizeof(sl->opname) - 1);
      sl->opname[sizeof(sl->opname) - 1] = '\0';
      sl->flags[0] = '\0';
      sl->condcode[0] = '\0';

      size_t onlen = strlen(sl->opname);

      if (onlen > 2 && is_condcode(sl->opname + onlen - 2)) {
        strncpy(sl->condcode, sl->opname + onlen - 2,
                sizeof(sl->condcode) - 1);
        sl->condcode[sizeof(sl->condcode) - 1] = '\0';
        sl->opname[onlen - 2] = '\0';

        sl_parse_s_suffix(sl);
        sl_parse_tbhs_suffixes(sl);
        sl_parse_addrmode_suffixes(sl);
      }
    }
  }

  if (!sl->condcode[0]) {
    char fn[128];

    snprintf(fn, sizeof(fn), "%s%s", sl->opname, sl->flags);

    if (is_conditionable(fn))
      strncpy(sl->condcode, "AL", sizeof(sl->condcode) - 1);
  }

  return sl_check_operation(sl);
}

static int sl_is_include(Sourceline *sl) {
  return strcmp(sl->opname, "INCLUDE") == 0 || strcmp(sl->opname, "GET") == 0;
}

static int sl_is_incbin(Sourceline *sl) {
  return strcmp(sl->opname, "INCBIN") == 0;
}

/* Handle LDR rX,=expr at size-calculation time. Returns 0 if the line is
 * not of that form, 1 if it was rewritten, -1 on error (errmsg set). */
static int sl_handle_ldr_literal(Sourceline *sl) {
  if (strcmp(sl->opname, "LDR") != 0 || sl->flags[0])
    return 0;

  if (!strchr(sl->operands, '='))
    return 0;

  char *parts[2];

  int n = split_by_comma_maxsplit(sl->operands, 1, parts, 2);
  if (n < 2) {
    if (n == 1)
      free(parts[0]);

    return 0;
  }

  char *rd = strtrim(parts[0]);
  char *rhs = strtrim(parts[1]);

  if (rhs[0] != '=') {
    free(parts[0]);
    free(parts[1]);

    return 0;
  }

  char expr[128];

  strncpy(expr, rhs + 1, sizeof(expr) - 1);
  expr[sizeof(expr) - 1] = '\0';

  char *e = strtrim(expr);

  int ret = -1;

  if (!is_reg(rd)) {
    strncpy(sl->errmsg, "Invalid operand: expected register",
            sizeof(sl->errmsg) - 1);
    sl->errmsg[sizeof(sl->errmsg) - 1] = '\0';
  } else if (!e[0]) {
    strncpy(sl->errmsg, "Missing literal expression after '='",
            sizeof(sl->errmsg) - 1);
    sl->errmsg[sizeof(sl->errmsg) - 1] = '\0';
  } else if (strlen(rhs + 1) >= sizeof(expr) - 1) {
    strncpy(sl->errmsg, "Literal expression too long",
            sizeof(sl->errmsg) - 1);
    sl->errmsg[sizeof(sl->errmsg) - 1] = '\0';
  } else {
    char newops[192];
    int rewritten = 0;

    if (is_valid_numeric_literal(e)) {
      /* value known now: use MOV/MVN when the constant is encodable */
      unsigned int v =
          (unsigned int)(numeric_literal_to_int(e) & 0xFFFFFFFFu);

      int i;
      for (i = 0; i < 32 && !rewritten; i += 2) {
        if (rotateleft32(v, i) < 256) {
          strncpy(sl->opname, "MOV", sizeof(sl->opname) - 1);
          snprintf(newops, sizeof(newops), "%s,#%u", rd, v);

          rewritten = 1;
        }
      }

      for (i = 0; i < 32 && !rewritten; i += 2) {
        if (rotateleft32(~v, i) < 256) {
          strncpy(sl->opname, "MVN", sizeof(sl->opname) - 1);
          snprintf(newops, sizeof(newops), "%s,#%u", rd, ~v);

          rewritten = 1;
        }
      }
    }

    if (!rewritten) {
      /* literal-pool load: reuse a pending literal with the same text */
      int idx = -1, k;

      for (k = g_pending_start; k < g_num_literals; k++) {
        if (strcmp(g_literals[k].expr, e) == 0) {
          idx = k;

          break;
        }
      }

      if (idx < 0 && g_num_literals >= MAX_LITERALS) {
        strncpy(sl->errmsg, "Too many literal-pool entries",
                sizeof(sl->errmsg) - 1);
        sl->errmsg[sizeof(sl->errmsg) - 1] = '\0';

        free(parts[0]);
        free(parts[1]);

        return -1;
      }

      if (idx < 0) {
        idx = g_num_literals++;

        strncpy(g_literals[idx].expr, e, sizeof(g_literals[idx].expr) - 1);
        g_literals[idx].expr[sizeof(g_literals[idx].expr) - 1] = '\0';
        g_literals[idx].addr = -1;
      }

      snprintf(newops, sizeof(newops), "%s,__lit%d", rd, idx);
    }

    set_str(&sl->operands, newops);

    ret = 1;
  }

  free(parts[0]);
  free(parts[1]);

  return ret;
}

static int sl_set_length_and_address(Sourceline *sl, long long address) {
  sl->address = address;

  if (sl_handle_ldr_literal(sl) < 0)
    return -1;

  char fullname[128];

  snprintf(fullname, sizeof(fullname), "%s%s", sl->opname, sl->flags);

  if (!fullname[0]) {
    sl->length = 0;

    return 0;
  }

  sl->length = get_size(fullname, sl->operands, address);
  if (sl->length == -1) {
    strncpy(sl->errmsg, "Could not calculate instruction size",
            sizeof(sl->errmsg) - 1);
    sl->errmsg[sizeof(sl->errmsg) - 1] = '\0';

    return -1;
  }

  return 0;
}

static int sl_replace_pseudoinstructions(Sourceline *sl) {
  if (!is_pseudoinstruction(sl->opname))
    return 0;

  char errbuf[512];

  errbuf[0] = '\0';

  check_pseudoinstruction(sl->opname, sl->operands, sl->address, errbuf,
                          sizeof(errbuf));

  if (errbuf[0]) {
    strncpy(sl->errmsg, errbuf, sizeof(sl->errmsg) - 1);
    sl->errmsg[sizeof(sl->errmsg) - 1] = '\0';

    return -1;
  }

  char newop[64], newflags[8], newoperands[4096];

  get_replacement(sl->opname, sl->operands, sl->address, newop, sizeof(newop),
                  newflags, sizeof(newflags), newoperands,
                  sizeof(newoperands));

  strncpy(sl->opname, newop, sizeof(sl->opname) - 1);
  strncpy(sl->flags, newflags, sizeof(sl->flags) - 1);
  set_str(&sl->operands, newoperands);

  return 0;
}

static int sl_check_syntax(Sourceline *sl) {
  char fullname[128];

  snprintf(fullname, sizeof(fullname), "%s%s", sl->opname, sl->flags);

  char errbuf[512];

  errbuf[0] = '\0';

  if (is_directive(fullname))
    check_directive(sl->opname, sl->operands, sl->address, errbuf,
                    sizeof(errbuf));
  else if (is_dataprocop(fullname))
    check_dataprocop(sl->opname, sl->operands, errbuf, sizeof(errbuf));
  else if (is_branchop(fullname))
    check_branchop(sl->opname, sl->operands, sl->address, errbuf,
                   sizeof(errbuf));
  else if (is_psrtransop(fullname))
    check_psrtransop(sl->opname, sl->operands, errbuf, sizeof(errbuf));
  else if (is_swiop(fullname))
    check_swiop(sl->opname, sl->operands, errbuf, sizeof(errbuf));
  else if (is_mulop(fullname))
    check_mulop(sl->opname, sl->operands, errbuf, sizeof(errbuf));
  else if (is_longmulop(fullname))
    check_longmulop(sl->opname, sl->operands, errbuf, sizeof(errbuf));
  else if (is_coprocregtransop(fullname))
    check_coprocregtransop(sl->opname, sl->operands, errbuf, sizeof(errbuf));
  else if (is_singledatatransop(fullname))
    check_singledatatransop(sl->flags, sl->operands, sl->address, errbuf,
                            sizeof(errbuf));
  else if (is_halfsigneddatatransop(fullname))
    check_halfsigneddatatransop(sl->operands, sl->address, errbuf,
                                sizeof(errbuf));
  else if (is_swapop(fullname))
    check_swapop(sl->operands, errbuf, sizeof(errbuf));
  else if (is_blockdatatransop(fullname))
    check_blockdatatransop(sl->opname, sl->operands, errbuf, sizeof(errbuf));
  else if (is_miscarithmeticop(fullname))
    check_miscarithmeticop(sl->opname, sl->operands, errbuf, sizeof(errbuf));
  else
    strncpy(errbuf,
            "Unknown or not implemented instruction (failed in _check_syntax)",
            sizeof(errbuf) - 1);
  if (errbuf[0]) {
    strncpy(sl->errmsg, errbuf, sizeof(sl->errmsg) - 1);
    sl->errmsg[sizeof(sl->errmsg) - 1] = '\0';

    return -1;
  }

  return 0;
}

static void sl_encode_line(Sourceline *sl, ByteBuf *out) {
  char fullname[128];

  snprintf(fullname, sizeof(fullname), "%s%s", sl->opname, sl->flags);

  if (is_directive(fullname))
    encode_directive(sl->opname, sl->operands, sl->address, out);
  else if (is_dataprocop(fullname))
    encode_dataprocop(sl->opname, sl->flags, sl->condcode, sl->operands, out);
  else if (is_branchop(fullname))
    encode_branchop(sl->opname, sl->condcode, sl->operands, sl->address, out);
  else if (is_psrtransop(fullname))
    encode_psrtransop(sl->opname, sl->condcode, sl->operands, out);
  else if (is_swiop(fullname))
    encode_swiop(sl->opname, sl->condcode, sl->operands, out);
  else if (is_mulop(fullname))
    encode_mulop(sl->opname, sl->flags, sl->condcode, sl->operands, out);
  else if (is_longmulop(fullname))
    encode_longmulop(sl->opname, sl->flags, sl->condcode, sl->operands, out);
  else if (is_coprocregtransop(fullname))
    encode_coprocregtransop(sl->opname, sl->condcode, sl->operands, out);
  else if (is_singledatatransop(fullname))
    encode_singledatatransop(sl->opname, sl->flags, sl->condcode, sl->operands,
                             sl->address, out);
  else if (is_halfsigneddatatransop(fullname))
    encode_halfsigneddatatransop(sl->opname, sl->flags, sl->condcode,
                                 sl->operands, sl->address, out);
  else if (is_swapop(fullname))
    encode_swapop(sl->opname, sl->flags, sl->condcode, sl->operands, out);
  else if (is_blockdatatransop(fullname))
    encode_blockdatatransop(sl->opname, sl->flags, sl->condcode, sl->operands,
                            out);
  else if (is_miscarithmeticop(fullname))
    encode_miscarithmeticop(sl->opname, sl->condcode, sl->operands, out);
}

static int sl_assemble(Sourceline *sl) {
  if (!sl->opname[0])
    return 0;

  if (sl_check_syntax(sl) != 0)
    return -1;

  sl_encode_line(sl, &sl->hexcode);

  if ((int)sl->hexcode.len != sl->length) {
    snprintf(sl->errmsg, sizeof(sl->errmsg),
             "Precalculated length (%d bytes) and real length (%zu bytes) are "
             "not the same",
             sl->length, sl->hexcode.len);

    return -1;
  }

  return 0;
}

typedef struct {
  Sourceline **lines;
  int n;
  int cap;
} CodeList;

static void cl_init(CodeList *cl) {
  cl->lines = NULL;
  cl->n = 0;
  cl->cap = 0;
}

static void cl_push(CodeList *cl, Sourceline *sl) {
  if (cl->n >= cl->cap) {
    cl->cap = cl->cap ? cl->cap * 2 : 64;
    cl->lines = realloc(cl->lines, cl->cap * sizeof(Sourceline *));
  }

  cl->lines[cl->n++] = sl;
}

static void cl_free(CodeList *cl) {
  int i;
  for (i = 0; i < cl->n; i++) {
    sl_free(cl->lines[i]);
    free(cl->lines[i]);
  }

  free(cl->lines);

  cl->lines = NULL;
  cl->n = cl->cap = 0;
}

static void cl_append(CodeList *dst, CodeList *src) {
  int i;
  for (i = 0; i < src->n; i++)
    cl_push(dst, src->lines[i]);

  free(src->lines);

  src->lines = NULL;
  src->n = src->cap = 0;
}

static void printerror(const char *filename, int linenum, const char *line,
                       const char *msg) {
  add_error("Error in file \"%s\" on line %d:%s\n\t%s", filename, linenum, line,
            msg);
}

static int read_file_and_stage1_parse(const char *infile,
                                      const char **filestack, int stackdepth,
                                      CodeList *out) {
  int numerrs = 0;
  char prevsrcpath[MAX_PATH];
  strncpy(prevsrcpath, get_sourcepath(), MAX_PATH - 1);

  prevsrcpath[MAX_PATH - 1] = '\0';

  change_sourcepath(infile);

  char fullpath[MAX_PATH];

  strncpy(fullpath, get_sourcepath(), MAX_PATH - 1);
  fullpath[MAX_PATH - 1] = '\0';

  /* check for circular includes */
  int i;
  for (i = 0; i < stackdepth; i++) {
    if (strcmp(filestack[i], fullpath) == 0) {
      printerror(fullpath, -1, "",
                 "file includes itself (possibly indirectly)");

      set_sourcepath(prevsrcpath);

      return 1;
    }
  }

  int nlines;

  char **textlines = get_sourcecode(&nlines);
  if (!textlines) {
    printerror(fullpath, -1, "", "could not read source file");

    set_sourcepath(prevsrcpath);

    return 1;
  }

  /* Create Sourceline objects */
  Sourceline **code = malloc(nlines * sizeof(Sourceline *));

  for (i = 0; i < nlines; i++) {
    code[i] = malloc(sizeof(Sourceline));

    sl_init(code[i], textlines[i]);

    code[i]->srcfile = strdup_s(fullpath);
    code[i]->srcline = i + 1;

    free(textlines[i]);
  }

  free(textlines);

  /* included code: array of (index, CodeList) */
  typedef struct {
    int idx;
    CodeList cl;
  } IncEntry;

  IncEntry *included = NULL;
  int num_included = 0;

  /* Stage 1 parse */
  for (i = 0; i < nlines; i++) {
    Sourceline *c = code[i];

    if (sl_parse_comments(c) != 0) {
      if (c->errmsg[0])
        printerror(fullpath, i + 1, c->line, c->errmsg);
      else
        printerror(fullpath, i + 1, c->line, "unknown error in parse_comments");

      numerrs++;

      continue;
    }

    if (sl_parse_labelpart(c) != 0) {
      if (c->errmsg[0])
        printerror(fullpath, i + 1, c->line, c->errmsg);
      else
        printerror(fullpath, i + 1, c->line, "unknown error in parse_labelpart");

      numerrs++;

      continue;
    }

    if (sl_parse_namepart(c) != 0) {
      if (c->errmsg[0])
        printerror(fullpath, i + 1, c->line, c->errmsg);
      else
        printerror(fullpath, i + 1, c->line, "unknown error in parse_namepart");

      numerrs++;

      continue;
    }

    if (sl_is_incbin(c)) {
      char absp[MAX_PATH];
      long sz = add_file(c->operands, absp, MAX_PATH);

      set_str(&c->operands, absp);

      if (sz < 0) {
        printerror(fullpath, i + 1, c->line, "error in add_file");

        numerrs++;

        continue;
      }
    }

    if (sl_is_include(c)) {
      const char **newstack = malloc((stackdepth + 1) * sizeof(char *));

      for (int j = 0; j < stackdepth; j++)
        newstack[j] = filestack[j];

      newstack[stackdepth] = fullpath;

      CodeList incl;

      cl_init(&incl);

      int errs = read_file_and_stage1_parse(c->operands, newstack,
                                            stackdepth + 1, &incl);

      free(newstack);

      numerrs += errs;

      included = realloc(included, (num_included + 1) * sizeof(IncEntry));
      included[num_included].idx = i;
      included[num_included].cl = incl;

      num_included++;
    }
  }

  set_sourcepath(prevsrcpath);

  if (numerrs != 0) {
    for (i = 0; i < nlines; i++) {
      sl_free(code[i]);
      free(code[i]);
    }

    free(code);

    for (i = 0; i < num_included; i++)
      cl_free(&included[i].cl);

    free(included);

    return numerrs;
  }

  /* Concatenate code with included snippets */
  int currpos = 0;

  for (i = 0; i < num_included; i++) {
    int idx = included[i].idx;

    int j;
    for (j = currpos; j < idx; j++)
      cl_push(out, code[j]);

    cl_append(out, &included[i].cl);

    /* the INCLUDE line itself is replaced by the included code */
    sl_free(code[idx]);
    free(code[idx]);

    currpos = idx + 1;
  }

  int j;
  for (j = currpos; j < nlines; j++)
    cl_push(out, code[j]);

  free(code);
  free(included);

  return 0;
}

static int assembler(const char *infile, const char *outfile, int use_zehn) {
  set_sourcepath("");
  num_labels = 0;

  literals_reset();

  CodeList code;

  cl_init(&code);

  int numerrs = read_file_and_stage1_parse(infile, NULL, 0, &code);
  if (numerrs > 0) {
    printf("Stopping assembler: %d Error(s)\n", numerrs);

    cl_free(&code);

    return -1;
  }

  /* Automatic end-of-program literal pool for pending LDR= literals */
  {
    Sourceline *endpool = malloc(sizeof(Sourceline));

    sl_init(endpool, "; <automatic literal pool>");

    strncpy(endpool->opname, "LTORG", sizeof(endpool->opname) - 1);
    endpool->opname[sizeof(endpool->opname) - 1] = '\0';
    endpool->srcfile = strdup_s(infile);
    endpool->srcline = 0;

    cl_push(&code, endpool);
  }

  /* Stage 2: calculate length and address */
  long long curaddr = 0;

  int i;
  for (i = 0; i < code.n; i++) {
    Sourceline *c = code.lines[i];

    if (sl_set_length_and_address(c, curaddr) != 0) {
      if (c->errmsg[0])
        printerror(c->srcfile ? c->srcfile : infile, c->srcline, c->line, c->errmsg);
      else
        printerror(c->srcfile ? c->srcfile : infile, c->srcline, c->line,
                   "unknown error in set_length_and_address");

      numerrs++;
    } else
      curaddr += c->length;
  }

  if (numerrs != 0) {
    printf("Stopping assembler: %d Error(s)\n", numerrs);

    cl_free(&code);

    return -1;
  }

  /* Stage 3: build label dictionary (addresses and EQU constants) */
  for (i = 0; i < code.n; i++) {
    Sourceline *c = code.lines[i];
    int is_equ = (strcmp(c->opname, "EQU") == 0);

    if (is_equ && !c->label[0]) {
      printerror(c->srcfile ? c->srcfile : infile, c->srcline, c->line,
                 "EQU requires a label on the same line");

      numerrs++;

      continue;
    }

    if (c->label[0]) {
      long long existing;
      long long value = c->address;

      if (label_lookup(c->label, &existing)) {
        printerror(c->srcfile ? c->srcfile : infile, c->srcline, c->line,
                   "Label name already used");

        numerrs++;
      } else if (is_equ && !eval_abs_expression(c->operands, &value)) {
        printerror(c->srcfile ? c->srcfile : infile, c->srcline, c->line,
                   "Invalid EQU value (expected a numeric literal or a "
                   "previously defined symbol)");

        numerrs++;
      } else if (label_insert(c->label, value) != 0) {
        printerror(c->srcfile ? c->srcfile : infile, c->srcline, c->line,
                   "Too many labels (max 4096)");

        numerrs++;
      }
    }
  }

  if (numerrs != 0) {
    printf("Stopping assembler: %d Error(s)\n", numerrs);

    cl_free(&code);

    return -1;
  }

  /* Stage 4: replace pseudoinstructions */
  for (i = 0; i < code.n; i++) {
    Sourceline *c = code.lines[i];

    if (sl_replace_pseudoinstructions(c) != 0) {
      if (c->errmsg[0])
        printerror(c->srcfile ? c->srcfile : infile, c->srcline, c->line, c->errmsg);
      else
        printerror(c->srcfile ? c->srcfile : infile, c->srcline, c->line,
                   "unknown error in replace_pseudoinstructions");

      numerrs++;
    }
  }

  if (numerrs != 0) {
    printf("Stopping assembler: %d Error(s)\n", numerrs);

    cl_free(&code);

    return -1;
  }

  /* Stage 5: assemble */
  for (i = 0; i < code.n; i++) {
    Sourceline *c = code.lines[i];

    if (sl_assemble(c) != 0) {
      if (c->errmsg[0])
        printerror(c->srcfile ? c->srcfile : infile, c->srcline, c->line, c->errmsg);
      else
        printerror(c->srcfile ? c->srcfile : infile, c->srcline, c->line, "unknown error in assemble");

      numerrs++;
    }
  }

  if (numerrs != 0) {
    printf("Stopping assembler: %d Error(s)\n", numerrs);

    cl_free(&code);

    return -1;
  }

  /* Stage 6: write output */
  FILE *f = fopen(outfile, "wb");
  if (!f) {
    fprintf(stderr, "Cannot open output file '%s'\n", outfile);
    cl_free(&code);
    return -1;
  }

  uint32_t exec_size = 0;
  for (i = 0; i < code.n; i++) {
    exec_size += code.lines[i]->hexcode.len;
  }

  if (use_zehn) {
    uint32_t flags[3];
    flags[0] = make_zehn_flag(RUNS_ON_COLOR, 1);
    flags[1] = make_zehn_flag(RUNS_ON_HWW, 1);
    flags[2] = make_zehn_flag(USES_LCD_BLIT, 1);

    uint32_t flag_count = 3;
    uint32_t flags_size = flag_count * sizeof(uint32_t);

    Zehn_header zhdr;
    zhdr.signature = ZEHN_SIGNATURE;
    zhdr.version = ZEHN_VERSION;
    zhdr.file_size = sizeof(Zehn_header) + flags_size + exec_size;
    zhdr.reloc_count = 0;
    zhdr.flag_count = flag_count;
    zhdr.extra_size = 0;
    zhdr.alloc_size = zhdr.file_size;
    zhdr.entry_offset = 0;

    fwrite(&zhdr, 1, sizeof(Zehn_header), f);
    fwrite(flags, 1, flags_size, f);
  } else {
    unsigned char header[4] = {'P', 'R', 'G', 0};
    fwrite(header, 1, 4, f);
  }

  for (i = 0; i < code.n; i++) {
    Sourceline *c = code.lines[i];
    if (c->hexcode.len > 0)
      fwrite(c->hexcode.data, 1, c->hexcode.len, f);
  }

  fclose(f);
  cl_free(&code);

  return (int)exec_size;
}

/*
 * Build the output path:
 *   strip all dot-extensions from the filename, append ".tns"
 *   e.g. /documents/foo/bar.asm.tns -> /documents/foo/bar.tns
 */
static void make_outpath(const char *infile, char *outbuf, size_t outmax) {
  strncpy(outbuf, infile, outmax - 1);
  outbuf[outmax - 1] = '\0';

  char *fname_start = strrchr(outbuf, '/');
  fname_start = fname_start ? fname_start + 1 : outbuf;

  char *dot = strchr(fname_start, '.');
  if (dot)
    *dot = '\0';

  size_t cur = strlen(outbuf);
  if (cur + 4 < outmax) {
    outbuf[cur] = '.';
    outbuf[cur + 1] = 't';
    outbuf[cur + 2] = 'n';
    outbuf[cur + 3] = 's';
    outbuf[cur + 4] = '\0';
  }
}

/* Format-select + assemble + report for one already-chosen input file.
   Shared by the interactive browser loop and the argv fast path. */
static void assemble_and_report(const char *infile, const char *outfile) {
  const char *fmt_lines[] = {
      "Select output binary format:", "",
      "Zehn: Native CX II support (Recommended)",
      "PRG: Legacy Nspire compatibility mode (Default)"};

  int fmt_ans =
      gfx_window_confirm2("Output Format", fmt_lines, 4, "Zehn", "PRG");
  int use_zehn = (fmt_ans == 0) ? 1 : 0;

  int ret = assembler(infile, outfile, use_zehn);

  for (int i = 0; i < num_cached_files; i++) {
    free(file_cache[i].data);
    file_cache[i].data = NULL;
  }
  num_cached_files = 0;

  if (ret >= 0) {
    char line1[MAX_PATH + 16];
    char line2[48];

    snprintf(line1, sizeof(line1), "Output: %s", outfile);
    snprintf(line2, sizeof(line2), "Code size: %d bytes", ret);

    const char *lines[] = {"Assembly successful.", line1, line2};

    gfx_window_alert("NASM", lines, 3, "OK", 0);
  } else {
    gfx_window_scrolltext("Errors Found", (const char **)g_err_lines,
                          g_num_err_lines, "Close");
  }

  clear_errors();
}

/* Assemble a single file given directly on the command line (nStudio's
   Assemble command).  Returns after reporting.  Skips the file browser
   but keeps the ASM-extension warning and overwrite guard. */
static void assemble_from_arg(const char *infile) {
  int is_valid_ext = 0;
  char ext_tns[64], ext_bare[64];
  snprintf(ext_tns, sizeof(ext_tns), ".%s.tns", g_settings.asm_extension);
  snprintf(ext_bare, sizeof(ext_bare), ".%s", g_settings.asm_extension);
  size_t in_len = strlen(infile);
  if (in_len >= strlen(ext_tns) &&
      strcasecmp_s(infile + in_len - strlen(ext_tns), ext_tns) == 0)
    is_valid_ext = 1;
  else if (in_len >= strlen(ext_bare) &&
           strcasecmp_s(infile + in_len - strlen(ext_bare), ext_bare) == 0)
    is_valid_ext = 1;

  if (!is_valid_ext) {
    const char *warn_lines[] = {"The selected file does not match",
                                "your configured ASM extension.",
                                "",
                                "It might not be a valid source file.",
                                "",
                                "Do you want to continue?"};
    if (gfx_window_confirm2("   Warning", warn_lines, 6, "Continue",
                            "Cancel") != 0)
      return;
  }

  char outfile[MAX_PATH];
  make_outpath(infile, outfile, MAX_PATH);
  if (strcmp(outfile, infile) == 0) {
    const char *err_lines[] = {"Output path would overwrite the input:",
                               outfile, "",
                               "Rename the source (e.g. name.asm.tns)."};
    gfx_window_alert("NASM", err_lines, 4, "OK", 0);
    return;
  }

  assemble_and_report(infile, outfile);
}

int main(int argc, char *argv[]) {
  settings_load();

  /* Parse the command line.  nStudio invokes us as:
       nasm <source> [extra flags] --fb <address>
     The framebuffer address, when given, is the caller's screen buffer;
     we seed our own with it so the editor shows behind our dialogs.
     Unknown flags are ignored. */
  const char *arg_infile = NULL;
  const void *parent_fb = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--fb") == 0) {
      if (i + 1 < argc)
        parent_fb = (const void *)(uintptr_t)strtoul(argv[++i], NULL, 0);
    } else if (argv[i][0] == '-') {
      continue; /* unknown flag */
    } else if (!arg_infile) {
      arg_infile = argv[i];
    }
  }

  gfx_init();

  if (arg_infile) {
    if (parent_fb)
      memcpy(gfx_framebuffer(), parent_fb,
             (size_t)GFX_W * GFX_H * sizeof(uint16_t));
    assemble_from_arg(arg_infile);
    gfx_deinit();
    return 0;
  }

  for (;;) {
    const char *infile = NULL;
    char outfile[MAX_PATH];

    while (1) {
      infile = filebrowser_select();
      if (!infile) {
        gfx_deinit();
        return 0;
      }

    int is_valid_ext = 0;
    char ext_tns[64], ext_bare[64];

    snprintf(ext_tns, sizeof(ext_tns), ".%s.tns", g_settings.asm_extension);
    snprintf(ext_bare, sizeof(ext_bare), ".%s", g_settings.asm_extension);

    size_t in_len = strlen(infile);

    if (in_len >= strlen(ext_tns) &&
        strcasecmp_s(infile + in_len - strlen(ext_tns), ext_tns) == 0) {
      is_valid_ext = 1;
    } else if (in_len >= strlen(ext_bare) &&
               strcasecmp_s(infile + in_len - strlen(ext_bare), ext_bare) ==
                   0) {
      is_valid_ext = 1;
    }

    if (!is_valid_ext) {
      const char *warn_lines[] = {"The selected file does not match",
                                  "your configured ASM extension.",
                                  "",
                                  "It might not be a valid source file.",
                                  "",
                                  "Do you want to continue?"};

      int ans = gfx_window_confirm2("   Warning", warn_lines, 6, "Continue",
                                    "Cancel");

      if (ans != 0) {
        continue;
      }
    }

    make_outpath(infile, outfile, MAX_PATH);

      if (strcmp(outfile, infile) == 0) {
        const char *err_lines[] = {"Output path would overwrite the input:",
                                   outfile, "",
                                   "Rename the source (e.g. name.asm.tns)."};

        gfx_window_alert("NASM", err_lines, 4, "OK", 0);

        continue;
      }

      break;
    }

    assemble_and_report(infile, outfile);

    /* back to the file browser for the next assemble/fix cycle */
  }
}
