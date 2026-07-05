// Full-screen triangle vertex shader (H4 post chain). No vertex buffer: three
// SV_VertexID-driven verts cover the screen; uv is the [0,1] texture coordinate.

struct PostVSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

PostVSOut VSMain(uint vid : SV_VertexID)
{
    PostVSOut o;
    // (0,0),(2,0),(0,2) in uv -> a triangle covering the [-1,1] clip square.
    o.uv  = float2((vid == 1) ? 2.0 : 0.0, (vid == 2) ? 2.0 : 0.0);
    o.pos = float4(o.uv.x * 2.0 - 1.0, 1.0 - o.uv.y * 2.0, 0.0, 1.0);
    return o;
}
