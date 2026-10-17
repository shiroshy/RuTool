import QtQuick
import QtQuick.Controls

Button {
    id: button
    property bool primary: false
    property bool link: false
    property bool chrome: false
    property color normalEdge: Theme.color("#91b4e5")
    property color primaryEdge: "transparent"
    property real primaryEdgeWidth: 0
    property color surfaceFirst: Theme.color("#f5ffffff")
    property color surfaceLast: Theme.color("#eef3f9ff")
    property real primaryGradientY: 0.7
    property real hoverOpacity: 0.84
    property real pressedOpacity: 0.65
    property color ink: enabled ? (primary ? "white" : link ? (hovered ? Theme.color("#1464b9") : Theme.color("#078fe9")) : Theme.color("#183764")) : Theme.color("#748ca5")
    property string hint: ""
    font.family: "Segoe UI"
    font.pixelSize: 15
    font.underline: link
    padding: chrome ? 0 : 8
    leftPadding: chrome ? 0 : link ? 9 : 16
    rightPadding: leftPadding
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    focusPolicy: Qt.StrongFocus
    opacity: enabled && !link ? (down ? pressedOpacity : hovered ? hoverOpacity : 1) : 1
    hoverEnabled: enabled && visible
    HoverHandler { enabled: button.enabled && button.visible; cursorShape: Qt.PointingHandCursor }
    ToolTip.visible: enabled && visible && hovered && hint.length > 0
    ToolTip.text: hint
    contentItem: LegacyText {
        text: button.text; color: button.ink; font: button.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: LegacySurface {
        radius: 7
        visible: !button.link
        firstColor: button.chrome ? "transparent" : !button.enabled ? Theme.color("#e4eef8") : button.primary ? Theme.color("#19ddec") : button.surfaceFirst
        lastColor: button.chrome ? "transparent" : !button.enabled ? Theme.color("#e4eef8") : button.primary ? Theme.color("#00a3f7") : button.surfaceLast
        gradientY: button.primary ? button.primaryGradientY : 1
        edgeColor: button.chrome ? "transparent" : !button.enabled ? Theme.color("#b7cbdd") : button.hovered ? Theme.color("#159cfa") : button.primary ? button.primaryEdge : button.normalEdge
        edgeWidth: button.chrome ? 0 : button.primary ? button.primaryEdgeWidth : 1
    }
}
