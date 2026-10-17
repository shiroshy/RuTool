import QtQuick
Item {
    Image { anchors.fill: parent; source: "qrc:/assets/Background.png"; fillMode: Image.PreserveAspectCrop; opacity: 1 - Theme.mix }
    Image { anchors.fill: parent; source: "qrc:/assets/BackgroundDark.jpg"; fillMode: Image.PreserveAspectCrop; opacity: Theme.mix }
}
