import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt5Compat.GraphicalEffects

Dialog {
    id: aboutDialog
    
    title: qsTr("About Murmur Desktop")
    modal: true
    anchors.centerIn: parent
    width: 500
    height: 600
    
    standardButtons: Dialog.Ok
    
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        
        ColumnLayout {
            width: parent.width
            spacing: 20
            
            // App icon and title
            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12
                
                // App icon
                Item {
                    Layout.alignment: Qt.AlignHCenter
                    width: 64
                    height: 64
                    
                    Image {
                        id: appIcon
                        anchors.fill: parent
                        source: "qrc:/icons/app.png"
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        
                        // Fallback when icon is not available
                        Rectangle {
                            anchors.fill: parent
                            radius: 12
                            color: "#2196F3"
                            visible: appIcon.status !== Image.Ready
                            
                            Text {
                                anchors.centerIn: parent
                                text: "M"
                                font.bold: true
                                font.pointSize: 32
                                color: "white"
                            }
                        }
                        
                        // Subtle shadow effect for loaded icon
                        DropShadow {
                            anchors.fill: parent
                            source: appIcon
                            radius: 8
                            samples: 16
                            color: Qt.rgba(0, 0, 0, 0.3)
                            visible: appIcon.status === Image.Ready
                        }
                    }
                }
                
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Murmur Desktop")
                    font.bold: true
                    font.pointSize: 18
                    color: palette.windowText
                }
                
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Version 1.0.0")
                    font.pointSize: 12
                    color: palette.placeholderText
                }
            }
            
            // Description
            Text {
                Layout.fillWidth: true
                text: qsTr("A desktop application for video sharing and processing through WebTorrent with AI-powered transcription.")
                font.pointSize: 11
                color: palette.windowText
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
            
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: palette.mid
            }
            
            // Features
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                
                Text {
                    text: qsTr("Key Features")
                    font.bold: true
                    font.pointSize: 12
                    color: palette.windowText
                }
                
                ColumnLayout {
                    Layout.leftMargin: 16
                    spacing: 4
                    
                    Text {
                        text: qsTr("• BitTorrent-based video sharing")
                        font.pointSize: 10
                        color: palette.windowText
                    }
                    Text {
                        text: qsTr("• AI-powered transcription with Whisper")
                        font.pointSize: 10
                        color: palette.windowText
                    }
                    Text {
                        text: qsTr("• Video conversion and processing")
                        font.pointSize: 10
                        color: palette.windowText
                    }
                    Text {
                        text: qsTr("• Cross-platform compatibility")
                        font.pointSize: 10
                        color: palette.windowText
                    }
                    Text {
                        text: qsTr("• Hardware-accelerated playback")
                        font.pointSize: 10
                        color: palette.windowText
                    }
                }
            }
            
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: palette.mid
            }
            
            // Technical information
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                
                Text {
                    text: qsTr("Built With")
                    font.bold: true
                    font.pointSize: 12
                    color: palette.windowText
                }
                
                GridLayout {
                    Layout.leftMargin: 16
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 4
                    
                    Text {
                        text: qsTr("Framework:")
                        font.pointSize: 10
                        color: palette.placeholderText
                    }
                    Text {
                        text: qsTr("Qt 6.7")
                        font.pointSize: 10
                        color: palette.windowText
                    }
                    
                    Text {
                        text: qsTr("BitTorrent:")
                        font.pointSize: 10
                        color: palette.placeholderText
                    }
                    Text {
                        text: qsTr("libtorrent 2.0")
                        font.pointSize: 10
                        color: palette.windowText
                    }
                    
                    Text {
                        text: qsTr("Media:")
                        font.pointSize: 10
                        color: palette.placeholderText
                    }
                    Text {
                        text: qsTr("FFmpeg 6.0")
                        font.pointSize: 10
                        color: palette.windowText
                    }
                    
                    Text {
                        text: qsTr("AI:")
                        font.pointSize: 10
                        color: palette.placeholderText
                    }
                    Text {
                        text: qsTr("whisper.cpp")
                        font.pointSize: 10
                        color: palette.windowText
                    }
                    
                    Text {
                        text: qsTr("Database:")
                        font.pointSize: 10
                        color: palette.placeholderText
                    }
                    Text {
                        text: qsTr("SQLite 3")
                        font.pointSize: 10
                        color: palette.windowText
                    }
                    
                    Text {
                        text: qsTr("Build:")
                        font.pointSize: 10
                        color: palette.placeholderText
                    }
                    Text {
                        text: qsTr("CMake + Conan")
                        font.pointSize: 10
                        color: palette.windowText
                    }
                }
            }
            
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: palette.mid
            }
            
            // System information
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                
                Text {
                    text: qsTr("System Information")
                    font.bold: true
                    font.pointSize: 12
                    color: palette.windowText
                }
                
                GridLayout {
                    Layout.leftMargin: 16
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 4
                    
                    Text {
                        text: qsTr("Qt Version:")
                        font.pointSize: 10
                        color: palette.placeholderText
                    }
                    Text {
                        text: Qt.platformOS + " " + Qt.platformVersion
                        font.pointSize: 10
                        color: palette.windowText
                    }
                    
                    Text {
                        text: qsTr("Platform:")
                        font.pointSize: 10
                        color: palette.placeholderText
                    }
                    Text {
                        text: qsTr("%1 (%2)").arg(Qt.platform.os).arg(Qt.platform.version)
                        font.pointSize: 10
                        color: palette.windowText
                        visible: typeof Qt.platform !== 'undefined'
                    }
                }
            }
            
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: palette.mid
            }
            
            // Copyright and license
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Copyright © 2024 Murmur Desktop Contributors")
                    font.pointSize: 10
                    color: palette.placeholderText
                    horizontalAlignment: Text.AlignHCenter
                }
                
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Licensed under the MIT License")
                    font.pointSize: 10
                    color: palette.placeholderText
                    horizontalAlignment: Text.AlignHCenter
                }
                
                // Links
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 16
                    
                    Button {
                        text: qsTr("Website")
                        flat: true
                        onClicked: Qt.openUrlExternally("https://github.com/murmur-desktop")
                    }
                    
                    Button {
                        text: qsTr("Source Code")
                        flat: true
                        onClicked: Qt.openUrlExternally("https://github.com/murmur-desktop/murmur")
                    }
                    
                    Button {
                        text: qsTr("Report Issues")
                        flat: true
                        onClicked: Qt.openUrlExternally("https://github.com/murmur-desktop/murmur/issues")
                    }
                }
            }
        }
    }
}