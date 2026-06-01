#version 450

layout(set = 0, binding = 0) uniform UBO {
    mat4 mvp;
} ubo;

layout(location = 0) in vec3 inPosition;

void main()
{
    gl_Position = ubo.mvp * vec4(inPosition, 1.0);
}
