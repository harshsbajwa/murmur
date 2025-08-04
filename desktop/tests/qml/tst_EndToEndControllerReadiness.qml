import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import QtTest 1.15
import Murmur 1.0

/**
 * End-to-end QML test that loads the main QML application and verifies
 * that all controllers report isReady == true within 10 seconds.
 * 
 * This test will fail initially and serve as a red/green safety net
 * for the entire refactoring process.
 */
TestCase {
    id: testCase
    name: "EndToEndControllerReadiness"
    
    // Test properties
    property bool appControllerInitialized: false
    property bool torrentControllerReady: false
    property bool mediaControllerReady: false
    property bool transcriptionControllerReady: false
    property bool fileManagerControllerReady: false
    property bool allControllersReady: false
    
    // Controllers that will be tested
    property var appController: null
    property var torrentController: null
    property var mediaController: null
    property var transcriptionController: null
    property var fileManagerController: null
    
    // Main application window loader
    Loader {
        id: mainWindowLoader
        active: false
        source: "../../src/qml/main.qml"
        
        onLoaded: {
            console.log("Main QML loaded successfully")
            
            // Get references to controllers from the loaded main window
            if (item) {
                torrentController = item.torrentController
                mediaController = item.mediaController
                transcriptionController = item.transcriptionController
                fileManagerController = item.fileManagerController
                
                // Check if appController is available globally
                if (typeof appController !== "undefined") {
                    testCase.appController = appController
                }
            }
        }
    }
    
    /**
     * Timer to periodically check controller readiness.
     * This gives the application time to initialize properly.
     */
    Timer {
        id: readinessCheckTimer
        interval: 100 // Check every 100ms
        repeat: true
        running: false
        
        onTriggered: {
            checkControllerReadiness()
        }
    }
    
    /**
     * Timeout timer to fail the test if controllers don't become ready
     * within the specified time limit.
     */
    Timer {
        id: timeoutTimer
        interval: 10000 // 10 seconds timeout
        repeat: false
        running: false
        
        onTriggered: {
            readinessCheckTimer.stop()
            fail("Controllers did not become ready within 10 seconds. Status:\n" +
                 "  AppController initialized: " + appControllerInitialized + "\n" +
                 "  TorrentController ready: " + torrentControllerReady + "\n" +
                 "  MediaController ready: " + mediaControllerReady + "\n" +
                 "  TranscriptionController ready: " + transcriptionControllerReady + "\n" +
                 "  FileManagerController ready: " + fileManagerControllerReady)
        }
    }
    
    /**
     * Initialize the test case.
     */
    function initTestCase() {
        console.log("Initializing end-to-end controller readiness test")
    }
    
    /**
     * Main test function that loads the application and checks controller readiness.
     */
    function test_allControllersBeomeReady() {
        console.log("Starting controller readiness test")
        
        // Load the main QML file
        mainWindowLoader.active = true
        
        // Wait for the loader to complete
        tryCompare(mainWindowLoader, "status", Loader.Ready, 5000)
        verify(mainWindowLoader.item !== null, "Main QML should load successfully")
        
        // Start checking for controller readiness
        readinessCheckTimer.start()
        timeoutTimer.start()
        
        // Wait for all controllers to be ready
        wait(10100) // Wait slightly longer than timeout to ensure proper test completion
        
        // If we get here without timeout, verify all controllers are ready
        verify(allControllersReady, "All controllers should be ready")
        
        // Additional verification
        verify(appControllerInitialized, "AppController should be initialized")
        verify(torrentControllerReady, "TorrentController should be ready")
        verify(mediaControllerReady, "MediaController should be ready")
        verify(transcriptionControllerReady, "TranscriptionController should be ready")
        verify(fileManagerControllerReady, "FileManagerController should be ready")
        
        console.log("All controllers are ready!")
    }
    
    /**
     * Check the readiness status of all controllers.
     */
    function checkControllerReadiness() {
        // Check AppController
        if (appController && appController.isInitialized) {
            appControllerInitialized = true
        }
        
        // Check TorrentController
        if (torrentController && torrentController.isReady) {
            torrentControllerReady = true
        }
        
        // Check MediaController
        if (mediaController && mediaController.isReady) {
            mediaControllerReady = true
        }
        
        // Check TranscriptionController
        if (transcriptionController && transcriptionController.isReady) {
            transcriptionControllerReady = true
        }
        
        // Check FileManagerController
        if (fileManagerController && fileManagerController.isReady) {
            fileManagerControllerReady = true
        }
        
        // Check if all controllers are ready
        if (appControllerInitialized &&
            torrentControllerReady &&
            mediaControllerReady &&
            transcriptionControllerReady &&
            fileManagerControllerReady) {
            
            allControllersReady = true
            readinessCheckTimer.stop()
            timeoutTimer.stop()
            
            console.log("All controllers are now ready!")
        }
    }
    
    /**
     * Test individual controller properties and signals.
     */
    function test_controllerPropertiesAndSignals() {
        skip("This test requires controllers to be ready first")
        
        // This test will be enabled once the main readiness test passes
        // It will verify that Q_PROPERTY declarations and signals work correctly
    }
    
    /**
     * Test that all engine properties are exposed and not null after initialization.
     */
    function test_enginePropertiesExposed() {
        console.log("Testing engine properties exposure")
        
        // Ensure AppController is available
        verify(appController !== null, "AppController should be available")
        
        // Initialize AppController
        appController.initialize()
        
        // Wait for initialization to complete
        tryCompare(appController, "isInitialized", true, 10000)
        
        // Now check that all engine properties are not null
        verify(appController.torrentEngine !== null, "torrentEngine property should not be null")
        verify(appController.mediaPipeline !== null, "mediaPipeline property should not be null")
        verify(appController.videoPlayer !== null, "videoPlayer property should not be null")
        verify(appController.storageManager !== null, "storageManager property should not be null")
        verify(appController.whisperEngine !== null, "whisperEngine property should not be null")
        verify(appController.fileManager !== null, "fileManager property should not be null")
        
        console.log("All engine properties are properly exposed and not null")
    }
    
    /**
     * Clean up after the test.
     */
    function cleanupTestCase() {
        console.log("Cleaning up end-to-end controller readiness test")
        
        // Stop timers if still running
        readinessCheckTimer.stop()
        timeoutTimer.stop()
        
        // Unload the main window
        mainWindowLoader.active = false
    }
}
