#version 450

layout(location = 0) out vec4 outColor;

void main()
{
    // Dark gray element edges, legible over both the lit surface and the
    // near-black background (wireframe mode).
    outColor = vec4(0.05, 0.05, 0.07, 1.0);
}
