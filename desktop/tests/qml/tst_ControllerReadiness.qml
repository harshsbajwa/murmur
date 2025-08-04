import QtQuick 2.15
import QtTest 1.15
import Murmur 1.0

TestCase {
    id: testCase
    name: "ControllerReadinessTest"
    
    // Reference to the test app controller
    property var appController: testAppController
    property var mediaController: testMediaController
    property var torrentController: testTorrentController
    property var transcriptionController: testTranscriptionController
    property var fileManagerController: testFileManagerController
    
    SignalSpy {
        id: appInitializedSpy
        target: appController
        signalName: "initializedChanged"
    }
    
    SignalSpy {
        id: mediaReadySpy
        target: mediaController
        signalName: "readyChanged"
    }
    
    SignalSpy {
        id: torrentReadySpy
        target: torrentController
        signalName: "readyChanged"
    }
    
    SignalSpy {
        id: transcriptionReadySpy
        target: transcriptionController
        signalName: "readyChanged"
    }
    
    SignalSpy {
        id: fileManagerReadySpy
        target: fileManagerController
        signalName: "readyChanged"
    }
    
    function init() {
        // Clear all signal spies before each test
        appInitializedSpy.clear()
        mediaReadySpy.clear()
        torrentReadySpy.clear()
        transcriptionReadySpy.clear()
        fileManagerReadySpy.clear()
    }
    
    function test_appControllerInitialization() {
        console.log("Testing AppController initialization")
        
        // Verify AppController is available
        verify(appController !== null, "AppController should be available")
        verify(appController !== undefined, "AppController should not be undefined")
        
        // Initially should not be initialized
        verify(!appController.isInitialized, "AppController should not be initialized initially")
        
        // Initialize the AppController
        console.log("Initializing AppController...")
        appController.initialize()
        
        // Wait for initialization to complete
        console.log("Waiting for AppController initialization...")
        if (!appController.isInitialized) {
            appInitializedSpy.wait(15000) // Wait up to 15 seconds
        }
        
        // Verify initialization completed
        verify(appController.isInitialized, "AppController should be initialized after waiting")
        console.log("AppController initialized successfully")
        
        // Verify engines are available
        verify(appController.torrentEngine !== null, "TorrentEngine should be available")
        verify(appController.mediaPipeline !== null, "MediaPipeline should be available")
        verify(appController.videoPlayer !== null, "VideoPlayer should be available")
        verify(appController.storageManager !== null, "StorageManager should be available")
        verify(appController.whisperEngine !== null, "WhisperEngine should be available")
        verify(appController.fileManager !== null, "FileManager should be available")
        
        console.log("All engines are available")
    }
    
    function test_controllerDependencyInjection() {
        console.log("Testing controller dependency injection")
        
        // Ensure AppController is initialized first
        if (!appController.isInitialized) {
            appController.initialize()
            appInitializedSpy.wait(15000)
        }
        
        // Test MediaController
        console.log("Setting up MediaController dependencies...")
        verify(!mediaController.isReady, "MediaController should not be ready initially")
        
        mediaController.setMediaPipeline(appController.mediaPipeline)
        mediaController.setVideoPlayer(appController.videoPlayer)
        mediaController.setStorageManager(appController.storageManager)
        
        // Wait a bit for ready state to update
        wait(100)
        
        verify(mediaController.isReady, "MediaController should be ready after setting dependencies")
        console.log("MediaController is ready")
        
        // Test TorrentController
        console.log("Setting up TorrentController dependencies...")
        verify(!torrentController.isReady, "TorrentController should not be ready initially")
        
        torrentController.setTorrentEngine(appController.torrentEngine)
        
        // Wait a bit for ready state to update
        wait(100)
        
        verify(torrentController.isReady, "TorrentController should be ready after setting dependencies")
        console.log("TorrentController is ready")
        
        // Test TranscriptionController
        console.log("Setting up TranscriptionController dependencies...")
        verify(!transcriptionController.isReady, "TranscriptionController should not be ready initially")
        
        transcriptionController.setWhisperEngine(appController.whisperEngine)
        transcriptionController.setStorageManager(appController.storageManager)
        transcriptionController.setMediaController(mediaController)
        
        // Wait a bit for ready state to update
        wait(100)
        
        verify(transcriptionController.isReady, "TranscriptionController should be ready after setting dependencies")
        console.log("TranscriptionController is ready")
        
        // Test FileManagerController
        console.log("Setting up FileManagerController dependencies...")
        verify(!fileManagerController.isReady, "FileManagerController should not be ready initially")
        
        fileManagerController.setFileManager(appController.fileManager)
        
        // Wait a bit for ready state to update
        wait(100)
        
        verify(fileManagerController.isReady, "FileManagerController should be ready after setting dependencies")
        console.log("FileManagerController is ready")
        
        console.log("All controllers are ready!")
    }
    
    function test_readySignals() {
        console.log("Testing ready signal emissions")
        
        // Ensure AppController is initialized
        if (!appController.isInitialized) {
            appController.initialize()
            appInitializedSpy.wait(15000)
        }
        
        // Test that ready signals are emitted when dependencies are set
        
        // MediaController
        mediaReadySpy.clear()
        mediaController.setMediaPipeline(null)
        wait(50)
        mediaController.setMediaPipeline(appController.mediaPipeline)
        wait(50)
        verify(mediaReadySpy.count > 0, "MediaController should emit readyChanged signal")
        
        // TorrentController
        torrentReadySpy.clear()
        torrentController.setTorrentEngine(null)
        wait(50)
        torrentController.setTorrentEngine(appController.torrentEngine)
        wait(50)
        verify(torrentReadySpy.count > 0, "TorrentController should emit readyChanged signal")
        
        // TranscriptionController
        transcriptionReadySpy.clear()
        transcriptionController.setWhisperEngine(null)
        wait(50)
        transcriptionController.setWhisperEngine(appController.whisperEngine)
        wait(50)
        verify(transcriptionReadySpy.count > 0, "TranscriptionController should emit readyChanged signal")
        
        // FileManagerController
        fileManagerReadySpy.clear()
        fileManagerController.setFileManager(null)
        wait(50)
        fileManagerController.setFileManager(appController.fileManager)
        wait(50)
        verify(fileManagerReadySpy.count > 0, "FileManagerController should emit readyChanged signal")
        
        console.log("All ready signals tested successfully")
    }
}
