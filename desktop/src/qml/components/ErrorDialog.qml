import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Dialog {
    id: errorDialog
    
    property string errorTitle: ""
    property string errorMessage: ""
    property string errorDetails: ""
    property bool showDetails: false
    
    function showError(title, message, details = "") {
        console.log("ErrorDialog.showError called:", title, message, details)
        errorTitle = title || "Error"
        errorMessage = message || "An unknown error occurred"
        errorDetails = details || ""
        showDetails = false
        open()
    }
    
    title: errorTitle
    modal: true
    anchors.centerIn: parent
    width: Math.min(600, parent.width * 0.8)
    height: showDetails ? Math.min(500, parent.height * 0.8) : implicitHeight
    z: 10000  // Ensure dialog appears above progress overlay
    
    standardButtons: Dialog.Ok | (errorDetails ? Dialog.Help : Dialog.NoButton)
    
    onHelpRequested: {
        showDetails = !showDetails
    }
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 16
        
        // Error icon and message
        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            
            // Error icon
            Rectangle {
                width: 48
                height: 48
                radius: 24
                color: "#ffebee"
                
                Text {
                    anchors.centerIn: parent
                    text: "⚠"
                    font.pointSize: 24
                    color: "#d32f2f"
                }
            }
            
            // Error message
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                
                Text {
                    Layout.fillWidth: true
                    text: errorDialog.errorMessage
                    font.pointSize: 12
                    color: palette.windowText
                    wrapMode: Text.WordWrap
                }
                
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Please try again or contact support if the problem persists.")
                    font.pointSize: 10
                    color: palette.placeholderText
                    wrapMode: Text.WordWrap
                    visible: errorDialog.errorMessage.length > 0
                }
            }
        }
        
        // Details section (collapsible)
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: 200
            visible: showDetails && errorDetails.length > 0
            
            TextArea {
                text: errorDialog.errorDetails
                readOnly: true
                font.family: "monospace"
                font.pointSize: 9
                color: palette.windowText
                background: Rectangle {
                    color: palette.base
                    border.color: palette.mid
                    border.width: 1
                    radius: 4
                }
            }
        }
    }
    
    footer: DialogButtonBox {
        Button {
            text: qsTr("Copy Details")
            visible: showDetails && errorDetails.length > 0
            onClicked: {
                // Copy error details to clipboard
                Qt.callLater(function() {
                    var clipboard = Qt.createQmlObject('import QtQuick 2.15; TextEdit { visible: false }', errorDialog)
                    clipboard.text = errorDetails
                    clipboard.selectAll()
                    clipboard.copy()
                    clipboard.destroy()
                })
            }
        }
        
        Button {
            text: showDetails ? qsTr("Hide Details") : qsTr("Show Details")
            visible: errorDetails.length > 0
            onClicked: {
                showDetails = !showDetails
            }
        }
        
        Button {
            text: qsTr("OK")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
    }
}