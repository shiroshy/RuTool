import QtQuick
import QtQuick.Controls

TextField {
    id: field
    property string icon: ""
    font.family: "Segoe UI"; font.pixelSize: 16
    color: Theme.color("#142f5b"); selectionColor: Theme.color("#80bffa")
    placeholderTextColor: Theme.color("#7f95ae")
    leftPadding: icon.length > 0 ? 44 : 12; rightPadding: 12
    topPadding: 9; bottomPadding: 9
    selectByMouse: true
    background: Rectangle {
        radius: 6; color: Theme.color("#70f1f7ff")
        border.color: field.activeFocus ? Theme.color("#229bfa") : Theme.color("#b9cfef")
        opacity: field.enabled ? 1 : 0.65
    }
    LegacyIcon { visible: field.icon.length > 0; kind: field.icon || "file"; x: 16; anchors.verticalCenter: parent.verticalCenter; width: 18; height: field.icon === "folder" ? 18 : 20; stroke: Theme.color("#6388bf"); fill: field.icon === "folder" ? Theme.color("#6388bf") : "transparent" }
}
