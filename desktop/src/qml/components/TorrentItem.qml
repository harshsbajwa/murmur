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
    radius: 8
    
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
        anchors.margins: 12
        spacing: 16
        
        // Status indicator
        Rectangle {
            width: 12
            height: 12
            radius: 6
            color: getStatusColor(torrentInfo ? torrentInfo.status : "")
            Layout.alignment: Qt.AlignTop | Qt.AlignLeft
        }
        
        // Main content
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            
            // Name and size
            RowLayout {
                Layout.fillWidth: true
                
                Text {
                    Layout.fillWidth: true
                    text: torrentInfo ? torrentInfo.name : ""
                    font.bold: true
                    font.pointSize: 12
                    color: palette.windowText
                    elide: Text.ElideRight
                }
                
                Text {
                    text: formatSize(torrentInfo ? torrentInfo.size : 0)
                    font.pointSize: 11
                    color: palette.placeholderText
                }
            }
            
            // Progress bar
            ProgressBar {
                Layout.fillWidth: true
                Layout.preferredHeight: 6
                from: 0
                to: 1
                value: torrentInfo ? torrentInfo.progress : 0
                
                background: Rectangle {
                    implicitHeight: 6
                    color: palette.mid
                    radius: 3
                }
                
                contentItem: Item {
                    implicitHeight: 6
                    
                    Rectangle {
                        width: torrentItem.torrentInfo ? 
                               (torrentItem.torrentInfo.progress * parent.width) : 0
                        height: parent.height
                        radius: 3
                        color: getProgressColor(torrentInfo ? torrentInfo.status : "")
                    }
                }
            }
            
            // Status and stats
            RowLayout {
                Layout.fillWidth: true
                
                Text {
                    text: getStatusText(torrentInfo ? torrentInfo.status : "")
                    font.pointSize: 10
                    color: palette.windowText
                }
                
                Text {
                    text: "•"
                    font.pointSize: 10
                    color: palette.placeholderText
                    visible: torrentInfo && (torrentInfo.status === "downloading" || torrentInfo.status === "seeding")
                }
                
                Text {
                    text: qsTr("%1% • ↓%2 ↑%3").arg(Math.round((torrentInfo ? torrentInfo.progress : 0) * 100))
                                                .arg(formatSpeed(torrentInfo ? torrentInfo.downloadSpeed : 0))
                                                .arg(formatSpeed(torrentInfo ? torrentInfo.uploadSpeed : 0))
                    font.pointSize: 10
                    color: palette.placeholderText
                    visible: torrentInfo && (torrentInfo.status === "downloading" || torrentInfo.status === "seeding")
                }
                
                Item {
                    Layout.fillWidth: true
                }
                
                Text {
                    text: qsTr("S:%1 L:%2").arg(torrentInfo ? torrentInfo.seeders : 0)
                                           .arg(torrentInfo ? torrentInfo.leechers : 0)
                    font.pointSize: 10
                    color: palette.placeholderText
                    visible: torrentInfo && (torrentInfo.seeders > 0 || torrentInfo.leechers > 0)
                }
            }
        }
        
        // Control buttons
        RowLayout {
            spacing: 8
            Layout.alignment: Qt.AlignTop
            
            // Play button (visible when torrent is completed/seeding)
            RoundButton {
                width: 32
                height: 32
                visible: torrentInfo && (torrentInfo.status === "seeding" || torrentInfo.status === "completed")
                
                icon.source: "qrc:/qt/qml/Murmur/resources/images/play_symbol.svg"
                icon.width: 20
                icon.height: 20
                icon.color: "#ffffff"
                
                background: Rectangle {
                    color: parent.pressed ? "#1976D2" : "#2196F3"
                    radius: 16
                }
                
                onClicked: {
                    torrentItem.selected(infoHash)
                }
                
                ToolTip.text: qsTr("Play")
                ToolTip.visible: hovered
            }
            
            // Pause/Resume button
            RoundButton {
                width: 32
                height: 32
                
                icon.source: (torrentInfo && torrentInfo.status === "downloading") ? 
                            "qrc:/qt/qml/Murmur/resources/images/pause_symbol.svg" : "qrc:/qt/qml/Murmur/resources/images/play_symbol.svg"
                icon.width: 20
                icon.height: 20
                icon.color: "#ffffff"
                
                background: Rectangle {
                    color: parent.pressed ? palette.mid : 
                          (torrentInfo && torrentInfo.status === "downloading") ? "#FF9800" : "#4CAF50"
                    radius: 16
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
            
            RoundButton {
                width: 32
                height: 32
                
                icon.source: "qrc:/qt/qml/Murmur/resources/images/more.svg"
                icon.width: 20
                icon.height: 20
                icon.color: "#ffffff"
                
                background: Rectangle {
                    color: parent.pressed ? "#ffcdd2" : "#f44336"
                    radius: 16
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
            case "checking": return "#9C27B0"     // Purple
            case "connecting": return "#607D8B"   // Blue Grey
            default: return "#9E9E9E"             // Grey
        }
    }
    
    function getProgressColor(status) {
        switch (status) {
            case "downloading": return "#2196F3"  // Blue
            case "seeding": return "#4CAF50"      // Green
            case "completed": return "#4CAF50"    // Green
            case "checking": return "#9C27B0"     // Purple
            case "connecting": return "#607D8B"   // Blue Grey
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
            case "checking": return qsTr("Checking files")
            case "connecting": return qsTr("Connecting")
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