#include "Mesh.h"

// ─────────────────────────────────────────────────────────────────────────────
// makeCube
//
// Unit cube spanning [-0.5, 0.5] on each axis. Each of the 6 faces is built from
// 4 unique vertices so the face normal is flat (no normal averaging across
// edges). Triangle winding is counter-clockwise when viewed from outside, which
// pairs with VK_FRONT_FACE_COUNTER_CLOCKWISE + back-face culling.
// ─────────────────────────────────────────────────────────────────────────────
MeshData makeCube()
{
    MeshData mesh;

    // Helper: append one face given its 4 corners (CCW from outside) and normal.
    auto addFace = [&mesh](const float c0[3], const float c1[3],
                           const float c2[3], const float c3[3],
                           const float n[3]) {
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({{c0[0], c0[1], c0[2]}, {n[0], n[1], n[2]}});
        mesh.vertices.push_back({{c1[0], c1[1], c1[2]}, {n[0], n[1], n[2]}});
        mesh.vertices.push_back({{c2[0], c2[1], c2[2]}, {n[0], n[1], n[2]}});
        mesh.vertices.push_back({{c3[0], c3[1], c3[2]}, {n[0], n[1], n[2]}});
        // Two triangles: (0,1,2) and (0,2,3).
        mesh.indices.insert(mesh.indices.end(),
                            {base + 0, base + 1, base + 2,
                             base + 0, base + 2, base + 3});
    };

    constexpr float p = 0.5f;
    constexpr float m = -0.5f;

    // +X face
    addFace((float[]){p, m, m}, (float[]){p, p, m}, (float[]){p, p, p}, (float[]){p, m, p},
            (float[]){1, 0, 0});
    // -X face
    addFace((float[]){m, m, p}, (float[]){m, p, p}, (float[]){m, p, m}, (float[]){m, m, m},
            (float[]){-1, 0, 0});
    // +Y face
    addFace((float[]){m, p, m}, (float[]){m, p, p}, (float[]){p, p, p}, (float[]){p, p, m},
            (float[]){0, 1, 0});
    // -Y face
    addFace((float[]){m, m, p}, (float[]){m, m, m}, (float[]){p, m, m}, (float[]){p, m, p},
            (float[]){0, -1, 0});
    // +Z face
    addFace((float[]){m, m, p}, (float[]){p, m, p}, (float[]){p, p, p}, (float[]){m, p, p},
            (float[]){0, 0, 1});
    // -Z face
    addFace((float[]){p, m, m}, (float[]){m, m, m}, (float[]){m, p, m}, (float[]){p, p, m},
            (float[]){0, 0, -1});

    return mesh;
}
