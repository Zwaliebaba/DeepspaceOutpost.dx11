#include "pch.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#include "elite.h"
#include "gfx.h"
#include "Scene3D.h" // Neuron::Graphics::Scene3D::SubmitModel - 3D models straight to the scene pass
#include "Camera.h"  // MainCamera() - the CPU paths project through the same optics as the GPU
#include "CameraRig.h"
#include "planet.h"
#include "vector.h"
#include "shipdata.h"
#include "shipface.h"
#include "threed.h"
#include "space.h"
#include "random.h"


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



void draw_explosion (struct local_object *obj)
{
	int i;
	int z;
	int q;
	int pr;
	int px,py;
	int cnt;
	int sizex,sizey,psx,psy;
	Matrix trans_mat;
	int sx,sy;
	double rx,ry,rz;
	int visible[32];
	struct vector vec;
	struct vector camera_vec;
	double cos_angle;
	double tmp;
	struct ship_face_normal *ship_norm;
	struct ship_point *sp;
	struct ship_data *ship;
	int np;
	int old_seed;
	
	
	if (obj->exp_delta > 251)
	{
		obj->flags |= FLG_REMOVE;
		return;
	}
	
	obj->exp_delta += 4;

	if (obj->location.z <= 0)
		return;

	ship = ship_list[obj->type];
	
	for (i = 0; i < 3; i++)
		trans_mat[i] = obj->rotmat[i];
		
	camera_vec = obj->location;
	mult_vector (&camera_vec, trans_mat);
	camera_vec = unit_vector (&camera_vec);
	
	ship_norm = ship->normals;
	
	for (i = 0; i < ship->num_faces; i++)
	{
		vec.x = ship_norm[i].x;
		vec.y = ship_norm[i].y;
		vec.z = ship_norm[i].z;

		vec = unit_vector (&vec);
		cos_angle = vector_dot_product (&vec, &camera_vec);

		visible[i] = (cos_angle < -0.13);
	}

	tmp = trans_mat[0].y;
	trans_mat[0].y = trans_mat[1].x;
	trans_mat[1].x = tmp;

	tmp = trans_mat[0].z;
	trans_mat[0].z = trans_mat[2].x;
	trans_mat[2].x = tmp;

	tmp = trans_mat[1].z;
	trans_mat[1].z = trans_mat[2].y;
	trans_mat[2].y = tmp;
	
	sp = ship->points;
	np = 0;
	
	for (i = 0; i < ship->num_points; i++)
	{
		if (visible[sp[i].face1] || visible[sp[i].face2] ||
			visible[sp[i].face3] || visible[sp[i].face4])
		{
			vec.x = sp[i].x;
			vec.y = sp[i].y;
			vec.z = sp[i].z;

			mult_vector (&vec, trans_mat);

			rx = vec.x + obj->location.x;
			ry = vec.y + obj->location.y;
			rz = vec.z + obj->location.z;

			project_to_screen (rx, ry, rz, &sx, &sy);

			point_list[np].x = sx;
			point_list[np].y = sy;
			np++;
		}
	}

	
	z = (int)obj->location.z;
	
	if (z >= 0x2000)
		q = 254;
	else
		q = (z / 32) | 1;

	pr = (obj->exp_delta * 256) / q;
	
//	if (pr > 0x1C00)
//		q = 254;
//	else

	q = pr / 32;	
		
	old_seed = get_rand_seed();
	set_rand_seed (obj->exp_seed);

	for (cnt = 0; cnt < np; cnt++)
	{
		sx = point_list[cnt].x;
		sy = point_list[cnt].y;
	
		for (i = 0; i < 16; i++)
		{
			px = rand255() - 128;
			py = rand255() - 128;		

			px = (px * q) / 256;
			py = (py * q) / 256;
		
			px = px + px + sx;
			py = py + py + sy;

			sizex = (randint() & 1) + 1;
			sizey = (randint() & 1) + 1;

			for (psy = 0; psy < sizey; psy++)
				for (psx = 0; psx < sizex; psx++)		
					hud_plot_pixel (px+psx, py+psy, GFX_COL_WHITE);
		}
	}

	set_rand_seed (old_seed);
}



/*
 * Draws an object handed in the WORLD frame (floating-origin-relative position +
 * world basis). The camera is decoupled from the ship: a camera-space copy is
 * built here through the Camera's view matrix for everything the CPU still does
 * (behind-the-eye and frustum culls, the explosion debris, the firing beam),
 * while the meshes are submitted world-frame and Scene3D applies view*projection
 * on the GPU. Explosion state advanced on the copy is written back to the
 * caller's object (the animation persists across frames).
 */

void draw_ship (struct local_object *ship)
{

	if ((current_screen != SCR_FRONT_VIEW) &&
		(current_screen != SCR_INTRO_ONE) && (current_screen != SCR_INTRO_TWO) &&
		(current_screen != SCR_GAME_OVER) && (current_screen != SCR_ESCAPE_POD))
		return;

	struct local_object cam = *ship;
	camera_view_object (&cam);

	if ((cam.flags & FLG_DEAD) && !(cam.flags & FLG_EXPLOSION))
	{
		cam.flags |= FLG_EXPLOSION;
		cam.exp_seed = randint();
		cam.exp_delta = 18;
	}

	if (cam.flags & FLG_EXPLOSION)
	{
		draw_explosion (&cam);
		ship->flags = cam.flags;
		ship->exp_seed = cam.exp_seed;
		ship->exp_delta = cam.exp_delta;
		return;
	}

	if (cam.location.z <= 0)	/* Only display objects in front of the camera. */
		return;

	if (ship->type == SHIP_PLANET)
	{
		draw_planet (ship);
		return;
	}

	if (ship->type == SHIP_SUN)
	{
		draw_sun (ship);
		return;
	}

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

	draw_solid_ship (ship);       // world-frame mesh; Scene3D applies the view + projection
}

