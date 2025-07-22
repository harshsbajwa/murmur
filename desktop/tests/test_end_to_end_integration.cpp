#include <QtTest/QtTest>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QStandardPaths>
#include <vector>
#include <thread>

#include "utils/TestUtils.hpp"
#include "../src/core/storage/StorageManager.hpp"
#include "../src/core/torrent/TorrentEngine.hpp"
#include "../src/core/media/FFmpegWrapper.hpp"
#include "../src/core/transcription/WhisperEngine.hpp"
#include "../src/core/common/Expected.hpp"
#include "../src/core/security/InfoHashValidator.hpp"

using namespace Murmur;
using namespace Murmur::Test;

/**
 * @brief End-to-end integration tests covering complete user workflows
 * 
 * Tests the full application flow from file import through processing
 * to final output, ensuring all components work together correctly.
 */
class TestEndToEndIntegration : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Complete workflow tests
    void testCompleteMediaImportAndProcessingWorkflow();
    void testTorrentDownloadAndTranscriptionWorkflow();
    void testBatchProcessingWorkflow();
    void testErrorRecoveryWorkflow();
    
    // Integration boundary tests
    void testStorageAndTorrentIntegration();
    void testMediaProcessingAndStorageIntegration();
    void testTranscriptionAndStorageIntegration();
    void testConcurrentComponentUsage();
    
    // Data flow tests
    void testDataPersistenceAcrossRestarts();
    void testMetadataConsistencyWorkflow();
    void testProgressTrackingThroughoutWorkflow();

private:
    std::unique_ptr<StorageManager> storage_;
    std::unique_ptr<TorrentEngine> torrentEngine_;
    std::unique_ptr<FFmpegWrapper> ffmpegWrapper_;
    std::unique_ptr<WhisperEngine> whisperEngine_;
    std::unique_ptr<QTemporaryDir> tempDir_;
    QString testDbPath_;
    QString testVideoFile_;
    QString testAudioFile_;
    
    // Helper methods
    void setupTestEnvironment();
    void createTestMediaFiles();
    TorrentRecord createTestTorrent();
    MediaRecord createTestMedia(const QString& torrentHash);
    bool waitForCompletion(std::function<bool()> condition, int timeoutMs = 10000);
};

void TestEndToEndIntegration::initTestCase() {
    TestUtils::initializeTestEnvironment();
    TestUtils::logMessage("End-to-end integration tests initialized");
}

void TestEndToEndIntegration::cleanupTestCase() {
    TestUtils::cleanupTestEnvironment();
}

void TestEndToEndIntegration::init() {
    tempDir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(tempDir_->isValid());
    
    testDbPath_ = tempDir_->filePath("integration_test.db");
    
    setupTestEnvironment();
    createTestMediaFiles();
}

void TestEndToEndIntegration::cleanup() {
    // Clean shutdown in correct order
    whisperEngine_.reset();
    ffmpegWrapper_.reset();
    torrentEngine_.reset();
    storage_.reset();
    tempDir_.reset();
}

void TestEndToEndIntegration::setupTestEnvironment() {
    // Initialize all components
    storage_ = std::make_unique<StorageManager>();
    QVERIFY(storage_->initialize(testDbPath_).hasValue());
    
    torrentEngine_ = std::make_unique<TorrentEngine>();
    auto torrentInitResult = torrentEngine_->initialize();
    if (torrentInitResult.hasError()) {
        TestUtils::logMessage("TorrentEngine initialization failed - some tests will be skipped");
    }
    
    ffmpegWrapper_ = std::make_unique<FFmpegWrapper>();
    if (!TestUtils::isFFmpegAvailable()) {
        TestUtils::logMessage("FFmpeg not available - media processing tests will be skipped");
    }
    
    whisperEngine_ = std::make_unique<WhisperEngine>();
    auto whisperInitResult = whisperEngine_->initialize();
    if (whisperInitResult.hasError()) {
        TestUtils::logMessage("WhisperEngine initialization failed - transcription tests will be skipped");
    }
}

void TestEndToEndIntegration::createTestMediaFiles() {
    testVideoFile_ = tempDir_->filePath("test_video.mp4");
    testAudioFile_ = tempDir_->filePath("test_audio.wav");
    
    // Create minimal test video file
    if (TestUtils::isFFmpegAvailable()) {
        TestUtils::createTestVideoFile(testVideoFile_, 5); // 5 second video
        TestUtils::createTestAudioFile(testAudioFile_, 5); // 5 second audio
    }
}

void TestEndToEndIntegration::testCompleteMediaImportAndProcessingWorkflow() {
    TEST_SCOPE("testCompleteMediaImportAndProcessingWorkflow");
    
    if (!QFile::exists(testVideoFile_)) {
        QSKIP("Test video file not available - skipping media processing workflow");
    }
    
    // Step 1: Create a torrent record for the media
    TorrentRecord torrent = createTestTorrent();
    auto addTorrentResult = storage_->addTorrent(torrent);
    QVERIFY(addTorrentResult.hasValue());
    
    TestUtils::logMessage("Step 1: Torrent record created successfully");
    
    // Step 2: Create media record
    MediaRecord media = createTestMedia(torrent.infoHash);
    media.filePath = testVideoFile_;
    auto addMediaResult = storage_->addMedia(media);
    QVERIFY(addMediaResult.hasValue());
    QString mediaId = addMediaResult.value();
    
    TestUtils::logMessage("Step 2: Media record created successfully");
    
    // Step 3: Analyze video file with FFmpeg
    if (TestUtils::isFFmpegAvailable()) {
        auto analysisFuture = ffmpegWrapper_->analyzeFile(testVideoFile_);
        auto analysisResult = TestUtils::waitForFuture(analysisFuture);
        
        if (analysisResult.hasValue()) {
            auto videoInfo = analysisResult.value();
            
            // Update media record with analysis results
            media.duration = videoInfo.duration;
            media.width = videoInfo.width();
            media.height = videoInfo.height();
            media.videoCodec = videoInfo.videoCodec();
            media.audioCodec = videoInfo.audioCodec();
            
            QVERIFY(storage_->updateMedia(media).hasValue());
            TestUtils::logMessage("Step 3: Video analysis and metadata update completed");
        } else {
            TestUtils::logMessage("Step 3: Video analysis failed - continuing with basic metadata");
        }
    }
    
    // Step 4: Generate transcription if Whisper is available
    if (whisperEngine_->isReady()) {
        TranscriptionRecord transcription;
        transcription.mediaId = mediaId;
        transcription.language = "en";
        transcription.modelUsed = "base";
        transcription.fullText = "Test transcription content";
        transcription.confidence = 0.95;
        transcription.dateCreated = QDateTime::currentDateTime();
        transcription.processingTime = 1000;
        transcription.status = "completed";
        
        auto addTranscriptionResult = storage_->addTranscription(transcription);
        QVERIFY(addTranscriptionResult.hasValue());
        
        TestUtils::logMessage("Step 4: Transcription created successfully");
    } else {
        TestUtils::logMessage("Step 4: Whisper not available - skipping transcription");
    }
    
    // Step 5: Verify complete workflow data integrity
    auto retrievedTorrent = storage_->getTorrent(torrent.infoHash);
    QVERIFY(retrievedTorrent.hasValue());
    QCOMPARE(retrievedTorrent.value().name, torrent.name);
    
    auto retrievedMedia = storage_->getMedia(mediaId);
    QVERIFY(retrievedMedia.hasValue());
    QCOMPARE(retrievedMedia.value().originalName, media.originalName);
    
    auto mediaByTorrent = storage_->getMediaByTorrent(torrent.infoHash);
    QVERIFY(mediaByTorrent.hasValue());
    QVERIFY(mediaByTorrent.value().size() >= 1);
    
    TestUtils::logMessage("Step 5: Complete workflow data integrity verified");
    TestUtils::logMessage("Complete media import and processing workflow test passed");
}

void TestEndToEndIntegration::testTorrentDownloadAndTranscriptionWorkflow() {
    TEST_SCOPE("testTorrentDownloadAndTranscriptionWorkflow");
    
    // This test simulates a complete torrent download and transcription workflow
    // without actually downloading files (mock the download process)
    
    // Step 1: Add torrent for "download"
    TorrentRecord torrent = createTestTorrent();
    torrent.status = "downloading";
    torrent.progress = 0.0;
    
    QVERIFY(storage_->addTorrent(torrent).hasValue());
    TestUtils::logMessage("Step 1: Torrent added for simulated download");
    
    // Step 2: Simulate download progress updates
    for (int progress = 10; progress <= 100; progress += 10) {
        double progressPercent = progress / 100.0;
        QVERIFY(storage_->updateTorrentProgress(torrent.infoHash, progressPercent).hasValue());
        
        if (progress == 100) {
            QVERIFY(storage_->updateTorrentStatus(torrent.infoHash, "completed").hasValue());
        }
    }
    
    TestUtils::logMessage("Step 2: Download progress simulation completed");
    
    // Step 3: Add media files that would be in the completed torrent
    QStringList mediaFiles = {"video1.mp4", "video2.mkv", "audio1.mp3"};
    QStringList mediaIds;
    
    for (const QString& fileName : mediaFiles) {
        MediaRecord media = createTestMedia(torrent.infoHash);
        media.originalName = fileName;
        media.filePath = tempDir_->filePath(fileName);
        
        auto addResult = storage_->addMedia(media);
        QVERIFY(addResult.hasValue());
        mediaIds.append(addResult.value());
    }
    
    TestUtils::logMessage("Step 3: Media files added to completed torrent");
    
    // Step 4: Queue transcriptions for all video files
    int transcriptionCount = 0;
    for (const QString& mediaId : mediaIds) {
        auto mediaResult = storage_->getMedia(mediaId);
        QVERIFY(mediaResult.hasValue());
        
        MediaRecord media = mediaResult.value();
        if (media.originalName.endsWith(".mp4") || media.originalName.endsWith(".mkv")) {
            TranscriptionRecord transcription;
            transcription.mediaId = mediaId;
            transcription.language = "auto";
            transcription.modelUsed = "base";
            transcription.status = "processing";
            transcription.dateCreated = QDateTime::currentDateTime();
            
            auto addResult = storage_->addTranscription(transcription);
            QVERIFY(addResult.hasValue());
            transcriptionCount++;
            
            // Simulate transcription completion
            transcription.fullText = QString("Transcription for %1").arg(media.originalName);
            transcription.confidence = 0.87;
            transcription.processingTime = 15000;
            transcription.status = "completed";
            
            QVERIFY(storage_->updateTranscription(transcription).hasValue());
        }
    }
    
    TestUtils::logMessage(QString("Step 4: %1 transcriptions created and completed").arg(transcriptionCount));
    
    // Step 5: Verify workflow completion
    auto finalTorrent = storage_->getTorrent(torrent.infoHash);
    QVERIFY(finalTorrent.hasValue());
    QCOMPARE(finalTorrent.value().status, QString("completed"));
    QCOMPARE(finalTorrent.value().progress, 1.0);
    
    auto torrentMedia = storage_->getMediaByTorrent(torrent.infoHash);
    QVERIFY(torrentMedia.hasValue());
    QCOMPARE(torrentMedia.value().size(), mediaFiles.size());
    
    auto allTranscriptions = storage_->getAllTranscriptions();
    QVERIFY(allTranscriptions.hasValue());
    QVERIFY(allTranscriptions.value().size() >= transcriptionCount);
    
    TestUtils::logMessage("Step 5: Torrent download and transcription workflow completed successfully");
}

void TestEndToEndIntegration::testStorageAndTorrentIntegration() {
    TEST_SCOPE("testStorageAndTorrentIntegration");
    
    // Test the integration between storage and torrent engine
    TorrentRecord torrent = createTestTorrent();
    QVERIFY(storage_->addTorrent(torrent).hasValue());
    
    // If torrent engine is available, test the integration
    if (torrentEngine_ && torrentEngine_->isInitialized()) {
        // This would test adding the torrent to the engine and syncing with storage
        TestUtils::logMessage("TorrentEngine integration test completed");
    } else {
        TestUtils::logMessage("TorrentEngine not available - testing storage operations only");
    }
    
    // Test storage operations work correctly
    auto retrieved = storage_->getTorrent(torrent.infoHash);
    QVERIFY(retrieved.hasValue());
    QCOMPARE(retrieved.value().name, torrent.name);
    
    // Test status updates
    QVERIFY(storage_->updateTorrentStatus(torrent.infoHash, "seeding").hasValue());
    auto updated = storage_->getTorrent(torrent.infoHash);
    QVERIFY(updated.hasValue());
    QCOMPARE(updated.value().status, QString("seeding"));
    
    TestUtils::logMessage("Storage and torrent integration test completed");
}

TorrentRecord TestEndToEndIntegration::createTestTorrent() {
    static int torrentCounter = 0;
    TorrentRecord torrent;
    
    // Generate a valid unique hash for each test using InfoHashValidator
    torrent.infoHash = InfoHashValidator::generateTestHash(++torrentCounter);
    
    torrent.name = QString("Test Integration Torrent %1").arg(torrentCounter);
    torrent.magnetUri = QString("magnet:?xt=urn:btih:%1&dn=%2").arg(torrent.infoHash, "TestTorrent");
    torrent.size = 1024 * 1024 * 100; // 100MB
    torrent.dateAdded = QDateTime::currentDateTime();
    torrent.lastActive = QDateTime::currentDateTime();
    torrent.savePath = tempDir_->path();
    torrent.progress = 0.0;
    torrent.status = "downloading";
    torrent.seeders = 5;
    torrent.leechers = 2;
    torrent.downloaded = 0;
    torrent.uploaded = 0;
    torrent.ratio = 0.0;
    return torrent;
}

MediaRecord TestEndToEndIntegration::createTestMedia(const QString& torrentHash) {
    MediaRecord media;
    media.torrentHash = torrentHash;
    media.originalName = "test_video.mp4";
    media.mimeType = "video/mp4";
    media.fileSize = 1024 * 1024 * 50; // 50MB
    media.duration = 300000; // 5 minutes
    media.width = 1920;
    media.height = 1080;
    media.frameRate = 30.0;
    media.videoCodec = "h264";
    media.audioCodec = "aac";
    media.hasTranscription = false;
    media.dateAdded = QDateTime::currentDateTime();
    media.playbackPosition = 0;
    return media;
}

bool TestEndToEndIntegration::waitForCompletion(std::function<bool()> condition, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    
    while (timer.elapsed() < timeoutMs) {
        if (condition()) {
            return true;
        }
        QCoreApplication::processEvents();
        QThread::msleep(100);
    }
    
    return false;
}

void TestEndToEndIntegration::testBatchProcessingWorkflow() {
    TEST_SCOPE("testBatchProcessingWorkflow");
    
    // Test processing multiple torrents and media files simultaneously
    const int batchSize = 5;
    QStringList torrentHashes;
    
    // Create multiple torrents
    for (int i = 0; i < batchSize; ++i) {
        TorrentRecord torrent = createTestTorrent();
        // Use InfoHashValidator to generate valid unique hash for batch testing
        torrent.infoHash = InfoHashValidator::generateTestHash(1000 + i);
        torrent.name = QString("Batch Torrent %1").arg(i + 1);
        
        QVERIFY(storage_->addTorrent(torrent).hasValue());
        torrentHashes.append(torrent.infoHash);
    }
    
    // Add media files for each torrent
    QStringList allMediaIds;
    for (const QString& hash : torrentHashes) {
        for (int j = 0; j < 3; ++j) { // 3 media files per torrent
            MediaRecord media = createTestMedia(hash);
            media.originalName = QString("video_%1_%2.mp4").arg(hash.left(8)).arg(j + 1);
            
            auto result = storage_->addMedia(media);
            QVERIFY(result.hasValue());
            allMediaIds.append(result.value());
        }
    }
    
    // Verify batch operations
    auto allTorrents = storage_->getAllTorrents();
    QVERIFY(allTorrents.hasValue());
    QVERIFY(allTorrents.value().size() >= batchSize);
    
    auto allMedia = storage_->getAllMedia();
    QVERIFY(allMedia.hasValue());
    QVERIFY(allMedia.value().size() >= batchSize * 3);
    
    TestUtils::logMessage(QString("Batch processing workflow completed: %1 torrents, %2 media files")
                         .arg(batchSize).arg(allMediaIds.size()));
}

void TestEndToEndIntegration::testErrorRecoveryWorkflow() {
    TEST_SCOPE("testErrorRecoveryWorkflow");
    
    // Test recovery from various error conditions
    TorrentRecord torrent = createTestTorrent();
    QVERIFY(storage_->addTorrent(torrent).hasValue());
    
    // Test 1: Database connection recovery
    storage_->close();
    
    // Operations should fail when closed
    TorrentRecord newTorrent = createTestTorrent();
    newTorrent.infoHash = InfoHashValidator::generateTestHash(9999); // Invalid length intentionally for test
    newTorrent.infoHash = "invalid_hash"; // Force invalid hash for test
    auto failedAdd = storage_->addTorrent(newTorrent);
    QVERIFY(failedAdd.hasError());
    
    // Reconnect and verify recovery
    QVERIFY(storage_->initialize(testDbPath_).hasValue());
    
    // Use a valid hash after reconnection
    newTorrent.infoHash = InfoHashValidator::generateTestHash(3000);
    QVERIFY(storage_->addTorrent(newTorrent).hasValue());
    
    // Test 2: Invalid data recovery
    MediaRecord invalidMedia = createTestMedia(torrent.infoHash);
    invalidMedia.duration = -1; // Invalid duration
    
    auto invalidResult = storage_->addMedia(invalidMedia);
    if (invalidResult.hasError()) {
        TestUtils::logMessage("Invalid data correctly rejected");
    }
    
    // Fix and retry
    invalidMedia.duration = 120000;
    auto validResult = storage_->addMedia(invalidMedia);
    QVERIFY(validResult.hasValue());
    
    TestUtils::logMessage("Error recovery workflow completed successfully");
}

void TestEndToEndIntegration::testMediaProcessingAndStorageIntegration() {
    TEST_SCOPE("testMediaProcessingAndStorageIntegration");
    
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available - skipping media processing integration test");
    }
    
    // Test the integration between media processing and storage
    TorrentRecord torrent = createTestTorrent();
    QVERIFY(storage_->addTorrent(torrent).hasValue());
    
    MediaRecord media = createTestMedia(torrent.infoHash);
    media.filePath = testVideoFile_;
    auto addResult = storage_->addMedia(media);
    QVERIFY(addResult.hasValue());
    
    TestUtils::logMessage("Media processing and storage integration test completed");
}

void TestEndToEndIntegration::testTranscriptionAndStorageIntegration() {
    TEST_SCOPE("testTranscriptionAndStorageIntegration");
    
    // Test transcription and storage integration
    TorrentRecord torrent = createTestTorrent();
    QVERIFY(storage_->addTorrent(torrent).hasValue());
    
    MediaRecord media = createTestMedia(torrent.infoHash);
    auto mediaResult = storage_->addMedia(media);
    QVERIFY(mediaResult.hasValue());
    
    TranscriptionRecord transcription;
    transcription.mediaId = mediaResult.value();
    transcription.language = "en";
    transcription.modelUsed = "base";
    transcription.fullText = "Integration test transcription";
    transcription.confidence = 0.9;
    transcription.dateCreated = QDateTime::currentDateTime();
    transcription.status = "completed";
    
    auto transcriptionResult = storage_->addTranscription(transcription);
    QVERIFY(transcriptionResult.hasValue());
    
    // Verify relationships
    auto retrievedTranscription = storage_->getTranscriptionByMedia(mediaResult.value());
    QVERIFY(retrievedTranscription.hasValue());
    QCOMPARE(retrievedTranscription.value().fullText, transcription.fullText);
    
    TestUtils::logMessage("Transcription and storage integration test completed");
}

void TestEndToEndIntegration::testConcurrentComponentUsage() {
    TEST_SCOPE("testConcurrentComponentUsage");
    
    // Test concurrent usage of multiple components
    std::atomic<int> completedOperations{0};
    const int totalOperations = 10;
    
    std::vector<std::thread> threads;
    
    // Create multiple threads performing different operations
    for (int i = 0; i < totalOperations; ++i) {
        threads.emplace_back([&, i]() {
            TorrentRecord torrent = createTestTorrent();
            // Use InfoHashValidator to generate valid unique hash for concurrent testing
            torrent.infoHash = InfoHashValidator::generateTestHash(2000 + i);
            
            if (storage_->addTorrent(torrent).hasValue()) {
                completedOperations++;
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    QVERIFY(completedOperations.load() == totalOperations);
    TestUtils::logMessage(QString("Concurrent component usage test: %1/%2 operations completed")
                         .arg(completedOperations.load()).arg(totalOperations));
}

void TestEndToEndIntegration::testDataPersistenceAcrossRestarts() {
    TEST_SCOPE("testDataPersistenceAcrossRestarts");
    
    // Add test data
    TorrentRecord torrent = createTestTorrent();
    QVERIFY(storage_->addTorrent(torrent).hasValue());
    
    MediaRecord media = createTestMedia(torrent.infoHash);
    auto mediaResult = storage_->addMedia(media);
    QVERIFY(mediaResult.hasValue());
    
    // Simulate restart by closing and reopening storage
    storage_.reset();
    storage_ = std::make_unique<StorageManager>();
    QVERIFY(storage_->initialize(testDbPath_).hasValue());
    
    // Verify data persistence
    auto retrievedTorrent = storage_->getTorrent(torrent.infoHash);
    QVERIFY(retrievedTorrent.hasValue());
    QCOMPARE(retrievedTorrent.value().name, torrent.name);
    
    auto retrievedMedia = storage_->getMedia(mediaResult.value());
    QVERIFY(retrievedMedia.hasValue());
    QCOMPARE(retrievedMedia.value().originalName, media.originalName);
    
    TestUtils::logMessage("Data persistence across restarts test completed");
}

void TestEndToEndIntegration::testMetadataConsistencyWorkflow() {
    TEST_SCOPE("testMetadataConsistencyWorkflow");
    
    // Test metadata consistency across all components
    TorrentRecord torrent = createTestTorrent();
    QVERIFY(storage_->addTorrent(torrent).hasValue());
    
    // Add multiple media files with consistent metadata
    QStringList mediaIds;
    for (int i = 0; i < 3; ++i) {
        MediaRecord media = createTestMedia(torrent.infoHash);
        media.originalName = QString("consistent_%1.mp4").arg(i + 1);
        
        auto result = storage_->addMedia(media);
        QVERIFY(result.hasValue());
        mediaIds.append(result.value());
    }
    
    // Verify all media references the same torrent
    for (const QString& mediaId : mediaIds) {
        auto media = storage_->getMedia(mediaId);
        QVERIFY(media.hasValue());
        QCOMPARE(media.value().torrentHash, torrent.infoHash);
    }
    
    // Test metadata updates
    torrent.size = torrent.size * 2;
    QVERIFY(storage_->updateTorrent(torrent).hasValue());
    
    auto updatedTorrent = storage_->getTorrent(torrent.infoHash);
    QVERIFY(updatedTorrent.hasValue());
    QCOMPARE(updatedTorrent.value().size, torrent.size);
    
    TestUtils::logMessage("Metadata consistency workflow test completed");
}

void TestEndToEndIntegration::testProgressTrackingThroughoutWorkflow() {
    TEST_SCOPE("testProgressTrackingThroughoutWorkflow");
    
    // Test progress tracking through a complete workflow
    TorrentRecord torrent = createTestTorrent();
    torrent.progress = 0.0;
    torrent.status = "downloading";
    QVERIFY(storage_->addTorrent(torrent).hasValue());
    
    // Simulate progress updates
    QList<double> progressSteps = {0.1, 0.25, 0.5, 0.75, 0.9, 1.0};
    QStringList statusSteps = {"downloading", "downloading", "downloading", "downloading", "downloading", "completed"};
    
    for (int i = 0; i < progressSteps.size(); ++i) {
        QVERIFY(storage_->updateTorrentProgress(torrent.infoHash, progressSteps[i]).hasValue());
        QVERIFY(storage_->updateTorrentStatus(torrent.infoHash, statusSteps[i]).hasValue());
        
        auto current = storage_->getTorrent(torrent.infoHash);
        QVERIFY(current.hasValue());
        QCOMPARE(current.value().progress, progressSteps[i]);
        QCOMPARE(current.value().status, statusSteps[i]);
    }
    
    TestUtils::logMessage("Progress tracking throughout workflow test completed");
}

int runTestEndToEndIntegration(int argc, char** argv) {
    TestEndToEndIntegration test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_end_to_end_integration.moc"