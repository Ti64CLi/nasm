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

#include "filebrowser.h"
#include "gfx.h"
#include "settings.h"

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

/* Trim leading/trailing whitespace in-place, returns pointer to start */
static char *strtrim(char *s) {
  char *end;

  while (isspace((unsigned char)*s))
    s++;

  if (*s == 0)
    return s;

  end = s + strlen(s) - 1;

  while (end > s && isspace((unsigned char)*end))
    end--;

  end[1] = '\0';

  return s;
}

/* Duplicate a string */
static char *strdup_s(const char *s) {
  size_t len = strlen(s) + 1;

  char *d = malloc(len);
  if (d)
    memcpy(d, s, len);

  return d;
}

/* Case-insensitive string compare */
static int strcasecmp_s(const char *a, const char *b) {
  while (*a && *b) {
    int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
    if (d != 0)
      return d;

    a++;
    b++;
  }

  return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/* Convert string to uppercase into dst (dst must be large enough) */
static void strupper(const char *src, char *dst, size_t maxlen) {
  size_t i;
  for (i = 0; i < maxlen - 1 && src[i]; i++)
    dst[i] = (char)toupper((unsigned char)src[i]);

  dst[i] = '\0';
}

/* startswith */
static int startswith(const char *s, const char *prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* endswith */
static int endswith(const char *s, const char *suffix) {
  size_t sl = strlen(s), pl = strlen(suffix);
  if (pl > sl)
    return 0;

  return strcmp(s + sl - pl, suffix) == 0;
}

/* Find first occurrence of any char in chars within s, return index or -1 */
static int find_first_of(const char *s, const char *chars) {
  int i;
  for (i = 0; s[i]; i++)
    if (strchr(chars, s[i]))
      return i;

  return -1;
}

/* Split string by first occurrence of sep, store left in left (maxleft), right
 * in right (maxright) */
/* Returns number of parts (1 or 2) */
static int split_once(const char *s, char sep, char *left, size_t maxleft,
                      char *right, size_t maxright) {
  const char *p = strchr(s, sep);
  if (!p) {
    strncpy(left, s, maxleft - 1);

    left[maxleft - 1] = '\0';
    right[0] = '\0';

    return 1;
  }

  size_t llen = p - s;
  if (llen >= maxleft)
    llen = maxleft - 1;

  memcpy(left, s, llen);

  left[llen] = '\0';

  strncpy(right, p + 1, maxright - 1);

  right[maxright - 1] = '\0';

  return 2;
}

typedef struct {
  unsigned char *data;
  size_t len;
  size_t cap;
} ByteBuf;

static void bb_init(ByteBuf *b) {
  b->data = NULL;
  b->len = 0;
  b->cap = 0;
}

static void bb_free(ByteBuf *b) {
  free(b->data);

  b->data = NULL;
  b->len = b->cap = 0;
}

static int bb_push(ByteBuf *b, unsigned char c) {
  if (b->len >= b->cap) {
    size_t newcap = b->cap ? b->cap * 2 : 16;
    unsigned char *nd = realloc(b->data, newcap);

    if (!nd)
      return -1;

    b->data = nd;
    b->cap = newcap;
  }

  b->data[b->len++] = c;

  return 0;
}

static int bb_append(ByteBuf *b, const unsigned char *src, size_t n) {
  size_t i;
  for (i = 0; i < n; i++)
    if (bb_push(b, src[i]) < 0)
      return -1;

  return 0;
}

static int bb_append_zeros(ByteBuf *b, size_t n) {
  size_t i;
  for (i = 0; i < n; i++)
    if (bb_push(b, 0) < 0)
      return -1;

  return 0;
}

static void bb_copy(ByteBuf *dst, const ByteBuf *src) {
  bb_init(dst);
  bb_append(dst, src->data, src->len);
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

  unsigned char *buf = malloc(sz + 1);
  if (!buf) {
    fclose(f);

    return -1;
  }

  fread(buf, 1, sz, f);
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

static unsigned int rotateleft32(unsigned int n, int r) {
  n &= 0xFFFFFFFFU;
  r %= 32;
  if (r == 0)
    return n;

  return ((n << r) | (n >> (32 - r))) & 0xFFFFFFFFU;
}

/* encode_32bit: l is array of {offset, length, value} triples, n is number of
 * triples */
static void encode_32bit_arr(const int *offsets, const int *lengths,
                             const unsigned int *values, int n,
                             unsigned char out[4]) {
  unsigned int word = 0;

  int i;
  for (i = 0; i < n; i++) {
    unsigned int mask =
        (lengths[i] == 32) ? 0xFFFFFFFFU : ((1U << lengths[i]) - 1U);

    word |= (values[i] & mask) << offsets[i];
  }

  out[0] = (word >> 24) & 0xFF;
  out[1] = (word >> 16) & 0xFF;
  out[2] = (word >> 8) & 0xFF;
  out[3] = word & 0xFF;
}

static void encode_16bit_arr(const int *offsets, const int *lengths,
                             const unsigned int *values, int n,
                             unsigned char out[2]) {
  unsigned int word = 0;

  int i;
  for (i = 0; i < n; i++) {
    unsigned int mask =
        (lengths[i] == 16) ? 0xFFFFU : ((1U << lengths[i]) - 1U);
    word |= (values[i] & mask) << offsets[i];
  }

  out[0] = (word >> 8) & 0xFF;
  out[1] = word & 0xFF;
}

static void bigendian_to_littleendian(unsigned char *b, size_t len) {
  /* swap 4-byte chunks */
  size_t i;

  if (len % 4 != 0)
    return;

  for (i = 0; i < len; i += 4) {
    unsigned char tmp;
    tmp = b[i];

    b[i] = b[i + 3];
    b[i + 3] = tmp;
    tmp = b[i + 1];
    b[i + 1] = b[i + 2];
    b[i + 2] = tmp;
  }
}

static void bigendian_to_littleendian_16bit(unsigned char *b, size_t len) {
  size_t i;

  if (len % 2 != 0)
    return;

  for (i = 0; i < len; i += 2) {
    unsigned char tmp = b[i];

    b[i] = b[i + 1];
    b[i + 1] = tmp;
  }
}

static int my_isalnum(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z');
}

static int str_isalnum(const char *s) {
  if (!s || !*s)
    return 0;

  while (*s) {
    if (!my_isalnum(*s))
      return 0;

    s++;
  }

  return 1;
}

static int str_isxdigit(const char *s) {
  if (!s || !*s)
    return 0;

  while (*s) {
    char c = *s;
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
          (c >= 'a' && c <= 'f')))
      return 0;

    s++;
  }

  return 1;
}

static int str_isoctdigit(const char *s) {
  if (!s || !*s)
    return 0;

  while (*s) {
    if (*s < '0' || *s > '7')
      return 0;

    s++;
  }

  return 1;
}

static int str_isbindigit(const char *s) {
  if (!s || !*s)
    return 0;

  while (*s) {
    if (*s != '0' && *s != '1')
      return 0;

    s++;
  }

  return 1;
}

static int str_isdigit_all(const char *s) {
  if (!s || !*s)
    return 0;

  while (*s) {
    if (!isdigit((unsigned char)*s))
      return 0;

    s++;
  }

  return 1;
}

static int is_valid_imval(const char *s);
static int is_shiftname(const char *s);
static int is_reg(const char *s);
static int is_directive(const char *s);
static int is_opname(const char *s);

static int is_shiftname(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "ASL") == 0 || strcmp(u, "LSL") == 0 ||
         strcmp(u, "LSR") == 0 || strcmp(u, "ASR") == 0 ||
         strcmp(u, "ROR") == 0;
}

static int is_coprocreg(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *list[] = {"C0",  "C1",  "C2",  "C3",  "C4",  "C5",
                        "C6",  "C7",  "C8",  "C9",  "C10", "C11",
                        "C12", "C13", "C14", "C15", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

static int is_coproc(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *list[] = {"P0",  "P1",  "P2",  "P3",  "P4",  "P5",
                        "P6",  "P7",  "P8",  "P9",  "P10", "P11",
                        "P12", "P13", "P14", "P15", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

static int is_psr(const char *s) {
  char u[16];

  strupper(s, u, sizeof(u));

  const char *list[] = {"CPSR",     "SPSR",     "CPSR_ALL", "SPSR_ALL",
                        "SPSR_FLG", "CPSR_FLG", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

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
  else if (t[1] == '\'' && t[strlen(t) - 1] == '\'' && strlen(t) == 4 &&
           (unsigned char)t[2] <= 255)
    res = 1;
  else if (startswith(t, "#0x") && strlen(t) >= 4 && str_isxdigit(t + 3))
    res = 1;
  else if (startswith(t, "#0b") && strlen(t) >= 4 && str_isbindigit(t + 3))
    res = 1;
  else if (startswith(t, "#0") && strlen(t) >= 3 && str_isoctdigit(t + 2))
    res = 1;
  else if (t[1] != '0' && str_isdigit_all(t + 1))
    res = 1;

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
  else
    val = strtoll(t + 1, NULL, 10);

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

static int get_reg_num(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *names[] = {"R0", "R1",  "R2", "R3",  "R4",  "R5",  "R6",
                         "R7", "R8",  "R9", "R10", "R11", "R12", "R13",
                         "SP", "R14", "LR", "R15", "PC",  NULL};
  const int nums[] = {0,  1,  2,  3,  4,  5,  6,  7,  8, 9,
                      10, 11, 12, 13, 13, 14, 14, 15, 15};

  int i;
  for (i = 0; names[i]; i++)
    if (strcmp(u, names[i]) == 0)
      return nums[i];

  return -1;
}

static int is_reg(const char *s) { return get_reg_num(s) != -1; }

static int get_condcode_value(const char *s) {
  char u[4];

  strupper(s, u, sizeof(u));

  const char *names[] = {"EQ", "NE", "HS", "CS", "LO", "CC", "MI", "PL", "VS",
                         "VC", "HI", "LS", "GE", "LT", "GT", "LE", "AL", NULL};
  const int vals[] = {0, 1, 2, 2, 3, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};

  int i;
  for (i = 0; names[i]; i++)
    if (strcmp(u, names[i]) == 0)
      return vals[i];

  return -1;
}

static int is_condcode(const char *s) { return get_condcode_value(s) != -1; }

static int is_preasm_directive(const char *s) {
  char u[16];

  strupper(s, u, sizeof(u));

  return strcmp(u, "GET") == 0 || strcmp(u, "INCLUDE") == 0;
}

static int is_directive(const char *s) {
  char u[16];

  strupper(s, u, sizeof(u));

  const char *list[] = {"DCD",   "DCDU", "DCW",    "DCWU",
                        "ALIGN", "DCB",  "INCBIN", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

static int is_dataproc_fullop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *list[] = {"ADC", "ADD", "RSB", "RSC", "SBC", "SUB",
                        "AND", "BIC", "EOR", "ORR", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

static int is_dataproc_testop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "CMP") == 0 || strcmp(u, "CMN") == 0 ||
         strcmp(u, "TEQ") == 0 || strcmp(u, "TST") == 0;
}

static int is_dataproc_movop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "MOV") == 0 || strcmp(u, "MVN") == 0;
}

static int get_dataprocop_num(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *names[] = {"ADC", "ADD", "RSB", "RSC", "SBC", "SUB",
                         "AND", "BIC", "EOR", "ORR", "CMP", "CMN",
                         "TEQ", "TST", "MOV", "MVN", NULL};
  const int vals[] = {5, 4, 3, 7, 6, 2, 0, 14, 1, 12, 10, 11, 9, 8, 13, 15};

  int i;
  for (i = 0; names[i]; i++)
    if (strcmp(u, names[i]) == 0)
      return vals[i];

  return -1;
}

static int is_dataprocop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *list[] = {"ADC",  "ADD",  "RSB",  "RSC",  "SBC",  "SUB",
                        "AND",  "BIC",  "EOR",  "ORR",  "CMP",  "CMN",
                        "TEQ",  "TST",  "MOV",  "MVN",  "ADCS", "ADDS",
                        "RSBS", "RSCS", "SBCS", "SUBS", "ANDS", "BICS",
                        "EORS", "ORRS", "MOVS", "MVNS", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

static int is_branchop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "BX") == 0 || strcmp(u, "B") == 0 || strcmp(u, "BL") == 0;
}

static int is_psrtransop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "MSR") == 0 || strcmp(u, "MRS") == 0;
}

static int is_mulop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "MUL") == 0 || strcmp(u, "MLA") == 0 ||
         strcmp(u, "MULS") == 0 || strcmp(u, "MLAS") == 0;
}

static int is_longmulop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *list[] = {"UMULL",  "SMULL",  "UMLAL",  "SMLAL", "UMULLS",
                        "SMULLS", "UMLALS", "SMLALS", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

static int is_swiop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "SWI") == 0 || strcmp(u, "SVC") == 0;
}

static int is_singledatatransop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *list[] = {"LDR",  "STR",   "LDRB",  "STRB", "LDRT",
                        "STRT", "LDRBT", "STRBT", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

static int is_halfsigneddatatransop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "LDRH") == 0 || strcmp(u, "LDRSH") == 0 ||
         strcmp(u, "LDRSB") == 0 || strcmp(u, "STRH") == 0;
}

static int is_swapop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "SWP") == 0 || strcmp(u, "SWPB") == 0;
}

static int is_blockdatatransop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  const char *list[] = {"LDMFD", "LDMED", "LDMFA", "LDMEA", "LDMIA", "LDMIB",
                        "LDMDA", "LDMDB", "STMFD", "STMED", "STMFA", "STMEA",
                        "STMIA", "STMIB", "STMDA", "STMDB", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

static int is_coprocregtransop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "MRC") == 0 || strcmp(u, "MCR") == 0;
}

static int is_pseudoinstructionop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "ADR") == 0;
}

static int is_miscarithmeticop(const char *s) {
  char u[8];

  strupper(s, u, sizeof(u));

  return strcmp(u, "CLZ") == 0;
}

static int is_otherkeyword(const char *s) {
  char u[16];

  strupper(s, u, sizeof(u));

  const char *list[] = {
      "LSL",      "LSR",      "ASL", "ASR",  "ROR",  "RRX",      "P0",
      "P1",       "P2",       "P3",  "P4",   "P5",   "P6",       "P7",
      "P8",       "P9",       "P10", "P11",  "P12",  "P13",      "P14",
      "P15",      "C0",       "C1",  "C2",   "C3",   "C4",       "C5",
      "C6",       "C7",       "C8",  "C9",   "C10",  "C11",      "C12",
      "C13",      "C14",      "C15", "CPSR", "SPSR", "CPSR_ALL", "SPSR_ALL",
      "SPSR_FLG", "CPSR_FLG", NULL};

  int i;
  for (i = 0; list[i]; i++)
    if (strcmp(u, list[i]) == 0)
      return 1;

  return 0;
}

static int is_opname(const char *s) {
  return is_preasm_directive(s) || is_directive(s) || is_dataprocop(s) ||
         is_branchop(s) || is_psrtransop(s) || is_mulop(s) || is_longmulop(s) ||
         is_swiop(s) || is_singledatatransop(s) ||
         is_halfsigneddatatransop(s) || is_swapop(s) ||
         is_blockdatatransop(s) || is_coprocregtransop(s) ||
         is_pseudoinstructionop(s) || is_miscarithmeticop(s);
}

static int is_conditionable(const char *s) {
  return !(is_preasm_directive(s) || is_directive(s)) && is_opname(s);
}

static int is_pseudoinstruction(const char *opname) {
  return is_pseudoinstructionop(opname);
}

static int is_valid_label(const char *s) {
  if (!s || !*s)
    return 0;

  if (!isalpha((unsigned char)s[0]))
    return 0;

  while (*s) {
    if (!my_isalnum(*s) && *s != '_')
      return 0;

    s++;
  }

  return 1;
}

static int is_private_label(const char *s) {
  return is_directive(s) || is_opname(s) || is_reg(s) || is_otherkeyword(s);
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

/* Split s by comma into parts. Returns array of strdup'd strings, *n = count.
 * Caller frees. */
static char **split_by_comma(const char *s, int *n) {
  char **parts = NULL;
  *n = 0;

  if (!s || !*s)
    return NULL;

  const char *start = s;

  while (1) {
    const char *comma = strchr(start, ',');
    char buf[4096];

    size_t len = comma ? (size_t)(comma - start) : strlen(start);
    if (len >= sizeof(buf))
      len = sizeof(buf) - 1;

    memcpy(buf, start, len);
    buf[len] = '\0';

    char *trimmed = strtrim(buf);

    parts = realloc(parts, (*n + 1) * sizeof(char *));
    parts[(*n)++] = strdup_s(trimmed);

    if (!comma)
      break;

    start = comma + 1;
  }

  return parts;
}

/* Split s by comma, max_n parts. Returns number of parts. Fills parts array
 * with strdup'd strings. */
static int split_by_comma_n(const char *s, char **parts, int max_n) {
  int n;
  char **tmp = split_by_comma(s, &n);
  int i;

  if (n > max_n)
    n = max_n;

  for (i = 0; i < n; i++)
    parts[i] = tmp[i];

  /* free extras */
  int total;
  char **tmp2 = split_by_comma(s, &total);

  for (i = n; i < total; i++)
    free(tmp2[i]);

  free(tmp2);
  free(tmp);

  return n;
}

static void free_parts(char **parts, int n) {
  int i;
  for (i = 0; i < n; i++)
    free(parts[i]);
}

/* Split by comma with limit (like Python split(',', maxsplit)) */
static int split_by_comma_maxsplit(const char *s, int maxsplit, char **parts,
                                   int max_parts) {
  int n = 0;
  const char *start = s;

  while (n < max_parts) {
    const char *comma = (n < maxsplit) ? strchr(start, ',') : NULL;
    char buf[4096];

    size_t len = comma ? (size_t)(comma - start) : strlen(start);
    if (len >= sizeof(buf))
      len = sizeof(buf) - 1;

    memcpy(buf, start, len);
    buf[len] = '\0';

    char *trimmed = strdup_s(strtrim(buf));
    parts[n++] = trimmed;

    if (!comma)
      break;

    start = comma + 1;
  }

  return n;
}

/* Split by whitespace (any amount), at most 2 parts */
static int split_whitespace(const char *s, char *left, size_t maxleft,
                            char *right, size_t maxright) {
  const char *p = s;
  while (*p && isspace((unsigned char)*p))
    p++;

  if (!*p) {
    left[0] = '\0';
    right[0] = '\0';

    return 0;
  }

  const char *start = p;
  while (*p && !isspace((unsigned char)*p))
    p++;

  size_t llen = p - start;
  if (llen >= maxleft)
    llen = maxleft - 1;

  memcpy(left, start, llen);
  left[llen] = '\0';

  while (*p && isspace((unsigned char)*p))
    p++;

  strncpy(right, p, maxright - 1);
  right[maxright - 1] = '\0';

  return *right ? 2 : 1;
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
      alignment = numeric_literal_to_int(parts[0]);
    if (n >= 2)
      offset = numeric_literal_to_int(parts[1]);

    free_parts(parts, n);
    free(parts);

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

  char *op = strtrim(parts[0]);
  if (!is_valid_imval(op)) {
    strncpy(errbuf, "Invalid operand: expected immediate value", errmax);

    free_parts(parts, n);
    free(parts);

    return;
  }

  long long com = imval_to_int(op);
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
  long long com = imval_to_int(op);
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

  r.rn = get_reg_num(subparts[0]);
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

      if (sn == 4) {
        /* shift */
        char *shiftstr = subparts[3];
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
  } else
    strncpy(errbuf, "Unknown pseudoinstruction (bug)", errmax);
}

static void get_replacement(const char *name, const char *operands,
                            long long address, char *newop, size_t newopmax,
                            char *newoperands, size_t newopmax2) {
  char u[8];

  strupper(name, u, sizeof(u));

  if (strcmp(u, "ADR") == 0) {
    char *parts[2];

    split_by_comma_maxsplit(operands, 1, parts, 2);

    char *reg = strtrim(parts[0]);
    char *expr = strtrim(parts[1]);
    long long offs = pcrelative_expression_to_int(expr, address);
    int sign, rot;
    unsigned int imv;

    int_to_signrotimv(offs, &sign, &rot, &imv);

    strncpy(newop, sign == 1 ? "ADD" : "SUB", newopmax - 1);
    newop[newopmax - 1] = '\0';

    long long absoffs = offs < 0 ? -offs : offs;

    snprintf(newoperands, newopmax2, "%s, PC, #%lld", reg, absoffs);

    free(parts[0]);
    free(parts[1]);
  }
}

/* directive check/encode */
static void check_directive(const char *name, const char *operands,
                            char *errbuf, size_t errmax) {
  errbuf[0] = '\0';

  char u[16];

  strupper(name, u, sizeof(u));

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
                "Numeric literal outside of 32bit range: lower than -2^15",
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
      snprintf(errbuf, errmax, "Could not open file \"%s\"", operands);

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
}

typedef struct {
  char line[4096];
  char notcomment[4096];
  char label[256];
  char operation[4096];
  char opname[64];
  char flags[8];
  char condcode[4];
  char operands[4096];
  long long address;
  ByteBuf hexcode;
  int length;
  char errmsg[512];
} Sourceline;

static void sl_init(Sourceline *sl, const char *line) {
  strncpy(sl->line, line, sizeof(sl->line) - 1);

  sl->line[sizeof(sl->line) - 1] = '\0';
  sl->notcomment[0] = '\0';
  sl->label[0] = '\0';
  sl->operation[0] = '\0';
  sl->opname[0] = '\0';
  sl->flags[0] = '\0';
  sl->condcode[0] = '\0';
  sl->operands[0] = '\0';
  sl->address = -1;

  bb_init(&sl->hexcode);

  sl->length = -1;
  sl->errmsg[0] = '\0';
}

static int sl_parse_comments(Sourceline *sl) {
  int sci = -1, dqi = -1;

  const char *p;
  for (p = sl->line; *p; p++) {
    if (*p == ';' && sci < 0)
      sci = (int)(p - sl->line);
    if (*p == '"' && dqi < 0)
      dqi = (int)(p - sl->line);
  }

  if (sci < 0 || (dqi >= 0 && dqi < sci)) {
    /* no semicolon, or semicolon is after a quote */
    if (sci >= 0) {
      /* check if ; is inside a string */
      char tmpline[4096];
      strncpy(tmpline, sl->line, sizeof(tmpline) - 1);
      tmpline[sizeof(tmpline) - 1] = '\0';

      /* replace \" with "" */
      char *bs;
      while ((bs = strstr(tmpline, "\\\"")) != NULL) {
        bs[0] = bs[1] = '"';
      }

      /* recalculate sci */
      sci = -1;

      int inquote = 0;

      int i;
      for (i = 0; sl->line[i]; i++) {
        if (sl->line[i] == '"')
          inquote = !inquote;
        if (sl->line[i] == ';' && !inquote) {
          sci = i;

          break;
        }
      }
    }
  }

  if (sci < 0)
    strncpy(sl->notcomment, sl->line, sizeof(sl->notcomment) - 1);
  else {
    strncpy(sl->notcomment, sl->line,
            sci < (int)sizeof(sl->notcomment) - 1
                ? sci
                : (int)sizeof(sl->notcomment) - 1);
    sl->notcomment[sci < (int)sizeof(sl->notcomment) - 1
                       ? sci
                       : (int)sizeof(sl->notcomment) - 1] = '\0';
  }

  sl->notcomment[sizeof(sl->notcomment) - 1] = '\0';

  /* rstrip */
  size_t l = strlen(sl->notcomment);
  while (l > 0 && isspace((unsigned char)sl->notcomment[l - 1])) {
    sl->notcomment[--l] = '\0';
  }

  return 0;
}

static int sl_parse_labelpart(Sourceline *sl) {
  if (!sl->notcomment[0] || isspace((unsigned char)sl->notcomment[0])) {
    char *t = strtrim(sl->notcomment);

    strncpy(sl->operation, t, sizeof(sl->operation) - 1);
    sl->operation[sizeof(sl->operation) - 1] = '\0';

    return 0;
  }

  char left[256], right[4096];

  int n = split_whitespace(sl->notcomment, left, sizeof(left), right,
                           sizeof(right));
  if (n == 0)
    return 0;

  strncpy(sl->label, left, sizeof(sl->label) - 1);
  sl->label[sizeof(sl->label) - 1] = '\0';

  if (n == 2) {
    char *t = strtrim(right);

    strncpy(sl->operation, t, sizeof(sl->operation) - 1);
    sl->operation[sizeof(sl->operation) - 1] = '\0';
  }

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
    if (startswith(sl->opname, tbhsoplist[i])) {
      if (sl->opname[strlen(sl->opname) - 1] == 'T') {
        sl->opname[strlen(sl->opname) - 1] = '\0';
        char tmp[8];

        snprintf(tmp, sizeof(tmp), "T%s", sl->flags);
        strncpy(sl->flags, tmp, sizeof(sl->flags) - 1);
      }

      if (sl->opname[strlen(sl->opname) - 1] == 'B') {
        sl->opname[strlen(sl->opname) - 1] = '\0';

        char tmp[8];

        snprintf(tmp, sizeof(tmp), "B%s", sl->flags);
        strncpy(sl->flags, tmp, sizeof(sl->flags) - 1);
      }

      if (sl->opname[strlen(sl->opname) - 1] == 'H') {
        sl->opname[strlen(sl->opname) - 1] = '\0';

        char tmp[8];

        snprintf(tmp, sizeof(tmp), "H%s", sl->flags);
        strncpy(sl->flags, tmp, sizeof(sl->flags) - 1);
      }

      if (sl->opname[strlen(sl->opname) - 1] == 'S') {
        sl->opname[strlen(sl->opname) - 1] = '\0';

        char tmp[8];

        snprintf(tmp, sizeof(tmp), "S%s", sl->flags);
        strncpy(sl->flags, tmp, sizeof(sl->flags) - 1);
      }

      break;
    }
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

  if (n == 2) {
    char *t = strtrim(right);

    strncpy(sl->operands, t, sizeof(sl->operands) - 1);
    sl->operands[sizeof(sl->operands) - 1] = '\0';
  }

  sl_parse_s_suffix(sl);
  sl_parse_tbhs_suffixes(sl);
  sl_parse_addrmode_suffixes(sl);
  sl_parse_condition_code(sl);

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

static int sl_set_length_and_address(Sourceline *sl, long long address) {
  sl->address = address;

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

    return -1;
  }

  char newop[64], newoperands[4096];

  get_replacement(sl->opname, sl->operands, sl->address, newop, sizeof(newop),
                  newoperands, sizeof(newoperands));

  strncpy(sl->opname, newop, sizeof(sl->opname) - 1);
  strncpy(sl->operands, newoperands, sizeof(sl->operands) - 1);

  return 0;
}

static int sl_check_syntax(Sourceline *sl) {
  char fullname[128];

  snprintf(fullname, sizeof(fullname), "%s%s", sl->opname, sl->flags);

  char errbuf[512];

  errbuf[0] = '\0';

  if (is_directive(fullname))
    check_directive(sl->opname, sl->operands, errbuf, sizeof(errbuf));
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
    bb_free(&cl->lines[i]->hexcode);
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
        printerror(fullpath, i, c->line, c->errmsg);
      else
        printerror(fullpath, i, c->line, "unknown error in parse_comments");

      numerrs++;

      continue;
    }

    if (sl_parse_labelpart(c) != 0) {
      if (c->errmsg[0])
        printerror(fullpath, i, c->line, c->errmsg);
      else
        printerror(fullpath, i, c->line, "unknown error in parse_labelpart");

      numerrs++;

      continue;
    }

    if (sl_parse_namepart(c) != 0) {
      if (c->errmsg[0])
        printerror(fullpath, i, c->line, c->errmsg);
      else
        printerror(fullpath, i, c->line, "unknown error in parse_namepart");

      numerrs++;

      continue;
    }

    if (sl_is_incbin(c)) {
      char absp[MAX_PATH];
      long sz = add_file(c->operands, absp, MAX_PATH);

      strncpy(c->operands, absp, sizeof(c->operands) - 1);

      if (sz < 0) {
        printerror(fullpath, i, c->line, "error in add_file");

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
      bb_free(&code[i]->hexcode);
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

  CodeList code;

  cl_init(&code);

  int numerrs = read_file_and_stage1_parse(infile, NULL, 0, &code);
  if (numerrs > 0) {
    printf("Stopping assembler: %d Error(s)\n", numerrs);

    cl_free(&code);

    return -1;
  }

  /* Stage 2: calculate length and address */
  long long curaddr = 0;

  int i;
  for (i = 0; i < code.n; i++) {
    Sourceline *c = code.lines[i];

    if (sl_set_length_and_address(c, curaddr) != 0) {
      if (c->errmsg[0])
        printerror(infile, i, c->line, c->errmsg);
      else
        printerror(infile, i, c->line,
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

  /* Stage 3: build label dictionary */
  for (i = 0; i < code.n; i++) {
    Sourceline *c = code.lines[i];

    if (c->label[0]) {
      long long existing;

      if (label_lookup(c->label, &existing)) {
        printerror(infile, i, c->line, "Label name already used");

        numerrs++;
      } else {
        label_insert(c->label, c->address);
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
        printerror(infile, i, c->line, c->errmsg);
      else
        printerror(infile, i, c->line,
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
        printerror(infile, i, c->line, c->errmsg);
      else
        printerror(infile, i, c->line, "unknown error in assemble");

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

  if (use_zehn) {
    uint32_t exec_size = 0;
    for (i = 0; i < code.n; i++) {
      exec_size += code.lines[i]->hexcode.len;
    }

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

  return 0;
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

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  settings_load();

  gfx_init();

  const char *infile = NULL;

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

      if (ans == 1) {
        continue;
      }
    }

    break;
  }

  char outfile[MAX_PATH];
  make_outpath(infile, outfile, MAX_PATH);

  const char *fmt_lines[] = {"Select output binary format:", "",
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

  if (ret == 0) {
    char line1[MAX_PATH + 16];
    snprintf(line1, sizeof(line1), "Output: %s", outfile);
    const char *lines[] = {"Assembly successful.", line1};
    gfx_window_alert("NASM", lines, 2, "OK", 0);
  } else {
    gfx_window_scrolltext("Errors Found", (const char **)g_err_lines,
                          g_num_err_lines, "Close");
  }

  clear_errors();
  gfx_deinit();
  return (ret == 0) ? 0 : 1;
}
