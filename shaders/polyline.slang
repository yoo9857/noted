// Polyline ribbon shader.
//
// One draw call per Stroke. The stroke engine tessellates each Stroke
// into a triangle-strip ribbon (left / right vertices alternating along
// the centerline, width modulated by pressure — see
// `engine/stroke/stroke_geometry.hpp::tessellate_ribbon`). The ribbon
// rides a single shared vertex buffer; `firstVertex` + `vertexCount`
// pick out each stroke's slice.
//
// Coordinate space:
//   - Vertex input `pos` is in canvas pixels (top-left origin), the
//     same units the stroke engine stores after `Camera::unproject`.
//   - `u_view.canvas_size` lets the vertex shader convert pixels →
//     NDC: `ndc = pos / canvas_size * 2 - 1`. Vulkan's y-down NDC
//     matches the y-down pixel convention so no flip is needed.
//
// Push-constant budget: 8 bytes (one float2). Well under the 128-byte
// Vulkan minimum, room for future view-state additions.

struct PolylinePush {
    float2 canvas_size;  // pixels
};

[[vk::push_constant]]
PolylinePush u_view;

struct VIn {
    float2 pos : POSITION;  // canvas pixels, top-left origin
    float4 col : COLOR;     // straight-alpha RGBA
};

struct VOut {
    float4 pos : SV_Position;
    float4 col : TEXCOORD0;
};

[shader("vertex")]
VOut vs_polyline(VIn input) {
    VOut o;
    // Pixel → NDC. Same form as the stamp shader's last line, so the
    // visual placement of a ribbon centerline matches what the disk
    // stamp used to draw at the same canvas pixel.
    o.pos = float4(input.pos / u_view.canvas_size * 2.0 - 1.0, 0.0, 1.0);
    o.col = input.col;
    return o;
}

[shader("fragment")]
float4 ps_polyline(VOut input) : SV_Target {
    // The vertex's straight-alpha RGBA flows through unchanged; the
    // pipeline's SRC_ALPHA / ONE_MINUS_SRC_ALPHA blend handles
    // compositing onto the canvas.
    return input.col;
}
