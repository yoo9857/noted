#version 450

// Samples a single 2D texture and writes the result to the color
// attachment. Set 0 binding 0 is the combined_image_sampler.

layout(set = 0, binding = 0) uniform sampler2D u_image;

layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 frag_color;

void main() {
    frag_color = texture(u_image, v_uv);
}
