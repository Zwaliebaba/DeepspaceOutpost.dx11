// Scene3D billboard pixel shader: reproduce the flat screen-space planet/sun disk on a
// camera-facing quad. The quad-local radius r = length(uv) (uv in -1..1) selects the
// look by mode (u_Params.x):
//   0 sun     - radial bands white -> yellow -> orange (matches render_sun thresholds)
//   1 ring    - wireframe planet: a thin rim of ~constant screen thickness (the legacy
//               white circle)
//   2 disk    - filled planet (the legacy "Green" style)
//   3 banded  - SNES / fractal approximation: concentric bands of the two planet colours
// Fragments outside the unit disk are discarded so the quad reads as a circle.

#include "partials/scenebb.hlsli"

float4 PSMain(VSOut i) : SV_Target
{
    const float r = length(i.uv);
    const int mode = (int) (u_Params.x + 0.5);

    if (mode == 0) // sun: radial bands, soft-ish edge a touch past the disk
    {
        if (r > 1.08)
            discard;
        float3 c;
        if (r < 0.78)
            c = float3(1.0, 1.0, 1.0);          // white core
        else if (r < 0.86)
            c = float3(1.0, 0.93, 0.40);         // yellow
        else if (r < 0.93)
            c = float3(1.0, 0.6, 0.12);          // orange
        else
            c = float3(0.78, 0.40, 0.06);        // dark orange rim
        return float4(c, 1.0);
    }

    if (mode == 1) // wireframe planet: thin rim of ~constant screen thickness
    {
        const float aa = fwidth(r);
        if (abs(r - 1.0) > 1.5 * aa)
            discard;
        return float4(i.col.rgb, 1.0);
    }

    if (r > 1.0)
        discard;

    if (mode == 2) // filled planet
        return float4(i.col.rgb, 1.0);

    // mode 3: banded approximation of the SNES / fractal landscape.
    const float band = frac(r * 4.0);
    const float3 c = (band < 0.5) ? i.col.rgb : u_ColorB.rgb;
    return float4(c, 1.0);
}
