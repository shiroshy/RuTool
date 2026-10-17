import QtQuick
import QtQuick.Controls
LegacyDialogWindow {
    id: panel
    panelWidth: 780; panelHeight: 735
    property string heading: ""
    property string body: ""
    title: heading; caption: "/  " + heading
    LegacySurface {
        x: 25; y: 75; width: panel.width - 50; height: panel.height - 100
        firstColor: Theme.card; lastColor: Theme.cardEnd; edgeColor: Theme.border
        LegacyText { x: 26; y: 20; width: parent.width - 52; text: panel.heading; font.pixelSize: 24; font.bold: true; wrapMode: Text.WordWrap }
        Flickable {
            id: scroll; x: 26; y: 80; width: parent.width - 52; height: parent.height - 110
            clip: true; contentWidth: width; contentHeight: copy.implicitHeight; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: LegacyScrollBar { anchors.right: scroll.right }
            TextEdit { id: copy; width: scroll.width - 16; text: panel.body; readOnly: true; selectByMouse: true; textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; color: Theme.text; font.family: "Segoe UI"; font.pixelSize: 15 }
        }
    }
}
