/* game.h - simulation state and rules. Depends only on util.h: it never
 * touches the canvas or the terminal, so it can run headlessly. */
#pragma once

#include <stdint.h>

#include "util.h"   /* MAXD, MAXB, MAXFX */

typedef struct { int x; float y, speed, flash; char ch; int hp, maxhp, alive; } Drop;
typedef struct { int x0, y0, x1, y1; float ttl; } Beam;
typedef struct { float x, y, vx, vy, ttl; char ch; uint8_t r, g, b; } Fx;

/* kind: 0 miss, 1 hit, 2 kill */
typedef struct { uint8_t kind, r, g, b; float age; } Mark;

enum { S_TITLE, S_PLAY, S_PAUSE, S_OVER };

#define MAX_MARKS 512

/* Every field draw.c needs. Exposed as a plain struct (rather than ~12
 * accessor functions) because the renderer reads most of it every frame. */
typedef struct {
    Drop   drops[MAXD];
    Beam   beams[MAXB];
    Fx     fxs[MAXFX];
    Mark   marks[MAX_MARKS];
    int    tlen;          /* prompt length */
    int    fx_head;       /* ring-buffer write cursor for fxs */
    int    state;
    int    quit_requested;
    int    score, kills, level, combo, maxcombo, shots, hits;
    float  spawn_t, miss_t, over_t;
    double elapsed;
} Game;

Game *game(void);          /* the single simulation */

void game_reset(void);
void game_update(float dt);
void game_advance_over(float dt);   /* the game-over overlay's settle timer */
void game_shoot(char c);
void game_on_key(int c);   /* state machine: start / pause / retry / quit */

/* game owns the play field size, so the sim needs no canvas. main calls this
 * whenever the viewport changes. */
void game_set_viewport(int w, int h);

/* 'q' asks to quit; the game layer does not reach into term's signal flag. */
int  game_quit_requested(void);
void game_clear_quit(void);

void game_set_state(int s);
int  game_over_t_reached(float secs);   /* crash overlay has settled? */

int  game_drops_alive(void);
int  game_beams_live(void);
int  game_fx_live(void);
