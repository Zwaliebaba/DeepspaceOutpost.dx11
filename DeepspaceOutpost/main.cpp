/*
 * main.cpp
 *
 * Platform-independent main game handler.
 * Derived from the Allegro alg_main.c; Allegro-specific glue (timer install,
 * readkey, END_OF_MAIN, process entry point) is handled by the platform layer.
 * The portable game loop and screen handlers live here.
 * The platform layer calls game_main() from its WinMain.
 */

#include "pch.h"

#include "config.h"
#include "gfx.h"
#include "GameUniverse.h"
#include "GameComponents.h"
#include "main.h"
#include "vector.h"
#include "elite.h"
#include "docked.h"
#include "intro.h"
#include "shipdata.h"
#include "space.h"
#include "sound.h"
#include "swat.h"
#include "random.h"
#include "stars.h"
#include "missions.h"
#include "pilot.h"
#include "file.h"
#include "keyboard.h"
#include "Camera.h"
#include "ReplicationClient.h"
#include "GuiOverlay.h"
#include "GameWindows.h"

int old_cross_x, old_cross_y;
int cross_timer;

int draw_lasers;
int mcount;
int message_count;
char message_string[80];
int rolling;
int climbing;
int game_paused;

int find_input;
char find_name[20];

/*
 * Flight-control responsiveness.
 *
 * PlayerFlight().roll / PlayerFlight().climb hold the ship's current turn rate (consumed by
 * move_local_object as alpha/beta). While a control key is held the rate ramps
 * toward full deflection; when released it auto-centres back to zero. These
 * steps set how many rate units we add/remove per frame - i.e. how snappily
 * the ship reacts - without changing the top turn rate (PlayerCaps().maxRoll /
 * max_climb), so handling and turn radius stay balanced. Higher = snappier.
 * This is frame-rate independent of game speed (speed_cap): it changes how
 * many frames the ramp takes, not how fast the game runs.
 */
#define ROLL_RAMP_STEP     4		/* was 2: ramp roll to full in ~8 frames not ~16 */
#define CLIMB_RAMP_STEP    2		/* was 1: ramp climb to full in ~4 frames not 8  */
#define ROLL_CENTRE_STEP   3		/* was 1: recentre roll ~3x faster on release    */
#define CLIMB_CENTRE_STEP  2		/* was 1: recentre climb ~2x faster on release   */

/*
 * Nudge the current turn rate toward full deflection by |steps| units this
 * frame (steps > 0 rolls/climbs one way, < 0 the other). Reuses the single-
 * step primitives so the per-ship max_roll / max_climb clamp still applies.
 */
static void ramp_flight_roll(int steps)
{
  int i;

  for (i = 0; i < steps; i++)
    increase_flight_roll();
  for (i = 0; i > steps; i--)
    decrease_flight_roll();
}

static void ramp_flight_climb(int steps)
{
  int i;

  for (i = 0; i < steps; i++)
    increase_flight_climb();
  for (i = 0; i > steps; i--)
    decrease_flight_climb();
}

/*
 * Auto-centre the turn rate back toward zero when no key is held, moving up to
 * CENTRE_STEP units per frame but never overshooting past zero into a reversal.
 */
static void centre_flight_roll(void)
{
  int i;

  for (i = 0; i < ROLL_CENTRE_STEP && PlayerFlight().roll != 0; i++)
  {
    if (PlayerFlight().roll > 0)
      decrease_flight_roll();
    else
      increase_flight_roll();
  }
}

static void centre_flight_climb(void)
{
  int i;

  for (i = 0; i < CLIMB_CENTRE_STEP && PlayerFlight().climb != 0; i++)
  {
    if (PlayerFlight().climb > 0)
      decrease_flight_climb();
    else
      increase_flight_climb();
  }
}

/*
 * Initialise the game parameters.
 */

void initialise_game(void)
{
  set_rand_seed(time(nullptr));
  current_screen = SCR_INTRO_ONE;

  /*
   * A2 flip: stand up the de-globalised world and the player's ship entity.
   * Seeded here but not yet read - legacy globals (myship, flight state,
   * local_objects[]) still drive the game and migrate onto this world cluster
   * by cluster. Created before anything else so the player entity always exists.
   */
  GameUniverse().Reset();
  {
    ECS::EntityId player = GameUniverse().Reg().Create();
    GameUniverse().Reg().Add<Game::PlayerTag>(player, Game::PlayerTag{});
    GameUniverse().Reg().Add<Game::Transform>(player, Game::Transform{});
    GameUniverse().Reg().Add<Game::ShipCaps>(player, Game::ShipCaps{});
    GameUniverse().Reg().Add<Game::FlightRates>(player, Game::FlightRates{});
    GameUniverse().Reg().Add<Game::Defense>(player, Game::Defense{});
    GameUniverse().SetPlayer(player);
  }

  /* Create the ECS slot entities that back local_objects[] (must exist before
     clear_local_objects / any local_objects[i] access below). */
  create_local_object_slots();

  restore_saved_commander();

  PlayerFlight().speed = 1;
  PlayerFlight().roll = 0;
  PlayerFlight().climb = 0;
  docked = 1;
  PlayerDefense().frontShield = 255;
  PlayerDefense().aftShield = 255;
  PlayerDefense().energy = 255;
  draw_lasers = 0;
  mcount = 0;
  hyper_ready = 0;
  detonate_bomb = 0;
  find_input = 0;
  witchspace = 0;
  game_paused = 0;
  auto_pilot = 0;

  create_new_stars();
  clear_local_objects();

  cross_x = -1;
  cross_y = -1;
  cross_timer = 0;

  PlayerCaps().maxSpeed = 40; /* 0.27 Light Mach */
  PlayerCaps().maxRoll = 31;
  PlayerCaps().maxClimb = 8; /* CF 8 */
  PlayerCaps().maxFuel = 70; /* 7.0 Light Years */
}

/*
 * Move the planet chart cross hairs to specified position.
 */

void move_cross(int dx, int dy)
{
  cross_timer = 5;

  if (current_screen == SCR_SHORT_RANGE)
  {
    cross_x += (dx * 4);
    cross_y += (dy * 4);
    return;
  }

  if (current_screen == SCR_GALACTIC_CHART)
  {
    cross_x += (dx * 2);
    cross_y += (dy * 2);

    if (cross_x < 1)
      cross_x = 1;

    if (cross_x > 510)
      cross_x = 510;

    if (cross_y < 37)
      cross_y = 37;

    if (cross_y > 293)
      cross_y = 293;
  }
}

/*
 * Draw the cross hairs at the specified position.
 */

// Draw the chart crosshair as a textured sprite (Textures/Crosshair.dds), centred on
// (cx,cy) and clipped to the chart area. The chart is redrawn every frame (see
// game_render_flight), so the crosshair is just drawn fresh on top each frame - no XOR
// erase (the old logic-op path was dropped in the Render2D move). The half-size matches
// the old cross reach: 16 px on the short-range chart, 8 px on the galactic chart.
void draw_cross(int cx, int cy)
{
  int half;
  int clipBottom;
  if (current_screen == SCR_SHORT_RANGE)
  {
    half = 16;
    clipBottom = 339;
  }
  else if (current_screen == SCR_GALACTIC_CHART)
  {
    half = 8;
    clipBottom = 293;
  }
  else
  {
    return;
  }

  gfx_set_clip_region(1, 37, 510, clipBottom);
  gfx_draw_sprite_scaled(IMG_CROSSHAIR, cx - half, cy - half, half * 2, half * 2);
  gfx_set_clip_region(1, 1, 510, 383);
}

void draw_laser_sights(void)
{
  int laser = 0;
  int x1, y1, x2, y2;

  switch (current_screen)
  {
  case SCR_FRONT_VIEW:
    gfx_display_centre_text(32, "Front View", 120, GFX_COL_WHITE);
    laser = cmdr.front_laser;
    break;

  case SCR_REAR_VIEW:
    gfx_display_centre_text(32, "Rear View", 120, GFX_COL_WHITE);
    laser = cmdr.rear_laser;
    break;

  case SCR_LEFT_VIEW:
    gfx_display_centre_text(32, "Left View", 120, GFX_COL_WHITE);
    laser = cmdr.left_laser;
    break;

  case SCR_RIGHT_VIEW:
    gfx_display_centre_text(32, "Right View", 120, GFX_COL_WHITE);
    laser = cmdr.right_laser;
    break;
  }

  if (laser)
  {
    // Centre the cross-hairs on the live view (window middle in full-window
    // flight, 256,192 in retro) with arm lengths that scale with the optics.
    const Client::ViewMetrics& vm = gfx_view_metrics();
    const int cx = static_cast<int>(vm.cx);
    const int cy = static_cast<int>(vm.cy);
    const double s = vm.focal / 256.0; // retro == GFX_SCALE (2)
    const int in8 = static_cast<int>(8 * s);
    const int in16 = static_cast<int>(16 * s);

    x1 = cx;
    y1 = cy - in8;
    y2 = cy - in16;

    gfx_draw_colour_line(x1 - 1, y1, x1 - 1, y2, GFX_COL_GREY_1);
    gfx_draw_colour_line(x1, y1, x1, y2, GFX_COL_WHITE);
    gfx_draw_colour_line(x1 + 1, y1, x1 + 1, y2, GFX_COL_GREY_1);

    y1 = cy + in8;
    y2 = cy + in16;

    gfx_draw_colour_line(x1 - 1, y1, x1 - 1, y2, GFX_COL_GREY_1);
    gfx_draw_colour_line(x1, y1, x1, y2, GFX_COL_WHITE);
    gfx_draw_colour_line(x1 + 1, y1, x1 + 1, y2, GFX_COL_GREY_1);

    x1 = cx - in8;
    y1 = cy;
    x2 = cx - in16;

    gfx_draw_colour_line(x1, y1 - 1, x2, y1 - 1, GFX_COL_GREY_1);
    gfx_draw_colour_line(x1, y1, x2, y1, GFX_COL_WHITE);
    gfx_draw_colour_line(x1, y1 + 1, x2, y1 + 1, GFX_COL_GREY_1);

    x1 = cx + in8;
    x2 = cx + in16;

    gfx_draw_colour_line(x1, y1 - 1, x2, y1 - 1, GFX_COL_GREY_1);
    gfx_draw_colour_line(x1, y1, x2, y1, GFX_COL_WHITE);
    gfx_draw_colour_line(x1, y1 + 1, x2, y1 + 1, GFX_COL_GREY_1);
  }
}

void arrow_right(void)
{
  switch (current_screen)
  {
  case SCR_SHORT_RANGE:
  case SCR_GALACTIC_CHART:
    move_cross(1, 0);
    break;

  case SCR_FRONT_VIEW:
  case SCR_REAR_VIEW:
  case SCR_RIGHT_VIEW:
  case SCR_LEFT_VIEW:
    if (PlayerFlight().roll > 0)
      PlayerFlight().roll = 0;
    else
    {
      ramp_flight_roll(-ROLL_RAMP_STEP);
      rolling = 1;
    }
    break;
  }
}

void arrow_left(void)
{
  switch (current_screen)
  {
  case SCR_SHORT_RANGE:
  case SCR_GALACTIC_CHART:
    move_cross(-1, 0);
    break;

  case SCR_FRONT_VIEW:
  case SCR_REAR_VIEW:
  case SCR_RIGHT_VIEW:
  case SCR_LEFT_VIEW:
    if (PlayerFlight().roll < 0)
      PlayerFlight().roll = 0;
    else
    {
      ramp_flight_roll(ROLL_RAMP_STEP);
      rolling = 1;
    }
    break;
  }
}

void arrow_up(void)
{
  switch (current_screen)
  {
  case SCR_SHORT_RANGE:
  case SCR_GALACTIC_CHART:
    move_cross(0, -1);
    break;

  case SCR_FRONT_VIEW:
  case SCR_REAR_VIEW:
  case SCR_RIGHT_VIEW:
  case SCR_LEFT_VIEW:
    if (PlayerFlight().climb > 0)
      PlayerFlight().climb = 0;
    else
      ramp_flight_climb(-CLIMB_RAMP_STEP);
    climbing = 1;
    break;
  }
}

void arrow_down(void)
{
  switch (current_screen)
  {
  case SCR_SHORT_RANGE:
  case SCR_GALACTIC_CHART:
    move_cross(0, 1);
    break;

  case SCR_FRONT_VIEW:
  case SCR_REAR_VIEW:
  case SCR_RIGHT_VIEW:
  case SCR_LEFT_VIEW:
    if (PlayerFlight().climb < 0)
      PlayerFlight().climb = 0;
    else
      ramp_flight_climb(CLIMB_RAMP_STEP);
    climbing = 1;
    break;
  }
}

void d_pressed(void)
{
  switch (current_screen)
  {
  case SCR_GALACTIC_CHART:
  case SCR_SHORT_RANGE:
    show_distance_to_planet();
    break;

  case SCR_FRONT_VIEW:
  case SCR_REAR_VIEW:
  case SCR_RIGHT_VIEW:
  case SCR_LEFT_VIEW:
    if (auto_pilot)
      disengage_auto_pilot();
    break;
  }
}

void f_pressed(void)
{
  if ((current_screen == SCR_GALACTIC_CHART) || (current_screen == SCR_SHORT_RANGE))
  {
    find_input = 1;
    *find_name = '\0';
    gfx_clear_text_area();
    gfx_display_text(16, 340, "Planet Name?");
  }
}

void add_find_char(int letter)
{
  char str[40];

  if (strlen(find_name) == 16)
    return;

  str[0] = toupper(letter);
  str[1] = '\0';
  strcat(find_name, str);

  sprintf(str, "Planet Name? %s", find_name);
  gfx_clear_text_area();
  gfx_display_text(16, 340, str);
}

void delete_find_char(void)
{
  char str[40];

  size_t len = strlen(find_name);
  if (len == 0)
    return;

  find_name[len - 1] = '\0';

  sprintf(str, "Planet Name? %s", find_name);
  gfx_clear_text_area();
  gfx_display_text(16, 340, str);
}

void o_pressed()
{
  switch (current_screen)
  {
  case SCR_GALACTIC_CHART:
  case SCR_SHORT_RANGE:
    move_cursor_to_origin();
    break;
  }
}

void auto_dock(void)
{
  struct local_object ship;

  ship.location.x = 0;
  ship.location.y = 0;
  ship.location.z = 0;

  set_init_matrix(ship.rotmat);
  ship.rotmat[2].z = 1;
  ship.rotmat[0].x = -1;
  ship.type = -96;
  ship.velocity = PlayerFlight().speed;
  ship.acceleration = 0;
  ship.bravery = 0;
  ship.rotz = 0;
  ship.rotx = 0;

  auto_pilot_ship(&ship);

  if (ship.velocity > 22)
    PlayerFlight().speed = 22;
  else
    PlayerFlight().speed = ship.velocity;

  if (ship.acceleration > 0)
  {
    PlayerFlight().speed++;
    if (PlayerFlight().speed > 22)
      PlayerFlight().speed = 22;
  }

  if (ship.acceleration < 0)
  {
    PlayerFlight().speed--;
    if (PlayerFlight().speed < 1)
      PlayerFlight().speed = 1;
  }

  if (ship.rotx == 0)
    PlayerFlight().climb = 0;

  if (ship.rotx < 0)
  {
    increase_flight_climb();

    if (ship.rotx < -1)
      increase_flight_climb();
  }

  if (ship.rotx > 0)
  {
    decrease_flight_climb();

    if (ship.rotx > 1)
      decrease_flight_climb();
  }

  if (ship.rotz == 127)
    PlayerFlight().roll = -14;
  else
  {
    if (ship.rotz == 0)
      PlayerFlight().roll = 0;

    if (ship.rotz > 0)
    {
      increase_flight_roll();

      if (ship.rotz > 1)
        increase_flight_roll();
    }

    if (ship.rotz < 0)
    {
      decrease_flight_roll();

      if (ship.rotz < -1)
        decrease_flight_roll();
    }
  }
}

void run_escape_sequence(void)
{
  int i;
  int newship;
  Matrix rotmat;

  current_screen = SCR_ESCAPE_POD;

  PlayerFlight().speed = 1;
  PlayerFlight().roll = 0;
  PlayerFlight().climb = 0;

  set_init_matrix(rotmat);
  rotmat[2].z = 1.0;

  newship = add_new_ship(SHIP_COBRA3, 0, 0, 200, rotmat, -127, -127);
  local_objects[newship].velocity = 7;
  snd_play_sample(SND_LAUNCH);

  for (i = 0; i < 90; i++)
  {
    if (i == 40)
    {
      local_objects[newship].flags |= FLG_DEAD;
      snd_play_sample(SND_EXPLODE);
    }

    gfx_set_clip_region(1, 1, 510, 383);
    gfx_clear_display();
    update_starfield();
    update_local_objects();

    local_objects[newship].location.x = 0;
    local_objects[newship].location.y = 0;
    local_objects[newship].location.z += 2;

    gfx_display_centre_text(358, "Escape pod launched - Ship auto-destuct initiated.", 120, GFX_COL_WHITE);

    update_console();
    gfx_update_screen();
  }

  while ((ship_count[SHIP_CORIOLIS] == 0) && (ship_count[SHIP_DODEC] == 0))
  {
    auto_dock();

    if ((abs(PlayerFlight().roll) < 3) && (abs(PlayerFlight().climb) < 3))
    {
      for (i = 0; i < MAX_LOCAL_OBJECTS; i++)
      {
        if (local_objects[i].type != 0)
          local_objects[i].location.z -= 1500;
      }
    }

    warp_stars = 1;
    gfx_set_clip_region(1, 1, 510, 383);
    gfx_clear_display();
    update_starfield();
    update_local_objects();
    update_console();
    gfx_update_screen();
  }

  abandon_ship();
}

// Pending fire-missile intent + the locked target it launches at, for the next
// input packet (thin-client mode). Set by launch_missile(), consumed and cleared by
// send_player_input().
static bool s_fire_missile_intent = false;
static unsigned int s_fire_missile_target = 0xFFFFFFFFu;

// The entity the missile is currently locked onto (picked from the replicated view
// with the target key), or 0xFFFFFFFF for none. Drives the HUD lock indicator and
// the on-target reticle (drawn in render_replicated_objects), and is the target M
// launches at. Global so the renderer can read it.
unsigned int g_missile_lock_target = 0xFFFFFFFFu;

// Lock the missile onto the ship in the crosshairs (T key). Thin client: the server
// is authoritative, so we pick the target from the replicated view and remember its
// id; M then launches a homing missile at exactly that target. With nothing in the
// sights, nothing locks. Falls back to the legacy arm when not replicated.
static void lock_missile_target(void)
{
  if (!Client::ReplicationClientInstance().IsOpen())
  {
    arm_missile();
    return;
  }

  if (cmdr.missiles == 0)
    return;

  const unsigned int tgt = find_lock_target();
  if (tgt == 0xFFFFFFFFu)
    return; // nothing in the sights to lock

  g_missile_lock_target = tgt;
  missile_target = 0; // HUD: a missile is locked (red indicator)
}

// Clear the missile lock (U key).
static void unlock_missile_target(void)
{
  if (!Client::ReplicationClientInstance().IsOpen())
  {
    unarm_missile();
    return;
  }

  g_missile_lock_target = 0xFFFFFFFFu;
  missile_target = MISSILE_UNARMED; // HUD: no lock
  snd_play_sample(SND_BOOP);
}

// Launch a missile (M key). Thin client: only when a target is locked (T) and a
// round is in the rack; the server spawns a projectile that homes that specific
// locked target. No lock -> nothing fires. Falls back to the legacy local spawn
// when not replicated.
static void launch_missile(void)
{
  if (!Client::ReplicationClientInstance().IsOpen())
  {
    fire_missile();
    return;
  }

  if ((g_missile_lock_target == 0xFFFFFFFFu) || (cmdr.missiles == 0))
    return;

  s_fire_missile_intent = true;
  s_fire_missile_target = g_missile_lock_target;
  cmdr.missiles--;
  g_missile_lock_target = 0xFFFFFFFFu;
  missile_target = MISSILE_UNARMED; // lock consumed
  snd_play_sample(SND_MISSILE);
}

void handle_flight_keys(void)
{
  int keyasc;

  kbd_poll_keyboard();

  if (game_paused)
  {
    if (kbd_resume_pressed)
      game_paused = 0;
    return;
  }

  if (kbd_F1_pressed)
  {
    find_input = 0;

    if (docked)
      launch_player();
    else
    {
      if (current_screen != SCR_FRONT_VIEW)
      {
        current_screen = SCR_FRONT_VIEW;
        flip_stars();
      }
    }
  }

  if (kbd_F2_pressed)
  {
    find_input = 0;

    if (!docked)
    {
      if (current_screen != SCR_REAR_VIEW)
      {
        current_screen = SCR_REAR_VIEW;
        flip_stars();
      }
    }
  }

  if (kbd_F3_pressed)
  {
    find_input = 0;

    if (!docked)
    {
      if (current_screen != SCR_LEFT_VIEW)
      {
        current_screen = SCR_LEFT_VIEW;
        flip_stars();
      }
    }
  }

  if (kbd_F4_pressed)
  {
    find_input = 0;

    if (docked)
      OpenEquipWindow();
    else
    {
      if (current_screen != SCR_RIGHT_VIEW)
      {
        current_screen = SCR_RIGHT_VIEW;
        flip_stars();
      }
    }
  }

  if (kbd_F5_pressed)
  {
    find_input = 0;
    old_cross_x = -1;
    display_galactic_chart();
  }

  if (kbd_F6_pressed)
  {
    find_input = 0;
    old_cross_x = -1;
    display_short_range_chart();
  }

  if (kbd_F7_pressed)
  {
    find_input = 0;
    // Single-player: the GUI planet-data window (local generated data). In thin-client
    // (MMO) mode the legacy screen shows server-replicated system data + chart cursor
    // selection, so keep it there.
    if (Neuron::Client::ReplicationClientInstance().IsOpen())
      display_data_on_planet();
    else
      OpenPlanetDataWindow();
  }

  if (kbd_F8_pressed && (!witchspace))
  {
    // Route the in-game market entry to the GUI overlay (Buy/Sell per row, live cash),
    // replacing the legacy gfx_display_* display_market_prices screen.
    find_input = 0;
    OpenMarketWindow();
  }

  if (kbd_F9_pressed)
  {
    find_input = 0;
    OpenCommanderWindow();
  }

  if (kbd_F10_pressed)
  {
    find_input = 0;
    OpenInventoryWindow();
  }

  if (kbd_F11_pressed)
  {
    // Route the in-game options entry to the GUI overlay (Options menu -> Game
    // Settings / Quit), replacing the legacy gfx_display_* display_options() screen.
    // The overlay floats over the running game and suppresses game input while open.
    find_input = 0;
    GuiOverlay::Open();
  }

  // F12 toggles cockpit <-> chase camera (the ship/camera-seam payoff). Edge-
  // triggered so holding the key flips the view exactly once.
  static int f12_was_down = 0;
  if (kbd_F12_pressed)
  {
    if (!f12_was_down)
    {
      Neuron::Client::SetCameraMode(Client::GetCameraMode() == Client::CameraMode::Cockpit
                                      ? Client::CameraMode::Chase
                                      : Client::CameraMode::Cockpit);
    }
    f12_was_down = 1;
  }
  else
    f12_was_down = 0;

  if (find_input)
  {
    keyasc = kbd_read_key();

    if (kbd_enter_pressed)
    {
      find_input = 0;
      find_planet_by_name(find_name);
      return;
    }

    if (kbd_backspace_pressed)
    {
      delete_find_char();
      return;
    }

    if (isalpha(keyasc))
      add_find_char(keyasc);

    return;
  }

  if (kbd_fire_pressed)
  {
    if ((!docked) && (draw_lasers == 0))
      draw_lasers = fire_laser();
  }

  if (kbd_dock_pressed)
  {
    if (!docked && cmdr.docking_computer)
    {
      if (instant_dock)
        engage_docking_computer();
      else
        engage_auto_pilot();
    }
  }

  if (kbd_d_pressed)
    d_pressed();

  if (kbd_ecm_pressed)
  {
    if (!docked && cmdr.ecm)
      activate_ecm(1);
  }

  if (kbd_find_pressed)
    f_pressed();

  if (kbd_hyperspace_pressed && (!docked))
  {
    if (kbd_ctrl_pressed)
      start_galactic_hyperspace();
    else
      start_hyperspace();
  }

  // Docked at a station's "teleport building": the hyperspace key on either chart
  // jumps to the system under the crosshair (server-validated). Thin-client only;
  // teleport_to_cursor() is a no-op without a replicated galaxy.
  if (kbd_hyperspace_pressed && docked && ((current_screen == SCR_GALACTIC_CHART) || (current_screen == SCR_SHORT_RANGE)))
    teleport_to_cursor();

  if (kbd_jump_pressed && (!docked) && (!witchspace))
    jump_warp();

  if (kbd_fire_missile_pressed)
  {
    if (!docked)
      launch_missile();
  }

  if (kbd_origin_pressed)
    o_pressed();

  if (kbd_pause_pressed)
    game_paused = 1;

  if (kbd_target_missile_pressed)
  {
    if (!docked)
      lock_missile_target();
  }

  if (kbd_unarm_missile_pressed)
  {
    if (!docked)
      unlock_missile_target();
  }

  if (kbd_inc_speed_pressed)
  {
    if (!docked)
    {
      if (PlayerFlight().speed < PlayerCaps().maxSpeed)
        PlayerFlight().speed++;
    }
  }

  if (kbd_dec_speed_pressed)
  {
    if (!docked)
    {
      if (PlayerFlight().speed > 1)
        PlayerFlight().speed--;
    }
  }

  if (kbd_up_pressed)
    arrow_up();

  if (kbd_down_pressed)
    arrow_down();

  if (kbd_left_pressed)
    arrow_left();

  if (kbd_right_pressed)
    arrow_right();

  if (kbd_energy_bomb_pressed)
  {
    if ((!docked) && (cmdr.energy_bomb))
    {
      detonate_bomb = 1;
      cmdr.energy_bomb = 0;
    }
  }

  if (kbd_escape_pressed)
  {
    if ((!docked) && (cmdr.escape_pod) && (!witchspace))
      run_escape_sequence();
  }
}

void set_commander_name(char* path)
{
  char *fname, *cname;
  int i;

  fname = get_filename(path);
  cname = cmdr.name;

  for (i = 0; i < 31; i++)
  {
    if (!isalnum(*fname))
      break;

    *cname++ = toupper(*fname++);
  }

  *cname = '\0';
}

// ---- Top-level game flow: the GameMain lifecycle state machine ----------------------
//
// The classic intro -> flight -> game-over sequence used to be a stack of blocking
// for(;;) loops inside game_main(). It now runs as an explicit state machine stepped one
// frame at a time by the engine: each gfx_update_screen() runs ClientEngine::Frame() ->
// GameApp::Update/RenderScene -> game_update()/game_render_scene(), which dispatch on
// s_state. Each enter_*() does a state's one-time setup; the per-frame work and the
// transition tests live in game_update()/game_render_scene().
//
// The deeply nested blocking sequences the game still spins up from inside a frame (the
// docking break pattern, mission briefs) keep working through the re-entrancy guard in
// ClientEngine::Frame: those nested gfx_update_screen() calls only present, they do not
// re-enter the state step.

enum class GameState
{
  Intro1,   // "DEEPSPACE OUTPOST" title (Elite theme)
  Intro2,   // ship parade (Blue Danube)
  Flight,   // in-flight / docked - the live game
  GameOver, // the death animation, then a fresh game
};

static GameState s_state = GameState::Intro1;
static int s_gameOverFrame = 0;   // game-over frames rendered (the animation runs 100)

// Enter the first intro screen (title + Elite theme).
static void enter_intro1(void)
{
  current_screen = SCR_INTRO_ONE;
  snd_play_midi(SND_ELITE_THEME, TRUE);
  initialise_intro1();
  s_state = GameState::Intro1;
}

// Enter the second intro screen (ship parade + Blue Danube).
static void enter_intro2(void)
{
  current_screen = SCR_INTRO_TWO;
  snd_play_midi(SND_BLUE_DANUBE, TRUE);
  initialise_intro2();
  PlayerFlight().speed = 3;
  PlayerFlight().roll = 0;
  PlayerFlight().climb = 0;
  s_state = GameState::Intro2;
}

// Enter live flight (and the docked menus); start on the commander status screen.
static void enter_flight(void)
{
  old_cross_x = -1;
  old_cross_y = -1;
  dock_player();
  display_commander_status();
  s_state = GameState::Flight;
}

// Enter the game-over animation: a dead Cobra tumbling through wreckage for 100 frames.
static void enter_game_over(void)
{
  current_screen = SCR_GAME_OVER;
  gfx_set_clip_region(1, 1, 510, 383);

  PlayerFlight().speed = 6;
  PlayerFlight().roll = 0;
  PlayerFlight().climb = 0;
  clear_local_objects();

  Matrix rotmat;
  set_init_matrix(rotmat);

  int newship = add_new_ship(SHIP_COBRA3, 0, 0, -400, rotmat, 0, 0);
  local_objects[newship].flags |= FLG_DEAD;

  for (int i = 0; i < 5; i++)
  {
    const int type = (rand255() & 1) ? SHIP_CARGO : SHIP_ALLOY;
    newship = add_new_ship(type, (rand255() & 63) - 32, (rand255() & 63) - 32, -400, rotmat, 0, 0);
    local_objects[newship].rotz = ((rand255() * 2) & 255) - 128;
    local_objects[newship].rotx = ((rand255() * 2) & 255) - 128;
    local_objects[newship].velocity = rand255() & 15;
  }

  s_gameOverFrame = 0;
  s_state = GameState::GameOver;
}

// Begin a fresh game: reset the world, dock, then roll into the intro sequence.
static void start_new_game(void)
{
  game_over = 0;
  initialise_game();
  dock_player();
  update_console();
  current_screen = SCR_FRONT_VIEW;
  enter_intro1();
}

// After the game-over animation. In thin-client mode the server has already respawned us
// in place (it keeps no permadeath yet), so clear the death and drop straight back into
// flight - the replicated snapshots drive the view again. The degraded single-player
// fallback has no server to respawn us, so it starts a fresh game (intro).
static void respawn_after_death(void)
{
  if (Client::ReplicationClientInstance().IsOpen())
  {
    game_over = 0;
    docked = 0;
    PlayerFlight().speed = 0;
    PlayerFlight().roll = 0;
    PlayerFlight().climb = 0;
    old_cross_x = -1;
    old_cross_y = -1;
    current_screen = SCR_FRONT_VIEW;
    s_state = GameState::Flight;
  }
  else
  {
    start_new_game();
  }
}

/*
 * Draw a break pattern (for launching, docking and hyperspacing).
 * Just draw a very simple one for the moment.
 */

void display_break_pattern(void)
{
  int i;

  gfx_set_clip_region(1, 1, 510, 383);
  gfx_clear_display();

  for (i = 0; i < 20; i++)
  {
    gfx_draw_circle(256, 192, 30 + i * 15, GFX_COL_WHITE);
    gfx_update_screen();
  }

  if (docked)
  {
    check_mission_brief();
    display_commander_status();
    update_console();
  }
  else
    current_screen = SCR_FRONT_VIEW;
}

void info_message(const char* message)
{
  strcpy(message_string, message);
  message_count = 37;
  //	snd_play_sample (SND_BEEP);
}

/*
 * Game entry point. Called by the platform layer (WinMain) once the
 * window, Direct3D 11 device and XAudio2 engine have been created.
 */

// Drain authoritative server events (thin-client mode): station buy/sell/dock
// results, plus entity despawns and deaths. Removing the entity on despawn/death
// is what stops destroyed things (a detonated missile, a killed ship) from
// lingering as motionless ghosts; a death also plays the explosion sound.
static void process_server_events(void)
{
  Client::ReplicationClient& rc = Client::ReplicationClientInstance();
  Net::ReliableMessage msg;
  while (rc.PollEvent(msg))
  {
    Net::StationResponse resp;
    uint32_t entityId = 0;
    uint32_t killerId = 0;

    if (Neuron::Net::DecodeStationResponse(msg, resp))
    {
      if (resp.status == Net::StationStatus::Ok)
      {
        cmdr.credits = resp.credits;
        if (resp.commodity < NO_OF_STOCK_ITEMS)
          cmdr.current_cargo[resp.commodity] = resp.cargo;
      }
    }
    else if (Neuron::Net::DecodeDeath(msg, entityId, killerId))
    {
      if (entityId == rc.LocalPlayer())
      {
        // We were killed. Trigger the game-over sequence (game_update_flight picks this
        // up next frame). The server respawns us in place, so after the animation we
        // resume flight rather than restart - see respawn_after_death().
        game_over = 1;
        snd_play_sample(SND_EXPLODE);
      }
      else
      {
        // Another entity died: clear the lock if it was on it, drop it from the view,
        // and play the explosion.
        if (entityId == g_missile_lock_target)
          g_missile_lock_target = 0xFFFFFFFFu;
        rc.Forget(entityId);
        snd_play_sample(SND_EXPLODE);
      }
    }
    else if (Neuron::Net::DecodeDespawn(msg, entityId))
    {
      if (entityId == g_missile_lock_target)
        g_missile_lock_target = 0xFFFFFFFFu;
      rc.Forget(entityId);
    }
  }
}

// The four in-flight cockpit views render the 3D scene full-window; every other
// screen (charts, station, intro, game-over, save/load) stays on the retro
// letterboxed canvas.
static int is_flight_view(int scr)
{
  return (scr == SCR_FRONT_VIEW) || (scr == SCR_REAR_VIEW) || (scr == SCR_LEFT_VIEW) || (scr == SCR_RIGHT_VIEW);
}

// Gather the player's flight intent and send it to the server. Driven from the
// legacy flight state (PlayerFlight roll/climb/speed) that the flight keys and
// the cockpit HUD already maintain, normalized to axes, so the server moves the
// ship at exactly the speed shown on the dashboard. Used only in thin-client mode.
static void send_player_input(void)
{
  static uint32_t seq = 0;

  const int maxRoll = (PlayerCaps().maxRoll > 0) ? PlayerCaps().maxRoll : 1;
  const int maxClimb = (PlayerCaps().maxClimb > 0) ? PlayerCaps().maxClimb : 1;
  const int maxSpeed = (PlayerCaps().maxSpeed > 0) ? PlayerCaps().maxSpeed : 1;

  Net::ClientInput in;
  in.sequence = ++seq;
  // The legacy roll/climb controls are expressed in the SCREEN (cockpit) frame,
  // whose handedness is the transpose of the world basis the server rotates and
  // the client renders (BuildRenderRecords projects with Bᵀ). Negating the two
  // rotation axes maps the screen-handed control into that world frame, so the
  // felt direction of roll and level pitch is identical to before while a banked
  // pitch now pivots about the ship's own axes instead of the world's.
  in.rollAxis = -static_cast<float>(PlayerFlight().roll) / static_cast<float>(maxRoll);
  in.pitchAxis = -static_cast<float>(PlayerFlight().climb) / static_cast<float>(maxClimb);
  in.throttle = static_cast<float>(PlayerFlight().speed) / static_cast<float>(maxSpeed);
  in.fire = (kbd_fire_pressed != 0);
  in.fireMissile = s_fire_missile_intent;
  in.missileTarget = s_fire_missile_intent ? s_fire_missile_target : Net::NO_MISSILE_TARGET;
  s_fire_missile_intent = false;

  Client::ReplicationClientInstance().SendInput(in);
}

// Per-frame logic for the in-flight/docked state: drain replicated state, advance sound,
// choose the scene/clip mode, read input, and run the per-frame bookkeeping. Split out of
// the old monolithic loop. Transitions to the game-over animation when the player dies.
static void game_update_flight(void)
{
  if (game_over)
  {
    enter_game_over();
    return;
  }

  // Drain any replicated world state that arrived since last frame. This is a no-op
  // until ReplicationClientInstance().Open() is called, so the single-player path is
  // unchanged; once open, the client consumes the server's authoritative snapshots here
  // instead of simulating locally.
  Client::ReplicationClientInstance().Pump();
  if (Client::ReplicationClientInstance().IsOpen())
  {
    process_server_events();
    // Backstop: forget entities that silently left our area of interest (no despawn
    // event is sent for those), so they don't pile up as ghosts. ~3s at 30 Hz.
    Client::ReplicationClientInstance().EvictStale(90);
  }

  snd_update_sound();

  // Full-window 3D for cockpit views (retro/letterboxed for menus); this also sets the
  // aspect-aware optics used by the projection in the render pass.
  gfx_set_scene_fullwindow(is_flight_view(current_screen));
  gfx_set_scene_clip();

  rolling = 0;
  climbing = 0;

  handle_flight_keys();

  // In thin-client mode, the player's intent goes to the server.
  if (Client::ReplicationClientInstance().IsOpen())
    send_player_input();

  if (game_paused)
    return;

  if (message_count > 0)
    message_count--;

  if (!rolling)
    centre_flight_roll();

  if (!climbing)
    centre_flight_climb();
}

// Per-frame draw for the in-flight/docked state: the 3D scene, HUD and overlays the old
// loop body emitted (with the simulation-and-draw steps that are still fused).
static void game_render_flight(void)
{
  if (game_paused)
    return;

  // Charts (galactic / short range): redraw the chart, the live selected-system readout
  // and the crosshair every frame. The replicated chart functions are idempotent (they
  // only re-park the cursor when it is off-screen), so a per-frame redraw suits the
  // clear-and-redraw back buffer and the crosshair sprite moves cleanly without the old
  // XOR erase. Handles the chart whether opened docked or in flight.
  if (current_screen == SCR_GALACTIC_CHART || current_screen == SCR_SHORT_RANGE)
  {
    if (current_screen == SCR_GALACTIC_CHART)
      display_galactic_chart();
    else
      display_short_range_chart();
    show_distance_to_planet();
    draw_cross(cross_x, cross_y);
    return;
  }

  if (!docked)
  {
    if ((current_screen == SCR_FRONT_VIEW) || (current_screen == SCR_REAR_VIEW) || (current_screen == SCR_LEFT_VIEW) || (current_screen
      == SCR_RIGHT_VIEW) || (current_screen == SCR_INTRO_ONE) || (current_screen == SCR_INTRO_TWO) || (current_screen == SCR_GAME_OVER))
    {
      gfx_clear_display();
      update_starfield();
    }

    if (auto_pilot)
    {
      auto_dock();
      if ((mcount & 127) == 0)
        info_message("Docking Computers On");
    }

    // In thin-client mode the server owns the world: render the replicated, interpolated
    // state instead of simulating locally.
    if (Client::ReplicationClientInstance().IsOpen())
      render_replicated_objects();
    else
      update_local_objects();

    if (docked)
    {
      update_console();
      return;
    }

    if ((current_screen == SCR_FRONT_VIEW) || (current_screen == SCR_REAR_VIEW) || (current_screen == SCR_LEFT_VIEW) || (current_screen
      == SCR_RIGHT_VIEW))
    {
      if (draw_lasers)
      {
        draw_laser_lines();
        draw_lasers--;
      }

      draw_laser_sights();
    }

    if (message_count > 0)
      gfx_display_centre_text(358, message_string, 120, GFX_COL_WHITE);

    if (hyper_ready)
    {
      display_hyper_status();
      if ((mcount & 3) == 0)
        countdown_hyperspace();
    }

    mcount--;
    if (mcount < 0)
      mcount = 255;

    if ((mcount & 7) == 0)
      regenerate_shields();

    if ((mcount & 31) == 10)
    {
      if (PlayerDefense().energy < 50)
      {
        info_message("ENERGY LOW");
        snd_play_sample(SND_BEEP);
      }

      update_altitude();
    }

    if ((mcount & 31) == 20)
      update_cabin_temp();

    if ((mcount == 0) && (!witchspace))
      random_encounter();

    cool_laser();
    time_ecm();

    update_console();
  }

  if (current_screen == SCR_BREAK_PATTERN)
    display_break_pattern();
}

// Per-frame logic hook (GameApp::Update): step the active state. Intro screens advance on
// Space; flight runs the live game; the game-over animation plays out then restarts.
void game_update(void)
{
  switch (s_state)
  {
    case GameState::Intro1:
      kbd_poll_keyboard();
      if (kbd_space_pressed)
      {
        snd_stop_midi();
        enter_intro2();
      }
      break;

    case GameState::Intro2:
      kbd_poll_keyboard();
      if (kbd_space_pressed)
      {
        snd_stop_midi();
        enter_flight();
      }
      break;

    case GameState::Flight:
      game_update_flight();
      break;

    case GameState::GameOver:
      if (s_gameOverFrame >= 100)
      {
        respawn_after_death();   // animation done -> resume flight (MMO) or fresh game
        break;
      }
      s_gameOverFrame++;
      break;
  }
}

// Per-frame draw hook (GameApp::RenderScene): draw the active state's scene into the 2D
// batch (the engine flushes it to the back buffer after this).
void game_render_scene(void)
{
  switch (s_state)
  {
    case GameState::Intro1:
      update_intro1();
      break;

    case GameState::Intro2:
      update_intro2();
      break;

    case GameState::Flight:
      game_render_flight();
      break;

    case GameState::GameOver:
      gfx_clear_display();
      update_starfield();
      update_local_objects();
      gfx_display_centre_text(190, "GAME OVER", 140, GFX_COL_GOLD);
      break;
  }
}

int game_main(void)
{
  read_config_file();

  if (gfx_graphics_startup() == 1)
    return 1;

  /* Start the sound system... */
  snd_sound_startup();

  /* Do any setup necessary for the keyboard... */
  kbd_keyboard_startup();

  // Server-only client: single-player has been retired, so we always connect to
  // the authoritative server and render its world. The bind port and server
  // address can be overridden with DSO_BIND / DSO_SERVER (dotted-quad host);
  // they default to loopback for local play. The local-simulation path remains
  // only as a degraded fallback if networking fails to initialise.
  {
    Client::ReplicationClient& rc = Client::ReplicationClientInstance();

    uint16_t bindPort = 50000;
    if (const char* b = getenv("DSO_BIND"))
      bindPort = static_cast<uint16_t>(atoi(b));

    int a = 127, c = 0, d = 0, e = 1;
    if (const char* host = getenv("DSO_SERVER"))
      sscanf(host, "%d.%d.%d.%d", &a, &c, &d, &e);

    rc.Open(bindPort);
    rc.SetServerEndpoint(Net::MakeEndpoint(static_cast<uint8_t>(a), static_cast<uint8_t>(c), static_cast<uint8_t>(d),
                                           static_cast<uint8_t>(e), 40000));
    // LocalPlayer is set by the server's AssignPlayer handshake; default 0.
  }

  finish = 0;
  auto_pilot = 0;

  // The whole game now runs through the GameMain lifecycle: each gfx_update_screen()
  // drives ClientEngine::Frame() -> GameApp::Update/RenderScene -> game_update()/
  // game_render_scene(), which step the state machine (intro -> flight -> game-over ->
  // new game). game_main() just boots the first game and pumps frames until the window
  // closes (the message pump exits the process on close).
  start_new_game();
  while (!finish)
    gfx_update_screen();

  snd_sound_shutdown();

  gfx_graphics_shutdown();

  return 0;
}
