// Procedural skybox pixel shader: a vertical gradient plus procedurally-placed stars.
// Cheap and asset-free; the look is driven entirely by the b0 params so Scene3D can tune
// gradient colours and star density/brightness.
//
// Stars are scattered, not gridded: each cell holds at most one candidate, but its position
// is jittered inside the cell and its size/brightness vary per star, so there is no visible
// lattice and no two stars look identical. We scan the 3x3 neighbourhood of the sampled
// cell so a star jittered near a cell edge still draws in full.

#include "partials/skybox.hlsli"

// Cheap 2D hash -> [0,1).
float hash21(float2 _p)
{
    _p = frac(_p * float2(127.1, 311.7));
    _p += dot(_p, _p + 34.56);
    return frac(_p.x * _p.y);
}

float4 PSMain(VSOut _i) : SV_Target
{
    float3 col = lerp(u_BottomColor.rgb, u_TopColor.rgb, saturate(_i.uv.y));

    // Star-sampling coordinate: centre, aspect-correct so the rotation is isotropic, then
    // rotate by the accumulated roll and offset by the pan. The gradient above stays
    // screen-fixed; only the stars track control input.
    const float aspect = max(u_Params.w, 0.0001);
    float2 p = _i.uv - 0.5;
    p.x *= aspect;
    const float2x2 rot = float2x2(u_SkyXform.x, -u_SkyXform.y,
                                  u_SkyXform.y,  u_SkyXform.x);
    p = mul(rot, p) + u_SkyXform.zw;

    const float density = max(u_Params.z, 1.0);
    const float2 grid = p * density;
    const float2 cell = floor(grid);
    const float2 f = grid - cell; // position within the sampled cell, [0,1)

    float star = 0.0;
    [unroll]
    for (int oy = -1; oy <= 1; ++oy)
    {
        [unroll]
        for (int ox = -1; ox <= 1; ++ox)
        {
            const float2 c = cell + float2(ox, oy);
            if (hash21(c) <= u_Params.x)
                continue; // no star in this cell

            // Jitter the star inside its cell so it never sits on the lattice.
            const float2 jitter = float2(hash21(c + 17.13), hash21(c + 43.71));
            const float2 d = (float2(ox, oy) + jitter) - f;

            // One random drives size + brightness, skewed so most stars are tiny faint
            // pinpoints and only a few are large and bright (r*r biases toward 0).
            const float r = hash21(c + 71.97);
            const float mag = r * r;
            const float radius = lerp(0.12, 0.55, mag); // in cell units
            const float bright = lerp(0.25, 1.0, mag);

            // Round soft point; squared falloff keeps small stars crisp without hard edges.
            const float point = saturate(1.0 - length(d) / radius);
            star = max(star, point * point * bright);
        }
    }

    col += star * u_Params.y;
    return float4(col, 1.0);
}
