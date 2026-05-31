#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
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

struct MeshData
{
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
};

// A unit cube centred at the origin, with per-face normals (24 vertices so each
// face gets correct flat normals). Used as the step-2 hardcoded mesh before the
// OBJ loader lands.
MeshData makeCube();
