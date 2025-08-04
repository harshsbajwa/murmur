import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import QtQuick.Dialogs
import QtMultimedia 6.0
import Murmur 1.0
import "components"

ApplicationWindow {
    id: window
    
    width: 1200
    height: 800
    visible: true
    title: qsTr("Murmur Desktop")
    color: "#1e1e1e" // Dark background for better contrast
    
    property bool isFullscreen: false
    property bool videoFullscreen: false
    
    menuBar: MainMenuBar {
        id: mainMenuBar
        hasActiveVideo: mediaCtrl.currentVideoSource.toString().length > 0
        hasTranscription: transcriptionCtrl.currentTranscription !== null
        
        onOpenFile: fileOpenDialog.open()
        onOpenFolder: folderOpenDialog.open()
        onAddMagnetLink: magnetDialog.open()
        onShowSettings: settingsDialog.open()
        onShowAbout: aboutDialog.open()
        onExitApplication: Qt.quit()
        onExportTranscription: function(format) {
            var fileName = "transcription." + format
            if (transcriptionCtrl.currentTranscription) {
                fileManagerCtrl.exportTranscription(
                    JSON.stringify(transcriptionCtrl.currentTranscription),
                    format,
                    fileManagerCtrl.defaultExportPath + "/" + fileName
                )
            }
        }
        onClearHistory: {
            // Clear playback history
            if (appController && appController.storageManager) {
                appController.storageManager.clearPlaybackPositions()
            }
        }
    }
    
    property alias torrentController: torrentCtrl
    property alias mediaController: mediaCtrl
    property alias transcriptionController: transcriptionCtrl
    property alias fileManagerController: fileManagerCtrl
    
    property bool fileManagerReady: false
    
    // Controllers
    TorrentController {
        id: torrentCtrl
        onTorrentError: function(infoHash, error) {
            errorDialog.showError("Torrent Error", error)
        }
        onOperationCompleted: function(message) {
            statusBar.showMessage(message, 3000)
        }
    }
    
    MediaController {
        id: mediaCtrl
        
        onConversionProgress: function(operationId, progress) {
            progressOverlay.updateProgress(progress, qsTr("Converting video..."))
        }
        
        onConversionCompleted: function(operationId, outputPath) {
            progressOverlay.hide()
            statusBar.showMessage(qsTr("Conversion completed: %1").arg(outputPath), 5000)
            // Refresh the file model to show the converted file
            fileManagerCtrl.refreshFileModel()
        }
        
        onConversionError: function(operationId, error) {
            progressOverlay.hide()
            errorDialog.showError(qsTr("Conversion Error"), error)
        }
        
        onProcessingChanged: {
            if (isProcessing && !progressOverlay.visible) {
                progressOverlay.show(qsTr("Processing"), qsTr("Processing media..."), -1, true)
            } else if (!isProcessing && progressOverlay.visible) {
                progressOverlay.hide()
            }
        }
        
        onThumbnailGenerated: function(videoPath, thumbnailPath) {
            console.log("Thumbnail generated for:", videoPath, "at:", thumbnailPath)
            videoPlayerComponent.thumbnailPath = thumbnailPath
        }
    }
    
    TranscriptionController {
        id: transcriptionCtrl
        
        onTranscriptionProgress: function(progress) {
            progressOverlay.updateProgress(progress, qsTr("Transcribing audio..."))
        }
        
        onTranscriptionCompleted: function(filePath, transcription) {
            progressOverlay.hide()
            statusBar.showMessage(qsTr("Transcription completed"), 3000)
        }
        
        onTranscriptionError: function(taskId, error) {
            console.log("onTranscriptionError called:", taskId, error)
            progressOverlay.hide()
            errorDialog.showError(qsTr("Transcription Error"), error)
        }
        
    }
    
    FileManagerController {
        id: fileManagerCtrl
        
        onVideoImported: function(sourcePath, destinationPath) {
            statusBar.showMessage(qsTr("Video imported: %1").arg(destinationPath), 3000)
            // Refresh the file model to show the new file
            fileManagerCtrl.refreshFileModel()
            // Optionally load the imported video
            mediaCtrl.loadLocalFile(Qt.resolvedUrl("file://" + destinationPath))
        }
        
        onVideosImported: function(importedPaths) {
            statusBar.showMessage(qsTr("%1 videos imported").arg(importedPaths.length), 3000)
            // Refresh the file model to show the new files
            fileManagerCtrl.refreshFileModel()
        }
        
        onVideoExported: function(sourcePath, destinationPath) {
            statusBar.showMessage(qsTr("Video exported: %1").arg(destinationPath), 3000)
        }
        
        onTranscriptionExported: function(outputPath, format) {
            statusBar.showMessage(qsTr("Transcription exported as %1: %2").arg(format.toUpperCase()).arg(outputPath), 3000)
        }
        
        onFileError: function(operation, path, error) {
            errorDialog.showError(qsTr("File Operation Error"), qsTr("%1 failed for %2: %3").arg(operation).arg(path).arg(error))
        }
        
        onOperationProgress: function(operationId, progress) {
            if (!progressOverlay.visible) {
                progressOverlay.show(qsTr("File Operation"), qsTr("Processing files..."), progress, true)
            } else {
                progressOverlay.updateProgress(progress, qsTr("Processing files..."))
            }
        }
        
        onOperationCompleted: function(operationId, result) {
            if (fileManagerCtrl.activeOperationsCount === 0) {
                progressOverlay.hide()
            }
        }
        
        onOperationFailed: function(operationId, error) {
            progressOverlay.hide()
            errorDialog.showError(qsTr("File Operation Failed"), error)
        }
    }
    
    // Main content area with sidebar
    SplitView {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statusBar.top
        anchors.margins: 16
        orientation: Qt.Horizontal
        
        // Left sidebar
        Rectangle {
            SplitView.minimumWidth: 250
            SplitView.preferredWidth: 300
            SplitView.maximumWidth: 500
            SplitView.fillHeight: true
            color: "#2d2d2d"
            radius: 8
            border.color: "#444444"
            border.width: 1
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 16
                
                // Torrents section
                GroupBox {
                    title: qsTr("Torrents")
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 12
                        
                        // Statistics
                        Rectangle {
                            Layout.fillWidth: true
                            height: 32
                            color: "#3d3d3d"
                            radius: 6
                            border.color: "#555555"
                            border.width: 1
                            
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 16
                                
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: parent.height
                                    color: "transparent"
                                    
                                    RowLayout {
                                        anchors.centerIn: parent
                                        spacing: 4
                                        
                                        Rectangle {
                                            width: 8
                                            height: 8
                                            radius: 4
                                            color: "#4CAF50"
                                        }
                                        
                                        Text {
                                            text: qsTr("Active: %1").arg(torrentCtrl.activeTorrentsCount)
                                            color: "#ffffff"
                                            font.pointSize: 9
                                            font.weight: Font.Medium
                                        }
                                    }
                                }
                                
                                Rectangle {
                                    width: 1
                                    height: parent.height - 8
                                    color: "#555555"
                                }
                                
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: parent.height
                                    color: "transparent"
                                    
                                    RowLayout {
                                        anchors.centerIn: parent
                                        spacing: 4
                                        
                                        Rectangle {
                                            width: 8
                                            height: 8
                                            radius: 4
                                            color: "#FF9800"
                                        }
                                        
                                        Text {
                                            text: qsTr("Seeding: %1").arg(torrentCtrl.seedingTorrentsCount)
                                            color: "#ffffff"
                                            font.pointSize: 9
                                            font.weight: Font.Medium
                                        }
                                    }
                                }
                            }
                        }
                        
                        // Add torrent controls
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            
                            TextField {
                                id: magnetField
                                Layout.fillWidth: true
                                placeholderText: qsTr("Paste magnet URI or drop torrent file...")
                                enabled: !torrentCtrl.isBusy
                                
                                background: Rectangle {
                                    color: magnetField.enabled ? "#3d3d3d" : "#2a2a2a"
                                    border.color: magnetField.activeFocus ? "#4CAF50" : "#555555"
                                    border.width: magnetField.activeFocus ? 2 : 1
                                    radius: 6
                                }
                                
                                color: "#ffffff"
                                placeholderTextColor: "#999999"
                                font.pointSize: 10
                                selectByMouse: true
                                
                                Keys.onReturnPressed: addTorrentButton.clicked()
                            }
                            
                            Button {
                                id: addTorrentButton
                                text: qsTr("Add")
                                enabled: !torrentCtrl.isBusy && magnetField.text.length > 0
                                icon.source: "qrc:/qt/qml/Murmur/resources/images/open_new.svg"
                                icon.width: 16
                                icon.height: 16
                                
                                background: Rectangle {
                                    color: parent.enabled ? (parent.pressed ? "#45a049" : "#4CAF50") : "#666666"
                                    radius: 6
                                    border.color: parent.enabled ? "#45a049" : "#555555"
                                    border.width: 1
                                }
                                
                                contentItem: RowLayout {
                                    spacing: 4
                                    
                                    Image {
                                        source: addTorrentButton.icon.source
                                        width: addTorrentButton.icon.width
                                        height: addTorrentButton.icon.height
                                        visible: addTorrentButton.icon.source != ""
                                    }
                                    
                                    Text {
                                        text: addTorrentButton.text
                                        color: addTorrentButton.enabled ? "#ffffff" : "#999999"
                                        font.pointSize: 10
                                        font.weight: Font.Medium
                                    }
                                }
                                
                                width: 80
                                height: 36
                            
                            onClicked: {
                                // Check if all controllers are ready
                                if (!appController || !appController.isInitialized) {
                                    errorDialog.showError(qsTr("Controller Error"), qsTr("App controller is not ready. Please wait for initialization to complete."))
                                    return
                                }
                                
                                if (!torrentCtrl.isReady) {
                                    errorDialog.showError(qsTr("Controller Error"), qsTr("Torrent controller is not ready. Please wait for initialization to complete."))
                                    return
                                }
                                
                                if (magnetField.text.startsWith("magnet:")) {
                                    console.log("Adding magnet link:", magnetField.text)
                                    torrentCtrl.addTorrent(magnetField.text)
                                } else if (magnetField.text.length > 0) {
                                    // Handle file path
                                    console.log("Adding torrent file:", magnetField.text)
                                    // Convert to QUrl format
                                    var fileUrl = "file://" + magnetField.text
                                    torrentCtrl.addTorrentFromFile(fileUrl)
                                } else {
                                    // Show file dialog
                                    fileDialog.open()
                                }
                                magnetField.clear()
                            }
                        }
                        }
                        
                        // Torrent list
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            
                            background: Rectangle {
                                color: "#2a2a2a"
                                radius: 6
                                border.color: "#444444"
                                border.width: 1
                            }
                            
                            ListView {
                                id: torrentList
                                model: torrentCtrl.torrentModel
                                spacing: 2
                                
                                delegate: TorrentItem {
                                    width: torrentList.width - 4
                                    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
                                    torrentInfo: model
                                    
                                    onRemoveRequested: function(infoHash) {
                                        torrentCtrl.removeTorrent(infoHash)
                                    }
                                    onPauseRequested: function(infoHash) {
                                        torrentCtrl.pauseTorrent(infoHash)
                                    }
                                    onResumeRequested: function(infoHash) {
                                        torrentCtrl.resumeTorrent(infoHash)
                                    }
                                    onSelected: function(infoHash) {
                                        // Load torrent for playback
                                        mediaCtrl.loadTorrent(infoHash)
                                    }
                                }
                            }
                            
                            // Empty state
                            Rectangle {
                                anchors.centerIn: parent
                                width: Math.min(parent.width - 32, 250)
                                height: 120
                                color: "transparent"
                                visible: torrentList.count === 0
                                
                                ColumnLayout {
                                    anchors.centerIn: parent
                                    spacing: 12
                                    
                                    Rectangle {
                                        Layout.alignment: Qt.AlignHCenter
                                        width: 48
                                        height: 48
                                        radius: 24
                                        color: "#3d3d3d"
                                        border.color: "#555555"
                                        border.width: 1
                                        
                                        Text {
                                            anchors.centerIn: parent
                                            text: "📁"
                                            font.pixelSize: 24
                                        }
                                    }
                                    
                                    Text {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: qsTr("No torrents yet")
                                        font.pointSize: 12
                                        font.weight: Font.Medium
                                        color: "#ffffff"
                                    }
                                    
                                    Text {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: qsTr("Add a magnet URI or torrent file to get started")
                                        font.pointSize: 10
                                        color: "#aaaaaa"
                                        horizontalAlignment: Text.AlignHCenter
                                        wrapMode: Text.WordWrap
                                        Layout.maximumWidth: 200
                                    }
                                }
                            }
                        }
                        
                        // Busy indicator
                        ProgressBar {
                            Layout.fillWidth: true
                            Layout.maximumWidth: parent.width
                            Layout.preferredHeight: 4
                            visible: torrentCtrl.isBusy
                            indeterminate: true
                            
                            background: Rectangle {
                                implicitHeight: 4
                                color: "#3d3d3d"
                                radius: 2
                            }
                            
                            contentItem: Item {
                                id: progressBackground
                                
                                Rectangle {
                                    id: progressRect
                                    width: progressBackground.width * 0.3
                                    height: progressBackground.height
                                    radius: 2
                                    color: "#4CAF50"
                                    
                                    SequentialAnimation on x {
                                        running: torrentCtrl.isBusy
                                        loops: Animation.Infinite
                                        NumberAnimation {
                                            from: -progressRect.width
                                            to: progressBackground.width
                                            duration: 1000
                                            easing.type: Easing.InOutQuad
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                
                // Files section
                GroupBox {
                    title: qsTr("Files")
                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 12
                        
                        RowLayout {
                            spacing: 8
                            Layout.fillWidth: true
                            
                            Button {
                                text: qsTr("Upload File")
                                icon.source: "qrc:/qt/qml/Murmur/resources/images/open_new.svg"
                                icon.width: 16
                                icon.height: 16
                                Layout.fillWidth: true
                                
                                background: Rectangle {
                                    color: parent.pressed ? "#357a32" : "#3d8b40"
                                    radius: 6
                                    border.color: "#45a049"
                                    border.width: 1
                                }
                                
                                contentItem: RowLayout {
                                    spacing: 6
                                    
                                    Image {
                                        source: parent.parent.icon.source
                                        width: parent.parent.icon.width
                                        height: parent.parent.icon.height
                                        Layout.alignment: Qt.AlignCenter
                                    }
                                    
                                    Text {
                                        text: parent.parent.text
                                        color: "#ffffff"
                                        font.pointSize: 9
                                        font.weight: Font.Medium
                                        Layout.alignment: Qt.AlignCenter
                                    }
                                }
                                
                                onClicked: fileOpenDialog.open()
                            }
                            
                            Button {
                                text: qsTr("Import Folder")
                                icon.source: "qrc:/qt/qml/Murmur/resources/images/open_new.svg"
                                icon.width: 16
                                icon.height: 16
                                Layout.fillWidth: true
                                
                                background: Rectangle {
                                    color: parent.pressed ? "#357a32" : "#3d8b40"
                                    radius: 6
                                    border.color: "#45a049"
                                    border.width: 1
                                }
                                
                                contentItem: RowLayout {
                                    spacing: 6
                                    
                                    Image {
                                        source: parent.parent.icon.source
                                        width: parent.parent.icon.width
                                        height: parent.parent.icon.height
                                        Layout.alignment: Qt.AlignCenter
                                    }
                                    
                                    Text {
                                        text: parent.parent.text
                                        color: "#ffffff"
                                        font.pointSize: 9
                                        font.weight: Font.Medium
                                        Layout.alignment: Qt.AlignCenter
                                    }
                                }
                                
                                onClicked: folderOpenDialog.open()
                            }
                        }
                        
                        // File list
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            
                            background: Rectangle {
                                color: "#2a2a2a"
                                radius: 6
                                border.color: "#444444"
                                border.width: 1
                            }
                            
                            ListView {
                                id: fileList
                                model: fileManagerCtrl.fileModel
                                
                                delegate: Rectangle {
                                    width: fileList.width
                                    height: 60
                                    color: mouseArea.containsMouse ? "#3d3d3d" : "transparent"
                                    radius: 4
                                    
                                    MouseArea {
                                        id: mouseArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        
                                        onClicked: {
                                            // Load file for playback
                                            mediaCtrl.loadLocalFile("file://" + modelData)
                                        }
                                    }
                                    
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 12
                                        
                                        // File icon
                                        Rectangle {
                                            width: 32
                                            height: 32
                                            color: "#4CAF50"
                                            radius: 4
                                            
                                            Text {
                                                anchors.centerIn: parent
                                                text: "🎬"
                                                font.pointSize: 16
                                                color: "#ffffff"
                                            }
                                        }
                                        
                                        // File name and info
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 4
                                            
                                            Text {
                                                Layout.fillWidth: true
                                                text: {
                                                    // Extract filename from full path
                                                    var parts = modelData.split("/");
                                                    return parts[parts.length - 1];
                                                }
                                                font.bold: true
                                                font.pointSize: 11
                                                color: "#ffffff"
                                                elide: Text.ElideRight
                                            }
                                            
                                            Text {
                                                text: "Video File"
                                                font.pointSize: 9
                                                color: "#aaaaaa"
                                            }
                                        }
                                        
                                        // Action buttons
                                        Row {
                                            spacing: 4
                                            
                                            Button {
                                                width: 28
                                                height: 28
                                                icon.source: "qrc:/qt/qml/Murmur/resources/images/play_symbol.svg"
                                                ToolTip.text: qsTr("Play")
                                                ToolTip.visible: hovered
                                                onClicked: {
                                                    // Load file for playback
                                                    mediaCtrl.loadLocalFile("file://" + modelData)
                                                }
                                            }
                                            
                                            Button {
                                                width: 28
                                                height: 28
                                                text: "🗑️"
                                                ToolTip.text: qsTr("Remove from list")
                                                ToolTip.visible: hovered
                                                onClicked: {
                                                    deleteConfirmDialog.fileToDelete = modelData
                                                    deleteConfirmDialog.open()
                                                }
                                                
                                                background: Rectangle {
                                                    color: parent.pressed ? "#c62828" : parent.hovered ? "#d32f2f" : "transparent"
                                                    radius: 4
                                                    border.color: parent.hovered ? "#f44336" : "transparent"
                                                    border.width: 1
                                                }
                                            }
                                            
                                            Button {
                                                width: 28
                                                height: 28
                                                text: "📁"
                                                ToolTip.text: qsTr("Show in folder")
                                                ToolTip.visible: hovered
                                                onClicked: {
                                                    Qt.openUrlExternally("file://" + modelData.substring(0, modelData.lastIndexOf("/")))
                                                }
                                                
                                                background: Rectangle {
                                                    color: parent.pressed ? "#424242" : parent.hovered ? "#616161" : "transparent"
                                                    radius: 4
                                                    border.color: parent.hovered ? "#757575" : "transparent"
                                                    border.width: 1
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                
                // System status section
                GroupBox {
                    title: qsTr("System Status")
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    
                    GridLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        columns: 2
                        rowSpacing: 8
                        columnSpacing: 16
                        
                        // FFmpeg status
                        Rectangle {
                            width: 20
                            height: 20
                            color: mediaCtrl.isReady ? "#4CAF50" : "#F44336"
                            radius: 10
                            
                            Text {
                                anchors.centerIn: parent
                                text: mediaCtrl.isReady ? "✓" : "✗"
                                color: "white"
                                font.bold: true
                                font.pointSize: 10
                            }
                        }
                        
                        Text {
                            text: qsTr("FFmpeg: ") + (mediaCtrl.isReady ? qsTr("Ready") : qsTr("Not ready"))
                            color: "#ffffff"
                        }
                        
                        // Whisper status
                        Rectangle {
                            width: 20
                            height: 20
                            color: transcriptionCtrl.isReady ? "#4CAF50" : "#F44336"
                            radius: 10
                            
                            Text {
                                anchors.centerIn: parent
                                text: transcriptionCtrl.isReady ? "✓" : "✗"
                                color: "white"
                                font.bold: true
                                font.pointSize: 10
                            }
                        }
                        
                        Text {
                            text: qsTr("Whisper: ") + (transcriptionCtrl.isReady ? qsTr("Ready") : qsTr("Not ready"))
                            color: "#ffffff"
                        }
                        
                        // Disk usage
                        Rectangle {
                            width: 20
                            height: 20
                            color: "#2196F3"
                            radius: 10
                            
                            Text {
                                anchors.centerIn: parent
                                text: "💾"
                                color: "white"
                                font.bold: true
                                font.pointSize: 10
                            }
                        }
                        
                        Text {
                            id: diskSpaceText
                            color: "#ffffff"
                            text: qsTr("Disk: Calculating...")
                        }
                    }
                }
            }
        }
        
        // Main content area
        Rectangle {
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            color: "#2d2d2d"
            radius: 8
            border.color: "#444444"
            border.width: 1
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 16
                
                // Video player
                VideoPlayer {
                    id: videoPlayerComponent
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 300
                    
                    // Connect to media controller's video source
                    source: mediaCtrl.currentVideoSource
                    fullscreen: window.videoFullscreen
                    transcriptionController: transcriptionCtrl
                    
                    onVideoClicked: {
                        // Handle video click events
                    }
                    
                    onFullscreenToggled: {
                        console.log("Toggling fullscreen, current state:", window.videoFullscreen)
                        window.videoFullscreen = !window.videoFullscreen
                    }
                    
                    // Connect position changes to transcription viewer
                    player.onPositionChanged: {
                        if (fullTranscriptionViewer) {
                            fullTranscriptionViewer.updateCurrentPosition(player.position)
                        }
                        
                        // Auto-save position through media controller
                        mediaCtrl.savePosition(player.position / player.duration)
                    }
                }
                
                // Tools section
                GroupBox {
                    title: qsTr("Video Tools")
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    
                    RowLayout {
                        anchors.fill: parent
                        spacing: 24
                        
                        // Conversion tools
                        ColumnLayout {
                            Layout.fillWidth: true
                            
                            Text {
                                text: qsTr("Convert Video")
                                font.bold: true
                                color: "#ffffff"
                            }
                            
                            RowLayout {
                                Layout.fillWidth: true
                                
                                Text {
                                    text: qsTr("Format:")
                                    color: "#ffffff"
                                }
                                
                                ComboBox {
                                    id: formatCombo
                                    model: [".mp4", ".webm", ".mov", ".mkv"]
                                    Layout.fillWidth: true
                                }
                                
                                Button {
                                    text: qsTr("Convert")
                                    enabled: mediaCtrl.currentVideoSource.toString().length > 0
                                    onClicked: {
                                        // Get selected format
                                        var format = formatCombo.currentText
                                        // Start conversion
                                        mediaCtrl.convertVideo(format)
                                    }
                                }
                            }
                        }
                        
                        // Transcription tools
                        ColumnLayout {
                            Layout.fillWidth: true
                            
                            Text {
                                text: qsTr("Transcribe Audio")
                                font.bold: true
                                color: "#ffffff"
                            }
                            
                            RowLayout {
                                Layout.fillWidth: true
                                
                                Text {
                                    text: qsTr("Language:")
                                    color: "#ffffff"
                                }
                                
                                ComboBox {
                                    id: languageCombo
                                    model: ["English", "Spanish", "French", "German", "Italian", "Portuguese", "Russian", "Chinese", "Japanese", "Korean"]
                                    Layout.fillWidth: true
                                }
                                
                                Button {
                                    text: qsTr("Transcribe")
                                    enabled: mediaCtrl.currentVideoSource.toString().length > 0
                                    onClicked: {
                                        // Get selected language and map to ISO code
                                        var language = languageCombo.currentText
                                        var languageMap = {
                                            "English": "en",
                                            "Spanish": "es", 
                                            "French": "fr",
                                            "German": "de",
                                            "Italian": "it",
                                            "Portuguese": "pt",
                                            "Russian": "ru",
                                            "Chinese": "zh",
                                            "Japanese": "ja",
                                            "Korean": "ko"
                                        }
                                        var isoCode = languageMap[language] || "en"
                                        console.log("Selected language:", language, "-> ISO:", isoCode)
                                        // Set the selected language
                                        transcriptionCtrl.selectedLanguage = isoCode
                                        // Start transcription
                                        transcriptionCtrl.transcribeCurrentVideo()
                                    }
                                }
                            }
                            
                            // Transcription status
                            RowLayout {
                                spacing: 8
                                visible: transcriptionCtrl.isTranscribing
                                
                                BusyIndicator {
                                    running: transcriptionCtrl.isTranscribing
                                }
                                
                                Text {
                                    text: {
                                        var progress = transcriptionCtrl.transcriptionProgress;
                                        var percentage = (progress !== undefined && !isNaN(progress)) ? Math.round(progress * 100) : 0;
                                        return qsTr("Transcribing...") + " " + percentage + "%";
                                    }
                                    color: "#ffffff"
                                }
                            }
                        }
                    }
                }
                
                // Transcription Panel
                GroupBox {
                    id: transcriptionPanel
                    title: qsTr("Transcription")
                    Layout.fillWidth: true
                    Layout.preferredHeight: panelCollapsed ? 160 : 400
                    Layout.minimumHeight: 160
                    Layout.maximumHeight: 600
                    
                    property bool panelCollapsed: true
                    property bool subtitleOverlayEnabled: false
                    
                    Behavior on Layout.preferredHeight {
                        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
                    }
                    
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 12
                        
                        // Control buttons row
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            
                            Button {
                                id: subtitleButton
                                text: transcriptionPanel.subtitleOverlayEnabled ? qsTr("Hide Subtitles") : qsTr("Show Subtitles")
                                enabled: transcriptionCtrl.currentTranscription && transcriptionCtrl.currentTranscription.length > 0
                                
                                onClicked: {
                                    transcriptionPanel.subtitleOverlayEnabled = !transcriptionPanel.subtitleOverlayEnabled
                                    videoPlayerComponent.setSubtitlesVisible(transcriptionPanel.subtitleOverlayEnabled)
                                }
                            }
                            
                            Item {
                                Layout.fillWidth: true
                            }
                            
                            Button {
                                id: expandCollapseButton
                                text: transcriptionPanel.panelCollapsed ? qsTr("Expand") : qsTr("Collapse")
                                onClicked: transcriptionPanel.panelCollapsed = !transcriptionPanel.panelCollapsed
                            }
                        }
                        
                        // Transcription content
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            
                            // Collapsed preview
                            ScrollView {
                                id: collapsedView
                                anchors.fill: parent
                                visible: transcriptionPanel.panelCollapsed
                                clip: true
                                
                                Text {
                                    width: collapsedView.width
                                    text: {
                                        var trans = transcriptionCtrl.currentTranscription
                                        if (!trans || trans.length === 0) {
                                            return qsTr("No transcription available. Load a video and generate transcription.")
                                        }
                                        return trans
                                    }
                                    color: palette.windowText
                                    font.pointSize: 11
                                    wrapMode: Text.WordWrap
                                }
                            }
                            
                            // Expanded full viewer
                            TranscriptionViewer {
                                id: fullTranscriptionViewer
                                anchors.fill: parent
                                visible: !transcriptionPanel.panelCollapsed
                                
                                transcription: transcriptionCtrl.currentTranscription
                                segments: transcriptionCtrl.currentSegments
                                
                                onSegmentClicked: function(startTime, endTime) {
                                    if (videoPlayerComponent.player) {
                                        videoPlayerComponent.player.setPosition(startTime)
                                    }
                                }
                                
                                onExportRequested: function(format) {
                                    transcriptionCtrl.exportTranscription(format, "transcription." + format)
                                }
                            }
                        }
                        
                        // Status bar
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 30
                            color: palette.window
                            border.color: palette.mid
                            border.width: 1
                            radius: 4
                            
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 12
                                
                                Text {
                                    text: {
                                        if (!transcriptionCtrl.currentTranscription || transcriptionCtrl.currentTranscription.length === 0) {
                                            return qsTr("Ready for transcription")
                                        }
                                        return qsTr("%1 characters").arg(transcriptionCtrl.currentTranscription.length)
                                    }
                                    color: palette.windowText
                                    font.pointSize: 10
                                }
                                
                                Item {
                                    Layout.fillWidth: true
                                }
                                
                                Text {
                                    text: transcriptionPanel.subtitleOverlayEnabled ? qsTr("Subtitles: ON") : qsTr("Subtitles: OFF")
                                    color: transcriptionPanel.subtitleOverlayEnabled ? "#4CAF50" : palette.placeholderText
                                    font.pointSize: 10
                                    font.bold: transcriptionPanel.subtitleOverlayEnabled
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Status bar
        Rectangle {
            id: statusBar
            anchors.bottom: parent.bottom
            width: parent.width
            height: 24
            color: "#2d2d2d"
            border.color: "#444444"
            border.width: 1
            
            property string message: qsTr("Ready")
            
            function showMessage(text, timeout) {
                message = text
                if (timeout > 0) {
                    statusTimer.interval = timeout
                    statusTimer.restart()
                }
            }
            
            Timer {
                id: statusTimer
                interval: 3000
                onTriggered: statusBar.message = qsTr("Ready")
            }
            
            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 8
                spacing: 10
                
                Text {
                    text: statusBar.message
                    font.pointSize: 9
                    color: "#ffffff"
                }
                
                // Controller status indicators
                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    color: appController && appController.isInitialized ? "#4CAF50" : "#F44336"
                    visible: appController ? true : false
                    
                    ToolTip.text: appController && appController.isInitialized ? qsTr("App Controller Ready") : qsTr("App Controller Initializing")
                    ToolTip.visible: mouseArea.containsMouse
                    
                    MouseArea {
                        id: mouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }
                
                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    color: mediaCtrl && mediaCtrl.isReady ? "#4CAF50" : "#F44336"
                    visible: mediaCtrl ? true : false
                    
                    ToolTip.text: mediaCtrl && mediaCtrl.isReady ? qsTr("Media Controller Ready") : qsTr("Media Controller Initializing")
                    ToolTip.visible: mouseArea2.containsMouse
                    
                    MouseArea {
                        id: mouseArea2
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }
                
                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    color: fileManagerCtrl && fileManagerCtrl.isReady ? "#4CAF50" : "#F44336"
                    visible: fileManagerCtrl ? true : false
                    
                    ToolTip.text: fileManagerCtrl && fileManagerCtrl.isReady ? qsTr("File Manager Ready") : qsTr("File Manager Initializing")
                    ToolTip.visible: mouseArea3.containsMouse
                    
                    MouseArea {
                        id: mouseArea3
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }
                
                // Additional status text
                Text {
                    text: {
                        if (appController && appController.isInitialized && 
                            mediaCtrl && mediaCtrl.isReady && 
                            fileManagerCtrl && fileManagerCtrl.isReady) {
                            return qsTr("All controllers ready")
                        } else if (appController && appController.isInitialized) {
                            return qsTr("Controllers initializing...")
                        } else {
                            return qsTr("App initializing...")
                        }
                    }
                    font.pointSize: 9
                    color: "#ffffff"
                }
            }
            
            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 8
                text: qsTr("Murmur Desktop v1.0")
                font.pointSize: 9
                color: "#aaaaaa"
            }
        }
    
    // File dialog for torrent files
    FileDialog {
        id: fileDialog
        title: qsTr("Select Torrent File")
        nameFilters: [qsTr("Torrent files (*.torrent)"), qsTr("All files (*)")]
        
        onAccepted: {
            if (selectedFile) {
                torrentCtrl.addTorrentFromFile(selectedFile)
            }
        }
    }
    
    // Dialogs
    ErrorDialog {
        id: errorDialog
    }
    
    SettingsDialog {
        id: settingsDialog
        
        onSettingsChanged: {
            // Apply settings to controllers
            if (appController) {
                appController.applySettings()
            }
        }
    }
    
    AboutDialog {
        id: aboutDialog
    }
    
    Dialog {
        id: deleteConfirmDialog
        title: qsTr("Remove File")
        modal: true
        anchors.centerIn: parent
        width: 400
        height: 200
        
        property string fileToDelete: ""
        
        background: Rectangle {
            color: palette.window
            border.color: palette.mid
            border.width: 1
            radius: 8
        }
        
        contentItem: Column {
            spacing: 20
            anchors.fill: parent
            anchors.margins: 20
            
            Text {
                text: qsTr("Remove file from list?")
                font.bold: true
                font.pointSize: 14
                color: palette.windowText
                anchors.horizontalCenter: parent.horizontalCenter
            }
            
            Text {
                text: {
                    if (deleteConfirmDialog.fileToDelete) {
                        var parts = deleteConfirmDialog.fileToDelete.split("/");
                        return qsTr("This will remove \"%1\" from the file list.\nThe actual file will not be deleted.").arg(parts[parts.length - 1]);
                    }
                    return "";
                }
                color: palette.windowText
                wrapMode: Text.WordWrap
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
            }
            
            Row {
                spacing: 12
                anchors.horizontalCenter: parent.horizontalCenter
                
                Button {
                    text: qsTr("Cancel")
                    onClicked: deleteConfirmDialog.close()
                }
                
                Button {
                    text: qsTr("Remove")
                    highlighted: true
                    onClicked: {
                        if (fileManagerCtrl && deleteConfirmDialog.fileToDelete) {
                            fileManagerCtrl.removeFile(deleteConfirmDialog.fileToDelete)
                        }
                        deleteConfirmDialog.close()
                    }
                    
                    background: Rectangle {
                        color: parent.pressed ? "#c62828" : parent.hovered ? "#d32f2f" : "#f44336"
                        radius: 4
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "#ffffff"
                        font: parent.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }
    
    ProgressOverlay {
        id: progressOverlay
        anchors.fill: parent
        
        onCancelled: {
            // Cancel current operations
            mediaCtrl.cancelAllOperations()
            transcriptionCtrl.cancelTranscription()
        }
    }
    
    // File dialogs
    FileDialog {
        id: fileOpenDialog
        title: qsTr("Open Video File")
        nameFilters: [qsTr("Video files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm *.m4v)"), qsTr("All files (*)")]
        
        onAccepted: {
            if (!selectedFile) {
                console.log("No file selected");
                return;
            }
            
            console.log("File selected:", selectedFile);
            // Check if all controllers are ready before loading
            if (appController && appController.isInitialized && 
                mediaCtrl && mediaCtrl.isReady && 
                fileManagerCtrl && fileManagerCtrl.isReady) {
                console.log("Controllers are ready, loading file");
                mediaCtrl.loadLocalFile(selectedFile)
                // Also add to file manager
                var localPath = selectedFile.toString().replace("file://", "")
                console.log("Importing to file manager:", localPath);
                fileManagerCtrl.importVideo(localPath)
            } else {
                console.log("Controllers not ready, cannot load file");
                console.log("AppController ready:", appController && appController.isInitialized);
                console.log("MediaController ready:", mediaCtrl && mediaCtrl.isReady);
                console.log("FileManagerController ready:", fileManagerCtrl && fileManagerCtrl.isReady);
                
                var statusText = "";
                if (!appController || !appController.isInitialized) {
                    statusText += qsTr("App controller not ready. ");
                }
                if (!mediaCtrl || !mediaCtrl.isReady) {
                    statusText += qsTr("Media controller not ready. ");
                }
                if (!fileManagerCtrl || !fileManagerCtrl.isReady) {
                    statusText += qsTr("File manager not ready. ");
                }
                
                errorDialog.showError(qsTr("Controller Error"), 
                                    qsTr("Controllers are not ready. Please wait for initialization to complete. Current status: ") + statusText)
            }
        }
    }
    
    FolderDialog {
        id: folderOpenDialog
        title: qsTr("Import Video Folder")
        
        onAccepted: {
            if (selectedFolder) {
                var folderPath = selectedFolder.toString().replace("file://", "")
                fileManagerCtrl.importVideoDirectory(folderPath)
            }
        }
    }
    
    Dialog {
        id: magnetDialog
        title: qsTr("Add Magnet Link")
        modal: true
        anchors.centerIn: parent
        width: 500
        standardButtons: Dialog.Ok | Dialog.Cancel
        
        onAccepted: {
            if (magnetInputField.text.length > 0 && magnetInputField.text.startsWith("magnet:")) {
                torrentCtrl.addTorrent(magnetInputField.text)
                magnetInputField.clear()
            } else if (magnetInputField.text.length > 0) {
                errorDialog.showError(qsTr("Invalid Magnet Link"), qsTr("Please enter a valid magnet URI starting with 'magnet:'"))
            }
        }
        
        TextField {
            id: magnetInputField
            anchors.fill: parent
            placeholderText: qsTr("Enter magnet URI...")
        }
    }
    
    // Initialize on startup
    // Timer to update disk space
    Timer {
        id: diskSpaceTimer
        interval: 5000  // Update every 5 seconds instead of 1 second
        repeat: true
        running: true
        onTriggered: {
            if (fileManagerCtrl && fileManagerCtrl.usedSpace >= 0 && fileManagerCtrl.totalSpace > 0) {
                diskSpaceText.text = qsTr("Disk: ") + formatFileSize(fileManagerCtrl.usedSpace) + " / " + formatFileSize(fileManagerCtrl.totalSpace)
            } else {
                diskSpaceText.text = qsTr("Disk: Calculating...")
                // Force an update of disk space
                if (fileManagerCtrl) {
                    fileManagerCtrl.updateDiskSpace()
                }
            }
        }
    }
    
    // Component initialization
    Component.onCompleted: {
        console.log("Main QML component initialized");
        // Set up controllers
        if (appController) {
            console.log("AppController found, initializing");
            // Initialize core engines if not already initialized
            if (!appController.isInitialized) {
                appController.initialize();
            }
            
            // Connect to the initializationComplete signal
            appController.initializationComplete.connect(function() {
                console.log("AppController initialization complete, setting up controller dependencies");
                // Set up controller dependencies
                if (appController.torrentEngine) {
                    torrentCtrl.setTorrentEngine(appController.torrentEngine);
                    console.log("Torrent engine set");
                } else {
                    console.log("Torrent engine is null");
                }
                
                if (appController.mediaPipeline) {
                    mediaCtrl.setMediaPipeline(appController.mediaPipeline);
                    console.log("Media pipeline set");
                } else {
                    console.log("Media pipeline is null");
                }
                
                if (appController.videoPlayer) {
                    mediaCtrl.setVideoPlayer(appController.videoPlayer);
                    console.log("Video player set");
                } else {
                    console.log("Video player is null");
                }
                
                if (appController.storageManager) {
                    mediaCtrl.setStorageManager(appController.storageManager);
                    transcriptionCtrl.setStorageManager(appController.storageManager);
                    console.log("Storage manager set");
                } else {
                    console.log("Storage manager is null");
                }
                
                if (appController.whisperEngine) {
                    transcriptionCtrl.setWhisperEngine(appController.whisperEngine);
                    console.log("Whisper engine set");
                } else {
                    console.log("Whisper engine is null");
                }
                
                if (appController.fileManager) {
                    fileManagerCtrl.setFileManager(appController.fileManager);
                    console.log("File manager set");
                } else {
                    console.log("File manager is null");
                }
                
                // Connect media controller to transcription controller
                transcriptionCtrl.setMediaController(mediaCtrl);
                console.log("Media controller connected to transcription controller");
                
                // Connect to file manager signals to update UI
                fileManagerCtrl.diskSpaceChanged.connect(function() {
                    console.log("Disk space changed");
                });
                
                // Verify initialization
                console.log("Controllers initialized");
                console.log("App controller ready:", appController.isInitialized);
                console.log("Torrent engine ready:", torrentCtrl.isReady);
                console.log("Media controller ready:", mediaCtrl.isReady);
                console.log("Transcription controller ready:", transcriptionCtrl.isReady);
                console.log("File manager controller ready:", fileManagerCtrl.isReady);
            });
            
            // Also connect to the already initialized signal if it's already initialized
            if (appController.isInitialized) {
                console.log("AppController already initialized");
                // Set up controller dependencies
                if (appController.torrentEngine) {
                    torrentCtrl.setTorrentEngine(appController.torrentEngine);
                    console.log("Torrent engine set");
                } else {
                    console.log("Torrent engine is null");
                }
                
                if (appController.mediaPipeline) {
                    mediaCtrl.setMediaPipeline(appController.mediaPipeline);
                    console.log("Media pipeline set");
                } else {
                    console.log("Media pipeline is null");
                }
                
                if (appController.videoPlayer) {
                    mediaCtrl.setVideoPlayer(appController.videoPlayer);
                    console.log("Video player set");
                } else {
                    console.log("Video player is null");
                }
                
                if (appController.storageManager) {
                    mediaCtrl.setStorageManager(appController.storageManager);
                    transcriptionCtrl.setStorageManager(appController.storageManager);
                    console.log("Storage manager set");
                } else {
                    console.log("Storage manager is null");
                }
                
                if (appController.whisperEngine) {
                    transcriptionCtrl.setWhisperEngine(appController.whisperEngine);
                    console.log("Whisper engine set");
                } else {
                    console.log("Whisper engine is null");
                }
                
                if (appController.fileManager) {
                    fileManagerCtrl.setFileManager(appController.fileManager);
                    console.log("File manager set");
                } else {
                    console.log("File manager is null");
                }
                
                // Connect media controller to transcription controller
                transcriptionCtrl.setMediaController(mediaCtrl);
                console.log("Media controller connected to transcription controller");
                
                // Connect to file manager signals to update UI
                fileManagerCtrl.diskSpaceChanged.connect(function() {
                    console.log("Disk space changed");
                });
                
                // Verify initialization
                console.log("Controllers initialized");
                console.log("App controller ready:", appController.isInitialized);
                console.log("Torrent engine ready:", torrentCtrl.isReady);
                console.log("Media controller ready:", mediaCtrl.isReady);
                console.log("Transcription controller ready:", transcriptionCtrl.isReady);
                console.log("File manager controller ready:", fileManagerCtrl.isReady);
            }
        } else {
            console.log("AppController not found!");
        }
        
        // Start the disk space timer
        diskSpaceTimer.start();
    }
    
    // Utility functions
    function formatFileSize(bytes) {
        if (bytes === 0) return "0 B"
        
        var k = 1024
        var sizes = ["B", "KB", "MB", "GB", "TB"]
        var i = Math.floor(Math.log(bytes) / Math.log(k))
        
        return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + " " + sizes[i]
    }
    
    function formatDate(timestamp) {
        var date = new Date(timestamp)
        return date.toLocaleDateString() + " " + date.toLocaleTimeString([], {hour: '2-digit', minute:'2-digit'})
    }
    
    // Fullscreen video overlay
    Rectangle {
        id: fullscreenOverlay
        anchors.fill: parent
        color: "black"
        visible: window.videoFullscreen
        z: 1000 // Ensure it's above everything else
        
        VideoPlayer {
            id: fullscreenVideoPlayer
            anchors.fill: parent
            source: videoPlayerComponent.source
            fullscreen: true
            showControls: true
            
            // Sync playback position with main player
            Component.onCompleted: {
                if (videoPlayerComponent.player) {
                    player.position = videoPlayerComponent.player.position
                    player.play()
                }
            }
            
            onFullscreenToggled: {
                window.videoFullscreen = false
            }
            
            // Handle Escape key to exit fullscreen
            Keys.onEscapePressed: {
                window.videoFullscreen = false
            }
            
            focus: window.videoFullscreen
        }
        
        // Handle clicks outside video controls to exit fullscreen
        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (!fullscreenVideoPlayer.controlsVisible) {
                    window.videoFullscreen = false
                }
            }
            z: -1 // Behind the video player
        }
    }
}