import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs

Dialog {
    id: settingsDialog
    
    title: qsTr("Settings")
    modal: true
    anchors.centerIn: parent
    width: 600
    height: 500
    
    standardButtons: Dialog.Ok | Dialog.Cancel | Dialog.Apply
    
    property alias downloadPath: downloadPathField.text
    property alias maxDownloadSpeed: maxDownloadSpeedSpinBox.value
    property alias maxUploadSpeed: maxUploadSpeedSpinBox.value
    property alias maxConnections: maxConnectionsSpinBox.value
    property alias enableDHT: enableDHTSwitch.checked
    property alias enablePEX: enablePEXSwitch.checked
    property alias enableUPnP: enableUPnPSwitch.checked
    property alias whisperModelSize: whisperModelCombo.currentText
    property alias hardwareAcceleration: hardwareAccelSwitch.checked
    property alias autoTranscribe: autoTranscribeSwitch.checked
    
    signal settingsChanged()
    
    onAccepted: {
        saveSettings()
        settingsChanged()
    }
    
    onApplied: {
        saveSettings()
        settingsChanged()
    }
    
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        
        ColumnLayout {
            width: parent.width
            spacing: 16
            
            // General Settings
            GroupBox {
                Layout.fillWidth: true
                title: qsTr("General")
                
                GridLayout {
                    anchors.fill: parent
                    columns: 3
                    columnSpacing: 12
                    rowSpacing: 8
                    
                    // Download path
                    Label {
                        text: qsTr("Download path:")
                        Layout.alignment: Qt.AlignVCenter
                    }
                    
                    TextField {
                        id: downloadPathField
                        Layout.fillWidth: true
                        readOnly: true
                        placeholderText: qsTr("Select download directory...")
                    }
                    
                    Button {
                        text: qsTr("Browse...")
                        onClicked: downloadPathDialog.open()
                    }
                }
            }
            
            // BitTorrent Settings
            GroupBox {
                Layout.fillWidth: true
                title: qsTr("BitTorrent")
                
                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    
                    // Speed limits
                    Label {
                        text: qsTr("Max download speed (KB/s):")
                    }
                    SpinBox {
                        id: maxDownloadSpeedSpinBox
                        from: 0
                        to: 999999
                        value: 0
                        textFromValue: function(value) {
                            return value === 0 ? qsTr("Unlimited") : value.toString()
                        }
                    }
                    
                    Label {
                        text: qsTr("Max upload speed (KB/s):")
                    }
                    SpinBox {
                        id: maxUploadSpeedSpinBox
                        from: 0
                        to: 999999
                        value: 0
                        textFromValue: function(value) {
                            return value === 0 ? qsTr("Unlimited") : value.toString()
                        }
                    }
                    
                    Label {
                        text: qsTr("Max connections:")
                    }
                    SpinBox {
                        id: maxConnectionsSpinBox
                        from: 1
                        to: 9999
                        value: 200
                    }
                    
                    // Protocol options
                    Label {
                        text: qsTr("Enable DHT:")
                    }
                    Switch {
                        id: enableDHTSwitch
                        checked: true
                    }
                    
                    Label {
                        text: qsTr("Enable PEX:")
                    }
                    Switch {
                        id: enablePEXSwitch
                        checked: true
                    }
                    
                    Label {
                        text: qsTr("Enable UPnP:")
                    }
                    Switch {
                        id: enableUPnPSwitch
                        checked: true
                    }
                }
            }
            
            // Media & Transcription Settings
            GroupBox {
                Layout.fillWidth: true
                title: qsTr("Media & Transcription")
                
                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    
                    Label {
                        text: qsTr("Whisper model size:")
                    }
                    ComboBox {
                        id: whisperModelCombo
                        model: ["tiny", "base", "small", "medium", "large"]
                        currentIndex: 1 // Default to "base"
                    }
                    
                    Label {
                        text: qsTr("Hardware acceleration:")
                    }
                    Switch {
                        id: hardwareAccelSwitch
                        checked: true
                    }
                    
                    Label {
                        text: qsTr("Hardware acceleration API:")
                    }
                    ComboBox {
                        id: hardwareAPICombo
                        model: [qsTr("Auto"), qsTr("VideoToolbox (macOS)"), qsTr("VA-API (Linux)"), qsTr("VDPAU (Linux)"), qsTr("DirectX (Windows)"), qsTr("DXVA (Windows)")]
                        currentIndex: 0
                        enabled: hardwareAccelSwitch.checked
                    }
                    
                    Label {
                        text: qsTr("Auto-transcribe videos:")
                    }
                    Switch {
                        id: autoTranscribeSwitch
                        checked: false
                    }
                    
                    Label {
                        text: qsTr("Transcription language:")
                    }
                    ComboBox {
                        id: transcriptionLanguageCombo
                        model: [qsTr("Auto-detect"), "English", "Spanish", "French", "German", "Italian", "Portuguese", "Russian", "Chinese", "Japanese"]
                        currentIndex: 0
                    }
                }
            }
            
            // Performance Settings
            GroupBox {
                Layout.fillWidth: true
                title: qsTr("Performance")
                
                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    
                    Label {
                        text: qsTr("Video cache size (MB):")
                    }
                    SpinBox {
                        id: videoCacheSizeSpinBox
                        from: 100
                        to: 10000
                        value: 1000
                        stepSize: 100
                    }
                    
                    Label {
                        text: qsTr("Thumbnail cache size (MB):")
                    }
                    SpinBox {
                        id: thumbnailCacheSizeSpinBox
                        from: 10
                        to: 1000
                        value: 100
                        stepSize: 10
                    }
                    
                    Label {
                        text: qsTr("Preferred GPU:")
                    }
                    ComboBox {
                        id: preferredGPUCombo
                        model: gpuModel
                        textRole: "name"
                        valueRole: "name"
                        currentIndex: 0
                        
                        property var gpuModel: ListModel {
                            ListElement { name: "Auto (System Default)" }
                        }
                        
                        Component.onCompleted: {
                            if (typeof appController !== 'undefined' && appController) {
                                // Load available GPUs from hardware accelerator
                                var gpus = appController.getAvailableGPUs()
                                for (var i = 0; i < gpus.length; i++) {
                                    gpuModel.append({name: gpus[i].name})
                                }
                            }
                        }
                    }
                    
                    Label {
                        text: qsTr("Power optimization:")
                    }
                    ComboBox {
                        id: powerOptimizationCombo
                        model: [qsTr("Auto"), qsTr("Performance"), qsTr("Battery Life")]
                        currentIndex: 0
                    }
                    
                    Label {
                        text: qsTr("FFmpeg threads:")
                    }
                    SpinBox {
                        id: ffmpegThreadsSpinBox
                        from: 0
                        to: 16
                        value: 0
                        textFromValue: function(value) {
                            return value === 0 ? qsTr("Auto") : value.toString()
                        }
                    }
                }
            }
            
            // Privacy Settings
            GroupBox {
                Layout.fillWidth: true
                title: qsTr("Privacy")
                
                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 8
                    
                    Label {
                        text: qsTr("Anonymous mode:")
                        Layout.columnSpan: 2
                        font.bold: true
                    }
                    
                    Label {
                        text: qsTr("Hide IP from trackers:")
                        Layout.leftMargin: 20
                    }
                    Switch {
                        id: anonymousModeSwitch
                        checked: false
                    }
                    
                    Label {
                        text: qsTr("Clear playback history on exit:")
                        Layout.leftMargin: 20
                    }
                    Switch {
                        id: clearHistorySwitch
                        checked: false
                    }
                }
            }
        }
    }
    
    // File dialogs
    FolderDialog {
        id: downloadPathDialog
        title: qsTr("Select Download Directory")
        
        onAccepted: {
            downloadPathField.text = selectedFolder.toString().replace("file://", "")
        }
    }
    
    function loadSettings() {
        // Load settings from application config
        if (typeof appController !== 'undefined' && appController) {
            // This would load from the actual settings system
            downloadPathField.text = appController.getSetting("downloadPath", "")
            maxDownloadSpeedSpinBox.value = appController.getSetting("maxDownloadSpeed", 0)
            maxUploadSpeedSpinBox.value = appController.getSetting("maxUploadSpeed", 0)
            maxConnectionsSpinBox.value = appController.getSetting("maxConnections", 200)
            enableDHTSwitch.checked = appController.getSetting("enableDHT", true)
            enablePEXSwitch.checked = appController.getSetting("enablePEX", true)
            enableUPnPSwitch.checked = appController.getSetting("enableUPnP", true)
            whisperModelCombo.currentIndex = Math.max(0, whisperModelCombo.model.indexOf(
                appController.getSetting("whisperModel", "base")))
            hardwareAccelSwitch.checked = appController.getSetting("hardwareAcceleration", true)
            hardwareAPICombo.currentIndex = appController.getSetting("hardwareAPI", 0)
            autoTranscribeSwitch.checked = appController.getSetting("autoTranscribe", false)
            transcriptionLanguageCombo.currentIndex = appController.getSetting("transcriptionLanguage", 0)
            videoCacheSizeSpinBox.value = appController.getSetting("videoCacheSize", 1000)
            thumbnailCacheSizeSpinBox.value = appController.getSetting("thumbnailCacheSize", 100)
            powerOptimizationCombo.currentIndex = appController.getSetting("powerOptimization", 0)
            ffmpegThreadsSpinBox.value = appController.getSetting("ffmpegThreads", 0)
            anonymousModeSwitch.checked = appController.getSetting("anonymousMode", false)
            clearHistorySwitch.checked = appController.getSetting("clearHistory", false)
        }
    }
    
    function saveSettings() {
        // Save settings to application config
        if (typeof appController !== 'undefined' && appController) {
            appController.setSetting("downloadPath", downloadPathField.text)
            appController.setSetting("maxDownloadSpeed", maxDownloadSpeedSpinBox.value)
            appController.setSetting("maxUploadSpeed", maxUploadSpeedSpinBox.value)
            appController.setSetting("maxConnections", maxConnectionsSpinBox.value)
            appController.setSetting("enableDHT", enableDHTSwitch.checked)
            appController.setSetting("enablePEX", enablePEXSwitch.checked)
            appController.setSetting("enableUPnP", enableUPnPSwitch.checked)
            appController.setSetting("whisperModel", whisperModelCombo.currentText)
            appController.setSetting("hardwareAcceleration", hardwareAccelSwitch.checked)
            appController.setSetting("hardwareAPI", hardwareAPICombo.currentIndex)
            appController.setSetting("autoTranscribe", autoTranscribeSwitch.checked)
            appController.setSetting("transcriptionLanguage", transcriptionLanguageCombo.currentIndex)
            appController.setSetting("videoCacheSize", videoCacheSizeSpinBox.value)
            appController.setSetting("thumbnailCacheSize", thumbnailCacheSizeSpinBox.value)
            appController.setSetting("preferredGPU", preferredGPUCombo.currentText)
            appController.setSetting("powerOptimization", powerOptimizationCombo.currentIndex)
            appController.setSetting("ffmpegThreads", ffmpegThreadsSpinBox.value)
            appController.setSetting("anonymousMode", anonymousModeSwitch.checked)
            appController.setSetting("clearHistory", clearHistorySwitch.checked)
        }
    }
    
    Component.onCompleted: {
        loadSettings()
    }
}