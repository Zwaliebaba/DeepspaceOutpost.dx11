#ifndef MAIN_H
#define MAIN_H

void info_message (const char *message);
void save_commander_screen (void);
void load_commander_screen (void);
void update_screen (void);

/* Game entry point, invoked by the platform layer's WinMain. */
int game_main (void);


#endif
