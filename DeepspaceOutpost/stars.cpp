#include "pch.h"
#include "elite.h"
#include "gfx.h"
#include "GameUniverse.h"
#include "Scene3D.h"
#include "stars.h"
#include "random.h"

int warp_stars;

struct star
{
  double x;
  double y;
  double z;
};

star stars[20];

/*
 * Map a star-space coordinate (roughly [-128,128] x [-96,96]) to screen pixels
 * using the current frame's optics, so the starfield fills the whole window in
 * full-window flight. At the retro 4:3 viewport this is the old
 * "(s + centre) * GFX_SCALE" mapping (focal 512 -> scale 2, centre 256,192).
 */
static inline void star_to_screen(double sx_in, double sy_in, int* sx, int* sy)
{
  const Client::ViewMetrics& vm = gfx_view_metrics();
  const double scale = vm.focal / 256.0;
  *sx = static_cast<int>(sx_in * scale + vm.cx);
  *sy = static_cast<int>(sy_in * scale + vm.cy);
}

static inline int star_on_screen(int sx, int sy)
{
  const Client::ViewMetrics& vm = gfx_view_metrics();
  return (sx >= 1) && (sx <= vm.width - 1) && (sy >= 1) && (sy <= vm.height - 1);
}

/* The starfield as scene-pass "dust": each drawn star is collected as a small clip-space
 * quad, which Scene3D draws as the scene background (the streaming-speed cue). This replaced
 * the legacy 2D white-pixel starfield. */
static std::vector<Graphics::Scene3D::DustVertex> s_dustQuads;

static void push_dust(int sx, int sy, double zz)
{
  const Client::ViewMetrics& vm = gfx_view_metrics();
  if (vm.width <= 0 || vm.height <= 0)
    return;

  /* A touch bigger for nearer stars (smaller z), echoing the legacy 1-4px dots. Sizes
   * are in pixels; tune to taste. */
  const float sizePx = (zz < 0x90) ? 2.4f : (zz < 0xC0 ? 1.8f : 1.2f);
  const float hx = sizePx / static_cast<float>(vm.width);
  const float hy = sizePx / static_cast<float>(vm.height);
  const float cx = 2.0f * static_cast<float>(sx) / static_cast<float>(vm.width) - 1.0f;
  const float cy = 1.0f - 2.0f * static_cast<float>(sy) / static_cast<float>(vm.height);
  constexpr float b = 1.0f;

  using DV = Graphics::Scene3D::DustVertex;
  const DV quad[6] = {
    {cx - hx, cy - hy, b}, {cx + hx, cy - hy, b}, {cx + hx, cy + hy, b}, {cx - hx, cy - hy, b}, {cx + hx, cy + hy, b},
    {cx - hx, cy + hy, b},
  };
  for (const DV& v : quad)
    s_dustQuads.push_back(v);
}

void create_new_stars(void)
{
  int nstars = witchspace ? 3 : 12;

  for (int i = 0; i < nstars; i++)
  {
    stars[i].x = (rand255() - 128) | 8;
    stars[i].y = (rand255() - 128) | 4;
    stars[i].z = rand255() | 0x90;
  }

  warp_stars = 0;
}

void front_starfield(void)
{
  int sx;
  int sy;

  int nstars = witchspace ? 3 : 12;

  double delta = warp_stars ? 50 : PlayerFlight().speed;
  double alpha = static_cast<double>(PlayerFlight().roll);
  double beta = static_cast<double>(PlayerFlight().climb);

  alpha /= 256.0;
  delta /= 2.0;

  for (int i = 0; i < nstars; i++)
  {
    /* Plot the stars in their current locations... */

    double zz = stars[i].z;
    star_to_screen(stars[i].x, stars[i].y, &sx, &sy);

    /* Each on-screen star becomes a small 3D "dust" quad drawn as the background in the
       scene pass - the streaming-speed cue. (Warp streaks are still 2D lines, below.) */
    if ((!warp_stars) && star_on_screen(sx, sy))
      push_dust(sx, sy, zz);

    /* Move the stars to their new locations...*/

    double Q = delta / stars[i].z;

    stars[i].z -= delta;
    double yy = stars[i].y + (stars[i].y * Q);
    double xx = stars[i].x + (stars[i].x * Q);
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
      star_to_screen(xx, yy, &ex, &ey);
      gfx_draw_line(sx, sy, ex, ey);
    }

    sx = xx;
    sy = yy;

    if ((sx > 120) || (sx < -120) || (sy > 120) || (sy < -120) || (zz < 16))
    {
      stars[i].x = (rand255() - 128) | 8;
      stars[i].y = (rand255() - 128) | 4;
      stars[i].z = rand255() | 0x90;
    }
  }

  warp_stars = 0;
}

void update_starfield(void)
{
  s_dustQuads.clear();

  switch (current_screen)
  {
  case SCR_FRONT_VIEW:
  case SCR_INTRO_ONE:
  case SCR_INTRO_TWO:
  case SCR_ESCAPE_POD:
  case SCR_GAME_OVER:
    front_starfield();
    break;
  }

  /* Hand this frame's stars to the scene pass as dust; Scene3D draws them as the scene
     background - the streaming-speed cue. (The warp-jump streaks above are drawn straight
     through gfx_draw_line now - no render queue.) */
  Graphics::Scene3D::SetDust(s_dustQuads.data(), static_cast<int>(s_dustQuads.size()));
}
