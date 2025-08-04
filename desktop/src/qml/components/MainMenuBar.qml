import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs

MenuBar {
    id: menuBar
    
    signal openFile()
    signal openFolder()
    signal addMagnetLink()
    signal showSettings()
    signal showAbout()
    signal toggleFullscreen()
    signal exportTranscription(string format)
    signal clearHistory()
    signal exitApplication()
    
    property bool hasActiveVideo: false
    property bool hasTranscription: false
    
    // File menu
    Menu {
        title: qsTr("&File")
        
        Action {
            text: qsTr("&Open Video File...")
            shortcut: StandardKey.Open
            icon.source: "qrc:/qt/qml/Murmur/resources/images/open_new.svg"
            onTriggered: menuBar.openFile()
        }
        
        Action {
            text: qsTr("Open &Folder...")
            shortcut: "Ctrl+Shift+O"
            icon.source: "qrc:/qt/qml/Murmur/resources/images/open_new.svg"
            onTriggered: menuBar.openFolder()
        }
        
        MenuSeparator {}
        
        Action {
            text: qsTr("Add &Magnet Link...")
            shortcut: "Ctrl+M"
            icon.source: "qrc:/qt/qml/Murmur/resources/images/link.svg"
            onTriggered: menuBar.addMagnetLink()
        }
        
        MenuSeparator {}
        
        Menu {
            title: qsTr("&Export")
            enabled: hasTranscription
            icon.source: "qrc:/qt/qml/Murmur/resources/images/open_new.svg"
            
            Action {
                text: qsTr("Export as &SRT...")
                onTriggered: menuBar.exportTranscription("srt")
            }
            
            Action {
                text: qsTr("Export as &VTT...")
                onTriggered: menuBar.exportTranscription("vtt")
            }
            
            Action {
                text: qsTr("Export as &JSON...")
                onTriggered: menuBar.exportTranscription("json")
            }
            
            Action {
                text: qsTr("Export as &Text...")
                onTriggered: menuBar.exportTranscription("txt")
            }
        }
        
        MenuSeparator {}
        
        Action {
            text: qsTr("&Settings...")
            shortcut: StandardKey.Preferences
            icon.source: "qrc:/qt/qml/Murmur/resources/images/more.svg"
            onTriggered: menuBar.showSettings()
        }
        
        MenuSeparator {}
        
        Action {
            text: qsTr("E&xit")
            shortcut: StandardKey.Quit
            onTriggered: menuBar.exitApplication()
        }
    }
    
    // Edit menu
    Menu {
        title: qsTr("&Edit")
        
        Action {
            text: qsTr("&Copy")
            shortcut: StandardKey.Copy
            enabled: false // Will be enabled when there's selected text
        }
        
        Action {
            text: qsTr("Select &All")
            shortcut: StandardKey.SelectAll
            enabled: hasTranscription
        }
        
        MenuSeparator {}
        
        Action {
            text: qsTr("&Find...")
            shortcut: StandardKey.Find
            enabled: hasTranscription
        }
        
        Action {
            text: qsTr("Find &Next")
            shortcut: StandardKey.FindNext
            enabled: hasTranscription
        }
    }
    
    // View menu
    Menu {
        title: qsTr("&View")
        
        Action {
            text: qsTr("&Fullscreen")
            shortcut: "F11"
            enabled: hasActiveVideo
            onTriggered: menuBar.toggleFullscreen()
        }
        
        MenuSeparator {}
        
        Action {
            text: qsTr("&Torrents Panel")
            checkable: true
            checked: true
            // This would toggle the sidebar visibility
        }
        
        Action {
            text: qsTr("&Transcription Panel")
            checkable: true
            checked: true
            // This would toggle the transcription viewer visibility
        }
        
        MenuSeparator {}
        
        Action {
            text: qsTr("&Status Bar")
            checkable: true
            checked: true
            // This would toggle the status bar visibility
        }
    }
    
    // Playback menu
    Menu {
        title: qsTr("&Playback")
        enabled: hasActiveVideo
        
        Action {
            text: qsTr("&Play/Pause")
            shortcut: "Space"
        }
        
        Action {
            text: qsTr("&Stop")
            shortcut: "S"
        }
        
        MenuSeparator {}
        
        Action {
            text: qsTr("&Restart")
            shortcut: "R"
        }
        
        MenuSeparator {}
        
        Action {
            text: qsTr("Jump &Forward 10s")
            shortcut: "Right"
        }
        
        Action {
            text: qsTr("Jump &Backward 10s")
            shortcut: "Left"
        }
        
        Action {
            text: qsTr("Jump Forward &30s")
            shortcut: "Shift+Right"
        }
        
        Action {
            text: qsTr("Jump Backward 3&0s")
            shortcut: "Shift+Left"
        }
        
        MenuSeparator {}
        
        Menu {
            title: qsTr("&Speed")
            
            Action {
                text: qsTr("0.5x")
                checkable: true
                // ButtonGroup would be used to make these mutually exclusive
            }
            
            Action {
                text: qsTr("0.75x")
                checkable: true
            }
            
            Action {
                text: qsTr("1.0x (Normal)")
                checkable: true
                checked: true
            }
            
            Action {
                text: qsTr("1.25x")
                checkable: true
            }
            
            Action {
                text: qsTr("1.5x")
                checkable: true
            }
            
            Action {
                text: qsTr("2.0x")
                checkable: true
            }
        }
        
        MenuSeparator {}
        
        Menu {
            title: qsTr("&Volume")
            
            Action {
                text: qsTr("&Mute")
                shortcut: "M"
                checkable: true
            }
            
            Action {
                text: qsTr("Volume &Up")
                shortcut: "Up"
            }
            
            Action {
                text: qsTr("Volume &Down")
                shortcut: "Down"
            }
        }
    }
    
    // Tools menu
    Menu {
        title: qsTr("&Tools")
        
        Action {
            text: qsTr("&Transcribe Current Video...")
            enabled: hasActiveVideo
        }
        
        Action {
            text: qsTr("&Convert Video...")
            enabled: hasActiveVideo
        }
        
        Action {
            text: qsTr("&Extract Audio...")
            enabled: hasActiveVideo
        }
        
        Action {
            text: qsTr("Generate &Thumbnail...")
            enabled: hasActiveVideo
        }
        
        MenuSeparator {}
        
        Action {
            text: qsTr("&Clear Playback History...")
            onTriggered: menuBar.clearHistory()
        }
        
        Action {
            text: qsTr("Clear &Cache...")
        }
        
        MenuSeparator {}
        
        Action {
            text: qsTr("&Download Whisper Models...")
        }
        
        Action {
            text: qsTr("&Update Trackers...")
        }
    }
    
    // Help menu
    Menu {
        title: qsTr("&Help")
        
        Action {
            text: qsTr("&User Guide")
            shortcut: "F1"
        }
        
        Action {
            text: qsTr("&Keyboard Shortcuts")
        }
        
        MenuSeparator {}
        
        Action {
            text: qsTr("&Report Issue...")
        }
        
        Action {
            text: qsTr("Check for &Updates...")
        }
        
        MenuSeparator {}
        
        Action {
            text: qsTr("&About Murmur...")
            onTriggered: menuBar.showAbout()
        }
    }
}