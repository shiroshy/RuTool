pragma Singleton
import QtQuick

QtObject {
    readonly property bool dark: uiState.darkTheme
    property real mix: dark ? 1 : 0
    Behavior on mix { NumberAnimation { duration: 360; easing.type: Easing.InOutCubic } }
    function rgba(value) {
        var hex = String(value).replace("#", "")
        var alpha = hex.length === 8 ? parseInt(hex.slice(0, 2), 16) / 255 : 1
        if (hex.length === 8) hex = hex.slice(2)
        return Qt.rgba(parseInt(hex.slice(0, 2), 16) / 255, parseInt(hex.slice(2, 4), 16) / 255, parseInt(hex.slice(4, 6), 16) / 255, alpha)
    }
    readonly property url background: dark ? "qrc:/assets/BackgroundDark.jpg" : "qrc:/assets/Background.png"
    readonly property color text: color("#112955")
    readonly property color muted: color("#637faa")
    readonly property color card: color("#f5ffffff")
    readonly property color cardEnd: color("#eef3f9ff")
    readonly property color border: color("#97c9fa")
    readonly property var darkColors: ({
        "#e6f3ff": "#081423", "#e9f6ff": "#081423",
        "#10eaf5ff": "#66091324", "#88eaf5ff": "#aa091324",
        "#bceaf5ff": "#c0091324", "#b9eaf5ff": "#c0091324", "#c3eaf5ff": "#c0091324",
        "#dfffffff": "#ee102239", "#e8ffffff": "#ee102239",
        "#f5ffffff": "#ed12243b", "#eef3f9ff": "#ee172e47",
        "#f8ffffff": "#f012243b", "#eef1f9ff": "#f0172e47",
        "#edf1f9ff": "#f0172e47", "#f9ffffff": "#f012243b", "#eef2faff": "#f0172e47",
        "#70f1f7ff": "#cf0b1c30", "#dff8fcff": "#e00c2036", "#a9f1f8ff": "#d00c2036",
        "#f7fcff": "#142d47", "#e0f3ff": "#234e70", "#c9f5faff": "#cc122c45",
        "#cceaf6ff": "#e018354e", "#e4eef8": "#21384e", "#c7eaf6ff": "#3b6490",
        "#d4fff0f2": "#e03e2233", "#d4fff8e8": "#e03e3421",
        "#d4f0fff5": "#e019392f", "#4727986a": "#7060d697",
        "#112955": "#e4effa", "#0c234d": "#e4effa", "#183764": "#e4effa",
        "#142f5b": "#e4effa", "#15345d": "#e4effa", "#173664": "#e4effa",
        "#254466": "#cbdbee", "#637faa": "#a1b5cf", "#637fa4": "#a1b5cf",
        "#627da6": "#a1b5cf", "#4e6998": "#a1b5cf", "#4c6686": "#a1b5cf",
        "#6582aa": "#a1b5cf", "#5678a0": "#a1b5cf", "#7f95ae": "#8fa5bf",
        "#748ca5": "#8ca0b6", "#cb495e": "#ff8495", "#b07718": "#f1c46e",
        "#159d50": "#60d697", "#2485cc": "#76c9ff", "#078fe9": "#66c5ff", "#1464b9": "#a0deff",
        "#638ab6": "#8dafd4", "#6388bf": "#8dafd4", "#527bbf": "#8dafd4", "#547dba": "#8dafd4",
        "#4d70a9": "#8dafd4", "#668abe": "#8dafd4", "#527ca6": "#8dafd4",
        "#95bde5": "#3b6181", "#91bfe8": "#3b6181", "#97c9fa": "#446888",
        "#89cdf8": "#446888", "#91c6f1": "#446888", "#97c7f0": "#446888",
        "#91b4e5": "#446888", "#9bc6eb": "#446888", "#b7cbdd": "#3d566e",
        "#b9cfef": "#446888", "#a9c9e9": "#446888", "#c3ddf1": "#446888",
        "#add3f3": "#446888", "#83c4f0": "#446888", "#a5d6f7": "#446888",
        "#cbddeb": "#36536e", "#90adce": "#52718e", "#94b4d7": "#52718e",
        "#a1bedf": "#52718e", "#a3bde0": "#52718e", "#c3d6ed": "#52718e", "#c2dcee": "#36536e"
    })
    function color(light) {
        var target = darkColors[String(light).toLowerCase()]
        if (!target) return light
        var a = rgba(light), b = rgba(target)
        return Qt.rgba(a.r + (b.r-a.r)*mix, a.g + (b.g-a.g)*mix, a.b + (b.b-a.b)*mix, a.a + (b.a-a.a)*mix)
    }
}
