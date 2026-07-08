/*
 * space.h
 *
 * The client's flight presentation: the local display-object pool, the
 * replicated-world renderer, the cockpit HUD, and the weapon/dock visuals.
 * Presentation only - all game rules live on the server.
 */

#ifndef SPACE_H
#define SPACE_H

#include <cstddef>

#include "vector.h"
#include "shipdata.h"

struct point
{
	int x;
	int y;
	int z;
};


struct local_object
{
	int type;
	Vector location;
	Matrix rotmat;
	int rotx;
	int rotz;
	int flags;
	int energy;
	int velocity;
	int acceleration;
	int missiles;
	int target;
	int bravery;
	int distance;
};

#define MAX_LOCAL_OBJECTS	20

/*
 * local_objects[i] is backed by the ECS (A2 flip): the Universe owns
 * MAX_LOCAL_OBJECTS permanent per-slot entities, each carrying a `local_object`
 * component. This proxy maps the legacy slot-index syntax onto them so the game
 * logic is unchanged (operator[] defined in LocalObjects.cpp). The slot pool is
 * pre-created and never grown, so element references and &local_objects[i]
 * pointers stay stable across a frame, exactly like the old array.
 */
class LocalObjectArray
{
public:
	struct local_object& operator[] (int slot);
};

extern LocalObjectArray local_objects;
extern int ship_count[NO_OF_SHIPS + 1];  /* many */

/* (Re)create the MAX_LOCAL_OBJECTS slot entities in the Universe. */
void create_local_object_slots (void);


/* The display-object pool (intro parade, game-over debris, replicated mirror). */
void clear_local_objects (void);
int add_new_ship (int ship_type, int x, int y, int z, struct vector *rotmat, int rotx, int rotz);
void remove_ship (int un);
void move_local_object (struct local_object *obj);
void update_local_objects (void);
void render_replicated_objects (void);
unsigned int pick_entity_at_screen (int mx, int my);   // I2: select the entity under the cursor

// H5 band-select (input.md): one replicated entity projected to screen pixels, and
// the projection of every entity (band and pick share the same optics). Defined in
// space.cpp.
struct ScreenEntity { unsigned int id; double sx; double sy; int type; };
std::size_t ProjectEntitiesToScreen (ScreenEntity *out, std::size_t cap);

// H5 band-select rectangle (state defined in main.cpp, drawn by draw_selection_band).
extern bool g_band_active;
extern int  g_band_x0, g_band_y0, g_band_x1, g_band_y1;
void draw_selection_band (void);

// I4 ability bar (defined in main.cpp): draw the flight ability strip, and report
// which bar button (if any) is under the cursor so the camera's select can ignore
// a click that landed on the bar.
void draw_ability_bar (void);
int  ability_bar_button_at (int mx, int my);

// I4 screen-nav icon strip (defined in main.cpp): a compact top-right strip that
// opens the chart / status / inventory windows by pointer (F-keys stay as
// accelerators). Like the ability bar it is non-modal, so the camera's select
// ignores a click that landed on it (nav_strip_button_at >= 0).
void draw_nav_strip (void);
int  nav_strip_button_at (int mx, int my);

// G3 chat: draw the scrollback + input line (defined in main.cpp), called from the
// HUD pass.
void draw_chat (void);

// Native flight-HUD primitives (defined in space.cpp). The cockpit dashboard and the
// I2/I3/I4 overlays draw straight into the Render2D pass RenderGameHud brackets during
// RenderCanvas, replacing the gfx2d deferred batch. Colours are palette indices (the
// GFX_COL_* macros); coordinates are offset by a floated draw origin. draw_ability_bar
// (main.cpp) uses these too, so they live in the shared header.
void hud_set_origin (int x, int y);
void hud_line (int x1, int y1, int x2, int y2, int col);
void hud_rect (int x1, int y1, int x2, int y2, int col);
void hud_text (int x, int y, const char *str, int col);

// Centred overlay text (intro titles/prompts, the flight info message, GAME OVER). Emitted
// from the RenderScene phase; hud_centre_text records the line and RenderOverlayText (called
// from RenderGameHud) draws them natively. Replaces gfx_display_centre_text.
void hud_centre_text (int y, const char *str, int psize, int col);
void RenderOverlayText (void);

// Deferred scene overlays: the target reticle (space.cpp) and the intro title sprite
// (intro.cpp). Recorded during RenderScene; drawn by RenderSceneOverlays from
// RenderGameHud. x == -1 centres a sprite on the window; a sprite w <= 0 uses its
// native size. (hud_plot_pixel is retired with the legacy 2D debris spray.)
void hud_sprite_deferred (int img, int x, int y);
void hud_sprite_scaled_deferred (int img, int x, int y, int w, int h);
void RenderSceneOverlays (void);

// I3 pointer-command feedback (state defined in main.cpp): the active order's kind
// (0 = none), an optional world Move point, and a short-lived toast. Drawn each
// frame by display_order_feedback() (space.cpp).
extern unsigned int g_order_kind;
extern bool         g_order_has_point;
extern long long    g_order_point[3];
extern char         g_order_toast[40];
extern int          g_order_toast_timer;
extern int          g_order_toast_col;

// I3 move-gizmo live state (defined in main.cpp, drawn by draw_move_gizmo()):
// the command plane (ship + camera-up normal), its in-plane base point and the
// elevated marker, all in the render (origin-relative) frame.
extern bool   g_gizmo_active;
extern double g_gizmo_ship[3];
extern double g_gizmo_normal[3];
extern double g_gizmo_base[3];
extern double g_gizmo_point[3];
void draw_move_gizmo (void);

// I3 radial context menu state (defined in main.cpp, drawn by draw_radial_menu()):
// open flag, slot count, highlighted slice, and each slot's precomputed screen
// centre + label.
extern bool        g_radial_open;
extern int         g_radial_count;
extern int         g_radial_hot;
extern int         g_radial_cx[];
extern int         g_radial_cy[];
extern const char* g_radial_labels[];
void draw_radial_menu (void);

// I3 roster join for the I2 info card: the player name behind an entity id, or
// nullptr for an NPC / unknown (defined in main.cpp).
const char* roster_name (unsigned int id);
int         roster_wanted (unsigned int id);   // wanted level, or -1 if not a player

// Entity index of the SELECTED / targeted entity (0xFFFFFFFF = none). Set by a
// pointer click (pick_entity_at_screen) or the centre-cone lock key; read by
// render_replicated_objects to draw the target reticle, by the camera rig as the
// orbit subject, and by the missile launch as its target - "the missile target IS
// the selected enemy" (interaction.md I2). Cleared when the entity dies/despawns.
extern unsigned int g_missile_lock_target;

/* Weapon / HUD presentation state (the server owns the authoritative state). */
#define MISSILE_UNARMED	-2
#define MISSILE_ARMED	-1

extern int ecm_active;       // E indicator countdown (set 32 on EcmPulse)
extern int missile_target;   // HUD lock indicator state

void reset_weapons (void);
int fire_laser (void);          // beam visual trigger (server resolves the shot)
void cool_laser (void);         // beam-visual pacing
void time_ecm (void);           // E indicator countdown

/* Frames left on the local hull's beam visual (armed by fire_laser, counted down
 * in main.cpp); while > 0 the own ship's render record carries FLG_FIRING and
 * draw_ship_laser draws the bolt from its muzzle. */
extern int draw_lasers;

void update_console (void);

void update_altitude (void);    // display-only HUD dial (never a consequence)

/* Sync local state to a server-confirmed docked state (respawn/pod/startup). */
void dock_player (void);
/* Ask the server to dock; the docked flow starts on StationResponse{Dock, Ok}. */
void request_dock (void);

void jump_warp (void);
void launch_player (void);

void engage_docking_computer (void);

// (spawn_replicated_explosion / spawn_explosion_at are retired: the EntityDeath and
// ExplosionAt handlers in main.cpp feed the Neuron::Client::Effects subsystem directly,
// and the legacy 2D pixel-spray draw_explosion is gone with them. See explosion.md.)

#endif
