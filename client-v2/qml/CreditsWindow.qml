import QtQuick
import "LegacyContent.js" as Copy

LegacyDialogWindow {
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
        Repeater {
            model: Copy.credits
            Item {
                required property var modelData
                required property int index
                x: 29; y: 117 + index * 39; width: parent.width - 58; height: 19
                LegacyText { text: modelData[0]; color: Theme.color("#627da6"); font.pixelSize: 14 }
                LegacyText { anchors.right: parent.right; text: modelData[1]; font.weight: Font.DemiBold; font.pixelSize: 14 }
            }
        }
        Rectangle { x: 29; y: 313; width: parent.width - 58; height: 1; color: Theme.color("#c2dcee") }
    }
}
