// Page background shader — draws one paper-coloured page rectangle
// with an optional pattern (blank / lined / grid / dotted) into the
// canvas. One draw call per page; the compositor pass then composites
// any layers + strokes on top.
//
// Coordinate plumbing:
//   - `u_page.origin_px` + `u_page.extent_px` describe the page's
//     rect in canvas pixels (top-left origin, y-down).
//   - `u_page.canvas_size` is the canvas image extent in pixels —
//     vertex shader uses it for pixel → NDC.
//   - `u_page.bg_kind` selects the fragment pattern (0..3 — wire
//     ordinals match `noted::canvas::PageBackground`).
//
// Push budget: 32 bytes — well under the Vulkan 128-byte minimum.

struct PageBgPush {
    float2 canvas_size;  // pixels
    float2 origin_px;    // page top-left in canvas pixels
    float2 extent_px;    // page width × height in canvas pixels
    uint   bg_kind;      // PageBackground ordinal
    uint   _pad;         // align to 32 bytes
};

[[vk::push_constant]]
PageBgPush u_page;

struct VOut {
    float4 pos      : SV_Position;
    float2 local_px : TEXCOORD0;  // pixel offset inside the page, [0, extent]
};

[shader("vertex")]
VOut vs_page_bg(uint vid : SV_VertexID) {
    // Standard 6-vertex quad. Order: tri1 = (0,0)(1,0)(0,1),
    // tri2 = (1,0)(1,1)(0,1). Counter-clockwise under the
    // renderer's no-cull policy.
    const float2 corners[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(1, 0), float2(1, 1), float2(0, 1),
    };
    const float2 corner = corners[vid];
    const float2 local_px = corner * u_page.extent_px;
    const float2 px = u_page.origin_px + local_px;
    const float2 ndc = px / u_page.canvas_size * 2.0 - 1.0;

    VOut o;
    o.pos = float4(ndc, 0.0, 1.0);
    o.local_px = local_px;
    return o;
}

// Paper colour — slightly warm off-white so it reads as paper rather
// than UI surface against the theme's neutral grey canvas. Matches
// what Notability / Goodnotes use as their default page.
static const float3 kPaper = float3(0.985, 0.982, 0.972);
// Pattern ink — a light grey that's visible on paper but doesn't
// fight pen ink for visual attention.
static const float3 kLine = float3(0.78, 0.80, 0.84);

// Returns 1 when `v` mod `period` is within `width / 2` of zero —
// the unit ramp for drawing a 1D periodic stripe. `width` in pixels.
// Anti-aliasing band ≈ 1 px via the smoothstep so lines don't shimmer
// when the camera fractionally pans.
float stripe(float v, float period, float width) {
    const float m = abs(v - round(v / period) * period);
    return 1.0 - smoothstep(width * 0.5 - 0.5, width * 0.5 + 0.5, m);
}

// Returns 1 inside a dot of `radius` centred on the nearest grid
// intersection of `period`. Same smoothstep AA band as `stripe`.
float dotted(float2 p, float period, float radius) {
    const float2 nearest = round(p / period) * period;
    const float d = length(p - nearest);
    return 1.0 - smoothstep(radius - 0.5, radius + 0.5, d);
}

[shader("fragment")]
float4 ps_page_bg(VOut input) : SV_Target {
    // Pattern parameters — chosen to look at home next to a US Letter
    // page at 72 DPI. A 32 px line spacing maps to roughly a 0.44 in
    // gap, close to standard wide-ruled notebook paper.
    const float kLineSpacing = 32.0;
    const float kDotSpacing = 24.0;
    const float kDotRadius = 1.4;

    float ink = 0.0;
    if (u_page.bg_kind == 1u) {
        // lined — horizontal only
        ink = stripe(input.local_px.y, kLineSpacing, 1.0);
    } else if (u_page.bg_kind == 2u) {
        // grid — horizontal + vertical
        ink = max(stripe(input.local_px.y, kLineSpacing, 1.0),
                  stripe(input.local_px.x, kLineSpacing, 1.0));
    } else if (u_page.bg_kind == 3u) {
        // dotted — small dots at grid intersections
        ink = dotted(input.local_px, kDotSpacing, kDotRadius);
    }
    // bg_kind == 0 (blank): ink stays 0 → solid paper.

    const float3 rgb = lerp(kPaper, kLine, ink);
    return float4(rgb, 1.0);
}
