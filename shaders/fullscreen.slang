// Fullscreen-triangle vertex + fragment entries.
//
// vs_main          — generates a NDC-covering triangle from gl_VertexIndex
//                    {0,1,2} and forwards a UV. No vertex buffer.
// ps_gradient      — UV-based gradient. Useful as a fallback or smoke test.
// ps_textured      — samples set 0 binding 0 (combined image sampler) at the
//                    forwarded UV. The main renderer uses this entry.
//
// Compiled to SPIR-V via slangc; see shaders/CMakeLists.txt.

// View transform applied by vs_main. `scale` and `translation` come
// from `noted::canvas::Camera::shader_scale_*` /
// `shader_translation_*` so the identity camera reproduces the
// original `ndc = uv * 2 - 1` mapping with scale=(2,2) and
// translation=(-1,-1). Push-constant size = 16 bytes — well under
// the 128-byte Vulkan minimum.
struct CompositePush {
    float2 scale;        // offset 0, 8 bytes
    float2 translation;  // offset 8, 8 bytes
};

[[vk::push_constant]]
CompositePush u_view;

struct VOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
VOut vs_main(uint vid : SV_VertexID) {
    VOut o;
    o.uv  = float2(float((vid << 1) & 2), float(vid & 2));
    // o.pos.xy = uv * scale + translation; at identity that reduces
    // to uv * 2 - 1 — the standard fullscreen-triangle mapping.
    o.pos = float4(o.uv * u_view.scale + u_view.translation, 0.0, 1.0);
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
