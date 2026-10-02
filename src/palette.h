/* palette.h - the game's colour language. Shared by game (which needs the
 * letter health colours to tint fx) and draw (which paints everything else).
 * Keeping it here stops game from having to include draw.h. */
#pragma once

/* letter health: green = 1 hit, amber = 2 hits, red = 3 hits */
void hp_color(int hp, int *r, int *g, int *b);

/* ui accents */
#define C_ACCENT_R 70
#define C_ACCENT_G 255
#define C_ACCENT_B 110

#define C_TEXT_R   200
#define C_TEXT_G   200
#define C_TEXT_B   200

#define C_DIM_R    120
#define C_DIM_G    120
#define C_DIM_B    120

#define C_MUTED_R  110
#define C_MUTED_G  110
#define C_MUTED_B  110

#define C_BAD_R    255
#define C_BAD_G    70
#define C_BAD_B    70

#define C_WARN_R   140
#define C_WARN_G   40
#define C_WARN_B   40

#define C_PAUSE_R  255
#define C_PAUSE_G  200
#define C_PAUSE_B  40

#define C_BRIGHT_R 230
#define C_BRIGHT_G 230
#define C_BRIGHT_B 230

#define C_LABEL_R  160
#define C_LABEL_G  160
#define C_LABEL_B  160
