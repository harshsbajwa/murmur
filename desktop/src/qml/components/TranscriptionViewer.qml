import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Murmur 1.0

Rectangle {
    id: transcriptionViewer
    
    property alias transcription: transcriptionText.text
    property var segments: []
    property int currentPosition: 0
    property bool autoScroll: true
    property bool showTimestamps: true
    property bool highlightCurrent: true
    
    signal segmentClicked(int startTime, int endTime)
    signal exportRequested(string format)
    
    color: palette.base
    border.color: palette.mid
    border.width: 1
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8
        
        // Header with controls
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            
            Text {
                text: qsTr("Transcription")
                font.bold: true
                font.pointSize: 12
                color: palette.windowText
            }
            
            Item {
                Layout.fillWidth: true
            }
            
            // Auto-scroll toggle
            CheckBox {
                id: autoScrollCheck
                text: qsTr("Auto-scroll")
                checked: autoScroll
                onCheckedChanged: autoScroll = checked
            }
            
            // Show timestamps toggle
            CheckBox {
                id: timestampsCheck
                text: qsTr("Timestamps")
                checked: showTimestamps
                onCheckedChanged: showTimestamps = checked
            }
            
            // Export button
            Button {
                text: qsTr("Export")
                onClicked: exportMenu.open()
                
                Menu {
                    id: exportMenu
                    
                    MenuItem {
                        text: qsTr("Export as TXT")
                        onTriggered: transcriptionViewer.exportRequested("txt")
                    }
                    
                    MenuItem {
                        text: qsTr("Export as SRT")
                        onTriggered: transcriptionViewer.exportRequested("srt")
                    }
                    
                    MenuItem {
                        text: qsTr("Export as VTT")
                        onTriggered: transcriptionViewer.exportRequested("vtt")
                    }
                    
                    MenuItem {
                        text: qsTr("Export as JSON")
                        onTriggered: transcriptionViewer.exportRequested("json")
                    }
                }
            }
        }
        
        // Search bar
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            
            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: qsTr("Search transcription...")
                onTextChanged: {
                    if (text.length > 0) {
                        highlightSearchResults(text)
                    } else {
                        clearSearchHighlights()
                    }
                }
                
                Keys.onReturnPressed: {
                    findNext()
                }
            }
            
            Button {
                text: qsTr("⬆")
                enabled: searchField.text.length > 0
                onClicked: findPrevious()
                ToolTip.text: qsTr("Find Previous")
                ToolTip.visible: hovered
            }
            
            Button {
                text: qsTr("⬇")
                enabled: searchField.text.length > 0
                onClicked: findNext()
                ToolTip.text: qsTr("Find Next")
                ToolTip.visible: hovered
            }
        }
        
        // Transcription content
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            clip: true
            
            Flickable {
                id: transcriptionFlickable
                
                contentWidth: transcriptionColumn.width
                contentHeight: transcriptionColumn.height
                
                Column {
                    id: transcriptionColumn
                    width: transcriptionFlickable.width
                    spacing: 4
                    
                    // Segment-based display
                    Repeater {
                        id: segmentRepeater
                        model: segments.length > 0 ? segments : []
                        
                        delegate: Rectangle {
                            width: parent.width
                            height: segmentText.height + 16
                            color: isCurrentSegment() ? palette.highlight : "transparent"
                            radius: 4
                            
                            function isCurrentSegment() {
                                if (!highlightCurrent || !modelData) return false
                                return currentPosition >= modelData.startTime && 
                                       currentPosition <= modelData.endTime
                            }
                            
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (modelData) {
                                        transcriptionViewer.segmentClicked(modelData.startTime, modelData.endTime)
                                    }
                                }
                                
                                hoverEnabled: true
                                onEntered: parent.color = Qt.rgba(palette.highlight.r, palette.highlight.g, palette.highlight.b, 0.3)
                                onExited: parent.color = parent.isCurrentSegment() ? palette.highlight : "transparent"
                            }
                            
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 12
                                
                                // Timestamp
                                Text {
                                    text: showTimestamps && modelData ? formatTime(modelData.startTime) : ""
                                    color: palette.placeholderText
                                    font.pointSize: 10
                                    font.family: "monospace"
                                    visible: showTimestamps
                                    Layout.minimumWidth: 60
                                }
                                
                                // Segment text
                                Text {
                                    id: segmentText
                                    Layout.fillWidth: true
                                    text: modelData ? modelData.text : ""
                                    color: parent.parent.isCurrentSegment() ? palette.highlightedText : palette.windowText
                                    font.pointSize: 11
                                    wrapMode: Text.WordWrap
                                }
                                
                                // Confidence indicator
                                Rectangle {
                                    width: 8
                                    height: 8
                                    radius: 4
                                    color: getConfidenceColor(modelData ? modelData.confidence : 0)
                                    visible: modelData && modelData.confidence !== undefined
                                    
                                    ToolTip.text: qsTr("Confidence: %1%").arg(Math.round((modelData ? modelData.confidence : 0) * 100))
                                    ToolTip.visible: confidenceMouseArea.containsMouse
                                    
                                    MouseArea {
                                        id: confidenceMouseArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                    }
                                }
                            }
                        }
                    }
                    
                    // Fallback text display (when no segments available)
                    Text {
                        id: transcriptionText
                        width: parent.width
                        visible: segments.length === 0
                        color: palette.windowText
                        font.pointSize: 11
                        wrapMode: Text.WordWrap
                        
                        onTextChanged: {
                            if (autoScroll && text.length > 0) {
                                scrollToBottom()
                            }
                        }
                    }
                }
            }
        }
        
        // Status bar
        Rectangle {
            Layout.fillWidth: true
            height: 24
            color: palette.window
            border.color: palette.mid
            border.width: 1
            visible: segments.length > 0 || transcriptionText.text.length > 0
            
            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                
                Text {
                    text: {
                        if (segments.length > 0) {
                            return qsTr("%1 segments").arg(segments.length)
                        } else if (transcriptionText.text.length > 0) {
                            return qsTr("%1 characters").arg(transcriptionText.text.length)
                        }
                        return ""
                    }
                    color: palette.windowText
                    font.pointSize: 9
                }
                
                Item {
                    Layout.fillWidth: true
                }
                
                Text {
                    text: getCurrentSegmentInfo()
                    color: palette.placeholderText
                    font.pointSize: 9
                    visible: highlightCurrent
                }
            }
        }
    }
    
    // Empty state
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 300)
        height: emptyColumn.height + 40
        color: "transparent"
        visible: segments.length === 0 && transcriptionText.text.length === 0
        
        Column {
            id: emptyColumn
            anchors.centerIn: parent
            spacing: 16
            width: parent.width - 40
            
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "📝"
                font.pointSize: 48
                color: palette.placeholderText
            }
            
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("No transcription available")
                font.pointSize: 14
                color: palette.windowText
            }
            
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                text: qsTr("Load a video and generate transcription to see the text here.")
                font.pointSize: 11
                color: palette.placeholderText
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
    
    // Functions
    function updateCurrentPosition(positionMs) {
        currentPosition = positionMs
        
        if (autoScroll && segments.length > 0) {
            scrollToCurrentSegment()
        }
    }
    
    function scrollToCurrentSegment() {
        for (var i = 0; i < segments.length; i++) {
            var segment = segments[i]
            if (currentPosition >= segment.startTime && currentPosition <= segment.endTime) {
                var segmentY = i * (segmentRepeater.itemAt(0) ? segmentRepeater.itemAt(0).height + 4 : 50)
                transcriptionFlickable.contentY = Math.max(0, segmentY - transcriptionFlickable.height / 2)
                break
            }
        }
    }
    
    function scrollToBottom() {
        transcriptionFlickable.contentY = transcriptionFlickable.contentHeight - transcriptionFlickable.height
    }
    
    function scrollToTop() {
        transcriptionFlickable.contentY = 0
    }
    
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
    
    function getConfidenceColor(confidence) {
        if (confidence >= 0.8) return "#4CAF50"  // Green
        if (confidence >= 0.6) return "#FF9800"  // Orange
        return "#F44336"  // Red
    }
    
    function getCurrentSegmentInfo() {
        if (segments.length === 0) return ""
        
        for (var i = 0; i < segments.length; i++) {
            var segment = segments[i]
            if (currentPosition >= segment.startTime && currentPosition <= segment.endTime) {
                return qsTr("Segment %1 of %2").arg(i + 1).arg(segments.length)
            }
        }
        
        return ""
    }
    
    function highlightSearchResults(searchText) {
        // This would implement search highlighting
        // For now, just scroll to first match
        var text = transcriptionText.text.toLowerCase()
        var searchLower = searchText.toLowerCase()
        var index = text.indexOf(searchLower)
        
        if (index >= 0) {
            // Approximate scroll position based on character position
            var approximateY = (index / text.length) * transcriptionFlickable.contentHeight
            transcriptionFlickable.contentY = Math.max(0, approximateY - transcriptionFlickable.height / 2)
        }
    }
    
    function clearSearchHighlights() {
        // Clear any search highlighting
    }
    
    function findNext() {
        // Implement find next functionality
        highlightSearchResults(searchField.text)
    }
    
    function findPrevious() {
        // Implement find previous functionality
        highlightSearchResults(searchField.text)
    }
    
    function loadSegments(segmentData) {
        segments = segmentData || []
    }
    
    function exportTranscription(format) {
        var content = ""
        
        switch (format.toLowerCase()) {
            case "txt":
                content = segments.length > 0 ? 
                         segments.map(s => s.text).join("\n") : 
                         transcriptionText.text
                break
                
            case "srt":
                content = generateSRT()
                break
                
            case "vtt":
                content = generateVTT()
                break
                
            case "json":
                content = JSON.stringify({
                    segments: segments,
                    fullText: transcriptionText.text,
                    timestamp: new Date().toISOString()
                }, null, 2)
                break
        }
        
        return content
    }
    
    function generateSRT() {
        if (segments.length === 0) return ""
        
        var srt = ""
        for (var i = 0; i < segments.length; i++) {
            var segment = segments[i]
            srt += (i + 1) + "\n"
            srt += formatSRTTime(segment.startTime) + " --> " + formatSRTTime(segment.endTime) + "\n"
            srt += segment.text + "\n\n"
        }
        
        return srt
    }
    
    function generateVTT() {
        if (segments.length === 0) return ""
        
        var vtt = "WEBVTT\n\n"
        for (var i = 0; i < segments.length; i++) {
            var segment = segments[i]
            vtt += formatVTTTime(segment.startTime) + " --> " + formatVTTTime(segment.endTime) + "\n"
            vtt += segment.text + "\n\n"
        }
        
        return vtt
    }
    
    function formatSRTTime(milliseconds) {
        var totalSeconds = Math.floor(milliseconds / 1000)
        var ms = milliseconds % 1000
        var seconds = totalSeconds % 60
        var minutes = Math.floor(totalSeconds / 60) % 60
        var hours = Math.floor(totalSeconds / 3600)
        
        return String(hours).padStart(2, '0') + ":" +
               String(minutes).padStart(2, '0') + ":" +
               String(seconds).padStart(2, '0') + "," +
               String(ms).padStart(3, '0')
    }
    
    function formatVTTTime(milliseconds) {
        var totalSeconds = Math.floor(milliseconds / 1000)
        var ms = milliseconds % 1000
        var seconds = totalSeconds % 60
        var minutes = Math.floor(totalSeconds / 60) % 60
        var hours = Math.floor(totalSeconds / 3600)
        
        return String(hours).padStart(2, '0') + ":" +
               String(minutes).padStart(2, '0') + ":" +
               String(seconds).padStart(2, '0') + "." +
               String(ms).padStart(3, '0')
    }
}