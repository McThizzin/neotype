#define _POSIX_C_SOURCE 200809L
#include "term.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

volatile sig_atomic_t quit_flag;

static struct termios orig;
static int raw_on;

/* ---------- output buffer ---------- */
static char *ob;
static size_t obn, obc;

size_t ob_len(void) { return obn; }
void ob_truncate(size_t n) { obn = n; }

void ob_put(const char *s, size_t n) {
    if (obn + n > obc) {
        obc = (obn + n) * 2 + 4096;
        ob = realloc(ob, obc);
        if (!ob) _exit(1);
    }
    memcpy(ob + obn, s, n);
    obn += n;
}

void ob_utf8(uint32_t c) {
    char t[4]; int n;
    if (c < 0x80)       { t[0] = (char)c; n = 1; }
    else if (c < 0x800) { t[0] = (char)(0xC0 | (c >> 6)); t[1] = (char)(0x80 | (c & 63)); n = 2; }
    else                { t[0] = (char)(0xE0 | (c >> 12)); t[1] = (char)(0x80 | ((c >> 6) & 63)); t[2] = (char)(0x80 | (c & 63)); n = 3; }
    ob_put(t, (size_t)n);
}

void flush_ob(void) {
    size_t off = 0;
    while (off < obn) {
        ssize_t n = write(1, ob + off, obn - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) { poll(NULL, 0, 1); continue; }
            break;
        }
        off += (size_t)n;
    }
    obn = 0;
}

void write_str(const char *s) {
    ssize_t r = write(1, s, strlen(s));
    (void)r;
}

/* ---------- terminal lifecycle ---------- */
void term_restore(void) {
    if (raw_on) {
        write_str("\x1b[0m\x1b[?25h\x1b[?7h\x1b[?2026l\x1b[?1049l");
        tcsetattr(0, TCSAFLUSH, &orig);
        raw_on = 0;
    }
}

static void on_sig(int s) { (void)s; quit_flag = 1; }

void term_init(void) {
    tcgetattr(0, &orig);
    struct termios t = orig;
    t.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    t.c_cflag |= CS8;
    t.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &t);
    raw_on = 1;
    atexit(term_restore);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sig;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    write_str("\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[2J");
}

void term_size(int *w, int *h) {
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *w = ws.ws_col;
        *h = ws.ws_row;
    } else {
        *w = 80;
        *h = 24;
    }
}
