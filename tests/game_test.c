/* game_test.c - menu state machine and the caps setting.
 *
 * Pure game.o + util.o: no canvas, no terminal, no audio. Digits drive the
 * menu, so this is where that contract is pinned.
 */
#include "game.h"

#include "util.h"

#include <stdio.h>

static int fails;
static void check(int ok, const char *what, const char *detail) {
    if (!ok) { printf("  FAIL: %s -- %s\n", what, detail); fails++; }
}
static char d[160];

/* A crash always lands with over_t back at 0, and game over swallows keys until
 * the overlay has settled. Reach that state the way the game does. */
static void crash(void) {
    game_reset();
    game_set_state(S_OVER);
}
static void settled(void) { game_advance_over(1.0f); }

static void expect(int got, int want, const char *what) {
    snprintf(d, sizeof d, "got %d, want %d", got, want);
    check(got == want, what, d);
}

/* ---------- menu state machine ---------- */

static void menu_keys(void) {
    game_set_state(S_MENU);
    game_reset();
    game()->sound = 1; game()->caps = 1;

    expect(game()->state, S_MENU, "starts on the menu");

    /* 1 starts a run, and the run starts clean */
    game_on_key('1');
    expect(game()->state, S_PLAY, "1 starts a run");
    expect(game()->level, 1, "a new run starts at level 1");

    /* 2 toggles sound, twice is back where it started */
    game_set_state(S_MENU);
    expect(game()->sound, 1, "sound on");
    game_on_key('2');
    expect(game()->sound, 0, "2 mutes");
    game_on_key('2');
    expect(game()->sound, 1, "2 again unmutes");

    /* 3 toggles caps */
    game_set_state(S_MENU);
    expect(game()->caps, 1, "caps on");
    game_on_key('3');
    expect(game()->caps, 0, "3 turns caps off");
    game_on_key('3');
    expect(game()->caps, 1, "3 again turns caps on");

    /* Enter is the keyboard-friendly alias for 1, and space for Enter. */
    for (int i = 0; i < 3; i++) {
        const char *what = i == 0 ? "enter starts" : i == 1 ? "newline starts" : "space starts";
        game_set_state(S_MENU);
        game_on_key(i == 0 ? '\r' : i == 1 ? '\n' : ' ');
        expect(game()->state, S_PLAY, what);
    }

    /* digits that are not menu items do nothing on the menu */
    game_set_state(S_MENU);
    for (int c = '4'; c <= '9'; c++) game_on_key(c);
    expect(game()->state, S_MENU, "4-9 are not menu items");
    expect(game()->sound, 1, "4-9 leave sound alone");
    expect(game()->caps, 1, "4-9 leave caps alone");

    /* letters do nothing on the menu (they are fire keys only in play) */
    for (int c = 'a'; c <= 'z'; c++) game_on_key(c);
    expect(game()->state, S_MENU, "letters are not menu items");
}

/* Settings are the player's, not the run's: they must survive a retry, a
 * fresh run, and a trip to the menu. */
static void settings_persist(void) {
    game_set_state(S_MENU);
    game_reset();
    game()->sound = 0; game()->caps = 0;

    game_on_key('1');
    expect(game()->state, S_PLAY, "started");
    expect(game()->sound + game()->caps, 0, "settings survive game_reset on start");

    game_on_key('\t');                       /* pause */
    game_on_key('\t');                       /* resume */
    expect(game()->sound + game()->caps, 0, "settings survive pause/resume");

    crash();
    settled();
    game_on_key('1');                        /* retry */
    expect(game()->state, S_PLAY, "retry starts a run");
    expect(game()->sound + game()->caps, 0, "settings survive a retry");

    crash();
    settled();
    game_on_key('2');                        /* back to the menu */
    expect(game()->state, S_MENU, "2 returns to the menu from game over");
    expect(game()->sound + game()->caps, 0, "settings survive the trip to the menu");

    /* and are still toggleable there */
    game_on_key('3');
    expect(game()->caps, 1, "caps toggles from the menu after a game over");
}

/* q used to quit from the menu and from game over. It must not any more. */
static void q_does_not_quit(void) {
    game_clear_quit();
    game_set_state(S_MENU);
    game_on_key('q');
    game_on_key('Q');
    expect(game_quit_requested(), 0, "q on the menu does not quit");

    crash();
    settled();
    game_on_key('q');
    game_on_key('Q');
    expect(game_quit_requested(), 0, "q on game over does not quit");

    /* Ctrl-C still does */
    game_on_key(3);
    expect(game_quit_requested(), 1, "Ctrl-C still quits");
    game_clear_quit();
}

/* Game over ignores keys until the crash has settled, then offers retry and
 * the menu. */
static void game_over_menu(void) {
    crash();
    int s0 = game()->state;
    game_on_key('1');
    game_on_key('2');
    expect(game()->state, s0, "keys ignored during the crash");

    settled();
    game_on_key('1');
    expect(game()->state, S_PLAY, "1 retries");

    /* Enter is the alias for 1 here too, and space for Enter. */
    for (int i = 0; i < 2; i++) {
        const char *what = i == 0 ? "enter retries" : "space retries";
        crash();
        settled();
        game_on_key(i == 0 ? '\r' : ' ');
        expect(game()->state, S_PLAY, what);
    }

    crash();
    settled();
    game_on_key('2');
    expect(game()->state, S_MENU, "2 goes to the menu");

    /* and back again: retry, crash, menu. The loop has no dead end. */
    game_on_key('1');
    crash();
    settled();
    game_on_key('2');
    expect(game()->state, S_MENU, "menu round-trips with a crash in between");
}

/* ---------- caps affects the rain, and only the rain ---------- */

/* Count uppercase letters among the live drops after some rain has fallen. */
static int upper_alive(void) {
    int n = 0;
    for (int i = 0; i < MAXD; i++)
        if (game()->drops[i].alive && game()->drops[i].ch >= 'A' && game()->drops[i].ch <= 'Z') n++;
    return n;
}

static int any_upper_seen(void) {
    for (int i = 0; i < MAXD; i++)
        if (game()->drops[i].ch >= 'A' && game()->drops[i].ch <= 'Z') return 1;
    return 0;
}

static void rain_case(int caps) {
    game_set_viewport(100, 30);
    game_set_state(S_MENU);
    game_reset();
    game()->caps = caps;
    rs = 12345;
    for (int i = 0; i < 8; i++) rnd32();

    int seen_upper = 0;
    for (int f = 0; f < 900; f++) {
        game_set_state(S_PLAY);
        game_update((float)FRAME);
        if (any_upper_seen()) seen_upper = 1;
        if (caps == 0 && upper_alive() > 0) {
            snprintf(d, sizeof d, "%d uppercase drops alive with caps off", upper_alive());
            check(0, "caps off means no uppercase in the rain", d);
            return;
        }
    }
    if (caps) check(seen_upper, "caps on puts uppercase in the rain", "none appeared in 900 frames");
    else      check(!seen_upper, "caps off keeps the rain lowercase", "an uppercase appeared");
}

/* The caps setting must not shift the RNG draw order, or toggling it would
 * silently change every future run. Same seed, same ending RNG state. */
static void caps_does_not_disturb_rng(void) {
    uint64_t end[2];
    for (int i = 0; i < 2; i++) {
        game_set_viewport(100, 30);
        game_set_state(S_MENU);
        game_reset();
        game()->caps = i;
        rs = 999;
        for (int k = 0; k < 8; k++) rnd32();
        for (int f = 0; f < 1200; f++) {
            game_set_state(S_PLAY);
            game_update((float)FRAME);
        }
        end[i] = rs;
    }
    snprintf(d, sizeof d, "rng %016llx vs %016llx",
             (unsigned long long)end[0], (unsigned long long)end[1]);
    check(end[0] == end[1], "caps does not perturb the RNG stream", d);
}

int main(void) {
    menu_keys();
    settings_persist();
    q_does_not_quit();
    game_over_menu();
    rain_case(1);
    rain_case(0);
    caps_does_not_disturb_rng();

    if (fails) { printf("game: FAILED (%d)\n", fails); return 1; }
    printf("game: ok\n");
    return 0;
}