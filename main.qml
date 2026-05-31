import QtQuick
import QtQuick.Controls
import VulkanCAEViewer

Window {
    id: root
    width:  1280
    height: 720
    visible: true
    title: "Vulkan CAE Viewer — Step 1"
    color: "#141416"

    // ── 3D Viewport ───────────────────────────────────────────────────────────
    ViewerItem {
        id: viewer
        anchors.fill: parent
    }

    // ── Overlay UI (demonstrates that QML sits correctly above the Vulkan surface)
    Rectangle {
        anchors {
            top:    parent.top
            left:   parent.left
            right:  parent.right
        }
        height: 40
        color: Qt.rgba(0, 0, 0, 0.55)

        Text {
            anchors.centerIn: parent
            text: "Vulkan → VkImage → QSGTexture  |  %1 × %2 px"
                      .arg(viewer.width).arg(viewer.height)
            color: "#cccccc"
            font.pixelSize: 13
            font.family: "monospace"
        }
    }

    // ── Resize feedback ───────────────────────────────────────────────────────
    Text {
        anchors {
            bottom: parent.bottom
            right:  parent.right
            margins: 12
        }
        text: Qt.size(viewer.width, viewer.height).width + " × "
            + Qt.size(viewer.width, viewer.height).height
        color: "#555"
        font.pixelSize: 11
    }
}
