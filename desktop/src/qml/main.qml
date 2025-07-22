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
    
    // Controllers
    TorrentController {
        id: torrentCtrl
        onTorrentError: errorDialog.showError("Torrent Error", error)
        onOperationCompleted: statusBar.showMessage(message, 3000)
    }
    
    MediaController {
        id: mediaCtrl
        
        onConversionProgress: function(operationId, progress) {
            progressOverlay.updateProgress(progress, qsTr("Converting video..."))
        }
        
        onConversionCompleted: function(operationId, outputPath) {
            progressOverlay.hide()
            statusBar.showMessage(qsTr("Conversion completed: %1").arg(outputPath), 5000)
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
        
        onTranscriptionError: function(error) {
            progressOverlay.hide()
            errorDialog.showError(qsTr("Transcription Error"), error)
        }
        
    }
    
    FileManagerController {
        id: fileManagerCtrl
        
        onVideoImported: function(sourcePath, destinationPath) {
            statusBar.showMessage(qsTr("Video imported: %1").arg(destinationPath), 3000)
            // Optionally load the imported video
            mediaCtrl.loadLocalFile(Qt.resolvedUrl("file://" + destinationPath))
        }
        
        onVideosImported: function(importedPaths) {
            statusBar.showMessage(qsTr("%1 videos imported").arg(importedPaths.length), 3000)
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
    
    // Main content
    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal
        
        // Sidebar with torrent list
        Rectangle {
            SplitView.minimumWidth: 250
            SplitView.preferredWidth: 300
            SplitView.maximumWidth: 500
            
            color: window.palette.window
            border.color: window.palette.mid
            border.width: 1
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8
                
                // Header
                Text {
                    text: qsTr("Torrents")
                    font.bold: true
                    font.pointSize: 12
                    Layout.fillWidth: true
                }
                
                // Statistics
                Row {
                    spacing: 16
                    Layout.fillWidth: true
                    
                    Text {
                        text: qsTr("Active: %1").arg(torrentCtrl.activeTorrentsCount)
                        font.pointSize: 10
                        color: window.palette.windowText
                    }
                    
                    Text {
                        text: qsTr("Seeding: %1").arg(torrentCtrl.seedingTorrentsCount)
                        font.pointSize: 10
                        color: window.palette.windowText
                    }
                }
                
                // Add torrent controls
                RowLayout {
                    Layout.fillWidth: true
                    
                    TextField {
                        id: magnetField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Magnet URI or torrent file...")
                        enabled: !torrentCtrl.isBusy
                        
                        Keys.onReturnPressed: addTorrentButton.clicked()
                    }
                    
                    Button {
                        id: addTorrentButton
                        text: qsTr("Add")
                        enabled: !torrentCtrl.isBusy && magnetField.text.length > 0
                        
                        onClicked: {
                            if (magnetField.text.startsWith("magnet:")) {
                                torrentCtrl.addTorrent(magnetField.text)
                            } else {
                                // Handle file path or show file dialog
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
                    
                    ListView {
                        id: torrentList
                        model: torrentCtrl.torrentModel
                        
                        delegate: TorrentItem {
                            width: torrentList.width
                            torrentInfo: model
                            
                            onRemoveRequested: torrentCtrl.removeTorrent(infoHash)
                            onPauseRequested: torrentCtrl.pauseTorrent(infoHash)
                            onResumeRequested: torrentCtrl.resumeTorrent(infoHash)
                            onSelected: {
                                // Load torrent for playback
                                // mediaCtrl.loadTorrent(infoHash)
                            }
                        }
                    }
                    
                    // Empty state
                    Label {
                        anchors.centerIn: parent
                        text: qsTr("No torrents\nAdd a magnet URI or torrent file to get started")
                        horizontalAlignment: Text.AlignHCenter
                        visible: torrentList.count === 0
                        color: window.palette.placeholderText
                    }
                }
                
                // Busy indicator
                ProgressBar {
                    Layout.fillWidth: true
                    visible: torrentCtrl.isBusy
                    indeterminate: true
                }
            }
        }
        
        // Main content area
        Rectangle {
            SplitView.fillWidth: true
            color: window.palette.base
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8
                
                // Video player
                VideoPlayer {
                    id: videoPlayerComponent
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 300
                    
                    // Connect to media controller's video source
                    source: mediaCtrl.currentVideoSource
                    
                    onVideoClicked: {
                        // Handle video click events
                    }
                    
                    onFullscreenToggled: {
                        // Handle fullscreen toggle
                        // This would require window management
                    }
                    
                    // Connect position changes to transcription viewer
                    player.onPositionChanged: {
                        transcriptionViewer.updateCurrentPosition(player.position)
                        
                        // Auto-save position through media controller
                        mediaCtrl.savePosition(player.position / player.duration)
                    }
                }
                
                // Transcription view
                TranscriptionViewer {
                    id: transcriptionViewer
                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    Layout.minimumHeight: 100
                    Layout.maximumHeight: 300
                    
                    // Connect to transcription controller
                    transcription: transcriptionCtrl.currentTranscription
                    
                    onSegmentClicked: function(startTime, endTime) {
                        // Seek video to segment start time
                        if (videoPlayerComponent.player) {
                            videoPlayerComponent.player.setPosition(startTime)
                        }
                    }
                    
                    onExportRequested: function(format) {
                        // Handle transcription export
                        transcriptionCtrl.exportTranscription(format, "transcription." + format)
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
        color: window.palette.window
        border.color: window.palette.mid
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
        
        Text {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 8
            text: statusBar.message
            font.pointSize: 9
            color: window.palette.windowText
        }
        
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: 8
            text: qsTr("Murmur Desktop v1.0")
            font.pointSize: 9
            color: window.palette.placeholderText
        }
    }
    
    // File dialog for torrent files
    FileDialog {
        id: fileDialog
        title: qsTr("Select Torrent File")
        nameFilters: [qsTr("Torrent files (*.torrent)"), qsTr("All files (*)")]
        
        onAccepted: {
            torrentCtrl.addTorrentFromFile(fileUrl)
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
            mediaCtrl.loadLocalFile(fileUrl)
        }
    }
    
    FolderDialog {
        id: folderOpenDialog
        title: qsTr("Import Video Folder")
        
        onAccepted: {
            var folderPath = selectedFolder.toString().replace("file://", "")
            fileManagerCtrl.importVideoDirectory(folderPath)
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
            if (magnetInputField.text.length > 0) {
                torrentCtrl.addTorrent(magnetInputField.text)
                magnetInputField.clear()
            }
        }
        
        TextField {
            id: magnetInputField
            anchors.fill: parent
            placeholderText: qsTr("Enter magnet URI...")
        }
    }
    
    // Initialize on startup
    Component.onCompleted: {
        // Set up controllers
        if (appController) {
            // Initialize core engines
            appController.initialize()
            
            // Set up controller dependencies
            torrentCtrl.setTorrentEngine(appController.torrentEngine)
            mediaCtrl.setMediaPipeline(appController.mediaPipeline)
            mediaCtrl.setVideoPlayer(appController.videoPlayer)
            mediaCtrl.setStorageManager(appController.storageManager)
            transcriptionCtrl.setWhisperEngine(appController.whisperEngine)
            fileManagerCtrl.setFileManager(appController.fileManager)
        }
    }
}