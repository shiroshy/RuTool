import QtQuick
import QtQuick.Controls

LegacyDialogWindow {
    id: panel
    property var entries: []
    objectName: "creditsWindow"; dock: "center"
    panelWidth: 490; panelHeight: 470
    title: "Credits · RuTool"
    wash: Theme.color("#c3eaf5ff"); headerMargin: 23; brandX: 24; captionX: 125
    caption: "/  CREDITS"; stripes: false; plainClose: true; wedge: "M0,0 L243,0 276,60 0,60 Z"
    LegacySurface {
        x: 23; y: 76; width: parent.width - 46; height: parent.height - 99
        firstColor: Theme.color("#f8ffffff"); lastColor: Theme.color("#edf1f9ff"); edgeColor: Theme.color("#97c7f0")
        LegacyText { x: 29; y: 24; text: "Команда RuTool"; font.pixelSize: 23; font.bold: true }
        LegacyText { x: 29; y: 59; text: "Люди, работавшие над проектом"; font.pixelSize: 14; color: Theme.color("#637fa4") }
        Rectangle { x: 29; y: 97; width: parent.width - 58; height: 1; color: Theme.color("#c2dcee") }
        Flickable {
            id: scroll; objectName: "creditsScroll"
            x: 29; y: 117; width: parent.width - 58; height: Math.max(0, parent.height - y - 34)
            clip: true; contentWidth: width; contentHeight: people.implicitHeight; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: LegacyScrollBar { anchors.right: scroll.right }
            Column {
                id: people; width: scroll.width - 12; spacing: 20
                Repeater {
                    model: panel.entries
                    Item {
                        required property var modelData
                        width: people.width; height: Math.max(role.implicitHeight, name.implicitHeight)
                        LegacyText { id: role; width: parent.width * 0.52; text: modelData.role; wrapMode: Text.Wrap; color: Theme.color("#627da6"); font.pixelSize: 14 }
                        LegacyText { id: name; anchors.right: parent.right; width: parent.width * 0.43; text: modelData.name; wrapMode: Text.Wrap; horizontalAlignment: Text.AlignRight; font.weight: Font.DemiBold; font.pixelSize: 14 }
                    }
                }
                LegacyText { visible: panel.entries.length === 0; text: "Список участников пока пуст"; width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 14; color: Theme.color("#637fa4") }
            }
        }
        Rectangle { x: 29; y: parent.height - 24; width: parent.width - 58; height: 1; color: Theme.color("#c2dcee") }
    }
}
