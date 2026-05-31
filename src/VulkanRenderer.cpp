#include "VulkanRenderer.h"

#include <QFile>
#include <QDebug>
#include <array>
#include <cstring>
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

    if (m_descriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    if (m_descriptorSetLayout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    if (m_uniformBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(m_device, m_uniformBuffer, nullptr);
    if (m_uniformBufferMemory != VK_NULL_HANDLE) {
        // Persistently mapped; unmap implicitly handled by freeing the memory.
        vkFreeMemory(m_device, m_uniformBufferMemory, nullptr);
    }

    if (m_indexBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(m_device, m_indexBuffer, nullptr);
    if (m_indexBufferMemory != VK_NULL_HANDLE)
        vkFreeMemory(m_device, m_indexBufferMemory, nullptr);
    if (m_vertexBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
    if (m_vertexBufferMemory != VK_NULL_HANDLE)
        vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);

    if (m_framebuffer != VK_NULL_HANDLE)
        vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
    if (m_renderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);

    destroyDepthResources();
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
    createDepthResources();
    createUniformBuffer();   // descriptor set layout used by the pipeline layout
    createPipeline();
    createDescriptors();     // pool + set, bound to the uniform buffer
    createFramebuffer();

    // Step 2: hardcoded cube. Replaced by a loaded mesh in a later step.
    createMeshBuffers(makeCube());
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
    destroyDepthResources();
    destroyImageResources();

    createImageResources();
    createDepthResources();
    createFramebuffer();
}

// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::render()
{
    // Upload the current MVP. QMatrix4x4 stores values column-major internally
    // and constData() returns them in the order GLSL/std140 expects for a mat4.
    std::memcpy(m_uniformMapped, m_mvp.constData(), sizeof(float) * 16);

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
    // Clear values are indexed by attachment: [0] = color, [1] = depth.
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color        = {{0.08f, 0.08f, 0.10f, 1.0f}}; // near-black background
    clearValues[1].depthStencil = {1.0f, 0};                     // far plane

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass        = m_renderPass;
    rpInfo.framebuffer       = m_framebuffer;
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = {static_cast<uint32_t>(m_size.width()),
                                static_cast<uint32_t>(m_size.height())};
    rpInfo.clearValueCount   = static_cast<uint32_t>(clearValues.size());
    rpInfo.pClearValues      = clearValues.data();

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

    vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    VkBuffer     vertexBuffers[] = {m_vertexBuffer};
    VkDeviceSize offsets[]       = {0};
    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(m_commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(m_commandBuffer, m_indexCount, 1, 0, 0, 0);

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

void VulkanRenderer::createDepthResources()
{
    // ── Create depth VkImage ──────────────────────────────────────────────────
    VkImageCreateInfo imageInfo{};
    imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType     = VK_IMAGE_TYPE_2D;
    imageInfo.format        = k_depthFormat;
    imageInfo.extent        = {static_cast<uint32_t>(m_size.width()),
                               static_cast<uint32_t>(m_size.height()), 1};
    imageInfo.mipLevels     = 1;
    imageInfo.arrayLayers   = 1;
    imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VK_CHECK(vkCreateImage(m_device, &imageInfo, nullptr, &m_depthImage),
             "vkCreateImage (depth)");

    // ── Allocate device memory ────────────────────────────────────────────────
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_device, m_depthImage, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(m_device, &allocInfo, nullptr, &m_depthImageMemory),
             "vkAllocateMemory (depth image)");
    VK_CHECK(vkBindImageMemory(m_device, m_depthImage, m_depthImageMemory, 0),
             "vkBindImageMemory (depth)");

    // ── Create depth VkImageView ──────────────────────────────────────────────
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image                           = m_depthImage;
    viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format                          = k_depthFormat;
    viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = 1;
    VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &m_depthImageView),
             "vkCreateImageView (depth)");
}

void VulkanRenderer::destroyDepthResources()
{
    if (m_depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    if (m_depthImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_depthImage, nullptr);
        m_depthImage = VK_NULL_HANDLE;
    }
    if (m_depthImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_depthImageMemory, nullptr);
        m_depthImageMemory = VK_NULL_HANDLE;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer helpers
// ─────────────────────────────────────────────────────────────────────────────

void VulkanRenderer::createBuffer(VkDeviceSize          size,
                                  VkBufferUsageFlags    usage,
                                  VkMemoryPropertyFlags properties,
                                  VkBuffer             &buffer,
                                  VkDeviceMemory       &memory) const
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size        = size;
    bufferInfo.usage       = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer), "vkCreateBuffer");

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(m_device, buffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, properties);
    VK_CHECK(vkAllocateMemory(m_device, &allocInfo, nullptr, &memory), "vkAllocateMemory (buffer)");

    VK_CHECK(vkBindBufferMemory(m_device, buffer, memory, 0), "vkBindBufferMemory");
}

void VulkanRenderer::uploadToDeviceLocalBuffer(const void        *src,
                                               VkDeviceSize       size,
                                               VkBufferUsageFlags usage,
                                               VkBuffer          &buffer,
                                               VkDeviceMemory    &memory)
{
    // 1. Host-visible staging buffer, filled via memcpy.
    VkBuffer       stagingBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
    createBuffer(size,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingBufferMemory);

    void *mapped = nullptr;
    VK_CHECK(vkMapMemory(m_device, stagingBufferMemory, 0, size, 0, &mapped), "vkMapMemory (staging)");
    std::memcpy(mapped, src, static_cast<size_t>(size));
    vkUnmapMemory(m_device, stagingBufferMemory);

    // 2. Device-local destination buffer.
    createBuffer(size,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 buffer, memory);

    // 3. One-time command buffer to copy staging → device-local.
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool        = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer copyCmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &copyCmd),
             "vkAllocateCommandBuffers (copy)");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(copyCmd, &beginInfo), "vkBeginCommandBuffer (copy)");

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(copyCmd, stagingBuffer, buffer, 1, &copyRegion);

    VK_CHECK(vkEndCommandBuffer(copyCmd), "vkEndCommandBuffer (copy)");

    VkSubmitInfo submitInfo{};
    submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers    = &copyCmd;
    VK_CHECK(vkQueueSubmit(m_queue, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit (copy)");
    // Upload happens once at init; a full queue wait is acceptable here.
    VK_CHECK(vkQueueWaitIdle(m_queue), "vkQueueWaitIdle (copy)");

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &copyCmd);
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingBufferMemory, nullptr);
}

void VulkanRenderer::createMeshBuffers(const MeshData &mesh)
{
    const VkDeviceSize vertexSize = sizeof(Vertex) * mesh.vertices.size();
    const VkDeviceSize indexSize  = sizeof(uint32_t) * mesh.indices.size();

    uploadToDeviceLocalBuffer(mesh.vertices.data(), vertexSize,
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                              m_vertexBuffer, m_vertexBufferMemory);

    uploadToDeviceLocalBuffer(mesh.indices.data(), indexSize,
                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                              m_indexBuffer, m_indexBufferMemory);

    m_indexCount = static_cast<uint32_t>(mesh.indices.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// Uniform buffer + descriptors (MVP)
// ─────────────────────────────────────────────────────────────────────────────

void VulkanRenderer::createUniformBuffer()
{
    // A single mat4 (MVP). Host-visible + coherent + persistently mapped so the
    // render thread can overwrite it each frame with no explicit flush.
    const VkDeviceSize bufferSize = sizeof(float) * 16;

    createBuffer(bufferSize,
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_uniformBuffer, m_uniformBufferMemory);

    VK_CHECK(vkMapMemory(m_device, m_uniformBufferMemory, 0, bufferSize, 0, &m_uniformMapped),
             "vkMapMemory (uniform)");

    // Descriptor set layout: binding 0 = uniform buffer, visible to vertex stage.
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding         = 0;
    uboBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings    = &uboBinding;
    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout),
             "vkCreateDescriptorSetLayout");
}

void VulkanRenderer::createDescriptors()
{
    // Pool sized for our single uniform-buffer descriptor set.
    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;
    poolInfo.maxSets       = 1;
    VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool),
             "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &m_descriptorSetLayout;
    VK_CHECK(vkAllocateDescriptorSets(m_device, &allocInfo, &m_descriptorSet),
             "vkAllocateDescriptorSets");

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = m_uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range  = sizeof(float) * 16;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_descriptorSet;
    write.dstBinding      = 0;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo     = &bufferInfo;
    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
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

    // ── Depth attachment ──────────────────────────────────────────────────────
    // Cleared on load, not stored (we never read it back), and not transitioned
    // for sampling — it lives and dies inside the render pass each frame.
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format         = k_depthFormat;
    depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // Subpass dependency: make sure the render pass finishes writing before
    // the subsequent pipeline barrier (image transition) can read it.
    // Two external dependencies:
    //  - [0] IN: the depth attachment's UNDEFINED→DEPTH layout transition and
    //    clear must not race a previous frame's depth tests.
    //  - [1] OUT: the color write must finish before our post-pass barrier
    //    transitions it to SHADER_READ_ONLY for Qt to sample.
    std::array<VkSubpassDependency, 2> dependencies{};

    dependencies[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass    = 0;
    dependencies[0].srcStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                                  | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                                  | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass    = 0;
    dependencies[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT
                                  | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpInfo.pAttachments    = attachments.data();
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    rpInfo.pDependencies   = dependencies.data();

    VK_CHECK(vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_renderPass),
             "vkCreateRenderPass");
}

void VulkanRenderer::createFramebuffer()
{
    // Attachment order must match the render pass: [0] color, [1] depth.
    std::array<VkImageView, 2> attachments = {m_colorImageView, m_depthImageView};

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass      = m_renderPass;
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments    = attachments.data();
    fbInfo.width           = static_cast<uint32_t>(m_size.width());
    fbInfo.height          = static_cast<uint32_t>(m_size.height());
    fbInfo.layers          = 1;
    VK_CHECK(vkCreateFramebuffer(m_device, &fbInfo, nullptr, &m_framebuffer),
             "vkCreateFramebuffer");
}

void VulkanRenderer::createPipeline()
{
    VkShaderModule vertModule = loadShaderModule(QStringLiteral(SHADER_DIR "mesh.vert.spv"));
    VkShaderModule fragModule = loadShaderModule(QStringLiteral(SHADER_DIR "mesh.frag.spv"));

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName  = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName  = "main";

    // Interleaved position+normal vertices from a single bound buffer.
    auto bindingDesc = Vertex::bindingDescription();
    auto attrDescs   = Vertex::attributeDescriptions();
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount   = 1;
    vertexInput.pVertexBindingDescriptions      = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions    = attrDescs.data();

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
    rasterizer.cullMode    = VK_CULL_MODE_BACK_BIT;
    // Cube faces wind CCW when viewed from outside. The MVP includes a Y-flip
    // (see TrackballCamera) to map OpenGL-style NDC to Vulkan, which preserves
    // winding, so CCW remains the front face.
    rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth   = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth test/write enabled, standard "smaller depth = closer" comparison.
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS;

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
    layoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts    = &m_descriptorSetLayout;
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
    pipelineInfo.pDepthStencilState  = &depthStencil;
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
