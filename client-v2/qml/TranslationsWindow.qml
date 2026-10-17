import QtQuick
import QtQuick.Controls

LegacyDialogWindow {
    id: dialog
    objectName: "translationsWindow"
    panelWidth: 810; panelHeight: 700
    title: "Переводы · RuTool"
    caption: "/  ПЕРЕВОДЫ"
    property var primaryRelease: null
    property var releases: []
    readonly property var orderedReleases: {
        var additional = releases.slice().sort(function(a, b) { return String(b.publishedAt).localeCompare(String(a.publishedAt)) })
        return primaryRelease ? [primaryRelease].concat(additional.filter(function(r) { return r.id !== primaryRelease.id })) : additional
    }
    signal manualRequested()
    signal downloadRequested()
    LegacySurface {
        x: 25; y: 75; width: dialog.width - 50; height: dialog.height - 100
        firstColor: Theme.color("#f8ffffff"); lastColor: Theme.color("#eef1f9ff"); edgeColor: Theme.color("#91c6f1")
        LegacyText { x: 28; y: 24; text: "Выберите перевод"; font.pixelSize: 24; font.bold: true }
        LegacyText { x: 28; y: 61; text: "Основной перевод от Studio всегда находится первым."; color: Theme.muted; font.pixelSize: 14 }
        Flickable {
            id: list; x: 28; y: 98; width: parent.width - 56; height: parent.height - 177
            clip: true; contentWidth: width; contentHeight: cards.implicitHeight; boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: LegacyScrollBar { anchors.right: list.right }
            Column {
                id: cards; width: list.width - 16; spacing: 14
                LegacySurface {
                    visible: !dialog.primaryRelease; width: parent.width; height: visible ? 150 : 0
                    firstColor: Theme.card; lastColor: Theme.cardEnd; edgeColor: Theme.color("#299ffc")
                    LegacyText { x: 22; y: 18; text: "Основной перевод"; font.pixelSize: 20; font.bold: true }
                    LegacyText { x: 22; y: 53; text: "Автор: —     Название: —     Дата загрузки: —"; color: Theme.muted; font.pixelSize: 14 }
                    LegacyText { x: 22; y: 90; width: parent.width - 44; text: "Данные каталога пока недоступны. Можно выбрать готовый файл вручную."; color: Theme.muted; font.pixelSize: 14; wrapMode: Text.WordWrap }
                }
                Repeater {
                    model: dialog.orderedReleases
                    LegacySurface {
                        required property var modelData
                        required property int index
                        width: cards.width; height: 148
                        edgeColor: dialog.primaryRelease && index === 0 ? Theme.color("#299ffc") : Theme.border
                        LegacyText { x: 22; y: 15; text: dialog.primaryRelease && parent.index === 0 ? "ОСНОВНОЙ · STUDIO" : "ПЕРЕВОД"; color: Theme.color("#299ffc"); font.pixelSize: 12; font.bold: true }
                        LegacyText { x: 22; y: 43; width: parent.width - 44; text: parent.modelData.name; font.pixelSize: 20; font.bold: true; elide: Text.ElideRight }
                        LegacyText { x: 22; y: 81; width: parent.width - 44; text: "Автор: " + parent.modelData.author + "    ·    Дата загрузки: " + parent.modelData.publishedAt; font.pixelSize: 14; color: Theme.muted; elide: Text.ElideRight }
                        LegacyButton { x: parent.width - 156; y: 108; width: 133; height: 30; text: "Скачать"; primary: true; font.pixelSize: 13; enabled: !client.busy; onClicked: dialog.downloadRequested() }
                    }
                }
            }
        }
        LegacyButton { x: 28; anchors.bottom: parent.bottom; anchors.bottomMargin: 22; text: "Ручная установка"; font.pixelSize: 14; onClicked: { dialog.close(); dialog.manualRequested() } }
        LegacyButton { anchors.right: parent.right; anchors.rightMargin: 28; anchors.bottom: parent.bottom; anchors.bottomMargin: 22; text: "Закрыть"; font.pixelSize: 14; onClicked: dialog.close() }
    }
}
