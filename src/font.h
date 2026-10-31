#ifndef FONT_H
#define FONT_H

#include <stddef.h>
#include <stdint.h>

#define FONT_HEIGHT 10

typedef struct {
    uint32_t cp;
    int w;
    const char *rows[FONT_HEIGHT];
} Glyph;

const Glyph *font_lookup(uint32_t cp);
int font_is_space(uint32_t cp);

#endif
