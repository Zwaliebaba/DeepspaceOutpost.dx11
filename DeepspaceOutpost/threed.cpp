#include "pch.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#include "config.h"
#include "elite.h"
#include "gfx.h"
#include "Scene3D.h" // Neuron::Graphics::Scene3D::SubmitModel - 3D models straight to the scene pass
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
 * pixels using the current frame's optics (gfx_view_metrics). This replaces the
 * old fixed "(r*256)/z + 128/96, *GFX_SCALE" inline, so the 3D follows the
 * window size; at the legacy 4:3 viewport it produces the same pixels.
 */
static inline void project_to_screen (double rx, double ry, double rz, int *sx, int *sy)
{
	double fx, fy;
	Neuron::Client::ProjectPoint (gfx_view_metrics(), rx, ry, rz, fx, fy);
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
	struct ship_data *ship = ship_list[obj->type];

	/* Emit the ship as a GPU 3D model. Scene3D applies the model->camera rotation
	 * (transpose of obj->rotmat) + translation, projects it with a real perspective and
	 * resolves visibility with the hardware z-buffer - replacing the old CPU vertex
	 * projection, signed-area backface test and painter's-sorted 2D polygons. */
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

	/* The laser bolt stays on the 2D path for now: project just the muzzle vertex
	 * through the same transform the GPU uses and draw the depth-sorted 2D line. */
	if (obj->flags & FLG_FIRING)
	{
		Matrix trans_mat;
		double tmp;
		struct vector vec;
		double rx, ry, rz;
		int sx, sy;
		int lasv;
		int col;

		for (int i = 0; i < 3; i++)
			trans_mat[i] = obj->rotmat[i];

		tmp = trans_mat[0].y; trans_mat[0].y = trans_mat[1].x; trans_mat[1].x = tmp;
		tmp = trans_mat[0].z; trans_mat[0].z = trans_mat[2].x; trans_mat[2].x = tmp;
		tmp = trans_mat[1].z; trans_mat[1].z = trans_mat[2].y; trans_mat[2].y = tmp;

		lasv = ship->front_laser;
		vec.x = ship->points[lasv].x;
		vec.y = ship->points[lasv].y;
		vec.z = ship->points[lasv].z;
		mult_vector (&vec, trans_mat);

		rx = vec.x + obj->location.x;
		ry = vec.y + obj->location.y;
		rz = vec.z + obj->location.z;
		if (rz <= 0)
			rz = 1;

		project_to_screen (rx, ry, rz, &sx, &sy);

		const Neuron::Client::ViewMetrics& vm = gfx_view_metrics();
		col = (obj->type == SHIP_VIPER) ? GFX_COL_CYAN : GFX_COL_WHITE;

		gfx_render_line (sx, sy,
						 obj->location.x > 0 ? 0 : vm.width - 1, (rand255() * vm.height) / 256,
						 (int) rz, col);
	}
}





/*
 * Draw a planet as a lit 3D sphere.
 */

void draw_planet (struct local_object *planet)
{
	if (planet->location.z <= 0)
		return;

	/* Emit the planet as a real 3D sphere: a lit UV-sphere mesh (built by SceneMeshes),
	 * drawn through the same depth-tested mesh pipeline as the ships - replacing the old
	 * camera-facing billboard disk. Scene3D applies the model->camera rotation + translation
	 * and the hardware z-buffer resolves occlusion against the ships. The colour is baked into
	 * the mesh, so this uses the ship (per-vertex, lit) colour path (md.colour = -1). */
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
	if (planet->location.z <= 0)
		return;

	/* Emit the sun as a GPU billboard (depth-tested radial-gradient disk), replacing the
	 * per-pixel render_sun rasterizer. Scene3D draws the white->yellow->orange bands. */
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
					gfx_plot_pixel (px+psx, py+psy, GFX_COL_WHITE);
		}
	}

	set_rand_seed (old_seed);
}



/*
 * Draws an object in local space.
 * (Ship, Planet, Sun etc).
 */

void draw_ship (struct local_object *ship)
{

	if ((current_screen != SCR_FRONT_VIEW) &&
		(current_screen != SCR_INTRO_ONE) && (current_screen != SCR_INTRO_TWO) &&
		(current_screen != SCR_GAME_OVER) && (current_screen != SCR_ESCAPE_POD))
		return;
	
	if ((ship->flags & FLG_DEAD) && !(ship->flags & FLG_EXPLOSION))
	{
		ship->flags |= FLG_EXPLOSION;
		ship->exp_seed = randint();
		ship->exp_delta = 18; 
	}

	if (ship->flags & FLG_EXPLOSION)
	{
		draw_explosion (ship);
		return;
	}
	
	if (ship->location.z <= 0)	/* Only display ships in front of us. */
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
	
	/* Field-of-vision cull against the real (aspect-aware) frustum, so ships at
	 * the edges of a wide window are not dropped early. */
	const Neuron::Client::ViewMetrics& vm = gfx_view_metrics();
	if ((fabs(ship->location.x) > Neuron::Client::HalfExtentX (vm, ship->location.z)) ||
		(fabs(ship->location.y) > Neuron::Client::HalfExtentY (vm, ship->location.z)))
		return;

	draw_solid_ship (ship);
}

