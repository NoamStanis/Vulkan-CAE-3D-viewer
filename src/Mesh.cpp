#include "Mesh.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

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

// ─────────────────────────────────────────────────────────────────────────────
Bounds MeshData::bounds() const
{
    Bounds b;
    if (vertices.empty())
        return b;

    for (int i = 0; i < 3; ++i)
        b.min[i] = b.max[i] = vertices[0].position[i];

    for (const Vertex &v : vertices) {
        for (int i = 0; i < 3; ++i) {
            b.min[i] = std::min(b.min[i], v.position[i]);
            b.max[i] = std::max(b.max[i], v.position[i]);
        }
    }
    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
EdgeData makeTriangleEdges(const MeshData &mesh)
{
    EdgeData edges;
    if (mesh.indices.size() < 3)
        return edges;

    // Collect unique undirected edges (min,max) so shared triangle edges aren't
    // drawn twice.
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> unique;
    auto addEdge = [&](uint32_t a, uint32_t b) {
        const uint32_t lo = std::min(a, b);
        const uint32_t hi = std::max(a, b);
        const uint64_t key = (static_cast<uint64_t>(lo) << 32) | hi;
        unique.emplace(key, std::make_pair(lo, hi));
    };

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t a = mesh.indices[i + 0];
        const uint32_t b = mesh.indices[i + 1];
        const uint32_t c = mesh.indices[i + 2];
        addEdge(a, b);
        addEdge(b, c);
        addEdge(c, a);
    }

    edges.positions.reserve(unique.size() * 6);
    for (const auto &kv : unique) {
        for (uint32_t idx : {kv.second.first, kv.second.second}) {
            const Vertex &v = mesh.vertices[idx];
            edges.positions.push_back(v.position[0]);
            edges.positions.push_back(v.position[1]);
            edges.positions.push_back(v.position[2]);
        }
    }
    return edges;
}

// ─────────────────────────────────────────────────────────────────────────────
// loadObj
//
// Triangulates faces via tinyobjloader and de-duplicates (position, normal)
// pairs into a unique vertex list with an index buffer. If the OBJ supplies no
// normals, flat per-face normals are generated from the triangle geometry.
// ─────────────────────────────────────────────────────────────────────────────
MeshData loadObj(const std::string &path)
{
    tinyobj::ObjReaderConfig config;
    config.triangulate = true;

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(path, config)) {
        std::string err = "loadObj: failed to parse '" + path + "'";
        if (!reader.Error().empty())
            err += ": " + reader.Error();
        throw std::runtime_error(err);
    }

    const tinyobj::attrib_t      &attrib = reader.GetAttrib();
    const std::vector<tinyobj::shape_t> &shapes = reader.GetShapes();

    MeshData mesh;

    // Key a unique vertex by its (position-index, normal-index) pair so shared
    // verts collapse but hard normal edges stay split.
    struct Key { int p; int n; };
    struct KeyHash {
        size_t operator()(const Key &k) const {
            return std::hash<long long>()((static_cast<long long>(k.p) << 32) ^ (k.n & 0xffffffffLL));
        }
    };
    struct KeyEq {
        bool operator()(const Key &a, const Key &b) const { return a.p == b.p && a.n == b.n; }
    };
    std::unordered_map<Key, uint32_t, KeyHash, KeyEq> unique;

    for (const tinyobj::shape_t &shape : shapes) {
        const auto &idx = shape.mesh.indices;
        for (size_t f = 0; f + 2 < idx.size(); f += 3) {
            // Gather the three corners; compute a geometric normal as fallback.
            tinyobj::index_t tri[3] = {idx[f + 0], idx[f + 1], idx[f + 2]};

            float facePos[3][3];
            for (int c = 0; c < 3; ++c) {
                const int vi = tri[c].vertex_index;
                facePos[c][0] = attrib.vertices[3 * vi + 0];
                facePos[c][1] = attrib.vertices[3 * vi + 1];
                facePos[c][2] = attrib.vertices[3 * vi + 2];
            }
            // Flat normal from edge cross product (used when the OBJ lacks normals).
            float e1[3], e2[3], fn[3];
            for (int i = 0; i < 3; ++i) {
                e1[i] = facePos[1][i] - facePos[0][i];
                e2[i] = facePos[2][i] - facePos[0][i];
            }
            fn[0] = e1[1] * e2[2] - e1[2] * e2[1];
            fn[1] = e1[2] * e2[0] - e1[0] * e2[2];
            fn[2] = e1[0] * e2[1] - e1[1] * e2[0];
            float len = std::sqrt(fn[0] * fn[0] + fn[1] * fn[1] + fn[2] * fn[2]);
            if (len > 0.0f) { fn[0] /= len; fn[1] /= len; fn[2] /= len; }

            for (int c = 0; c < 3; ++c) {
                const Key key{tri[c].vertex_index, tri[c].normal_index};
                auto it = unique.find(key);
                if (it != unique.end()) {
                    mesh.indices.push_back(it->second);
                    continue;
                }

                Vertex v{};
                v.position[0] = facePos[c][0];
                v.position[1] = facePos[c][1];
                v.position[2] = facePos[c][2];

                if (tri[c].normal_index >= 0 && !attrib.normals.empty()) {
                    const int ni = tri[c].normal_index;
                    v.normal[0] = attrib.normals[3 * ni + 0];
                    v.normal[1] = attrib.normals[3 * ni + 1];
                    v.normal[2] = attrib.normals[3 * ni + 2];
                } else {
                    v.normal[0] = fn[0];
                    v.normal[1] = fn[1];
                    v.normal[2] = fn[2];
                }

                const uint32_t newIndex = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(v);
                unique.emplace(key, newIndex);
                mesh.indices.push_back(newIndex);
            }
        }
    }

    if (mesh.vertices.empty())
        throw std::runtime_error("loadObj: '" + path + "' contained no triangles");

    return mesh;
}
