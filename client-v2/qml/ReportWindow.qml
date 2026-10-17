import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import "LegacyContent.js" as Copy

LegacyDialogWindow {
    id: dialog
    objectName: "reportWindow"; dock: "right"
    panelWidth: 650; panelHeight: 775
    title: "Сообщить о проблеме · RuTool"
    headerMargin: 25; brandX: 47; captionX: 160
    caption: "/  СООБЩИТЬ О ПРОБЛЕМЕ"
    property var attachments: []
    property string feedback: ""
    property bool diagnosticSelection: false
    signal exportLogRequested()
    function addDiagnostic(url) {
        addFiles([url], true)
    }
    function addFiles(urls, diagnostic) {
        if (client.checkingAttachments) return
        diagnosticSelection = diagnostic
        client.checkReportAttachments(attachments.concat(Array.from(urls)))
    }
    Connections {
        target: client
        function onReportAttachmentsChecked(urls, ok, message) {
            if (ok) dialog.attachments = Array.from(urls)
            dialog.feedback = ok ? (dialog.diagnosticSelection ? "Диагностический лог прикреплён." : "Вложения добавлены.") : message
        }
        function onReportSent(accepted, text) { dialog.feedback = text; if (accepted) { message.clear(); dialog.attachments = [] } }
    }
    FileDialog {
        id: files; parentWindow: dialog.transientParent; title: "Добавить вложения"; fileMode: FileDialog.OpenFiles
        nameFilters: ["Изображения и логи (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.tif *.tiff *.txt)"]
        onAccepted: {
            dialog.addFiles(selectedFiles, false)
        }
    }
    LegacySurface {
        id: panel; x: 25; y: 75; width: dialog.width - 50; height: dialog.height - 98
        firstColor: Theme.color("#f8ffffff"); lastColor: Theme.color("#eef1f9ff"); edgeColor: Theme.color("#91c6f1"); radius: 11
        Flickable {
            id: formScroll; objectName: "reportFormScroll"
            x: 28; y: 24; width: panel.width - 56; height: panel.height - 48
            contentWidth: width; contentHeight: form.height; clip: true; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: LegacyScrollBar { anchors.right: formScroll.right }
            Item {
            id: form; width: formScroll.width - 16; height: Math.max(629, formScroll.height)
            LegacyText { text: "Расскажите, что случилось"; font.pixelSize: 22; font.bold: true }
            LegacyText { y: 34; width: parent.width; text: "Опишите ошибку и при необходимости приложите скриншоты."; font.pixelSize: 14; color: Theme.color("#637fa4"); wrapMode: Text.WordWrap }
            LegacyText { y: 69; text: "Выберите тему"; font.pixelSize: 14; font.weight: Font.DemiBold }
            ComboBox {
                id: topic; objectName: "reportTopic"; y: 96; width: parent.width; height: 40
                currentIndex: -1; model: Copy.reportTopics
                font.family: "Segoe UI"; font.pixelSize: 14
                leftPadding: 12; rightPadding: 35
                background: Rectangle { radius: 6; color: Theme.color("#dff8fcff"); border.color: Theme.color("#a9c9e9") }
                contentItem: LegacyText { text: topic.displayText; font: topic.font; color: Theme.color("#15345d"); verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                indicator: LegacyText { x: topic.width - 27; anchors.verticalCenter: parent.verticalCenter; text: "⌄"; color: Theme.color("#527ca6"); font.pixelSize: 19 }
                delegate: ItemDelegate { width: topic.width; text: modelData; font: topic.font; highlighted: topic.highlightedIndex === index; contentItem: LegacyText { text: modelData; font: topic.font; color: Theme.color("#15345d") } background: Rectangle { color: parent.highlighted ? Theme.color("#e0f3ff") : Theme.color("#f7fcff") } }
                popup: Popup { y: topic.height; width: topic.width; padding: 2; implicitHeight: Math.min(contentItem.implicitHeight + 4, 250); contentItem: ListView { clip: true; implicitHeight: contentHeight; model: topic.popup.visible ? topic.delegateModel : null; currentIndex: topic.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} } background: Rectangle { radius: 6; color: Theme.color("#f7fcff"); border.color: Theme.color("#9bc6eb") } }
            }
            LegacyText { y: 153; text: "Текст сообщения"; font.pixelSize: 14; font.weight: Font.DemiBold }
            LegacyText { y: 153; anchors.right: parent.right; text: message.length + " / 400"; color: Theme.color("#6582aa"); font.pixelSize: 14 }
            ScrollView {
                id: messageScroll; objectName: "reportMessageScroll"
                y: 180; width: parent.width; height: 128
                contentWidth: width
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical: LegacyScrollBar { objectName: "reportMessageScrollBar"; parent: messageScroll; anchors.right: messageScroll.right; y: 0; height: messageScroll.height }
                background: Rectangle { color: Theme.color("#dff8fcff"); border.color: Theme.color("#a9c9e9") }
                TextArea {
                    id: message; objectName: "reportText"; font.family: "Segoe UI"; font.pixelSize: 15; color: Theme.color("#15345d")
                    width: messageScroll.width
                    padding: 12; rightPadding: 24; wrapMode: TextEdit.Wrap; selectByMouse: true
                    text: ""
                    onTextChanged: if (length > 400) remove(400, length)
                    background: null
                }
            }
            LegacyText { y: 325; text: "Контакт для ответа"; font.pixelSize: 14; font.weight: Font.DemiBold }
            LegacyField { id: contact; y: 352; width: parent.width; height: 40; maximumLength: 64; placeholderText: "Желательно ваш тг юз"; font.pixelSize: 15; background: Rectangle { color: Theme.color("#dff8fcff"); border.color: Theme.color("#a9c9e9") } }
            LegacyText { y: 421; text: "Вложения"; font.pixelSize: 14; font.weight: Font.DemiBold }
            LegacyButton { y: 412; x: parent.width - 290; text: "+ Лог ошибки"; font.pixelSize: 14; height: 37; leftPadding: 12; rightPadding: 12; enabled: !client.exportingLog && !client.checkingAttachments; onClicked: dialog.exportLogRequested() }
            LegacyButton { y: 412; anchors.right: parent.right; text: "+ Добавить файлы"; font.pixelSize: 14; height: 37; leftPadding: 14; rightPadding: 14; enabled: !client.checkingAttachments; surfaceFirst: Theme.color("#f8ffffff"); surfaceLast: Theme.color("#eef1f9ff"); normalEdge: Theme.color("#9bc6eb"); onClicked: files.open() }
            LegacyText { y: 457; width: parent.width; text: "До 10 файлов, общий размер до 8 МБ · Изображения и TXT-логи"; font.pixelSize: 12; color: Theme.color("#637fa4"); wrapMode: Text.WordWrap }
            Rectangle {
                y: 481; width: parent.width; height: Math.max(67, parent.height - 556); radius: 6; color: Theme.color("#a9f1f8ff"); border.color: Theme.color("#c3ddf1")
                Flickable {
                    id: attachmentScroll
                    anchors.fill: parent; anchors.margins: 8
                    contentWidth: width; contentHeight: attachmentList.implicitHeight
                    clip: true; boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: LegacyScrollBar { anchors.right: attachmentScroll.right }
                    Column {
                        id: attachmentList; width: attachmentScroll.width - 16; spacing: 6
                        Repeater {
                            model: dialog.attachments
                            Row {
                                required property var modelData
                                required property int index
                                width: attachmentList.width; spacing: 6
                                LegacyText { width: parent.width - 26; text: decodeURIComponent(String(parent.modelData)).split('/').pop(); font.pixelSize: 12; elide: Text.ElideRight }
                                LegacyButton { text: "×"; chrome: true; width: 20; height: 20; enabled: !client.checkingAttachments; onClicked: { var next = dialog.attachments.slice(); next.splice(index, 1); dialog.attachments = next } }
                            }
                        }
                    }
                }
            }
            LegacyText { y: parent.height - 76; width: parent.width; text: dialog.feedback; color: Theme.color("#5678a0"); font.pixelSize: 14; wrapMode: Text.WordWrap }
            LegacyButton { anchors.bottom: parent.bottom; x: parent.width - 243; width: 83; height: 37; text: "Закрыть"; font.pixelSize: 14; surfaceFirst: Theme.color("#f8ffffff"); surfaceLast: Theme.color("#eef1f9ff"); normalEdge: Theme.color("#9bc6eb"); onClicked: dialog.close() }
            LegacyButton {
                objectName: "reportSend"; anchors.right: parent.right; anchors.bottom: parent.bottom; width: 148; height: 37
                text: client.sendingReport ? "Отправляем…" : "Отправить"; primary: true; font.pixelSize: 14; font.bold: true
                primaryEdge: Theme.color("#68cfff"); primaryEdgeWidth: 1; primaryGradientY: 1
                enabled: !client.sendingReport && !client.checkingAttachments && topic.currentIndex >= 0 && message.text.trim().length > 0 && client.reportCooldown.length === 0
                hint: client.reportCooldown
                onClicked: { dialog.feedback = "Отправляем репорт…"; client.submitReport(contact.text, message.text, topic.currentText, dialog.attachments) }
            }
        }
        }
    }
}
