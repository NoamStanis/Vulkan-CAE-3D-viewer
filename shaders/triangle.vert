#version 450

// Hardcoded NDC positions and per-vertex colors for the step-1 triangle.
// No vertex buffers needed — we index into these arrays with gl_VertexIndex.
vec2 positions[3] = vec2[](
    vec2( 0.0, -0.6),   // top centre
    vec2( 0.6,  0.6),   // bottom right
    vec2(-0.6,  0.6)    // bottom left
);

vec3 colors[3] = vec3[](
    vec3(0.20, 0.60, 1.00),  // blue
    vec3(0.00, 0.85, 0.55),  // teal
    vec3(0.90, 0.30, 0.10)   // orange-red
);

layout(location = 0) out vec3 fragColor;

void main()
{
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor   = colors[gl_VertexIndex];
}
