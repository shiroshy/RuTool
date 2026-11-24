import QtQuick

Item {
    id: background
    property int verticalAlignment: Image.AlignVCenter

    Image {
        anchors.fill: parent
        source: "qrc:/assets/Background.png"
        fillMode: Image.PreserveAspectCrop
        verticalAlignment: background.verticalAlignment
    }
    Image {
        objectName: "darkBackgroundImage"
        anchors.fill: parent
        source: "qrc:/assets/BackgroundDark.jpg"
        fillMode: Image.PreserveAspectCrop
        verticalAlignment: background.verticalAlignment
        opacity: Theme.mix
    }
}
