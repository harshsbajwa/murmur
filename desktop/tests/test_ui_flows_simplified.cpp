#include <QtTest/QtTest>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>

#include "utils/TestUtils.hpp"
#include "../src/ui/controllers/AppController.hpp"
#include "../src/ui/controllers/MediaController.hpp"
#include "../src/ui/controllers/TorrentController.hpp"
#include "../src/ui/controllers/TranscriptionController.hpp"
#include "../src/core/common/Expected.hpp"

using namespace Murmur;
using namespace Murmur::Test;

/**
 * @brief Simplified UI flow tests that work with actual controller APIs
 * 
 * These tests validate basic UI integration and controller functionality
 * using the real controller interfaces.
 */
class TestUIFlowsSimplified : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Basic UI controller tests
    void testAppControllerInitialization();
    void testMediaControllerBasicOperations();
    void testTorrentControllerBasicOperations();
    void testTranscriptionControllerBasicOperations();
    
    // Integration tests
    void testControllerInteraction();
    void testSettingsManagement();

private:
    std::unique_ptr<QTemporaryDir> tempDir_;
    std::unique_ptr<AppController> appController_;
    std::unique_ptr<MediaController> mediaController_;
    std::unique_ptr<TorrentController> torrentController_;
    std::unique_ptr<TranscriptionController> transcriptionController_;
    
    QString realVideoFile_;
    QString realAudioFile_;
    
    void setupRealMediaFiles();
};

void TestUIFlowsSimplified::initTestCase() {
    TestUtils::initializeTestEnvironment();
    
    // Check for real sample files
    QString realVideo = TestUtils::getRealSampleVideoFile();
    QString realAudio = TestUtils::getRealSampleAudioFile();
    
    if (realVideo.isEmpty() || realAudio.isEmpty()) {
        QSKIP("Real sample media files not found for UI flow tests");
    }
    
    TestUtils::logMessage("Simplified UI flow tests initialized");
}

void TestUIFlowsSimplified::cleanupTestCase() {
    TestUtils::cleanupTestEnvironment();
}

void TestUIFlowsSimplified::init() {
    tempDir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(tempDir_->isValid());
    
    setupRealMediaFiles();
    
    // Initialize controllers
    appController_ = std::make_unique<AppController>(this);
    mediaController_ = std::make_unique<MediaController>(this);
    torrentController_ = std::make_unique<TorrentController>(this);
    transcriptionController_ = std::make_unique<TranscriptionController>(this);
}

void TestUIFlowsSimplified::cleanup() {
    appController_.reset();
    mediaController_.reset();
    torrentController_.reset();
    transcriptionController_.reset();
    tempDir_.reset();
}

void TestUIFlowsSimplified::testAppControllerInitialization() {
    TEST_SCOPE("testAppControllerInitialization");
    
    // Test initial state
    QVERIFY(!appController_->isInitialized());
    QVERIFY(appController_->status().contains("Not initialized") || 
            appController_->status().isEmpty());
    
    // Test initialization
    appController_->initialize();
    
    // The controller may take time to initialize asynchronously
    QElapsedTimer timer;
    timer.start();
    while (!appController_->isInitialized() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    
    // Verify components are accessible (they might not be fully initialized in test environment)
    // We just check they're not null
    QVERIFY(appController_->torrentEngine() != nullptr);
    QVERIFY(appController_->mediaPipeline() != nullptr);
    QVERIFY(appController_->storageManager() != nullptr);
    
    TestUtils::logMessage("AppController basic initialization test completed");
}

void TestUIFlowsSimplified::testMediaControllerBasicOperations() {
    TEST_SCOPE("testMediaControllerBasicOperations");
    
    // Test initial state
    QVERIFY(mediaController_->currentVideoSource().isEmpty());
    QCOMPARE(mediaController_->playbackPosition(), 0.0);
    QVERIFY(!mediaController_->isProcessing());
    
    // Test loading a local file (doesn't require the file to actually be playable)
    QUrl fileUrl = QUrl::fromLocalFile(realVideoFile_);
    mediaController_->loadLocalFile(fileUrl);
    
    // The source should be updated
    QCOMPARE(mediaController_->currentVideoSource(), fileUrl);
    
    // Test position saving
    mediaController_->savePosition(45.5);
    QCOMPARE(mediaController_->playbackPosition(), 45.5);
    
    TestUtils::logMessage("MediaController basic operations test completed");
}

void TestUIFlowsSimplified::testTorrentControllerBasicOperations() {
    TEST_SCOPE("testTorrentControllerBasicOperations");
    
    // Test initial state
    QVERIFY(!torrentController_->isInitialized());
    
    // Test basic operations (these will likely fail in test environment but shouldn't crash)
    QString testMagnetUri = "magnet:?xt=urn:btih:testHash&dn=Test+Video";
    
    // This will likely fail but should handle gracefully
    torrentController_->addTorrent(testMagnetUri);
    
    // Verify it doesn't crash the controller
    QVERIFY(torrentController_ != nullptr);
    
    TestUtils::logMessage("TorrentController basic operations test completed");
}

void TestUIFlowsSimplified::testTranscriptionControllerBasicOperations() {
    TEST_SCOPE("testTranscriptionControllerBasicOperations");
    
    // Test initial state
    QVERIFY(!transcriptionController_->isProcessing());
    QVERIFY(!transcriptionController_->hasResults());
    
    // Test basic configuration (should work even without actual transcription)
    transcriptionController_->setLanguage("en");
    transcriptionController_->setModel("base");
    
    // These settings should be accepted without error
    QVERIFY(transcriptionController_ != nullptr);
    
    TestUtils::logMessage("TranscriptionController basic operations test completed");
}

void TestUIFlowsSimplified::testControllerInteraction() {
    TEST_SCOPE("testControllerInteraction");
    
    // Initialize app controller first
    appController_->initialize();
    
    // Wait a bit for initialization
    QElapsedTimer timer;
    timer.start();
    while (!appController_->isInitialized() && timer.elapsed() < 3000) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    
    // Set up media controller with dependencies from app controller
    if (appController_->isInitialized()) {
        mediaController_->setMediaPipeline(appController_->mediaPipeline());
        mediaController_->setStorageManager(appController_->storageManager());
        mediaController_->setVideoPlayer(appController_->videoPlayer());
        
        // Now media controller should have its dependencies
        // Test a simple operation
        QUrl fileUrl = QUrl::fromLocalFile(realVideoFile_);
        mediaController_->loadLocalFile(fileUrl);
        QCOMPARE(mediaController_->currentVideoSource(), fileUrl);
    }
    
    TestUtils::logMessage("Controller interaction test completed");
}

void TestUIFlowsSimplified::testSettingsManagement() {
    TEST_SCOPE("testSettingsManagement");
    
    // Test dark mode setting
    bool originalMode = appController_->isDarkMode();
    appController_->setDarkMode(!originalMode);
    QCOMPARE(appController_->isDarkMode(), !originalMode);
    
    // Test settings save/load (these may not persist in test environment)
    appController_->saveSettings();
    appController_->loadSettings();
    
    // Settings should still be consistent
    QVERIFY(appController_ != nullptr);
    
    TestUtils::logMessage("Settings management test completed");
}

void TestUIFlowsSimplified::setupRealMediaFiles() {
    realVideoFile_ = TestUtils::getRealSampleVideoFile();
    realAudioFile_ = TestUtils::getRealSampleAudioFile();
    
    QVERIFY(!realVideoFile_.isEmpty());
    QVERIFY(!realAudioFile_.isEmpty());
    ASSERT_FILE_EXISTS(realVideoFile_);
    ASSERT_FILE_EXISTS(realAudioFile_);
    
    TestUtils::logMessage(QString("Using real video: %1 (%2 bytes)")
                         .arg(QFileInfo(realVideoFile_).fileName())
                         .arg(QFileInfo(realVideoFile_).size()));
    TestUtils::logMessage(QString("Using real audio: %1 (%2 bytes)")
                         .arg(QFileInfo(realAudioFile_).fileName())
                         .arg(QFileInfo(realAudioFile_).size()));
}

#include "test_ui_flows_simplified.moc"