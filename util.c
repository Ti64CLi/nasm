/*
 * util.c
 * Generic string, splitting and bit/byte helpers.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

/* Trim leading/trailing whitespace in-place, returns pointer to start */
char *strtrim(char *s) {
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
char *strdup_s(const char *s) {
  size_t len = strlen(s) + 1;

  char *d = malloc(len);
  if (d)
    memcpy(d, s, len);

  return d;
}

/* Case-insensitive string compare */
int strcasecmp_s(const char *a, const char *b) {
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
void strupper(const char *src, char *dst, size_t maxlen) {
  size_t i;
  for (i = 0; i < maxlen - 1 && src[i]; i++)
    dst[i] = (char)toupper((unsigned char)src[i]);

  dst[i] = '\0';
}

/* startswith */
int startswith(const char *s, const char *prefix) {
  return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* endswith */
int endswith(const char *s, const char *suffix) {
  size_t sl = strlen(s), pl = strlen(suffix);
  if (pl > sl)
    return 0;

  return strcmp(s + sl - pl, suffix) == 0;
}

/* Find the next comma in s that is not inside a "..." string or a
 * 'c' character literal. Returns pointer to it, or NULL. */
const char *find_comma_unquoted(const char *s) {
  int indq = 0;
  size_t i;

  for (i = 0; s[i]; i++) {
    char c = s[i];

    if (indq) {
      if (c == '"')
        indq = 0;
      continue;
    }

    if (c == '"') {
      indq = 1;
      continue;
    }

    if (c == '\'' && s[i + 1] && s[i + 2] == '\'') {
      i += 2;
      continue;
    }

    if (c == ',')
      return s + i;
  }

  return NULL;
}

/* Split s by comma into parts. Returns array of strdup'd strings, *n = count.
 * Caller frees. Commas inside quotes are not split points. */
char **split_by_comma(const char *s, int *n) {
  char **parts = NULL;
  *n = 0;

  if (!s || !*s)
    return NULL;

  const char *start = s;

  while (1) {
    const char *comma = find_comma_unquoted(start);
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

/* Split by comma with limit (like Python split(',', maxsplit)) */
int split_by_comma_maxsplit(const char *s, int maxsplit, char **parts,
                                   int max_parts) {
  int n = 0;
  const char *start = s;

  while (n < max_parts) {
    const char *comma = (n < maxsplit) ? find_comma_unquoted(start) : NULL;
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

void free_parts(char **parts, int n) {
  int i;
  for (i = 0; i < n; i++)
    free(parts[i]);
}

/* Split by whitespace (any amount), at most 2 parts */
int split_whitespace(const char *s, char *left, size_t maxleft,
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

int my_isalnum(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z');
}

int str_isxdigit(const char *s) {
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

int str_isoctdigit(const char *s) {
  if (!s || !*s)
    return 0;

  while (*s) {
    if (*s < '0' || *s > '7')
      return 0;

    s++;
  }

  return 1;
}

int str_isbindigit(const char *s) {
  if (!s || !*s)
    return 0;

  while (*s) {
    if (*s != '0' && *s != '1')
      return 0;

    s++;
  }

  return 1;
}

int str_isdigit_all(const char *s) {
  if (!s || !*s)
    return 0;

  while (*s) {
    if (!isdigit((unsigned char)*s))
      return 0;

    s++;
  }

  return 1;
}

unsigned int rotateleft32(unsigned int n, int r) {
  n &= 0xFFFFFFFFU;
  r %= 32;
  if (r == 0)
    return n;

  return ((n << r) | (n >> (32 - r))) & 0xFFFFFFFFU;
}

/* encode_32bit: l is array of {offset, length, value} triples, n is number of
 * triples */
void encode_32bit_arr(const int *offsets, const int *lengths,
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

void encode_16bit_arr(const int *offsets, const int *lengths,
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

void bigendian_to_littleendian(unsigned char *b, size_t len) {
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

void bigendian_to_littleendian_16bit(unsigned char *b, size_t len) {
  size_t i;

  if (len % 2 != 0)
    return;

  for (i = 0; i < len; i += 2) {
    unsigned char tmp = b[i];

    b[i] = b[i + 1];
    b[i + 1] = tmp;
  }
}
