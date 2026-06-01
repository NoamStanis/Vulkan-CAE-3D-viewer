#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Vertex / Mesh
//
// CPU-side mesh representation shared between the (future) loader and the
// renderer. A Mesh is just interleaved vertices plus an index list; the
// renderer uploads it into device-local VkBuffers.
//
// Vertex layout is interleaved: position then normal, both vec3. This matches
// the binding/attribute description returned below and the inputs declared in
// mesh.vert.
// ─────────────────────────────────────────────────────────────────────────────
struct Vertex
{
    float position[3];
    float normal[3];

    // One vertex buffer bound at binding 0, tightly packed.
    static VkVertexInputBindingDescription bindingDescription()
    {
        VkVertexInputBindingDescription binding{};
        binding.binding   = 0;
        binding.stride    = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    // location 0 = position, location 1 = normal.
    static std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 2> attrs{};

        attrs[0].location = 0;
        attrs[0].binding  = 0;
        attrs[0].format   = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset   = offsetof(Vertex, position);

        attrs[1].location = 1;
        attrs[1].binding  = 0;
        attrs[1].format   = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset   = offsetof(Vertex, normal);

        return attrs;
    }
};

// Axis-aligned bounding box of a mesh, plus convenience center/radius used to
// frame the camera so any loaded model fits the view on load.
struct Bounds
{
    float min[3]{0, 0, 0};
    float max[3]{0, 0, 0};

    void center(float out[3]) const
    {
        for (int i = 0; i < 3; ++i) out[i] = 0.5f * (min[i] + max[i]);
    }
    // Radius of the bounding sphere (half the diagonal of the box).
    float radius() const
    {
        float c[3];
        center(c);
        float r2 = 0.0f;
        for (int i = 0; i < 3; ++i) {
            const float d = max[i] - c[i];
            r2 += d * d;
        }
        float r = 0.0f;
        // simple sqrt without pulling <cmath> into the header
        if (r2 > 0.0f) { r = r2; for (int k = 0; k < 20; ++k) r = 0.5f * (r + r2 / r); }
        return r;
    }
};

struct MeshData
{
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;

    Bounds bounds() const;
};

// Line-segment geometry for the element-edge overlay: a flat list of float
// triples, two consecutive positions per edge (line-list topology). Empty when
// no edges are available (e.g. an unsupported source).
struct EdgeData
{
    std::vector<float> positions;  // 3 floats per endpoint, 6 per edge

    size_t vertexCount() const { return positions.size() / 3; }
    bool   empty()       const { return positions.empty(); }
};

// Element edges of a triangle mesh: every triangle edge, de-duplicated. Used for
// OBJ meshes (which have no FE element faces); for Nastran the edges come from
// VtkSurface::extractEdges instead (true element-face edges, no triangulation
// diagonals).
EdgeData makeTriangleEdges(const MeshData &mesh);

// A unit cube centred at the origin, with per-face normals (24 vertices so each
// face gets correct flat normals). Used as a fallback when no mesh is loaded.
MeshData makeCube();

// Load a triangulated mesh from a Wavefront OBJ file. Faces are triangulated
// and per-vertex normals are taken from the file, or generated from face
// geometry when the OBJ has none. Throws std::runtime_error on failure.
MeshData loadObj(const std::string &path);
