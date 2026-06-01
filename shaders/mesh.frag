#version 450

layout(location = 0) in  vec3 fragNormal;
layout(location = 0) out vec4 outColor;

// Push constant: lighting toggle (1 = lit/diffuse, 0 = flat/unlit).
layout(push_constant) uniform Push {
    uint lit;
} pc;

void main()
{
    vec3 base = vec3(0.55, 0.65, 0.85);

    if (pc.lit == 0u) {
        // Flat / unlit: solid base color, no shading.
        outColor = vec4(base, 1.0);
        return;
    }

    // Fixed directional light in object space. Because the camera orbits the
    // object (rather than the object rotating under a world light), lighting the
    // object-space normal gives stable, readable shading as the view rotates.
    vec3  N        = normalize(fragNormal);
    vec3  lightDir = normalize(vec3(0.4, 0.7, 0.6));
    float diffuse  = max(dot(N, lightDir), 0.0);
    float ambient  = 0.25;

    outColor = vec4(base * (ambient + diffuse), 1.0);
}
