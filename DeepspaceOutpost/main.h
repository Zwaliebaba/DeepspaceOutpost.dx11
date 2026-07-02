#ifndef MAIN_H
#define MAIN_H

void info_message (const char *message);
void update_screen (void);

/* Per-frame hooks driven through the GameMain lifecycle (GameApp::Update/RenderScene ->
 * ClientEngine::Frame). They step the top-level game state machine (intro -> flight ->
 * game-over -> new game): game_update() advances the active state's logic and transitions;
 * game_render_scene() draws the active state's scene. */
void game_update (void);
void game_render_scene (void);

/* Game entry point, invoked by the platform layer's WinMain. */
int game_main (void);


#endif
