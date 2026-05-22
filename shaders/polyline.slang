// Polyline ribbon shader — per-segment capsule SDF rendering.
//
// One draw call per Stroke. The stroke engine tessellates each Stroke
// into a TRIANGLE_LIST of per-segment quads (6 verts per segment) —
// see `engine/stroke/stroke_geometry.hpp::tessellate_ribbon`. Each
// quad covers one consecutive sample pair, extended by the per-
// segment radius `r` past each endpoint along the tangent so the
// fragment shader has room to draw a rounded cap on both ends.
// Adjacent segment quads overlap at their shared sample point and
// the SDF makes the join naturally rounded, so the rendering is
// robust to U-turns / zigzags / sharp angles where the old per-
// sample triangle-strip topology would pinch and break.
//
// SDF reconstruction in the fragment from (side, t, K):
//   cap = max(0, |t|*(K+1) - K)   // 0 in body, >0 in cap
//   dist = length(side, cap)      // 1.0 at capsule edge
//   alpha *= 1.0 - smoothstep(1 - aa, 1, dist)
//
// Where:
//   - `side`: perpendicular signed distance, -1 left edge, +1 right.
//   - `t`: tangential signed coord, -1 start-cap rim, +1 end-cap rim.
//   - `K`: body half-length / radius. K → ∞ means a long thin capsule
//     (almost a rectangle), K → 0 means a stubby dot-like capsule.
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
    float2 pos       : POSITION;   // canvas pixels, top-left origin
    float4 col       : COLOR;      // straight-alpha RGBA
    float  side      : TEXCOORD0;  // -1 left edge, +1 right edge
    float  t         : TEXCOORD1;  // -1 start-cap rim, +1 end-cap rim
    float  K         : TEXCOORD2;  // body-half-length / radius
    float  softness  : TEXCOORD3;  // [0, 1] edge feather fraction
};

struct VOut {
    float4 pos       : SV_Position;
    float4 col       : TEXCOORD0;
    float3 sdf       : TEXCOORD1;  // (side, t, K) interpolated across the quad
    float  softness  : TEXCOORD2;
};

[shader("vertex")]
VOut vs_polyline(VIn input) {
    VOut o;
    // Pixel → NDC.
    o.pos = float4(input.pos / u_view.canvas_size * 2.0 - 1.0, 0.0, 1.0);
    o.col = input.col;
    o.sdf = float3(input.side, input.t, input.K);
    o.softness = input.softness;
    return o;
}

[shader("fragment")]
float4 ps_polyline(VOut input) : SV_Target {
    // Capsule SDF distance.
    //
    // `side` is normalized so the perpendicular edge is at |side|=1.
    // `t` is normalized so the outer cap rim is at |t|=1. The body
    // occupies |t| ≤ K/(K+1); cap pixels are |t| > K/(K+1).
    //
    // `cap` is 0 inside the body and grows linearly past the body
    // boundary to 1 at the outer cap rim. `length(side, cap)` is
    // the unsigned SDF distance: 1 at the capsule edge, < 1 inside,
    // > 1 outside the capsule (transparent).
    const float K = input.sdf.z;
    const float cap = max(0.0, abs(input.sdf.y) * (K + 1.0) - K);
    const float dist = length(float2(input.sdf.x, cap));

    // Hard-edge AA band from screen-space derivatives — narrow
    // gradient (~1 pixel) at the capsule rim. Caps guard against
    // thin strokes / zoom-out eating all body opacity.
    const float aa_band = min(fwidth(dist), 0.35);

    // Soft brush feather, driven by per-vertex `softness` (0..1).
    // The feather widens the smoothstep band, fading the outer
    // `softness * 0.85` of the capsule from solid to transparent.
    // 0.85 cap keeps even fully-soft brushes from going invisible
    // at the centre. When softness=0 we get the hard-edge AA path
    // (max picks the wider band, so the larger of "aa" and
    // "feather" wins).
    const float feather = clamp(input.softness, 0.0, 1.0) * 0.85;
    const float band = max(aa_band, feather);
    const float edge = smoothstep(1.0 - band, 1.0, dist);
    float4 col = input.col;
    col.a *= 1.0 - edge;
    return col;
}
