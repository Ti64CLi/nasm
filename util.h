/*
 * util.h
 * Generic string, splitting and bit/byte helpers shared by the assembler.
 */
#ifndef UTIL_H_INCLUDED
#define UTIL_H_INCLUDED

#include <stddef.h>

/* --- string helpers --- */
char *strtrim(char *s);
char *strdup_s(const char *s);
int strcasecmp_s(const char *a, const char *b);
void strupper(const char *src, char *dst, size_t maxlen);
int startswith(const char *s, const char *prefix);
int endswith(const char *s, const char *suffix);

/* --- character-class helpers --- */
int my_isalnum(char c);
int str_isxdigit(const char *s);
int str_isoctdigit(const char *s);
int str_isbindigit(const char *s);
int str_isdigit_all(const char *s);

/* --- operand splitting (quote-aware) --- */
const char *find_comma_unquoted(const char *s);
char **split_by_comma(const char *s, int *n);
int split_by_comma_maxsplit(const char *s, int maxsplit, char **parts,
                            int max_parts);
void free_parts(char **parts, int n);
int split_whitespace(const char *s, char *left, size_t maxleft, char *right,
                     size_t maxright);

/* --- bit/byte helpers --- */
unsigned int rotateleft32(unsigned int n, int r);
void encode_32bit_arr(const int *offsets, const int *lengths,
                      const unsigned int *values, int n, unsigned char out[4]);
void encode_16bit_arr(const int *offsets, const int *lengths,
                      const unsigned int *values, int n, unsigned char out[2]);
void bigendian_to_littleendian(unsigned char *b, size_t len);
void bigendian_to_littleendian_16bit(unsigned char *b, size_t len);

#endif /* UTIL_H_INCLUDED */
