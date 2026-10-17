import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

T.ScrollBar {
    id: bar
    implicitWidth: 12
    implicitHeight: 12
    width: 12
    padding: 3
    topPadding: 8; bottomPadding: 8
    policy: ScrollBar.AsNeeded
    visible: policy !== ScrollBar.AlwaysOff && size < 1
    minimumSize: 0.08
    stepSize: 0.05
    hoverEnabled: true
    opacity: pressed || hovered || active ? 1 : 0.6
    Behavior on opacity { NumberAnimation { duration: 120 } }
    background: Rectangle {
        x: bar.leftPadding; y: bar.topPadding
        width: bar.availableWidth; height: bar.availableHeight
        radius: width / 2; color: Theme.color("#227ec9ef")
    }
    contentItem: Rectangle {
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0; color: bar.pressed ? Theme.color("#119de4") : Theme.color("#40d9ee") }
            GradientStop { position: 1; color: bar.pressed ? Theme.color("#087dcc") : Theme.color("#169cf4") }
        }
    }
}
