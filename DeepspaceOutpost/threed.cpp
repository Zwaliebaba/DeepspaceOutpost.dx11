#include "pch.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#include "elite.h"
#include "GamePalette.h"
#include "GameScene.h"
#include "Scene3D.h" // Neuron::Graphics::Scene3D::SubmitModel - 3D models straight to the scene pass
#include "Camera.h"  // MainCamera() - the CPU paths project through the same optics as the GPU
#include "CameraRig.h"
#include "planet.h"
#include "vector.h"
#include "shipdata.h"
#include "shipface.h"
#include "threed.h"
#include "space.h"
#include "RenderTable.h"   // H1: NetType -> render descriptor table


static struct point point_list[100];


/*
 * Project a camera-space point (x right, y up, z forward) to integer screen
 * pixels through the main Camera's projection - the same optics the GPU scene
 * pass uses, so the CPU-drawn effects (explosion debris, firing beams) land on
 * the same pixels as the meshes.
 */
static inline void project_to_screen (double rx, double ry, double rz, int *sx, int *sy)
{
	int w, h;
	gfx_scene_size (&w, &h);

	double fx, fy;
	if (!Neuron::Client::CameraSpaceToPixels (Neuron::Client::MainCamera(), rx, ry, rz, w, h, fx, fy))
	{
		*sx = 0;
		*sy = 0;
		return;
	}
	*sx = (int) fx;
	*sy = (int) fy;
}


/*
 * Hacked version of the draw ship routine to display solid ships...
 * This needs a lot of tidying...
 *
 * Check for hidden surface supplied by T.Harte.
 */

void draw_solid_ship (struct local_object *obj)
{
	/* Emit the ship as a GPU 3D model in the WORLD frame (floating-origin-relative
	 * position + world basis). Scene3D composes it with the Camera's view and
	 * projection and resolves visibility with the hardware z-buffer - replacing the
	 * old CPU vertex projection, backface test and painter's-sorted 2D polygons. */
	Neuron::Render::ModelDraw md;
	md.type = obj->type;
	md.colour = -1;
	md.flags = obj->flags;
	md.location[0] = obj->location.x;
	md.location[1] = obj->location.y;
	md.location[2] = obj->location.z;
	for (int i = 0; i < 3; i++)
	{
		md.rotmat[i][0] = obj->rotmat[i].x;
		md.rotmat[i][1] = obj->rotmat[i].y;
		md.rotmat[i][2] = obj->rotmat[i].z;
	}
	md.distance = obj->distance;
	Neuron::Graphics::Scene3D::SubmitModel (md);
}


// The firing-beam visual (draw_ship_laser) was a first-person effect: it drew a 2D bolt
// from the muzzle to a random SCREEN EDGE, which is meaningless in the third-person camera
// (like the warp streaks and the break pattern). Removed - laser hits are server-resolved;
// a world-space beam VFX can be added to Scene3D later if wanted.





/*
 * Draw a planet as a lit 3D sphere.
 */

void draw_planet (struct local_object *planet)
{
	/* Emit the planet as a real 3D sphere: a lit UV-sphere mesh (built by SceneMeshes),
	 * drawn through the same depth-tested mesh pipeline as the ships, in the world frame
	 * (Scene3D applies the Camera's view + projection); the hardware z-buffer resolves
	 * occlusion against the ships. The colour is baked into the mesh, so this uses the
	 * ship (per-vertex, lit) colour path (md.colour = -1). The behind-the-eye guard runs
	 * in draw_ship, on the camera-space copy. */
	Neuron::Render::ModelDraw md;
	md.type = SHIP_PLANET;
	md.colour = -1;
	md.location[0] = planet->location.x;
	md.location[1] = planet->location.y;
	md.location[2] = planet->location.z;
	for (int i = 0; i < 3; i++)
	{
		md.rotmat[i][0] = planet->rotmat[i].x;
		md.rotmat[i][1] = planet->rotmat[i].y;
		md.rotmat[i][2] = planet->rotmat[i].z;
	}
	md.distance = planet->distance;

	Neuron::Graphics::Scene3D::SubmitModel (md);
}


void draw_sun (struct local_object *planet)
{
	/* Emit the sun as a GPU billboard (depth-tested radial-gradient disk), replacing the
	 * per-pixel render_sun rasterizer. Scene3D view-transforms the world-frame centre and
	 * draws the white->yellow->orange bands. */
	Neuron::Render::ModelDraw md;
	md.type = SHIP_SUN;
	md.location[0] = planet->location.x;
	md.location[1] = planet->location.y;
	md.location[2] = planet->location.z;
	md.distance = planet->distance;
	md.colour = GFX_COL_WHITE;

	Neuron::Graphics::Scene3D::SubmitModel (md);
}



/*
 * Draws an object handed in the WORLD frame (floating-origin-relative position +
 * world basis). The camera is decoupled from the ship: a camera-space copy is
 * built here through the Camera's view matrix for everything the CPU still does
 * (behind-the-eye and frustum culls, the firing beam), while the meshes are
 * submitted world-frame and Scene3D applies view*projection on the GPU.
 * (The legacy 2D pixel-spray explosion is retired: deaths go through the
 * Neuron::Client::Effects debris/particle subsystem now - see explosion.md.)
 */

void draw_ship (struct local_object *ship)
{

	if ((current_screen != SCR_FRONT_VIEW) &&
		(current_screen != SCR_INTRO_ONE) && (current_screen != SCR_INTRO_TWO) &&
		(current_screen != SCR_ESCAPE_POD))
		return;

	struct local_object cam = *ship;
	camera_view_object (&cam);

	if (cam.location.z <= 0)	/* Only display objects in front of the camera. */
		return;

	/* H1: the NetType -> render descriptor table replaces the hand-written type
	 * if-chain, so a new hull is a data row (RenderTable.h), not an edit here. */
	const RenderDescriptor rd = RenderFor (ship->type);
	if (rd.kind == RenderKind::Planet)
	{
		draw_planet (ship);
		return;
	}
	if (rd.kind == RenderKind::Sun)
	{
		draw_sun (ship);
		return;
	}
	if (rd.kind == RenderKind::Hidden)
		return;   /* not drawn by the mesh path */

	/* Field-of-vision cull against the camera's real frustum (|x| <= z*tan(fovX/2),
	 * |y| <= z*tan(fovY/2)), so ships at the edges of a wide window are not
	 * dropped early. */
	{
		Neuron::Client::Camera& camera = Neuron::Client::MainCamera();
		const double tx = Neuron::Client::CameraTanHalfFovX (camera);
		const double ty = Neuron::Client::CameraTanHalfFovY (camera);
		if ((fabs(cam.location.x) > cam.location.z * tx) ||
			(fabs(cam.location.y) > cam.location.z * ty))
			return;
	}

	/* H3 iconic LOD: a hull too distant to read as a mesh draws as a cheap contact
	 * glyph at its projected position instead (the tactical-digital look + the LOD
	 * cull in one). The mesh path handles everything nearer. */
	if (ShouldDrawAsGlyph (cam.location.z))
	{
		int vw = 0, vh = 0;
		gfx_scene_size (&vw, &vh);
		double sx = 0.0, sy = 0.0;
		if (Neuron::Client::CameraSpaceToPixels (Neuron::Client::MainCamera (),
				cam.location.x, cam.location.y, cam.location.z, vw, vh, sx, sy))
			hud_sprite_scaled_deferred (IMG_GREEN_DOT, (int) sx - 3, (int) sy - 3, 6, 6);
		return;
	}

	draw_solid_ship (ship);       // world-frame mesh; Scene3D applies the view + projection
}

