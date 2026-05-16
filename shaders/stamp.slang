// Disk-stamp shader.
//
// Each instance of the pipeline draws a single anti-aliased disk into the
// canvas. The stamp's center / radius / color come from a push constant; the
// vertex shader synthesizes a 6-vertex quad in pixel space and maps it to
// NDC, and the fragment shader computes a signed-distance-to-disk and
// alpha-blends a smoothstep edge.
//
// Pixel-space contract:
//   - canvas_size : the canvas extent in pixels.
//   - center_px   : disk center, (0,0) = top-left, matches GLFW pointer
//                   coordinates after the same flip-free chain everyone
//                   else uses.
//   - radius_px   : disk radius in pixels.
//   - softness_px : width (in pixels) of the smoothstep edge. ~1px gives
//                   a hard but anti-aliased look; larger values are softer
//                   brushes.
//
// Color is straight-alpha RGBA. The pipeline pairs this with
// VK_BLEND_FACTOR_SRC_ALPHA / VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA (see
// stroke_engine.cpp).
//
// Push-constant budget: 40 bytes — well under the Vulkan-minimum 128.
//
// Member order is chosen so std430 lays the struct out with zero internal
// padding: vec4 first (16-byte align), then vec2s (8-byte align), then
// scalars (4-byte align). The matching C++ POD in stroke_engine.cpp keeps
// the same order.

struct StampPush {
    float4 color;        // straight RGBA   — offset  0, 16 bytes
    float2 canvas_size;  // pixels          — offset 16,  8 bytes
    float2 center_px;    // pixels, top-left origin — offset 24, 8 bytes
    float  radius_px;    //                  — offset 32,  4 bytes
    float  softness_px;  //                  — offset 36,  4 bytes
};

[[vk::push_constant]]
StampPush u_stamp;

struct StampVOut {
    float4 pos      : SV_Position;
    float2 local_px : TEXCOORD0;  // offset from stamp center, in pixels
};

[shader("vertex")]
StampVOut vs_stamp(uint vid : SV_VertexID) {
    // Two triangles forming a quad. Order chosen so the front face under
    // CCW + the renderer's CULL_NONE convention is moot (we draw both).
    const float2 kCorners[6] = {
        float2(-1.0, -1.0), float2( 1.0, -1.0), float2(-1.0,  1.0),
        float2( 1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0),
    };
    float2 corner   = kCorners[vid];
    float2 local_px = corner * u_stamp.radius_px;
    float2 pixel    = u_stamp.center_px + local_px;
    // Pixel-to-NDC: maps [0, canvas_size] to [-1, 1].
    float2 ndc      = pixel / u_stamp.canvas_size * 2.0 - 1.0;

    StampVOut o;
    o.pos      = float4(ndc, 0.0, 1.0);
    o.local_px = local_px;
    return o;
}

[shader("fragment")]
float4 ps_stamp(StampVOut input) : SV_Target {
    // Signed distance to the disk's edge: negative inside, positive outside.
    float d = length(input.local_px) - u_stamp.radius_px;
    // smoothstep(-softness, 0, d): 0 deep inside, 1 outside. Invert for alpha.
    float alpha_mask = 1.0 - smoothstep(-u_stamp.softness_px, 0.0, d);
    float4 c = u_stamp.color;
    c.a *= alpha_mask;
    return c;
}
