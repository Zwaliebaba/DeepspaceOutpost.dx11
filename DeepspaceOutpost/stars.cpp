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
 * quad, which Scene3D draws over the skybox (the streaming-speed cue). This replaced the
 * legacy 2D white-pixel starfield. */
static std::vector<Graphics::Scene3D::DustVertex> s_dustQuads;

static void push_dust(int sx, int sy, double zz)
{
  const Client::ViewMetrics& vm = gfx_view_metrics();
  if (vm.width <= 0 || vm.height <= 0)
    return;

  /* A touch bigger for nearer stars (smaller z), echoing the legacy 1-4px dots. Sizes
   * are in pixels; tune to taste with the skybox. */
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

    /* Each on-screen star becomes a small 3D "dust" quad drawn over the skybox in the
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

/* Skybox camera->world orientation. The cube skybox is sampled by view direction, so it
   needs the camera's orientation in the world. We track the ship's orientation as a 3x3 that
   maps view-space (x right, y up, z forward) to the world the cubemap art is authored in,
   integrating the player's roll (about forward/z) and climb (about right/x) each frame; the
   camera always looks along the ship's nose (the single forward view). Rotation rates are
   tuned for feel - the sky is a distant backdrop, and the cubemap can be re-oriented to
   match. */
static float s_shipRot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // view->world, accumulated

/* Per-frame integration gains (radians per unit of roll/climb). */
static constexpr double kSkyRollGain = 1.0 / 256.0;
static constexpr double kSkyPitchGain = 1.0 / 256.0;

/* C = A * B for row-major 3x3 (element (r,c) = m[r*3+c]). */
static void mat3_mul(const float a[9], const float b[9], float out[9])
{
  float tmp[9];
  for (int r = 0; r < 3; r++)
  {
    for (int c = 0; c < 3; c++)
    {
      tmp[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c] + a[r * 3 + 1] * b[1 * 3 + c] + a[r * 3 + 2] * b[2 * 3 + c];
    }
  }
  for (int i = 0; i < 9; i++)
    out[i] = tmp[i];
}

/* Gram-Schmidt renormalise so repeated multiplies don't drift off SO(3). */
static void mat3_orthonormalise(float m[9])
{
  /* rows as basis vectors */
  float* x = &m[0];
  float* y = &m[3];
  float* z = &m[6];
  auto norm = [](float* v)
  {
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f)
    {
      v[0] /= l;
      v[1] /= l;
      v[2] /= l;
    }
  };
  auto dot = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
  norm(x);
  float dy = dot(y, x);
  y[0] -= dy * x[0];
  y[1] -= dy * x[1];
  y[2] -= dy * x[2];
  norm(y);
  /* z = x cross y */
  z[0] = x[1] * y[2] - x[2] * y[1];
  z[1] = x[2] * y[0] - x[0] * y[2];
  z[2] = x[0] * y[1] - x[1] * y[0];
}

static void accumulate_skybox_orientation(void)
{
  const double roll = static_cast<double>(PlayerFlight().roll) * kSkyRollGain;
  const double climb = static_cast<double>(PlayerFlight().climb) * kSkyPitchGain;

  /* Local-frame increments (compose on the right of the current orientation): roll about
     the view forward axis (z), pitch about the view right axis (x). */
  const float cr = static_cast<float>(cos(roll)), sr = static_cast<float>(sin(roll));
  const float cp = static_cast<float>(cos(climb)), sp = static_cast<float>(sin(climb));
  const float rz[9] = {cr, -sr, 0, sr, cr, 0, 0, 0, 1};
  const float rx[9] = {1, 0, 0, 0, cp, -sp, 0, sp, cp};

  mat3_mul(s_shipRot, rz, s_shipRot);
  mat3_mul(s_shipRot, rx, s_shipRot);
  mat3_orthonormalise(s_shipRot);

  /* The camera always looks forward, so camera->world is the ship orientation. */
  Graphics::Scene3D::SetSkyboxOrientation(s_shipRot);
}

void update_starfield(void)
{
  s_dustQuads.clear();

  accumulate_skybox_orientation();

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

  /* Hand this frame's stars to the scene pass as dust; Scene3D draws them over the
     skybox as the streaming-speed cue. (The warp-jump streaks above are drawn straight
     through gfx_draw_line now - no render queue.) */
  Graphics::Scene3D::SetDust(s_dustQuads.data(), static_cast<int>(s_dustQuads.size()));
}
