#pragma once

#include "Mesh.h"
#include "DisplayMode.h"

#include <vulkan/vulkan.h>
#include <QSize>
#include <QString>
#include <QMatrix4x4>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// VulkanRenderer
//
// Owns all Vulkan objects needed to render an indexed mesh into an offscreen
// VkImage. Intentionally does NOT own the VkInstance, VkPhysicalDevice, or
// VkDevice — those are borrowed from Qt's scene graph so we share one GPU
// context with the compositor.
//
// Lifecycle (all calls must happen on the render thread):
//   1. Construct with borrowed handles.
//   2. init(size) — allocates image, pipeline, etc.
//   3. resize(size) — call when the item's pixel size changes.
//   4. render() — records and submits one frame; blocks until idle (step 1).
//   5. ~VulkanRenderer() — releases everything.
//
// Thread safety: none. The caller (ViewerItem) is responsible for driving this
// exclusively from the Qt render thread.
// ─────────────────────────────────────────────────────────────────────────────
class VulkanRenderer
{
public:
    VulkanRenderer(VkInstance       instance,
                   VkPhysicalDevice physicalDevice,
                   VkDevice         device,
                   uint32_t         graphicsQueueFamilyIndex,
                   VkQueue          graphicsQueue);
    ~VulkanRenderer();

    // No copy / no move — owns raw Vulkan handles.
    VulkanRenderer(const VulkanRenderer &) = delete;
    VulkanRenderer &operator=(const VulkanRenderer &) = delete;

    // `mesh` is uploaded as the object to render; `edges` are the element-edge
    // line segments for the overlay (may be empty); `axisLength` sizes the XYZ
    // orientation gizmo (typically the mesh's bounding radius).
    void init(const QSize &size, const MeshData &mesh,
              const EdgeData &edges, float axisLength);
    void resize(const QSize &size);
    void render();

    // Replace the rendered geometry on a live renderer (e.g. after File → Open).
    // Waits for the GPU to idle, then re-uploads mesh/edge/axis buffers. The
    // pipelines, descriptors, and uniform buffer are geometry-independent and
    // are left intact. Render-thread call.
    void setGeometry(const MeshData &mesh, const EdgeData &edges, float axisLength);

    // Set the model-view-projection matrix used for the next render().
    // Called on the render thread (from updatePaintNode) before render().
    void setMvp(const QMatrix4x4 &mvp) { m_mvp = mvp; }

    // Surface/edge display mode for the next render(). Render-thread call.
    void setDisplayMode(DisplayMode mode) { m_displayMode = mode; }

    // Toggle diffuse lighting on the surface (false = flat/unlit). Render thread.
    void setLit(bool lit) { m_lit = lit; }

    // The image is in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL after render().
    VkImage     colorImage()  const { return m_colorImage; }
    VkImageView colorImageView() const { return m_colorImageView; }
    QSize       size()        const { return m_size; }

private:
    // ── Helpers ──────────────────────────────────────────────────────────────
    void createImageResources();
    void destroyImageResources();
    void createDepthResources();
    void destroyDepthResources();
    void createRenderPass();
    void createFramebuffer();
    void createPipeline();
    void createCommandPool();
    void createSyncObjects();
    void createMeshBuffers(const MeshData &mesh);
    void createUniformBuffer();
    void createDescriptors();
    void createAxesPipeline();
    void createAxesBuffer(float length);
    void createEdgesPipeline();
    void createEdgesBuffer(const EdgeData &edges);

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    VkShaderModule loadShaderModule(const QString &spvPath) const;

    // Allocate a VkBuffer + backing memory. Caller owns and must destroy both.
    void createBuffer(VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer &buffer,
                      VkDeviceMemory &memory) const;

    // Copy host data into a new device-local buffer via a staging buffer.
    // Used for vertex/index data that never changes after upload.
    void uploadToDeviceLocalBuffer(const void *src,
                                   VkDeviceSize size,
                                   VkBufferUsageFlags usage,
                                   VkBuffer &buffer,
                                   VkDeviceMemory &memory);

    void transitionImageLayout(VkCommandBuffer cmd,
                               VkImage image,
                               VkImageLayout oldLayout,
                               VkImageLayout newLayout);

    // ── Borrowed handles (not owned) ─────────────────────────────────────────
    VkInstance       m_instance       = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice         m_device         = VK_NULL_HANDLE;
    uint32_t         m_queueFamilyIdx = 0;
    VkQueue          m_queue          = VK_NULL_HANDLE;

    // ── Owned resources ──────────────────────────────────────────────────────
    QSize m_size;

    // Offscreen render target
    VkImage        m_colorImage       = VK_NULL_HANDLE;
    VkDeviceMemory m_colorImageMemory = VK_NULL_HANDLE;
    VkImageView    m_colorImageView   = VK_NULL_HANDLE;

    // Depth buffer (not sampled by Qt — render-pass-internal only)
    VkImage        m_depthImage       = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView    m_depthImageView   = VK_NULL_HANDLE;

    // Render pass + framebuffer
    VkRenderPass  m_renderPass  = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;

    // Graphics pipeline
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       m_pipeline       = VK_NULL_HANDLE;

    // Mesh geometry (device-local, uploaded once)
    VkBuffer       m_vertexBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer       m_indexBuffer        = VK_NULL_HANDLE;
    VkDeviceMemory m_indexBufferMemory  = VK_NULL_HANDLE;
    uint32_t       m_indexCount         = 0;

    // XYZ axis gizmo: a separate line-topology pipeline over a small
    // position+color vertex buffer, reusing the shared MVP descriptor.
    VkPipeline     m_axesPipeline       = VK_NULL_HANDLE;
    VkBuffer       m_axesBuffer         = VK_NULL_HANDLE;
    VkDeviceMemory m_axesBufferMemory   = VK_NULL_HANDLE;
    uint32_t       m_axesVertexCount    = 0;

    // Element-edge overlay: a line-topology pipeline over a position-only buffer,
    // depth-biased so edges sit on top of the surface without z-fighting.
    VkPipeline     m_edgesPipeline      = VK_NULL_HANDLE;
    VkBuffer       m_edgesBuffer        = VK_NULL_HANDLE;
    VkDeviceMemory m_edgesBufferMemory  = VK_NULL_HANDLE;
    uint32_t       m_edgesVertexCount   = 0;

    DisplayMode    m_displayMode        = DisplayMode::Shaded;
    bool           m_lit                = true;

    // Uniform buffer (MVP), host-visible and persistently mapped.
    VkBuffer              m_uniformBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory        m_uniformBufferMemory = VK_NULL_HANDLE;
    void                 *m_uniformMapped       = nullptr;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
    VkDescriptorSet       m_descriptorSet       = VK_NULL_HANDLE;

    // Current MVP, uploaded into the uniform buffer each render().
    QMatrix4x4 m_mvp;

    // Command recording
    VkCommandPool   m_commandPool   = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;

    // Synchronisation (step 1: simple fence for CPU–GPU sync)
    VkFence m_fence = VK_NULL_HANDLE;

    static constexpr VkFormat k_colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    static constexpr VkFormat k_depthFormat = VK_FORMAT_D32_SFLOAT;
};
