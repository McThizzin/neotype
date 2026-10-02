#define _POSIX_C_SOURCE 200809L
#include "input.h"

#include "game.h"
#include "term.h"

#include <string.h>
#include <unistd.h>

/* Escape sequences can be split across read() boundaries, so an incomplete
 * tail is carried over to the next call instead of being reinterpreted as
 * keystrokes. */
static unsigned char pend[16];
static size_t pend_n;

void input_poll(void) {
    unsigned char b[256];
    ssize_t n = read(0, b, sizeof b);
    if (n == 0) { quit_flag = 1; return; }
    if (n < 0) return;

    /* prepend anything carried over from last time */
    unsigned char buf[256 + sizeof pend];
    if (pend_n) {
        memcpy(buf, pend, pend_n);
        memcpy(buf + pend_n, b, (size_t)n);
        n += (ssize_t)pend_n;
        pend_n = 0;
    } else {
        memcpy(buf, b, (size_t)n);
    }

    for (ssize_t i = 0; i < n; i++) {
        int c = buf[i];

        if (c == 27) {                       /* ESC: possibly a sequence */
            if (i + 1 >= n) {                /* lone Esc at end of buffer */
                if (i == 0) { quit_flag = 1; return; }
                /* carry the bare Esc forward; decide next round */
                if (n - i < (ssize_t)sizeof pend) {
                    memcpy(pend, buf + i, (size_t)(n - i));
                    pend_n = (size_t)(n - i);
                } else {
                    quit_flag = 1; return;
                }
                break;
            }
            ssize_t j = i + 1;
            if (buf[j] == '[' || buf[j] == 'O') {
                j++;
                while (j < n && !(buf[j] >= 0x40 && buf[j] <= 0x7e)) j++;
                if (j >= n) {                /* sequence truncated mid-flight */
                    if (n - i < (ssize_t)sizeof pend) {
                        memcpy(pend, buf + i, (size_t)(n - i));
                        pend_n = (size_t)(n - i);
                    } else {
                        quit_flag = 1; return;
                    }
                    break;
                }
            }
            i = j;
            continue;
        }
        game_on_key(c);
    }
}
