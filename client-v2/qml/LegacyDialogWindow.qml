import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
Item {
    id: dialog
    property var transientParent
    parent: transientParent ? transientParent.contentItem : null
    property real panelWidth: 780
    property real panelHeight: 735
    width: Math.min(panelWidth, parent ? parent.width - 32 : panelWidth)
    height: Math.min(panelHeight, parent ? parent.height - 32 : panelHeight)
    property string dock: "center"
    property string title: ""
    property bool opened: false
    property bool canDismiss: true
    property var returnFocus: null
    property real progress: opened ? 1 : 0
    property bool animating: motion.running
    Behavior on progress { NumberAnimation { id: motion; duration: 320; easing.type: Easing.OutCubic } }
    visible: opened || progress > 0
    enabled: opened
    z: 40
    clip: true
    readonly property real finalX: !parent ? 0 : dock === "left" ? 16 : dock === "right" ? parent.width - width - 16 : (parent.width - width) / 2
    readonly property real finalY: !parent ? 0 : dock === "center" ? (parent.height - height) / 2 : parent.height - height - 16
    x: finalX + (1-progress) * (dock === "left" ? -width-16 : dock === "right" ? width+16 : 0)
    y: finalY + (1-progress) * (parent ? parent.height - finalY : height)
    function show() { if (!opened && transientParent) returnFocus = transientParent.activeFocusItem; opened = true; forceActiveFocus() }
    function close() { if (canDismiss) opened = false }
    onVisibleChanged: {
        if (!visible) Qt.callLater(function() { if (!dialog.visible && dialog.returnFocus) dialog.returnFocus.forceActiveFocus() })
    }
    Keys.onEscapePressed: close()
    Rectangle {
        parent: dialog.parent; anchors.fill: parent; z: dialog.z - 1
        visible: dialog.visible; opacity: dialog.progress * 0.32; color: "#071a30"
        HoverHandler { enabled: dialog.visible; blocking: true; cursorShape: Qt.ArrowCursor }
        MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: dialog.close() }
    }
    MouseArea { anchors.fill: parent; hoverEnabled: true }
    ThemedBackground { anchors.fill: parent; anchors.margins: 1 }
    Rectangle { anchors.fill: parent; anchors.margins: 1; color: dialog.wash }
    Rectangle { anchors.fill: parent; color: "transparent"; border.color: Theme.color("#91bfe8") }
    property color wash: Theme.color("#bceaf5ff")
    property real headerMargin: 25
    property string caption: ""
    property string wedge: "M0,0 L260,0 294,60 0,60 Z"
    property bool stripes: true
    property bool closeButton: true
    property bool plainClose: false
    property real brandX: 47
    property real brandSize: 29
    property real captionX: 160
    property real captionY: 32
    Item {
        x: dialog.headerMargin; y: 1; width: dialog.width - 2 * dialog.headerMargin; height: 74
        Shape {
            width: 326; height: 63; preferredRendererType: Shape.CurveRenderer
            ShapePath { strokeWidth: 0; strokeColor: "transparent"; fillColor: Theme.color("#e8ffffff"); PathSvg { path: dialog.wedge } }
        }
        Shape {
            x: 8; y: 20; width: 36; height: 18; visible: dialog.stripes
            preferredRendererType: Shape.CurveRenderer
            ShapePath { strokeWidth: 0; strokeColor: "transparent"; fillColor: Theme.color("#69cfff"); PathSvg { path: "M0,18 L12,0 21,0 9,18 Z M15,18 L27,0 36,0 24,18 Z" } }
        }
        LegacyText { x: dialog.brandX + (dialog.stripes ? 6 : 0); y: 16; text: "RuTool"; font.pixelSize: dialog.brandSize; font.italic: true; font.weight: Font.Black }
        LegacyText { x: dialog.captionX; y: dialog.captionY; width: parent.width - x - 48; elide: Text.ElideRight; text: dialog.caption; font.pixelSize: 13; font.weight: Font.DemiBold; color: Theme.color("#1595e7") }
        LegacyButton {
            x: parent.width - width; y: 12; width: 38; height: 36; chrome: dialog.plainClose
            surfaceFirst: Theme.color("#f8ffffff"); surfaceLast: Theme.color("#eef1f9ff"); normalEdge: Theme.color("#9bc6eb")
            visible: dialog.closeButton; hint: "Закрыть"; onClicked: dialog.close()
            contentItem: Item {
                LegacyIcon { visible: !dialog.plainClose; anchors.centerIn: parent; kind: "close"; width: 12; height: 12; stroke: Theme.color("#173664") }
                LegacyText { visible: dialog.plainClose; text: "×"; font.pixelSize: 27; color: Theme.color("#168fe0"); anchors.centerIn: parent }
            }
        }
    }
}
