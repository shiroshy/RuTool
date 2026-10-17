import QtQuick
import QtQuick.Shapes

Item {
    id: surface
    property color firstColor: Theme.color("#f5ffffff")
    property color lastColor: Theme.color("#eef3f9ff")
    property color edgeColor: Theme.color("#97c9fa")
    property real edgeWidth: 1
    property real radius: 10
    property real gradientY: 1
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: 0; y1: 0; x2: surface.width; y2: surface.height * surface.gradientY
                GradientStop { position: 0; color: surface.firstColor }
                GradientStop { position: 1; color: surface.lastColor }
            }
            PathSvg {
                path: "M " + surface.radius + ",0 H " + (surface.width - surface.radius)
                    + " Q " + surface.width + ",0 " + surface.width + "," + surface.radius
                    + " V " + (surface.height - surface.radius) + " Q " + surface.width + "," + surface.height
                    + " " + (surface.width - surface.radius) + "," + surface.height
                    + " H " + surface.radius + " Q 0," + surface.height + " 0," + (surface.height - surface.radius)
                    + " V " + surface.radius + " Q 0,0 " + surface.radius + ",0 Z"
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: surface.radius
        border.color: surface.edgeColor
        border.width: surface.edgeWidth
        antialiasing: true
    }
}
