#pragma once

#include "TrackballCamera.h"

#include <QQuickItem>
#include <QSGTexture>
#include <memory>

class VulkanRenderer;

// ─────────────────────────────────────────────────────────────────────────────
// ViewerItem
//
// A QQuickItem that hosts the Vulkan renderer and presents its output as a
// native texture in the Qt scene graph. The surrounding QML can overlay any
// controls on top without the z-order issues of an embedded native window.
//
// All Vulkan work happens on Qt's render thread. The QML/main thread only
// touches the item's public Qt properties and signals.
// ─────────────────────────────────────────────────────────────────────────────
class ViewerItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit ViewerItem(QQuickItem *parent = nullptr);
    ~ViewerItem() override;

    // Camera controls, called from QML input handlers on the main thread.
    // dx/dy are pixel deltas; they are normalised by the item size internally.
    Q_INVOKABLE void orbit(qreal dxPixels, qreal dyPixels);
    Q_INVOKABLE void zoom(qreal steps);

protected:
    // Called on the render thread when the scene graph is ready.
    // Safe to create Vulkan resources here.
    void handleSceneGraphInitialized();

    // Called on the render thread just before the scene graph is invalidated.
    // Must release all GPU resources.
    void handleSceneGraphInvalidated();

    // Called on the render thread each frame — builds/updates the scene graph node.
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

    // Called on the main thread when the item's geometry changes.
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    std::unique_ptr<VulkanRenderer> m_renderer;

    // Pixel size passed from the main thread to the render thread.
    // Protected by Qt's render-thread synchronisation (updatePaintNode is
    // only entered after the main thread is blocked).
    QSize m_pendingSize;
    bool  m_sizeChanged = false;

    // Camera lives on the main thread; only the resulting matrix crosses to the
    // render thread, inside updatePaintNode (main thread blocked there).
    TrackballCamera m_camera;
};
