#define _GNU_SOURCE

#include <getopt.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "render.h"
#include "term.h"
#include "utf8.h"
#include "util.h"

#define VERSION "0.1"

typedef struct {
    Text *v;
    size_t n;
} TextList;

static void usage(FILE *f)
{
    fputs(
        "Usage: peatitle [-g TEXT] [-s TEXT] [-w SECONDS]\n"
        "       peatitle -h | -v\n"
        "\n"
        "Show large pixel text in the center of the terminal.\n"
        "\n"
        "Options:\n"
        "  -g TEXT     title; \"\\n\" starts a new line, lines are shown one by one\n"
        "  -s TEXT     subtitle; same rules as the title\n"
        "  -w SECONDS  delay between lines, default 1 (for example 0.5)\n"
        "  -h          show this help\n"
        "  -v          show version\n"
        "\n"
        "Quit with q, Esc or Ctrl-C.\n"
        "\n"
        "Examples:\n"
        "  peatitle -g \"Hello\"\n"
        "  peatitle -g \"text1\\ntext2\\ntext3\" -w 0.5\n"
        "  peatitle -g \"Привет\" -s \"Мир\"\n",
        f);
}

static void add_line(TextList *l, const char *s, size_t n)
{
    l->v = xrealloc(l->v, (l->n + 1) * sizeof(*l->v));
    text_from_bytes(&l->v[l->n], s, n);
    l->n++;
}

static void split_arg(TextList *l, const char *arg)
{
    size_t len = strlen(arg), n = 0, i;
    char *cur = xmalloc(len + 1);

    for (i = 0; i < len; i++) {
        char c = arg[i];
        if (c == '\\' && i + 1 < len) {
            if (arg[i + 1] == 'n') {
                add_line(l, cur, n);
                n = 0;
                i++;
                continue;
            }
            if (arg[i + 1] == '\\') {
                cur[n++] = '\\';
                i++;
                continue;
            }
        }
        if (c == '\n') {
            add_line(l, cur, n);
            n = 0;
            continue;
        }
        if (c == '\r')
            continue;
        cur[n++] = c;
    }
    add_line(l, cur, n);
    free(cur);
}

static void free_list(TextList *l)
{
    size_t i;

    for (i = 0; i < l->n; i++)
        free(l->v[i].cp);
    free(l->v);
    l->v = NULL;
    l->n = 0;
}

static int parse_interval(const char *s, double *out)
{
    char buf[64], *end;
    size_t i, len = strlen(s);
    double v;

    if (len == 0 || len >= sizeof(buf))
        return -1;
    for (i = 0; i <= len; i++)
        buf[i] = (s[i] == ',') ? '.' : s[i];
    v = strtod(buf, &end);
    if (*end != '\0' || !(v >= 0.01 && v <= 1000000.0))
        return -1;
    *out = v;
    return 0;
}

static double now_sec(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int is_quit_key(const unsigned char *b, size_t i, size_t n)
{
    unsigned char c = b[i];

    if (c == 'q' || c == 'Q' || c == 0x03 || c == 0x04)
        return 1;
    if (c == 0xD0 && i + 1 < n && (b[i + 1] == 0xB9 || b[i + 1] == 0x99))
        return 1;
    return 0;
}

static void handle_input(int fd, short revents)
{
    unsigned char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf));
    size_t i;

    if (n <= 0) {
        if (n == 0 && (revents & (POLLHUP | POLLERR)))
            term_quit = 1;
        return;
    }
    for (i = 0; i < (size_t)n; i++) {
        if (buf[i] == 0x1B) {
            if (i + 1 == (size_t)n) {
                struct pollfd p = { fd, POLLIN, 0 };
                if (poll(&p, 1, 50) > 0) {
                    ssize_t k = read(fd, buf, sizeof(buf));
                    (void)k;
                    return;
                }
                term_quit = 1;
            }
            return;
        }
        if (is_quit_key(buf, i, (size_t)n)) {
            term_quit = 1;
            return;
        }
    }
}

static void run(const TextList *titles, const TextList *subs, double interval)
{
    static const Text empty = { NULL, 0 };
    Buf out = { NULL, 0, 0 };
    double start = now_sec();
    long long last_tick = -1;
    int first = 1;

    while (!term_quit) {
        double now = now_sec();
        long long tick = (long long)((now - start) / interval);

        if (first || tick != last_tick || term_resized) {
            int rows, cols;
            const Text *t = titles->n ? &titles->v[(size_t)(tick % (long long)titles->n)] : &empty;
            const Text *s = subs->n ? &subs->v[(size_t)(tick % (long long)subs->n)] : &empty;

            term_resized = 0;
            first = 0;
            last_tick = tick;
            term_size(&rows, &cols);
            render_frame(&out, t, s, rows, cols);
            if (term_write(out.p, out.len) < 0)
                break;
        }

        now = now_sec();
        {
            double wait = start + (double)(tick + 1) * interval - now;
            int ms = (int)(wait * 1000.0) + 1;
            struct pollfd p;

            if (ms < 1)
                ms = 1;
            if (ms > 200)
                ms = 200;
            p.fd = term_input_fd();
            p.events = POLLIN;
            p.revents = 0;
            if (poll(&p, 1, ms) > 0)
                handle_input(p.fd, p.revents);
        }
    }
    buf_free(&out);
}

int main(int argc, char **argv)
{
    static const struct option longopts[] = {
        { "help", no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'v' },
        { NULL, 0, NULL, 0 }
    };
    TextList titles = { NULL, 0 }, subs = { NULL, 0 };
    double interval = 1.0;
    int opt;

    opterr = 0;
    while ((opt = getopt_long(argc, argv, ":g:s:w:hv", longopts, NULL)) != -1) {
        switch (opt) {
        case 'g':
            free_list(&titles);
            split_arg(&titles, optarg);
            break;
        case 's':
            free_list(&subs);
            split_arg(&subs, optarg);
            break;
        case 'w':
            if (parse_interval(optarg, &interval) < 0) {
                fprintf(stderr, "peatitle: invalid interval '%s' (expected seconds, at least 0.01)\n", optarg);
                return 1;
            }
            break;
        case 'h':
            usage(stdout);
            return 0;
        case 'v':
            puts("peatitle " VERSION);
            return 0;
        case ':':
            fprintf(stderr, "peatitle: option -%c requires an argument\n", optopt);
            fputs("Try 'peatitle -h' for more information.\n", stderr);
            return 1;
        default:
            if (optopt)
                fprintf(stderr, "peatitle: invalid option -%c\n", optopt);
            else
                fprintf(stderr, "peatitle: invalid option '%s'\n", argv[optind - 1]);
            fputs("Try 'peatitle -h' for more information.\n", stderr);
            return 1;
        }
    }
    if (optind < argc) {
        fprintf(stderr, "peatitle: unexpected argument '%s'\n", argv[optind]);
        fputs("Try 'peatitle -h' for more information.\n", stderr);
        return 1;
    }
    if (titles.n == 0 && subs.n == 0) {
        fputs("peatitle: no text given, use -g TEXT\n", stderr);
        fputs("Try 'peatitle -h' for more information.\n", stderr);
        return 1;
    }

    if (term_init() < 0)
        return 1;
    run(&titles, &subs, interval);
    term_restore();
    free_list(&titles);
    free_list(&subs);
    return 0;
}
