#include "VulkanRenderer.h"

#include <QFile>
#include <QDebug>
#include <array>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Tiny helper – throw on Vulkan error
// ─────────────────────────────────────────────────────────────────────────────
static void VK_CHECK(VkResult result, const char *msg)
{
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(msg) + " (VkResult=" + std::to_string(result) + ")");
}

// ─────────────────────────────────────────────────────────────────────────────
VulkanRenderer::VulkanRenderer(VkInstance       instance,
                               VkPhysicalDevice physicalDevice,
                               VkDevice         device,
                               uint32_t         graphicsQueueFamilyIndex,
                               VkQueue          graphicsQueue)
    : m_instance(instance)
    , m_physicalDevice(physicalDevice)
    , m_device(device)
    , m_queueFamilyIdx(graphicsQueueFamilyIndex)
    , m_queue(graphicsQueue)
{}

VulkanRenderer::~VulkanRenderer()
{
    if (m_device == VK_NULL_HANDLE)
        return;

    vkDeviceWaitIdle(m_device);

    if (m_fence != VK_NULL_HANDLE)
        vkDestroyFence(m_device, m_fence, nullptr);
    if (m_commandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    if (m_pipeline != VK_NULL_HANDLE)
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pipelineLayout != VK_NULL_HANDLE)
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_framebuffer != VK_NULL_HANDLE)
        vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
    if (m_renderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);

    destroyImageResources();
}

// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::init(const QSize &size)
{
    m_size = size;

    createCommandPool();
    createSyncObjects();
    createRenderPass();
    createImageResources();
    createFramebuffer();
    createPipeline();
}

void VulkanRenderer::resize(const QSize &size)
{
    if (m_size == size)
        return;

    vkDeviceWaitIdle(m_device);
    m_size = size;

    // Recreate size-dependent resources
    vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
    m_framebuffer = VK_NULL_HANDLE;
    destroyImageResources();

    createImageResources();
    createFramebuffer();
}

// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::render()
{
    // Reset fence and command buffer
    vkResetFences(m_device, 1, &m_fence);
    vkResetCommandBuffer(m_commandBuffer, 0);

    // ── Begin recording ───────────────────────────────────────────────────────
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(m_commandBuffer, &beginInfo), "vkBeginCommandBuffer");

    // Transition: UNDEFINED → COLOR_ATTACHMENT_OPTIMAL
    transitionImageLayout(m_commandBuffer, m_colorImage,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // ── Render pass ───────────────────────────────────────────────────────────
    VkClearValue clearColor{};
    clearColor.color = {{0.08f, 0.08f, 0.10f, 1.0f}}; // near-black background

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass        = m_renderPass;
    rpInfo.framebuffer       = m_framebuffer;
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = {static_cast<uint32_t>(m_size.width()),
                                static_cast<uint32_t>(m_size.height())};
    rpInfo.clearValueCount   = 1;
    rpInfo.pClearValues      = &clearColor;

    vkCmdBeginRenderPass(m_commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(m_size.width());
    viewport.height   = static_cast<float>(m_size.height());
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<uint32_t>(m_size.width()),
                      static_cast<uint32_t>(m_size.height())};
    vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);

    vkCmdDraw(m_commandBuffer, 3, 1, 0, 0); // 3 hardcoded verts in the shader

    vkCmdEndRenderPass(m_commandBuffer);

    // Transition: COLOR_ATTACHMENT_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
    // Qt's scene graph will sample this image as a texture.
    transitionImageLayout(m_commandBuffer, m_colorImage,
                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VK_CHECK(vkEndCommandBuffer(m_commandBuffer), "vkEndCommandBuffer");

    // ── Submit ────────────────────────────────────────────────────────────────
    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &m_commandBuffer;

    VK_CHECK(vkQueueSubmit(m_queue, 1, &submitInfo, m_fence), "vkQueueSubmit");

    // Step 1: block until done so Qt can safely sample the image.
    // In a later step this fence wait will be replaced by a semaphore
    // signalled to Qt's present queue.
    vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
}

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────────────────

void VulkanRenderer::createCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamilyIdx;
    VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool),
             "vkCreateCommandPool");

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &m_commandBuffer),
             "vkAllocateCommandBuffers");
}

void VulkanRenderer::createSyncObjects()
{
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // Start signalled so the first vkResetFences doesn't deadlock
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &m_fence), "vkCreateFence");
}

void VulkanRenderer::createImageResources()
{
    // ── Create VkImage ────────────────────────────────────────────────────────
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = k_colorFormat;
    imageInfo.extent        = {static_cast<uint32_t>(m_size.width()),
                               static_cast<uint32_t>(m_size.height()), 1};
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                            | VK_IMAGE_USAGE_SAMPLED_BIT;  // Qt samples it as a texture
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VK_CHECK(vkCreateImage(m_device, &imageInfo, nullptr, &m_colorImage),
             "vkCreateImage");

    // ── Allocate device memory ────────────────────────────────────────────────
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_device, m_colorImage, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(m_device, &allocInfo, nullptr, &m_colorImageMemory),
             "vkAllocateMemory (color image)");
    VK_CHECK(vkBindImageMemory(m_device, m_colorImage, m_colorImageMemory, 0),
             "vkBindImageMemory");

    // ── Create VkImageView ────────────────────────────────────────────────────
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = m_colorImage;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = k_colorFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;
    VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &m_colorImageView),
             "vkCreateImageView");
}

void VulkanRenderer::destroyImageResources()
{
    if (m_colorImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_colorImageView, nullptr);
        m_colorImageView = VK_NULL_HANDLE;
    }
    if (m_colorImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_colorImage, nullptr);
        m_colorImage = VK_NULL_HANDLE;
    }
    if (m_colorImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_colorImageMemory, nullptr);
        m_colorImageMemory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::createRenderPass()
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = k_colorFormat;
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // The render pass takes the image from COLOR_ATTACHMENT_OPTIMAL ...
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // ... and leaves it in COLOR_ATTACHMENT_OPTIMAL.
    // We manually transition to SHADER_READ_ONLY after the pass ends.
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    // Subpass dependency: make sure the render pass finishes writing before
    // the subsequent pipeline barrier (image transition) can read it.
    VkSubpassDependency dependency{};
    dependency.srcSubpass    = 0;
    dependency.dstSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT
                             | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments    = &colorAttachment;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies   = &dependency;

    VK_CHECK(vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderPass),
             "vkCreateRenderPass");
}

void VulkanRenderer::createFramebuffer()
{
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass      = m_renderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments    = &m_colorImageView;
    fbInfo.width           = static_cast<uint32_t>(m_size.width());
    fbInfo.height          = static_cast<uint32_t>(m_size.height());
    fbInfo.layers          = 1;
    VK_CHECK(vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_framebuffer),
             "vkCreateFramebuffer");
}

void VulkanRenderer::createPipeline()
{
    VkShaderModule vertModule = loadShaderModule(QStringLiteral(SHADER_DIR "triangle.vert.spv"));
    VkShaderModule fragModule = loadShaderModule(QStringLiteral(SHADER_DIR "triangle.frag.spv"));

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName  = "main";

    // No vertex buffers — positions are hardcoded in the vertex shader.
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport and scissor are dynamic — set per-frame in render()
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode    = VK_CULL_MODE_NONE;
    rasterizer.frontFace   = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Simple opaque blend
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                   | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable    = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments    = &blendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates    = dynamicStates.data();

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VK_CHECK(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout),
             "vkCreatePipelineLayout");

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount          = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages             = stages.data();
    pipelineInfo.pVertexInputState   = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pColorBlendState    = &colorBlend;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = m_pipelineLayout;
    pipelineInfo.renderPass          = m_renderPass;
    pipelineInfo.subpass             = 0;

    VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1,
                                       &pipelineInfo, nullptr, &m_pipeline),
             "vkCreateGraphicsPipelines");

    // Shader modules are no longer needed once the pipeline is compiled.
    vkDestroyShaderModule(m_device, vertModule, nullptr);
    vkDestroyShaderModule(m_device, fragModule, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
uint32_t VulkanRenderer::findMemoryType(uint32_t typeFilter,
                                        VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i))
            && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    throw std::runtime_error("findMemoryType: no suitable memory type found");
}

VkShaderModule VulkanRenderer::loadShaderModule(const QString &spvPath) const
{
    QFile file(spvPath);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Failed to open shader: " + spvPath.toStdString());

    QByteArray bytes = file.readAll();

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = static_cast<size_t>(bytes.size());
    moduleInfo.pCode    = reinterpret_cast<const uint32_t *>(bytes.constData());

    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(m_device, &moduleInfo, nullptr, &shaderModule),
             "vkCreateShaderModule");
    return shaderModule;
}

void VulkanRenderer::transitionImageLayout(VkCommandBuffer cmd,
                                           VkImage         image,
                                           VkImageLayout   oldLayout,
                                           VkImageLayout   newLayout)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED
        && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
             && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        throw std::runtime_error("transitionImageLayout: unsupported layout transition");
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                         0, nullptr,
                         0, nullptr,
                         1, &barrier);
}
