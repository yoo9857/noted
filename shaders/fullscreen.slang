// Fullscreen-triangle vertex + fragment entries.
//
// vs_main          — generates a NDC-covering triangle from gl_VertexIndex
//                    {0,1,2} and forwards a UV. No vertex buffer.
// ps_gradient      — UV-based gradient. Useful as a fallback or smoke test.
// ps_textured      — samples set 0 binding 0 (combined image sampler) at the
//                    forwarded UV. The main renderer uses this entry.
//
// Compiled to SPIR-V via slangc; see shaders/CMakeLists.txt.

struct VOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
VOut vs_main(uint vid : SV_VertexID) {
    VOut o;
    o.uv  = float2(float((vid << 1) & 2), float(vid & 2));
    o.pos = float4(o.uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 ps_gradient(VOut input) : SV_Target {
    return float4(input.uv, 0.5, 1.0);
}

[[vk::binding(0, 0)]]
Sampler2D u_image;

[shader("fragment")]
float4 ps_textured(VOut input) : SV_Target {
    return u_image.Sample(input.uv);
}
