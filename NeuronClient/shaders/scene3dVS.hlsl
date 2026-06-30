// Scene3D vertex shader: transform a model-space vertex to clip space through the
// per-model model->view->projection matrix (b0). Normal + colour pass through.

#include "partials/scene3d.hlsli"

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = mul(u_MVP, float4(i.pos, 1.0));
    o.nrm = i.nrm;
    o.col = i.col;
    return o;
}
