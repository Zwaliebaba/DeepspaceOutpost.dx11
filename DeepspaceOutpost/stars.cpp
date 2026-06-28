#include "pch.h"

#include <stdlib.h>
#include <math.h>

#include "config.h"
#include "elite.h"
#include "gfx.h"
#include "GameUniverse.h"
#include "RenderContext.h"
#include "vector.h"
#include "stars.h"
#include "random.h"

int warp_stars;

struct star
{
	double x;
	double y;
	double z;
};

struct star stars[20];


/*
 * Map a star-space coordinate (roughly [-128,128] x [-96,96]) to screen pixels
 * using the current frame's optics, so the starfield fills the whole window in
 * full-window flight. At the retro 4:3 viewport this is the old
 * "(s + centre) * GFX_SCALE" mapping (focal 512 -> scale 2, centre 256,192).
 */
static inline void star_to_screen (double sx_in, double sy_in, int *sx, int *sy)
{
	const Neuron::Client::ViewMetrics& vm = gfx_view_metrics();
	const double scale = vm.focal / 256.0;
	*sx = (int) (sx_in * scale + vm.cx);
	*sy = (int) (sy_in * scale + vm.cy);
}

static inline int star_on_screen (int sx, int sy)
{
	const Neuron::Client::ViewMetrics& vm = gfx_view_metrics();
	return (sx >= 1) && (sx <= vm.width - 1) && (sy >= 1) && (sy <= vm.height - 1);
}


void create_new_stars (void)
{
	int i;
	int nstars;
	
	nstars = witchspace ? 3 : 12;

	for (i = 0; i < nstars; i++)
	{
		stars[i].x = (rand255() - 128) | 8;
		stars[i].y = (rand255() - 128) | 4;
		stars[i].z = rand255() | 0x90;
	}

	warp_stars = 0;
}


void front_starfield (void)
{
	int i;
	double Q;
	double delta;
	double alpha = 0;
	double beta = 0;
	double xx,yy,zz;
	int sx;
	int sy;
	int nstars;
	
	nstars = witchspace ? 3 : 12;

	delta = warp_stars ? 50 : PlayerFlight().speed;	
	alpha = (double)PlayerFlight().roll;
	beta = (double)PlayerFlight().climb;

	alpha /= 256.0;
	delta /= 2.0;
	
	for (i = 0; i < nstars; i++)
	{
		/* Plot the stars in their current locations... */

		zz = stars[i].z;
		star_to_screen (stars[i].x, stars[i].y, &sx, &sy);

		if ((!warp_stars) && star_on_screen (sx, sy))
		{
			ActiveRenderQueue().Pixel (sx, sy, GFX_COL_WHITE);

			if (zz < 0xC0)
				ActiveRenderQueue().Pixel (sx+1, sy, GFX_COL_WHITE);

			if (zz < 0x90)
			{
				ActiveRenderQueue().Pixel (sx, sy+1, GFX_COL_WHITE);
				ActiveRenderQueue().Pixel (sx+1, sy+1, GFX_COL_WHITE);
			}
		}


		/* Move the stars to their new locations...*/

		Q = delta / stars[i].z;

		stars[i].z -= delta;
		yy = stars[i].y + (stars[i].y * Q);
		xx = stars[i].x + (stars[i].x * Q);
		zz = stars[i].z;

		yy = yy + (xx * alpha);
		xx = xx - (yy * alpha);

/*
		tx = yy * beta;
		xx = xx + (tx * tx * 2);
*/
		yy = yy + beta;

		stars[i].y = yy;
		stars[i].x = xx;


		if (warp_stars)
		{
			int ex, ey;
			star_to_screen (xx, yy, &ex, &ey);
			ActiveRenderQueue().Line (sx, sy, ex, ey);
		}

		sx = xx;
		sy = yy;

		if ((sx > 120) || (sx < -120) ||
			(sy > 120) || (sy < -120) || (zz < 16))
		{
			stars[i].x = (rand255() - 128) | 8;
			stars[i].y = (rand255() - 128) | 4;
			stars[i].z = rand255() | 0x90;
			continue;
		}

	}

	warp_stars = 0;
}



void rear_starfield (void)
{
	int i;
	double Q;
	double delta;
	double alpha = 0;
	double beta = 0;
	double xx,yy,zz;
	int sx,sy;
	int ex,ey;
	int nstars;
	
	nstars = witchspace ? 3 : 12;

	delta = warp_stars ? 50 : PlayerFlight().speed;	
	alpha = -PlayerFlight().roll;
	beta = -PlayerFlight().climb;

	alpha /= 256.0;
	delta /= 2.0;
	
	for (i = 0; i < nstars; i++)
	{
		/* Plot the stars in their current locations... */

		zz = stars[i].z;
		star_to_screen (stars[i].x, stars[i].y, &sx, &sy);

		if ((!warp_stars) && star_on_screen (sx, sy))
		{
			ActiveRenderQueue().Pixel (sx, sy, GFX_COL_WHITE);

			if (zz < 0xC0)
				ActiveRenderQueue().Pixel (sx+1, sy, GFX_COL_WHITE);

			if (zz < 0x90)
			{
				ActiveRenderQueue().Pixel (sx, sy+1, GFX_COL_WHITE);
				ActiveRenderQueue().Pixel (sx+1, sy+1, GFX_COL_WHITE);
			}
		}


		/* Move the stars to their new locations...*/

		Q = delta / stars[i].z;

		stars[i].z += delta;
		yy = stars[i].y - (stars[i].y * Q);
		xx = stars[i].x - (stars[i].x * Q);
		zz = stars[i].z;

		yy = yy + (xx * alpha);
		xx = xx - (yy * alpha);

/*
		tx = yy * beta;
		xx = xx + (tx * tx * 2);
*/
		yy = yy + beta;

		if (warp_stars)
		{
			star_to_screen (xx, yy, &ex, &ey);

			if (star_on_screen (sx, sy) && star_on_screen (ex, ey))
				ActiveRenderQueue().Line (sx, sy, ex, ey);
		}

		stars[i].y = yy;
		stars[i].x = xx;

		if ((zz >= 300) || (abs((int)yy) >= 110))
		{
			stars[i].z = (rand255() & 127) + 51;
			
			if (rand255() & 1)
			{
				stars[i].x = rand255() - 128;
				stars[i].y = (rand255() & 1) ? -115 : 115;
			}
			else
			{
				stars[i].x = (rand255() & 1) ? -126 : 126;
				stars[i].y = rand255() - 128; 
			}
		}

	}

	warp_stars = 0;
}


void side_starfield (void)
{
	int i;
	double delta;
	double alpha;
	double beta;
	double xx,yy,zz;
	int sx;
	int sy;
	double delt8;
	int nstars;
	
	nstars = witchspace ? 3 : 12;
	
	delta = warp_stars ? 50 : PlayerFlight().speed;	
	alpha = PlayerFlight().roll;
	beta = PlayerFlight().climb;

	if (current_screen == SCR_LEFT_VIEW)
	{
		delta = -delta;
		alpha = -alpha;
		beta = -beta;
	} 
	
	for (i = 0; i < nstars; i++)
	{
		zz = stars[i].z;
		star_to_screen (stars[i].x, stars[i].y, &sx, &sy);

		if ((!warp_stars) && star_on_screen (sx, sy))
		{
			ActiveRenderQueue().Pixel (sx, sy, GFX_COL_WHITE);

			if (zz < 0xC0)
				ActiveRenderQueue().Pixel (sx+1, sy, GFX_COL_WHITE);

			if (zz < 0x90)
			{
				ActiveRenderQueue().Pixel (sx, sy+1, GFX_COL_WHITE);
				ActiveRenderQueue().Pixel (sx+1, sy+1, GFX_COL_WHITE);
			}
		}

		yy = stars[i].y;
		xx = stars[i].x;
		zz = stars[i].z;

		delt8 = delta / (zz / 32);
		xx = xx + delt8;

		xx += (yy * (beta / 256));
		yy -= (xx * (beta / 256));

		xx += ((yy / 256) * (alpha / 256)) * (-xx);
		yy += ((yy / 256) * (alpha / 256)) * (yy);

		yy += alpha;

		stars[i].y = yy;
		stars[i].x = xx;

		if (warp_stars)
		{
			int ex, ey;
			star_to_screen (xx, yy, &ex, &ey);
			ActiveRenderQueue().Line (sx, sy, ex, ey);
		}

		
		if (abs((int)stars[i].x) >= 116)
		{
			stars[i].y = rand255() - 128;
			stars[i].x = (current_screen == SCR_LEFT_VIEW) ? 115 : -115;
			stars[i].z = rand255() | 8;
		}
		else if (abs((int)stars[i].y) >= 116)
		{
			stars[i].x = rand255() - 128;
			stars[i].y = (alpha > 0) ? -110 : 110;
			stars[i].z = rand255() | 8;
		} 
		
	}

	warp_stars = 0;
}


/*
 * When we change view, flip the stars over so they look like other stars.
 */

void flip_stars (void)
{
	int i;
	int nstars;
	int sx;
	int sy;
	
	nstars = witchspace ? 3 : 12;
	for (i = 0; i < nstars; i++)
	{
		sy = stars[i].y;
		sx = stars[i].x;
		stars[i].x = sy;
		stars[i].y = sx;
	}
}


void update_starfield (void)
{
	switch (current_screen)
	{
		case SCR_FRONT_VIEW:
		case SCR_INTRO_ONE:
		case SCR_INTRO_TWO:
		case SCR_ESCAPE_POD:
			front_starfield();
			break;
		
		case SCR_REAR_VIEW:
		case SCR_GAME_OVER:
			rear_starfield();
			break;
		
		case SCR_LEFT_VIEW:
		case SCR_RIGHT_VIEW:
			side_starfield();
			break;
	}

	/* Replay the recorded starfield into the gfx backend at this same point,
	   so the on-screen result is identical to the old direct gfx_ calls. */
	FlushRenderQueue();
}
