#include "pch.h"
#include "elite.h"
#include "GameScene.h"
#include "Scene3D.h"
#include "Camera.h"
#include "stars.h"
#include "random.h"

int warp_stars;

/* The camera's motion this frame, fed by the camera rig (set_starfield_motion):
 * forward speed along the look (legacy-speed scaled) and the frame's look deltas
 * as screen-space pan. The dust used to stream with the SHIP's speed/roll/climb;
 * with the camera decoupled it streams with the CAMERA instead. */
static double s_cueSpeed = 0.0;
static double s_cuePanX = 0.0;
static double s_cuePanY = 0.0;

void set_starfield_motion(double legacy_speed, double pan_x, double pan_y)
{
  s_cueSpeed = legacy_speed;
  s_cuePanX = pan_x;
  s_cuePanY = pan_y;
}

struct star
{
  double x;
  double y;
  double z;
};

star stars[64];

// How many stars to plot (also the count regenerated on a fresh field). Denser than
// the legacy 12 so the field fills a modern wide window rather than dotting it.
static inline int star_count(void) { return witchspace ? 12 : 48; }

// Star-space half-extents that cover the CURRENT window through star_to_screen (which
// maps star-space s -> s*scale + centre, scale = focal/256). Generating and respawning
// stars across these bounds keeps the field filling the whole window at any aspect and
// any zoom, instead of a fixed central box the old ±120 range left bare at the edges.
static inline void star_field_bounds(double* hx, double* hy)
{
  int w, h;
  gfx_scene_size(&w, &h);
  const double focal = Client::CameraFocalPixels(Client::MainCamera(), static_cast<float>(h > 0 ? h : 1));
  const double scale = (focal > 1e-6) ? focal / 256.0 : 2.0;
  *hx = (w > 0 ? w : 512) * 0.5 / scale;
  *hy = (h > 0 ? h : 384) * 0.5 / scale;
}

// Uniform star-space coordinate in [-half, half], never dead-centre (a star at 0 does
// not stream outward, so it would sit frozen in the middle).
static inline double star_rand_span(double half)
{
  double v = (static_cast<double>(rand255()) / 255.0 * 2.0 - 1.0) * half;
  if (v > -1.0 && v < 1.0)
    v = (v < 0.0) ? -1.0 : 1.0;
  return v;
}

/*
 * Map a star-space coordinate (roughly [-128,128] x [-96,96]) to screen pixels
 * using the main Camera's optics, so the starfield fills the whole window in
 * full-window flight. At the retro 4:3 viewport this is the old
 * "(s + centre) * GFX_SCALE" mapping (focal 512 -> scale 2, centre 256,192).
 */
static inline void star_to_screen(double sx_in, double sy_in, int* sx, int* sy)
{
  int w, h;
  gfx_scene_size(&w, &h);
  const double focal = Client::CameraFocalPixels(Client::MainCamera(), static_cast<float>(h));
  const double scale = focal / 256.0;
  *sx = static_cast<int>(sx_in * scale + w * 0.5);
  *sy = static_cast<int>(sy_in * scale + h * 0.5);
}

static inline int star_on_screen(int sx, int sy)
{
  int w, h;
  gfx_scene_size(&w, &h);
  return (sx >= 1) && (sx <= w - 1) && (sy >= 1) && (sy <= h - 1);
}

/* The starfield as scene-pass "dust": each drawn star is collected as a small clip-space
 * quad, which Scene3D draws as the scene background (the streaming-speed cue). This replaced
 * the legacy 2D white-pixel starfield. */
static std::vector<Graphics::Scene3D::DustVertex> s_dustQuads;

static void push_dust(int sx, int sy, double zz)
{
  int w, h;
  gfx_scene_size(&w, &h);
  if (w <= 0 || h <= 0)
    return;

  /* A touch bigger for nearer stars (smaller z), echoing the legacy 1-4px dots. Sizes
   * are in pixels; tune to taste. */
  const float sizePx = (zz < 0x90) ? 2.4f : (zz < 0xC0 ? 1.8f : 1.2f);
  const float hx = sizePx / static_cast<float>(w);
  const float hy = sizePx / static_cast<float>(h);
  const float cx = 2.0f * static_cast<float>(sx) / static_cast<float>(w) - 1.0f;
  const float cy = 1.0f - 2.0f * static_cast<float>(sy) / static_cast<float>(h);
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
  double hx, hy;
  star_field_bounds(&hx, &hy);

  const int nstars = star_count();
  for (int i = 0; i < nstars; i++)
  {
    stars[i].x = star_rand_span(hx);
    stars[i].y = star_rand_span(hy);
    stars[i].z = rand255() | 0x90;
  }

  warp_stars = 0;
}

void front_starfield(void)
{
  int sx;
  int sy;

  const int nstars = star_count();

  /* Star-space bounds that cover the window this frame (aspect/zoom aware). */
  double hx, hy;
  star_field_bounds(&hx, &hy);

  /* The streaming/panning inputs come from the CAMERA's motion (set by the rig
   * each frame): delta streams the stars toward/away from the eye as the camera
   * dollies; the pan terms slide the whole field opposite to a look turn, the
   * successor to the old ship roll/climb drift. Warp jumps still force streaks. */
  double delta = warp_stars ? 50 : s_cueSpeed;

  delta /= 2.0;

  for (int i = 0; i < nstars; i++)
  {
    /* Plot the stars in their current locations... */

    double zz = stars[i].z;
    star_to_screen(stars[i].x, stars[i].y, &sx, &sy);

    /* Each on-screen star becomes a small 3D "dust" quad drawn as the background in the
       scene pass - the streaming-speed cue. During a warp the field just streams faster
       (delta above); the old 2D line streaks were a first-person effect and are gone. */
    if (star_on_screen(sx, sy))
      push_dust(sx, sy, zz);

    /* Move the stars to their new locations...*/

    double Q = delta / stars[i].z;

    stars[i].z -= delta;
    double yy = stars[i].y + (stars[i].y * Q);
    double xx = stars[i].x + (stars[i].x * Q);
    zz = stars[i].z;

    xx = xx + s_cuePanX;
    yy = yy + s_cuePanY;

    stars[i].y = yy;
    stars[i].x = xx;

    /* Respawn a star once it leaves the screen-covering bounds (streamed past the
       edge on forward motion), OR once it converges toward the eye - zooming OUT
       drives z up and x,y toward the centre, so without a far-z cutoff the field
       would collapse into a small central cluster. Fresh stars are scattered across
       the full window so it stays filled at any zoom. */
    if ((xx > hx) || (xx < -hx) || (yy > hy) || (yy < -hy) || (zz < 16.0) || (zz > 320.0))
    {
      stars[i].x = star_rand_span(hx);
      stars[i].y = star_rand_span(hy);
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
