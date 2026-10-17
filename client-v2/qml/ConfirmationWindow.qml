import QtQuick
import QtQuick.Controls

LegacyDialogWindow {
    id: dialog
    objectName: "confirmationWindow"
    panelWidth: 710; panelHeight: 410
    caption: "/  ПОДТВЕРЖДЕНИЕ"
    property string token: ""
    property string heading: ""
    property string message: ""
    onOpenedChanged: if (!opened && token.length) client.confirmAction(token, false)
    LegacySurface {
        x: 25; y: 75; width: dialog.width - 50; height: dialog.height - 100
        LegacyText { x: 24; y: 22; width: parent.width - 48; text: dialog.heading; font.pixelSize: 22; font.bold: true; wrapMode: Text.WordWrap }
        Flickable {
            x: 24; y: 64; width: parent.width - 48; height: parent.height - 133; clip: true
            contentHeight: detail.implicitHeight; boundsBehavior: Flickable.StopAtBounds
            LegacyText { id: detail; width: parent.width; text: dialog.message; font.pixelSize: 15; wrapMode: Text.WordWrap }
            ScrollBar.vertical: LegacyScrollBar {}
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 24; anchors.bottom: parent.bottom; anchors.bottomMargin: 19; spacing: 12
            LegacyButton { text: "Отмена"; onClicked: dialog.close() }
            LegacyButton { objectName: "confirmProceed"; text: "Продолжить"; primary: true; onClicked: { client.confirmAction(dialog.token, true); dialog.close() } }
        }
    }
}
