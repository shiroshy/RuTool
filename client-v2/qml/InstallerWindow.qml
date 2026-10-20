import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Shapes
import QtQuick.Window

ApplicationWindow {
    id: window
    objectName: "installerWindow"
    width: 1100; height: 825; minimumWidth: 940; minimumHeight: 700
    visible: false
    title: "RuTool Installer"
    flags: Qt.Window | Qt.FramelessWindowHint
    color: Theme.color("#e6f3ff")
    font.family: "Segoe UI"; font.pixelSize: 15
    property string statusOverride: ""
    readonly property bool packagePending: client.packagePending
    readonly property bool packageDownloading: client.packageDownloading
    readonly property bool working: client.busy
    readonly property bool packageError: client.packageError
    readonly property bool packageChosen: client.hasPackage
    property bool attachLogAfterExport: false
    onClosing: function(event) { if (client.busy) { event.accepted = false; client.cancel() } }
    FileDialog {
        id: logPicker; title: "Сохранить подробный лог ошибки"; fileMode: FileDialog.SaveFile
        nameFilters: ["Диагностический лог (*.txt)"]; defaultSuffix: "txt"
        onAccepted: client.exportLog(selectedFile)
        onRejected: window.attachLogAfterExport = false
    }
    function saveErrorLog(attach) {
        attachLogAfterExport = attach
        logPicker.parentWindow = window
        logPicker.currentFile = client.suggestedLogFile()
        logPicker.open()
    }
    Connections {
        target: client
        function onConfirmationRequested(token, heading, message) {
            confirmation.token = token; confirmation.heading = heading; confirmation.message = message; confirmation.show()
        }
        function onLogExported(url, ok, message) {
            window.statusOverride = message
            if (ok && window.attachLogAfterExport) report.addDiagnostic(url)
            window.attachLogAfterExport = false
        }
    }
    FileDialog {
        id: packagePicker; title: "Выберите файл .rut"; nameFilters: ["Пакет RuTool (*.rut)"]
        onAccepted: { window.statusOverride = ""; client.inspectPackage(selectedFile) }
    }
    FolderDialog { id: gamePicker; title: "Выберите папку Blue Archive"; onAccepted: client.chooseGameFolder(selectedFolder) }
    CreditsWindow { id: credits; transientParent: window; entries: client.credits; onOpenedChanged: if (opened) client.refreshContent() }
    DisclaimerWindow { id: disclaimer; transientParent: window; onAgreementRequested: license.show(); onConsentGiven: uiState.acceptAgreement(client.licenseHash) }
    TextContentPanel { id: faq; objectName: "faqPanel"; transientParent: window; heading: "FAQ"; body: client.faqText }
    TextContentPanel { id: license; objectName: "licensePanel"; transientParent: window; heading: "Лицензия Apache-2.0"; body: client.licenseText; z: 60 }
    Connections {
        target: client
        function onContentChanged() {
            if (!uiState.hasAcceptedAgreement(client.licenseHash)) disclaimer.showMandatory()
        }
    }
    ReportWindow { id: report; transientParent: window; onExportLogRequested: window.saveErrorLog(true) }
    TranslationsWindow { id: translations; transientParent: window; primaryRelease: client.primaryRelease.id ? client.primaryRelease : null; onManualRequested: packagePicker.open(); onDownloadRequested: { translations.close(); client.downloadTranslation() } }
    ConfirmationWindow { id: confirmation; transientParent: window; z: 65 }
    Component.onCompleted: {
        uiState.restoreWindow(window)
        visible = true
        if (!uiState.hasAcceptedAgreement(client.licenseHash)) disclaimer.showMandatory()
        client.refreshContent()
        client.start()
    }

    Item {
        id: mainContent; anchors.fill: parent
        enabled: !disclaimer.visible && !report.visible && !credits.visible && !translations.visible && !faq.visible && !license.visible && !confirmation.visible
    ThemedBackground { objectName: "mainBackground"; anchors.fill: parent; anchors.margins: 1 }
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        gradient: Gradient { GradientStop { position: 0; color: Theme.color("#10eaf5ff") } GradientStop { position: 1; color: Theme.color("#88eaf5ff") } }
    }
    Item {
        id: header; x: 1; y: 1; width: window.width - 2; height: 98
        MouseArea {
            anchors.fill: parent
            onPressed: window.startSystemMove()
            onDoubleClicked: window.visibility = window.visibility === Window.Maximized ? Window.Windowed : Window.Maximized
        }
        Shape {
            x: 0; y: 31; width: 714; height: 66
            preferredRendererType: Shape.CurveRenderer
            ShapePath { strokeWidth: 0; strokeColor: "transparent"; fillColor: Theme.color("#dfffffff"); PathSvg { path: "M0,0 L680,0 714,66 0,66 Z" } }
        }
        Shape {
            x: 12; y: 47; width: 43; height: 24
            preferredRendererType: Shape.CurveRenderer
            ShapePath { strokeWidth: 0; strokeColor: "transparent"; fillColor: Theme.color("#7acfff"); PathSvg { path: "M0,24 L16,0 27,0 11,24 Z M16,24 L32,0 43,0 27,24 Z" } }
        }
        Row {
            x: 67; y: 36; height: 56
            LegacyText { text: "RuTool"; font.pixelSize: 42; font.weight: Font.Black; font.italic: true; color: Theme.color("#0c234d") }
            Item { width: 20; height: 1 }
            LegacyText { text: "I N S T A L L E R"; color: Theme.color("#168fff"); font.pixelSize: 15; font.weight: Font.DemiBold; y: 20 }
            Item { width: 23; height: 1 }
            Rectangle { width: 1; height: 32; color: Theme.color("#90adce"); y: 15 }
            Item { width: 23; height: 1 }
            LegacyText {
                objectName: "versionLabel"; text: "v" + client.applicationVersion; color: Theme.color("#4e6998"); y: 21
                HoverHandler { id: versionHover; enabled: mainContent.enabled && mainContent.visible }
                ToolTip.visible: mainContent.enabled && mainContent.visible && versionHover.hovered
                ToolTip.text: "Версия установленной программы"
            }
            Item { width: 20; height: 1 }
            Rectangle { width: 1; height: 22; color: Theme.color("#90adce"); opacity: 0.65; y: 20 }
            Item { width: 20; height: 1 }
            LegacyText { text: "Установок: " + client.installationCount; font.pixelSize: 13; color: Theme.color("#4e6998"); y: 22 }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 10; y: 4
            LegacyButton { objectName: "themeToggle"; width: 44; height: 38; chrome: true; text: Theme.dark ? "☼" : "☾"; font.pixelSize: 21; hint: Theme.dark ? "Светлая тема" : "Тёмная тема"; Accessible.name: hint; onClicked: uiState.darkTheme = !uiState.darkTheme }
            LegacyButton { width: 44; height: 38; chrome: true; text: "−"; font.pixelSize: 22; hint: "Свернуть"; Accessible.name: "Свернуть"; onClicked: window.showMinimized() }
            LegacyButton { width: 44; height: 38; chrome: true; text: "×"; font.pixelSize: 22; hint: "Закрыть"; Accessible.name: "Закрыть"; onClicked: window.close() }
        }
        LegacyButton {
            visible: !!client.applicationUpdateVersion
            enabled: !window.working
            text: "Обновить до " + (client.applicationUpdateVersion || "2.0.0"); font.pixelSize: 13; leftPadding: 12; rightPadding: 12; topPadding: 5; bottomPadding: 5
            onClicked: client.requestApplicationUpdate()
            anchors.right: parent.right; anchors.rightMargin: 24; anchors.bottom: parent.bottom; anchors.bottomMargin: 15
        }
    }
    Flickable {
        id: steps
        objectName: "stepsScroll"
        x: 1; y: 99; width: window.width - 2; height: window.height - 164
        contentWidth: width; contentHeight: 661; clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: LegacyScrollBar { objectName: "stepsScrollBar"; anchors.right: steps.right; anchors.rightMargin: 8 }
        Item {
            id: cards; x: 33; width: steps.width - 66; height: 644
            LegacySurface {
                objectName: "introCard"; width: cards.width; height: 76; edgeColor: Theme.color("#89cdf8"); clip: true
                Rectangle { x: 1; y: 1; width: 5; height: 74; radius: 2; color: Theme.color("#71ddf6") }
                Shape {
                    anchors.right: parent.right; anchors.rightMargin: 32; y: -10; width: 157; height: 100
                    ShapePath { strokeWidth: 0; strokeColor: "transparent"; fillColor: Theme.color("#1f4fbff7"); PathSvg { path: "M0,100 L65,0 95,0 30,100 Z M80,100 L145,0 157,0 92,100 Z" } }
                }
                Rectangle {
                    x: 25; y: 20; width: 36; height: 36; radius: 18; color: Theme.color("#299ffc")
                    LegacyText { text: "i"; color: "white"; font.family: "Georgia"; font.pixelSize: 29; font.bold: true; anchors.centerIn: parent; y: -1 }
                }
                LegacyText { x: 88; y: 13; text: "Установка перевода"; font.pixelSize: 20; font.bold: true }
                Text { x: 88; y: 43; width: parent.width - 110; text: "Выберите папку игры, скачайте перевод или укажите файл .rut и нажмите «Установить перевод»."; color: Theme.color("#637faa"); font.family: "Segoe UI"; font.pointSize: 10.875; renderType: Text.NativeRendering; wrapMode: Text.WordWrap }
            }
            LegacyStepCard {
                objectName: "gameCard"; y: 88; width: cards.width; height: 126; step: "01"
                Item {
                    anchors.fill: parent
                    LegacyIcon { x: 3; y: 10; width: 33; height: 33; kind: "game" }
                    LegacyText { x: 57; y: 0; text: "Папка игры"; font.pixelSize: 20; font.bold: true }
                    LegacyText { x: 57; y: 30; text: "Укажите папку, в которой установлена игра."; color: Theme.color("#637faa") }
                    LegacyText { anchors.right: parent.right; y: 7; text: client.gameState; font.pixelSize: 13; color: client.gameValid ? Theme.color("#159d50") : Theme.color("#637faa") }
                    LegacyField { id: game; objectName: "gamePath"; y: parent.height - 41; width: parent.width - 176; height: 41; icon: "folder"; enabled: !window.working; text: client.gameFolder; onTextEdited: client.gameFolder = text; Accessible.name: "Папка игры" }
                    LegacyButton {
                        objectName: "chooseGame"; width: 162; height: 41; anchors.right: parent.right; anchors.bottom: parent.bottom
                        enabled: !window.working; onClicked: gamePicker.open()
                        contentItem: Item { implicitHeight: gameButtonBody.implicitHeight; Row { id: gameButtonBody; spacing: 13; anchors.centerIn: parent; LegacyIcon { width: 18; height: 18; kind: "folder"; stroke: Theme.color("#547dba"); anchors.verticalCenter: parent.verticalCenter } LegacyText { text: "Обзор…"; color: Theme.color("#183764") } } }
                    }
                }
            }
            LegacyStepCard {
                objectName: "packageCard"; y: 226; width: cards.width; height: 132; step: "02"
                Item {
                    anchors.fill: parent
                    LegacyIcon { x: 5; y: 11; width: 28; height: 32; kind: "file"; stroke: Theme.color("#527bbf") }
                    LegacyText { x: 57; text: "Файл перевода .rut"; font.pixelSize: 20; font.bold: true }
                    LegacyText { x: 57; y: 30; text: "Скачайте перевод или выберите локальный файл .rut."; color: Theme.color("#637faa") }
                    Row {
                        anchors.right: parent.right; spacing: 12
                        LegacyText { y: 7; text: window.packagePending ? window.packageDownloading ? "Скачиваем…" : "Проверяем…" : window.packageError ? "!  Ошибка пакета" : window.packageChosen ? "✓  Файл выбран" : "Файл не выбран"; font.pixelSize: 13; color: window.packagePending ? Theme.color("#637faa") : window.packageError ? Theme.color("#cb495e") : window.packageChosen ? Theme.color("#159d50") : Theme.color("#637faa") }
                        LegacyButton { objectName: "choosePackage"; text: "Ручная установка"; font.pixelSize: 13; height: 30; leftPadding: 10; rightPadding: 10; topPadding: 4; bottomPadding: 4; enabled: !window.working; hint: "Выбрать локальный файл .rut"; onClicked: packagePicker.open() }
                    }
                    LegacyField { objectName: "packagePath"; y: parent.height - 41; width: parent.width - 176; height: 41; icon: "file"; readOnly: true; text: client.packagePath; enabled: !window.working; Accessible.name: "Файл перевода .rut" }
                    LegacyButton {
                        objectName: "serverPackage"; width: 162; height: 41; anchors.right: parent.right; anchors.bottom: parent.bottom
                        text: "Скачать перевод"; primary: true; font.pixelSize: 14
                        enabled: !window.working; onClicked: { client.loadCatalog(); translations.show() }
                    }
                }
            }
            LegacyStepCard {
                objectName: "verificationCard"; y: 370; width: cards.width; height: 194; step: "03"
                Item {
                    anchors.fill: parent
                    LegacyIcon { x: 4; y: 7; width: 28; height: 32; kind: "shield" }
                    LegacyText { x: 57; text: "Проверка пакета"; font.pixelSize: 20; font.bold: true }
                    LegacyText { x: 57; y: 30; text: window.packageDownloading ? "Проверка начнётся после скачивания файла." : "Проверяем целостность файла и подпись автора…"; color: Theme.color("#637faa") }
                    Rectangle {
                        objectName: "trustCard"; y: 53; width: parent.width; height: parent.height - 53; radius: 7
                        color: window.packagePending ? Theme.color("#cceaf6ff") : window.packageError ? Theme.color("#d4fff0f2") : client.officialPackage ? Theme.color("#d4f0fff5") : client.hasPackage ? Theme.color("#d4fff8e8") : Theme.color("#cceaf6ff")
                        border.color: window.packagePending ? Theme.color("#add3f3") : window.packageError ? Theme.color("#47cb495e") : client.officialPackage ? Theme.color("#4727986a") : client.hasPackage ? Theme.color("#47b07718") : Theme.color("#add3f3")
                        readonly property color trustColor: window.packagePending ? Theme.color("#349eea") : window.packageError ? Theme.color("#cb495e") : client.officialPackage ? Theme.color("#27986a") : client.hasPackage ? Theme.color("#b07718") : Theme.color("#349eea")
                        Rectangle {
                            x: 23; anchors.verticalCenter: parent.verticalCenter; width: 64; height: 64; radius: 32; color: "transparent"; border.width: 4; border.color: parent.trustColor
                            LegacyText { anchors.centerIn: parent; anchors.verticalCenterOffset: -2; text: window.packagePending ? "…" : client.officialPackage ? "✓" : window.packageError || client.hasPackage ? "!" : "i"; font.pixelSize: 39; font.weight: Font.DemiBold; color: parent.parent.trustColor }
                        }
                        Item {
                            x: 114; width: (parent.width - 47 - 74) / 1.94 - 33; height: titleText.implicitHeight + trustDetail.implicitHeight + 5; anchors.verticalCenter: parent.verticalCenter
                            LegacyText { id: titleText; objectName: "trustTitle"; width: parent.width; text: window.packagePending ? window.packageDownloading ? "Скачиваем файл…" : "Проверяем пакет…" : window.packageError ? "Ошибка пакета" : client.officialPackage ? "Подпись подтверждена" : client.hasPackage ? "Подпись не подтверждена" : "Ожидаем файл .rut"; color: window.packagePending ? Theme.color("#2485cc") : window.packageError ? Theme.color("#cb495e") : client.officialPackage ? Theme.color("#159d50") : client.hasPackage ? Theme.color("#b07718") : Theme.color("#2485cc"); font.pixelSize: 21; font.bold: true; wrapMode: Text.WordWrap }
                            Text { id: trustDetail; y: titleText.implicitHeight + 5; width: parent.width; font.family: "Segoe UI"; font.pointSize: 10.875; renderType: Text.NativeRendering; color: Theme.color("#4c6686"); text: window.packagePending ? window.packageDownloading ? "Проверка целостности и подписи начнётся после завершения скачивания." : "Файл готов. Проверяем целостность и подпись автора…" : client.hasPackage ? client.packageStatus : "Выберите пакет для проверки подписи."; wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight }
                        }
                        Rectangle { id: trustSeparator; x: 23 + 74 + (parent.width - 47 - 74) / 1.94; y: 15; width: 1; height: parent.height - 30; color: Theme.color("#cbddeb") }
                        Grid {
                            x: trustSeparator.x + 26; anchors.verticalCenter: parent.verticalCenter; columns: 2; columnSpacing: 0; rowSpacing: 6
                            LegacyText { width: 77; text: "Автор:"; color: Theme.color("#637faa"); font.pixelSize: 14 }
                            LegacyText { text: window.packagePending ? "—" : client.packageSigner || (client.hasPackage ? "Не подтверждён" : "—"); font.pixelSize: 14 }
                            LegacyText { width: 77; text: "Название:"; color: Theme.color("#637faa"); font.pixelSize: 14 }
                            LegacyText { width: Math.max(80, cards.width - trustSeparator.x - 206); text: client.hasPackage || window.packagePending ? client.packageName || "Перевод Blue Archive" : "—"; font.pixelSize: 14; elide: Text.ElideRight }
                            LegacyText { width: 77; text: "Тип:"; color: Theme.color("#637faa"); font.pixelSize: 14 }
                            LegacyText { text: window.packagePending ? "—" : window.packageError ? "Ошибочный пакет" : client.packageKind; font.pixelSize: 14 }
                        }
                    }
                }
            }
            Item {
                id: actions; objectName: "actions"; y: 580; width: cards.width; height: 64
                readonly property real available: width - 61 - reportAction.width
                LegacyButton {
                    id: installAction; objectName: "installButton"; width: actions.available * 1.64 / 2.64; height: 64; enabled: client.canInstall; primary: true
                    hint: "Установить проверенный пакет в выбранную папку игры"; onClicked: client.installTranslation()
                    contentItem: Item {
                        implicitHeight: installButtonBody.implicitHeight
                        Row {
                            id: installButtonBody; anchors.centerIn: parent
                            Item { width: 32; height: 58; LegacyIcon { width: 28; height: 28; kind: "download"; stroke: installAction.enabled ? "white" : Theme.color("#748ca5"); anchors.centerIn: parent } }
                            Item { width: installLabel.implicitWidth + 34; height: 58; LegacyText { id: installLabel; x: 17; text: "Установить перевод"; font.pixelSize: 21; font.bold: true; color: installAction.enabled ? "white" : Theme.color("#748ca5"); anchors.verticalCenter: parent.verticalCenter } }
                            Item { width: 25; height: 58; LegacyText { text: "›"; font.pixelSize: 43; color: installAction.enabled ? "white" : Theme.color("#748ca5"); opacity: 0.65; anchors.verticalCenter: parent.verticalCenter; anchors.verticalCenterOffset: -3 } }
                        }
                    }
                }
                LegacyButton {
                    objectName: "restoreButton"; x: actions.available * 1.64 / 2.64 + 14; width: actions.available / 2.64; height: 64; enabled: client.canRestore; onClicked: client.restoreFiles()
                    contentItem: Item { implicitHeight: restoreButtonBody.implicitHeight; Row { id: restoreButtonBody; anchors.centerIn: parent; spacing: 16; LegacyIcon { width: 24; height: 24; kind: "restore"; stroke: Theme.color("#4d70a9"); anchors.verticalCenter: parent.verticalCenter } LegacyText { text: "Восстановить файлы"; font.pixelSize: 16; color: Theme.color("#748ca5"); anchors.verticalCenter: parent.verticalCenter } } }
                }
                Rectangle { x: actions.available + 38; y: 18; width: 1; height: 28; color: Theme.color("#94b4d7") }
                LegacySurface {
                    id: reportAction; x: actions.width - width; y: 10; width: 211; height: 44; radius: 7; firstColor: Theme.color("#c9f5faff"); lastColor: firstColor; edgeColor: Theme.color("#83c4f0")
                    LegacyIcon { x: 9; anchors.verticalCenter: parent.verticalCenter; width: 23; height: 23; kind: "warning"; stroke: Theme.color("#638ab6") }
                    LegacyButton { x: 36; height: parent.height; width: parent.width - 45; text: "Сообщить о проблеме"; link: true; font.pixelSize: 14; onClicked: report.show() }
                }
            }
        }
    }
    Item {
        objectName: "arona"; x: window.width - 198; y: 62 - steps.contentY; width: 145; height: 127; clip: true
        AnimatedImage {
            objectName: "aronaImage"; width: 145; height: 145
            source: "qrc:/assets/Arona.gif"; fillMode: Image.PreserveAspectFit
            playing: true
            Accessible.name: "Анимация Ароны"
        }
    }
    Rectangle {
        id: footer; objectName: "footer"; x: 1; y: window.height - 65; width: window.width - 2; height: 64; color: Theme.color("#dfffffff")
        Rectangle { width: parent.width; height: 1; color: Theme.color("#a5d6f7") }
        LegacyButton {
            objectName: "disclaimerButton"; x: 26; y: 20; width: 23; height: 23; chrome: true; hint: "Открыть дисклеймер"; Accessible.name: "Открыть дисклеймер"; onClicked: { disclaimer.mandatory = false; disclaimer.show() }
            background: Rectangle { radius: 12; color: parent.hovered ? Theme.color("#c7eaf6ff") : "transparent"; border.width: 1.5; border.color: Theme.color("#668abe") }
            contentItem: LegacyText { text: "i"; font.family: "Georgia"; font.bold: true; font.pixelSize: 18; color: Theme.color("#668abe"); horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        }
        LegacyText {
            x: 71; anchors.verticalCenter: parent.verticalCenter; width: footerLinks.x - x - (window.working || client.hasDiagnosticErrors || window.packageError ? 170 : 18); height: Math.min(implicitHeight, 50)
            font.pixelSize: 14; wrapMode: Text.WordWrap; clip: true
            text: window.statusOverride || client.operationStatus || (client.hasPackage ? client.packageStatus : "Готово к работе.")
        }
        LegacyButton {
            visible: !window.working && (client.hasDiagnosticErrors || window.packageError)
            x: footerLinks.x - 167; anchors.verticalCenter: parent.verticalCenter; width: 151; height: 32
            text: client.exportingLog ? "Сохраняем…" : "Сохранить лог"; font.pixelSize: 13
            enabled: !client.exportingLog; onClicked: window.saveErrorLog(false)
        }
        Row {
            visible: window.working; x: footerLinks.x - 170; anchors.verticalCenter: parent.verticalCenter; spacing: 12
            ProgressBar { width: 72; height: 4; indeterminate: true; anchors.verticalCenter: parent.verticalCenter }
            LegacyButton { text: "Отменить"; font.pixelSize: 13; leftPadding: 12; rightPadding: 12; topPadding: 5; bottomPadding: 5; onClicked: client.cancel() }
        }
        Row {
            id: footerLinks; anchors.right: parent.right; anchors.rightMargin: 25; anchors.verticalCenter: parent.verticalCenter
            Rectangle { width: 1; height: 21; color: Theme.color("#a1bedf"); anchors.verticalCenter: parent.verticalCenter }
            Item { width: 12; height: 1 }
            LegacyButton { objectName: "faqButton"; width: 47; text: "FAQ"; link: true; font.pixelSize: 13; onClicked: faq.show() }
            Item { width: 10; height: 1 }
            Rectangle { width: 1; height: 22; color: Theme.color("#a3bde0"); opacity: 0.65; anchors.verticalCenter: parent.verticalCenter }
            Item { width: 10; height: 1 }
            LegacyButton { width: 59; text: "На чай"; link: true; font.pixelSize: 13; onClicked: Qt.openUrlExternally("https://boosty.to/shizukax") }
            Item { width: 10; height: 1 }
            Rectangle { width: 1; height: 22; color: Theme.color("#a3bde0"); opacity: 0.65; anchors.verticalCenter: parent.verticalCenter }
            Item { width: 10; height: 1 }
            LegacyButton { objectName: "creditsButton"; width: 58; text: "Credits"; link: true; font.pixelSize: 13; onClicked: credits.show() }
            Item { width: 14; height: 1 }
            Rectangle { width: 1; height: 19; color: Theme.color("#c3d6ed"); anchors.verticalCenter: parent.verticalCenter }
            Item { width: 14; height: 1 }
            LegacyButton { width: 83; text: "Telegram ↗"; link: true; font.pixelSize: 13; onClicked: Qt.openUrlExternally("https://t.me/bluearchive_ru") }
            Item { width: 14; height: 1 }
            Rectangle { width: 1; height: 19; color: Theme.color("#c3d6ed"); anchors.verticalCenter: parent.verticalCenter }
            Item { width: 14; height: 1 }
            LegacyButton { width: 75; text: "Discord ↗"; link: true; font.pixelSize: 13; onClicked: Qt.openUrlExternally("https://discord.gg/yZUcSD7KPe") }
        }
    }
    }
    Rectangle { anchors.fill: parent; color: "transparent"; border.color: Theme.color("#95bde5"); border.width: 1 }
    Repeater {
        model: [Qt.TopEdge, Qt.BottomEdge, Qt.LeftEdge, Qt.RightEdge, Qt.TopEdge | Qt.LeftEdge, Qt.TopEdge | Qt.RightEdge, Qt.BottomEdge | Qt.LeftEdge, Qt.BottomEdge | Qt.RightEdge]
        MouseArea {
            required property int modelData
            readonly property bool leftEdge: (modelData & Qt.LeftEdge) !== 0
            readonly property bool rightEdge: (modelData & Qt.RightEdge) !== 0
            readonly property bool topEdge: (modelData & Qt.TopEdge) !== 0
            readonly property bool bottomEdge: (modelData & Qt.BottomEdge) !== 0
            x: rightEdge ? window.width - 6 : 0; y: bottomEdge ? window.height - 6 : 0
            width: leftEdge || rightEdge ? 6 : window.width; height: topEdge || bottomEdge ? 6 : window.height
            cursorShape: (leftEdge || rightEdge) && (topEdge || bottomEdge) ? (leftEdge === topEdge ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor) : leftEdge || rightEdge ? Qt.SizeHorCursor : Qt.SizeVerCursor
            onPressed: window.startSystemResize(modelData)
        }
    }
}
