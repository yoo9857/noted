// Per-layer fullscreen-triangle compositor shader.
//
// Two fragment entry points share the same vertex stage:
//
//   * `ps_layer`           — fixed-function blend path. The pipeline's
//                            blend state mixes this shader's output
//                            (`u_layer.color * mask`) with the dst
//                            via FF. Used for `normal` / `multiply` /
//                            `screen` / `linear_dodge`.
//
//   * `ps_layer_shader_blend` — Photoshop blend modes that FF can't
//                            express. Reads the previous composite
//                            result from `u_dst` (sampled snapshot of
//                            the canvas, copied by the compositor
//                            before this draw), computes the per-mode
//                            blend formula, and outputs the FINAL
//                            pixel with `blendEnable = FALSE`. Twelve
//                            modes share one entry — a `switch` on
//                            `u_layer.mode` picks the formula. Modes
//                            without a case (or with `mode == 0` for
//                            NORMAL) collapse to a straight `over`
//                            composite.
//
// Mask convention:
//   - 0.0 = fully outside selection (this layer's contribution to the
//     pixel becomes zero — the dst pixel survives every blend mode).
//   - 1.0 = fully inside selection (no masking).
//   - The compositor binds a 1x1 "all selected" dummy when the caller
//     doesn't supply a real mask, so the shader is unconditional.
//
// Pre-multiplied alpha is assumed on the input and output. For the
// shader-blend path the math un-premultiplies internally (dividing by
// alpha + a tiny epsilon to avoid div-by-zero on transparent pixels),
// runs the per-mode formula on straight RGB, then re-premultiplies
// before output.
//
// Mode ordinals match `noted::domain::BlendMode` exactly so the C++
// side can push the same byte without translation. See
// docs/architecture/0038-shader-blend-modes.md.
//
// Push constant: 32 bytes (vec4 color + uint mode + 12 bytes pad).
// Well under the 128-byte Vulkan minimum.

struct LayerPush {
    float4 color;  // premultiplied RGBA — the source layer's contribution
    uint mode;     // BlendMode ordinal (0..15). See enum BlendMode in layer.hpp.
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

[[vk::push_constant]]
LayerPush u_layer;

[[vk::binding(0, 0)]]
Sampler2D u_mask;

// Sampled snapshot of the canvas as it stood after the previous
// layer. The compositor copies the canvas image into this binding
// just before the shader-blend draw — the sampler reads dst RGBA
// at the same UV the fragment writes to, with linear filter giving
// pixel-exact correspondence at the fullscreen-triangle scale.
//
// Only the `ps_layer_shader_blend` entry samples this. The FF path
// never binds it — set 1 stays unused for the FF pipeline.
[[vk::binding(0, 1)]]
Sampler2D u_dst;

struct LayerVOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
LayerVOut vs_layer(uint vid : SV_VertexID) {
    // Fullscreen triangle: three vertices forming a triangle that
    // covers the [-1, 1] NDC viewport. UV in [0, 1] over the
    // visible quad — same parameterisation the canvas-space
    // selection mask was rasterized in, so a 1:1 sample matches
    // the destination pixel.
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

// ---- Shader-blend math (12 modes) -----------------------------------------

// Tiny epsilon to keep un-premultiply / division-by-zero pixels well
// behaved. Picked to be small relative to 1/255 (the smallest
// distinguishable 8-bit channel value) so it has no visible effect
// on the output.
static const float kEps = 1e-5;

// W3C compositing-1.0 helpers for the four HSL modes (hue,
// saturation, color, luminosity). Reference math:
// https://www.w3.org/TR/compositing-1/#blendingnonseparable
float luminance(float3 c) {
    return 0.3 * c.r + 0.59 * c.g + 0.11 * c.b;
}

float channel_min(float3 c) { return min(c.r, min(c.g, c.b)); }
float channel_max(float3 c) { return max(c.r, max(c.g, c.b)); }

float3 clip_color(float3 c) {
    float l = luminance(c);
    float n = channel_min(c);
    float x = channel_max(c);
    if (n < 0.0) {
        c = l + (c - l) * l / max(l - n, kEps);
    }
    if (x > 1.0) {
        c = l + (c - l) * (1.0 - l) / max(x - l, kEps);
    }
    return c;
}

float3 set_lum(float3 c, float l_target) {
    float d = l_target - luminance(c);
    return clip_color(c + d);
}

float3 set_sat(float3 c, float s_target) {
    // W3C reference: sort RGB by value, scale [min, max] into the
    // [0, s_target] range, leave min at 0 and max at s_target.
    // Branchless implementation via min/max channel lookups.
    float cmax = channel_max(c);
    float cmin = channel_min(c);
    if (cmax > cmin) {
        return (c - cmin) * s_target / (cmax - cmin);
    }
    return float3(0.0, 0.0, 0.0);
}

float saturation_w3c(float3 c) {
    return channel_max(c) - channel_min(c);
}

// Returns the Photoshop blend of straight (un-premultiplied) src
// and dst RGB at the given mode ordinal. Modes that fall through
// (`mode == 0` / NORMAL / unsupported) return src untouched — the
// alpha-compositing step below then collapses to standard "over".
float3 photoshop_blend(uint mode, float3 src, float3 dst) {
    switch (mode) {
        case 1: // multiply
            return src * dst;
        case 2: // screen
            return src + dst - src * dst;
        case 3: { // overlay  = hard_light with src/dst swapped
            float3 lo = 2.0 * src * dst;
            float3 hi = 1.0 - 2.0 * (1.0 - src) * (1.0 - dst);
            return lerp(lo, hi, step(0.5, dst));
        }
        case 4: { // soft_light (W3C compositing reference)
            float3 g = lerp(((16.0 * dst - 12.0) * dst + 4.0) * dst,
                            sqrt(max(dst, 0.0)),
                            step(0.25, dst));
            float3 lo = dst - (1.0 - 2.0 * src) * dst * (1.0 - dst);
            float3 hi = dst + (2.0 * src - 1.0) * (g - dst);
            return lerp(lo, hi, step(0.5, src));
        }
        case 5: { // hard_light
            float3 lo = 2.0 * src * dst;
            float3 hi = 1.0 - 2.0 * (1.0 - src) * (1.0 - dst);
            return lerp(lo, hi, step(0.5, src));
        }
        case 6: { // color_dodge: dst / (1 - src), clamped
            // Photoshop convention: dst==0 → 0; src==1 → 1; else
            // dst / (1-src) clamped to [0,1].
            float3 base = dst / max(1.0 - src, kEps);
            float3 r = saturate(base);
            r = lerp(r, float3(0.0), step(dst, float3(0.0)));
            r = lerp(r, float3(1.0), step(float3(1.0), src));
            return r;
        }
        case 7: { // color_burn: 1 - (1 - dst) / src, clamped
            float3 base = 1.0 - (1.0 - dst) / max(src, kEps);
            float3 r = saturate(base);
            r = lerp(r, float3(1.0), step(float3(1.0), dst));
            r = lerp(r, float3(0.0), step(src, float3(0.0)));
            return r;
        }
        case 8: // linear_dodge (= add)
            return saturate(src + dst);
        case 9: // linear_burn
            return saturate(src + dst - 1.0);
        case 10: // difference
            return abs(src - dst);
        case 11: // exclusion
            return src + dst - 2.0 * src * dst;
        case 12: // hue: dst luminance + dst saturation + src hue
            return set_lum(set_sat(src, saturation_w3c(dst)), luminance(dst));
        case 13: // saturation
            return set_lum(set_sat(dst, saturation_w3c(src)), luminance(dst));
        case 14: // color: src hue+sat with dst luminance
            return set_lum(src, luminance(dst));
        case 15: // luminosity: dst hue+sat with src luminance
            return set_lum(dst, luminance(src));
        default: // 0 = normal, or unknown
            return src;
    }
}

[shader("fragment")]
float4 ps_layer_shader_blend(LayerVOut input) : SV_Target {
    float m = u_mask.Sample(input.uv).r;
    float4 src_premul = u_layer.color * m;
    float4 dst_premul = u_dst.Sample(input.uv);

    // Un-premultiply both sides so the blend formulas operate on
    // straight RGB (their textbook domain). Guard against division
    // by zero — fully-transparent pixels contribute zero anyway,
    // so the unmul result is irrelevant beyond "not NaN".
    float3 src_rgb = src_premul.rgb / max(src_premul.a, kEps);
    float3 dst_rgb = dst_premul.rgb / max(dst_premul.a, kEps);

    float3 blended = photoshop_blend(u_layer.mode, src_rgb, dst_rgb);

    // W3C compositing: with `over` alpha-compositing the new RGB is
    //   src.a * blended + (1 - src.a) * dst.a * dst.rgb / out.a
    // and `out.a = src.a + dst.a * (1 - src.a)`. Premultiply for the
    // attachment write (matches the pipeline's straight-alpha →
    // premultiplied convention).
    float src_a = src_premul.a;
    float dst_a = dst_premul.a;
    float out_a = src_a + dst_a * (1.0 - src_a);
    float3 out_rgb_straight =
        src_a * blended + (1.0 - src_a) * dst_rgb * (dst_a / max(out_a, kEps));
    float3 out_rgb_premul = out_rgb_straight * out_a;
    return float4(out_rgb_premul, out_a);
}
