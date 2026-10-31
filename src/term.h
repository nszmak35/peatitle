#ifndef TERM_H
#define TERM_H

#include <signal.h>
#include <stddef.h>

extern volatile sig_atomic_t term_quit;
extern volatile sig_atomic_t term_resized;

int term_init(void);
void term_restore(void);
void term_size(int *rows, int *cols);
int term_write(const char *s, size_t n);
int term_input_fd(void);

#endif
