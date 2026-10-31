#ifndef RENDER_H
#define RENDER_H

#include <stddef.h>

#include "utf8.h"

typedef struct {
    char *p;
    size_t len;
    size_t cap;
} Buf;

void buf_free(Buf *b);
void render_frame(Buf *out, const Text *title, const Text *sub, int rows, int cols);

#endif
