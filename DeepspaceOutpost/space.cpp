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
#include "gfx.h"
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
#include "Camera.h"      // NeuronClient: MainCamera() + CPU projection helpers
#include "CameraRig.h"   // the free camera: origin, world->camera transforms
#include "ReplicationClient.h"
#include "Messages/Defs/Travel.h"   // TravelRequest (hyperspace / jump drive)
#include "ReplicatedScene.h"


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
	constexpr int kMaxExplosionFrames = 120;   // ~2s safety cap if it never faces us
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
				gfx_draw_sprite_scaled (IMG_TARGET_LOCK, sx - (int)half, sy - (int)half, box, box);
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
			return (e.obj.flags & FLG_REMOVE) || e.frames > kMaxExplosionFrames;
		});
	}

	/* The frame's replicated 3D scene is fully submitted: draw it now, onto the cleared
	   back buffer, under the 2D HUD (the game drives the pass; no scene-marker flag). */
	gfx_render_3d_scene();
}


// Pick the missile lock target / camera selection (T key): the nearest ship near
// the CENTRE OF THE VIEW. The server then homes a missile at exactly this entity,
// so we return its replicated entity index (0xFFFFFFFF when nothing suitable is
// ahead); the orbit camera also treats it as its selected object. Planets, the
// sun, other missiles, and the player's own hull are not lockable.
unsigned int find_lock_target (void)
{
	Neuron::Client::ReplicationClient& rc = Neuron::Client::ReplicationClientInstance();
	if (!rc.IsOpen() || !camera_rig_ready())
		return 0xFFFFFFFFu;

	const long long* org = camera_rig_origin();
	std::vector<Neuron::Net::EntitySnapshot> ents = rc.SampleAll (1.0);
	std::vector<Neuron::Client::RenderRecord> records =
		Neuron::Client::BuildRenderRecords (ents, org[0], org[1], org[2]);

	unsigned int best = 0xFFFFFFFFu;
	double bestDist = 1.0e18;

	for (const Neuron::Client::RenderRecord& rec : records)
	{
		// Lockable = a ship (not the planet/sun, not another missile, not us)...
		if (rec.type < 0 || rec.type == SHIP_MISSILE)
			continue;
		if (rec.id == rc.LocalPlayer())
			continue;

		// ...in front of the camera and inside the central cone of the view.
		struct vector camPos = rec.location;
		camera_view_point (&camPos);
		if (camPos.z <= 0.0)
			continue;
		if (fabs (camPos.x) > camPos.z || fabs (camPos.y) > camPos.z)
			continue;

		if (rec.distance < bestDist)
		{
			bestDist = rec.distance;
			best = rec.id;
		}
	}

	return best;
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
			
		gfx_draw_colour_line (x1+2, y2,   x1-3, y2, colour);
		gfx_draw_colour_line (x1+2, y2+1, x1-3, y2+1, colour);
		gfx_draw_colour_line (x1+2, y2+2, x1-3, y2+2, colour);
		gfx_draw_colour_line (x1+2, y2+3, x1-3, y2+3, colour);


		gfx_draw_colour_line (x1,   y1, x1,   y2, colour);
		gfx_draw_colour_line (x1+1, y1, x1+1, y2, colour);
		gfx_draw_colour_line (x1+2, y1, x1+2, y2, colour);
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
		gfx_draw_sprite (IMG_RED_DOT, compass_x, compass_y);
	}
	else
	{
		gfx_draw_sprite (IMG_GREEN_DOT, compass_x, compass_y);
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
		gfx_draw_colour_line (sx, sy + i, sx + len, sy + i, colour);
	}
}


/*
 * Draw an indicator bar.
 * Used for shields and energy banks.
 */

void display_dial_bar (int len, int x, int y)
{
	int i = 0;

	gfx_draw_colour_line (x, y + 384, x + len, y + 384, GFX_COL_GOLD);
	i++;
	gfx_draw_colour_line (x, y + i + 384, x + len, y + i + 384, GFX_COL_GOLD);
	
	for (i = 2; i < 7; i++)
		gfx_draw_colour_line (x, y + i + 384, x + len, y + i + 384, GFX_COL_YELLOW_1);

	gfx_draw_colour_line (x, y + i + 384, x + len, y + i + 384, GFX_COL_DARK_RED);
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
		gfx_draw_colour_line (pos + i, sy, pos + i, sy + 7, GFX_COL_GOLD);
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
		gfx_draw_colour_line (pos + i, sy, pos + i, sy + 7, GFX_COL_GOLD);
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
		gfx_draw_sprite ((missile_target < 0) ? IMG_MISSILE_YELLOW :
											    IMG_MISSILE_RED, x, y);
		x += 16;
		nomiss--;
	}

	for (; nomiss > 0; nomiss--)
	{
		gfx_draw_sprite (IMG_MISSILE_GREEN, x, y);
		x += 16;
	}
}


void update_console (void)
{
	// Float the classic 512x514 dashboard to the bottom-centre of the window when
	// the 3D fills the screen (no-op offset in retro mode). gfx_set_clip_region
	// and every draw below pick up this origin, so the layout is unchanged - it
	// just slides as a unit.
	int hud_ox, hud_oy;
	gfx_hud_anchor (&hud_ox, &hud_oy);
	gfx_set_draw_origin (hud_ox, hud_oy);

	gfx_set_clip_region (0, 0, 512, 512);
	gfx_draw_scanner();

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

	if (docked)
	{
		gfx_set_draw_origin (0, 0);
		return;
	}

	update_scanner();
	update_compass();

	if (ship_count[SHIP_CORIOLIS] || ship_count[SHIP_DODEC])
		gfx_draw_sprite (IMG_BIG_S, 387, 490);

	if (ecm_active)
		gfx_draw_sprite (IMG_BIG_E, 115, 490);

	gfx_set_draw_origin (0, 0);
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

	current_screen = SCR_BREAK_PATTERN;
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
