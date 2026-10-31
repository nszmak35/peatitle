#ifndef UTF8_H
#define UTF8_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t *cp;
    size_t len;
} Text;

size_t utf8_decode(const unsigned char *s, size_t n, uint32_t *cp);
size_t utf8_encode(uint32_t cp, char *out);
int cp_width(uint32_t cp);
void text_from_bytes(Text *t, const char *s, size_t n);

#endif
