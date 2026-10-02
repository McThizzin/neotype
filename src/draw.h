/* draw.h - paints the current game state onto the canvas. Reads game state
 * through game(); never mutates it. */
#pragma once

void draw_frame(void);   /* clear + paint one whole frame */
