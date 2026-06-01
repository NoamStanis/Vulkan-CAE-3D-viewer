#include "ViewerItem.h"
#include "VulkanRenderer.h"
#include "Mesh.h"

#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <QVulkanInstance>
#include <QFileInfo>
#include <QUrl>
#include <QDebug>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>

#ifdef HAVE_VTK
#include "io/NastranReader.h"
#include "io/VtkSurface.h"
#endif

namespace {

// Load a model file by extension, filling both the shaded surface (`mesh`) and
// the element-edge overlay (`edges`). Nastran .bdf is supported only when the
// app was compiled with VTK (HAVE_VTK). For .bdf, edges are the true element-face
// edges from VTK; for OBJ they are the de-duplicated triangle edges. Throws on
// failure; the caller decides the fallback.
void loadModelFile(const QString &path, MeshData &mesh, EdgeData &edges)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    const std::string p = path.toStdString();

    if (suffix == "bdf" || suffix == "nas" || suffix == "dat") {
#ifdef HAVE_VTK
        auto grid = NastranReader::read(p);
        mesh  = VtkSurface::toMeshData(grid);
        edges = VtkSurface::extractEdges(grid);
        return;
#else
        throw std::runtime_error(
            "Nastran .bdf requires a VTK-enabled build (set VTK_DIR and rebuild)");
#endif
    }
    // Default: Wavefront OBJ. Edges are derived from the triangle mesh.
    mesh  = loadObj(p);
    edges = makeTriangleEdges(mesh);
}

} // namespace

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

    // Load the default model on the main thread. Prefer a Nastran .bdf when the
    // build has VTK, otherwise the bundled OBJ. On failure, fall back to a cube.
#ifdef HAVE_VTK
    const QString defaultModel = QStringLiteral(ASSET_DIR "plane.bdf");
#else
    const QString defaultModel = QStringLiteral(ASSET_DIR "satellite.obj");
#endif
    if (!loadModel(defaultModel)) {
        qWarning() << "ViewerItem: default model load failed — using fallback cube";
        m_mesh  = makeCube();
        m_edges = makeTriangleEdges(m_mesh);
        frameAndSizeAxes();
    }
    // The initial geometry is uploaded at scene-graph init, not via the
    // pending-swap path, so clear the flag loadModel() sets.
    m_geometryChanged = false;

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
        m_renderer->init(pixelSize, m_mesh, m_edges, m_axisLength);
        m_renderer->setDisplayMode(m_displayMode);
        m_renderer->setLit(m_lit);
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

    // Apply newly opened geometry (File → Open). Safe here: the main thread is
    // blocked during updatePaintNode, so m_mesh/m_edges aren't being mutated.
    if (m_geometryChanged) {
        try {
            m_renderer->setGeometry(m_mesh, m_edges, m_axisLength);
        } catch (const std::exception &e) {
            qWarning() << "ViewerItem: setGeometry failed:" << e.what();
        }
        m_geometryChanged = false;
    }

    // Push the current camera matrix. We are on the render thread here, but the
    // main thread is blocked during updatePaintNode, so reading the camera is
    // safe without locking.
    const QSize sz = m_renderer->size();
    const float aspect = sz.height() > 0
        ? static_cast<float>(sz.width()) / static_cast<float>(sz.height())
        : 1.0f;
    m_renderer->setMvp(m_camera.viewProjection(aspect));
    m_renderer->setDisplayMode(m_displayMode);
    m_renderer->setLit(m_lit);

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

void ViewerItem::pan(qreal dxPixels, qreal dyPixels)
{
    const qreal w = width()  > 0 ? width()  : 1.0;
    const qreal h = height() > 0 ? height() : 1.0;
    m_camera.pan(static_cast<float>(dxPixels / w),
                 static_cast<float>(dyPixels / h));
    update();
}

void ViewerItem::zoom(qreal steps)
{
    m_camera.zoom(static_cast<float>(steps));
    update();
}

void ViewerItem::setDisplayMode(int mode)
{
    if (mode < 0 || mode >= static_cast<int>(DisplayMode::Count))
        return;
    const auto m = static_cast<DisplayMode>(mode);
    if (m == m_displayMode)
        return;
    m_displayMode = m;
    emit displayModeChanged();
    update();  // re-render with the new mode (picked up in updatePaintNode)
}

void ViewerItem::cycleDisplayMode()
{
    const int next = (static_cast<int>(m_displayMode) + 1)
                     % static_cast<int>(DisplayMode::Count);
    setDisplayMode(next);
}

void ViewerItem::setLit(bool lit)
{
    if (lit == m_lit)
        return;
    m_lit = lit;
    emit litChanged();
    update();
}

void ViewerItem::fitToModel()
{
    // Re-center and re-zoom to the model, but keep the current rotation so "fit"
    // doesn't snap the view back to the default orientation.
    m_camera.frame(m_modelCenter, m_modelRadius, /*resetOrientation=*/false);
    update();
}

void ViewerItem::frameAndSizeAxes()
{
    const Bounds b = m_mesh.bounds();
    float c[3];
    b.center(c);
    m_modelCenter = QVector3D(c[0], c[1], c[2]);
    m_modelRadius = b.radius() > 0.0f ? b.radius() : 1.0f;
    m_camera.frame(m_modelCenter, m_modelRadius);  // reset orientation on (re)load

    // Axes emanate from the origin, so size them to clear the model's farthest
    // extent from the origin (not just its center), plus margin.
    float maxFromOrigin = 0.0f;
    for (int i = 0; i < 3; ++i)
        maxFromOrigin = std::max(maxFromOrigin,
                                 std::max(std::abs(b.min[i]), std::abs(b.max[i])));
    m_axisLength = (maxFromOrigin > 0.0f ? maxFromOrigin : m_modelRadius) * 1.5f;
}

bool ViewerItem::loadModel(const QString &path, QString *errorOut)
{
    MeshData mesh;
    EdgeData edges;
    try {
        loadModelFile(path, mesh, edges);
    } catch (const std::exception &e) {
        if (errorOut) *errorOut = QString::fromUtf8(e.what());
        return false;
    }

    m_mesh  = std::move(mesh);
    m_edges = std::move(edges);
    frameAndSizeAxes();
    m_geometryChanged = true;  // upload to the renderer on the next frame

    qInfo() << "ViewerItem: loaded model" << path
            << "(" << static_cast<int>(m_mesh.vertices.size()) << "verts,"
            << static_cast<int>(m_mesh.indices.size() / 3) << "tris,"
            << static_cast<int>(m_edges.vertexCount() / 2) << "edges )";
    update();
    return true;
}

void ViewerItem::openFile(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QString err;
    if (!loadModel(path, &err)) {
        qWarning() << "ViewerItem: open failed for" << path << ":" << err;
        emit loadError(QStringLiteral("Could not open %1:\n%2").arg(path, err));
    }
}
