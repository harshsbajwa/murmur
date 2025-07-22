import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import QtMultimedia 6.0
import Murmur 1.0

Rectangle {
    id: videoPlayerRoot
    
    property alias source: videoPlayer.source
    property alias player: videoPlayer
    property bool showControls: true
    property bool fullscreen: false
    
    signal videoClicked()
    signal fullscreenToggled()
    
    color: "#000000"
    
    // Video output
    VideoOutput {
        id: videoOutput
        anchors.fill: parent
        
        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (mouse.button === Qt.LeftButton) {
                    videoPlayerRoot.videoClicked()
                    controlsHideTimer.restart()
                }
            }
            onDoubleClicked: {
                if (mouse.button === Qt.LeftButton) {
                    videoPlayerRoot.fullscreenToggled()
                }
            }
            
            hoverEnabled: true
            onEntered: {
                if (showControls) {
                    playerControls.visible = true
                    controlsHideTimer.restart()
                }
            }
            onExited: {
                if (videoPlayer.playbackState === VideoPlayer.Playing) {
                    controlsHideTimer.restart()
                }
            }
        }
    }
    
    // Loading indicator
    BusyIndicator {
        id: loadingIndicator
        anchors.centerIn: parent
        width: 64
        height: 64
        visible: videoPlayer.mediaStatus === VideoPlayer.Loading || 
                videoPlayer.mediaStatus === VideoPlayer.Buffering
        running: visible
    }
    
    // Error message
    Rectangle {
        id: errorMessage
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 400)
        height: errorText.height + 40
        color: "#80000000"
        radius: 8
        visible: videoPlayer.playbackState === VideoPlayer.Error
        
        Text {
            id: errorText
            anchors.centerIn: parent
            width: parent.width - 20
            text: qsTr("Video playback error occurred")
            color: "#ffffff"
            font.pointSize: 12
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
        
        Button {
            anchors.top: errorText.bottom
            anchors.topMargin: 20
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Retry")
            onClicked: {
                videoPlayer.play()
            }
        }
    }
    
    // Player controls overlay
    Rectangle {
        id: playerControls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 80
        
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#00000000" }
            GradientStop { position: 1.0; color: "#80000000" }
        }
        
        visible: showControls && (controlsHideTimer.running || 
                 videoPlayer.playbackState !== VideoPlayer.Playing ||
                 bottomControlsArea.containsMouse)
        
        Behavior on opacity {
            NumberAnimation { duration: 300 }
        }
        
        MouseArea {
            id: bottomControlsArea
            anchors.fill: parent
            hoverEnabled: true
            
            onEntered: controlsHideTimer.stop()
            onExited: {
                if (videoPlayer.playbackState === VideoPlayer.Playing) {
                    controlsHideTimer.restart()
                }
            }
        }
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            
            // Progress bar
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                
                Text {
                    id: currentTimeLabel
                    text: formatTime(videoPlayer.position)
                    color: "#ffffff"
                    font.pointSize: 10
                }
                
                Slider {
                    id: progressSlider
                    Layout.fillWidth: true
                    from: 0
                    to: videoPlayer.duration
                    value: videoPlayer.position
                    
                    enabled: videoPlayer.seekable
                    
                    onMoved: {
                        videoPlayer.setPosition(value)
                    }
                    
                    // Custom slider appearance
                    background: Rectangle {
                        x: progressSlider.leftPadding
                        y: progressSlider.topPadding + progressSlider.availableHeight / 2 - height / 2
                        implicitWidth: 200
                        implicitHeight: 4
                        width: progressSlider.availableWidth
                        height: implicitHeight
                        radius: 2
                        color: "#40ffffff"
                        
                        Rectangle {
                            width: progressSlider.visualPosition * parent.width
                            height: parent.height
                            color: "#ff6b6b"
                            radius: 2
                        }
                    }
                    
                    handle: Rectangle {
                        x: progressSlider.leftPadding + progressSlider.visualPosition * (progressSlider.availableWidth - width)
                        y: progressSlider.topPadding + progressSlider.availableHeight / 2 - height / 2
                        implicitWidth: 16
                        implicitHeight: 16
                        radius: 8
                        color: progressSlider.pressed ? "#ff5252" : "#ff6b6b"
                        border.color: "#ffffff"
                        border.width: 1
                    }
                }
                
                Text {
                    id: durationLabel
                    text: formatTime(videoPlayer.duration)
                    color: "#ffffff"
                    font.pointSize: 10
                }
            }
            
            // Control buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                
                // Play/pause button
                Button {
                    id: playPauseButton
                    width: 40
                    height: 40
                    
                    background: Rectangle {
                        color: parent.pressed ? "#40ffffff" : "#20ffffff"
                        radius: 20
                        border.color: "#ffffff"
                        border.width: 1
                    }
                    
                    Text {
                        anchors.centerIn: parent
                        text: videoPlayer.playbackState === VideoPlayer.Playing ? "⏸" : "▶"
                        color: "#ffffff"
                        font.pointSize: 16
                    }
                    
                    onClicked: {
                        videoPlayer.togglePlayPause()
                        controlsHideTimer.restart()
                    }
                }
                
                // Seek backward/forward buttons
                Button {
                    width: 32
                    height: 32
                    text: "⏮"
                    
                    ToolTip.text: qsTr("Seek backward 30 seconds")
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    
                    background: Rectangle {
                        color: parent.pressed ? "#40ffffff" : parent.hovered ? "#20ffffff" : "transparent"
                        radius: 16
                        border.color: parent.hovered ? "#60ffffff" : "transparent"
                        border.width: 1
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "#ffffff"
                        font.pointSize: 14
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    onClicked: {
                        videoPlayer.seekBackward(30000) // 30 seconds
                        controlsHideTimer.restart()
                    }
                }
                
                Button {
                    width: 32
                    height: 32
                    text: "⏭"
                    
                    ToolTip.text: qsTr("Seek forward 30 seconds")
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    
                    background: Rectangle {
                        color: parent.pressed ? "#40ffffff" : parent.hovered ? "#20ffffff" : "transparent"
                        radius: 16
                        border.color: parent.hovered ? "#60ffffff" : "transparent"
                        border.width: 1
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "#ffffff"
                        font.pointSize: 14
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    onClicked: {
                        videoPlayer.seekForward(30000) // 30 seconds
                        controlsHideTimer.restart()
                    }
                }
                
                // Audio track selector
                Button {
                    width: 32
                    height: 32
                    text: "🔊"
                    
                    background: Rectangle {
                        color: parent.pressed ? "#40ffffff" : "transparent"
                        radius: 16
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "#ffffff"
                        font.pointSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    onClicked: {
                        audioTrackMenu.visible = !audioTrackMenu.visible
                        controlsHideTimer.restart()
                    }
                }
                
                // Subtitle selector
                Button {
                    width: 32
                    height: 32
                    text: "CC"
                    
                    background: Rectangle {
                        color: parent.pressed ? "#40ffffff" : "transparent"
                        radius: 16
                        border.color: videoPlayer.currentSubtitleTrack >= 0 ? "#ffff00" : "transparent"
                        border.width: 2
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: videoPlayer.currentSubtitleTrack >= 0 ? "#ffff00" : "#ffffff"
                        font.pointSize: 10
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    onClicked: {
                        subtitleMenu.visible = !subtitleMenu.visible
                        controlsHideTimer.restart()
                    }
                }
                
                // Spacer
                Item {
                    Layout.fillWidth: true
                }
                
                // Speed control
                ComboBox {
                    id: speedComboBox
                    width: 80
                    height: 32
                    
                    model: ["0.25x", "0.5x", "0.75x", "1x", "1.25x", "1.5x", "2x"]
                    currentIndex: 3 // 1x
                    
                    background: Rectangle {
                        color: "#20ffffff"
                        border.color: "#ffffff"
                        border.width: 1
                        radius: 4
                    }
                    
                    contentItem: Text {
                        text: speedComboBox.displayText
                        color: "#ffffff"
                        font.pointSize: 10
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 8
                    }
                    
                    onCurrentIndexChanged: {
                        var speeds = [0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0]
                        if (currentIndex >= 0 && currentIndex < speeds.length) {
                            videoPlayer.setPlaybackRate(speeds[currentIndex])
                        }
                    }
                }
                
                // Volume control
                RowLayout {
                    spacing: 8
                    
                    Button {
                        width: 32
                        height: 32
                        
                        background: Rectangle {
                            color: parent.pressed ? "#40ffffff" : "transparent"
                            radius: 16
                        }
                        
                        contentItem: Text {
                            text: videoPlayer.muted ? "🔇" : (videoPlayer.volume > 50 ? "🔊" : "🔉")
                            color: "#ffffff"
                            font.pointSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        
                        onClicked: {
                            videoPlayer.toggleMute()
                            controlsHideTimer.restart()
                        }
                    }
                    
                    Slider {
                        id: volumeSlider
                        width: 80
                        height: 32
                        from: 0
                        to: 100
                        value: videoPlayer.audioOutput.volume * 100
                        
                        onMoved: {
                            videoPlayer.audioOutput.volume = value / 100
                        }
                        
                        background: Rectangle {
                            x: volumeSlider.leftPadding
                            y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                            implicitWidth: 200
                            implicitHeight: 4
                            width: volumeSlider.availableWidth
                            height: implicitHeight
                            radius: 2
                            color: "#40ffffff"
                            
                            Rectangle {
                                width: volumeSlider.visualPosition * parent.width
                                height: parent.height
                                color: "#ffffff"
                                radius: 2
                            }
                        }
                        
                        handle: Rectangle {
                            x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                            y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                            implicitWidth: 12
                            implicitHeight: 12
                            radius: 6
                            color: "#ffffff"
                            border.color: "#cccccc"
                            border.width: 1
                        }
                    }
                }
                
                // Fullscreen button
                Button {
                    width: 32
                    height: 32
                    
                    background: Rectangle {
                        color: parent.pressed ? "#40ffffff" : "transparent"
                        radius: 16
                    }
                    
                    contentItem: Text {
                        text: fullscreen ? "⛶" : "⛶"
                        color: "#ffffff"
                        font.pointSize: 14
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    onClicked: {
                        videoPlayerRoot.fullscreenToggled()
                        controlsHideTimer.restart()
                    }
                }
            }
        }
    }
    
    // Buffering progress
    Rectangle {
        anchors.bottom: playerControls.visible ? playerControls.top : parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 12
        height: 2
        color: "#40ffffff"
        visible: videoPlayer.mediaStatus === VideoPlayer.Buffering
        
        Rectangle {
            width: parent.width * (videoPlayer.bufferProgress || 0)
            height: parent.height
            color: "#ff6b6b"
        }
    }
    
    // Auto-hide timer for controls
    Timer {
        id: controlsHideTimer
        interval: 3000
        repeat: false
    }
    
    // Keyboard shortcuts
    focus: true
    Keys.onSpacePressed: {
        videoPlayer.togglePlayPause()
        controlsHideTimer.restart()
    }
    
    Keys.onLeftPressed: {
        videoPlayer.seekBackward(5000) // 5 seconds
        controlsHideTimer.restart()
    }
    
    Keys.onRightPressed: {
        videoPlayer.seekForward(5000) // 5 seconds
        controlsHideTimer.restart()
    }
    
    Keys.onUpPressed: {
        videoPlayer.increaseVolume(10)
        controlsHideTimer.restart()
    }
    
    Keys.onDownPressed: {
        videoPlayer.decreaseVolume(10)
        controlsHideTimer.restart()
    }
    
    Keys.onPressed: {
        if (event.key === Qt.Key_F) {
            videoPlayerRoot.fullscreenToggled()
            event.accepted = true
        } else if (event.key === Qt.Key_M) {
            videoPlayer.toggleMute()
            event.accepted = true
        }
    }
    
    // Helper functions
    function formatTime(milliseconds) {
        if (isNaN(milliseconds) || milliseconds < 0) {
            return "0:00"
        }
        
        var seconds = Math.floor(milliseconds / 1000)
        var minutes = Math.floor(seconds / 60)
        var hours = Math.floor(minutes / 60)
        
        seconds = seconds % 60
        minutes = minutes % 60
        
        var timeString = ""
        if (hours > 0) {
            timeString += hours + ":"
            timeString += (minutes < 10 ? "0" : "") + minutes + ":"
        } else {
            timeString += minutes + ":"
        }
        timeString += (seconds < 10 ? "0" : "") + seconds
        
        return timeString
    }
    
    // Audio track selection menu
    Rectangle {
        id: audioTrackMenu
        anchors.bottom: playerControls.top
        anchors.right: parent.right
        anchors.margins: 12
        width: 200
        height: Math.min(audioTrackList.contentHeight + 20, 300)
        color: "#cc000000"
        radius: 8
        border.color: "#ffffff"
        border.width: 1
        visible: false
        
        Column {
            anchors.fill: parent
            anchors.margins: 10
            
            Text {
                text: qsTr("Audio Tracks")
                color: "#ffffff"
                font.bold: true
                font.pointSize: 12
            }
            
            ListView {
                id: audioTrackList
                width: parent.width
                height: parent.height - 30
                model: videoPlayer.audioTracks
                
                delegate: Rectangle {
                    width: audioTrackList.width
                    height: 30
                    color: index === videoPlayer.currentAudioTrack ? "#40ffffff" : "transparent"
                    
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 5
                        text: modelData.title || qsTr("Track %1").arg(index + 1)
                        color: "#ffffff"
                        font.pointSize: 10
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            videoPlayer.setAudioTrack(index)
                            audioTrackMenu.visible = false
                        }
                    }
                }
            }
        }
    }
    
    // Subtitle selection menu
    Rectangle {
        id: subtitleMenu
        anchors.bottom: playerControls.top
        anchors.right: parent.right
        anchors.rightMargin: 50
        anchors.bottomMargin: 12
        width: 220
        height: Math.min(subtitleList.contentHeight + 20, 300)
        color: "#cc000000"
        radius: 8
        border.color: "#ffffff"
        border.width: 1
        visible: false
        
        Column {
            anchors.fill: parent
            anchors.margins: 10
            
            Text {
                text: qsTr("Subtitles")
                color: "#ffffff"
                font.bold: true
                font.pointSize: 12
            }
            
            ListView {
                id: subtitleList
                width: parent.width
                height: parent.height - 30
                
                model: ListModel {
                    ListElement { title: "Off"; trackId: -1 }
                }
                
                Component.onCompleted: {
                    // Add subtitle tracks from videoPlayer
                    for (var i = 0; i < videoPlayer.subtitleTracks.length; i++) {
                        var track = videoPlayer.subtitleTracks[i]
                        model.append({
                            title: track.title || qsTr("Subtitle %1").arg(i + 1),
                            trackId: i
                        })
                    }
                }
                
                delegate: Rectangle {
                    width: subtitleList.width
                    height: 30
                    color: trackId === videoPlayer.currentSubtitleTrack ? "#40ffffff" : "transparent"
                    
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 5
                        text: title
                        color: "#ffffff"
                        font.pointSize: 10
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            videoPlayer.setSubtitleTrack(trackId)
                            subtitleMenu.visible = false
                        }
                    }
                }
            }
            
            Rectangle {
                width: parent.width
                height: 1
                color: "#40ffffff"
                anchors.margins: 5
            }
            
            // Load external subtitles button
            Rectangle {
                width: parent.width
                height: 30
                color: "transparent"
                
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 5
                    text: qsTr("Load External Subtitles...")
                    color: "#ffff00"
                    font.pointSize: 10
                }
                
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        subtitleFileDialog.open()
                        subtitleMenu.visible = false
                    }
                }
            }
        }
    }
    
    // Subtitle file dialog
    FileDialog {
        id: subtitleFileDialog
        title: qsTr("Select Subtitle File")
        nameFilters: [qsTr("Subtitle files (*.srt *.vtt *.ass *.ssa *.sub)"), qsTr("All files (*)")]
        
        onAccepted: {
            if (selectedFile) {
                videoPlayer.loadExternalSubtitles(selectedFile.toString().replace("file://", ""))
            }
        }
    }
    
    // Media player instance
    MediaPlayer {
        id: videoPlayer
        videoOutput: videoOutput
        audioOutput: AudioOutput {}
        
        onPlaybackStateChanged: {
            if (playbackState === MediaPlayer.PlayingState) {
                controlsHideTimer.restart()
            }
        }
        
        onErrorOccurred: function(error, errorString) {
            console.error("VideoPlayer error:", error, errorString)
        }
    }
    
    // Component initialization
    Component.onCompleted: {
        // Set focus for keyboard shortcuts
        forceActiveFocus()
    }
}