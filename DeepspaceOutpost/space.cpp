/*
 * space.c
 *
 * The client's flight presentation: the local display-object pool (intro ship
 * parade, game-over debris, replicated-world mirror for the scanner/compass),
 * the replicated-world renderer, the cockpit HUD, and the weapon/dock visuals.
 * All game rules live on the server (GameLogic); nothing here mutates
 * credits/fuel/cargo/energy/shields or ends the player's life.
 */

#include "pch.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "vector.h"

#include "elite.h"
#include "GamePalette.h"
#include "GameScene.h"
#include "GameUniverse.h"
#include "docked.h"
#include "intro.h"
#include "shipdata.h"
#include "shipface.h"
#include "space.h"
#include "threed.h"
#include "sound.h"
#include "main.h"
#include "random.h"
#include "stars.h"
#include "GraphicsCore.h" // Graphics::Core::GetOutputSize (viewport size)
#include "Camera.h"      // NeuronClient: MainCamera() + CPU projection helpers
#include "CameraRig.h"   // the free camera: origin, world->camera transforms
#include "ReplicationClient.h"
#include "Vector3i64.h"             // Neuron::Math::Vector3i64 (spawn_explosion_at full def)
#include "Messages/Defs/Travel.h"   // TravelRequest (hyperspace / jump drive)
#include "ReplicatedScene.h"
#include "Render2D.h"       // native 2D pass the flight HUD now draws straight into
#include "TextRenderer.h"   // g_gameFont - the shared bitmap font (HUD text)
#include "TextureManager.h" // sprite / scanner .dds
#include "Renderer.h"       // platform_renderer()->paletteColour (HUD palette)

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>


// ---- Weapon / HUD presentation state (moved from the retired swat.cpp) -------
//
// These drive the cockpit indicators and the laser-beam visual only. The server
// owns the authoritative weapon state (laser heat, energy, ECM validation); the
// client mirrors it via PlayerStatus and the EcmPulse event.

int ecm_active;                       // E indicator + countdown (set 32 on EcmPulse)
int missile_target = MISSILE_UNARMED; // HUD lock indicator state

static int laser_counter;            // pulse pacing for the beam visual
static int laser;                     // the ship's laser type while firing

// ---- The local display-object pool (moved from the retired swat.cpp) ---------
//
// local_objects[] is presentation-only: the intro parade and game-over debris
// animate in it, and render_replicated_objects mirrors the replicated world
// into it each frame for the legacy scanner/compass HUD.

int ship_count[NO_OF_SHIPS + 1];  /* many */

void clear_local_objects (void)
{
	int i;

	for (i = 0; i < MAX_LOCAL_OBJECTS; i++)
		local_objects[i].type = 0;

	for (i = 0; i <= NO_OF_SHIPS; i++)
		ship_count[i] = 0;
}


int add_new_ship (int ship_type, int x, int y, int z, struct vector *rotmat, int rotx, int rotz)
{
	int i;

	for (i = 0; i < MAX_LOCAL_OBJECTS; i++)
	{
		if (local_objects[i].type == 0)
		{
			local_objects[i].type = ship_type;
			local_objects[i].location.x = x;
			local_objects[i].location.y = y;
			local_objects[i].location.z = z;

			local_objects[i].distance = sqrt((double)x*x + (double)y*y + (double)z*z);

			local_objects[i].rotmat[0] = rotmat[0];
			local_objects[i].rotmat[1] = rotmat[1];
			local_objects[i].rotmat[2] = rotmat[2];

			local_objects[i].rotx = rotx;
			local_objects[i].rotz = rotz;

			local_objects[i].velocity = 0;
			local_objects[i].acceleration = 0;
			local_objects[i].bravery = 0;
			local_objects[i].target = 0;
			local_objects[i].flags = 0;

			if ((ship_type != SHIP_PLANET) && (ship_type != SHIP_SUN))
			{
				local_objects[i].energy = ship_list[ship_type]->energy;
				local_objects[i].missiles = ship_list[ship_type]->missiles;
				ship_count[ship_type]++;
			}

			return i;
		}
	}

	return -1;
}


void remove_ship (int un)
{
	const int type = local_objects[un].type;

	if (type == 0)
		return;

	if (type > 0)
		ship_count[type]--;

	local_objects[un].type = 0;
}



void rotate_x_first (double *a, double *b, int direction)
{
	double fx,ux;

	fx = *a;
	ux = *b;

	if (direction < 0)
	{	
		*a = fx - (fx / 512) + (ux / 19);
		*b = ux - (ux / 512) - (fx / 19);
	}
	else
	{
		*a = fx - (fx / 512) - (ux / 19);
		*b = ux - (ux / 512) + (fx / 19);
	}
}


void rotate_vec (struct vector *vec, double alpha, double beta)
{
	double x,y,z;
	
	x = vec->x;
	y = vec->y;
	z = vec->z;

	y = y - alpha * x;
	x = x + alpha * y;
	y = y - beta * z;
	z = z + beta * y;
	
	vec->x = x;
	vec->y = y;
	vec->z = z;
}


/*
 * Update an object's location in local space.
 */

void move_local_object (struct local_object *obj)
{
	double x,y,z;
	double k2;
	double alpha;
	double beta;
	int rotx,rotz;
	double speed;
	
	alpha = PlayerFlight().roll / 256.0;
	beta = PlayerFlight().climb / 256.0;
	
	x = obj->location.x;
	y = obj->location.y;
	z = obj->location.z;

	if (!(obj->flags & FLG_DEAD))
	{ 
		if (obj->velocity != 0)
		{
			speed = obj->velocity;
			speed *= 1.5; 	
			x += obj->rotmat[2].x * speed; 
			y += obj->rotmat[2].y * speed; 
			z += obj->rotmat[2].z * speed; 
		}

		if (obj->acceleration != 0)
		{
			obj->velocity += obj->acceleration;
			obj->acceleration = 0;
			if (obj->velocity > ship_list[obj->type]->velocity)
				obj->velocity = ship_list[obj->type]->velocity;
			
			if (obj->velocity <= 0)
				obj->velocity = 1;
		}
	}
	
	k2 = y - alpha * x;
	z = z + beta * k2;
	y = k2 - z * beta;
	x = x + alpha * y;

	z = z - PlayerFlight().speed;

	obj->location.x = x;
	obj->location.y = y;
	obj->location.z = z;	

	obj->distance = sqrt (x*x + y*y + z*z);
	
	if (obj->type == SHIP_PLANET)
		beta = 0.0;
	
	rotate_vec (&obj->rotmat[2], alpha, beta);
	rotate_vec (&obj->rotmat[1], alpha, beta);
	rotate_vec (&obj->rotmat[0], alpha, beta);

	if (obj->flags & FLG_DEAD)
		return;


	rotx = obj->rotx;
	rotz = obj->rotz;
	
	/* If necessary rotate the object around the X axis... */

	if (rotx != 0)
	{
		rotate_x_first (&obj->rotmat[2].x, &obj->rotmat[1].x, rotx);
		rotate_x_first (&obj->rotmat[2].y, &obj->rotmat[1].y, rotx);	
		rotate_x_first (&obj->rotmat[2].z, &obj->rotmat[1].z, rotx);

		if ((rotx != 127) && (rotx != -127))
			obj->rotx -= (rotx < 0) ? -1 : 1;
	}	

	
	/* If necessary rotate the object around the Z axis... */

	if (rotz != 0)
	{	
		rotate_x_first (&obj->rotmat[0].x, &obj->rotmat[1].x, rotz);
		rotate_x_first (&obj->rotmat[0].y, &obj->rotmat[1].y, rotz);	
		rotate_x_first (&obj->rotmat[0].z, &obj->rotmat[1].z, rotz);	

		if ((rotz != 127) && (rotz != -127))
			obj->rotz -= (rotz < 0) ? -1 : 1;
	}


	/* Orthonormalize the rotation matrix... */

	tidy_matrix (obj->rotmat);
}


/*
 * Dock the player into the space station.
 */

void dock_player (void)
{
	docked = 1;

	// Tell the server we've docked so it permits station trade. Used only where
	// the server already has (or is about to confirm) us docked: the respawn and
	// escape-pod flows, and the initial docked state. In-flight docking goes
	// through request_dock() and flips on the server's StationResponse instead.
	{
		Neuron::Net::StationRequest req;
		req.kind = Neuron::Net::StationRequestKind::Dock;
		Neuron::Client::ReplicationClientInstance().SendStationRequest(req);
	}

	PlayerFlight().speed = 0;
	PlayerFlight().roll = 0;
	PlayerFlight().climb = 0;
	PlayerCaps().altitude = 255;   // display defaults; vitals mirror PlayerStatus
	PlayerCaps().cabTemp = 30;
	reset_weapons();
}


// Ask the server to dock us (rate-limited: the proximity check below runs every
// frame while near the station). The docked flow starts only when the server's
// StationResponse{Dock, Ok} arrives - see the handler in main.cpp.
void request_dock (void)
{
	static int cooldown = 0;

	if (docked)
		return;

	if (cooldown > 0)
	{
		cooldown--;
		return;
	}

	cooldown = 30;   // ~1 request per second at display rate while in range
	Neuron::Net::StationRequest req;
	req.kind = Neuron::Net::StationRequestKind::Dock;
	Neuron::Client::ReplicationClientInstance().SendStationRequest(req);
}


// Display-only altitude readout for the HUD dial, recomputed from the replicated
// planet's mirrored position (local_objects[0]). Never a consequence: the server
// owns planet collisions and kills authoritatively (EntityDeath).
void update_altitude (void)
{
	double x,y,z;
	double dist;

	PlayerCaps().altitude = 255;

	if (witchspace)
		return;

	x = fabs(local_objects[0].location.x);
	y = fabs(local_objects[0].location.y);
	z = fabs(local_objects[0].location.z);

	if ((x > 65535) || (y > 65535) || (z > 65535))
		return;

	x /= 256;
	y /= 256;
	z /= 256;

	dist = (x * x) + (y * y) + (z * z);

	if (dist > 65535)
		return;

	dist -= 9472;
	if (dist < 1)
	{
		PlayerCaps().altitude = 0;
		return;
	}

	dist = sqrt (dist);

	PlayerCaps().altitude = (dist < 1) ? 0 : dist;
}


/*
 * Animate and draw the local display objects.
 *
 * Presentation only: this drives the intro ship parade and the game-over debris
 * tumble. No AI, no combat, no docking, no scooping - the legacy local
 * simulation was retired with the single-player fallback; the live game renders
 * the server's replicated world (render_replicated_objects) instead.
 *
 * These scenes run against the IDENTITY camera (camera_rig_reset), so the
 * world-frame objects draw_ship expects coincide with the legacy camera-space
 * animation; draw_ship writes any explosion progress back into the slot.
 */

void update_local_objects (void)
{
	int i;
	int type;

	for (i = 0; i < MAX_LOCAL_OBJECTS; i++)
	{
		type = local_objects[i].type;

		if (type != 0)
		{
			if (local_objects[i].flags & FLG_REMOVE)
			{
				remove_ship (i);   // a finished explosion animation
				continue;
			}

			move_local_object (&local_objects[i]);

			if (local_objects[i].distance > 57344)
			{
				remove_ship (i);   // drifted out of the display range
				continue;
			}

			draw_ship (&local_objects[i]);
		}
	}

	/* The frame's 3D scene is fully submitted (dust background + the models handed to
	   Scene3D::SubmitModel above): draw it now, onto the cleared back buffer, under the 2D HUD. */
	gfx_render_3d_scene();
}


/*
 * Render the replicated world.
 *
 * The server is authoritative: we sample the interpolated snapshots from the
 * ReplicationClient, rebase them around the local player (the floating origin)
 * into the legacy render frame, and draw them through the same pipeline. No game
 * logic runs here - the client only displays. While disconnected the flight
 * screen shows the connection-lost state instead (see game_render_flight).
 */

// World-unit distance to the closest replicated station this frame (1e18 = none
// in view). Lets the docking computer dock only when actually in range, matching
// the server's proximity gate so it never optimistically docks from across AOI.
static double s_nearest_station_dist = 1.0e18;

// Client-side explosion effects for replicated ships that die. The server sends an
// EntityDeath (which vanishes the ship); the client turns it into a short debris
// burst here so a kill is visible, not just audible. Each holds the dying ship's
// absolute world position and a persistent local_object carrying the legacy
// explosion state (exp_seed/exp_delta, grown by draw_ship/draw_explosion). It is
// world-anchored - rebased around the moving player every frame like the ships -
// until the legacy animation finishes (FLG_REMOVE) or a safety lifetime elapses.
namespace
{
	struct ReplicatedExplosion
	{
		Neuron::Math::Vector3i64 worldPos{};
		struct local_object obj;   // memset in spawn_replicated_explosion
		int frames = 0;
	};
	std::vector<ReplicatedExplosion> s_explosions;
	constexpr int MAX_EXPLOSION_FRAMES = 120;   // ~2s safety cap if it never faces us
}

// Start an explosion for a dying replicated ship, from its last snapshot (captured
// before the entity is forgotten). Skips non-ship types (planet/sun/untyped).
void spawn_replicated_explosion (const Neuron::Net::EntitySnapshot& snap)
{
	const int type = snap.type;
	if (type <= 0 || type > NO_OF_SHIPS)
		return;

	ReplicatedExplosion ex;
	ex.worldPos = Neuron::Math::Vector3i64{ snap.x, snap.y, snap.z };
	memset (&ex.obj, 0, sizeof (ex.obj));
	ex.obj.type = type;
	ex.obj.flags = FLG_DEAD;          // draw_ship promotes this to an animated explosion
	set_init_matrix (ex.obj.rotmat);
	s_explosions.push_back (ex);
}

// G1: start a world-anchored explosion at an absolute point, from an ExplosionAt
// broadcast (a player kill the killer/bystanders should see, decoupled from the
// respawned victim entity). Uses a fighter hull for the debris mesh; the server's
// `scale` is a size hint the legacy animation doesn't parameterise, so it is unused
// for now beyond gating a sane minimum.
void spawn_explosion_at (const Neuron::Math::Vector3i64& world_pos, int /*scale*/)
{
	ReplicatedExplosion ex;
	ex.worldPos = world_pos;
	memset (&ex.obj, 0, sizeof (ex.obj));
	ex.obj.type = SHIP_VIPER;          // player hulls are Vipers; a fighter-sized pop
	ex.obj.flags = FLG_DEAD;
	set_init_matrix (ex.obj.rotmat);
	s_explosions.push_back (ex);
}

void render_replicated_objects (void)
{
	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();

	// Rebuild ship_count[] from what the server actually replicated this tick, so
	// the legacy "is a station nearby?" tests (safe zone, docking computer) work
	// off the live world instead of the retired single-player spawner.
	for (int t = 0; t <= NO_OF_SHIPS; t++)
		ship_count[t] = 0;

	s_nearest_station_dist = 1.0e18;   // recomputed below from the live stations

	// Mirror the replicated world into local_objects[] each frame so the legacy HUD that
	// reads that array - the scanner blips and the compass - reflects the live server world.
	// Slots 0/1 are reserved for planet/station (the compass convention, update_compass
	// reads [0]/[1]); everything else fills from slot 2 up.
	{
		struct local_object empty;
		memset (&empty, 0, sizeof (empty));
		for (int i = 0; i < MAX_LOCAL_OBJECTS; i++)
			local_objects[i] = empty;
	}
	int localFill = 2;

	// The camera rig anchors to the replicated ship; until it has, there is no
	// floating origin to rebase around - draw just the background.
	if (!camera_rig_ready())
	{
		gfx_render_3d_scene();
		return;
	}

	// Interpolate at a render-time alpha: sample ~one snapshot interval in the past
	// and tween prev->curr, so replicated motion is smooth at display rate instead
	// of snapping to the latest tick.
	const double alpha = rc.InterpolationAlpha();
	std::vector<Neuron::Net::EntitySnapshot> ents = rc.SampleAll(alpha);

	// World-frame records rebased about the CAMERA's floating origin. The camera is
	// decoupled from the ship, so the records include the player's own hull - with
	// no cockpit view, the active ship renders like any other entity.
	const long long* org = camera_rig_origin();
	std::vector<Neuron::Client::RenderRecord> records =
		Neuron::Client::BuildRenderRecords(ents, org[0], org[1], org[2]);

	// The ship's own world position (origin-relative), for the SHIP-relative gates
	// below (docking proximity, nearest-station range) - those are about the hull,
	// not about where the camera happens to float.
	Neuron::Net::EntitySnapshot meSnap;
	const bool haveMe = rc.Sample (rc.LocalPlayer(), alpha, meSnap);
	const double meX = haveMe ? static_cast<double>(meSnap.x - org[0]) : 0.0;
	const double meY = haveMe ? static_cast<double>(meSnap.y - org[1]) : 0.0;
	const double meZ = haveMe ? static_cast<double>(meSnap.z - org[2]) : 0.0;

	int drawn = 0;
	for (const Neuron::Client::RenderRecord& rec : records)
	{
		if (drawn >= MAX_LOCAL_OBJECTS)
			break;

		// Draw each entity as its replicated model (planet, station, ships);
		// entities with no type fall back to a generic ship.
		struct local_object obj;
		memset (&obj, 0, sizeof(obj));
		obj.type = (rec.type != 0) ? rec.type : SHIP_VIPER;
		if (obj.type > 0 && obj.type <= NO_OF_SHIPS)
			ship_count[obj.type]++;
		obj.location = rec.location;
		obj.rotmat[0] = rec.rotmat[0];
		obj.rotmat[1] = rec.rotmat[1];
		obj.rotmat[2] = rec.rotmat[2];
		obj.distance = (int) rec.distance;

		// The player's own beam: with the cockpit view gone the local hull renders
		// like any other ship, so its shots use the same muzzle-beam visual
		// (draw_lasers is armed by fire_laser and counted down in main.cpp).
		if (haveMe && rec.id == rc.LocalPlayer() && draw_lasers > 0)
			obj.flags |= FLG_FIRING;

		// Ship-relative offsets for the dock/nearest-station gates.
		const double sdx = rec.location.x - meX;
		const double sdy = rec.location.y - meY;
		const double sdz = rec.location.z - meZ;
		const double shipDist = haveMe ? sqrt (sdx * sdx + sdy * sdy + sdz * sdz) : 1.0e18;

		if ((obj.type == SHIP_CORIOLIS || obj.type == SHIP_DODEC) && shipDist < s_nearest_station_dist)
			s_nearest_station_dist = shipDist;

		// Record this entity for the scanner / compass (see the clear above): planet ->
		// slot 0, station -> slot 1, everything else from slot 2 up. The mirror holds
		// CAMERA-SPACE positions (the frame the blip layout expects), so the scanner
		// and compass read relative to the view.
		struct local_object camObj = obj;
		camera_view_object (&camObj);
		{
			const int slot = (obj.type == SHIP_PLANET) ? 0
						   : (obj.type == SHIP_CORIOLIS || obj.type == SHIP_DODEC) ? 1
						   : (localFill < MAX_LOCAL_OBJECTS ? localFill++ : -1);
			if (slot >= 0)
				local_objects[slot] = camObj;
		}

		draw_ship (&obj);
		++drawn;

		// Target reticle: overlay the lock marker (Textures/TargetLock.dds) on the
		// missile-locked ship, centred and SIZED to the ship's on-screen extent so
		// it sits just around the hull (not a fixed oversized box). Projected from
		// the camera-space centre through the same optics the meshes use.
		if (rec.id == g_missile_lock_target && camObj.location.z > 0.0)
		{
			int w, h;
			gfx_scene_size (&w, &h);
			Neuron::Client::Camera& camera = Neuron::Client::MainCamera();
			const double focal = Neuron::Client::CameraFocalPixels (camera, (float) h);

			double fx, fy;
			if (Neuron::Client::CameraSpaceToPixels (camera, camObj.location.x, camObj.location.y,
													 camObj.location.z, w, h, fx, fy))
			{
				const int sx = (int)fx;
				const int sy = (int)fy;

				const double radius =
					(obj.type > 0 && obj.type <= NO_OF_SHIPS && ship_list[obj.type] != NULL)
						? sqrt (ship_list[obj.type]->size) : 80.0;
				double half = (radius * focal / camObj.location.z) * 1.15;
				const double clampScale = focal / 512.0;   // keep the box proportional to the view
				if (half < 8.0  * clampScale) half = 8.0  * clampScale;
				if (half > 80.0 * clampScale) half = 80.0 * clampScale;

				const int box = (int)(half * 2.0);
				hud_sprite_scaled_deferred (IMG_TARGET_LOCK, sx - (int)half, sy - (int)half, box, box);
			}
		}

		// Docking. Authentic Elite demands a precise slot alignment, but with a
		// static (non-spinning) station and network lag that is punishing. So we
		// dock forgivingly: when the SHIP is near the station (within ~600 units),
		// station roughly off its nose, at low speed, a dock REQUEST goes to the
		// server. Ship-relative on purpose - the camera floats freely and has no
		// bearing on where the hull is. The docked flow starts only when the
		// server's StationResponse{Dock, Ok} arrives (see main.cpp).
		const int dockSpeedLimit = (PlayerCaps().maxSpeed > 0) ? (PlayerCaps().maxSpeed / 4) : 10;
		if (haveMe && (obj.type == SHIP_CORIOLIS || obj.type == SHIP_DODEC) &&
			shipDist < 600 && PlayerFlight().speed <= dockSpeedLimit)
		{
			const double ahead = (shipDist > 1.0)
				? (sdx * meSnap.noseX + sdy * meSnap.noseY + sdz * meSnap.noseZ) / shipDist
				: 1.0;
			if (ahead > 0.5)   // station roughly off the ship's nose -> ask to dock
				request_dock ();
		}
	}

	// Replicated explosions: draw each dying ship's debris burst, world-anchored via
	// the same floating-origin rebasing the ships use, until the legacy animation
	// finishes (FLG_REMOVE) or the safety lifetime elapses. draw_ship writes the
	// per-frame explosion progress back into the persistent object.
	if (!s_explosions.empty())
	{
		std::vector<Neuron::Net::EntitySnapshot> es;
		es.reserve (s_explosions.size());
		for (size_t i = 0; i < s_explosions.size(); i++)
		{
			Neuron::Net::EntitySnapshot s;   // defaults: nose +z, roof +y
			s.id = 0x80000000u | (uint32_t) i;   // synthetic id, distinct from real + local
			s.x = s_explosions[i].worldPos.x;
			s.y = s_explosions[i].worldPos.y;
			s.z = s_explosions[i].worldPos.z;
			s.type = (int16_t) s_explosions[i].obj.type;
			es.push_back (s);
		}

		std::vector<Neuron::Client::RenderRecord> exrecs =
			Neuron::Client::BuildRenderRecords (es, org[0], org[1], org[2]);   // order matches s_explosions

		for (size_t i = 0; i < exrecs.size() && i < s_explosions.size(); i++)
		{
			struct local_object& o = s_explosions[i].obj;
			o.location = exrecs[i].location;
			o.rotmat[0] = exrecs[i].rotmat[0];
			o.rotmat[1] = exrecs[i].rotmat[1];
			o.rotmat[2] = exrecs[i].rotmat[2];
			o.distance = (int) exrecs[i].distance;
			draw_ship (&o);   // FLG_DEAD -> explosion; grows exp_delta; sets FLG_REMOVE when done
		}

		for (ReplicatedExplosion& ex : s_explosions)
			ex.frames++;
		std::erase_if (s_explosions, [](const ReplicatedExplosion& e)
		{
			return (e.obj.flags & FLG_REMOVE) || e.frames > MAX_EXPLOSION_FRAMES;
		});
	}

	/* The frame's replicated 3D scene is fully submitted: draw it now, onto the cleared
	   back buffer, under the 2D HUD (the game drives the pass; no scene-marker flag). */
	gfx_render_3d_scene();
}


// (I7: find_lock_target - the legacy centre-of-view cone lock behind the retired
//  T key - is gone. Selection is the cursor pick below (pick_entity_at_screen).)


// I2 (interaction.md): pick the replicated entity nearest the SCREEN CURSOR
// (mx,my in the same full-window pixel space input_mouse_state reports). Projects
// every entity through the SAME optics the target reticle uses (camera_view_point
// -> CameraSpaceToPixels over Graphics::Core::GetOutputSize), so what you click
// is what you see.
// Unlike find_lock_target's centre-cone lock, ANY entity is selectable (stations,
// planets and canisters too - I3's orders act on them) except your own hull and
// in-flight missiles. Returns 0xFFFFFFFF when nothing is within the hit radius.
unsigned int pick_entity_at_screen (int mx, int my)
{
	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	if (!rc.IsOpen() || !camera_rig_ready())
		return 0xFFFFFFFFu;

	const auto sz = Neuron::Graphics::Core::GetOutputSize();
	const int vw = static_cast<int>(sz.Width);
	const int vh = static_cast<int>(sz.Height);
	Neuron::Client::Camera& camera = Neuron::Client::MainCamera();

	const long long* org = camera_rig_origin();
	std::vector<Neuron::Net::EntitySnapshot> ents = rc.SampleAll (1.0);
	std::vector<Neuron::Client::RenderRecord> records =
		Neuron::Client::BuildRenderRecords (ents, org[0], org[1], org[2]);

	// A generous, roughly screen-constant hit radius (pixels), scaled to the
	// viewport so selection feels the same at any window size.
	const double hitRadius = (double) vh * 0.06 + 24.0;

	unsigned int best = 0xFFFFFFFFu;
	double bestPixDist = 1.0e18;

	for (const Neuron::Client::RenderRecord& rec : records)
	{
		if (rec.type == SHIP_MISSILE)      // don't target in-flight missiles
			continue;
		if (rec.id == rc.LocalPlayer())    // never your own hull
			continue;

		struct vector camPos = rec.location;
		camera_view_point (&camPos);
		if (camPos.z <= 0.0)               // behind the eye
			continue;

		double sx = 0.0, sy = 0.0;
		if (!Neuron::Client::CameraSpaceToPixels (camera, camPos.x, camPos.y, camPos.z, vw, vh, sx, sy))
			continue;

		const double dpx = sx - (double) mx;
		const double dpy = sy - (double) my;
		const double pd = sqrt (dpx * dpx + dpy * dpy);
		if (pd <= hitRadius && pd < bestPixDist)
		{
			bestPixDist = pd;
			best = rec.id;             // nearest to the cursor wins
		}
	}

	return best;
}




// ---- Native flight-HUD primitives -------------------------------------------
//
// The cockpit dashboard (scanner, dials, compass, missiles) and the I2/I3/I4 overlays
// used to be emitted into the gfx2d deferred batch (gfx_draw_*) and replayed at flush
// time. They now draw straight into the Render2D pass that RenderGameHud (HudRender.cpp)
// brackets during RenderCanvas - so the dashboard no longer rides gfx2d at all. These
// helpers mirror the old gfx2d primitives (palette-indexed colour, a floated draw origin,
// the shared bitmap font) but submit immediately to the open pass instead of batching.
//
// Unlike the old path there is no leftover scanner scissor: the whole HUD draws in the
// full client window, so the top-anchored overlays (ability bar, target card, order toast)
// are no longer clipped out by the dashboard's clip rect.

using Neuron::Graphics::Render2D;

extern char scanner_filename[256];   // configured scanner art (elite.cpp)

namespace {

// The classic 512-wide dashboard is authored at (0,0) and floated to the bottom-centre
// of the window; every HUD coordinate is offset by this origin (was gfx_set_draw_origin).
int s_hud_ox = 0;
int s_hud_oy = 0;

// TextRenderer advance is size*0.6 per glyph; 8/0.6 reproduces the gfx2d body font's
// 8px monospaced cell (the dashboard layout assumes an 8px advance).
constexpr float HUD_FONT_PX = 8.0f / 0.6f;

// Palette index -> opaque 0xAABBGGRR, exactly as the gfx2d batch resolved colours.
uint32_t hud_col (int index)
{
	Renderer* r = platform_renderer();
	uint32_t c = r ? r->paletteColour(index) : 0xFFFFFFFFu;
	return c | 0xFF000000u;
}

// Borrow a sprite/scanner SRV from the TextureManager (it caches, so this is a hash
// lookup after the first frame). Fills the pixel size when asked.
ID3D11ShaderResourceView* hud_tex (const char* fn, float* w, float* h)
{
	auto t = Neuron::Graphics::TextureManager::LoadTexture(fn);
	if (!t || !t->IsLoaded())
		return nullptr;
	if (w) *w = t->GetWidth();
	if (h) *h = t->GetHeight();
	return t->GetShaderResourceView();
}

const char* hud_sprite_file (int sprite_no)
{
	switch (sprite_no)
	{
		case IMG_GREEN_DOT:      return "greendot.dds";
		case IMG_RED_DOT:        return "reddot.dds";
		case IMG_BIG_S:          return "safe.dds";
		case IMG_BIG_E:          return "ecm.dds";
		case IMG_MISSILE_GREEN:  return "missgrn.dds";
		case IMG_MISSILE_YELLOW: return "missyell.dds";
		case IMG_MISSILE_RED:    return "missred.dds";
		case IMG_TARGET_LOCK:    return "Textures/TargetLock.dds";
		case IMG_ELITE_TXT:      return "elitetx3.dds";
		default:                 return nullptr;
	}
}

} // namespace

// Float the HUD (pass (0,0) to draw in absolute window space again).
void hud_set_origin (int x, int y) { s_hud_ox = x; s_hud_oy = y; }

// A horizontal/vertical run fills a 1px-tall/-wide rect (matching the old batch, which
// turned axis-aligned lines into filled rects); a diagonal is a true line.
void hud_line (int x1, int y1, int x2, int y2, int col)
{
	const uint32_t c = hud_col(col);
	x1 += s_hud_ox; x2 += s_hud_ox; y1 += s_hud_oy; y2 += s_hud_oy;
	if (y1 == y2)
		Render2D::FillRect((float)std::min(x1, x2), (float)y1, (float)(std::max(x1, x2) + 1), (float)(y1 + 1), c);
	else if (x1 == x2)
		Render2D::FillRect((float)x1, (float)std::min(y1, y2), (float)(x1 + 1), (float)(std::max(y1, y2) + 1), c);
	else
		Render2D::DrawLine(x1 + 0.5f, y1 + 0.5f, x2 + 0.5f, y2 + 0.5f, c);
}

// Filled box (gfx_draw_rectangle was a filled quad; the ability-bar buttons rely on it).
void hud_rect (int x1, int y1, int x2, int y2, int col)
{
	const uint32_t c = hud_col(col);
	const int l = std::min(x1, x2) + s_hud_ox, t = std::min(y1, y2) + s_hud_oy;
	const int r = std::max(x1, x2) + s_hud_ox, b = std::max(y1, y2) + s_hud_oy;
	Render2D::FillRect((float)l, (float)t, (float)(r + 1), (float)(b + 1), c);
}

// Bitmap-font text. Shadow-on routes g_gameFont through the same Render2D text-outline
// program the gfx2d HUD text used, so it stays crisp over the busy 3D. The +3/+7 undoes
// TextRenderer's built-in compatibility offset so the glyph top-left lands at (x,y).
void hud_text (int x, int y, const char* str, int col)
{
	if (!str) return;
	const uint32_t c = hud_col(col);
	g_gameFont.SetColor((uint8_t)(c & 0xff), (uint8_t)((c >> 8) & 0xff), (uint8_t)((c >> 16) & 0xff), 255);
	g_gameFont.SetRenderShadow(true);
	g_gameFont.DrawText2D((float)(x + s_hud_ox) + 3.0f, (float)(y + s_hud_oy) + 7.0f, HUD_FONT_PX, str);
	g_gameFont.SetRenderShadow(false);
}

// A HUD sprite at its native size (IMG_* -> .dds via the TextureManager).
static void hud_sprite (int sprite_no, int x, int y)
{
	const char* fn = hud_sprite_file(sprite_no);
	if (!fn) return;
	float w = 0.0f, h = 0.0f;
	ID3D11ShaderResourceView* srv = hud_tex(fn, &w, &h);
	if (!srv) return;
	const float x0 = (float)(x + s_hud_ox), y0 = (float)(y + s_hud_oy);
	Render2D::TexQuad(srv, x0, y0, x0 + w, y0 + h, 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
}

// A HUD sprite stretched to an explicit w x h (the missile-target reticle).
static void hud_sprite_scaled (int sprite_no, int x, int y, int w, int h)
{
	const char* fn = hud_sprite_file(sprite_no);
	if (!fn) return;
	ID3D11ShaderResourceView* srv = hud_tex(fn, nullptr, nullptr);
	if (!srv) return;
	const float x0 = (float)(x + s_hud_ox), y0 = (float)(y + s_hud_oy);
	Render2D::TexQuad(srv, x0, y0, x0 + (float)w, y0 + (float)h, 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
}

// The scanner console backdrop. The configured name is a .bmp (Renderer still reads it for
// the palette) but the sprite loads as .dds, so map the extension across, as gfx2d did.
static void hud_scanner (void)
{
	const char* cfg = (scanner_filename[0] != '\0') ? scanner_filename : "scanner.bmp";
	std::string fn = cfg;
	if (const size_t dot = fn.find_last_of('.'); dot != std::string::npos)
		fn.replace(dot, std::string::npos, ".dds");
	else
		fn += ".dds";
	float w = 0.0f, h = 0.0f;
	ID3D11ShaderResourceView* srv = hud_tex(fn.c_str(), &w, &h);
	if (!srv) return;
	const float x0 = (float)s_hud_ox, y0 = (float)(385 + s_hud_oy);
	Render2D::TexQuad(srv, x0, y0, x0 + w, y0 + h, 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
}

// ---- Deferred centred overlay text ------------------------------------------
//
// The centred titles / prompts (intro screens), the transient flight info message and the
// GAME OVER banner are still emitted from the RenderScene phase (intro.cpp / main.cpp),
// where there is no open 2D pass. hud_centre_text records them; RenderOverlayText draws
// them natively from RenderGameHud during RenderCanvas, then clears the list for the next
// frame. Only the active state records, so the list is naturally state-correct. This
// replaces the last gfx2d text path (gfx_display_centre_text).

namespace {
struct OverlayLine { int y; int psize; int col; std::string text; };
std::vector<OverlayLine> s_overlay_text;
}

void hud_centre_text (int y, const char* str, int psize, int col)
{
	if (str) s_overlay_text.push_back({ y, psize, col, str });
}

void RenderOverlayText (void)
{
	if (s_overlay_text.empty()) return;
	const auto sz = Neuron::Graphics::Core::GetOutputSize();
	const float midx = static_cast<int>(sz.Width) / 2.0f;
	for (const OverlayLine& ln : s_overlay_text)
	{
		// psize 140 selects the larger heading font, 120 the body font. TextRenderer's glyph
		// height IS the size and the advance is size*0.6, matching gfx2d's 20px / 13px cells;
		// the +7 undoes TextRenderer's compat offset so the line sits at the requested y.
		const uint32_t c = hud_col(ln.col);
		const float px = (ln.psize == 140) ? 20.0f : HUD_FONT_PX;
		g_gameFont.SetColor((uint8_t)(c & 0xff), (uint8_t)((c >> 8) & 0xff), (uint8_t)((c >> 16) & 0xff), 255);
		g_gameFont.SetRenderShadow(true);
		g_gameFont.DrawText2DCenter(midx, (float)ln.y + 7.0f, px, ln.text);
		g_gameFont.SetRenderShadow(false);
	}
	s_overlay_text.clear();
}

// ---- Deferred scene overlays (points + sprites) -----------------------------
//
// The last drawing that still went through the gfx2d batch: the ship-death debris spray
// (screen-projected points, threed.cpp), the per-ship target reticle and the intro title
// art (sprites, space.cpp / intro.cpp). Like the centred text they are emitted from the
// RenderScene phase, so they queue here and RenderSceneOverlays draws them natively from
// RenderGameHud - BEFORE the dashboard and the overlay text, matching the old order where
// the batch flushed under the HUD. With this the gfx2d 2D batch has no producers left.

namespace {
struct OverlayPoint  { int x, y, col; };
struct OverlaySprite { int img, x, y, w, h; };   // w <= 0 -> the sprite's native size
std::vector<OverlayPoint>  s_overlay_points;
std::vector<OverlaySprite> s_overlay_sprites;
}

void hud_plot_pixel (int x, int y, int col) { s_overlay_points.push_back({ x, y, col }); }
void hud_sprite_deferred (int img, int x, int y) { s_overlay_sprites.push_back({ img, x, y, 0, 0 }); }
void hud_sprite_scaled_deferred (int img, int x, int y, int w, int h)
{
	s_overlay_sprites.push_back({ img, x, y, w, h });
}

void RenderSceneOverlays (void)
{
	for (const OverlayPoint& p : s_overlay_points)
		Render2D::PlotPoint((float)p.x + 0.5f, (float)p.y + 0.5f, hud_col(p.col));

	if (!s_overlay_sprites.empty())
	{
		const auto sz = Neuron::Graphics::Core::GetOutputSize();
		for (const OverlaySprite& s : s_overlay_sprites)
		{
			const char* fn = hud_sprite_file(s.img);
			if (!fn) continue;
			float tw = 0.0f, th = 0.0f;
			ID3D11ShaderResourceView* srv = hud_tex(fn, &tw, &th);
			if (!srv) continue;
			const float w = (s.w > 0) ? (float)s.w : tw;
			const float h = (s.h > 0) ? (float)s.h : th;
			// x == -1 centres on the window (the intro title sprite), matching gfx_draw_sprite.
			const float x = (s.x == -1) ? (static_cast<int>(sz.Width) - tw) * 0.5f : (float)s.x;
			const float y = (float)s.y;
			Render2D::TexQuad(srv, x, y, x + w, y + h, 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
		}
	}

	s_overlay_points.clear();
	s_overlay_sprites.clear();
}


/*
 * Update the scanner and draw all the lollipops.
 */

void update_scanner (void)
{
	int i;
	int x,y,z;
	int x1,y1,y2;
	int colour;
	
	for (i = 0; i < MAX_LOCAL_OBJECTS; i++)
	{
		if ((local_objects[i].type <= 0) ||
			(local_objects[i].flags & FLG_DEAD) ||
			(local_objects[i].flags & FLG_CLOAKED))
			continue;
	
		x = local_objects[i].location.x / 256;
		y = local_objects[i].location.y / 256;
		z = local_objects[i].location.z / 256;

		x1 = x;
		y1 = -z / 4;
		y2 = y1 - y / 2;

		if ((y2 < -28) || (y2 > 28) ||
			(x1 < -50) || (x1 > 50))
			continue;

		x1 += scanner_cx;
		y1 += scanner_cy;
		y2 += scanner_cy;

		colour = (local_objects[i].flags & FLG_HOSTILE) ? GFX_COL_YELLOW_5 : GFX_COL_WHITE;
			
		switch (local_objects[i].type)
		{
			case SHIP_MISSILE:
				colour = 137;
				break;

			case SHIP_DODEC:
			case SHIP_CORIOLIS:
				colour = GFX_COL_GREEN_1;
				break;
				
			case SHIP_VIPER:
				colour = 252;
				break;
		}
			
		hud_line(x1+2, y2,   x1-3, y2, colour);
		hud_line(x1+2, y2+1, x1-3, y2+1, colour);
		hud_line(x1+2, y2+2, x1-3, y2+2, colour);
		hud_line(x1+2, y2+3, x1-3, y2+3, colour);


		hud_line(x1,   y1, x1,   y2, colour);
		hud_line(x1+1, y1, x1+1, y2, colour);
		hud_line(x1+2, y1, x1+2, y2, colour);
	}
}


/*
 * Update the compass which tracks the space station / planet.
 */

void update_compass (void)
{
	struct vector dest;
	int compass_x;
	int compass_y;
	int un = 0;

	if (witchspace)
		return;
	
	if (ship_count[SHIP_CORIOLIS] || ship_count[SHIP_DODEC])
		un = 1;
	
	dest = unit_vector (&local_objects[un].location);
	
	compass_x = compass_centre_x + (dest.x * 16);
	compass_y = compass_centre_y + (dest.y * -16);
	
	if (dest.z < 0)
	{
		hud_sprite(IMG_RED_DOT, compass_x, compass_y);
	}
	else
	{
		hud_sprite(IMG_GREEN_DOT, compass_x, compass_y);
	}
				
}


/*
 * Display the speed bar.
 */

void display_speed (void)
{
	int sx,sy;
	int i;
	int len;
	int colour;

	sx = 417;
	sy = 384 + 9;

	len = ((PlayerFlight().speed * 64) / PlayerCaps().maxSpeed) - 1;

	colour = (PlayerFlight().speed > (PlayerCaps().maxSpeed * 2 / 3)) ? GFX_COL_DARK_RED : GFX_COL_GOLD;

	for (i = 0; i < 6; i++)
	{
		hud_line(sx, sy + i, sx + len, sy + i, colour);
	}
}


/*
 * Draw an indicator bar.
 * Used for shields and energy banks.
 */

void display_dial_bar (int len, int x, int y)
{
	int i = 0;

	hud_line(x, y + 384, x + len, y + 384, GFX_COL_GOLD);
	i++;
	hud_line(x, y + i + 384, x + len, y + i + 384, GFX_COL_GOLD);
	
	for (i = 2; i < 7; i++)
		hud_line(x, y + i + 384, x + len, y + i + 384, GFX_COL_YELLOW_1);

	hud_line(x, y + i + 384, x + len, y + i + 384, GFX_COL_DARK_RED);
}


/*
 * Display the current shield strengths.
 */

void display_shields (void)
{
	if (PlayerDefense().frontShield > 3)
		display_dial_bar (PlayerDefense().frontShield / 4, 31, 7);

	if (PlayerDefense().aftShield > 3)
		display_dial_bar (PlayerDefense().aftShield / 4, 31, 23);
}


void display_altitude (void)
{
	if (PlayerCaps().altitude > 3)
		display_dial_bar (PlayerCaps().altitude / 4, 31, 92);
}

void display_cabin_temp (void)
{
	if (PlayerCaps().cabTemp > 3)
		display_dial_bar (PlayerCaps().cabTemp / 4, 31, 60);
}


void display_laser_temp (void)
{
	if (PlayerDefense().laserHeat > 0)
		display_dial_bar (PlayerDefense().laserHeat / 4, 31, 76);
}


/*
 * Display the energy banks.
 */

void display_energy (void)
{
	int e1,e2,e3,e4;

	e1 = PlayerDefense().energy > 64 ? 64 : PlayerDefense().energy;
	e2 = PlayerDefense().energy > 128 ? 64 : PlayerDefense().energy - 64;
	e3 = PlayerDefense().energy > 192 ? 64 : PlayerDefense().energy - 128;
	e4 = PlayerDefense().energy - 192;  	
	
	if (e4 > 0)
		display_dial_bar (e4, 416, 61);

	if (e3 > 0)
		display_dial_bar (e3, 416, 79);

	if (e2 > 0)
		display_dial_bar (e2, 416, 97);

	if (e1 > 0)
		display_dial_bar (e1, 416, 115);
}



void display_flight_roll (void)
{
	int sx,sy;
	int i;
	int pos;

	sx = 416;
	sy = 384 + 9 + 14;

	pos = sx - ((PlayerFlight().roll * 28) / PlayerCaps().maxRoll);
	pos += 32;

	for (i = 0; i < 4; i++)
	{
		hud_line(pos + i, sy, pos + i, sy + 7, GFX_COL_GOLD);
	}
}

void display_flight_climb (void)
{
	int sx,sy;
	int i;
	int pos;

	sx = 416;
	sy = 384 + 9 + 14 + 16;

	pos = sx + ((PlayerFlight().climb * 28) / PlayerCaps().maxClimb);
	pos += 32;

	for (i = 0; i < 4; i++)
	{
		hud_line(pos + i, sy, pos + i, sy + 7, GFX_COL_GOLD);
	}
}


void display_fuel (void)
{
	if (cmdr.fuel > 0)
		display_dial_bar ((cmdr.fuel * 64) / PlayerCaps().maxFuel, 31, 44);
}


void display_missiles (void)
{
	int nomiss;
	int x,y;

	if (cmdr.missiles == 0)
		return;
	
	nomiss = cmdr.missiles > 4 ? 4 : cmdr.missiles;

	x = (4 - nomiss) * 16 + 35;
	y = 113 + 385;
	
	if (missile_target != MISSILE_UNARMED)
	{
		hud_sprite((missile_target < 0) ? IMG_MISSILE_YELLOW :
											    IMG_MISSILE_RED, x, y);
		x += 16;
		nomiss--;
	}

	for (; nomiss > 0; nomiss--)
	{
		hud_sprite(IMG_MISSILE_GREEN, x, y);
		x += 16;
	}
}


// I2 info card: a compact readout for the currently SELECTED entity
// (g_missile_lock_target) - its name (players), kind, legal status and range -
// drawn top-left of the flight view. Nothing shows when nothing is selected or the
// selection is off-screen (out of the AOI); it clears automatically the frame the
// entity despawns because Sample() then fails.
static void display_selection_info (void)
{
	if (g_missile_lock_target == 0xFFFFFFFFu)
		return;

	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	Neuron::Net::EntitySnapshot ts{};
	if (!rc.IsOpen() || !rc.Sample (g_missile_lock_target, 1.0, ts))
		return;   // not currently replicated: no card (and no stale one)

	const long long* org = camera_rig_origin();
	const double dx = (double) ts.x - (double) org[0];
	const double dy = (double) ts.y - (double) org[1];
	const double dz = (double) ts.z - (double) org[2];
	const long dist = (long) sqrt (dx * dx + dy * dy + dz * dz);

	const char* kind;
	if (ts.type == SHIP_PLANET)                             kind = "PLANET";
	else if (ts.type == SHIP_CORIOLIS || ts.type == SHIP_DODEC) kind = "STATION";
	else if (ts.type == SHIP_CARGO)                         kind = "CARGO";
	else if (ts.type < 0)                                   kind = "OBJECT";   // sun etc.
	else                                                    kind = "SHIP";

	hud_set_origin (0, 0);

	// Line 1: name (players) or kind; line 2: legal status + range. The roster join
	// (I3) supplies the player name/wanted; NPCs and objects show their kind only.
	const char* name = roster_name (g_missile_lock_target);
	const int   wanted = roster_wanted (g_missile_lock_target);

	char line[64];
	if (name != nullptr)
		snprintf (line, sizeof (line), "TARGET %s", name);
	else
		snprintf (line, sizeof (line), "TARGET %s", kind);
	hud_text (16, 16, line, GFX_COL_YELLOW_2);

	const char* legal = (wanted < 0) ? "" : (wanted > 0 ? "WANTED " : "CLEAN ");
	char line2[64];
	snprintf (line2, sizeof (line2), "%s%s  %ld", legal, name != nullptr ? kind : "", dist);
	hud_text (16, 28, line2, (wanted > 0) ? GFX_COL_RED : GFX_COL_GREY_1);
}


// I3 move gizmo (interaction.md §3.4): while an RMB move drag is in progress, draw
// the command plane through the ship (a depth-faded ring + cross on the camera-up
// plane), the route line from the ship to the marker, the vertical elevation stem,
// and the destination marker. State (relative-frame ship / plane normal / base /
// marker) is set in main.cpp; this only projects and draws it.
static bool project_relative (double _rx, double _ry, double _rz, double& _sx, double& _sy)
{
	struct vector p; p.x = _rx; p.y = _ry; p.z = _rz;
	camera_view_point (&p);
	if (p.z <= 0.0)
		return false;
	const auto sz = Neuron::Graphics::Core::GetOutputSize();
	return Neuron::Client::CameraSpaceToPixels (Neuron::Client::MainCamera(),
		p.x, p.y, p.z, static_cast<int>(sz.Width), static_cast<int>(sz.Height), _sx, _sy);
}

void draw_move_gizmo (void)
{
	if (!g_gizmo_active || !camera_rig_ready())
		return;

	hud_set_origin (0, 0);

	// Two in-plane axes perpendicular to the plane normal, for the grid.
	struct vector n; n.x = g_gizmo_normal[0]; n.y = g_gizmo_normal[1]; n.z = g_gizmo_normal[2];
	struct vector ref; ref.x = 0; ref.y = 0; ref.z = 0;
	if (fabs (n.y) < 0.9) ref.y = 1.0; else ref.x = 1.0;
	struct vector e1, e2;
	// e1 = normalize(ref x n); e2 = n x e1
	e1.x = ref.y * n.z - ref.z * n.y;
	e1.y = ref.z * n.x - ref.x * n.z;
	e1.z = ref.x * n.y - ref.y * n.x;
	double e1len = sqrt (e1.x * e1.x + e1.y * e1.y + e1.z * e1.z);
	if (e1len < 1e-6) return;
	e1.x /= e1len; e1.y /= e1len; e1.z /= e1len;
	e2.x = n.y * e1.z - n.z * e1.y;
	e2.y = n.z * e1.x - n.x * e1.z;
	e2.z = n.x * e1.y - n.y * e1.x;

	// Grid radius ~ 18% of the ship->base distance (a readable tactical patch).
	const double bx = g_gizmo_base[0] - g_gizmo_ship[0];
	const double by = g_gizmo_base[1] - g_gizmo_ship[1];
	const double bz = g_gizmo_base[2] - g_gizmo_ship[2];
	double r = 0.18 * sqrt (bx * bx + by * by + bz * bz);
	if (r < 200.0) r = 200.0;

	// A ring on the plane around the base (16 segments), depth-faded (dim blue).
	double px = 0.0, py = 0.0, first_x = 0.0, first_y = 0.0;
	bool have_prev = false, have_first = false;
	for (int i = 0; i <= 16; ++i)
	{
		const double a = (2.0 * 3.14159265 * i) / 16.0;
		const double ox = cos (a) * r, oy = sin (a) * r;
		const double wx = g_gizmo_base[0] + e1.x * ox + e2.x * oy;
		const double wy = g_gizmo_base[1] + e1.y * ox + e2.y * oy;
		const double wz = g_gizmo_base[2] + e1.z * ox + e2.z * oy;
		double sx = 0.0, sy = 0.0;
		if (project_relative (wx, wy, wz, sx, sy))
		{
			if (have_prev)
				hud_line ((int) px, (int) py, (int) sx, (int) sy, GFX_COL_BLUE_2);
			px = sx; py = sy; have_prev = true;
			if (!have_first) { first_x = sx; first_y = sy; have_first = true; }
		}
		else
			have_prev = false;
	}
	(void) first_x; (void) first_y;

	// Route line ship -> marker, and the vertical elevation stem base -> marker.
	double shipSx = 0.0, shipSy = 0.0, baseSx = 0.0, baseSy = 0.0, markSx = 0.0, markSy = 0.0;
	const bool shipOk = project_relative (g_gizmo_ship[0], g_gizmo_ship[1], g_gizmo_ship[2], shipSx, shipSy);
	const bool baseOk = project_relative (g_gizmo_base[0], g_gizmo_base[1], g_gizmo_base[2], baseSx, baseSy);
	const bool markOk = project_relative (g_gizmo_point[0], g_gizmo_point[1], g_gizmo_point[2], markSx, markSy);

	if (shipOk && markOk)
		hud_line ((int) shipSx, (int) shipSy, (int) markSx, (int) markSy, GFX_COL_CYAN);
	if (baseOk && markOk)
		hud_line ((int) baseSx, (int) baseSy, (int) markSx, (int) markSy, GFX_COL_WHITE);

	if (markOk)
	{
		const int box = 24;
		hud_sprite_scaled (IMG_TARGET_LOCK, (int) markSx - box / 2, (int) markSy - box / 2, box, box);
	}
}


// I3 radial context menu (interaction.md §3.3): a ring of order labels around the
// press point; the highlighted slice (g_radial_hot) draws bright. State is laid out
// in main.cpp; this only draws it.
void draw_radial_menu (void)
{
	if (!g_radial_open)
		return;

	hud_set_origin (0, 0);
	for (int i = 0; i < g_radial_count; ++i)
	{
		const bool hot = (i == g_radial_hot);
		const int w = 64, h = 18;
		const int x = g_radial_cx[i] - w / 2;
		const int y = g_radial_cy[i] - h / 2;
		hud_rect (x, y, x + w, y + h, hot ? GFX_COL_YELLOW_2 : GFX_COL_GREY_1);
		const char* label = g_radial_labels[i] ? g_radial_labels[i] : "";
		const int len = (int) strlen (label);
		hud_text (x + (w - len * 8) / 2, y + 5, label, hot ? GFX_COL_WHITE : GFX_COL_GREY_3);
	}
}


// I3 pointer-command feedback: a short-lived toast (what was ordered / why it was
// refused) and, for a Move order, a marker projected at the destination point. The
// state lives in main.cpp (set when an order is sent and by the UnitOrderAck
// subscriber); this only draws it. Entity-targeted orders reuse the target reticle
// (drawn on g_missile_lock_target), so only Move needs its own world marker.
static void display_order_feedback (void)
{
	if (g_order_toast_timer > 0)
	{
		hud_set_origin (0, 0);
		hud_text (220, 40, g_order_toast, g_order_toast_col);
		--g_order_toast_timer;
	}

	if (g_order_kind == 0 || !g_order_has_point)
		return;

	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	if (!rc.IsOpen() || !camera_rig_ready())
		return;

	const long long* org = camera_rig_origin();
	struct vector p;
	p.x = (double) (g_order_point[0] - org[0]);
	p.y = (double) (g_order_point[1] - org[1]);
	p.z = (double) (g_order_point[2] - org[2]);
	camera_view_point (&p);
	if (p.z <= 0.0)
		return;   // the Move point is behind the camera

	const auto sz = Neuron::Graphics::Core::GetOutputSize();
	const int vw = static_cast<int>(sz.Width);
	const int vh = static_cast<int>(sz.Height);
	double sx = 0.0, sy = 0.0;
	if (Neuron::Client::CameraSpaceToPixels (Neuron::Client::MainCamera(), p.x, p.y, p.z, vw, vh, sx, sy))
	{
		hud_set_origin (0, 0);
		const int box = 24;
		hud_sprite_scaled (IMG_TARGET_LOCK, (int) sx - box / 2, (int) sy - box / 2, box, box);
	}
}


// Draw the cockpit dashboard + flight overlays into the native HUD pass. Called from
// RenderGameHud (HudRender.cpp) inside its Render2D Begin/End during RenderCanvas, so it
// gates itself: the dashboard shows only while actually flying (connected, undocked, front
// view). It reads the frame's already-populated state (render_replicated_objects ran during
// RenderScene), so nothing here mutates game state - it is pure drawing.
void update_console (void)
{
	if (!Neuron::Client::ReplicationClientInstance().IsOpen() || docked || current_screen != SCR_FRONT_VIEW)
		return;

	// Float the classic 512-wide dashboard to the bottom-centre of the window; every draw
	// below picks up this origin, so the layout is unchanged - it just slides as a unit.
	// (No clip: the whole HUD draws in the full client window now. The gfx2d path left a
	// stale scanner scissor that clipped the top-anchored overlays below out of view.)
	const auto sz = Neuron::Graphics::Core::GetOutputSize();
	int hud_ox = (static_cast<int>(sz.Width) - 512) / 2;
	int hud_oy = static_cast<int>(sz.Height) - 514;
	if (hud_ox < 0) hud_ox = 0;
	if (hud_oy < 0) hud_oy = 0;
	hud_set_origin (hud_ox, hud_oy);

	hud_scanner();

	display_speed();
	display_flight_climb();
	display_flight_roll();
	display_shields();
	display_altitude();
	display_energy();
	display_cabin_temp();
	display_laser_temp();
	display_fuel();
	display_missiles();

	update_scanner();
	update_compass();

	if (ship_count[SHIP_CORIOLIS] || ship_count[SHIP_DODEC])
		hud_sprite(IMG_BIG_S, 387, 490);

	if (ecm_active)
		hud_sprite(IMG_BIG_E, 115, 490);

	// I2/I3/I4 overlays LAST: they reset the draw origin to (0,0) for their own
	// full-view placement, so they must run after the dashboard-anchored draws.
	display_selection_info();
	display_order_feedback();
	draw_move_gizmo();
	draw_radial_menu();
	draw_ability_bar();
	draw_nav_strip();
	draw_chat();

	hud_set_origin (0, 0);
}

void jump_warp (void)
{
	// The server owns the mass-lock rules and moves the ship (G7); ask it to
	// jump and let the new position ride the snapshot stream. The local
	// star-warp visual still fires for immediate feedback; a MassLocked
	// TravelResponse shows the classic message (see main.cpp).
	Neuron::Msg::TravelRequest req;
	req.kind = Neuron::Msg::TravelKind::InSystemJump;
	Neuron::Client::ReplicationClientInstance().Send(req);
	warp_stars = 1;
	mcount &= 63;
}


// Defined in GameWindows.cpp; closes the docked station-menu window on undock (declared
// here to keep this legacy TU off the winrt/GUI headers).
void CloseStationMenu (void);

void launch_player (void)
{
	docked = 0;

	// Tell the server we've undocked; the launch offset and the world around us
	// arrive on the snapshot stream.
	{
		Neuron::Net::StationRequest req;
		req.kind = Neuron::Net::StationRequestKind::Undock;
		Neuron::Client::ReplicationClientInstance().SendStationRequest(req);
	}

	// Piloting is retired (camera-only client): the hull launches at rest and
	// idles outside the station; the player flies the CAMERA around it.
	PlayerFlight().speed = 0;
	PlayerFlight().roll = 0;
	PlayerFlight().climb = 0;
	create_new_stars();
	clear_local_objects();

	current_screen = SCR_FRONT_VIEW;   // launch straight into the camera-space flight view
	CloseStationMenu();                // leave the station: dismiss the hub window
	snd_play_sample (SND_LAUNCH);
}



/*
 * Engage the docking computer: request a dock when genuinely within the
 * server's docking range (the server validates its own DOCK_RANGE and replies;
 * the docked flow starts on the StationResponse, never locally).
 */

void engage_docking_computer (void)
{
	if ((ship_count[SHIP_CORIOLIS] || ship_count[SHIP_DODEC]) &&
		s_nearest_station_dist < 5000.0)
	{
		request_dock();
	}
}



// ---- Weapon visuals (presentation only; moved from the retired swat.cpp) -----

// Reset the weapon HUD/visual state (docking, respawn). The authoritative laser
// heat and ECM state are the server's; these are just the local indicators.
void reset_weapons (void)
{
	laser_counter = 0;
	laser = 0;
	ecm_active = 0;
	missile_target = MISSILE_UNARMED;
}


// Trigger the laser-beam visual for this frame's fire intent (the shot itself is
// resolved by the server from InputCommand.fire; damage and heat come back via
// PlayerStatus). Honours the server-mirrored trigger lock (laserTemp >= 242) and
// the legacy pulse pacing so the beam flashes like the original. Returns the
// number of frames to draw the beam (0 = no laser / too hot). The beam itself is
// the ship's muzzle bolt: while draw_lasers counts down, the local hull's render
// record carries FLG_FIRING and draw_ship_laser draws it - the old cockpit
// corner-beams went with the cockpit view.
int fire_laser (void)
{
	if ((laser_counter == 0) && (PlayerDefense().laserHeat < 242))
	{
		laser = cmdr.front_laser;

		if (laser != 0)
		{
			laser_counter = (laser > 127) ? 0 : (laser & 0xFA);
			laser &= 127;

			snd_play_sample (SND_PULSE);

			return 2;
		}
	}

	return 0;
}


// Advance the beam-visual pacing each frame. (The authoritative laser heat cools
// server-side and rides PlayerStatus; nothing to simulate here.)
void cool_laser (void)
{
	laser = 0;

	if (laser_counter > 0)
		laser_counter--;

	if (laser_counter > 0)
		laser_counter--;
}


// Count the E indicator down after an EcmPulse event lit it.
void time_ecm (void)
{
	if (ecm_active != 0)
		ecm_active--;
}
