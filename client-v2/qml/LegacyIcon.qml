import QtQuick
import QtQuick.Shapes

Item {
    id: icon
    property string kind: "file"
    property color stroke: Theme.color("#199fff")
    property color fill: "transparent"
    property real lineWidth: 1.8
    readonly property var definition: ({
        close: [1, 1, 10, 10, "M1,1 L11,11 M11,1 L1,11"],
        folder: [2, 5, 20, 15, "M2,5 L9,5 11,7 22,7 22,20 2,20 Z"],
        file: [5, 2, 16, 21, "M5,2 L14,2 21,9 21,23 5,23 Z M14,2 L14,9 21,9"],
        shield: [2, 2, 20, 23, "M12,2 C16,5 19,5 22,6 L22,13 C22,19 17,23 12,25 C7,23 2,19 2,13 L2,6 C5,5 8,5 12,2 Z M7,13 L11,17 17,10"],
        game: [1, 2, 22, 19.287254333496094, "M7,6 L17,6 C21,6 23,12 23,18 C23,23 19,22 16,17 L8,17 C5,22 1,23 1,18 C1,12 3,6 7,6 Z M7,9 L7,15 M4,12 L10,12 M16,10 L16,12 M19,13 L19,15 M12,6 L12,2"],
        download: [2, 1, 20, 22, "M12,1 L12,16 M6,10 L12,16 18,10 M2,17 L2,23 22,23 22,17"],
        restore: [3, 3, 18, 19, "M21,9 C19,3 11,1 6,6 L3,9 M3,3 L3,9 9,9 M3,16 C5,22 13,24 18,19 L21,16 M15,16 L21,16 21,22"],
        warning: [0, 1, 24, 22, "M12,1 L24,23 0,23 Z M12,8 L12,15 M12,18 L12,20"]
    })[kind]
    readonly property real factor: Math.min((width - lineWidth) / definition[2], (height - lineWidth) / definition[3])
    Shape {
        x: icon.lineWidth / 2 - icon.definition[0] * icon.factor
        y: icon.lineWidth / 2 - icon.definition[1] * icon.factor
        width: icon.definition[2]; height: icon.definition[3]
        transform: Scale { xScale: icon.factor; yScale: icon.factor }
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: icon.stroke; fillColor: icon.fill
            strokeWidth: icon.lineWidth / icon.factor
            capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.definition[4] }
        }
    }
}
