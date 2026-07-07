/*
 * bytebuf.h
 * Growable byte buffer used for emitted machine code.
 */
#ifndef BYTEBUF_H_INCLUDED
#define BYTEBUF_H_INCLUDED

#include <stddef.h>

typedef struct {
  unsigned char *data;
  size_t len;
  size_t cap;
} ByteBuf;

void bb_init(ByteBuf *b);
void bb_free(ByteBuf *b);
int bb_reserve(ByteBuf *b, size_t extra);
int bb_push(ByteBuf *b, unsigned char c);
int bb_append(ByteBuf *b, const unsigned char *src, size_t n);
int bb_append_zeros(ByteBuf *b, size_t n);

#endif /* BYTEBUF_H_INCLUDED */
