import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

LegacyDialogWindow {
    id: dialog
    objectName: "translationsWindow"
    dock: "right"
    backdropOpacity: 0.24
    panelWidth: 530
    title: "Скачать перевод · RuTool"
    caption: "Скачать перевод"
    wash: Theme.color("#f6faff")
    property var primaryRelease: null
    property var releases: []
    property bool loadingScreensAvailable: false
    property bool gameReady: false
    property bool mainServerAvailable: client.premiumMainAvailable
    readonly property var orderedReleases: {
        var additional = releases.slice().sort(function(a, b) { return String(b.publishedAt).localeCompare(String(a.publishedAt)) })
        return primaryRelease ? [primaryRelease].concat(additional.filter(function(r) { return r.id !== primaryRelease.id })) : additional
    }
    signal manualRequested()
    signal downloadRequested(string releaseId)
    signal codeRequested()
    signal loadingScreensRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 26
        anchors.topMargin: 82
        spacing: 12

        LegacyText {
            Layout.fillWidth: true
            text: "После скачивания RuTool проверит пакет и предложит установить его."
            wrapMode: Text.WordWrap
            font.pixelSize: 14
            color: Theme.muted
        }
        Flickable {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: cards.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: LegacyScrollBar { anchors.right: list.right }

            Column {
                id: cards
                width: list.width - 12
                spacing: 12
                LegacySurface {
                    visible: !dialog.primaryRelease
                    width: cards.width
                    height: unavailable.implicitHeight + 34
                    firstColor: Theme.card; lastColor: Theme.cardEnd
                    Column {
                        id: unavailable
                        x: 17; y: 17
                        width: parent.width - 34
                        spacing: 6
                        LegacyText { text: "Основной перевод"; font.pixelSize: 18; font.bold: true }
                        LegacyText {
                            width: parent.width
                            text: "Основной перевод пока недоступен в каталоге. Если у вас есть файл .rut, выберите его вручную."
                            color: Theme.muted
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                        }
                    }
                }
                Repeater {
                    model: dialog.orderedReleases
                    LegacySurface {
                        id: card
                        required property var modelData
                        readonly property bool mainRelease: dialog.primaryRelease !== null && modelData.id === dialog.primaryRelease.id
                        width: cards.width
                        height: releaseDetails.implicitHeight + 34
                        edgeColor: mainRelease ? Theme.color("#299ffc") : Theme.border

                        ColumnLayout {
                            id: releaseDetails
                            x: 17; y: 17
                            width: parent.width - 34
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                LegacyText {
                                    Layout.fillWidth: true
                                    text: card.mainRelease ? "Основной перевод" : card.modelData.premium ? "Тестовая сборка" : "Перевод"
                                    color: Theme.color("#2485cc")
                                    font.pixelSize: 13
                                    font.bold: true
                                }
                                Rectangle {
                                    objectName: "premiumBadge-" + card.modelData.id
                                    visible: card.modelData.premium === true
                                    Layout.preferredWidth: 78; Layout.preferredHeight: 24
                                    radius: 12
                                    color: "#fff1c8"
                                    border.color: "#d7a644"
                                    LegacyText { anchors.centerIn: parent; text: "Premium"; font.pixelSize: 12; font.bold: true; color: "#805810" }
                                }
                            }
                            LegacyText {
                                Layout.fillWidth: true
                                text: card.modelData.name
                                font.pixelSize: 19
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            LegacyText {
                                Layout.fillWidth: true
                                text: "Автор: " + card.modelData.author + "  ·  " + card.modelData.publishedAt
                                font.pixelSize: 13
                                color: Theme.muted
                                wrapMode: Text.Wrap
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                LegacyButton {
                                    objectName: "translationDownload-" + card.modelData.id
                                    text: card.modelData.premium && !client.premiumActive ? "Ввести код бусти" : card.mainRelease ? "Скачать перевод" : "Скачать"
                                    primary: true
                                    enabled: !client.busy
                                    hint: card.mainRelease ? "Скачать с GitHub" : ""
                                    onClicked: {
                                        if (card.modelData.premium && !client.premiumActive) dialog.codeRequested()
                                        else dialog.downloadRequested(card.modelData.id)
                                    }
                                }
                                LegacyButton {
                                    objectName: "translationMainServer"
                                    visible: card.mainRelease && dialog.mainServerAvailable
                                    text: "С сервера · Premium"
                                    enabled: !client.busy
                                    onClicked: {
                                        if (!client.premiumActive) dialog.codeRequested()
                                        else dialog.downloadRequested("main")
                                    }
                                }
                            }
                        }
                    }
                }
                LegacySurface {
                    visible: dialog.loadingScreensAvailable
                    width: cards.width
                    height: addonDetails.implicitHeight + 34

                    ColumnLayout {
                        id: addonDetails
                        x: 17; y: 17
                        width: parent.width - 34
                        spacing: 8
                        LegacyText {
                            text: "Дополнение · Premium"
                            color: Theme.color("#2485cc")
                            font.pixelSize: 13
                            font.bold: true
                        }
                        LegacyText { text: "Русские экраны загрузки"; font.pixelSize: 19; font.bold: true }
                        LegacyText {
                            Layout.fillWidth: true
                            text: dialog.gameReady ? "Устанавливается отдельно от основного перевода." : "Для установки укажите папку игры в настройках."
                            wrapMode: Text.WordWrap
                            color: Theme.muted
                            font.pixelSize: 13
                        }
                        LegacyButton {
                            objectName: "loadingScreensAction"
                            text: client.premiumActive ? "Скачать и установить" : "Ввести код бусти"
                            enabled: !client.busy && (!client.premiumActive || dialog.gameReady)
                            primary: true
                            onClicked: {
                                if (client.premiumActive) dialog.loadingScreensRequested()
                                else dialog.codeRequested()
                            }
                        }
                    }
                }
            }
        }
        LegacyButton {
            Layout.alignment: Qt.AlignLeft
            text: "Выбрать файл .rut"
            enabled: !client.busy
            onClicked: { dialog.close(); dialog.manualRequested() }
        }
    }
}
