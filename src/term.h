/* term.h - output buffering, raw-mode terminal, signals, window size.
 * POSIX only; knows nothing about the game. */
#pragma once

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* buffered stdout */
size_t ob_len(void);            /* bytes currently staged */
void   ob_truncate(size_t n);   /* roll back staged bytes (present()'s no-op path) */
void ob_put(const char *s, size_t n);
void ob_utf8(uint32_t c);
void flush_ob(void);
void write_str(const char *s);

#define OBF(...) do { char t_[96]; int n_ = snprintf(t_, sizeof t_, __VA_ARGS__); ob_put(t_, (size_t)n_); } while (0)

/* set by SIGTERM/SIGHUP/SIGINT; polled by the main loop */
extern volatile sig_atomic_t quit_flag;

/* terminal lifecycle. term_init() arranges for term_restore() to run at exit,
 * so the terminal is never left in raw mode even on an abnormal return. */
void term_init(void);
void term_restore(void);
void term_size(int *w, int *h);   /* falls back to 80x24 on failure */
