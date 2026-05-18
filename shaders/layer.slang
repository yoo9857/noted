// Per-layer fullscreen-triangle compositor shader.
//
// The compositor draws one fullscreen triangle per visible LayerNode.
// The blend mode comes from pipeline state (fixed-function blend); the
// per-layer color comes from a push constant; the fragment samples an
// R8 selection mask at (set=0, binding=0) and multiplies output alpha
// by it.
//
// Mask convention:
//   - 0.0 = fully outside selection (this layer's contribution to the
//     pixel becomes zero — the dst pixel survives every blend mode).
//   - 1.0 = fully inside selection (no masking).
//   - The compositor binds a 1x1 "all selected" dummy when the caller
//     doesn't supply a real mask, so the shader is unconditional. The
//     cost is one bilinear tap, which the compositor's call site is
//     fine with at fullscreen-triangle scale.
//
// Pre-multiplied alpha is assumed on the input: r/g/b already carry
// the alpha multiplication. Multiplying output by mask preserves the
// premultiplied invariant — output is `(rgb*m, a*m)` which is the
// pre-multiplied form of `(rgb, a*m)` since (rgb) was already (rgb*a).
//
// Push constant: 16 bytes — well under the 128-byte Vulkan minimum.

struct LayerPush {
    float4 color;  // premultiplied RGBA
};

[[vk::push_constant]]
LayerPush u_layer;

[[vk::binding(0, 0)]]
Sampler2D u_mask;

struct LayerVOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
LayerVOut vs_layer(uint vid : SV_VertexID) {
    // Same fullscreen-triangle trick as fullscreen.slang: three
    // vertices forming a triangle that covers the [-1, 1] NDC viewport.
    // UV is in [0, 1] over the visible quad — same parameterization
    // the canvas-space selection mask was rasterized in, so a 1:1
    // sample matches the destination pixel.
    float2 uv = float2(float((vid << 1) & 2), float(vid & 2));
    LayerVOut o;
    o.uv  = uv;
    o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 ps_layer(LayerVOut input) : SV_Target {
    // R8_UNORM mask returns the red channel in [0, 1] on the .r swizzle.
    float m = u_mask.Sample(input.uv).r;
    return u_layer.color * m;
}
