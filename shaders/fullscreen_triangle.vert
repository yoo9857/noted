#version 450

// Emits a single triangle covering the full clip-space rectangle without
// a vertex buffer. The trick: with gl_VertexIndex in {0, 1, 2} we produce
// positions (-1,-1), (3,-1), (-1,3). The clipped result covers the whole
// NDC quad. Barycentric UVs are forwarded for the fragment shader.

layout(location = 0) out vec2 v_uv;

void main() {
    v_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
}
