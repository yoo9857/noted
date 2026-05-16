// Per-layer fullscreen-triangle compositor shader.
//
// The compositor draws one fullscreen triangle per visible LayerNode.
// The blend mode comes from pipeline state (fixed-function blend); the
// per-layer color comes from a push constant; the fragment shader is a
// flat color emitter.
//
// Pre-multiplied alpha is assumed: r/g/b already carry the alpha
// multiplication. This pairs with the standard "src + (1-src.a)*dst"
// blend math the compositor configures.
//
// 16 bytes total — well under the 128-byte Vulkan minimum.

struct LayerPush {
    float4 color;  // premultiplied RGBA
};

[[vk::push_constant]]
LayerPush u_layer;

struct LayerVOut {
    float4 pos : SV_Position;
};

[shader("vertex")]
LayerVOut vs_layer(uint vid : SV_VertexID) {
    // Same fullscreen-triangle trick as fullscreen.slang: three vertices
    // forming a triangle that covers the [-1, 1] NDC viewport.
    float2 uv = float2(float((vid << 1) & 2), float(vid & 2));
    LayerVOut o;
    o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 ps_layer(LayerVOut /*input*/) : SV_Target {
    return u_layer.color;
}
