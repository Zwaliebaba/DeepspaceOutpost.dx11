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

#include "GamePalette.h"
#include "GameScene.h"
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
#include "Messages/Defs/ExplosionAt.h"       // ExplosionAt (G1 kill VFX)
#include "input/OrderMenu.h"                  // contextual orders + radial-menu legality (I3 core)
#include "input/MoveGizmo.h"                  // move-gizmo geometry (I3 core)
#include "input/Selection.h"                  // selected-unit set (input.md H2)
#include "input/MovePlan.h"                   // movement-grid state machine (input.md H6)
#include "GuiOverlay.h"
#include "GameWindows.h"
#include "ChartData.h"    // ChartData::Kind for the F5/F6/F7 chart overlay
#include "Scene3D.h"
#include "SceneGlow.h"
#include "Effects.h"                          // Neuron::Client::EffectsInstance() (explosion VFX)
#include "Camera.h"                           // MainCamera() (I3 move-order unprojection)
#include "input_win.h"                        // input_mouse_state (I3 pointer commands)
#include "GraphicsCore.h"                     // Graphics::Core::GetOutputSize (viewport size)

#include <DirectXMath.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <set>
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
 * hull. The ship moves by orders - the legacy roll/climb ramp and auto-centre
 * machinery went with the cockpit view, and the wire InputCommand carries no
 * flight axes at all (protocol v4). PlayerFlight() remains as presentation
 * state (the intro parade and game-over debris animate off its speed).
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

// Pending fire-missile intent + the locked target it launches at. Set by
// launch_missile(), published as a LaunchMissile ActionTriggered (and cleared) by
// send_player_input(); the subscriber turns it into a reliable AbilityRequest.
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
// entity under the cursor -> its default order (Attack/Dock/Collect/Approach) by
// OrderMenu::DefaultContextOrder; empty space -> the MOVE GIZMO (interaction.md
// §3.4). The order rides the reliable UnitOrder lane (I1); an optimistic toast
// shows what was asked and a rejecting UnitOrderAck flashes it red. Camera orbit
// moved to LMB-drag (CameraRig) so RMB is free for commands.
//
// The full model now (the classification / geometry / legality lives in the
// headless-tested NeuronClient/input cores; this file is the DX11 glue):
//   * MOVE GIZMO - RMB press on empty space drops a marker where the cursor ray
//     meets the CAMERA-UP command plane through the ship; a vertical RMB drag
//     before release slides it along the plane normal (the elevation stem);
//     release sends UnitOrder{Move}. A plain click is a zero-elevation move.
//   * RADIAL MENU - RMB HOLD >= ~0.35 s over an entity opens a radial of all the
//     legal orders for that target (OrderMenu::LegalOrders) + Info; drag to a
//     slice and release to issue it. This is where Attack on a CLEAN player lives
//     (the deliberate friction - a plain click on a clean player is Approach).
//   * ATTACK-FRICTION - DefaultContextOrder never returns Attack for a clean
//     player; the crime is still validated + owner-attributed server-side.
// The same long-press opens the menu on touch (I5 feeds this path).

// Optimistic order feedback, drawn by space.cpp's display_order_feedback().
unsigned int g_order_kind = 0;           // active order's OrderKind (0 = none)
bool         g_order_has_point = false;  // the order carries a world point (Move)
long long    g_order_point[3] = {0, 0, 0};
char         g_order_toast[40] = {0};
int          g_order_toast_timer = 0;    // frames the toast stays up
int          g_order_toast_col = 0;

// Move-gizmo live state (interaction.md §3.4), read by space.cpp's draw_move_gizmo().
// All points are in the render (origin-relative) frame so the projector draws them
// directly. Active only during an in-progress RMB move drag.
bool         g_gizmo_active = false;
double       g_gizmo_ship[3]   = {0, 0, 0};   // plane origin (ship)
double       g_gizmo_normal[3] = {0, 1, 0};   // plane normal (camera up)
double       g_gizmo_base[3]   = {0, 0, 0};   // in-plane point (elevation 0) - stem foot
double       g_gizmo_point[3]  = {0, 0, 0};   // marker (base + elevation) - stem head
double       g_gizmo_scale     = 1.0;         // world units per screen pixel at the marker depth

// Radial context menu state (interaction.md §3.3), read by space.cpp's
// draw_radial_menu(). Option screen positions are precomputed at open so the
// hit-test (here) and the render (space.cpp) never diverge.
bool         g_radial_open = false;
int          g_radial_count = 0;
int          g_radial_hot = -1;               // highlighted slice (-1 = none/cancel)
int          g_radial_cx[Neuron::Input::MAX_MENU_OPTIONS] = {0};
int          g_radial_cy[Neuron::Input::MAX_MENU_OPTIONS] = {0};
const char*  g_radial_labels[Neuron::Input::MAX_MENU_OPTIONS] = {nullptr};

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

// Radial menu internals that space.cpp does not need to read.
namespace
{
  unsigned int          s_radial_target = 0xFFFFFFFFu;
  Neuron::Msg::OrderKind s_radial_orders[Neuron::Input::MAX_MENU_OPTIONS] = {};
  bool                  s_radial_isinfo[Neuron::Input::MAX_MENU_OPTIONS] = {};
}

// Unproject the cursor to a world RAY in the render (origin-relative) frame: the
// same View*Projection inverse the retired cursor_to_move_point used, but split so
// the plane math lives in the headless-tested MoveGizmo core.
static bool cursor_ray(int _mx, int _my, Neuron::Input::GVec3& _ro, Neuron::Input::GVec3& _rd)
{
  const auto sz = Neuron::Graphics::Core::GetOutputSize();
  const int vw = static_cast<int>(sz.Width);
  const int vh = static_cast<int>(sz.Height);
  if (vw <= 0 || vh <= 0)
    return false;

  using namespace DirectX;
  Client::Camera& cam = Client::MainCamera();
  const XMMATRIX invVP = XMMatrixInverse(nullptr, XMMatrixMultiply(cam.View(), cam.Projection()));

  const float ndcx = 2.0f * static_cast<float>(_mx) / static_cast<float>(vw) - 1.0f;
  const float ndcy = 1.0f - 2.0f * static_cast<float>(_my) / static_cast<float>(vh);
  XMVECTOR pNear = XMVector4Transform(XMVectorSet(ndcx, ndcy, 0.0f, 1.0f), invVP);
  XMVECTOR pFar  = XMVector4Transform(XMVectorSet(ndcx, ndcy, 1.0f, 1.0f), invVP);
  pNear = XMVectorScale(pNear, 1.0f / XMVectorGetW(pNear));
  pFar  = XMVectorScale(pFar,  1.0f / XMVectorGetW(pFar));

  _ro = { XMVectorGetX(pNear), XMVectorGetY(pNear), XMVectorGetZ(pNear) };
  const XMVECTOR d = XMVectorSubtract(pFar, pNear);
  _rd = { XMVectorGetX(d), XMVectorGetY(d), XMVectorGetZ(d) };
  return true;
}

// The player's own ship position in the render (origin-relative) frame.
static bool ship_relative(Neuron::Input::GVec3& _out)
{
  Client::ReplicationClient& rc = Client::ReplicationClientInstance();
  Neuron::Net::EntitySnapshot me{};
  if (!rc.IsOpen() || !rc.Sample(rc.LocalPlayer(), 1.0, me))
    return false;
  const long long* org = camera_rig_origin();
  _out = { static_cast<double>(me.x) - static_cast<double>(org[0]),
           static_cast<double>(me.y) - static_cast<double>(org[1]),
           static_cast<double>(me.z) - static_cast<double>(org[2]) };
  return true;
}

// Begin a move gizmo at cursor (mx,my): fix the command plane and its in-plane
// base point, and derive the elevation-per-pixel scale from the base's depth so a
// vertical drag tracks the pointer 1:1 on screen. False if the ship is not
// currently sampleable. The plane is the Homeworld tactical grid: HORIZONTAL,
// normal = world +Y through the ship (input.md C6/H6) - so a destination reads
// the same however the camera is orbited, and a Shift/second-finger drag along
// the normal IS the Y-axis (altitude) modification.
static bool gizmo_begin(int _mx, int _my)
{
  using namespace Neuron::Input;
  GVec3 ro, rd, ship;
  const GVec3 up{0.0, 1.0, 0.0};
  if (!cursor_ray(_mx, _my, ro, rd) || !ship_relative(ship))
    return false;

  const PlaneHit hit = RayPlanePoint(ro, rd, ship, up);
  g_gizmo_ship[0] = ship.x;   g_gizmo_ship[1] = ship.y;   g_gizmo_ship[2] = ship.z;
  g_gizmo_normal[0] = up.x;   g_gizmo_normal[1] = up.y;   g_gizmo_normal[2] = up.z;
  g_gizmo_base[0] = hit.point.x; g_gizmo_base[1] = hit.point.y; g_gizmo_base[2] = hit.point.z;

  struct vector bp; bp.x = hit.point.x; bp.y = hit.point.y; bp.z = hit.point.z;
  camera_view_point(&bp);
  const auto sz = Neuron::Graphics::Core::GetOutputSize();
  const double focal = Neuron::Client::CameraFocalPixels(Client::MainCamera(),
                                                         static_cast<float>(sz.Height));
  g_gizmo_scale = (bp.z > 1.0 && focal > 1.0) ? bp.z / focal : 50.0;
  return true;
}

// Apply an elevation (world units, along the plane normal) to the gizmo base, clamp
// to the server Move reach, update the marker for rendering, and return the ABSOLUTE
// world point for the order.
static void gizmo_apply(double _elev, long long _out[3])
{
  using namespace Neuron::Input;
  const GVec3 base{g_gizmo_base[0], g_gizmo_base[1], g_gizmo_base[2]};
  const GVec3 up  {g_gizmo_normal[0], g_gizmo_normal[1], g_gizmo_normal[2]};
  const GVec3 ship{g_gizmo_ship[0], g_gizmo_ship[1], g_gizmo_ship[2]};
  const GVec3 clamped = ClampReach(ApplyElevation(base, up, _elev), ship);

  g_gizmo_point[0] = clamped.x; g_gizmo_point[1] = clamped.y; g_gizmo_point[2] = clamped.z;
  const long long* org = camera_rig_origin();
  _out[0] = static_cast<long long>(clamped.x + static_cast<double>(org[0]));
  _out[1] = static_cast<long long>(clamped.y + static_cast<double>(org[1]));
  _out[2] = static_cast<long long>(clamped.z + static_cast<double>(org[2]));
}

// Map a replicated entity's net type to the OrderMenu EntityClass (the raw-type
// switch stays here; the semantics live in the tested core).
static Neuron::Input::EntityClass entity_class_of(int _netType)
{
  using EC = Neuron::Input::EntityClass;
  if (_netType == SHIP_PLANET)                              return EC::Planet;
  if (_netType == SHIP_CORIOLIS || _netType == SHIP_DODEC)  return EC::Station;
  if (_netType == SHIP_CARGO)                               return EC::Canister;
  if (_netType < 0)                                         return EC::Sun;   // sun/object
  return EC::Ship;
}

// Look up whether a picked entity is a known player and its wanted level (the roster
// join). Defined after g_playerRoster.
static bool pick_player_wanted(unsigned int _id, int& _wanted);

// Classify a pick into a command TargetKind (empty when nothing is hit).
static Neuron::Input::TargetKind classify_pick(unsigned int _tgt, int _netType)
{
  using namespace Neuron::Input;
  if (_tgt == 0xFFFFFFFFu)
    return TargetKind::Empty;
  const EntityClass cls = entity_class_of(_netType);
  bool isPlayer = false; int wanted = 0;
  if (cls == EntityClass::Ship)
    isPlayer = pick_player_wanted(_tgt, wanted);
  // Pre-F1 the only owned unit is your own ship, which picking already excludes,
  // so isSelf / isOwnUnit are both false here.
  return ClassifyTarget(cls, /*isSelf*/ false, /*isOwnUnit*/ false, isPlayer, wanted > 0);
}

// Send a UnitOrder and set the optimistic feedback. `_pt` is the absolute world
// point for Move (nullptr otherwise).
static void send_order(Neuron::Msg::OrderKind _kind, unsigned int _target, const long long* _pt)
{
  Client::ReplicationClient& rc = Client::ReplicationClientInstance();
  const unsigned int self = rc.LocalPlayer();
  if (!rc.IsOpen() || self == 0xFFFFFFFFu)
    return;

  Neuron::Msg::UnitOrder ord;
  ord.unitId = self;
  ord.order  = _kind;
  ord.target = _target;
  if (_kind == Neuron::Msg::OrderKind::Move && _pt)
  {
    ord.targetX = _pt[0]; ord.targetY = _pt[1]; ord.targetZ = _pt[2];
    g_order_has_point = true;
    g_order_point[0] = _pt[0]; g_order_point[1] = _pt[1]; g_order_point[2] = _pt[2];
  }
  else
  {
    g_order_has_point = false;
  }

  g_order_kind = static_cast<unsigned int>(_kind);
  rc.SendUnitOrder(ord);
  set_order_toast(order_kind_name(g_order_kind), GFX_COL_YELLOW_2);   // optimistic
}

// Fly-to-and-dock, issued from the station hub's "Dock" button when the ship is out
// in space (not static: the docked-hub UI in GameWindows.cpp binds it). Prefers the
// currently selected station and issues the OrderKind::Dock autopilot (the ship flies
// to the station and docks on arrival); if nothing suitable is selected it falls back
// to request_dock(), which docks at the nearest station once within range.
void dock_station(void)
{
  if (g_missile_lock_target != 0xFFFFFFFFu)
  {
    Neuron::Net::EntitySnapshot ts{};
    if (Client::ReplicationClientInstance().Sample(g_missile_lock_target, 1.0, ts) &&
        classify_pick(g_missile_lock_target, ts.type) == Neuron::Input::TargetKind::Station)
    {
      send_order(Neuron::Msg::OrderKind::Dock, g_missile_lock_target, nullptr);
      return;
    }
  }
  request_dock();
}

// Issue the contextual default order for an RMB click at (mx,my): an entity under
// the cursor gets its OrderMenu default (clean players resolve to Approach - the
// friction); empty space is a zero-elevation move.
static void dispatch_context_order(int _mx, int _my)
{
  const unsigned int tgt = pick_entity_at_screen(_mx, _my);
  if (tgt != 0xFFFFFFFFu)
  {
    Neuron::Net::EntitySnapshot ts{};
    if (!Client::ReplicationClientInstance().Sample(tgt, 1.0, ts))
      return;
    const Neuron::Input::TargetKind tk = classify_pick(tgt, ts.type);
    g_missile_lock_target = tgt;   // command-as-you-select: reticle + orbit follow it
    send_order(Neuron::Input::DefaultContextOrder(tk), tgt, nullptr);
  }
  else
  {
    if (!gizmo_begin(_mx, _my))
      return;
    long long pt[3];
    gizmo_apply(0.0, pt);
    send_order(Neuron::Msg::OrderKind::Move, 0xFFFFFFFFu, pt);
  }
}

// The radial slice count / anchor is laid out at open; a slice center sits on a
// ring around the anchor, first at the top then clockwise.
static void radial_layout(int _ax, int _ay, int _count)
{
  const double R = 72.0;
  for (int i = 0; i < _count; ++i)
  {
    const double a = -1.5707963 + (2.0 * 3.14159265 * i) / static_cast<double>(_count);
    g_radial_cx[i] = _ax + static_cast<int>(R * std::cos(a));
    g_radial_cy[i] = _ay + static_cast<int>(R * std::sin(a));
  }
}

// The radial slice under (mx,my), or -1 (center / outside = cancel).
static int radial_slice_at(int _mx, int _my)
{
  for (int i = 0; i < g_radial_count; ++i)
  {
    const int dx = _mx - g_radial_cx[i];
    const int dy = _my - g_radial_cy[i];
    if (dx * dx + dy * dy <= 32 * 32)
      return i;
  }
  return -1;
}

// Open the radial context menu for whatever is under (mx,my). Empty space has no
// menu (it is the move gizmo), so this no-ops there.
static void open_radial_menu(int _mx, int _my)
{
  const unsigned int tgt = pick_entity_at_screen(_mx, _my);
  Neuron::Input::TargetKind tk = Neuron::Input::TargetKind::Empty;
  s_radial_target = 0xFFFFFFFFu;
  if (tgt != 0xFFFFFFFFu)
  {
    Neuron::Net::EntitySnapshot ts{};
    if (Client::ReplicationClientInstance().Sample(tgt, 1.0, ts))
    {
      tk = classify_pick(tgt, ts.type);
      s_radial_target = tgt;
      g_missile_lock_target = tgt;   // opening a menu also selects the target
    }
  }
  if (tk == Neuron::Input::TargetKind::Empty)
    return;

  const Neuron::Input::MenuOptions opts = Neuron::Input::LegalOrders(tk);
  g_radial_count = static_cast<int>(opts.count);
  for (int i = 0; i < g_radial_count; ++i)
  {
    g_radial_labels[i] = opts.items[i].label;
    s_radial_orders[i] = opts.items[i].order;
    s_radial_isinfo[i] = opts.items[i].isInfo;
  }
  radial_layout(_mx, _my, g_radial_count);
  g_radial_hot = -1;
  g_radial_open = true;
}

// Issue the highlighted slice's order and close the menu (Info just keeps the
// selection, whose card is already shown).
static void radial_commit(void)
{
  if (g_radial_hot >= 0 && g_radial_hot < g_radial_count && !s_radial_isinfo[g_radial_hot])
    send_order(s_radial_orders[g_radial_hot], s_radial_target, nullptr);
  g_radial_open = false;
  g_radial_hot = -1;
}

// ---- H2/H5/H6 selection, focus, band-select, movement grid (input.md) ----------

namespace
{
  // H2: the selected-unit SET. g_missile_lock_target stays as the DERIVED enemy
  // handle (reticle + missile target) so its many readers are untouched; s_selection
  // is the set the band fills and the command grammar consults.
  Neuron::Input::Selection s_selection;

  bool IsOwnUnit(unsigned int _id)
  {
    return _id != 0xFFFFFFFFu
        && _id == Client::ReplicationClientInstance().LocalPlayer();
  }

  // H6: the movement-grid state. g_gridMode is the two-step machine (MovePlan.h);
  // s_gridElev is the accumulated Y offset; s_gridAnchorY is the pointer y when the
  // vertical modifier (Shift) engaged.
  Neuron::Input::GridMode g_gridMode = Neuron::Input::GridMode::Off;
  double s_gridElev = 0.0;
  int    s_gridAnchorY = 0;
}

// H5 band-select rectangle (drawn by draw_selection_band, space.cpp).
bool g_band_active = false;
int  g_band_x0 = 0, g_band_y0 = 0, g_band_x1 = 0, g_band_y1 = 0;

// The world point the F-key / double-tap focus targets: the selected entity if any,
// else the own ship. False if nothing is sampleable. Called by the camera rig.
bool SelectionFocusWorld(double _out[3])
{
  Client::ReplicationClient& rc = Client::ReplicationClientInstance();
  const unsigned int id = (g_missile_lock_target != 0xFFFFFFFFu)
                        ? g_missile_lock_target : rc.LocalPlayer();
  Neuron::Net::EntitySnapshot s{};
  if (!rc.IsOpen() || !rc.Sample(id, rc.InterpolationAlpha(), s))
    return false;
  _out[0] = static_cast<double>(s.x);
  _out[1] = static_cast<double>(s.y);
  _out[2] = static_cast<double>(s.z);
  return true;
}

// Clear all pointer-command state. Called from the camera rig's reset seam so
// nothing survives a scene change (input.md §6.2).
void ResetCommandState(void)
{
  s_selection.Clear();
  g_missile_lock_target = 0xFFFFFFFFu;
  g_gridMode = Neuron::Input::GridMode::Off;
  g_gizmo_active = false;
  g_band_active = false;
}

// The single gate the flight command verbs consult (input.md §6.5): world in
// front, not docked, no modal window. (s_state == Flight is guaranteed by the
// call sites, which run only in the flight update.)
static bool FlightInputActive(void)
{
  return !docked && current_screen == SCR_FRONT_VIEW && !GuiOverlay::IsShown();
}

// Re-place the grid's in-plane (X/Z) point from the cursor ray against the FIXED
// horizontal plane captured at grid open (g_gizmo_ship + world-up normal).
static void grid_replace_xz(int _mx, int _my)
{
  using namespace Neuron::Input;
  GVec3 ro, rd;
  if (!cursor_ray(_mx, _my, ro, rd))
    return;
  const GVec3 ship{g_gizmo_ship[0], g_gizmo_ship[1], g_gizmo_ship[2]};
  const GVec3 up{g_gizmo_normal[0], g_gizmo_normal[1], g_gizmo_normal[2]};
  const PlaneHit hit = RayPlanePoint(ro, rd, ship, up);
  g_gizmo_base[0] = hit.point.x; g_gizmo_base[1] = hit.point.y; g_gizmo_base[2] = hit.point.z;
}

// H6: the M-key persistent movement grid. Returns true while the grid owns pointer
// input (so the caller skips the RMB machine and selection). The transitions run
// through the headless-tested StepGrid core; this glue does the geometry + order.
static bool HandleMovementGrid(int _mx, int _my)
{
  using namespace Neuron::Input;
  Client::ReplicationClient& rc = Client::ReplicationClientInstance();

  static bool s_prevM = false;
  const bool mDown = input_key_down('M');
  const bool mEdge = mDown && !s_prevM;
  s_prevM = mDown;

  if (g_gridMode == GridMode::Off)
  {
    // M spawns the horizontal tactical grid over the cursor (RMB-on-empty enters
    // via the one-gesture fast path in handle_pointer_commands instead).
    if (mEdge && rc.IsOpen() && gizmo_begin(_mx, _my))
    {
      g_gridMode = GridMode::PlacingXZ;
      s_gridElev = 0.0;
      g_gizmo_active = true;   // the gizmo renders + suppresses RMB-rotate while open
    }
    return g_gridMode != GridMode::Off;
  }

  // Cancel on M-again / Esc / an RMB press edge; confirm on an LMB click.
  static bool s_prevRmbGrid = false;
  int mx = 0, my = 0; bool lmb = false, rmb = false, mmb = false;
  PointerInput::MouseState(mx, my, lmb, rmb, mmb);
  const bool rmbEdge = rmb && !s_prevRmbGrid;
  s_prevRmbGrid = rmb;

  const bool shift = input_key_down(VK_SHIFT);
  int cx = 0, cy = 0;
  bool has = false;
  GridEvent ev = GridEvent::Cancel;
  if (mEdge || input_key_down(VK_ESCAPE) || rmbEdge)
  { ev = GridEvent::Cancel; has = true; }
  else if (PointerInput::TakeClick(PointerButton::Left, cx, cy))
  { ev = GridEvent::Confirm; has = true; }
  else if (shift && g_gridMode == GridMode::PlacingXZ)
  { ev = GridEvent::ShiftDown; has = true; s_gridAnchorY = _my; }
  else if (!shift && g_gridMode == GridMode::AdjustingY)
  { ev = GridEvent::ShiftUp; has = true; }

  // Live marker: hover the X/Z point while placing; slide Y while adjusting. The
  // elevation is KEPT across a Shift release, so both modes preview base + elev.
  if (g_gridMode == GridMode::PlacingXZ)
    grid_replace_xz(_mx, _my);
  else // AdjustingY: dragging UP (smaller y) raises the marker
    s_gridElev = static_cast<double>(s_gridAnchorY - _my) * g_gizmo_scale;

  long long pt[3];
  gizmo_apply(s_gridElev, pt);

  if (has)
  {
    const GridTransition tr = StepGrid(g_gridMode, ev, /*hasOwnUnit*/ rc.IsOpen());
    g_gridMode = tr.next;
    if (tr.confirm)
    {
      gizmo_apply(s_gridElev, pt);
      send_order(Neuron::Msg::OrderKind::Move, 0xFFFFFFFFu, pt);
    }
    if (tr.confirm || tr.cancel)
      g_gizmo_active = false;
  }
  return true;   // the grid owns pointer input while active
}

// Select every OWN unit whose projected position lands inside the band rectangle.
// Pre-F1 the only own unit is the primary ship, so this is degenerate but exercises
// the multi-select mechanism (input.md H5).
static void band_select(int _x0, int _y0, int _x1, int _y1)
{
  const int lox = _x0 < _x1 ? _x0 : _x1, hix = _x0 < _x1 ? _x1 : _x0;
  const int loy = _y0 < _y1 ? _y0 : _y1, hiy = _y0 < _y1 ? _y1 : _y0;

  ScreenEntity ents[64];
  const std::size_t n = ProjectEntitiesToScreen(ents, 64);
  s_selection.Clear();
  for (std::size_t i = 0; i < n; ++i)
  {
    if (!IsOwnUnit(ents[i].id))
      continue;
    if (ents[i].sx >= lox && ents[i].sx <= hix && ents[i].sy >= loy && ents[i].sy <= hiy)
      s_selection.Add(ents[i].id);
  }
}

// H2/H5: LMB click selects the entity under the cursor (empty space clears); an LMB
// drag rubber-bands and selects the own units inside. Runs after the camera and the
// command handler; a press that began on the ability/nav bar is theirs, and the grid
// (when active) owns input instead.
void handle_selection(void)
{
  if (!FlightInputActive() || g_gridMode != Neuron::Input::GridMode::Off)
  {
    g_band_active = false;
    return;
  }

  int mx = 0, my = 0; bool lmb = false, rmb = false, mmb = false;
  PointerInput::MouseState(mx, my, lmb, rmb, mmb);

  static bool s_prevLmb = false;
  static int  s_downX = 0, s_downY = 0;
  static bool s_downOnBar = false;
  if (lmb && !s_prevLmb)
  {
    s_downX = mx; s_downY = my;
    s_downOnBar = ability_bar_button_at(mx, my) >= 0 || nav_strip_button_at(mx, my) >= 0;
  }

  // Band: an active LMB drag not begun on a bar draws the rubber-band rectangle.
  float dx = 0.f, dy = 0.f;
  const bool dragging = PointerInput::DragState(PointerButton::Left, dx, dy);
  if (dragging && !s_downOnBar)
  {
    g_band_active = true;
    g_band_x0 = s_downX; g_band_y0 = s_downY;
    g_band_x1 = mx;      g_band_y1 = my;
  }

  // Band release -> select the own units inside; then drop the rectangle.
  if (!lmb && s_prevLmb && g_band_active)
  {
    band_select(g_band_x0, g_band_y0, g_band_x1, g_band_y1);
    g_band_active = false;
  }

  // Click select (a press+release within slop): the entity under the cursor, else
  // clear. The reticle / missile target is the selected ENEMY (never the own hull).
  int cx = 0, cy = 0;
  if (PointerInput::TakeClick(PointerButton::Left, cx, cy))
  {
    if (ability_bar_button_at(cx, cy) < 0 && nav_strip_button_at(cx, cy) < 0)
    {
      const unsigned int t = pick_entity_at_screen(cx, cy);
      s_selection.Set(t);
      g_missile_lock_target = (t != 0xFFFFFFFFu && !IsOwnUnit(t)) ? t : 0xFFFFFFFFu;
    }
  }

  s_prevLmb = lmb;
}

// Per-frame pointer-command polling (called from the flight update, after the
// camera). RMB drives the command grammar: a click issues the contextual default,
// a HOLD over an entity opens the radial menu, and a press on empty space is the
// move gizmo (vertical drag = elevation). The M-key persistent movement grid takes
// precedence when open. Selection (LMB) is handled by handle_selection after this.
void handle_pointer_commands(void)
{
  input_pointer_tick();   // drive the recognizers' long-press timers every frame

  if (!FlightInputActive())
  {
    g_gizmo_active = false;
    g_radial_open  = false;
    if (g_gridMode != Neuron::Input::GridMode::Off)
      g_gridMode = Neuron::Input::GridMode::Off;
    return;
  }

  int mx = 0, my = 0;
  bool lmb = false, rmb = false, mmb = false;
  PointerInput::MouseState(mx, my, lmb, rmb, mmb);

  // The M-key movement grid owns pointer input whenever it is open.
  if (HandleMovementGrid(mx, my))
    return;

  // I5 touch gestures. A LONG-PRESS opens the radial menu (touch equivalent of the
  // RMB-hold); the finger then drives the highlight and lifting commits. A
  // DOUBLE-TAP selects and focuses the camera on whatever is under it.
  static bool s_touchRadial = false;
  int gx = 0, gy = 0;
  if (input_take_double_tap(gx, gy))
  {
    const unsigned int t = pick_entity_at_screen(gx, gy);
    if (t != 0xFFFFFFFFu)
    {
      s_selection.Set(t);
      g_missile_lock_target = (!IsOwnUnit(t)) ? t : 0xFFFFFFFFu;
    }
    camera_rig_focus();   // double-tap = focus (input.md §3.2)
  }
  if (input_take_long_press(gx, gy))
  {
    open_radial_menu(gx, gy);
    s_touchRadial = g_radial_open;
  }
  if (s_touchRadial)
  {
    if (g_radial_open && input_touch_count() > 0)
    {
      g_radial_hot = radial_slice_at(mx, my);
      return;   // the touch radial owns this frame
    }
    if (g_radial_open)
      radial_commit();   // finger lifted -> issue the highlighted slice
    s_touchRadial = false;
    return;
  }

  static bool s_prevRmb = false;
  static int  s_downX = 0, s_downY = 0;
  static bool s_moved = false;
  static int  s_downFrames = 0;
  static bool s_overEntity = false;   // press began over an entity (menu) vs empty (gizmo)

  constexpr int LONGPRESS_FRAMES = 11; // ~0.35 s at the 30 Hz command tick
  constexpr int SLOP = 6;

  // The LMB+RMB pan chord is a camera pan, not a command: hide RMB from the command
  // machine while both buttons are held so it never arms a gizmo/menu (input.md H4).
  rmb = rmb && !lmb;

  if (rmb && !s_prevRmb)
  {
    s_downX = mx; s_downY = my; s_moved = false; s_downFrames = 0;
    g_radial_open = false;
    s_overEntity = (pick_entity_at_screen(mx, my) != 0xFFFFFFFFu);
  }
  else if (rmb)   // held
  {
    ++s_downFrames;
    int ddx = mx - s_downX; if (ddx < 0) ddx = -ddx;
    int ddy = my - s_downY; if (ddy < 0) ddy = -ddy;
    if (ddx > SLOP || ddy > SLOP) s_moved = true;

    // A stationary hold over an entity opens the radial menu; a moved RMB is a
    // camera rotate (the rig owns it while no gizmo/menu is up).
    if (g_radial_open)
      g_radial_hot = radial_slice_at(mx, my);
    else if (s_overEntity && s_downFrames >= LONGPRESS_FRAMES && !s_moved)
      open_radial_menu(s_downX, s_downY);
  }
  else if (s_prevRmb)   // release
  {
    if (g_radial_open)
    {
      radial_commit();
    }
    else if (!s_moved)
    {
      // A plain RMB click (no drag): an entity issues its contextual default order;
      // empty space opens the movement grid (input.md §0.1) - the same grid the M
      // key opens, where Shift then sets the altitude.
      if (s_overEntity)
      {
        dispatch_context_order(mx, my);
      }
      else if (gizmo_begin(mx, my))
      {
        g_gridMode = Neuron::Input::GridMode::PlacingXZ;
        s_gridElev = 0.0;
        g_gizmo_active = true;
      }
    }
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

// ---- I4 screen-nav strip (interaction.md §3.8): screens reachable by pointer -----
//
// A compact strip of screen icons in the top-RIGHT of the flight view - Chart /
// Status / Inventory - so the F-key screens are one tap away without the keyboard.
// Same non-modal discipline as the ability bar: it does not raise GuiOverlay; the
// camera's select ignores a click that began on it (nav_strip_button_at), and a
// click opens the matching GUI overlay window (which the F-keys still open too).
// Market/Equip stay on the docked station UI (they need docking), so the flight
// strip carries the always-available screens.

namespace
{
  enum NavAct { NAV_CHART, NAV_STATUS, NAV_INV, NAV_COUNT };
  struct NavButton { const char* label; int act; };
  const NavButton s_nav[NAV_COUNT] = {
    { "CHART", NAV_CHART }, { "STATUS", NAV_STATUS }, { "INV", NAV_INV },
  };
  constexpr int NAV_SLOT_W = 64;
  constexpr int NAV_SLOT_H = 20;
  constexpr int NAV_GAP    = 4;
  constexpr int NAV_TOP_Y  = 8;
  constexpr int NAV_MARGIN = 8;   // gap from the right edge

  int s_nav_down_btn = -1;
}

static int nav_strip_origin_x(int _vw)
{
  const int total = NAV_COUNT * NAV_SLOT_W + (NAV_COUNT - 1) * NAV_GAP;
  int x0 = _vw - NAV_MARGIN - total;
  if (x0 < 4) x0 = 4;
  return x0;
}

// The nav button under (mx,my), or -1. Exposed so the camera's select ignores a
// click that landed on the strip. Flight view only.
int nav_strip_button_at(int _mx, int _my)
{
  if (GuiOverlay::IsShown() || current_screen != SCR_FRONT_VIEW || docked)
    return -1;
  const int vw = static_cast<int>(Neuron::Graphics::Core::GetOutputSize().Width);
  const int x0 = nav_strip_origin_x(vw);
  for (int i = 0; i < NAV_COUNT; ++i)
  {
    const int x = x0 + i * (NAV_SLOT_W + NAV_GAP);
    if (_mx >= x && _mx < x + NAV_SLOT_W && _my >= NAV_TOP_Y && _my < NAV_TOP_Y + NAV_SLOT_H)
      return i;
  }
  return -1;
}

static void nav_trigger(int _act)
{
  switch (_act)
  {
    case NAV_CHART:  OpenChartWindow(ChartData::GALACTIC); break;
    case NAV_STATUS: OpenCommanderWindow(); break;
    case NAV_INV:    OpenInventoryWindow(); break;
    default: break;
  }
}

void draw_nav_strip(void)
{
  if (GuiOverlay::IsShown() || current_screen != SCR_FRONT_VIEW || docked)
    return;

  const int vw = static_cast<int>(Neuron::Graphics::Core::GetOutputSize().Width);
  hud_set_origin(0, 0);
  const int x0 = nav_strip_origin_x(vw);
  for (int i = 0; i < NAV_COUNT; ++i)
  {
    const int x = x0 + i * (NAV_SLOT_W + NAV_GAP);
    hud_rect(x, NAV_TOP_Y, x + NAV_SLOT_W, NAV_TOP_Y + NAV_SLOT_H, GFX_COL_GREY_1);
    const int len = static_cast<int>(strlen(s_nav[i].label));
    hud_text(x + (NAV_SLOT_W - len * 8) / 2, NAV_TOP_Y + 6, s_nav[i].label, GFX_COL_CYAN);
  }
}

// Per-frame nav-strip input: an LMB press-release on the same button opens its
// screen. Runs before the camera (like the ability bar) so a strip click is
// consumed, not treated as a world select.
void handle_nav_strip(void)
{
  if (GuiOverlay::IsShown() || current_screen != SCR_FRONT_VIEW || docked)
  {
    s_nav_down_btn = -1;
    return;
  }

  int mx = 0, my = 0;
  bool lmb = false, rmb = false;
  input_mouse_state(mx, my, lmb, rmb);

  static bool s_prevLmb = false;
  if (lmb && !s_prevLmb)
  {
    s_nav_down_btn = nav_strip_button_at(mx, my);
  }
  else if (!lmb && s_prevLmb && s_nav_down_btn >= 0)
  {
    if (nav_strip_button_at(mx, my) == s_nav_down_btn)
      nav_trigger(s_nav[s_nav_down_btn].act);
    s_nav_down_btn = -1;
  }
  s_prevLmb = lmb;
}

// ---- G3 chat: a rate-limited relay with a client-side mute list -----------------
//
// The server rate-limits + sanitises + stamps the sender (playerId); the client
// keeps a small scrollback, a per-playerId mute set, and a one-line input opened
// with Enter. "/mute <id>" / "/unmute <id>" manage the mute set locally (the
// server-persisted mute list is a B4 follow-up).

namespace
{
  std::deque<std::string>                   g_chatLog;      // recent lines (name: text)
  std::unordered_map<uint32_t, std::string> g_chatNames;    // playerId -> name (from PlayerInfo)
  std::set<uint32_t>                        g_chatMutes;     // muted playerIds
  bool                                      g_chatInputActive = false;
  std::string                               g_chatInput;
  constexpr std::size_t CHAT_SCROLLBACK = 8;
}

const char* chat_name_for(uint32_t _pid)
{
  if (_pid == 0) return "SYSTEM";
  const auto it = g_chatNames.find(_pid);
  return it == g_chatNames.end() ? "???" : it->second.c_str();
}

static void chat_submit(const std::string& _s)
{
  if (_s.empty())
    return;
  // Local mute commands never hit the wire.
  if (_s.rfind("/mute ", 0) == 0)
  {
    const uint32_t pid = static_cast<uint32_t>(atoi(_s.c_str() + 6));
    if (pid != 0) g_chatMutes.insert(pid);
    return;
  }
  if (_s.rfind("/unmute ", 0) == 0)
  {
    const uint32_t pid = static_cast<uint32_t>(atoi(_s.c_str() + 8));
    if (pid != 0) g_chatMutes.erase(pid);
    return;
  }
  Neuron::Msg::Chat msg;
  msg.sender = 0;   // the server stamps the authenticated sender; this is ignored
  msg.text = _s;
  Client::ReplicationClientInstance().Send(msg);
}

// Per-frame chat text entry: drain the WM_CHAR ring. Enter opens the input (or
// submits it when open); Backspace edits; printable chars append while open.
void handle_chat_input(void)
{
  if (GuiOverlay::IsShown() || current_screen != SCR_FRONT_VIEW || docked)
  {
    g_chatInputActive = false;
    return;
  }

  for (;;)
  {
    const int ch = kbd_read_key();
    if (kbd_enter_pressed)
    {
      kbd_enter_pressed = 0;
      if (!g_chatInputActive) { g_chatInputActive = true; g_chatInput.clear(); }
      else { chat_submit(g_chatInput); g_chatInput.clear(); g_chatInputActive = false; }
      continue;
    }
    if (kbd_backspace_pressed)
    {
      kbd_backspace_pressed = 0;
      if (g_chatInputActive && !g_chatInput.empty()) g_chatInput.pop_back();
      continue;
    }
    if (ch == 0)
      break;   // ring drained
    if (g_chatInputActive && ch >= 0x20 && ch < 0x7F && g_chatInput.size() < 160)
      g_chatInput.push_back(static_cast<char>(ch));
  }
}

// Draw the chat scrollback (and the input line while typing) bottom-left of the
// flight view. Called from the HUD pass.
void draw_chat(void)
{
  if (GuiOverlay::IsShown() || current_screen != SCR_FRONT_VIEW || docked)
    return;
  if (g_chatLog.empty() && !g_chatInputActive)
    return;

  hud_set_origin(0, 0);
  const int vh = static_cast<int>(Neuron::Graphics::Core::GetOutputSize().Height);
  int y = vh - 60 - static_cast<int>(g_chatLog.size()) * 11;
  for (const std::string& line : g_chatLog)
  {
    hud_text(12, y, line.c_str(), GFX_COL_GREY_1);
    y += 11;
  }
  if (g_chatInputActive)
  {
    char buf[200];
    snprintf(buf, sizeof(buf), "> %s_", g_chatInput.c_str());
    hud_text(12, vh - 46, buf, GFX_COL_YELLOW_2);
  }
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
  // No combat target while docked: the orbit camera (RMB-drag around the station)
  // centres on the player's own ship, not a stale lock from before docking.
  g_missile_lock_target = 0xFFFFFFFFu;
  OpenStationMenu();
}

// Enter live flight (and the docked menus); start docked at the station.
static void enter_flight(void)
{
  dock_player();
  enter_station();
  s_state = GameState::Flight;
}

// Death VFX helpers (defined below, next to the bus handlers that also use them).
static void emit_effect_burst(const Neuron::Math::Vector3i64& _pos, float _radius);
static DirectX::XMFLOAT3X3 identity_basis(void);
static float hull_radius(int _type);

// Enter the game-over animation: your hull shatters into tumbling debris while cargo
// wreckage drifts past, for 100 frames. The scene animates in camera space against the
// identity camera (camera_rig_reset), so the effects origin is pinned to zero and the
// wreck point {0,0,1000} sits ahead on +z. The debris shatter (EXPLOSION_LIFETIME ~ 3s)
// burns out just before the animation ends.
static void enter_game_over(void)
{
  current_screen = SCR_GAME_OVER;
  camera_rig_reset();
  gfx_set_clip_region(1, 1, 510, 383);

  PlayerFlight().speed = 6;   // presentation only: paces the cargo drift past the wreck
  PlayerFlight().roll = 0;
  PlayerFlight().climb = 0;
  clear_local_objects();

  Matrix rotmat;
  set_init_matrix(rotmat);

  // Your hull's death: the engine debris/particle effect at the wreck point (the legacy
  // FLG_DEAD pixel-spray cobra is retired - explosion.md decision 1). The rig was just
  // reset, so the origin is zero for the whole animation.
  {
    const Neuron::Math::Vector3i64 wreck{ 0, 0, 1000 };
    auto& fx = Neuron::Client::EffectsInstance();
    fx.SetOrigin(Neuron::Math::Vector3i64{ 0, 0, 0 });
    fx.AddExplosion(SHIP_COBRA3, wreck, identity_basis(), 1.0f);
    emit_effect_burst(wreck, hull_radius(SHIP_COBRA3));
  }

  for (int i = 0; i < 5; i++)
  {
    const int type = (rand255() & 1) ? SHIP_CARGO : SHIP_ALLOY;
    const int newship = add_new_ship(type, (rand255() & 63) - 32, (rand255() & 63) - 32, 1000, rotmat, 0, 0);
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

// I3 roster join: is this entity a known player, and if so how wanted? Used by the
// command classifier for clean-player Attack-friction and by the I2 info card.
static bool pick_player_wanted(unsigned int _id, int& _wanted)
{
  const auto it = g_playerRoster.find(_id);
  if (it == g_playerRoster.end())
    return false;
  _wanted = it->second.wanted;
  return true;
}

// The roster name for an entity, or nullptr if it is not a known player.
const char* roster_name(unsigned int _id)
{
  const auto it = g_playerRoster.find(_id);
  return it == g_playerRoster.end() ? nullptr : it->second.name.c_str();
}

// The wanted level for a known player entity, or -1 if it is not a known player.
int roster_wanted(unsigned int _id)
{
  const auto it = g_playerRoster.find(_id);
  return it == g_playerRoster.end() ? -1 : it->second.wanted;
}

// The hull's rough world radius for effect sizing: ship_data.size is the legacy SQUARED
// collision radius, so the radius is its square root. ~50 (a fighter) when out of range.
static float hull_radius(int _type)
{
  if (_type < 1 || _type > NO_OF_SHIPS || ship_list[_type] == nullptr)
    return 50.0f;
  const double sz = ship_list[_type]->size;
  return (sz > 1.0) ? static_cast<float>(sqrt(sz)) : 50.0f;
}

// Emit a short-lived additive-particle burst at an absolute world point - the fireball half
// of the death VFX (explosion.md). _radius (the hull's rough world radius) scales the count,
// sprite size and outward speeds, so a canister pops and a station erupts. Random outward
// velocities on the game-side PRNG (exe-side, so random.h is fine here). The engine subsystem
// integrates + draws it; the game only spawns.
static void emit_effect_burst(const Neuron::Math::Vector3i64& _pos, float _radius)
{
  using namespace DirectX;
  Neuron::Client::Effects& fx = Neuron::Client::EffectsInstance();

  const float rel = _radius / 50.0f;                 // 1.0 = fighter-sized baseline
  int count = static_cast<int>(24.0f * rel);
  count = (count < 12) ? 12 : ((count > 96) ? 96 : count);
  const float size = 150.0f * ((rel < 0.6f) ? 0.6f : ((rel > 2.5f) ? 2.5f : rel));
  const float speedScale = (rel < 0.7f) ? 0.7f : ((rel > 2.5f) ? 2.5f : rel);

  for (int i = 0; i < count; ++i)
  {
    const float x = (rand255() - 128) / 128.0f;
    const float y = (rand255() - 128) / 128.0f;
    const float z = (rand255() - 128) / 128.0f;
    XMVECTOR dir = XMVectorSet(x, y, z, 0.0f);
    if (XMVectorGetX(XMVector3LengthSq(dir)) < 1e-4f)
      dir = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // avoid a zero-length direction
    dir = XMVector3Normalize(dir);
    const float speed = (200.0f + (rand255() / 255.0f) * 300.0f) * speedScale;
    fx.CreateParticle(_pos, XMVectorScale(dir, speed), Neuron::Client::ParticleTypeId::ExplosionCore, size);
  }
}

static DirectX::XMFLOAT3X3 identity_basis(void)
{
  DirectX::XMFLOAT3X3 b;
  DirectX::XMStoreFloat3x3(&b, DirectX::XMMatrixIdentity());
  return b;
}

// The dying hull's world basis for the debris shatter, from its last snapshot's nose/roof
// (side = roof x nose - the BuildRenderRecords convention). Rows [side, roof, nose], the
// ModelDraw/RenderRecord row-vector layout the Effects subsystem expects.
static DirectX::XMFLOAT3X3 snapshot_basis(const Neuron::Net::EntitySnapshot& _s)
{
  using namespace DirectX;
  const XMVECTOR noseRaw = XMVectorSet(_s.noseX, _s.noseY, _s.noseZ, 0.0f);
  const XMVECTOR roofRaw = XMVectorSet(_s.roofX, _s.roofY, _s.roofZ, 0.0f);
  if (XMVectorGetX(XMVector3LengthSq(noseRaw)) < 1e-6f ||
      XMVectorGetX(XMVector3LengthSq(roofRaw)) < 1e-6f)
    return identity_basis();   // degenerate snapshot -> unrotated shatter
  const XMVECTOR nose = XMVector3Normalize(noseRaw);
  const XMVECTOR roof = XMVector3Normalize(roofRaw);
  const XMVECTOR side = XMVector3Cross(roof, nose);
  XMFLOAT3X3 b;
  XMStoreFloat3x3(&b, XMMATRIX(side, roof, nose, XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f)));
  return b;
}

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
    // Capture the dying ship's last position/type/orientation BEFORE forgetting it, so
    // the death is visible where it happened (the server just vanishes the entity):
    // shatter the hull mesh into tumbling debris + a fireball burst (explosion.md).
    Net::EntitySnapshot vs;
    if (rc.Sample(_death.victim, 1.0, vs))
    {
      const Neuron::Math::Vector3i64 at{ vs.x, vs.y, vs.z };
      if (vs.type >= 1 && vs.type <= NO_OF_SHIPS)   // real hulls only (not planet/sun)
        Neuron::Client::EffectsInstance().AddExplosion(vs.type, at, snapshot_basis(vs), 1.0f);
      emit_effect_burst(at, hull_radius(vs.type));
    }
    rc.Forget(_death.victim);
    snd_play_sample(SND_EXPLODE);
  });

  // G1: a world-anchored kill VFX (a player death the killer/bystanders should see;
  // the victim itself got a private EntityDeath and respawned elsewhere). Shatter a
  // fighter hull at the broadcast point (player hulls are Vipers; the message carries
  // no entity/orientation, so the debris mesh and basis are representative).
  g_clientBus.Subscribe<Neuron::Msg::ExplosionAt>([](const Neuron::Msg::ExplosionAt& _boom)
  {
    const Neuron::Math::Vector3i64 at{ _boom.x, _boom.y, _boom.z };
    const int scale = (_boom.scale > 0) ? ((_boom.scale < 4) ? _boom.scale : 4) : 1;
    Neuron::Client::EffectsInstance().AddExplosion(SHIP_VIPER, at, identity_basis(), 1.0f);
    emit_effect_burst(at, hull_radius(SHIP_VIPER) * static_cast<float>(scale));
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
    g_chatNames[_pi.playerId] = _pi.name;   // G3: chat is keyed by playerId, not hull
  });

  // G3 chat: a relayed line (sender = playerId, or 0 = system). Drop muted senders;
  // format "name: text" and push to the scrollback ring.
  g_clientBus.Subscribe<Neuron::Msg::Chat>([](const Neuron::Msg::Chat& _c)
  {
    if (_c.sender != 0 && g_chatMutes.count(_c.sender) != 0)
      return;   // muted
    char line[220];
    snprintf(line, sizeof(line), "%s: %s", chat_name_for(_c.sender), _c.text.c_str());
    g_chatLog.push_back(line);
    while (g_chatLog.size() > CHAT_SCROLLBACK)
      g_chatLog.pop_front();
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
    PlayerCaps().cabTemp = _ps.cabinTemp;        // cabin-temp dial (G4): sun-proximity heat
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

  // Ability dispatch: the input layer publishes WHAT the player did (a LocalOnly
  // ActionTriggered on the client bus); this subscriber maps it onto the reliable
  // AbilityRequest so a button press is never lost to datagram loss (roadmap #6 -
  // the one activation path). Movement is a UnitOrder; the per-frame InputCommand
  // is only the heartbeat/ack.
  g_clientBus.Subscribe<Neuron::Msg::ActionTriggered>([](const Neuron::Msg::ActionTriggered& _a)
  {
    Neuron::Msg::AbilityRequest req;
    switch (_a.action)
    {
      case Neuron::Msg::InputAction::LaunchMissile:
        req.kind = Neuron::Msg::AbilityKind::FireMissile;
        req.target = _a.param;
        break;
      case Neuron::Msg::InputAction::Ecm:
        req.kind = Neuron::Msg::AbilityKind::Ecm;
        break;
      case Neuron::Msg::InputAction::EnergyBomb:
        req.kind = Neuron::Msg::AbilityKind::EnergyBomb;
        break;
      case Neuron::Msg::InputAction::EscapePod:
        req.kind = Neuron::Msg::AbilityKind::EscapePod;
        break;
      default:
        return;   // Fire (the manual laser) is retired - attack is an order (I7)
    }
    Client::ReplicationClientInstance().SendAbility(req);
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
    Neuron::Msg::ExplosionAt boom;
    Neuron::Msg::Chat chat;

    if (Neuron::Msg::TryDecode(msg, resp))
      g_clientBus.Publish(resp);
    else if (Neuron::Msg::TryDecode(msg, oack))
      g_clientBus.Publish(oack);
    else if (Neuron::Msg::TryDecode(msg, boom))
      g_clientBus.Publish(boom);
    else if (Neuron::Msg::TryDecode(msg, chat))
      g_clientBus.Publish(chat);
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

// Send the player's per-frame heartbeat to the server. Piloting is retired (the
// player flies the CAMERA; the hull moves by orders) and the discrete abilities
// ride the reliable AbilityRequest, so the command carries only the sequence and
// the snapshot ack the delta stream depends on - the cadence must not stop (it is
// also the liveness signal that keeps the ship from safe-parking). Thin-client only.
static void send_player_input(void)
{
  static uint32_t seq = 0;

  // A latched missile intent (launch_missile) publishes this frame's LaunchMissile
  // action; the ActionTriggered subscriber sends the reliable AbilityRequest.
  register_client_event_handlers();
  if (s_fire_missile_intent)
  {
    g_clientBus.Publish(Neuron::Msg::ActionTriggered{ Neuron::Msg::InputAction::LaunchMissile, s_fire_missile_target });
    s_fire_missile_intent = false;
  }
  g_clientBus.Dispatch();

  Msg::InputCommand in;
  in.sequence = ++seq;
  Client::ReplicationClientInstance().SendInput(in);   // SendInput stamps ackSnapshotTick
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
  handle_nav_strip();          // I4: screen-nav strip clicks (same, top-right)
  handle_chat_input();         // G3: chat text entry (Enter opens/sends)

  camera_rig_update();

  // Hand the engine-owned effects subsystem this frame's floating origin (it can't reach the
  // game-side CameraRig). Its particles/debris rebase against this when their vertices are built
  // in ClientEngine::Frame's Advance, just after this update returns.
  {
    const long long* org = camera_rig_origin();
    Neuron::Client::EffectsInstance().SetOrigin(Neuron::Math::Vector3i64{ org[0], org[1], org[2] });
  }

  handle_pointer_commands();   // RMB contextual orders + the M-key movement grid
  handle_selection();          // H2/H5: LMB click-select + drag-band (after commands)

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
      // Advance on Space or a tap/click anywhere (input.md §3.8/§6.4); the tap one-
      // shot stays off the selection path (there is nothing to select in an intro).
      if (kbd_space_pressed || PointerInput::TakeAnyTap())
      {
        snd_stop_midi();
        enter_intro2();
      }
      break;

    case GameState::Intro2:
      kbd_poll_keyboard();
      if (kbd_space_pressed || PointerInput::TakeAnyTap())
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
  // The two H2/H4 render options, likewise refreshed each frame. Both default off, so the
  // proven per-model, no-glow path is unchanged unless the player opts in.
  Neuron::Graphics::Scene3D::SetInstancingEnabled(scene_instancing != 0);
  Neuron::Graphics::SceneGlow::SetEnabled(scene_glow != 0);

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
