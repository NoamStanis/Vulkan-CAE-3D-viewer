import QtQuick
import QtQuick.Controls
import VulkanCAEViewer

Window {
    id: root
    width:  1280
    height: 720
    visible: true
    title: "Vulkan CAE Viewer — drag · scroll zoom · R/P rotate/pan · E edges · Space fit"
    color: "#141416"

    // Mode-button labels reflect the lighting state: with shading off the
    // surface is flat, so "Shaded" reads as "Flat". Wireframe has no surface, so
    // it is unaffected.
    readonly property var displayModeNames: viewer.lit
        ? ["Shaded", "Shaded + Edges", "Wireframe"]
        : ["Flat", "Flat + Edges", "Wireframe"]

    // Left-drag interaction: false = rotate (orbit), true = pan.
    property bool panMode: false

    // Keyboard shortcut: cycle Shaded → Shaded+Edges → Wireframe.
    Shortcut {
        sequence: "E"
        onActivated: viewer.cycleDisplayMode()
    }

    // R / P: set left-drag to rotate or pan (mirrors the on-screen toggle).
    Shortcut {
        sequence: "R"
        onActivated: root.panMode = false
    }
    Shortcut {
        sequence: "P"
        onActivated: root.panMode = true
    }

    // Space: fit the camera to the model.
    Shortcut {
        sequence: "Space"
        onActivated: viewer.fitToModel()
    }

    // ── 3D Viewport ───────────────────────────────────────────────────────────
    ViewerItem {
        id: viewer
        anchors.fill: parent

        // Left-drag uses the configured mode (root.panMode); right-drag does the
        // opposite. So with the default (rotate): left = rotate, right = pan; if
        // panMode is set to pan, left = pan, right = rotate. Scroll zooms.
        // Handling the wheel inside the MouseArea avoids a separate pointer
        // handler competing with it for the same events.
        MouseArea {
            id: dragArea
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            property real lastX: 0
            property real lastY: 0
            // Which button started the current drag (set on press).
            property int dragButton: Qt.NoButton

            onPressed: (mouse) => {
                dragButton = mouse.button;
                lastX = mouse.x;
                lastY = mouse.y;
            }
            onPositionChanged: (mouse) => {
                // Right button inverts the configured mode.
                var pan = (dragButton === Qt.RightButton) ? !root.panMode
                                                           : root.panMode;
                if (pan)
                    viewer.pan(mouse.x - lastX, mouse.y - lastY);
                else
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
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 12
            text: "Vulkan → VkImage → QSGTexture  |  %1 × %2 px"
                      .arg(viewer.width).arg(viewer.height)
            color: "#cccccc"
            font.pixelSize: 13
            font.family: "monospace"
        }

        // Display-mode selector (mirrors the 'E' shortcut). The highlighted
        // button reflects viewer.displayMode, so shortcut and buttons stay in sync.
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 12
            spacing: 6

            // Left-drag mode toggle: Rotate vs. Pan.
            Rectangle {
                width: dragLabel.implicitWidth + 16
                height: 24
                radius: 4
                color: root.panMode ? "#3a6ea5" : Qt.rgba(1, 1, 1, 0.10)
                border.color: root.panMode ? "#5b9bd5" : "transparent"
                border.width: 1

                Text {
                    id: dragLabel
                    anchors.centerIn: parent
                    text: root.panMode ? "Drag: Pan" : "Drag: Rotate"
                    color: "#e0e0e0"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: root.panMode = !root.panMode
                }
            }

            // Separator before the display-mode buttons.
            Rectangle {
                width: 1
                height: 18
                anchors.verticalCenter: parent.verticalCenter
                color: Qt.rgba(1, 1, 1, 0.25)
            }

            Repeater {
                model: root.displayModeNames
                Rectangle {
                    width: label.implicitWidth + 16
                    height: 24
                    radius: 4
                    color: viewer.displayMode === index ? "#3a6ea5" : Qt.rgba(1, 1, 1, 0.10)
                    border.color: viewer.displayMode === index ? "#5b9bd5" : "transparent"
                    border.width: 1

                    Text {
                        id: label
                        anchors.centerIn: parent
                        text: modelData
                        color: "#e0e0e0"
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: viewer.displayMode = index
                    }
                }
            }

            // Thin separator before the lighting toggle.
            Rectangle {
                width: 1
                height: 18
                anchors.verticalCenter: parent.verticalCenter
                color: Qt.rgba(1, 1, 1, 0.25)
            }

            // Lighting on/off toggle.
            Rectangle {
                width: litLabel.implicitWidth + 16
                height: 24
                radius: 4
                color: viewer.lit ? "#3a6ea5" : Qt.rgba(1, 1, 1, 0.10)
                border.color: viewer.lit ? "#5b9bd5" : "transparent"
                border.width: 1

                Text {
                    id: litLabel
                    anchors.centerIn: parent
                    text: viewer.lit ? "Shading: On" : "Shading: Off"
                    color: "#e0e0e0"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: viewer.lit = !viewer.lit
                }
            }
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
