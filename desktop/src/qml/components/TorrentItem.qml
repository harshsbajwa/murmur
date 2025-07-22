import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: torrentItem
    
    property var torrentInfo
    property string infoHash: torrentInfo ? torrentInfo.infoHash : ""
    
    signal removeRequested(string infoHash)
    signal pauseRequested(string infoHash)
    signal resumeRequested(string infoHash)
    signal selected(string infoHash)
    
    height: 80
    color: mouseArea.containsMouse ? Qt.rgba(palette.highlight.r, palette.highlight.g, palette.highlight.b, 0.1) : "transparent"
    border.color: palette.mid
    border.width: 1
    radius: 4
    
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        
        onClicked: {
            torrentItem.selected(infoHash)
        }
    }
    
    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 12
        
        // Status indicator
        Rectangle {
            width: 8
            height: 8
            radius: 4
            color: getStatusColor(torrentInfo ? torrentInfo.status : "")
            Layout.alignment: Qt.AlignTop | Qt.AlignLeft
            Layout.topMargin: 8
        }
        
        // Main content
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            
            // Name and size
            RowLayout {
                Layout.fillWidth: true
                
                Text {
                    Layout.fillWidth: true
                    text: torrentInfo ? torrentInfo.name : ""
                    font.bold: true
                    font.pointSize: 11
                    color: palette.windowText
                    elide: Text.ElideRight
                }
                
                Text {
                    text: formatSize(torrentInfo ? torrentInfo.size : 0)
                    font.pointSize: 10
                    color: palette.placeholderText
                }
            }
            
            // Progress bar
            ProgressBar {
                Layout.fillWidth: true
                Layout.preferredHeight: 4
                from: 0
                to: 1
                value: torrentInfo ? torrentInfo.progress : 0
                
                background: Rectangle {
                    implicitHeight: 4
                    color: palette.mid
                    radius: 2
                }
                
                contentItem: Item {
                    implicitHeight: 4
                    
                    Rectangle {
                        width: torrentItem.torrentInfo ? 
                               (torrentItem.torrentInfo.progress * parent.width) : 0
                        height: parent.height
                        radius: 2
                        color: getProgressColor(torrentInfo ? torrentInfo.status : "")
                    }
                }
            }
            
            // Status and stats
            RowLayout {
                Layout.fillWidth: true
                
                Text {
                    text: getStatusText(torrentInfo ? torrentInfo.status : "")
                    font.pointSize: 9
                    color: palette.windowText
                }
                
                Text {
                    text: "•"
                    font.pointSize: 9
                    color: palette.placeholderText
                    visible: torrentInfo && torrentInfo.status === "downloading"
                }
                
                Text {
                    text: qsTr("%1% • ↓%2 ↑%3").arg(Math.round((torrentInfo ? torrentInfo.progress : 0) * 100))
                                                .arg(formatSpeed(torrentInfo ? torrentInfo.downloadSpeed : 0))
                                                .arg(formatSpeed(torrentInfo ? torrentInfo.uploadSpeed : 0))
                    font.pointSize: 9
                    color: palette.placeholderText
                    visible: torrentInfo && (torrentInfo.status === "downloading" || torrentInfo.status === "seeding")
                }
                
                Item {
                    Layout.fillWidth: true
                }
                
                Text {
                    text: qsTr("S:%1 L:%2").arg(torrentInfo ? torrentInfo.seeders : 0)
                                           .arg(torrentInfo ? torrentInfo.leechers : 0)
                    font.pointSize: 9
                    color: palette.placeholderText
                    visible: torrentInfo && (torrentInfo.seeders > 0 || torrentInfo.leechers > 0)
                }
            }
        }
        
        // Control buttons
        RowLayout {
            spacing: 4
            Layout.alignment: Qt.AlignTop
            
            Button {
                width: 24
                height: 24
                
                background: Rectangle {
                    color: parent.pressed ? palette.mid : "transparent"
                    radius: 12
                }
                
                contentItem: Text {
                    text: (torrentInfo && torrentInfo.status === "downloading") ? "⏸" : "▶"
                    color: palette.windowText
                    font.pointSize: 10
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                onClicked: {
                    if (torrentInfo && torrentInfo.status === "downloading") {
                        torrentItem.pauseRequested(infoHash)
                    } else {
                        torrentItem.resumeRequested(infoHash)
                    }
                }
                
                ToolTip.text: (torrentInfo && torrentInfo.status === "downloading") ? 
                             qsTr("Pause") : qsTr("Resume")
                ToolTip.visible: hovered
            }
            
            Button {
                width: 24
                height: 24
                
                background: Rectangle {
                    color: parent.pressed ? "#ffcdd2" : "transparent"
                    radius: 12
                }
                
                contentItem: Text {
                    text: "✕"
                    color: "#f44336"
                    font.pointSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                onClicked: {
                    torrentItem.removeRequested(infoHash)
                }
                
                ToolTip.text: qsTr("Remove")
                ToolTip.visible: hovered
            }
        }
    }
    
    function getStatusColor(status) {
        switch (status) {
            case "downloading": return "#2196F3"  // Blue
            case "seeding": return "#4CAF50"      // Green
            case "paused": return "#FF9800"       // Orange
            case "error": return "#F44336"        // Red
            case "completed": return "#4CAF50"    // Green
            default: return "#9E9E9E"             // Grey
        }
    }
    
    function getProgressColor(status) {
        switch (status) {
            case "downloading": return "#2196F3"  // Blue
            case "seeding": return "#4CAF50"      // Green
            case "completed": return "#4CAF50"    // Green
            default: return "#9E9E9E"             // Grey
        }
    }
    
    function getStatusText(status) {
        switch (status) {
            case "downloading": return qsTr("Downloading")
            case "seeding": return qsTr("Seeding")
            case "paused": return qsTr("Paused")
            case "error": return qsTr("Error")
            case "completed": return qsTr("Completed")
            default: return qsTr("Unknown")
        }
    }
    
    function formatSize(bytes) {
        if (bytes === 0) return "0 B"
        
        var k = 1024
        var sizes = ["B", "KB", "MB", "GB", "TB"]
        var i = Math.floor(Math.log(bytes) / Math.log(k))
        
        return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + " " + sizes[i]
    }
    
    function formatSpeed(bytesPerSecond) {
        if (bytesPerSecond === 0) return "0 B/s"
        
        var k = 1024
        var sizes = ["B/s", "KB/s", "MB/s", "GB/s"]
        var i = Math.floor(Math.log(bytesPerSecond) / Math.log(k))
        
        return parseFloat((bytesPerSecond / Math.pow(k, i)).toFixed(1)) + " " + sizes[i]
    }
}