import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: progressOverlay
    visible: overlayVisible
    
    property string title: ""
    property string message: ""
    property real progress: 0.0  // 0.0 to 1.0, or -1 for indeterminate
    property bool cancellable: false
    property bool overlayVisible: false
    
    signal cancelled()
    
    function show(titleText, messageText, progressValue = -1, canCancel = false) {
        title = titleText
        message = messageText
        progress = progressValue
        cancellable = canCancel
        overlayVisible = true
        fadeIn.start()
    }
    
    function hide() {
        fadeOut.start()
    }
    
    function updateProgress(value, messageText = "") {
        progress = value
        if (messageText.length > 0) {
            message = messageText
        }
    }
    
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.6)
    opacity: 0
    z: 1000
    
    // Fade animations
    NumberAnimation {
        id: fadeIn
        target: progressOverlay
        property: "opacity"
        from: 0
        to: 1
        duration: 200
        easing.type: Easing.OutQuad
    }
    
    NumberAnimation {
        id: fadeOut
        target: progressOverlay
        property: "opacity"
        from: 1
        to: 0
        duration: 200
        easing.type: Easing.InQuad
        onFinished: progressOverlay.overlayVisible = false
    }
    
    // Center content
    Rectangle {
        anchors.centerIn: parent
        width: 400
        height: contentColumn.implicitHeight + 48
        radius: 8
        color: palette.window
        border.color: palette.mid
        border.width: 1
        
        // Simple border styling instead of drop shadow
        
        ColumnLayout {
            id: contentColumn
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16
            
            // Title
            Text {
                Layout.fillWidth: true
                text: progressOverlay.title
                font.bold: true
                font.pointSize: 14
                color: palette.windowText
                horizontalAlignment: Text.AlignHCenter
                visible: title.length > 0
            }
            
            // Message
            Text {
                Layout.fillWidth: true
                text: progressOverlay.message
                font.pointSize: 11
                color: palette.windowText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: message.length > 0
            }
            
            // Progress indicator
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: progressOverlay.progress >= 0 ? progressOverlay.progress : 0
                    indeterminate: progressOverlay.progress < 0
                    
                    background: Rectangle {
                        implicitHeight: 6
                        color: palette.mid
                        radius: 3
                    }
                    
                    contentItem: Item {
                        implicitHeight: 6
                        clip: true  // Ensure progress indicator stays within bounds
                        
                        Rectangle {
                            width: progressOverlay.progress >= 0 ? 
                                   (progressOverlay.progress * parent.width) : (parent.width * 0.3)
                            height: parent.height
                            radius: 3
                            color: "#2196F3"
                            
                            // Indeterminate animation
                            SequentialAnimation on x {
                                running: progressOverlay.progress < 0 && progressOverlay.overlayVisible
                                loops: Animation.Infinite
                                
                                NumberAnimation {
                                    from: -width
                                    to: parent.width
                                    duration: 1000
                                    easing.type: Easing.InOutQuad
                                }
                            }
                        }
                    }
                }
                
                // Progress percentage
                Text {
                    Layout.fillWidth: true
                    text: progressOverlay.progress >= 0 ? 
                          qsTr("%1%").arg(Math.round(progressOverlay.progress * 100)) :
                          qsTr("Processing...")
                    font.pointSize: 10
                    color: palette.placeholderText
                    horizontalAlignment: Text.AlignHCenter
                }
            }
            
            // Cancel button
            Button {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Cancel")
                visible: progressOverlay.cancellable
                
                onClicked: {
                    progressOverlay.cancelled()
                    progressOverlay.hide()
                }
            }
        }
    }
    
    // Block mouse events
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
    }
}