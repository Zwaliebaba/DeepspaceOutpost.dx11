// Procedural skybox pixel shader: a vertical gradient plus procedurally-placed stars.
// Cheap and asset-free; the look is driven entirely by the b0 params so Scene3D can tune
// gradient colours and star density/brightness. (Screen-space stars for now - see the
// note in partials/skybox.hlsli.)

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

    // One candidate star per grid cell: keep it when its hash passes the threshold,
    // brightest at the cell centre so it reads as a small round point.
    const float density = max(u_Params.z, 1.0);
    const float2 grid = p * density;
    const float2 cell = floor(grid);
    if (hash21(cell) > u_Params.x)
    {
        const float2 f = frac(grid) - 0.5;
        const float d = saturate(1.0 - length(f) * 3.0);
        col += (d * d) * u_Params.y;
    }

    return float4(col, 1.0);
}
