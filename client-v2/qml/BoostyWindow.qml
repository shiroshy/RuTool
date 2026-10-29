import QtQuick
import QtQuick.Controls

LegacyDialogWindow {
    id: dialog
    objectName: "boostyWindow"
    panelWidth: 650; panelHeight: 370; z: 60
    title: "Код бусти · RuTool"; caption: "/  КОД БУСТИ"
    onOpenedChanged: if (opened) Qt.callLater(function() { code.forceActiveFocus() })
    LegacySurface {
        x: 25; y: 75; width: dialog.width - 50; height: dialog.height - 100
        LegacyText { x: 24; y: 20; text: "Доступ к Premium"; font.pixelSize: 23; font.bold: true }
        LegacyText { x: 24; y: 58; width: parent.width - 48; text: "Введите код подписчика бусти для тестовых переводов."; font.pixelSize: 14; color: Theme.muted; wrapMode: Text.WordWrap }
        LegacyField {
            id: code; objectName: "boostyCode"; x: 24; y: 92; width: parent.width - 48; height: 42
            placeholderText: "BST-…"; maximumLength: 80; text: client.boostyCode
            enabled: !client.busy; echoMode: TextInput.PasswordEchoOnEdit
            onTextEdited: client.boostyCode = text
            onAccepted: client.verifyBoostyCode()
            Accessible.name: "Код бусти"
        }
        LegacyText {
            objectName: "boostyStatus"; x: 24; y: 147; width: parent.width - 48; height: 52; font.pixelSize: 14
            text: client.checkingPremium ? "Проверяем код бусти…" : client.premiumStatus
            wrapMode: Text.WordWrap; color: client.premiumActive ? Theme.color("#159d50") : Theme.muted
        }
        Row {
            anchors.left: parent.left; anchors.leftMargin: 24; anchors.bottom: parent.bottom; anchors.bottomMargin: 20; spacing: 10
            LegacyButton { objectName: "clearBoostyCode"; text: "Убрать код"; enabled: !client.busy && client.boostyCode.length > 0; onClicked: client.boostyCode = "" }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 24; anchors.bottom: parent.bottom; anchors.bottomMargin: 20; spacing: 10
            LegacyButton { objectName: "verifyBoostyCode"; text: "Проверить"; primary: true; enabled: !client.busy && !client.checkingPremium && client.boostyCode.length > 0; onClicked: client.verifyBoostyCode() }
            LegacyButton { text: "Закрыть"; onClicked: dialog.close() }
        }
    }
}
