#version 450

// Outputs a UV-based gradient so a missing texture or pipeline issue is
// visible. Replace with a textured sample once the texture binding lands
// in feat/render-image.

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 frag_color;

void main() {
    frag_color = vec4(v_uv, 0.5, 1.0);
}
