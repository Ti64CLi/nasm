/*
 * bytebuf.c
 * Growable byte buffer used for emitted machine code.
 */

#include <stdlib.h>
#include <string.h>

#include "bytebuf.h"

void bb_init(ByteBuf *b) {
  b->data = NULL;
  b->len = 0;
  b->cap = 0;
}

void bb_free(ByteBuf *b) {
  free(b->data);

  b->data = NULL;
  b->len = b->cap = 0;
}

int bb_reserve(ByteBuf *b, size_t extra) {
  if (b->len + extra <= b->cap)
    return 0;

  size_t newcap = b->cap ? b->cap * 2 : 16;
  while (newcap < b->len + extra)
    newcap *= 2;

  unsigned char *nd = realloc(b->data, newcap);
  if (!nd)
    return -1;

  b->data = nd;
  b->cap = newcap;

  return 0;
}

int bb_push(ByteBuf *b, unsigned char c) {
  if (bb_reserve(b, 1) < 0)
    return -1;

  b->data[b->len++] = c;

  return 0;
}

int bb_append(ByteBuf *b, const unsigned char *src, size_t n) {
  if (n == 0)
    return 0;

  if (bb_reserve(b, n) < 0)
    return -1;

  memcpy(b->data + b->len, src, n);
  b->len += n;

  return 0;
}

int bb_append_zeros(ByteBuf *b, size_t n) {
  if (n == 0)
    return 0;

  if (bb_reserve(b, n) < 0)
    return -1;

  memset(b->data + b->len, 0, n);
  b->len += n;

  return 0;
}
