#include "pch.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

#include "config.h"
#include "elite.h"
#include "gfx.h"
#include "RenderContext.h"
#include "planet.h"
#include "vector.h"
#include "shipdata.h"
#include "shipface.h"
#include "threed.h"
#include "space.h"
#include "random.h"

#define MAX(x,y) (((x) > (y)) ? (x) : (y))


#define LAND_X_MAX	128
#define LAND_Y_MAX	128

static unsigned char landscape[LAND_X_MAX+1][LAND_Y_MAX+1];

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
 * The following routine is used to draw a wireframe represtation of a ship.
 *
 * caveat: it is a work in progress.
 * A number of features (such as not showing detail at distance) have not yet been implemented.
 *
 */

void draw_wireframe_ship (struct local_object *obj)
{
	Matrix trans_mat;
	int i;
	int sx,sy,ex,ey;
	double rx,ry,rz;
	int visible[32];
	Vector vec;
	Vector camera_vec;
	double cos_angle;
	double tmp;
	struct ship_face_normal *ship_norm;
	int num_faces;
	struct ship_data *ship;
	int lasv;

	ship = ship_list[obj->type];
	
	for (i = 0; i < 3; i++)
		trans_mat[i] = obj->rotmat[i];
		
	camera_vec = obj->location;
	mult_vector (&camera_vec, trans_mat);
	camera_vec = unit_vector (&camera_vec);
	
	num_faces = ship->num_faces;
	
	for (i = 0; i < num_faces; i++)
	{
		ship_norm = ship->normals;

		vec.x = ship_norm[i].x;
		vec.y = ship_norm[i].y;
		vec.z = ship_norm[i].z;

		if ((vec.x == 0) && (vec.y == 0) && (vec.z == 0))
			visible[i] = 1;
		else
		{
			vec = unit_vector (&vec);
			cos_angle = vector_dot_product (&vec, &camera_vec);
			visible[i] = (cos_angle < -0.2);
		}
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

	for (i = 0; i < ship->num_points; i++)
	{
		vec.x = ship->points[i].x;
		vec.y = ship->points[i].y;
		vec.z = ship->points[i].z;

		mult_vector (&vec, trans_mat);

		rx = vec.x + obj->location.x;
		ry = vec.y + obj->location.y;
		rz = vec.z + obj->location.z;

		project_to_screen (rx, ry, rz, &sx, &sy);

		point_list[i].x = sx;
		point_list[i].y = sy;

	}

	for (i = 0; i < ship->num_lines; i++)
	{
		if (visible[ship->lines[i].face1] ||
			visible[ship->lines[i].face2])
		{
			sx = point_list[ship->lines[i].start_point].x;
			sy = point_list[ship->lines[i].start_point].y;

			ex = point_list[ship->lines[i].end_point].x;
			ey = point_list[ship->lines[i].end_point].y;

			ActiveRenderQueue().Line (sx, sy, ex, ey);
		}
	}


	if (obj->flags & FLG_FIRING)
	{
		const Neuron::Client::ViewMetrics& vm = gfx_view_metrics();
		lasv = ship_list[obj->type]->front_laser;
		ActiveRenderQueue().Line (point_list[lasv].x, point_list[lasv].y,
					   obj->location.x > 0 ? 0 : vm.width - 1, (rand255() * vm.height) / 256);
	}
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
	md.style = 0;
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
	ActiveRenderQueue().DrawModel (md);

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

		ActiveRenderQueue().RenderLine (sx, sy,
						 obj->location.x > 0 ? 0 : vm.width - 1, (rand255() * vm.height) / 256,
						 (int) rz, col);
	}
}





/*
 * Colour map used to generate a SNES-style planet.
 * This is a quick hack and needs tidying up.
 */

int snes_planet_colour[] =
{
	102, 102,
	134, 134, 134, 134,
	167, 167, 167, 167,
	213, 213,
	255,
	83,83,83,83,
	122,
	83,83,
	249,249,249,249, 
	83,
	122,
	249,249,249,249,249,249,
	83, 83,
	122,
	83,83, 83, 83,
	255,
	213, 213,
	167,167, 167, 167,
	134,134, 134, 134,
	102, 102
}; 


/*
 * Generate a landscape map for a SNES-style planet.
 */

void generate_snes_landscape (void)
{
	int x,y;
	int colour;
	
	for (y = 0; y <= LAND_Y_MAX; y++)
	{
		colour = snes_planet_colour[y * (sizeof(snes_planet_colour)/sizeof(int)) / LAND_Y_MAX];  
		for (x = 0; x <= LAND_X_MAX; x++)
		{
			landscape[x][y] = colour;		
		}
	}	
}




/*
 * Guassian random number generator.
 * Returns a number between -7 and +8 with Gaussian distribution.
 */

int grand (void)
{
	int i;
	int r;
	
	r = 0;
	for (i = 0; i < 12; i++)
		r += randint() & 15;
	
	r /= 12;
	r -= 7;

	return r;
}


/*
 * Calculate the midpoint between two given points.
 */

int calc_midpoint (int sx, int sy, int ex, int ey)
{
	int a,b,n;

	a = landscape[sx][sy];
	b = landscape[ex][ey];
	
	n = ((a + b) / 2) + grand();
	if (n < 0)
		n = 0;
	if (n > 255)
		n = 255;
	
	return n;
} 


/*
 * Calculate a square on the midpoint map.
 */

void midpoint_square (int tx, int ty, int w)
{
	int mx,my;
	int bx,by;
	int d;

	d = w / 2;	
	mx = tx + d;
	my = ty + d;
	bx = tx + w;
	by = ty + w;
	
	landscape[mx][ty] = calc_midpoint(tx,ty,bx,ty);
	landscape[mx][by] = calc_midpoint(tx,by,bx,by);
	landscape[tx][my] = calc_midpoint(tx,ty,tx,by);
	landscape[bx][my] = calc_midpoint(bx,ty,bx,by);
	landscape[mx][my] = calc_midpoint(tx,my,bx,my); 

	if (d == 1)
		return;
	
	midpoint_square (tx,ty,d);
	midpoint_square (mx,ty,d);
	midpoint_square (tx,my,d);
	midpoint_square (mx,my,d);
}


/*
 * Generate a fractal landscape.
 * Uses midpoint displacement method.
 */

void generate_fractal_landscape (int rnd_seed)
{
	int x,y,d,h;
	double dist;
	int dark;
	int old_seed;
	
	old_seed = get_rand_seed();
	set_rand_seed(rnd_seed);
	
	d = LAND_X_MAX / 8;
	
	for (y = 0; y <= LAND_Y_MAX; y += d)
		for (x = 0; x <= LAND_X_MAX; x += d)
			landscape[x][y] = randint() & 255;

	for (y = 0; y < LAND_Y_MAX; y += d)
		for (x = 0; x < LAND_X_MAX; x += d)	
			midpoint_square (x,y,d);

	for (y = 0; y <= LAND_Y_MAX; y++)
	{
		for (x = 0; x <= LAND_X_MAX; x++)
		{
			dist = x*x + y*y;
			dark = dist > 10000;
			h = landscape[x][y];
			if (h > 166)
				landscape[x][y] = dark ? GFX_COL_GREEN_1 : GFX_COL_GREEN_2;
			else 
				landscape[x][y] = dark ? GFX_COL_BLUE_2 : GFX_COL_BLUE_1;

		}
	}

	set_rand_seed (old_seed);
}


void generate_landscape (int rnd_seed)
{
	switch (planet_render_style)
	{
		case 0:		/* Wireframe... do nothing for now... */
			break;
		
		case 1:
			/* generate_green_landscape (); */
			break;
		
		case 2:
			generate_snes_landscape();
			break;
		
		case 3:
			generate_fractal_landscape (rnd_seed);
			break;
	}
}

 
 
/*
 * Draw a line of the planet with appropriate rotation.
 */


void render_planet_line (int xo, int yo, int x, int y, int radius, int vx, int vy)
{
	int lx, ly;
	int rx, ry;
	int colour;
	int sx,sy;
	int ex;
	int div;

	/* Clip against the live view, not the fixed 512x384 rectangle, so a planet
	 * that fills a large window is not cut off at the old right/bottom edge. */
	const Neuron::Client::ViewMetrics& vm = gfx_view_metrics();
	const int v_right  = vm.width  - 1;
	const int v_bottom = vm.height - 1;

	sy = y + yo;

	if ((sy < 0) || (sy > v_bottom))
		return;

	sx = xo - x;
	ex = xo + x;

	rx = -x * vx - y * vy;
	ry = -x * vy + y * vx;
	rx += radius << 16;
	ry += radius << 16;
	div = radius << 10;	 /* radius * 2 * LAND_X_MAX >> 16 */


	for (; sx <= ex; sx++)
	{
		if ((sx >= 0) && (sx <= v_right))
		{
			lx = rx / div;
			ly = ry / div;
			colour = landscape[lx][ly];
 
			ActiveRenderQueue().FastPixel (sx, sy, colour);
		}
		rx += vx;
		ry += vy;
	}
}


/*
 * Draw a solid planet.  Based on Doros circle drawing alogorithm.
 */

void render_planet (int xo, int yo, int radius, struct vector *vec)
{
	int x,y;
	int s;
	int vx,vy;

	xo += GFX_X_OFFSET;
	yo += GFX_Y_OFFSET;
	
	vx = vec[1].x * 65536;
	vy = vec[1].y * 65536;	
	
	s = radius;
	x = radius;
	y = 0;

	s -= x + x;
	while (y <= x)
	{
		render_planet_line (xo, yo, x, y, radius, vx, vy);
		render_planet_line (xo, yo, x,-y, radius, vx, vy);
		render_planet_line (xo, yo, y, x, radius, vx, vy);
		render_planet_line (xo, yo, y,-x, radius, vx, vy);
		
		s += y + y + 1;
		y++;
		if (s >= 0)
		{
			s -= x + x + 2;
			x--;
		}				
	}
}


/*
 * Draw a wireframe planet.
 * At the moment we just draw a circle.
 * Need to add in the two arcs that the original had.
 */

void draw_wireframe_planet (int xo, int yo, int radius, struct vector *vec)
{
	ActiveRenderQueue().Circle (xo, yo, radius, GFX_COL_WHITE);
}


/*
 * Draw a planet.
 * We can currently do three different types of planet...
 * - Wireframe.
 * - Fractal landscape.
 * - SNES-style.
 */

void draw_planet (struct local_object *planet)
{
	if (planet->location.z <= 0)
		return;

	/* Emit the planet as a GPU billboard (a depth-tested camera-facing disk), so it
	 * occludes correctly against the 3D ships and no longer floods the framebuffer with
	 * per-pixel software rasterization. Scene3D derives the on-screen radius from the
	 * distance + focal length and reproduces the style:
	 *   0 wireframe -> ring, 1 green -> filled disk, 2/3 SNES/fractal -> banded disk. */
	Neuron::Render::ModelDraw md;
	md.type = SHIP_PLANET;
	md.style = planet_render_style;
	md.location[0] = planet->location.x;
	md.location[1] = planet->location.y;
	md.location[2] = planet->location.z;
	md.distance = planet->distance;

	switch (planet_render_style)
	{
		case 0:  md.colour = GFX_COL_WHITE;   break;                              /* wireframe ring */
		case 1:  md.colour = GFX_COL_GREEN_1; break;                              /* filled green   */
		default: md.colour = GFX_COL_GREEN_1; md.colour2 = GFX_COL_BLUE_1; break; /* SNES / fractal */
	}

	ActiveRenderQueue().DrawModel (md);
}


void render_sun_line (int xo, int yo, int x, int y, int radius)
{
	int sy = yo + y;
	int sx,ex;
	int colour;
	int dx,dy;
	int distance;
	int inner,outer;
	int inner2;
	int mix;

	const Neuron::Client::ViewMetrics& vm = gfx_view_metrics();
	const int v_right  = vm.width  - 1;
	const int v_bottom = vm.height - 1;

	if ((sy < 0) || (sy > v_bottom))
		return;

	sx = xo - x;
	ex = xo + x;

	sx -= (radius * (2 + (randint() & 7))) >> 8;
	ex += (radius * (2 + (randint() & 7))) >> 8;

	if ((sx > v_right) || (ex < 0))
		return;

	if (sx < 0)
		sx = 0;

	if (ex > v_right)
		ex = v_right;

	inner = (radius * (200 + (randint() & 7))) >> 8;
	inner *= inner;
	
	inner2 = (radius * (220 + (randint() & 7))) >> 8;
	inner2 *= inner2;
	
	outer = (radius * (239 + (randint() & 7))) >> 8;
	outer *= outer;	

	dy = y * y;
	dx = sx - xo;
	
	for (; sx <= ex; sx++,dx++)
	{
		mix = (sx ^ y) & 1;
		distance = dx * dx + dy;

		if (distance < inner)
			colour = GFX_COL_WHITE;
		else if (distance < inner2)
			colour = GFX_COL_YELLOW_4;
		else if (distance < outer)
			colour = GFX_ORANGE_3;
		else
			colour = mix ? GFX_ORANGE_1 : GFX_ORANGE_2;
		
		ActiveRenderQueue().FastPixel (sx, sy, colour);
	} 	
}


void render_sun (int xo, int yo, int radius)
{
	int x,y;
	int s;
	
	xo += GFX_X_OFFSET;
	yo += GFX_Y_OFFSET;
	
	s = -radius;
	x = radius;
	y = 0;

	// s -= x + x;
	while (y <= x)
	{
		render_sun_line (xo, yo, x, y, radius);
		render_sun_line (xo, yo, x,-y, radius);
		render_sun_line (xo, yo, y, x, radius);
		render_sun_line (xo, yo, y,-x, radius);
		
		s += y + y + 1;
		y++;
		if (s >= 0)
		{
			s -= x + x + 2;
			x--;
		}				
	}
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

	ActiveRenderQueue().DrawModel (md);
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
					ActiveRenderQueue().Pixel (px+psx, py+psy, GFX_COL_WHITE);
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

	if ((current_screen != SCR_FRONT_VIEW) && (current_screen != SCR_REAR_VIEW) && 
		(current_screen != SCR_LEFT_VIEW) && (current_screen != SCR_RIGHT_VIEW) &&
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

	if (wireframe)
		draw_wireframe_ship (ship);
	else
		draw_solid_ship (ship);
}

