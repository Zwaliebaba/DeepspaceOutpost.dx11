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
#include "random.h"
#include "stars.h"
#include "keyboard.h"
#include "CameraRig.h"
#include "ReplicationClient.h"
#include "Messages/MessageBus.h"
#include "Messages/Framing.h"            // Neuron::Msg::PROTOCOL_VERSION (handshake)
#include "Messages/Defs/CoreEvents.h"
#include "Messages/Defs/InputActions.h"
#include "Messages/Defs/EquipmentEvents.h"   // EcmPulse / EscapePodUsed (G8)
#include "Messages/Defs/Travel.h"            // TravelRequest / TravelResponse
#include "Messages/Defs/UnitOrder.h"         // UnitOrder / UnitOrderAck (I1/I3 command protocol)
#include "GuiOverlay.h"
#include "GameWindows.h"
#include "ChartData.h"    // ChartData::Kind for the F5/F6/F7 chart overlay
#include "Scene3D.h"
#include "Camera.h"                           // MainCamera() (I3 move-order unprojection)
#include "input_win.h"                        // input_mouse_state (I3 pointer commands)
#include "GraphicsCore.h"                     // Graphics::Core::GetOutputSize (viewport size)
#include "gfx.h"                              // GFX_COL_* + 2D draw primitives (legacy)

#include <DirectXMath.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

// The client's in-process event bus. Inbound reliable facts (decoded from the
// server) are published here and independent subscribers (commerce, audio/VFX,
// view) react - mirroring the server's Msg::MessageBus. New presentation reactions
// (camera shake, kill feed, ...) just Subscribe<> instead of editing one switch.
// Defined up here with the includes: the input key handlers publish onto it well
// before the subscriber-registration block further down.
static Neuron::Msg::MessageBus g_clientBus;

int draw_lasers;
int mcount;
int message_count;
char message_string[80];

int find_input;
char find_name[20];

/*
 * Piloting is retired: the player flies the CAMERA (see CameraRig), not the
 * hull. The ship's flight intent is always zero - the legacy roll/climb ramp
 * and auto-centre machinery went with the cockpit view. PlayerFlight() remains
 * as presentation state (the intro parade and game-over debris animate off its
 * speed) and as the zero source send_player_input reads.
 */

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
  find_input = 0;
  witchspace = 0;

  create_new_stars();
  clear_local_objects();

  cross_x = -1;
  cross_y = -1;

  PlayerCaps().maxSpeed = 40; /* 0.27 Light Mach */
  PlayerCaps().maxRoll = 31;
  PlayerCaps().maxClimb = 8; /* CF 8 */
  PlayerCaps().maxFuel = 70; /* 7.0 Light Years */
}

// The chart crosshair, arrow-key steering, and D/F/O + name-search keys were the
// keyboard controls for the letterboxed charts. The charts are a native GUI overlay
// window now (ChartWindow) - it is mouse-driven and the overlay suppresses game keys
// while open - so that whole keyboard subsystem is retired.

// Pending fire-missile intent + the locked target it launches at, for the next
// input packet (thin-client mode). Set by launch_missile(), consumed and cleared by
// send_player_input().
static bool s_fire_missile_intent = false;
static unsigned int s_fire_missile_target = 0xFFFFFFFFu;

// The entity the missile is aimed at (0xFFFFFFFF = none). Since I2 this is the
// SELECTED entity (set by a pointer click, not the retired T/U lock keys): it drives
// the on-target reticle (render_replicated_objects), the orbit-camera subject, and
// the target the ability bar's Missile button launches at. Global so the renderer
// reads it.
unsigned int g_missile_lock_target = 0xFFFFFFFFu;

// Launch a missile at the current SELECTION (the ability bar Missile button / A1
// residue path): only when something is selected and a round is in the rack; the
// server spawns a homing projectile and the rack count comes back on PlayerStatus.
static void launch_missile(void)
{
  if ((g_missile_lock_target == 0xFFFFFFFFu) || (cmdr.missiles == 0))
    return;

  s_fire_missile_intent = true;
  s_fire_missile_target = g_missile_lock_target;
  g_missile_lock_target = 0xFFFFFFFFu;
  missile_target = MISSILE_UNARMED; // lock consumed
  snd_play_sample(SND_MISSILE);
}

// ---- I3 pointer commands (interaction.md): right-click to order your ship -------
//
// The order half of Track I's UX. An RMB CLICK on the flight view issues a
// contextual default order to the player's own ship (the only unit today): an
// entity under the cursor -> Attack/Dock/Collect/Approach by its kind; empty space
// -> Move to where the cursor ray meets a horizontal plane through the ship. The
// order rides the reliable UnitOrder lane (I1); an optimistic toast shows what was
// asked and a rejecting UnitOrderAck flashes it red. Camera orbit moved to LMB-drag
// (CameraRig) so RMB is free for commands.
//
// Deferred (documented in IMPLEMENTATION.md): the full move gizmo (elevation-drag
// stem + depth-faded grid), the RMB-hold radial menu, and clean-player Attack-
// friction (default Approach) - this increment lands the playable core.

// Optimistic order feedback, drawn by space.cpp's display_order_feedback().
unsigned int g_order_kind = 0;           // active order's OrderKind (0 = none)
bool         g_order_has_point = false;  // the order carries a world point (Move)
long long    g_order_point[3] = {0, 0, 0};
char         g_order_toast[40] = {0};
int          g_order_toast_timer = 0;    // frames the toast stays up
int          g_order_toast_col = 0;

static const char* order_kind_name(unsigned int _k)
{
  switch (static_cast<Neuron::Msg::OrderKind>(_k))
  {
    case Neuron::Msg::OrderKind::Stop:     return "STOP";
    case Neuron::Msg::OrderKind::Move:     return "MOVE";
    case Neuron::Msg::OrderKind::Approach: return "APPROACH";
    case Neuron::Msg::OrderKind::Dock:     return "DOCK";
    case Neuron::Msg::OrderKind::Attack:   return "ATTACK";
    case Neuron::Msg::OrderKind::Collect:  return "COLLECT";
    case Neuron::Msg::OrderKind::Escort:   return "ESCORT";
    default:                               return "ORDER";
  }
}

static void set_order_toast(const char* _text, int _col)
{
  snprintf(g_order_toast, sizeof(g_order_toast), "%s", _text);
  g_order_toast_timer = 90;   // ~3 s
  g_order_toast_col = _col;
}

// Cursor ray -> the point where it meets a horizontal plane through the ship, in
// absolute world coords, clamped to the server's Move reach. Returns false when the
// ship isn't visible or the ray is parallel to / behind the plane.
static bool cursor_to_move_point(int _mx, int _my, long long _out[3])
{
  Client::ReplicationClient& rc = Client::ReplicationClientInstance();
  Neuron::Net::EntitySnapshot me{};
  if (!rc.IsOpen() || !rc.Sample(rc.LocalPlayer(), 1.0, me))
    return false;

  const auto sz = Neuron::Graphics::Core::GetOutputSize();
  const int vw = static_cast<int>(sz.Width);
  const int vh = static_cast<int>(sz.Height);
  if (vw <= 0 || vh <= 0)
    return false;

  using namespace DirectX;
  Client::Camera& cam = Client::MainCamera();
  const XMMATRIX vp = XMMatrixMultiply(cam.View(), cam.Projection());
  const XMMATRIX invVP = XMMatrixInverse(nullptr, vp);

  const float ndcx = 2.0f * static_cast<float>(_mx) / static_cast<float>(vw) - 1.0f;
  const float ndcy = 1.0f - 2.0f * static_cast<float>(_my) / static_cast<float>(vh);
  XMVECTOR pNear = XMVector4Transform(XMVectorSet(ndcx, ndcy, 0.0f, 1.0f), invVP);
  XMVECTOR pFar  = XMVector4Transform(XMVectorSet(ndcx, ndcy, 1.0f, 1.0f), invVP);
  pNear = XMVectorScale(pNear, 1.0f / XMVectorGetW(pNear));
  pFar  = XMVectorScale(pFar,  1.0f / XMVectorGetW(pFar));

  // Ray + ship live in origin-relative space (the camera eye is the floating-origin
  // remainder), so the plane point is the ship minus the render origin.
  const long long* org = camera_rig_origin();
  const XMVECTOR ro = pNear;
  const XMVECTOR rd = XMVectorSubtract(pFar, pNear);
  const XMVECTOR planePt = XMVectorSet(
      static_cast<float>(static_cast<double>(me.x) - static_cast<double>(org[0])),
      static_cast<float>(static_cast<double>(me.y) - static_cast<double>(org[1])),
      static_cast<float>(static_cast<double>(me.z) - static_cast<double>(org[2])), 0.0f);
  const XMVECTOR n = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);   // horizontal plane through the ship

  const float denom = XMVectorGetX(XMVector3Dot(rd, n));
  if (std::fabs(denom) < 1e-4f)
    return false;
  const float t = XMVectorGetX(XMVector3Dot(XMVectorSubtract(planePt, ro), n)) / denom;
  if (t < 0.0f)
    return false;
  const XMVECTOR hit = XMVectorAdd(ro, XMVectorScale(rd, t));

  // Back to absolute world, clamped to the server's per-order Move reach so the
  // client's request matches what the server will accept.
  auto clampAxis = [](double _from, double _to) -> long long
  {
    double d = _to - _from;
    if (d >  1.0e6) d =  1.0e6;
    if (d < -1.0e6) d = -1.0e6;
    return static_cast<long long>(_from + d);
  };
  _out[0] = clampAxis(static_cast<double>(me.x), static_cast<double>(org[0]) + XMVectorGetX(hit));
  _out[1] = clampAxis(static_cast<double>(me.y), static_cast<double>(org[1]) + XMVectorGetY(hit));
  _out[2] = clampAxis(static_cast<double>(me.z), static_cast<double>(org[2]) + XMVectorGetZ(hit));
  return true;
}

// Issue the contextual default order for an RMB click at (mx,my).
static void dispatch_context_order(int _mx, int _my)
{
  Client::ReplicationClient& rc = Client::ReplicationClientInstance();
  const unsigned int self = rc.LocalPlayer();
  if (!rc.IsOpen() || self == 0xFFFFFFFFu)
    return;

  Neuron::Msg::UnitOrder ord;
  ord.unitId = self;

  const unsigned int tgt = pick_entity_at_screen(_mx, _my);
  if (tgt != 0xFFFFFFFFu)
  {
    Neuron::Net::EntitySnapshot ts{};
    if (!rc.Sample(tgt, 1.0, ts))
      return;
    ord.target = tgt;
    if (ts.type == SHIP_PLANET || ts.type < 0)                 ord.order = Neuron::Msg::OrderKind::Approach;
    else if (ts.type == SHIP_CORIOLIS || ts.type == SHIP_DODEC) ord.order = Neuron::Msg::OrderKind::Dock;
    else if (ts.type == SHIP_CARGO)                            ord.order = Neuron::Msg::OrderKind::Collect;
    else                                                       ord.order = Neuron::Msg::OrderKind::Attack;
    g_order_has_point = false;
    g_missile_lock_target = tgt;   // command-as-you-select: reticle + orbit follow it
  }
  else
  {
    long long pt[3];
    if (!cursor_to_move_point(_mx, _my, pt))
      return;
    ord.order = Neuron::Msg::OrderKind::Move;
    ord.targetX = pt[0]; ord.targetY = pt[1]; ord.targetZ = pt[2];
    g_order_has_point = true;
    g_order_point[0] = pt[0]; g_order_point[1] = pt[1]; g_order_point[2] = pt[2];
  }

  g_order_kind = static_cast<unsigned int>(ord.order);
  rc.SendUnitOrder(ord);
  set_order_toast(order_kind_name(g_order_kind), GFX_COL_YELLOW_2);   // optimistic
}

// Per-frame pointer-command polling (called from the flight update, after the
// camera). An RMB press-release inside the slop is a CLICK -> contextual order.
// Camera orbit (LMB-drag) and selection (LMB click, I2) live in CameraRig.
void handle_pointer_commands(void)
{
  if (GuiOverlay::IsShown() || current_screen != SCR_FRONT_VIEW || docked)
    return;

  int mx = 0, my = 0;
  bool lmb = false, rmb = false;
  input_mouse_state(mx, my, lmb, rmb);

  static bool s_prevRmb = false;
  static int  s_rmbDownX = 0, s_rmbDownY = 0;
  static bool s_rmbMoved = false;

  if (rmb && !s_prevRmb)
  {
    s_rmbDownX = mx; s_rmbDownY = my; s_rmbMoved = false;
  }
  else if (rmb)
  {
    int ddx = mx - s_rmbDownX; if (ddx < 0) ddx = -ddx;
    int ddy = my - s_rmbDownY; if (ddy < 0) ddy = -ddy;
    if (ddx > 6 || ddy > 6) s_rmbMoved = true;
  }
  else if (s_prevRmb && !s_rmbMoved)
  {
    dispatch_context_order(mx, my);   // RMB click -> contextual order
  }
  s_prevRmb = rmb;
}

// ---- I4 ability bar (interaction.md 3.7): abilities reachable by pointer --------
//
// A persistent, NON-MODAL strip of ability buttons across the top of the flight
// view - Stop / Missile / ECM / Bomb / Pod / Jump - so every combat verb is a click,
// not a key. It is non-modal: it does not raise GuiOverlay (which suppresses game
// input); instead the camera's LMB select skips a click whose press began over a
// button (ability_bar_button_at), and this handler triggers it. Availability greys
// each button from the PlayerStatus / equipment mirrors, and Bomb/Pod need a HOLD so
// a stray tap can't fire them.
//
// Deferred (documented): the screen-nav icon strip (charts/market/status/equip) and
// the FORMAL retirement of the A/E/Tab/M/C/J keys (they still work in parallel -
// key retirement is I7); "Launch"/undock stays on the docked screen's own UI.

namespace
{
  enum AbilityAct { ACT_STOP, ACT_MISSILE, ACT_ECM, ACT_BOMB, ACT_POD, ACT_JUMP, ACT_COUNT };
  struct AbilityButton { const char* label; int act; };
  const AbilityButton s_bar[ACT_COUNT] = {
    { "STOP", ACT_STOP }, { "MISSILE", ACT_MISSILE }, { "ECM", ACT_ECM },
    { "BOMB", ACT_BOMB }, { "POD", ACT_POD }, { "JUMP", ACT_JUMP },
  };
  constexpr int BAR_SLOT_W = 80;
  constexpr int BAR_SLOT_H = 20;
  constexpr int BAR_GAP    = 4;
  constexpr int BAR_TOP_Y  = 8;
  constexpr int BOMB_POD_HOLD_FRAMES = 18;   // ~0.6 s hold-to-confirm

  int  s_ability_down_btn = -1;   // button the current LMB press started on (-1 = none)
  int  s_ability_held_frames = 0; // frames that press has been held
}

// The left edge of the button row for the current window (centred, clamped on-screen).
static int ability_bar_origin_x(int _vw)
{
  const int total = ACT_COUNT * BAR_SLOT_W + (ACT_COUNT - 1) * BAR_GAP;
  int x0 = (_vw - total) / 2;
  if (x0 < 4) x0 = 4;
  return x0;
}

// The bar button under (mx,my), or -1. Exposed so the camera's select can ignore a
// click that landed on the bar. Only live on the flight view (not docked).
int ability_bar_button_at(int _mx, int _my)
{
  if (GuiOverlay::IsShown() || current_screen != SCR_FRONT_VIEW || docked)
    return -1;
  const int vw = static_cast<int>(Neuron::Graphics::Core::GetOutputSize().Width);
  const int x0 = ability_bar_origin_x(vw);
  for (int i = 0; i < ACT_COUNT; ++i)
  {
    const int x = x0 + i * (BAR_SLOT_W + BAR_GAP);
    if (_mx >= x && _mx < x + BAR_SLOT_W && _my >= BAR_TOP_Y && _my < BAR_TOP_Y + BAR_SLOT_H)
      return i;
  }
  return -1;
}

static bool ability_enabled(int _act)
{
  switch (_act)
  {
    case ACT_STOP:    return true;
    case ACT_MISSILE: return g_missile_lock_target != 0xFFFFFFFFu && cmdr.missiles > 0;
    case ACT_ECM:     return cmdr.ecm != 0;
    case ACT_BOMB:    return cmdr.energy_bomb != 0;
    case ACT_POD:     return cmdr.escape_pod != 0;
    case ACT_JUMP:    return !docked && !witchspace;
    default:          return false;
  }
}

static void ability_trigger(int _act)
{
  Client::ReplicationClient& rc = Client::ReplicationClientInstance();
  switch (_act)
  {
    case ACT_STOP:
    {
      Neuron::Msg::UnitOrder ord;
      ord.unitId = rc.LocalPlayer();
      ord.order = Neuron::Msg::OrderKind::Stop;
      rc.SendUnitOrder(ord);
      g_order_kind = static_cast<unsigned int>(Neuron::Msg::OrderKind::Stop);
      g_order_has_point = false;
      set_order_toast("STOP", GFX_COL_YELLOW_2);
      break;
    }
    case ACT_MISSILE: launch_missile(); break;   // fires at the selection (rack-gated)
    case ACT_ECM:
      if (cmdr.ecm)
        g_clientBus.Publish(Neuron::Msg::ActionTriggered{ Neuron::Msg::InputAction::Ecm, 0 });
      break;
    case ACT_BOMB:
      if (cmdr.energy_bomb)
      {
        g_clientBus.Publish(Neuron::Msg::ActionTriggered{ Neuron::Msg::InputAction::EnergyBomb, 0 });
        cmdr.energy_bomb = 0;   // optimistic (no equipment mirror yet - see A1 residue)
      }
      break;
    case ACT_POD:
      if (cmdr.escape_pod)
        g_clientBus.Publish(Neuron::Msg::ActionTriggered{ Neuron::Msg::InputAction::EscapePod, 0 });
      break;
    case ACT_JUMP:
      if (!docked && !witchspace)
        jump_warp();
      break;
    default: break;
  }
}

// Draw the ability bar (called from the HUD pass in space.cpp). A box per button,
// its label greyed when unavailable and flashed red while a Bomb/Pod hold-to-confirm
// is in progress.
void draw_ability_bar(void)
{
  if (GuiOverlay::IsShown() || current_screen != SCR_FRONT_VIEW || docked)
    return;

  const int vw = static_cast<int>(Neuron::Graphics::Core::GetOutputSize().Width);
  hud_set_origin(0, 0);
  const int x0 = ability_bar_origin_x(vw);

  for (int i = 0; i < ACT_COUNT; ++i)
  {
    const int x = x0 + i * (BAR_SLOT_W + BAR_GAP);
    const bool en = ability_enabled(s_bar[i].act);
    const bool confirming = (i == s_ability_down_btn)
                         && (s_bar[i].act == ACT_BOMB || s_bar[i].act == ACT_POD);

    const int frameCol = confirming ? GFX_COL_RED : (en ? GFX_COL_GREY_1 : GFX_COL_GREY_3);
    const int textCol  = confirming ? GFX_COL_RED : (en ? GFX_COL_WHITE  : GFX_COL_GREY_3);
    hud_rect(x, BAR_TOP_Y, x + BAR_SLOT_W, BAR_TOP_Y + BAR_SLOT_H, frameCol);

    const int len = static_cast<int>(strlen(s_bar[i].label));
    const int tx = x + (BAR_SLOT_W - len * 8) / 2;
    hud_text(tx, BAR_TOP_Y + 6, s_bar[i].label, textCol);
  }
}

// Per-frame ability-bar input: an LMB press-release on the same button triggers it
// (Bomb/Pod require the press be HELD past the confirm threshold). Runs before the
// camera; the camera's select ignores a click whose press began on the bar.
void handle_ability_bar(void)
{
  if (GuiOverlay::IsShown() || current_screen != SCR_FRONT_VIEW || docked)
  {
    s_ability_down_btn = -1;
    return;
  }

  int mx = 0, my = 0;
  bool lmb = false, rmb = false;
  input_mouse_state(mx, my, lmb, rmb);

  static bool s_prevLmb = false;
  if (lmb && !s_prevLmb)
  {
    s_ability_down_btn = ability_bar_button_at(mx, my);
    s_ability_held_frames = 0;
  }
  else if (lmb && s_ability_down_btn >= 0)
  {
    ++s_ability_held_frames;
  }
  else if (!lmb && s_prevLmb && s_ability_down_btn >= 0)
  {
    // Release: fire only if it landed back on the same button and it is available;
    // Bomb/Pod additionally need the hold-to-confirm dwell.
    if (ability_bar_button_at(mx, my) == s_ability_down_btn
        && ability_enabled(s_bar[s_ability_down_btn].act))
    {
      const int act = s_bar[s_ability_down_btn].act;
      const bool holdReq = (act == ACT_BOMB || act == ACT_POD);
      if (!holdReq || s_ability_held_frames >= BOMB_POD_HOLD_FRAMES)
        ability_trigger(act);
    }
    s_ability_down_btn = -1;
  }
  s_prevLmb = lmb;
}

// The charts moved to a native GUI overlay window (ChartWindow, GameWindows.cpp):
// F5/F6/F7 open it, a click on its map selects the nearest system, and its own
// HYPERSPACE button jumps. The old on-canvas pointer handler and the letterboxed
// draw pass are gone - the overlay draws itself over the flight view.

void handle_flight_keys(void)
{
  kbd_poll_keyboard();

  if (kbd_F1_pressed)
  {
    find_input = 0;

    if (docked)
      launch_player();
    else
      current_screen = SCR_FRONT_VIEW;   // back to the cockpit (e.g. from a chart)
  }

  if (kbd_F4_pressed)
  {
    find_input = 0;

    if (docked)
      OpenEquipWindow();
  }

  if (kbd_F5_pressed)
  {
    find_input = 0;
    OpenChartWindow(ChartData::GALACTIC);      // native chart overlay (click to select, HYPERSPACE to jump)
  }

  if (kbd_F6_pressed)
  {
    find_input = 0;
    OpenChartWindow(ChartData::SHORT_RANGE);
  }

  if (kbd_F7_pressed)
  {
    find_input = 0;
    // System data now lives in the chart window's side panel (updates with the selection).
    OpenChartWindow(ChartData::GALACTIC);
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

  // F12 toggles the camera controller: first-person free flight <-> orbit around
  // the selected object (the missile-lock target, else the own ship). Edge-
  // triggered so holding the key flips the mode exactly once.
  static int f12_was_down = 0;
  if (kbd_F12_pressed)
  {
    if (!f12_was_down)
      camera_rig_toggle_mode();
    f12_was_down = 1;
  }
  else
    f12_was_down = 0;

  // (I7: A=fire retired -> the Attack order fires; E/Tab/M/pod/J = ability bar;
  //  T/U = pointer select. The chart keys - D/F/O, name search, arrow crosshair -
  //  retired with the letterboxed charts; the native chart window is mouse-driven.)

  if (kbd_dock_pressed)
  {
    // The docking computer requests a dock when within the server's range; the
    // docked flow starts on the StationResponse. (The legacy client-side
    // autopilot was retired with the single-player fallback.)
    if (!docked && cmdr.docking_computer)
      engage_docking_computer();
  }
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

// Enter the first intro screen (title + Elite theme). The intro scenes animate
// in camera space, so the rig resets to the identity camera.
static void enter_intro1(void)
{
  current_screen = SCR_INTRO_ONE;
  camera_rig_reset();
  snd_play_midi(SND_ELITE_THEME, TRUE);
  initialise_intro1();
  s_state = GameState::Intro1;
}

// Enter the second intro screen (ship parade + Blue Danube).
static void enter_intro2(void)
{
  current_screen = SCR_INTRO_TWO;
  camera_rig_reset();
  snd_play_midi(SND_BLUE_DANUBE, TRUE);
  initialise_intro2();
  PlayerFlight().speed = 3;   // presentation only: paces the parade's approach
  PlayerFlight().roll = 0;
  PlayerFlight().climb = 0;
  s_state = GameState::Intro2;
}

// The docked "home": the camera-space 3D view of the station (current_screen stays the
// front view so the scene renders full-window) with the native station menu window
// floating over it - replacing the legacy 512x514 commander-status screen.
static void enter_station(void)
{
  current_screen = SCR_FRONT_VIEW;
  OpenStationMenu();
}

// Enter live flight (and the docked menus); start docked at the station.
static void enter_flight(void)
{
  dock_player();
  enter_station();
  s_state = GameState::Flight;
}

// Enter the game-over animation: a dead Cobra tumbling through wreckage for 100 frames.
// The scene animates in camera space against the identity camera (camera_rig_reset):
// the wreck spawns well ahead at +z and drifts toward the eye; +1000 with speed 6
// keeps it in front for the whole 100-frame animation.
static void enter_game_over(void)
{
  current_screen = SCR_GAME_OVER;
  camera_rig_reset();
  gfx_set_clip_region(1, 1, 510, 383);

  PlayerFlight().speed = 6;   // presentation only: paces the drift toward the wreck
  PlayerFlight().roll = 0;
  PlayerFlight().climb = 0;
  clear_local_objects();

  Matrix rotmat;
  set_init_matrix(rotmat);

  int newship = add_new_ship(SHIP_COBRA3, 0, 0, 1000, rotmat, 0, 0);
  local_objects[newship].flags |= FLG_DEAD;

  for (int i = 0; i < 5; i++)
  {
    const int type = (rand255() & 1) ? SHIP_CARGO : SHIP_ALLOY;
    newship = add_new_ship(type, (rand255() & 63) - 32, (rand255() & 63) - 32, 1000, rotmat, 0, 0);
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
  current_screen = SCR_FRONT_VIEW;
  enter_intro1();
}

// After the game-over animation. The server has respawned us DOCKED at the
// nearest station (the G3 death rule, minus cargo), so enter the station menus
// rather than resuming flight - mirroring enter_flight. dock_player()
// re-confirms the dock with the server and resets our ship state; the replicated
// snapshots (now at the station) drive the view when we launch.
static void respawn_after_death(void)
{
  game_over = 0;
  // The server dropped our cargo on death; clear the local display to match (the
  // authoritative hold is already empty server-side).
  memset(cmdr.current_cargo, 0, sizeof(cmdr.current_cargo));
  dock_player();
  enter_station();
  s_state = GameState::Flight;
}

// The break-pattern transition (concentric rings filling the cockpit on launch / dock /
// hyperspace) was a FIRST-PERSON effect. In the third-person camera model the ship simply
// appears in space (or at the station) via the server's snapshots, so the transition is
// gone: launch/dock/arrival just switch the view directly.

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
// This frame's discrete combat input, accumulated from ActionTriggered messages and
// consumed by send_player_input. Continuous flight (roll/pitch/throttle) is NOT here -
// it stays the legacy rate-based PlayerFlight state, normalized to axes at send time.
static bool     s_frameFire = false;
static bool     s_frameMissile = false;
static unsigned int s_frameMissileTarget = 0xFFFFFFFFu;
static bool     s_frameEcm = false;
static bool     s_frameEnergyBomb = false;
static bool     s_frameEscapePod = false;

// The other players in view, keyed by entity id: their commander name + legal
// status, as replicated by PlayerInfo. Used to label ships and (later) chat; an
// entry is dropped when its ship despawns/dies. This is presentation-only mirror
// state - the server stays authoritative.
struct PlayerRosterEntry
{
  std::string name;
  int wanted = 0;
};
static std::unordered_map<uint32_t, PlayerRosterEntry> g_playerRoster;

static void register_client_event_handlers(void)
{
  static bool registered = false;
  if (registered)
    return;
  registered = true;

  // Travel: the authoritative outcome of a hyperspace / in-system jump request.
  // Position and fuel changes ride the snapshot stream and PlayerStatus; this
  // drives the screen flow and the classic messages.
  g_clientBus.Subscribe<Neuron::Msg::TravelResponse>([](const Neuron::Msg::TravelResponse& _t)
  {
    // A hyperspace jump (G7) arrives in FLIGHT near the destination, or misfires
    // into a witchspace ambush - either way, leave the station screen for space.
    // The witchspace flag is mirrored for the HUD/UX guards (compass, market key).
    if (_t.kind == Neuron::Msg::TravelKind::Hyperspace &&
        (_t.status == Neuron::Msg::TravelStatus::Arrived || _t.status == Neuron::Msg::TravelStatus::Witchspace))
    {
      witchspace = (_t.status == Neuron::Msg::TravelStatus::Witchspace) ? 1 : 0;
      docked = 0;
      current_screen = SCR_FRONT_VIEW;   // arrive in space (camera-space view)
      CloseStationMenu();
      snd_play_sample(SND_HYPERSPACE);
      return;
    }

    if (_t.status == Neuron::Msg::TravelStatus::MassLocked)
      info_message("Mass Locked");
    else if (_t.status == Neuron::Msg::TravelStatus::NotEnoughFuel ||
             _t.status == Neuron::Msg::TravelStatus::OutOfRange)
      info_message("Out Of Fuel Range");
  });

  // Commerce: apply the authoritative station result to the local commander.
  g_clientBus.Subscribe<Net::StationResponse>([](const Net::StationResponse& _resp)
  {
    if (_resp.status != Net::StationStatus::Ok)
      return;

    // The server confirmed a dock request: NOW the docked flow starts (the
    // client never flips itself docked on a proximity guess - see request_dock).
    if (_resp.kind == Net::StationRequestKind::Dock)
    {
      if (!docked)
      {
        docked = 1;
        PlayerFlight().speed = 0;
        PlayerFlight().roll = 0;
        PlayerFlight().climb = 0;
        PlayerCaps().altitude = 255;   // display defaults; vitals mirror PlayerStatus
        PlayerCaps().cabTemp = 30;
        reset_weapons();
        g_missile_lock_target = 0xFFFFFFFFu;
        snd_play_sample(SND_DOCK);
        enter_station();   // docked: camera-space view + the station menu window
      }
      cmdr.credits = _resp.credits;
      return;
    }

    cmdr.credits = _resp.credits;

    // An equipment purchase confirmed: mirror the granted item's ownership (the
    // server owns the fact; we only reflect its echoed result). Missiles and fuel
    // ride PlayerStatus, so nothing to do for those here.
    if (_resp.kind == Net::StationRequestKind::Equip)
    {
      switch (static_cast<Net::EquipItem>(_resp.commodity))
      {
        case Net::EquipItem::LargeCargoBay: cmdr.cargo_capacity = 35; break;
        case Net::EquipItem::Ecm:           cmdr.ecm = 1;             break;
        case Net::EquipItem::FuelScoop:     cmdr.fuel_scoop = 1;      break;
        case Net::EquipItem::EnergyBomb:    cmdr.energy_bomb = 1;     break;
        case Net::EquipItem::EscapePod:     cmdr.escape_pod = 1;      break;
        case Net::EquipItem::Missile:       break;   // count rides PlayerStatus
      }
      return;
    }

    // Buy/Sell echo the affected commodity's resulting hold quantity. (Guard the
    // kind: an Equip response reuses `commodity` for the EquipItem id, which would
    // otherwise clobber a cargo slot of the same index.)
    if ((_resp.kind == Net::StationRequestKind::Buy || _resp.kind == Net::StationRequestKind::Sell) &&
        _resp.commodity < NO_OF_STOCK_ITEMS)
      cmdr.current_cargo[_resp.commodity] = _resp.cargo;
  });

  // Death: our own death triggers the game-over sequence; any other entity's death
  // drops it from the view, clears a missile lock on it, and plays the explosion.
  g_clientBus.Subscribe<Neuron::Msg::EntityDeath>([](const Neuron::Msg::EntityDeath& _death)
  {
    Client::ReplicationClient& rc = Client::ReplicationClientInstance();
    if (_death.victim == rc.LocalPlayer())
    {
      // We were killed. Trigger the game-over sequence (game_update_flight picks this
      // up next frame). The server respawns us in place, so after the animation we
      // resume flight rather than restart - see respawn_after_death().
      game_over = 1;
      snd_play_sample(SND_EXPLODE);
      return;
    }
    if (_death.victim == g_missile_lock_target)
      g_missile_lock_target = 0xFFFFFFFFu;
    g_playerRoster.erase(_death.victim);
    // Capture the dying ship's last position/type BEFORE forgetting it, so we can
    // play a debris burst where it died (the server just vanishes the entity).
    Net::EntitySnapshot vs;
    if (rc.Sample(_death.victim, 1.0, vs))
      spawn_replicated_explosion(vs);
    rc.Forget(_death.victim);
    snd_play_sample(SND_EXPLODE);
  });

  // Despawn: drop the entity from the view and clear a missile lock on it.
  g_clientBus.Subscribe<Neuron::Msg::EntityDespawn>([](const Neuron::Msg::EntityDespawn& _ds)
  {
    if (_ds.entityId == g_missile_lock_target)
      g_missile_lock_target = 0xFFFFFFFFu;
    g_playerRoster.erase(_ds.entityId);
    Client::ReplicationClientInstance().Forget(_ds.entityId);
  });

  // Roster: another player's name/legal status. Mirror it for ship labels + chat.
  g_clientBus.Subscribe<Neuron::Msg::PlayerInfo>([](const Neuron::Msg::PlayerInfo& _pi)
  {
    g_playerRoster[_pi.entityId] = PlayerRosterEntry{ _pi.name, _pi.wantedLevel };
  });

  // Status: our own authoritative vitals for the HUD. The server owns shields and
  // energy, so mirror them into the legacy defense state the cockpit HUD draws
  // (fuel/score wiring lands with G7/later).
  g_clientBus.Subscribe<Neuron::Msg::PlayerStatus>([](const Neuron::Msg::PlayerStatus& _ps)
  {
    cmdr.credits = _ps.credits;
    cmdr.fuel = _ps.fuel;   // hyperspace tank (G7): server-owned, drives the fuel gauge
    cmdr.missiles = _ps.missiles;   // rack count: server-owned (buy/launch/respawn)
    PlayerDefense().frontShield = _ps.frontShield;
    PlayerDefense().aftShield = _ps.aftShield;
    PlayerDefense().energy = _ps.energy;
    PlayerDefense().laserHeat = _ps.laserTemp;   // laser dial (G8): server-owned heat
  });

  // I3 order outcome: the server accepted or refused a UnitOrder. An accept keeps the
  // optimistic marker/toast running; a refusal flashes the reason red and drops the
  // marker so the client stops showing an order that isn't happening.
  g_clientBus.Subscribe<Neuron::Msg::UnitOrderAck>([](const Neuron::Msg::UnitOrderAck& _ack)
  {
    if (_ack.status == Neuron::Msg::OrderStatus::Accepted)
      return;
    const char* why =
        _ack.status == Neuron::Msg::OrderStatus::NotYours   ? "NOT YOURS"    :
        _ack.status == Neuron::Msg::OrderStatus::BadTarget  ? "BAD TARGET"   :
        _ack.status == Neuron::Msg::OrderStatus::Docked     ? "UNDOCK FIRST" :
        _ack.status == Neuron::Msg::OrderStatus::Illegal    ? "ILLEGAL"      :
        _ack.status == Neuron::Msg::OrderStatus::OutOfRange ? "OUT OF RANGE" :
                                                              "REJECTED";
    set_order_toast(why, GFX_COL_RED);
    g_order_kind = 0;
    g_order_has_point = false;
  });

  // ECM burst (G8): someone's unit fired - play the classic buzz and light the
  // E indicator (it counts down via time_ecm). The downed missiles arrive as
  // EntityDeath events (explosions) alongside.
  g_clientBus.Subscribe<Neuron::Msg::EcmPulse>([](const Neuron::Msg::EcmPulse&)
  {
    ecm_active = 32;
    snd_play_sample(SND_ECM);
  });

  // Escape pod (G8): our pod fired - the ship is gone and the server has us
  // docked at the nearest station with an empty hold and a clean record (the
  // CargoManifest/PlayerStatus refreshes ride alongside). Flip into the docked
  // flow, legacy abandon_ship style.
  g_clientBus.Subscribe<Neuron::Msg::EscapePodUsed>([](const Neuron::Msg::EscapePodUsed& _e)
  {
    if (_e.entityId != Client::ReplicationClientInstance().LocalPlayer())
      return;
    cmdr.escape_pod = 0;
    memset(cmdr.current_cargo, 0, sizeof(cmdr.current_cargo));
    snd_play_sample(SND_DOCK);
    dock_player();
    enter_station();   // escape pod: respawn docked at the station
  });

  // Cargo manifest: the authoritative per-commodity hold, resent after a scoop or a
  // respawn emptied it. Mirror it into the commander and, if the hold actually grew
  // (a scoop, not a respawn), play a pickup cue - the legacy client had no netcode,
  // so the server is the source of truth for what we're carrying.
  g_clientBus.Subscribe<Neuron::Msg::CargoManifest>([](const Neuron::Msg::CargoManifest& _cm)
  {
    int before = 0;
    for (int i = 0; i < NO_OF_STOCK_ITEMS; ++i)
      before += cmdr.current_cargo[i];

    int after = 0;
    const int n = static_cast<int>(_cm.units.size());
    for (int i = 0; i < NO_OF_STOCK_ITEMS; ++i)
    {
      cmdr.current_cargo[i] = (i < n) ? _cm.units[i] : 0;
      after += cmdr.current_cargo[i];
    }

    if (after > before)
      snd_play_sample(SND_BEEP);   // scooped something
  });

  // Input command-builder: a discrete combat action sets this frame's intent, which
  // send_player_input folds into the outgoing InputCommand.
  g_clientBus.Subscribe<Neuron::Msg::ActionTriggered>([](const Neuron::Msg::ActionTriggered& _a)
  {
    switch (_a.action)
    {
      case Neuron::Msg::InputAction::Fire:
        s_frameFire = true;
        break;
      case Neuron::Msg::InputAction::LaunchMissile:
        s_frameMissile = true;
        s_frameMissileTarget = _a.param;
        break;
      case Neuron::Msg::InputAction::Ecm:
        s_frameEcm = true;
        break;
      case Neuron::Msg::InputAction::EnergyBomb:
        s_frameEnergyBomb = true;
        break;
      case Neuron::Msg::InputAction::EscapePod:
        s_frameEscapePod = true;
        break;
    }
  });
}

static void process_server_events(void)
{
  register_client_event_handlers();

  // Decode each inbound reliable fact to its catalog type and publish it onto the
  // client bus; the subscribers above react. Dispatch once after draining.
  Client::ReplicationClient& rc = Client::ReplicationClientInstance();
  Net::ReliableMessage msg;
  while (rc.PollEvent(msg))
  {
    Net::StationResponse resp;
    Neuron::Msg::TravelResponse travel;
    Neuron::Msg::EntityDeath death;
    Neuron::Msg::EntityDespawn despawn;
    Neuron::Msg::PlayerInfo info;
    Neuron::Msg::PlayerStatus status;
    Neuron::Msg::CargoManifest cargo;
    Neuron::Msg::EcmPulse ecm;
    Neuron::Msg::EscapePodUsed pod;
    Neuron::Msg::UnitOrderAck oack;

    if (Neuron::Msg::TryDecode(msg, resp))
      g_clientBus.Publish(resp);
    else if (Neuron::Msg::TryDecode(msg, oack))
      g_clientBus.Publish(oack);
    else if (Neuron::Msg::TryDecode(msg, travel))
      g_clientBus.Publish(travel);
    else if (Neuron::Msg::TryDecode(msg, death))
      g_clientBus.Publish(death);
    else if (Neuron::Msg::TryDecode(msg, despawn))
      g_clientBus.Publish(despawn);
    else if (Neuron::Msg::TryDecode(msg, info))
      g_clientBus.Publish(info);
    else if (Neuron::Msg::TryDecode(msg, status))
      g_clientBus.Publish(status);
    else if (Neuron::Msg::TryDecode(msg, cargo))
      g_clientBus.Publish(cargo);
    else if (Neuron::Msg::TryDecode(msg, ecm))
      g_clientBus.Publish(ecm);
    else if (Neuron::Msg::TryDecode(msg, pod))
      g_clientBus.Publish(pod);
  }
  g_clientBus.Dispatch();
}

// The in-flight scene renders the 3D full-window; every other screen (charts,
// station, intro, game-over, save/load) stays on the retro letterboxed canvas.
static int is_flight_view(int scr)
{
  return scr == SCR_FRONT_VIEW;
}

// Send the player's per-frame intent to the server. Piloting is retired (the
// player flies the CAMERA; the hull idles), so the flight axes are always zero -
// but the command still carries the discrete combat intents AND the snapshot ack
// the delta stream depends on, so the cadence must not stop. Thin-client only.
static void send_player_input(void)
{
  static uint32_t seq = 0;

  Msg::InputCommand in;
  in.sequence = ++seq;
  // Zero flight intent: the ship holds station (the server clamps and decays
  // motion authoritatively; a zero throttle brings the hull to rest).
  in.rollAxis = 0.0f;
  in.pitchAxis = 0.0f;
  in.throttle = 0.0f;

  // Discrete combat actions flow as LocalOnly ActionTriggered messages through the
  // client bus into this frame's intent (the command-builder pattern): the input
  // layer publishes what the player did, the subscriber accumulates it here.
  register_client_event_handlers();
  s_frameFire = false;
  s_frameMissile = false;
  s_frameMissileTarget = Msg::NO_MISSILE_TARGET;
  s_frameEcm = false;
  s_frameEnergyBomb = false;
  s_frameEscapePod = false;
  // (I7: manual A=fire is retired - the Attack order drives the ship's laser
  //  server-side now. in.fire therefore stays false; the ability bar / orders own
  //  the other actions.)
  if (s_fire_missile_intent)
  {
    g_clientBus.Publish(Neuron::Msg::ActionTriggered{ Neuron::Msg::InputAction::LaunchMissile, s_fire_missile_target });
    s_fire_missile_intent = false;
  }
  g_clientBus.Dispatch();

  in.fire = s_frameFire;
  in.fireMissile = s_frameMissile;
  in.missileTarget = s_frameMissile ? s_frameMissileTarget : Msg::NO_MISSILE_TARGET;
  in.ecm = s_frameEcm;
  in.energyBomb = s_frameEnergyBomb;
  in.escapePod = s_frameEscapePod;

  Client::ReplicationClientInstance().SendInput(in);
}

// The client's connection configuration (read once from the environment in
// game_main) and the retry that keeps trying to (re)establish the socket if it
// failed to open. There is no offline mode: the server owns the game.
static uint16_t s_bindPort = 50000;
static Net::Endpoint s_serverEndpoint;

static void ensure_connection(void)
{
  Client::ReplicationClient& rc = Client::ReplicationClientInstance();
  if (rc.IsOpen())
    return;

  static int retryCountdown = 0;
  if (retryCountdown-- > 0)
    return;
  retryCountdown = 120;   // retry roughly every 2 s at display rate

  if (rc.Open(s_bindPort))
  {
    rc.SetServerEndpoint(s_serverEndpoint);
    // The opening handshake (protocol version + commander name) rides the
    // reliable Control lane, redelivered until the server accepts it.
    rc.SendHello(Neuron::Msg::PROTOCOL_VERSION, cmdr.name);
  }
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

  // The server owns the game: keep the connection up (retries if the socket
  // failed to open), then drain the replicated world state that arrived since
  // last frame. Everything below no-ops harmlessly while disconnected; the
  // render pass shows the connection-lost state instead of a world.
  ensure_connection();
  Client::ReplicationClientInstance().Pump();
  process_server_events();
  // Backstop: forget entities that silently left our area of interest (no despawn
  // event is sent for those), so they don't pile up as ghosts. ~3s at 30 Hz.
  Client::ReplicationClientInstance().EvictStale(90);

  snd_update_sound();

  // Full-window 3D for the flight view (retro/letterboxed for menus); this also sets
  // the main Camera's projection for the live viewport.
  gfx_set_scene_fullwindow(is_flight_view(current_screen));
  gfx_set_scene_clip();

  // The free camera: gather mouse/wheel/key input, advance the active controller
  // (first-person or orbit), and write this frame's view. Runs before the key
  // handler so a fresh missile lock orbits from the next frame.
  handle_ability_bar();        // I4: ability-bar clicks (before the camera, so a bar
                               //     click is consumed instead of selecting behind it)

  camera_rig_update();

  handle_pointer_commands();   // I3: RMB contextual orders (after the camera reads input)

  handle_flight_keys();

  // The player's remaining intent goes to the server (no-op while disconnected):
  // zero flight axes - piloting is retired - plus the combat intents and the
  // snapshot ack the delta stream needs.
  send_player_input();

  if (message_count > 0)
    message_count--;
}

// Render-free accessor for the native HUD pass (HudRender.cpp, a winrt TU that stays off
// the legacy game headers): is the replication socket up?
bool game_client_connected(void) { return Client::ReplicationClientInstance().IsOpen(); }

// Per-frame draw for the in-flight/docked state: the 3D scene, HUD and overlays the old
// loop body emitted (with the simulation-and-draw steps that are still fused).
static void game_render_flight(void)
{
  // The charts and the docked station menu are native GUI overlay windows now - they
  // draw themselves over the camera-space view below. There is no retro chart / commander
  // -status branch here; system data lives in the chart window's panel.

  // No offline mode: without a connection there is no world to render (docked or in
  // flight). ensure_connection keeps retrying; the native HUD pass (RenderGameHud) draws
  // the "connection lost" banner meanwhile.
  if (!Client::ReplicationClientInstance().IsOpen())
    return;

  // The camera-space 3D scene renders in BOTH docked and flight: docked shows the station
  // in view with the StationMenuWindow floating over it (current_screen stays the front
  // view so this is full-window); in flight it is the live world.
  if ((current_screen == SCR_FRONT_VIEW) || (current_screen == SCR_INTRO_ONE) ||
      (current_screen == SCR_INTRO_TWO) || (current_screen == SCR_GAME_OVER))
  {
    gfx_clear_display();
    update_starfield();
  }

  // The server owns the world: render the replicated, interpolated state.
  render_replicated_objects();

  // The flight HUD + display-only upkeep run only in flight; docked, the station menu
  // window is the UI (no dashboard).
  if (!docked)
  {
    // The local hull's beam visual: while armed (fire_laser), the own ship's render
    // record carries FLG_FIRING and the muzzle bolt draws with the ship. Count down here.
    if ((current_screen == SCR_FRONT_VIEW) && draw_lasers)
      draw_lasers--;

    if (message_count > 0)
      hud_centre_text(358, message_string, 120, GFX_COL_WHITE);

    mcount--;
    if (mcount < 0)
      mcount = 255;

    // Display-only upkeep: the energy-low warning and the altitude dial read the
    // server-mirrored vitals; the laser/ECM steps pace the beam visual and the E
    // indicator. No game rule runs client-side.
    if ((mcount & 31) == 10)
    {
      if (PlayerDefense().energy < 50)
      {
        info_message("ENERGY LOW");
        snd_play_sample(SND_BEEP);
      }

      update_altitude();
    }

    cool_laser();
    time_ecm();
  }
  // The cockpit dashboard + flight overlays (update_console) now draw natively in the
  // RenderCanvas HUD pass (RenderGameHud), not into the gfx2d batch here.
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
  // Push the current "Ship Shading" setting to the 3D renderer (cheap; the flag may
  // change at runtime via the options window). Off = faithful flat per-face colour.
  Neuron::Graphics::Scene3D::SetLightingEnabled(scene_shading != 0);

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
    {
      // Client-space scene (like the intro): the starfield + ships fill the window and
      // "GAME OVER" is centred vertically. Needs the full-window clip or the client-space
      // text is clipped by the default 512x514 scissor.
      gfx_set_scene_fullwindow(1);
      gfx_set_scene_clip();
      int ch;
      gfx_canvas_size(nullptr, &ch);
      gfx_clear_display();
      update_starfield();
      update_local_objects();
      hud_centre_text(ch / 2 - 10, "GAME OVER", 140, GFX_COL_GOLD);
      break;
    }
  }
}

int game_main(void)
{
  if (gfx_graphics_startup() == 1)
    return 1;

  /* Start the sound system... */
  snd_sound_startup();

  /* Do any setup necessary for the keyboard... */
  kbd_keyboard_startup();

  // Server-only client: single-player has been retired, so we always connect to
  // the authoritative server and render its world. The bind port and server
  // address can be overridden with DSO_BIND / DSO_SERVER (dotted-quad host);
  // they default to loopback for local play. If the socket fails to open,
  // ensure_connection keeps retrying and the flight screen shows the
  // connection-lost state - there is no offline simulation.
  {
    if (const char* b = getenv("DSO_BIND"))
      s_bindPort = static_cast<uint16_t>(atoi(b));

    int a = 127, c = 0, d = 0, e = 1;
    if (const char* host = getenv("DSO_SERVER"))
      sscanf(host, "%d.%d.%d.%d", &a, &c, &d, &e);

    s_serverEndpoint = Net::MakeEndpoint(static_cast<uint8_t>(a), static_cast<uint8_t>(c), static_cast<uint8_t>(d),
                                         static_cast<uint8_t>(e), 40000);
    ensure_connection();
    // LocalPlayer is learned from the server's HelloAck handshake reply; the
    // sentinel (unassigned) holds until then.
  }

  finish = 0;

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
