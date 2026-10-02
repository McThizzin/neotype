#include "palette.h"

void hp_color(int hp, int *r, int *g, int *b) {
    switch (hp) {
    case 1:  *r = 125;  *g = 230; *b = 93; break;   /* green */
    case 2:  *r = 251; *g = 155; *b = 50;  break;   /* amber */
    default: *r = 211; *g = 9;  *b = 82;  break;   /* red   */
    }
}
