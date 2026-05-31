#pragma once

#include <vulkan/vulkan.h>
#include <QSize>
#include <QString>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// VulkanRenderer
//
// Owns all Vulkan objects needed to render a colored triangle into an offscreen
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

    void init(const QSize &size);
    void resize(const QSize &size);
    void render();

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

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    VkShaderModule loadShaderModule(const QString &spvPath) const;
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

    // Command recording
    VkCommandPool   m_commandPool   = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;

    // Synchronisation (step 1: simple fence for CPU–GPU sync)
    VkFence m_fence = VK_NULL_HANDLE;

    static constexpr VkFormat k_colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    static constexpr VkFormat k_depthFormat = VK_FORMAT_D32_SFLOAT;
};
