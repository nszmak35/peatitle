#include "render.h"
#include "font.h"
#include "util.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define LETTER_GAP 1
#define WORD_GAP 4
#define LINE_GAP 2

typedef struct {
    const Glyph **g;
    size_t n;
    int px;
} Word;

typedef struct {
    size_t first;
    size_t count;
    int px;
} Line;

typedef struct {
    size_t start;
    size_t end;
    int width;
} Span;

static void buf_add(Buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < b->len + n + 1)
            cap *= 2;
        b->p = xrealloc(b->p, cap);
        b->cap = cap;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = '\0';
}

static void buf_puts(Buf *b, const char *s)
{
    buf_add(b, s, strlen(s));
}

static void buf_addf(Buf *b, const char *fmt, ...)
{
    char tmp[64];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0)
        buf_add(b, tmp, (size_t)n);
}

void buf_free(Buf *b)
{
    free(b->p);
    b->p = NULL;
    b->len = b->cap = 0;
}

static int word_px(const Glyph **g, size_t n)
{
    int w = 0;
    size_t i;

    for (i = 0; i < n; i++)
        w += g[i]->w;
    if (n > 1)
        w += (int)(n - 1) * LETTER_GAP;
    return w;
}

static size_t build_words(const Text *t, Word **words, const Glyph ***all)
{
    size_t i, nw = 0, ng = 0;
    int in_word = 0;

    *words = xmalloc((t->len + 1) * sizeof(Word));
    *all = xmalloc((t->len + 1) * sizeof(const Glyph *));
    for (i = 0; i < t->len; i++) {
        if (font_is_space(t->cp[i])) {
            in_word = 0;
            continue;
        }
        if (!in_word) {
            (*words)[nw].g = *all + ng;
            (*words)[nw].n = 0;
            nw++;
            in_word = 1;
        }
        (*all)[ng++] = font_lookup(t->cp[i]);
        (*words)[nw - 1].n++;
    }
    for (i = 0; i < nw; i++)
        (*words)[i].px = word_px((*words)[i].g, (*words)[i].n);
    return nw;
}

static size_t break_words(Word **words, size_t nw, int max_px)
{
    size_t i, total = 0, out = 0;
    Word *res;

    for (i = 0; i < nw; i++)
        total += (*words)[i].n;
    res = xmalloc((total + 1) * sizeof(Word));
    for (i = 0; i < nw; i++) {
        const Word *w = &(*words)[i];
        size_t k = 0;
        while (k < w->n) {
            size_t s = k;
            int px = w->g[k]->w;
            k++;
            while (k < w->n && px + LETTER_GAP + w->g[k]->w <= max_px) {
                px += LETTER_GAP + w->g[k]->w;
                k++;
            }
            res[out].g = w->g + s;
            res[out].n = k - s;
            res[out].px = px;
            out++;
        }
    }
    free(*words);
    *words = res;
    return out;
}

static int wrap_words(const Word *w, size_t n, int max_px, Line *lines, size_t *nl)
{
    size_t i, l = 0;

    for (i = 0; i < n; i++) {
        if (w[i].px > max_px)
            return 0;
        if (l > 0 && lines[l - 1].px + WORD_GAP + w[i].px <= max_px) {
            lines[l - 1].px += WORD_GAP + w[i].px;
            lines[l - 1].count++;
        } else {
            lines[l].first = i;
            lines[l].count = 1;
            lines[l].px = w[i].px;
            l++;
        }
    }
    *nl = l;
    return 1;
}

static void draw_glyph(unsigned char *bm, int bw, int x, int y, const Glyph *g)
{
    int yy, xx;

    for (yy = 0; yy < FONT_HEIGHT; yy++) {
        const char *r = g->rows[yy];
        for (xx = 0; r[xx] && xx < g->w; xx++)
            if (r[xx] == '#')
                bm[(y + yy) * bw + x + xx] = 1;
    }
}

static size_t wrap_sub(const Text *t, int cols, size_t maxl, Span *spans)
{
    size_t i = 0, start = 0, n = 0;
    long lastsp = -1;
    int w = 0;

    while (i < t->len) {
        int cw = cp_width(t->cp[i]);
        if (w + cw > cols && i > start) {
            size_t end, next;
            if (lastsp >= (long)start) {
                end = (size_t)lastsp;
                next = end + 1;
            } else {
                end = next = i;
            }
            if (n < maxl) {
                spans[n].start = start;
                spans[n].end = end;
                n++;
            }
            start = next;
            i = next;
            w = 0;
            lastsp = -1;
            continue;
        }
        if (t->cp[i] == ' ')
            lastsp = (long)i;
        w += cw;
        i++;
    }
    if (n < maxl && start < t->len) {
        spans[n].start = start;
        spans[n].end = t->len;
        n++;
    }
    return n;
}

static size_t trim_spans(const Text *t, Span *spans, size_t n)
{
    size_t i, out = 0, k;

    for (i = 0; i < n; i++) {
        size_t s = spans[i].start, e = spans[i].end;
        int w = 0;
        while (s < e && t->cp[s] == ' ')
            s++;
        while (e > s && t->cp[e - 1] == ' ')
            e--;
        if (s == e)
            continue;
        for (k = s; k < e; k++)
            w += cp_width(t->cp[k]);
        spans[out].start = s;
        spans[out].end = e;
        spans[out].width = w;
        out++;
    }
    return out;
}

static void emit_sub_line(Buf *out, const Text *t, const Span *sp, int cols)
{
    size_t k;
    char enc[4];
    int x = (cols - sp->width) / 2;

    if (x < 0)
        x = 0;
    buf_addf(out, "\x1b[%dG\x1b[1m", x + 1);
    for (k = sp->start; k < sp->end; k++)
        buf_add(out, enc, utf8_encode(t->cp[k], enc));
    buf_puts(out, "\x1b[0m");
}

void render_frame(Buf *out, const Text *title, const Text *sub, int rows, int cols)
{
    Word *words = NULL;
    const Glyph **all = NULL;
    Line *lines = NULL;
    Span *spans = NULL;
    unsigned char *bm = NULL;
    size_t nw, nl = 0, nsub = 0, maxl, li, wi, gi;
    int mv, mh, gap, avail, t = 0, tt, tmax, max_px = 1;
    int bw = 0, bh = 0, title_rows = 0, block, y0, x0, r;

    out->len = 0;
    if (out->p)
        out->p[0] = '\0';

    nw = build_words(title, &words, &all);

    maxl = (size_t)(rows / 4);
    if (maxl < 1)
        maxl = 1;
    spans = xmalloc((sub->len + 1) * sizeof(Span));
    nsub = trim_spans(sub, spans, wrap_sub(sub, cols > 0 ? cols : 1, maxl, spans));

    mv = rows >= 12 ? rows / 12 : 0;
    mh = cols / 20;
    gap = (nw > 0 && nsub > 0) ? 1 : 0;
    avail = rows - 2 * mv - (int)nsub - gap;
    if (nw > 0 && nsub > 0 && avail < 4) {
        nsub = 0;
        gap = 0;
        avail = rows - 2 * mv;
    }
    if (avail < 4)
        avail = rows;
    if (avail < 1)
        avail = 1;

    if (nw > 0) {
        lines = xmalloc(nw * sizeof(Line) + sizeof(Line));
        tmax = 2 * avail / FONT_HEIGHT;
        if (tmax < 1)
            tmax = 1;
        for (tt = tmax; tt >= 1; tt--) {
            int h;
            max_px = (cols - 2 * mh) / tt;
            if (max_px < 1)
                continue;
            if (!wrap_words(words, nw, max_px, lines, &nl))
                continue;
            h = ((int)nl * FONT_HEIGHT + ((int)nl - 1) * LINE_GAP) * tt;
            if ((h + 1) / 2 <= avail) {
                t = tt;
                break;
            }
        }
        if (!t) {
            t = 1;
            max_px = cols - 2 * mh;
            if (max_px < 1)
                max_px = 1;
            nw = break_words(&words, nw, max_px);
            free(lines);
            lines = xmalloc(nw * sizeof(Line) + sizeof(Line));
            wrap_words(words, nw, max_px, lines, &nl);
        }

        for (li = 0; li < nl; li++)
            if (lines[li].px > bw)
                bw = lines[li].px;
        bh = (int)nl * FONT_HEIGHT + ((int)nl - 1) * LINE_GAP;
        bm = xcalloc((size_t)bw * (size_t)bh, 1);
        for (li = 0; li < nl; li++) {
            int x = (bw - lines[li].px) / 2;
            int y = (int)li * (FONT_HEIGHT + LINE_GAP);
            for (wi = 0; wi < lines[li].count; wi++) {
                const Word *w = &words[lines[li].first + wi];
                for (gi = 0; gi < w->n; gi++) {
                    draw_glyph(bm, bw, x, y, w->g[gi]);
                    x += w->g[gi]->w;
                    if (gi + 1 < w->n)
                        x += LETTER_GAP;
                }
                x += WORD_GAP;
            }
        }
        title_rows = (bh * t + 1) / 2;
    }

    block = title_rows + gap + (int)nsub;
    y0 = (rows - block) / 2;
    if (y0 < 0)
        y0 = 0;
    x0 = (cols - bw * t) / 2;

    for (r = 0; r < rows; r++) {
        int tr = r - y0;
        int sr = r - (y0 + title_rows + gap);

        buf_addf(out, "\x1b[%d;1H\x1b[0m\x1b[2K", r + 1);
        if (bm && tr >= 0 && tr < title_rows) {
            int c, c0 = x0 < 0 ? 0 : x0;
            int c1 = x0 + bw * t > cols ? cols : x0 + bw * t;
            int h0 = 2 * tr, h1 = 2 * tr + 1, rev = 0;

            buf_addf(out, "\x1b[%dG", c0 + 1);
            for (c = c0; c < c1; c++) {
                int px = (c - x0) / t;
                int top = h0 < bh * t ? bm[(h0 / t) * bw + px] : 0;
                int bot = h1 < bh * t ? bm[(h1 / t) * bw + px] : 0;
                if (top && bot) {
                    if (!rev) {
                        buf_puts(out, "\x1b[7m");
                        rev = 1;
                    }
                    buf_puts(out, " ");
                    continue;
                }
                if (rev) {
                    buf_puts(out, "\x1b[27m");
                    rev = 0;
                }
                if (top)
                    buf_puts(out, "\xE2\x96\x80");
                else if (bot)
                    buf_puts(out, "\xE2\x96\x84");
                else
                    buf_puts(out, " ");
            }
            buf_puts(out, "\x1b[0m");
        } else if (sr >= 0 && sr < (int)nsub) {
            emit_sub_line(out, sub, &spans[sr], cols);
        }
    }

    free(bm);
    free(lines);
    free(spans);
    free(words);
    free(all);
}
