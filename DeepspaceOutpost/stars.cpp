#include "pch.h"
#include "elite.h"
#include "GameScene.h"
#include "Scene3D.h"
#include "Camera.h"
#include "stars.h"
#include "random.h"

#include <cmath>

int warp_stars;

// Frame tick, advanced once per update_starfield. Drives the near layer's gentle twinkle;
// a bare counter is enough since the effect is a slow, framerate-tolerant shimmer.
static unsigned s_frame = 0;

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

// A uniform [0,1) draw from the shared 8-bit RNG.
static inline double rand01(void) { return static_cast<double>(rand255()) / 255.0; }

// Pick a spectral colour weighted toward white, with occasional cool/warm accents so a field
// reads like a real sky rather than a wall of identical white dots. Low saturation - tasteful
// tint, not a disco. Shared by the near layer and the distant backdrop.
static inline void pick_spectral(float& r, float& g, float& b)
{
  const int t = rand255();
  if (t < 26)       { r = 0.75f; g = 0.83f; b = 1.00f; } // blue-white (hot)
  else if (t < 64)  { r = 0.86f; g = 0.91f; b = 1.00f; } // white-blue
  else if (t < 186) { r = 1.00f; g = 1.00f; b = 1.00f; } // white (majority)
  else if (t < 226) { r = 1.00f; g = 0.95f; b = 0.82f; } // yellow
  else if (t < 246) { r = 1.00f; g = 0.83f; b = 0.63f; } // orange
  else              { r = 1.00f; g = 0.72f; b = 0.60f; } // red (cool)
}

static inline void star_pick_color(star& s) { pick_spectral(s.r, s.g, s.b); }

// Assign a star's intrinsic look: a power-law magnitude (u^2 -> most stars faint, a few
// brilliant, like a real magnitude distribution) and a spectral tint. Call after seeding
// its position/depth so the field isn't a wall of identical white dots.
static inline void star_appearance(star& s)
{
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

// Star-space half-extents that cover the current window through star_to_screen (scale =
// focal/256), so the backdrop fills the whole window at any size/aspect - unlike the near
// layer's fixed +-120 central box.
static inline void star_window_half(double* hx, double* hy)
{
  int w, h;
  gfx_scene_size(&w, &h);
  const double focal = Client::CameraFocalPixels(Client::MainCamera(), static_cast<float>(h > 0 ? h : 1));
  const double scale = (focal > 1e-6) ? focal / 256.0 : 2.0;
  *hx = (w > 0 ? w : 512) * 0.5 / scale;
  *hy = (h > 0 ? h : 384) * 0.5 / scale;
}

// The distant backdrop layer. Where the near stars[] stream past to cue speed, this dense
// field of faint far stars barely moves: it pans with the camera's look but never dollies
// (infinitely far, so no z-streaming). Those two rates apart give the field genuine depth
// (motion parallax) and the vastness of deep space behind the streaming near layer. Built
// once to fill the window and wrapped at the edges so the constellation persists.
struct backstar
{
  double x, y;
  float mag, r, g, b;
};
static std::vector<backstar> s_backdrop;
static double s_backdropHalfX = 0.0; // window half-extent the current backdrop was built for

static inline int backdrop_count(void) { return witchspace ? 70 : 240; }

static void build_backdrop(void)
{
  double hx, hy;
  star_window_half(&hx, &hy);
  hx *= 1.15; // a little past the window so a pan doesn't reveal a bare margin before the wrap
  hy *= 1.15;

  s_backdrop.resize(backdrop_count());
  for (backstar& s : s_backdrop)
  {
    s.x = (rand01() * 2.0 - 1.0) * hx;
    s.y = (rand01() * 2.0 - 1.0) * hy;
    const double u = rand01();
    s.mag = 0.18f + 0.32f * static_cast<float>(u * u); // fainter + tighter than the near layer
    pick_spectral(s.r, s.g, s.b);
  }
  s_backdropHalfX = hx;
}

static void draw_backdrop(void)
{
  double hx, hy;
  star_window_half(&hx, &hy);
  hx *= 1.15;
  hy *= 1.15;

  // Rebuild on a count change (witchspace) or a window resize (else the field would cluster
  // centrally until it slowly panned out).
  const double dHalf = (hx > s_backdropHalfX) ? hx - s_backdropHalfX : s_backdropHalfX - hx;
  if (static_cast<int>(s_backdrop.size()) != backdrop_count() || dHalf > 1.0)
    build_backdrop(); // rebuilds against this same window; hx/hy above stay valid

  int sx, sy;
  for (backstar& s : s_backdrop)
  {
    // Pan fully with the camera's look (distant stars slide with a turn just like near ones),
    // but never stream in z - that missing dolly parallax is exactly what reads as "far".
    s.x += s_cuePanX;
    s.y += s_cuePanY;
    if (s.x > hx) s.x -= 2.0 * hx; else if (s.x < -hx) s.x += 2.0 * hx;
    if (s.y > hy) s.y -= 2.0 * hy; else if (s.y < -hy) s.y += 2.0 * hy;

    star_to_screen(s.x, s.y, &sx, &sy);
    if (star_on_screen(sx, sy))
    {
      // Keep a >=5px floor: below that the 128px glow sprite minifies to near-transparent
      // (see push_dust). Faint + small, but never averaged out of existence.
      const float sizePx = 5.0f + 4.0f * s.mag; // small, soft points (near layer is 7..23px)
      push_dust(sx, sy, sizePx, s.r, s.g, s.b, s.mag);
    }
  }
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
    star_appearance(stars[i]);
  }

  s_backdrop.clear(); // fresh field (e.g. a jump) -> rebuild the distant layer next frame

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

  /* The distant backdrop first: a dense, near-static deep field behind the streaming near
   * stars. Additive blending makes draw order irrelevant, but drawing it first matches its
   * role as the background the near layer parallaxes across. */
  draw_backdrop();

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
      /* The Starburst sprite is a 128px soft glow (with faint diffraction spikes) whose energy
         sits in the centre. Drawn at a 1-2px point it minifies to near-transparent (the top
         mips average the glow into the vast transparent surround), so a star needs real screen
         size to read: a small soft point for faint stars, a wider halo for bright ones. The
         cubic term only kicks in near the top of the range, so the few brightest stars get big
         enough for the sprite's spikes to show - the lens-sparkle on the standout stars. */
      const float sizePx = 5.0f + 18.0f * intensity + 8.0f * intensity * intensity * intensity;
      /* Subtle twinkle: brighter stars scintillate a touch (amplitude scales with brightness,
         so the faint far field stays steady). Space has no atmosphere - keep it gentle, a hint
         of life rather than christmas lights. Per-star golden-angle phase decorrelates them. */
      const double ph = i * 2.3999632 + s_frame * 0.12;
      const float twinkle = 1.0f + 0.12f * intensity * static_cast<float>(std::sin(ph));
      push_dust(sx, sy, sizePx, stars[i].r, stars[i].g, stars[i].b, intensity * twinkle);
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
      star_appearance(stars[i]);
    }
  }

  warp_stars = 0;
}

void update_starfield(void)
{
  s_dustQuads.clear();
  ++s_frame;

  switch (current_screen)
  {
  case SCR_FRONT_VIEW:
  case SCR_INTRO_ONE:
  case SCR_INTRO_TWO:
  case SCR_ESCAPE_POD:
    front_starfield();
    break;
  }

  /* Hand this frame's stars to the scene pass as dust; Scene3D draws them as the scene
     background - the streaming-speed cue. (The warp-jump streaks above are drawn straight
     through gfx_draw_line now - no render queue.) */
  Graphics::Scene3D::SetDust(s_dustQuads.data(), static_cast<int>(s_dustQuads.size()));
}
