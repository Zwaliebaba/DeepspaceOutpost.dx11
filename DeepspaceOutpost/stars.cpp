#include "pch.h"

#include <stdlib.h>
#include <math.h>
#include <vector>

#include "config.h"
#include "elite.h"
#include "gfx.h"
#include "GameUniverse.h"
#include "RenderContext.h"
#include "Scene3D.h" // Neuron::Graphics::Scene3D::SetDust - starfield as scene-pass dust
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

/* The starfield as scene-pass "dust": when the skybox is enabled, each drawn star is
 * collected as a small clip-space quad instead of the legacy 2D pixels. Scene3D draws
 * these over the skybox. With the skybox off, this stays empty and the 2D starfield shows. */
static std::vector<Neuron::Graphics::Scene3D::DustVertex> s_dustQuads;

static void push_dust (int sx, int sy, double zz)
{
	const Neuron::Client::ViewMetrics& vm = gfx_view_metrics();
	if (vm.width <= 0 || vm.height <= 0)
		return;

	/* A touch bigger for nearer stars (smaller z), echoing the legacy 1-4px dots. Sizes
	 * are in pixels; tune to taste with the skybox. */
	const float sizePx = (zz < 0x90) ? 2.4f : (zz < 0xC0 ? 1.8f : 1.2f);
	const float hx = sizePx / static_cast<float>(vm.width);
	const float hy = sizePx / static_cast<float>(vm.height);
	const float cx = 2.0f * static_cast<float>(sx) / static_cast<float>(vm.width) - 1.0f;
	const float cy = 1.0f - 2.0f * static_cast<float>(sy) / static_cast<float>(vm.height);
	const float b = 1.0f;

	using DV = Neuron::Graphics::Scene3D::DustVertex;
	const DV quad[6] = {
		{cx - hx, cy - hy, b}, {cx + hx, cy - hy, b}, {cx + hx, cy + hy, b},
		{cx - hx, cy - hy, b}, {cx + hx, cy + hy, b}, {cx - hx, cy + hy, b},
	};
	for (const DV& v : quad)
		s_dustQuads.push_back (v);
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
			/* With the skybox on, the streaming stars are 3D dust in the scene pass;
			   the legacy 2D pixels would then double-draw them, so emit one or the
			   other, never both. */
			if (Neuron::Graphics::Scene3D::IsSkyboxEnabled())
			{
				push_dust (sx, sy, zz);
			}
			else
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
			/* With the skybox on, the streaming stars are 3D dust in the scene pass;
			   the legacy 2D pixels would then double-draw them, so emit one or the
			   other, never both. */
			if (Neuron::Graphics::Scene3D::IsSkyboxEnabled())
			{
				push_dust (sx, sy, zz);
			}
			else
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
			/* With the skybox on, the streaming stars are 3D dust in the scene pass;
			   the legacy 2D pixels would then double-draw them, so emit one or the
			   other, never both. */
			if (Neuron::Graphics::Scene3D::IsSkyboxEnabled())
			{
				push_dust (sx, sy, zz);
			}
			else
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
	s_dustQuads.clear();

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

	/* Hand this frame's stars to the scene pass as dust. Scene3D draws them over the
	   skybox only when the skybox is enabled; otherwise this is inert (the 2D starfield
	   above still shows), so the default look is unchanged. */
	Neuron::Graphics::Scene3D::SetDust (s_dustQuads.data(), static_cast<int>(s_dustQuads.size()));
}
