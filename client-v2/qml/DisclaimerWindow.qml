import QtQuick
import QtQuick.Controls
import "LegacyContent.js" as Copy

LegacyDialogWindow {
    id: dialog
    objectName: "disclaimerWindow"; dock: "left"
    panelWidth: 780; panelHeight: 735
    title: "Дисклеймер · RuTool Installer"
    wash: Theme.color("#b9eaf5ff"); headerMargin: 26; brandX: 46; brandSize: 30; captionX: 162
    caption: "/  I N S T A L L E R"; closeButton: !mandatory; canDismiss: !mandatory
    wedge: "M0,0 L292,0 326,63 0,63 Z"
    property bool mandatory: false
    property bool acknowledged: false
    signal agreementRequested()
    signal consentGiven()
    function showMandatory() { mandatory = true; agreement.checked = false; show() }
    readonly property int paragraphCount: Copy.disclaimer.length
    LegacySurface {
        x: 26; y: 75; width: parent.width - 52; height: parent.height - 100
        firstColor: Theme.color("#f9ffffff"); lastColor: Theme.color("#eef2faff"); edgeColor: Theme.color("#91c6f1"); radius: 11
        LegacyText { y: 24; width: parent.width; text: "Дисклеймер"; font.pixelSize: 26; font.bold: true; horizontalAlignment: Text.AlignHCenter }
        Rectangle { x: 198; y: 74; width: parent.width - 396; height: 1; color: Theme.color("#8ecbf2") }
        Flickable {
            id: bodyScroll; objectName: "disclaimerScroll"
            x: 33; y: 90; width: parent.width - 66; height: parent.height - (dialog.mandatory ? 246 : 212)
            contentWidth: width
            contentHeight: paragraphs.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: LegacyScrollBar { objectName: "disclaimerScrollBar"; anchors.right: bodyScroll.right }
            Column {
                id: paragraphs
                width: bodyScroll.width - 16; spacing: 13
                Repeater {
                    model: Copy.disclaimer
                    Item {
                        required property string modelData
                        required property int index
                        objectName: "disclaimerParagraph" + index
                        width: parent.width; height: Math.max(23, paragraph.implicitHeight + 1)
                        LegacyText {
                            id: paragraph; objectName: "disclaimerText" + parent.index; y: -2; width: parent.width; text: parent.modelData; textFormat: Text.RichText
                            color: Theme.color("#254466"); font.pixelSize: 15; lineHeightMode: Text.FixedHeight; lineHeight: 23
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
        LegacyButton {
            objectName: "sourceArchiveLink"; x: parent.width - width - 33; y: parent.height - (dialog.mandatory ? 150 : 122)
            width: 160; text: "Исходный код ↗"; link: true; font.pixelSize: 14
            onClicked: Qt.openUrlExternally("https://github.com/shiroshy/RuTool")
        }
        LegacyButton {
            objectName: "agreementLink"; x: 33; y: parent.height - (dialog.mandatory ? 150 : 122)
            text: "Открыть пользовательское соглашение"; link: true; font.pixelSize: 14
            onClicked: dialog.agreementRequested()
        }
        CheckBox {
            id: agreement; objectName: "agreementConsent"; visible: dialog.mandatory
            x: 33; y: parent.height - 120; width: parent.width - 66; height: 32
            text: "Я соглашаюсь с пользовательским соглашением"
            indicator: Rectangle { x: 0; y: (agreement.height-height)/2; width: 20; height: 20; radius: 4; color: Theme.card; border.color: "#159cfa"
                LegacyText { anchors.centerIn: parent; text: agreement.checked ? "✓" : ""; color: "#159cfa"; font.bold: true }
            }
            contentItem: LegacyText { text: agreement.text; leftPadding: 30; verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap; font.pixelSize: 14 }
        }
        LegacyButton {
            objectName: "disclaimerContinue"; enabled: !dialog.mandatory || agreement.checked
            x: (parent.width - width) / 2; y: parent.height - 75; width: 240; height: 49
            text: "Понятно, продолжить"; primary: true; font.pixelSize: 16; font.bold: true
            primaryEdge: Theme.color("#69cbf7"); primaryEdgeWidth: 1; hoverOpacity: 0.88; pressedOpacity: 0.68
            onClicked: { if (dialog.mandatory) dialog.consentGiven(); dialog.acknowledged = true; dialog.mandatory = false; dialog.close() }
        }
        LegacyButton {
            x: 33; y: parent.height - 69; height: 37; text: "Выйти"; link: true; visible: dialog.mandatory
            onClicked: dialog.transientParent.close()
        }
    }
}
