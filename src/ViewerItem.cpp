#include "ViewerItem.h"
#include "VulkanRenderer.h"
#include "Mesh.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <QVulkanInstance>
#include <QDebug>
#include <vulkan/vulkan.h>

// QNativeInterface::QSGVulkanTexture is declared in Qt 6.2+.
// It is typically pulled in by <QQuickWindow> transitively.
// If the compiler can't find the symbol, explicitly add:
//   #include <QtQuick/qsgnativetexture.h>   (Qt 6.4+)
// or check Qt's installed headers for the exact path on your SDK.

// ─────────────────────────────────────────────────────────────────────────────
ViewerItem::ViewerItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);

    // Load the model on the main thread (file I/O + bounds), falling back to a
    // unit cube if the asset is missing or unreadable. The prepared MeshData is
    // handed to the renderer when the scene graph initialises.
    try {
        m_mesh = loadObj(ASSET_DIR "satellite.obj");
    } catch (const std::exception &e) {
        qWarning() << "ViewerItem: OBJ load failed, using fallback cube:" << e.what();
        m_mesh = makeCube();
    }

    // Frame the camera to the loaded mesh's bounds so it fits on load, and size
    // the axis gizmo to roughly the model's extent.
    const Bounds b = m_mesh.bounds();
    float c[3];
    b.center(c);
    const float radius = b.radius();
    m_camera.frame(QVector3D(c[0], c[1], c[2]), radius > 0.0f ? radius : 1.0f);
    m_axisLength = (radius > 0.0f ? radius : 1.0f) * 1.25f;

    // These signals are emitted on the render thread, so use DirectConnection.
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *w) {
        if (!w) return;

        connect(w, &QQuickWindow::sceneGraphInitialized,
                this, &ViewerItem::handleSceneGraphInitialized,
                Qt::DirectConnection);

        connect(w, &QQuickWindow::sceneGraphInvalidated,
                this, &ViewerItem::handleSceneGraphInvalidated,
                Qt::DirectConnection);
    });
}

ViewerItem::~ViewerItem() = default;

// ─────────────────────────────────────────────────────────────────────────────
// Render-thread slots
// ─────────────────────────────────────────────────────────────────────────────

void ViewerItem::handleSceneGraphInitialized()
{
    // Qt guarantees this runs on the render thread with the Vulkan context current.
    QQuickWindow *w = window();
    if (!w) return;

    QSGRendererInterface *ri = w->rendererInterface();
    if (ri->graphicsApi() != QSGRendererInterface::Vulkan) {
        qWarning() << "ViewerItem: window is not using Vulkan backend!";
        return;
    }

    // Borrow Qt's Vulkan handles. Qt owns these — do NOT destroy them.
    auto *vkInstance    = reinterpret_cast<VkInstance *>(
        ri->getResource(w, QSGRendererInterface::VulkanInstanceResource));
    auto *vkPhysDevice  = reinterpret_cast<VkPhysicalDevice *>(
        ri->getResource(w, QSGRendererInterface::PhysicalDeviceResource));
    auto *vkDevice      = reinterpret_cast<VkDevice *>(
        ri->getResource(w, QSGRendererInterface::DeviceResource));
    auto *vkQueue        = reinterpret_cast<VkQueue *>(
        ri->getResource(w, QSGRendererInterface::CommandQueueResource));
    // Qt 6 exposes this as int*. Cast to uint32_t for Vulkan APIs.
    auto *queueFamilyIdx = reinterpret_cast<int *>(
        ri->getResource(w, QSGRendererInterface::GraphicsQueueFamilyIndexResource));

    if (!vkInstance || !vkPhysDevice || !vkDevice || !vkQueue || !queueFamilyIdx) {
        qWarning() << "ViewerItem: failed to retrieve Vulkan handles from Qt";
        return;
    }

    QSize pixelSize = (size() * w->devicePixelRatio()).toSize();
    if (!pixelSize.isValid())
        pixelSize = QSize(1, 1);

    try {
        m_renderer = std::make_unique<VulkanRenderer>(
            *vkInstance, *vkPhysDevice, *vkDevice,
            static_cast<uint32_t>(*queueFamilyIdx), *vkQueue);
        m_renderer->init(pixelSize, m_mesh, m_axisLength);
    } catch (const std::exception &e) {
        qWarning() << "ViewerItem: renderer init FAILED:" << e.what();
        m_renderer.reset();
    }
}

void ViewerItem::handleSceneGraphInvalidated()
{
    // Render thread — safe to destroy GPU resources.
    m_renderer.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene graph update — render thread
// ─────────────────────────────────────────────────────────────────────────────

QSGNode *ViewerItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!m_renderer || !window())
        return nullptr;

    // Apply a pending resize if the item was resized on the main thread.
    if (m_sizeChanged) {
        m_renderer->resize(m_pendingSize);
        m_sizeChanged = false;
    }

    // Push the current camera matrix. We are on the render thread here, but the
    // main thread is blocked during updatePaintNode, so reading the camera is
    // safe without locking.
    const QSize sz = m_renderer->size();
    const float aspect = sz.height() > 0
        ? static_cast<float>(sz.width()) / static_cast<float>(sz.height())
        : 1.0f;
    m_renderer->setMvp(m_camera.viewProjection(aspect));

    // Render our Vulkan scene into the offscreen VkImage.
    try {
        m_renderer->render();
    } catch (const std::exception &e) {
        qWarning() << "ViewerItem: render() FAILED:" << e.what();
        delete oldNode;
        return nullptr;
    }

    // ── Wrap the VkImage as a QSGTexture ─────────────────────────────────────
    // QNativeInterface::QSGVulkanTexture::fromNative is the Qt 6 API for this.
    // The texture object is owned by the scene graph node below.
    VkImage vkImage = m_renderer->colorImage();

    // fromNative wraps without taking ownership of the VkImage.
    // fromNative signature: (VkImage, VkImageLayout, QQuickWindow*, QSize, options)
    QSGTexture *texture = QNativeInterface::QSGVulkanTexture::fromNative(
        vkImage,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        window(),
        sz);

    if (!texture) {
        qWarning() << "ViewerItem: QNativeInterface::QSGVulkanTexture::fromNative returned null";
        delete oldNode;
        return nullptr;
    }

    // ── Build / update the scene graph node ──────────────────────────────────
    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);
    if (!node) {
        node = new QSGSimpleTextureNode();
        node->setFiltering(QSGTexture::Linear);
        node->setOwnsTexture(true); // node owns the wrapper; the VkImage stays ours
    }

    // Because the node owns its texture, setTexture() deletes the previous
    // wrapper for us. Each fromNative() call above allocates a fresh wrapper,
    // so we must NOT delete it manually here — doing so double-frees it and
    // corrupts the heap (crash inside setTexture on the next frame).
    node->setTexture(texture);
    node->setRect(boundingRect());

    // Ask Qt to call updatePaintNode again next frame so we render continuously.
    // Remove this for a static scene; call update() only when something changes.
    update();

    return node;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main-thread geometry change
// ─────────────────────────────────────────────────────────────────────────────

void ViewerItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);

    if (!window()) return;

    QSize pixelSize = (newGeometry.size() * window()->devicePixelRatio()).toSize();
    if (pixelSize.isValid() && pixelSize != m_pendingSize) {
        m_pendingSize = pixelSize;
        m_sizeChanged = true;
        update(); // trigger updatePaintNode on the render thread
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera input — main thread (invoked from QML)
// ─────────────────────────────────────────────────────────────────────────────

void ViewerItem::orbit(qreal dxPixels, qreal dyPixels)
{
    // Normalise by item size so a full-width drag is a consistent angular sweep.
    const qreal w = width()  > 0 ? width()  : 1.0;
    const qreal h = height() > 0 ? height() : 1.0;
    m_camera.rotate(static_cast<float>(dxPixels / w),
                    static_cast<float>(dyPixels / h));
    update();
}

void ViewerItem::zoom(qreal steps)
{
    m_camera.zoom(static_cast<float>(steps));
    update();
}
