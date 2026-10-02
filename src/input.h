/* input.h - reads stdin and decodes raw bytes into game key events.
 * Splits escape sequences that straddle read() boundaries. */
#pragma once

void input_poll(void);   /* non-blocking; delivers keys via game_on_key() */
