#ifndef MAIN_H
#define MAIN_H

void info_message (const char *message);
void update_screen (void);

/* Per-frame hooks for the in-flight/docked loop, driven through the GameMain lifecycle
 * (GameApp::Update/RenderScene -> ClientEngine::Tick). game_update() advances the frame's
 * logic (network, sound, input, bookkeeping); game_render_scene() draws the scene + HUD.
 * Both are no-ops unless the main game loop is active (game_main gates them), so the
 * intro/game-over/mission blocking sequences still drive their own frames. */
void game_update (void);
void game_render_scene (void);

/* Game entry point, invoked by the platform layer's WinMain. */
int game_main (void);


#endif
