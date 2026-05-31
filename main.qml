import QtQuick
import QtQuick.Controls
import VulkanCAEViewer

Window {
    id: root
    width:  1280
    height: 720
    visible: true
    title: "Vulkan CAE Viewer — drag to rotate, scroll to zoom"
    color: "#141416"

    // ── 3D Viewport ───────────────────────────────────────────────────────────
    ViewerItem {
        id: viewer
        anchors.fill: parent

        // Drag to orbit (left button); scroll wheel / trackpad to zoom. Handling
        // the wheel inside the MouseArea avoids a separate pointer handler
        // competing with it for the same events.
        MouseArea {
            id: dragArea
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            property real lastX: 0
            property real lastY: 0

            onPressed: (mouse) => {
                lastX = mouse.x;
                lastY = mouse.y;
            }
            onPositionChanged: (mouse) => {
                viewer.orbit(mouse.x - lastX, mouse.y - lastY);
                lastX = mouse.x;
                lastY = mouse.y;
            }
            onWheel: (wheel) => {
                // angleDelta is in eighths of a degree; 120 == one notch. Trackpads
                // report smaller continuous deltas, which scale naturally.
                viewer.zoom(wheel.angleDelta.y / 120.0);
            }
        }
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
