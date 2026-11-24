import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Effects
import QtQuick.Layouts
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
    onClosing: function(event) { if (client.busy) { event.accepted = false; client.cancel() } }
    onWorkingChanged: if (working) statusOverride = ""
    readonly property color chromeColor: Theme.color("#f6faff")
    readonly property real backdropProgress: Math.max(disclaimer.progress, translations.progress, report.progress)
    function openCatalog() {
        client.loadCatalog()
        client.loadPremiumCatalog()
        translations.show()
    }
    function runPrimaryAction() {
        if (!client.gameValid) gamePicker.open()
        else if (client.canInstall) client.installTranslation()
        else if (!window.packageChosen) openCatalog()
    }
    FileDialog {
        id: logPicker; title: "Сохранить подробный лог ошибки"; fileMode: FileDialog.SaveFile
        nameFilters: ["Диагностический лог (*.txt)"]; defaultSuffix: "txt"
        onAccepted: client.exportLog(selectedFile)
    }
    function saveErrorLog() {
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
            if (ok) report.addDiagnostic(url)
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
    ReportWindow { id: report; transientParent: window; onExportLogRequested: window.saveErrorLog() }
    TranslationsWindow {
        id: translations
        transientParent: window
        primaryRelease: client.primaryRelease.id ? client.primaryRelease : null
        releases: client.premiumReleases
        loadingScreensAvailable: client.loadingScreensAvailable
        gameReady: client.gameValid
        onLoadingScreensRequested: { translations.close(); client.downloadAndInstallLoadingScreens() }
        onManualRequested: packagePicker.open()
        onDownloadRequested: function(releaseId) { translations.close(); client.downloadTranslation(releaseId) }
        onCodeRequested: { translations.close(); boostyDialog.show() }
    }
    BoostyWindow { id: boostyDialog; transientParent: window }
    ConfirmationWindow { id: confirmation; transientParent: window; z: 65 }
    Component.onCompleted: {
        uiState.restoreWindow(window)
        visible = true
        if (!uiState.hasAcceptedAgreement(client.licenseHash)) disclaimer.showMandatory()
        client.refreshContent()
        client.start()
    }

    LegacyDialogWindow {
        id: settings
        objectName: "settingsWindow"
        transientParent: window
        title: "Настройки · RuTool"
        caption: "/  НАСТРОЙКИ"
        panelWidth: 720
        panelHeight: 444
        enabled: opened && !boostyDialog.visible
        wash: window.chromeColor
        z: 38

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 28
            anchors.topMargin: 84
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                LegacyText { text: "Тема"; font.pixelSize: 17; font.bold: true }
                Repeater {
                    model: ["Светлая", "Тёмная"]
                    LegacyButton {
                        required property int index
                        required property string modelData
                        objectName: index === 0 ? "lightThemeButton" : "darkThemeButton"
                        text: modelData
                        primary: Theme.dark === (index === 1)
                        onClicked: uiState.darkTheme = index === 1
                    }
                }
                Item { Layout.fillWidth: true }
                LegacyText {
                    visible: text.length > 0
                    text: client.checkingPremium ? "Проверяем…" : client.premiumActive ? "Активен" : client.boostyCode.length ? "Код не активен" : ""
                    font.pixelSize: 13
                    color: client.premiumActive ? Theme.color("#159d50") : Theme.muted
                }
                LegacyButton {
                    objectName: "boostyButton"
                    text: "Код бусти"
                    enabled: !window.working
                    hint: client.premiumStatus
                    onClicked: boostyDialog.show()
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.color("#cbddeb") }
            LegacyText { text: "Папка игры"; font.pixelSize: 19; font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                LegacyField {
                    objectName: "gamePath"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    icon: "folder"
                    text: client.gameFolder
                    placeholderText: "Папка не выбрана"
                    enabled: !window.working
                    onTextEdited: client.gameFolder = text
                    Accessible.name: "Папка игры"
                }
                LegacyButton {
                    objectName: "chooseGame"
                    text: "Обзор…"
                    enabled: !window.working
                    onClicked: gamePicker.open()
                }
            }
            LegacyText { text: "Пакет перевода"; font.pixelSize: 19; font.bold: true }
            LegacyField {
                objectName: "packagePath"
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                icon: "file"
                readOnly: true
                text: client.packagePath
                placeholderText: "Файл .rut не выбран"
                Accessible.name: "Файл перевода .rut"
            }
            LegacyText {
                Layout.fillWidth: true
                visible: window.packageChosen && !window.packagePending
                text: [client.packageName, client.packageSigner || "Автор не подтверждён", client.packageKind].filter(Boolean).join("  ·  ")
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.pixelSize: 14
                color: Theme.muted
                ToolTip.visible: truncated && packageMetadataHover.hovered
                ToolTip.text: text
                HoverHandler { id: packageMetadataHover }
            }
            LegacyText {
                Layout.fillWidth: true
                visible: (window.packagePending || window.packageChosen || window.packageError) && text.length > 0
                text: client.packageStatus
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                color: Theme.muted
                font.pixelSize: 14
                ToolTip.visible: truncated && packageStatusHover.hovered
                ToolTip.text: text
                HoverHandler { id: packageStatusHover }
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                LegacyButton {
                    objectName: "choosePackage"
                    text: "Ручная установка"
                    enabled: !window.working
                    hint: "Выбрать локальный файл .rut"
                    onClicked: { settings.close(); packagePicker.open() }
                }
                Item { Layout.fillWidth: true }
                LegacyButton {
                    objectName: "restoreButton"
                    text: "Восстановить файлы"
                    enabled: client.canRestore
                    onClicked: { settings.close(); client.restoreFiles() }
                }
            }
        }
    }

    Item {
        id: mainContent
        anchors.fill: parent
        anchors.margins: 1
        layer.enabled: window.backdropProgress > 0 && GraphicsInfo.api !== GraphicsInfo.Software
        layer.effect: MultiEffect {
            autoPaddingEnabled: false
            blurEnabled: true
            blurMax: 16
            blur: window.backdropProgress * 0.45
        }
        enabled: !disclaimer.visible && !report.visible && !credits.visible && !translations.visible
                 && !boostyDialog.visible && !faq.visible && !license.visible && !confirmation.visible && !settings.visible

        Item {
            id: header
            objectName: "launcherHeader"
            width: parent.width
            height: 72
            z: 1

            MouseArea {
                anchors.fill: parent
                onPressed: window.startSystemMove()
                onDoubleClicked: window.visibility = window.visibility === Window.Maximized ? Window.Windowed : Window.Maximized
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 6
                spacing: 10
                Rectangle {
                    objectName: "brandPlate"
                    Layout.preferredWidth: brand.implicitWidth + 24
                    Layout.preferredHeight: 48
                    radius: 7
                    color: Theme.color("#eaf6faff")
                    border.color: Theme.border
                    RowLayout {
                        id: brand
                        anchors.centerIn: parent
                        spacing: 10
                        Shape {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 18
                            ShapePath {
                                strokeWidth: 0; strokeColor: "transparent"; fillColor: Theme.color("#1464b9")
                                PathSvg { path: "M0,18 L12,0 21,0 9,18 Z M15,18 L27,0 36,0 24,18 Z" }
                            }
                        }
                        LegacyText { text: "RuTool"; color: Theme.text; font.pixelSize: 30; font.weight: Font.Black; font.italic: true }
                        LegacyText {
                            objectName: "versionLabel"
                            text: "v" + client.applicationVersion
                            color: Theme.muted
                            font.pixelSize: 14
                            HoverHandler { id: versionHover; enabled: mainContent.enabled }
                            ToolTip.visible: mainContent.enabled && versionHover.hovered
                            ToolTip.text: "Версия установленной программы"
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                LegacyButton {
                    objectName: "applicationUpdateButton"
                    visible: !!client.applicationUpdateVersion
                    enabled: !window.working
                    font.pixelSize: 13
                    text: "Обновить до " + client.applicationUpdateVersion
                    onClicked: client.requestApplicationUpdate()
                }
                Row {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 10
                    spacing: 4
                    Repeater {
                        model: [{ label: "Настройки", name: "settingsButton" },
                                { label: "Свернуть", name: "minimizeButton" },
                                { label: "Закрыть", name: "closeButton" }]
                        LegacyButton {
                            id: windowButton
                            required property int index
                            required property var modelData
                            readonly property bool closingButton: index === 2
                            readonly property bool closeHovered: closingButton && hovered
                            objectName: modelData.name
                            width: 32; height: 32
                            padding: 0
                            leftPadding: 0; rightPadding: 0
                            hint: modelData.label
                            Accessible.name: hint
                            ink: closeHovered ? "white" : enabled ? Theme.text : Theme.muted
                            normalEdge: Theme.border
                            surfaceFirst: closeHovered ? "#b4243f" : Theme.color("#f8ffffff")
                            surfaceLast: closeHovered ? surfaceFirst : Theme.color("#eef1f9ff")
                            onClicked: {
                                if (closingButton) window.close()
                                else if (index === 1) window.showMinimized()
                                else settings.show()
                            }
                            contentItem: Item {
                                Rectangle {
                                    objectName: "minimizeIcon"
                                    visible: windowButton.index === 1
                                    anchors.centerIn: parent
                                    width: 10; height: 2; radius: 1
                                    color: windowButton.ink
                                }
                                LegacyIcon {
                                    objectName: windowButton.closingButton ? "closeIcon" : "settingsIcon"
                                    visible: windowButton.index !== 1
                                    anchors.centerIn: parent
                                    kind: windowButton.closingButton ? "close" : "settings"
                                    width: windowButton.closingButton ? 10 : 15; height: width
                                    lineWidth: 1.6
                                    stroke: windowButton.ink
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: hero
            objectName: "launcherHero"
            color: window.chromeColor
            anchors.top: parent.top
            anchors.bottom: actions.top
            width: parent.width
            clip: true

            ThemedBackground {
                objectName: "mainBackground"
                anchors.fill: parent
                verticalAlignment: Image.AlignBottom
            }
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Theme.color("#e8e9f6ff") }
                    GradientStop { position: 0.43; color: Theme.color("#a8e6f3ff") }
                    GradientStop { position: 1; color: Theme.color("#12e6f3ff") }
                }
            }
            Column {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 56
                anchors.bottomMargin: 48
                spacing: 10
                LegacyText { text: "Blue Archive · Steam"; color: Theme.color("#405e7e"); font.pixelSize: 18 }
                LegacyText { text: "Русификатор"; color: Theme.color("#102944"); font.pixelSize: 68; font.weight: Font.DemiBold }
                LegacyText {
                    objectName: "trustTitle"
                    text: window.packagePending ? (window.packageDownloading ? "Скачиваем файл…" : "Проверяем пакет…")
                          : window.packageError ? "Ошибка пакета"
                          : window.packageChosen ? (client.officialPackage ? "Подпись подтверждена" : "Подпись не подтверждена")
                          : !client.gameValid ? "Укажите папку игры"
                          : client.canRestore ? "Перевод установлен" : "Выберите перевод"
                    color: window.packageError ? Theme.color("#a3314a")
                           : window.packageChosen && !window.packagePending && !client.officialPackage ? Theme.color("#805106")
                           : Theme.color("#075995")
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                }
            }
            AnimatedImage {
                objectName: "aronaImage"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 30
                anchors.bottomMargin: -10
                width: 140; height: width
                source: "qrc:/assets/Arona.gif"
                fillMode: Image.PreserveAspectFit
                playing: window.visibility !== Window.Minimized
                Accessible.name: "Анимация Ароны"
            }
        }

        Rectangle {
            id: actions
            objectName: "actions"
            anchors.bottom: footer.top
            width: parent.width
            height: 110
            color: window.chromeColor

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 32
                anchors.rightMargin: 32
                spacing: 16
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    LegacyText {
                        Layout.fillWidth: true
                        text: client.gameValid ? "Игра найдена" : client.gameFolder.length ? "Проверьте папку игры" : "Папка игры не выбрана"
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    LegacyText {
                        id: operationStatus
                        Layout.fillWidth: true
                        text: (window.working ? client.operationStatus : window.statusOverride || client.operationStatus) || (window.packageChosen || window.packageError ? client.packageStatus : "Готово к работе.")
                        color: Theme.muted
                        font.pixelSize: 14
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        HoverHandler { id: statusHover }
                        ToolTip.visible: mainContent.enabled && statusHover.hovered && truncated
                        ToolTip.text: text
                    }
                    ProgressBar {
                        objectName: "operationProgress"
                        visible: window.working
                        Layout.fillWidth: true
                        Layout.maximumWidth: 360
                        indeterminate: true
                        Accessible.name: operationStatus.text
                    }
                }
                LegacyButton {
                    objectName: "serverPackage"
                    visible: !window.working && (!client.gameValid || client.canInstall)
                    text: "Каталог переводов"
                    onClicked: window.openCatalog()
                }
                ColumnLayout {
                    visible: !window.working
                    spacing: 6
                    LegacyButton {
                        id: primaryAction
                        objectName: "installButton"
                        Layout.preferredWidth: 236
                        Layout.preferredHeight: 54
                        enabled: !client.gameValid || !window.packageChosen || client.canInstall
                        text: !client.gameValid ? "Выбрать папку игры" : window.packageChosen ? "Установить перевод" : client.canRestore ? "Обновить перевод" : "Скачать перевод"
                        font.pixelSize: 19
                        font.weight: Font.DemiBold
                        primary: true
                        onClicked: window.runPrimaryAction()
                        background: Item {
                            Shape {
                                anchors.fill: parent
                                ShapePath {
                                    strokeWidth: 0; strokeColor: "transparent"
                                    fillGradient: LinearGradient {
                                        x1: 0; y1: 0
                                        x2: primaryAction.width
                                        y2: primaryAction.height * primaryAction.primaryGradientY
                                        GradientStop { position: 0; color: primaryAction.enabled ? Theme.primaryStart : Theme.color("#e4eef8") }
                                        GradientStop { position: 1; color: primaryAction.enabled ? Theme.primaryEnd : Theme.color("#e4eef8") }
                                    }
                                    PathSvg { path: "M0,0 H216 L236,20 V54 H0 Z" }
                                }
                            }
                        }
                    }
                    LegacyText {
                        objectName: "installationCount"
                        Layout.alignment: Qt.AlignHCenter
                        Layout.maximumWidth: 220
                        text: "Установок: " + client.installationCount
                        font.pixelSize: 13
                        color: Theme.muted
                        elide: Text.ElideRight
                        HoverHandler { id: installationsHover }
                        ToolTip.visible: mainContent.enabled && installationsHover.hovered && truncated
                        ToolTip.text: text
                    }
                }
                LegacyButton {
                    objectName: "cancelOperation"
                    visible: window.working && client.canCancel
                    text: "Отменить"
                    onClicked: client.cancel()
                }
            }
        }

        Rectangle {
            id: footer
            objectName: "footer"
            anchors.bottom: parent.bottom
            width: parent.width
            height: 49
            color: window.chromeColor
            Rectangle { width: parent.width; height: 1; color: Theme.color("#cbddeb") }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 23
                spacing: 9
                LegacyButton {
                    objectName: "disclaimerButton"
                    Layout.preferredWidth: 30; Layout.preferredHeight: 30; chrome: true
                    text: "ⓘ"
                    font.pixelSize: 19
                    hint: "Открыть дисклеймер"
                    Accessible.name: hint
                    onClicked: { disclaimer.mandatory = false; disclaimer.show() }
                }
                LegacyButton { objectName: "reportButton"; text: "Сообщить о проблеме"; link: true; font.pixelSize: 13; onClicked: report.show() }
                Item { Layout.fillWidth: true }
                LegacyButton { objectName: "faqButton"; text: "FAQ"; link: true; font.pixelSize: 13; onClicked: faq.show() }
                LegacyButton { text: "На чай"; link: true; font.pixelSize: 13; onClicked: Qt.openUrlExternally("https://boosty.to/shizukax") }
                LegacyButton { objectName: "creditsButton"; text: "Credits"; link: true; font.pixelSize: 13; onClicked: credits.show() }
                LegacyButton { text: "Telegram ↗"; link: true; font.pixelSize: 13; onClicked: Qt.openUrlExternally("https://t.me/bluearchive_ru") }
                LegacyButton { text: "Discord ↗"; link: true; font.pixelSize: 13; onClicked: Qt.openUrlExternally("https://discord.gg/yZUcSD7KPe") }
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
