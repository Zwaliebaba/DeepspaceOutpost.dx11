/*
 * space.h
 *
 * The client's flight presentation: the local display-object pool, the
 * replicated-world renderer, the cockpit HUD, and the weapon/dock visuals.
 * Presentation only - all game rules live on the server.
 */

#ifndef SPACE_H
#define SPACE_H

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
	int exp_delta;
	int exp_seed;
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
unsigned int find_lock_target (void);

// Entity index of the missile-locked ship (0xFFFFFFFF = none). Set by the missile
// lock keys in main.cpp; read by render_replicated_objects to draw the target
// reticle on the locked ship.
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
void draw_laser_lines (void);

void update_console (void);

void update_altitude (void);    // display-only HUD dial (never a consequence)

void increase_flight_roll (void);
void decrease_flight_roll (void);
void increase_flight_climb (void);
void decrease_flight_climb (void);

/* Sync local state to a server-confirmed docked state (respawn/pod/startup). */
void dock_player (void);
/* Ask the server to dock; the docked flow starts on StationResponse{Dock, Ok}. */
void request_dock (void);

void jump_warp (void);
void launch_player (void);

void engage_docking_computer (void);

// Start a client-side explosion for a replicated ship that just died, from its last
// snapshot. Defined in space.cpp; called by the EntityDeath handler (main.cpp).
namespace Neuron::Net { struct EntitySnapshot; }
void spawn_replicated_explosion (const Neuron::Net::EntitySnapshot& snap);

#endif
