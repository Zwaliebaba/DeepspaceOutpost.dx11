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
  float mag;     // intrinsic brightness 0..1, power-law skewed toward faint (few brilliant)
  float r, g, b; // spectral tint (blue-white .. white .. yellow .. red)
};

star stars[20];

// (Re)seed one star: scattered position, fresh depth, a power-law magnitude (u^2 -> most
// stars faint, a few brilliant, like a real magnitude distribution) and a spectral tint.
static inline void star_spawn(star& s, double hx, double hy)
{
  s.x = star_rand_span(hx);
  s.y = star_rand_span(hy);
  s.z = rand255() | 0x90;
  const double u = rand01();
  s.mag = 0.30f + 0.70f * static_cast<float>(u * u);
  star_pick_color(s);
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

// Emit one star as a textured, additively-blended sprite quad. sizePx sizes the Starburst
// sprite (bright stars a little larger so their glow/spikes read); r,g,b is the spectral
// tint and intensity the magnitude x distance falloff the pixel shader multiplies in. The
// six vertices carry sprite uvs (0..1) so the pixel shader can sample the soft profile.
static void push_dust(int sx, int sy, float sizePx, float r, float g, float b, float intensity)
{
  int w, h;
  gfx_scene_size(&w, &h);
  if (w <= 0 || h <= 0)
    return;

  const float hx = sizePx / static_cast<float>(w);
  const float hy = sizePx / static_cast<float>(h);
  const float cx = 2.0f * static_cast<float>(sx) / static_cast<float>(w) - 1.0f;
  const float cy = 1.0f - 2.0f * static_cast<float>(sy) / static_cast<float>(h);

  using DV = Graphics::Scene3D::DustVertex;
  // pos.xy, uv, rgb, intensity. Two triangles; uv spans the sprite across the quad.
  const DV quad[6] = {
    {cx - hx, cy - hy, 0.0f, 1.0f, r, g, b, intensity}, {cx + hx, cy - hy, 1.0f, 1.0f, r, g, b, intensity},
    {cx + hx, cy + hy, 1.0f, 0.0f, r, g, b, intensity}, {cx - hx, cy - hy, 0.0f, 1.0f, r, g, b, intensity},
    {cx + hx, cy + hy, 1.0f, 0.0f, r, g, b, intensity}, {cx - hx, cy + hy, 0.0f, 0.0f, r, g, b, intensity},
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

    /* Each on-screen star becomes a small textured "dust" sprite drawn as the background in
       the scene pass - the streaming-speed cue. During a warp the field just streams faster
       (delta above); the old 2D line streaks were a first-person effect and are gone.
       Brightness ties to depth: nearer stars (smaller z) glow brighter and a touch larger,
       far ones fade toward invisible, so the field reads as a volume rather than a plane. */
    if (star_on_screen(sx, sy))
    {
      float distF = 1.2f - static_cast<float>(zz / 320.0);
      distF = (distF < 0.0f) ? 0.0f : (distF > 1.0f ? 1.0f : distF);
      const float intensity = stars[i].mag * distF;
      const float sizePx = 1.0f + 2.5f * intensity; /* continuous: bright = larger soft disc */
      push_dust(sx, sy, sizePx, stars[i].r, stars[i].g, stars[i].b, intensity);
    }

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
