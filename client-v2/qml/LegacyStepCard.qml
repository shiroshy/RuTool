import QtQuick
import QtQuick.Shapes

LegacySurface {
    id: card
    property string step: "01"
    default property alias content: body.data
    Item { id: body; x: 81; y: 14; width: card.width - 99; height: card.height - 28 }
    Shape {
        x: 1; y: 1; width: 74; height: card.height - 2
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeWidth: 0; strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: 0; y1: 0; x2: 74; y2: 42
                GradientStop { position: 0; color: Theme.color("#19ddec") }
                GradientStop { position: 1; color: Theme.color("#00a3f7") }
            }
            PathSvg { path: "M0,10 Q0,0 10,0 L74,0 52,34 Q47,43 36,43 L20,43 Q5,43 5,60 V" + (card.height - 12) + " Q5," + (card.height - 7) + " 10," + (card.height - 7) + " L10," + (card.height - 2) + " Q0," + (card.height - 2) + " 0," + (card.height - 12) + " Z" }
        }
    }
    LegacyText { x: 16; y: 9; text: card.step; color: "white"; font.pixelSize: 24; font.bold: true }
    Repeater {
        model: 9
        Rectangle { required property int index; x: 25 + (index % 3) * 12; y: 60 + Math.floor(index / 3) * 12; width: 2; height: 2; color: Theme.color("#82d5ff"); opacity: 0.6 }
    }
}
