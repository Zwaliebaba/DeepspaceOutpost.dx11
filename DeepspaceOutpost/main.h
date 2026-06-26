/*
 * Elite - The New Kind.
 *
 * Reverse engineered from the BBC disk version of Elite.
 * Additional material by C.J.Pinder.
 *
 * The original Elite code is (C) I.Bell & D.Braben 1984.
 * This version re-engineered in C by C.J.Pinder 1999-2001.
 *
 * email: <christian@newkind.co.uk>
 *
 *
 */

#ifndef MAIN_H
#define MAIN_H

void info_message (const char *message);
void save_commander_screen (void);
void load_commander_screen (void);
void update_screen (void);

/* Game entry point, invoked by the platform layer's WinMain. */
int game_main (void);


#endif
