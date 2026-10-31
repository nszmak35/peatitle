#include "term.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define SEQ_ENTER "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J"
#define SEQ_LEAVE "\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l"

volatile sig_atomic_t term_quit;
volatile sig_atomic_t term_resized;

static struct termios saved_tio;
static int in_fd = -1;
static int own_in_fd;
static int active;

static void on_quit(int sig)
{
    (void)sig;
    term_quit = 1;
}

static void on_winch(int sig)
{
    (void)sig;
    term_resized = 1;
}

static void install_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_quit;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

int term_write(const char *s, size_t n)
{
    while (n > 0) {
        ssize_t w = write(STDOUT_FILENO, s, n);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        s += w;
        n -= (size_t)w;
    }
    return 0;
}

int term_input_fd(void)
{
    return in_fd;
}

int term_init(void)
{
    struct termios raw;

    if (!isatty(STDOUT_FILENO)) {
        fputs("peatitle: standard output is not a terminal\n", stderr);
        return -1;
    }
    if (isatty(STDIN_FILENO)) {
        in_fd = STDIN_FILENO;
    } else {
        in_fd = open("/dev/tty", O_RDONLY);
        if (in_fd < 0) {
            fputs("peatitle: cannot access the terminal for input\n", stderr);
            return -1;
        }
        own_in_fd = 1;
    }
    if (tcgetattr(in_fd, &saved_tio) < 0) {
        perror("peatitle: tcgetattr");
        return -1;
    }
    raw = saved_tio;
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL | INLCR);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(in_fd, TCSAFLUSH, &raw) < 0) {
        perror("peatitle: tcsetattr");
        return -1;
    }
    active = 1;
    atexit(term_restore);
    install_signals();
    term_write(SEQ_ENTER, strlen(SEQ_ENTER));
    return 0;
}

void term_restore(void)
{
    if (!active)
        return;
    active = 0;
    term_write(SEQ_LEAVE, strlen(SEQ_LEAVE));
    tcsetattr(in_fd, TCSAFLUSH, &saved_tio);
    if (own_in_fd)
        close(in_fd);
}

void term_size(int *rows, int *cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return;
    }
    *rows = 24;
    *cols = 80;
}
