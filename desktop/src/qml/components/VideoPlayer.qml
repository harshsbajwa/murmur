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
    property var mediaController: null
    property bool controlsVisible: false
    property string thumbnailPath: ""
    property bool subtitlesVisible: false
    property var transcriptionController: null
    
    signal videoClicked()
    signal fullscreenToggled()
    
    // Function to set subtitle visibility
    function setSubtitlesVisible(visible) {
        subtitlesVisible = visible
    }
    
    color: "#000000"
    
    
    // Monitor playback state to show controls when not playing
    Connections {
        target: videoPlayer
        function onPlaybackStateChanged() {
            if (videoPlayer.playbackState !== MediaPlayer.PlayingState) {
                controlsVisible = true
                controlsHideTimer.stop()
            }
        }
        function onSourceChanged() {
            // Try to find existing thumbnail when source changes
            updateThumbnailPath()
        }
    }
    
    // Function to calculate expected thumbnail path from video source
    function updateThumbnailPath() {
        if (videoPlayer.source.toString().length > 0) {
            var sourcePath = videoPlayer.source.toString()
            if (sourcePath.startsWith("file://")) {
                sourcePath = sourcePath.substring(7) // Remove file:// prefix
            }
            
            // Calculate expected thumbnail path: {directory}/{basename}_thumbnail.jpg
            var lastSlash = sourcePath.lastIndexOf("/")
            var lastDot = sourcePath.lastIndexOf(".")
            
            if (lastSlash >= 0 && lastDot > lastSlash) {
                var directory = sourcePath.substring(0, lastSlash)
                var baseName = sourcePath.substring(lastSlash + 1, lastDot)
                var expectedThumbnailPath = directory + "/" + baseName + "_thumbnail.jpg"
                
                // Check if thumbnail file exists (this is a simple heuristic)
                // In a full implementation, you might use Qt.resolvedUrl or file system API
                thumbnailPath = expectedThumbnailPath
            }
        } else {
            thumbnailPath = ""
        }
    }
    
    // Thumbnail display when video is not loaded or playing
    Image {
        id: thumbnailImage
        anchors.fill: parent
        source: thumbnailPath.length > 0 ? "file://" + thumbnailPath : ""
        fillMode: Image.PreserveAspectFit
        smooth: true
        visible: thumbnailPath.length > 0 && 
                (videoPlayer.mediaStatus === MediaPlayer.NoMedia || 
                 videoPlayer.mediaStatus === MediaPlayer.InvalidMedia ||
                 (videoPlayer.playbackState === MediaPlayer.StoppedState && videoPlayer.position === 0))
        
        // Overlay with play button
        Rectangle {
            anchors.centerIn: parent
            width: 80
            height: 80
            radius: 40
            color: "#80000000"
            visible: parent.visible
            
            Rectangle {
                anchors.centerIn: parent
                width: 0
                height: 0
                
                // Triangle play button shape
                Canvas {
                    id: playIcon
                    anchors.centerIn: parent
                    width: 32
                    height: 32
                    
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.fillStyle = "#ffffff"
                        ctx.beginPath()
                        ctx.moveTo(8, 6)
                        ctx.lineTo(24, 16)
                        ctx.lineTo(8, 26)
                        ctx.closePath()
                        ctx.fill()
                    }
                }
            }
        }
        
        MouseArea {
            anchors.fill: parent
            onClicked: {
                // Toggle play/pause on thumbnail/overlay click
                if (videoPlayer.playbackState === MediaPlayer.PlayingState) {
                    videoPlayer.pause()
                } else if (videoPlayer.source.toString().length > 0) {
                    videoPlayer.play()
                }
                videoPlayerRoot.videoClicked()
            }
            onDoubleClicked: function(mouse) {
                if (mouse.button === Qt.LeftButton) {
                    videoPlayerRoot.fullscreenToggled()
                }
            }
        }
    }

    // Video output
    VideoOutput {
        id: videoOutput
        anchors.fill: parent
        
        MouseArea {
            anchors.fill: parent
            onClicked: function(mouse) {
                if (mouse.button === Qt.LeftButton) {
                    // Toggle play/pause on video click
                    if (videoPlayer.playbackState === MediaPlayer.PlayingState) {
                        videoPlayer.pause()
                    } else if (videoPlayer.source.toString().length > 0) {
                        videoPlayer.play()
                    }
                    videoPlayerRoot.videoClicked()
                    controlsHideTimer.restart()
                }
            }
            onDoubleClicked: function(mouse) {
                if (mouse.button === Qt.LeftButton) {
                    videoPlayerRoot.fullscreenToggled()
                }
            }
            
            hoverEnabled: true
            onEntered: {
                if (showControls) {
                    controlsVisible = true
                    controlsHideTimer.restart()
                }
            }
            onExited: {
                if (videoPlayer.playbackState === MediaPlayer.PlayingState) {
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
        visible: videoPlayer.mediaStatus === MediaPlayer.Loading || 
                videoPlayer.mediaStatus === MediaPlayer.Buffering
        running: visible
    }
    
    // Subtitle overlay
    Rectangle {
        id: subtitleOverlay
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: playerControls.visible ? playerControls.top : parent.bottom
        anchors.bottomMargin: 20
        height: subtitleText.height + 20
        
        color: "#80000000"
        radius: 8
        visible: subtitlesVisible && subtitleText.text.length > 0
        
        Text {
            id: subtitleText
            anchors.centerIn: parent
            width: parent.width - 40
            text: ""
            color: "#ffffff"
            font.pointSize: 16
            font.bold: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            
            // Outline effect for better readability
            style: Text.Outline
            styleColor: "#000000"
            
            Component.onCompleted: {
                if (subtitlesVisible) {
                    text = getCurrentSubtitleText()
                }
            }
        }
        
        Behavior on opacity {
            NumberAnimation { duration: 200 }
        }
    }
    
    // Error message
    Rectangle {
        id: errorMessage
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 400)
        height: errorText.height + 40
        color: "#80000000"
        radius: 8
        visible: videoPlayer.playbackState === MediaPlayer.ErrorState
        
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
        
        visible: showControls && controlsVisible
        
        Behavior on opacity {
            NumberAnimation { duration: 300 }
        }
        
        MouseArea {
            id: bottomControlsArea
            anchors.fill: parent
            hoverEnabled: true
            
            onEntered: {
                controlsVisible = true
                controlsHideTimer.stop()
            }
            onExited: {
                if (videoPlayer.playbackState === MediaPlayer.PlayingState) {
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
                    to: videoPlayer.duration > 0 ? videoPlayer.duration : 100
                    value: videoPlayer.position
                    
                    enabled: videoPlayer.seekable && videoPlayer.duration > 0
                    
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
                RoundButton {
                    id: playPauseButton
                    width: 40
                    height: 40
                    radius: 20
                    
                    icon.source: videoPlayer.playbackState === MediaPlayer.PlayingState ? 
                "qrc:/qt/qml/Murmur/resources/images/pause_symbol.svg" : "qrc:/qt/qml/Murmur/resources/images/play_symbol.svg"
                    icon.width: 24
                    icon.height: 24
                    icon.color: "#ffffff"
                    
                    onClicked: {
                        console.log("Play/pause button clicked, current state:", videoPlayer.playbackState)
                        if (videoPlayer.playbackState === MediaPlayer.PlayingState) {
                            videoPlayer.pause()
                        } else {
                            videoPlayer.play()
                        }
                        controlsHideTimer.restart()
                    }
                }
                
                // Seek backward/forward buttons
                RoundButton {
                    width: 32
                    height: 32
                    icon.source: "qrc:/qt/qml/Murmur/resources/images/backward10.svg"
                    icon.width: 24
                    icon.height: 24
                    icon.color: "#ffffff"
                    
                    ToolTip.text: qsTr("Seek backward 10 seconds")
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    
                    onClicked: {
                        videoPlayer.setPosition(Math.max(0, videoPlayer.position - 10000)) // 10 seconds
                        controlsHideTimer.restart()
                    }
                }
                
                RoundButton {
                    width: 32
                    height: 32
                    icon.source: "qrc:/qt/qml/Murmur/resources/images/forward10.svg"
                    icon.width: 24
                    icon.height: 24
                    icon.color: "#ffffff"
                    
                    ToolTip.text: qsTr("Seek forward 10 seconds")
                    ToolTip.visible: hovered
                    ToolTip.delay: 500
                    
                    onClicked: {
                        videoPlayer.setPosition(Math.min(videoPlayer.duration, videoPlayer.position + 10000)) // 10 seconds
                        controlsHideTimer.restart()
                    }
                }
                
                // Audio track selector
                RoundButton {
                    width: 32
                    height: 32
                    icon.source: "qrc:/qt/qml/Murmur/resources/images/more.svg"
                    icon.width: 24
                    icon.height: 24
                    icon.color: "#ffffff"
                    
                    onClicked: {
                        audioTrackMenu.visible = !audioTrackMenu.visible
                        controlsHideTimer.restart()
                    }
                }
                
                // Subtitle selector
                RoundButton {
                    width: 32
                    height: 32
                    text: "CC"
                    
                    background: Rectangle {
                        color: parent.pressed ? "#40ffffff" : "transparent"
                        radius: 16
                        border.color: videoPlayer.activeSubtitleTrack >= 0 ? "#ffff00" : "transparent"
                        border.width: 2
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: videoPlayer.activeSubtitleTrack >= 0 ? "#ffff00" : "#ffffff"
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
                            videoPlayer.playbackRate = speeds[currentIndex]
                        }
                    }
                }
                
                // Volume control
                RowLayout {
                    spacing: 8
                    
                    RoundButton {
                        width: 32
                        height: 32
                        
                        icon.source: "qrc:/qt/qml/Murmur/resources/images/volume.svg"
                        icon.width: 24
                        icon.height: 24
                        icon.color: "#ffffff"
                        
                        onClicked: {
                            videoPlayer.audioOutput.muted = !videoPlayer.audioOutput.muted
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
                RoundButton {
                    width: 32
                    height: 32
                    
                    icon.source: fullscreen ? "qrc:/qt/qml/Murmur/resources/images/zoom_minimize.svg" : "qrc:/qt/qml/Murmur/resources/images/zoom_maximize.svg"
                    icon.width: 24
                    icon.height: 24
                    icon.color: "#ffffff"
                    
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
        visible: videoPlayer.mediaStatus === MediaPlayer.Buffering
        
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
        onTriggered: {
            if (videoPlayer.playbackState === MediaPlayer.PlayingState && !bottomControlsArea.containsMouse) {
                controlsVisible = false
            }
        }
    }
    
    // Keyboard shortcuts
    focus: true
    Keys.onSpacePressed: {
        if (videoPlayer.playbackState === MediaPlayer.PlayingState) {
            videoPlayer.pause()
        } else {
            videoPlayer.play()
        }
        controlsHideTimer.restart()
    }
    
    Keys.onLeftPressed: {
        videoPlayer.setPosition(Math.max(0, videoPlayer.position - 5000)) // 5 seconds
        controlsHideTimer.restart()
    }
    
    Keys.onRightPressed: {
        videoPlayer.setPosition(Math.min(videoPlayer.duration, videoPlayer.position + 5000)) // 5 seconds
        controlsHideTimer.restart()
    }
    
    Keys.onUpPressed: {
        videoPlayer.audioOutput.volume = Math.min(1.0, videoPlayer.audioOutput.volume + 0.1)
        controlsHideTimer.restart()
    }
    
    Keys.onDownPressed: {
        videoPlayer.audioOutput.volume = Math.max(0.0, videoPlayer.audioOutput.volume - 0.1)
        controlsHideTimer.restart()
    }
    
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_F) {
            videoPlayerRoot.fullscreenToggled()
            event.accepted = true
        } else if (event.key === Qt.Key_M) {
            videoPlayer.audioOutput.muted = !videoPlayer.audioOutput.muted
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
                    color: index === videoPlayer.activeAudioTrack ? "#40ffffff" : "transparent"
                    
                    Text {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 5
                        text: modelData ? (modelData.name || qsTr("Track %1").arg(index + 1)) : qsTr("Track %1").arg(index + 1)
                        color: "#ffffff"
                        font.pointSize: 10
                    }
                    
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            videoPlayer.activeAudioTrack = index
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
                            title: track.name || qsTr("Subtitle %1").arg(i + 1),
                            trackId: i
                        })
                    }
                }
                
                delegate: Rectangle {
                    width: subtitleList.width
                    height: 30
                    color: trackId === videoPlayer.activeSubtitleTrack ? "#40ffffff" : "transparent"
                    
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
                            videoPlayer.activeSubtitleTrack = trackId
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
                videoPlayer.addSubtitle(selectedFile)
            }
        }
    }
    
    // Media player instance
    MediaPlayer {
        id: videoPlayer
        videoOutput: videoOutput
        audioOutput: AudioOutput {
            id: audioOutput
        }
        
        onPlaybackStateChanged: {
            if (playbackState === MediaPlayer.PlayingState) {
                controlsHideTimer.restart()
            }
        }
        
        onErrorOccurred: function(error, errorString) {
            console.error("VideoPlayer error:", error, errorString)
            // Show error message in UI
            errorText.text = qsTr("Video playback error: ") + errorString
            errorMessage.visible = true
        }
    }
    
    // Function to get current subtitle text based on video position
    function getCurrentSubtitleText() {
        if (!transcriptionController || !transcriptionController.currentSegments) {
            return ""
        }
        
        var currentTime = videoPlayer.position // in milliseconds
        var segments = transcriptionController.currentSegments
        
        // Find the segment that contains the current playback time
        for (var i = 0; i < segments.length; i++) {
            var segment = segments[i]
            if (currentTime >= segment.startTime && currentTime <= segment.endTime) {
                return segment.text
            }
        }
        
        // If no segments available, show preview of full transcription
        var trans = transcriptionController.currentTranscription
        if (typeof trans === "string" && trans.length > 0) {
            return trans.length > 80 ? trans.substring(0, 80) + "..." : trans
        }
        
        return ""
    }
    
    // Update subtitle text when position changes
    Connections {
        target: videoPlayer
        function onPositionChanged() {
            if (subtitlesVisible) {
                subtitleText.text = getCurrentSubtitleText()
            }
        }
    }
    
    // Component initialization
    Component.onCompleted: {
        // Initialize controls visibility
        controlsVisible = true
        
        // Set focus for keyboard shortcuts
        forceActiveFocus()
    }
}