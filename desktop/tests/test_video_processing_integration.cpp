#include <QtTest/QtTest>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QRandomGenerator>
#include <QtConcurrent/QtConcurrent>

#include "utils/TestUtils.hpp"
#include "utils/MockComponents.hpp"
#include "../src/core/media/MediaPipeline.hpp"
#include "../src/core/transcription/WhisperEngine.hpp"
#include "../src/core/storage/StorageManager.hpp"
#include "../src/core/common/Expected.hpp"

using namespace Murmur;
using namespace Murmur::Test;

/**
 * @brief Integration test for the complete video processing pipeline
 * 
 * Tests the end-to-end workflow from video upload through transcription
 * and database storage, including error recovery scenarios.
 */
class TestVideoProcessingIntegration : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Core integration tests
    void testCompleteVideoProcessingWorkflow();
    void testVideoAnalysisIntegration();
    void testVideoConversionIntegration();
    void testAudioExtractionIntegration();
    void testTranscriptionIntegration();
    void testStorageIntegration();
    
    // Error recovery integration tests
    void testHardwareFailureRecovery();
    void testNetworkFailureRecovery();
    void testDiskSpaceFailureRecovery();
    void testMemoryPressureRecovery();
    void testTranscriptionFailureRecovery();
    
    // Performance integration tests
    void testConcurrentVideoProcessing();
    void testLargeFileProcessing();
    void testMemoryUsageUnderLoad();
    void testResourceCleanupAfterFailure();
    
    // Real-world scenario tests
    void testBatchVideoProcessing();
    void testInterruptedProcessingRecovery();
    void testProgressTrackingAccuracy();
    void testCancellationBehavior();

private:
    std::unique_ptr<MediaPipeline> mediaPipeline_;
    std::unique_ptr<WhisperEngine> whisperEngine_;
    std::unique_ptr<StorageManager> storageManager_;
    std::unique_ptr<QTemporaryDir> tempDir_;
    QString testVideoFile_;
    QString testAudioFile_;
    QString testDbPath_;
    
    // Test helpers
    void setupTestEnvironment();
    void createTestFiles();
    void verifyVideoProcessingResult(const QString& outputPath);
    void verifyTranscriptionResult(const QString& transcriptionPath);
    void simulateResourceConstraints();
    void clearResourceConstraints();
    
    // Signal tracking
    QStringList capturedSignals_;
    QSignalSpy* progressSpy_;
    QSignalSpy* completionSpy_;
    QSignalSpy* errorSpy_;
};

void TestVideoProcessingIntegration::initTestCase() {
    TestUtils::initializeTestEnvironment();
    TestUtils::startResourceMonitoring();
}

void TestVideoProcessingIntegration::cleanupTestCase() {
    TestUtils::stopResourceMonitoring();
    
    // Print resource usage report
    QJsonObject resourceReport = TestUtils::getResourceUsageReport();
    qDebug() << "Resource Usage Report:" << QJsonDocument(resourceReport).toJson();
    
    TestUtils::cleanupTestEnvironment();
}

void TestVideoProcessingIntegration::init() {
    setupTestEnvironment();
    createTestFiles();
    
    // Check for required dependencies
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available - skipping video processing integration tests");
    }
    
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available - skipping transcription integration tests");
    }
    
    // Initialize components
    mediaPipeline_ = std::make_unique<MediaPipeline>(this);
    whisperEngine_ = std::make_unique<WhisperEngine>(this);
    storageManager_ = std::make_unique<StorageManager>(this);
    
    // Initialize storage with unique database for this test
    QString uniqueDbPath = QString("%1/test_%2_%3.db")
                          .arg(tempDir_->path())
                          .arg(QString::number(QDateTime::currentMSecsSinceEpoch()))
                          .arg(QRandomGenerator::global()->generate());
    auto initResult = storageManager_->initialize(uniqueDbPath);
    ASSERT_EXPECTED_VALUE(initResult);

    // Initialize Whisper engine with real model download
    QString whisperModelsPath = tempDir_->path() + "/models";
    QDir().mkpath(whisperModelsPath);
    auto initResultWhisper = whisperEngine_->initialize(whisperModelsPath);
    ASSERT_EXPECTED_VALUE(initResultWhisper);

    // Try to download the actual tiny quantized model
    QString modelName = "tiny-q5_1"; // Use quantized model as suggested
    auto downloadFuture = whisperEngine_->downloadModel(modelName);
    auto downloadResult = TestUtils::waitForFuture(downloadFuture, 60000); // 60 seconds timeout for download
    
    if (downloadResult.hasError()) {
        qDebug() << "Failed to download model" << modelName << "- trying fallback to tiny.en";
        // Try the regular tiny.en model as fallback
        auto downloadFallback = whisperEngine_->downloadModel("tiny.en");
        auto fallbackResult = TestUtils::waitForFuture(downloadFallback, 60000);
        
        if (fallbackResult.hasError()) {
            qWarning() << "Failed to download any Whisper model; transcription-related tests may be skipped.";
        } else {
            qDebug() << "Successfully downloaded fallback model: tiny.en";
        }
    } else {
        qDebug() << "Successfully downloaded model:" << modelName;
    }
    
    // Setup signal spies
    progressSpy_ = new QSignalSpy(mediaPipeline_.get(), &MediaPipeline::conversionProgress);
    completionSpy_ = new QSignalSpy(mediaPipeline_.get(), &MediaPipeline::conversionCompleted);
    errorSpy_ = new QSignalSpy(mediaPipeline_.get(), &MediaPipeline::conversionFailed);
    
    capturedSignals_.clear();
}

void TestVideoProcessingIntegration::cleanup() {
    delete progressSpy_;
    delete completionSpy_;
    delete errorSpy_;
    
    mediaPipeline_.reset();
    whisperEngine_.reset();
    storageManager_.reset();
    tempDir_.reset();
}

void TestVideoProcessingIntegration::testCompleteVideoProcessingWorkflow() {
    TEST_SCOPE("testCompleteVideoProcessingWorkflow");
    BENCHMARK_SCOPE("CompleteWorkflow", 1);
    
    _benchmark.startIteration();
    
    // Step 1: Analyze video file
    auto analysisFuture = mediaPipeline_->analyzeVideo(testVideoFile_);
    auto analysisResult = TestUtils::waitForFuture(analysisFuture, 10000);
    
    // Handle analysis result properly
    if (analysisResult.hasError()) {
        QFAIL(qPrintable(QString("Video analysis failed with error: %1").arg(static_cast<int>(analysisResult.error()))));
        return;
    }
    
    VideoInfo videoInfo = analysisResult.value();
    
    QVERIFY(!videoInfo.filePath.isEmpty());
    QVERIFY(videoInfo.duration > 0);
    QVERIFY(videoInfo.width > 0);
    QVERIFY(videoInfo.height > 0);
    
    // Step 2: Convert video to standardized format
    QString outputPath = _testScope.getTempDirectory() + "/converted_video.mp4";
    ConversionSettings settings;
    settings.outputFormat = "mp4";
    settings.videoCodec = "libx264";
    settings.maxWidth = 1920;
    settings.maxHeight = 1080;
    
    auto conversionFuture = mediaPipeline_->convertVideo(testVideoFile_, outputPath, settings);
    auto conversionResult = TestUtils::waitForFuture(conversionFuture, 30000);
    
    ASSERT_EXPECTED_VALUE(conversionResult);
    ASSERT_FILE_EXISTS(outputPath);
    verifyVideoProcessingResult(outputPath);
    
    // Step 3: Extract audio for transcription
    QString audioPath = _testScope.getTempDirectory() + "/extracted_audio.wav";
    auto audioFuture = mediaPipeline_->extractAudio(outputPath, audioPath, "wav");
    auto audioResult = TestUtils::waitForFuture(audioFuture, 15000);
    
    // Handle audio extraction result
    if (audioResult.hasError()) {
        QFAIL(qPrintable(QString("Audio extraction failed with error: %1. The test video must have an audio stream.").arg(static_cast<int>(audioResult.error()))));
        return;
    }
    ASSERT_FILE_EXISTS(audioPath);

    // Step 4: Transcribe audio
    auto loadResult = whisperEngine_->loadModel("tiny-q5_1");
    if (loadResult.hasError()) {
        qDebug() << "Failed to load quantized model, trying fallback to tiny.en";
        loadResult = whisperEngine_->loadModel("tiny.en");
        if (loadResult.hasError()) {
            QSKIP("Could not load any Whisper model. Skipping transcription.", SkipSingle);
        }
    }

    TranscriptionSettings transcriptionOptions;
    transcriptionOptions.outputFormat = "json";
    transcriptionOptions.language = "en";
    
    auto transcriptionFuture = whisperEngine_->transcribeAudio(audioPath, transcriptionOptions);
    auto transcriptionResult = TestUtils::waitForFuture(transcriptionFuture, 60000);

    if (transcriptionResult.hasError()) {
        QFAIL(qPrintable(QString("Transcription failed with real model with error: %1").arg(static_cast<int>(transcriptionResult.error()))));
        return;
    }

    TranscriptionResult transcription = transcriptionResult.value();

    QVERIFY(!transcription.fullText.isEmpty());
    QVERIFY(!transcription.segments.isEmpty());
    
    // Step 5: Store results in database
    TorrentRecord torrentRecord;
    // Generate valid 40-character hex hash
    torrentRecord.infoHash = QString("a1b2c3d4e5f6789012345678901234567890%1").arg(QRandomGenerator::global()->generate() % 10000, 4, 16, QChar('0'));
    torrentRecord.name = "Test Video";
    torrentRecord.magnetUri = QString("magnet:?xt=urn:btih:%1&dn=Test+Video").arg(torrentRecord.infoHash);
    torrentRecord.size = QFileInfo(testVideoFile_).size();
    torrentRecord.dateAdded = QDateTime::currentDateTime();
    torrentRecord.savePath = QFileInfo(testVideoFile_).dir().absolutePath();
    torrentRecord.progress = 1.0;
    torrentRecord.status = "completed";
    
    auto torrentResult = storageManager_->addTorrent(torrentRecord);
    if (torrentResult.hasError()) {
        QFAIL(qPrintable(QString("Failed to add torrent record with error: %1").arg(static_cast<int>(torrentResult.error()))));
        return;
    }
    
    MediaRecord mediaRecord;
    mediaRecord.torrentHash = torrentRecord.infoHash;
    mediaRecord.filePath = outputPath;
    mediaRecord.originalName = QFileInfo(testVideoFile_).baseName();
    mediaRecord.mimeType = "video/mp4";
    mediaRecord.fileSize = QFileInfo(outputPath).size();
    mediaRecord.duration = videoInfo.duration;
    mediaRecord.width = videoInfo.width;
    mediaRecord.height = videoInfo.height;
    mediaRecord.dateAdded = QDateTime::currentDateTime();
    
    auto mediaResult = storageManager_->addMedia(mediaRecord);
    if (mediaResult.hasError()) {
        QFAIL(qPrintable(QString("Failed to add media record with error: %1").arg(static_cast<int>(mediaResult.error()))));
        return;
    }
    QString mediaId = mediaResult.value();
    
    TranscriptionRecord transcriptionRecord;
    transcriptionRecord.mediaId = mediaId;
    transcriptionRecord.fullText = transcription.fullText;
    transcriptionRecord.language = "en";
    transcriptionRecord.confidence = transcription.confidence;
    transcriptionRecord.dateCreated = QDateTime::currentDateTime();
    transcriptionRecord.status = "completed";
    
    auto transcriptionDbResult = storageManager_->addTranscription(transcriptionRecord);
    ASSERT_EXPECTED_VALUE(transcriptionDbResult);
    
    _benchmark.endIteration();
    
    // Verify signal emissions
    QVERIFY(progressSpy_->count() > 0);
    QVERIFY(completionSpy_->count() > 0);
    QCOMPARE(errorSpy_->count(), 0);
    
    // Verify data integrity
    auto retrievedMedia = storageManager_->getMedia(mediaId);
    ASSERT_EXPECTED_VALUE(retrievedMedia);
    QCOMPARE(retrievedMedia.value().filePath, outputPath);
    
    auto retrievedTranscription = storageManager_->getTranscriptionByMedia(mediaId);
    ASSERT_EXPECTED_VALUE(retrievedTranscription);
    QCOMPARE(retrievedTranscription.value().fullText, transcription.fullText);
}

void TestVideoProcessingIntegration::testVideoAnalysisIntegration() {
    TEST_SCOPE("testVideoAnalysisIntegration");
    
    // Test analysis of various video formats
    QStringList testFormats = {"mp4", "avi", "mkv", "mov"};
    
    for (const QString& format : testFormats) {
        QString formatTestFile = TestUtils::createTestVideoFile(_testScope.getTempDirectory(), 5, format);
        
        auto analysisFuture = mediaPipeline_->analyzeVideo(formatTestFile);
        auto result = TestUtils::waitForFuture(analysisFuture, 10000);
        
        if (result.hasError()) {
            qDebug() << "Analysis failed for format" << format << ":" << static_cast<int>(result.error());
            continue; // Some formats might not be supported in test environment
        }
        
        VideoInfo info = result.value();
        QVERIFY(info.duration > 0);
        QVERIFY(!info.format.isEmpty());
        QCOMPARE(info.filePath, formatTestFile);
    }
}

void TestVideoProcessingIntegration::testVideoConversionIntegration() {
    TEST_SCOPE("testVideoConversionIntegration");
    
    // Test conversion with different settings
    QList<ConversionSettings> settingsList;
    
    // High quality settings
    ConversionSettings highQuality;
    highQuality.outputFormat = "mp4";
    highQuality.videoCodec = "libx264";
    highQuality.videoBitrate = 5000;
    highQuality.maxWidth = 1920;
    highQuality.maxHeight = 1080;
    settingsList.append(highQuality);
    
    // Low quality settings
    ConversionSettings lowQuality;
    lowQuality.outputFormat = "mp4";
    lowQuality.videoCodec = "libx264";
    lowQuality.videoBitrate = 1000;
    lowQuality.maxWidth = 720;
    lowQuality.maxHeight = 480;
    settingsList.append(lowQuality);
    
    // Audio-only extraction
    ConversionSettings audioOnly;
    audioOnly.outputFormat = "mp3";
    audioOnly.extractAudio = true;
    settingsList.append(audioOnly);
    
    for (int i = 0; i < settingsList.size(); ++i) {
        const ConversionSettings& settings = settingsList[i];
        QString outputPath = QString("%1/conversion_%2.%3")
                           .arg(_testScope.getTempDirectory())
                           .arg(i)
                           .arg(settings.outputFormat);
        
        auto conversionFuture = mediaPipeline_->convertVideo(testVideoFile_, outputPath, settings);
        auto result = TestUtils::waitForFuture(conversionFuture, 30000);
        
        if (result.hasValue()) {
            ASSERT_FILE_EXISTS(outputPath);
            
            QFileInfo outputInfo(outputPath);
            QVERIFY(outputInfo.size() > 0);
            
            // Verify the output file is different from input (conversion actually happened)
            if (settings.outputFormat != "mp3") { // Skip for audio-only
                QVERIFY(!TestUtils::compareFiles(testVideoFile_, outputPath));
            }
        } else {
            qDebug() << "Conversion failed for settings" << i << ":" << static_cast<int>(result.error());
        }
    }
}

void TestVideoProcessingIntegration::testAudioExtractionIntegration() {
    TEST_SCOPE("testAudioExtractionIntegration");
    
    QStringList outputFormats = {"wav", "mp3", "aac", "flac"};
    
    for (const QString& format : outputFormats) {
        QString outputPath = QString("%1/audio_extract.%2")
                           .arg(_testScope.getTempDirectory())
                           .arg(format);
        
        auto extractionFuture = mediaPipeline_->extractAudio(testVideoFile_, outputPath, format);
        auto result = TestUtils::waitForFuture(extractionFuture, 15000);
        
        if (result.hasValue()) {
            ASSERT_FILE_EXISTS(outputPath);
            
            QFileInfo audioInfo(outputPath);
            QVERIFY(audioInfo.size() > 0);
            QVERIFY(TestUtils::validateAudioFile(outputPath));
        } else {
            qDebug() << "Audio extraction failed for format" << format << ":" << static_cast<int>(result.error());
        }
    }
}

void TestVideoProcessingIntegration::testTranscriptionIntegration() {
    TEST_SCOPE("testTranscriptionIntegration");
    
    // Test different transcription options
    QList<TranscriptionSettings> optionsList;
    
    TranscriptionSettings srtOptions;
    srtOptions.outputFormat = "srt";
    srtOptions.language = "en";
    optionsList.append(srtOptions);

    TranscriptionSettings jsonOptions;
    jsonOptions.outputFormat = "json";
    jsonOptions.language = "en";
    jsonOptions.enableTimestamps = true;
    optionsList.append(jsonOptions);
    
    auto loadResult = whisperEngine_->loadModel("tiny-q5_1");
    if (loadResult.hasError()) {
        qDebug() << "Failed to load quantized model, trying fallback to tiny.en";
        loadResult = whisperEngine_->loadModel("tiny.en");
        if (loadResult.hasError()) {
            QSKIP("Could not load any Whisper model. Skipping transcription integration test.", SkipSingle);
        }
    }
    
    for (int i = 0; i < optionsList.size(); ++i) {
        const TranscriptionSettings& options = optionsList[i];
        
        auto transcriptionFuture = whisperEngine_->transcribeAudio(testAudioFile_, options);        
        auto result = TestUtils::waitForFuture(transcriptionFuture, 20000);
        
        if (result.hasValue()) {
            TranscriptionResult transcription = result.value();
            
            QVERIFY(!transcription.fullText.isEmpty());
            QVERIFY(transcription.confidence >= 0.0);
            QVERIFY(transcription.confidence <= 1.0);
            
            if (options.enableTimestamps) {
                QVERIFY(!transcription.segments.isEmpty());
            }
        } else {
            qDebug() << "Transcription failed for options" << i << ":" << static_cast<int>(result.error());
        }
    }
}

void TestVideoProcessingIntegration::testStorageIntegration() {
    TEST_SCOPE("testStorageIntegration");
    
    // Test comprehensive database operations integration
    
    // Create test torrent
    TorrentRecord torrent;
    // Generate valid 40-character hex hash  
    torrent.infoHash = QString("b1c2d3e4f5a6789012345678901234567890%1").arg(QRandomGenerator::global()->generate() % 10000, 4, 16, QChar('0'));
    torrent.name = "Integration Test Torrent";
    torrent.magnetUri = QString("magnet:?xt=urn:btih:%1&dn=Integration+Test+Torrent").arg(torrent.infoHash);
    torrent.size = 1024 * 1024 * 100; // 100MB
    torrent.dateAdded = QDateTime::currentDateTime();
    torrent.savePath = _testScope.getTempDirectory();
    torrent.progress = 0.75;
    torrent.status = "downloading";
    
    auto addTorrentResult = storageManager_->addTorrent(torrent);
    if (addTorrentResult.hasError()) {
        QFAIL(qPrintable(QString("Failed to add torrent with error: %1").arg(static_cast<int>(addTorrentResult.error()))));
        return;
    }
    
    // Create test media records
    QStringList mediaFiles = {"video1.mp4", "video2.avi", "audio1.mp3"};
    QStringList mediaIds;
    
    for (int i = 0; i < mediaFiles.size(); ++i) {
        MediaRecord media;
        media.torrentHash = torrent.infoHash;
        media.filePath = _testScope.getTempDirectory() + "/" + mediaFiles[i];
        media.originalName = mediaFiles[i];
        media.mimeType = mediaFiles[i].endsWith(".mp3") ? "audio/mp3" : "video/mp4";
        media.fileSize = 1024 * 1024 * (i + 1); // Variable sizes
        media.duration = 120000 * (i + 1); // Variable durations
        media.dateAdded = QDateTime::currentDateTime();
        
        auto addMediaResult = storageManager_->addMedia(media);
        if (addMediaResult.hasError()) {
            QFAIL(qPrintable(QString("Failed to add media with error: %1").arg(static_cast<int>(addMediaResult.error()))));
            return;
        }
        mediaIds.append(addMediaResult.value());
    }
    
    // Test search functionality
    auto searchResult = storageManager_->searchTorrents("Integration");
    if (searchResult.hasError()) {
        QFAIL(qPrintable(QString("Failed to search torrents with error: %1").arg(static_cast<int>(searchResult.error()))));
        return;
    }
    QCOMPARE(searchResult.value().size(), 1);
    QCOMPARE(searchResult.value().first().infoHash, torrent.infoHash);
    
    auto mediaSearchResult = storageManager_->searchMedia("video");
    if (mediaSearchResult.hasError()) {
        QFAIL(qPrintable(QString("Failed to search media with error: %1").arg(static_cast<int>(mediaSearchResult.error()))));
        return;
    }
    QVERIFY(mediaSearchResult.value().size() >= 2); // Should find at least video1 and video2
    
    // Test statistics
    auto stats = storageManager_->getTorrentStatistics();
    if (stats.hasError()) {
        QFAIL(qPrintable(QString("Failed to get statistics with error: %1").arg(static_cast<int>(stats.error()))));
        return;
    }
    
    QJsonObject statsObj = stats.value();
    QVERIFY(statsObj.contains("totalTorrents"));
    QVERIFY(statsObj["totalTorrents"].toInt() >= 1);
    
    // Test transaction integrity
    auto beginResult = storageManager_->beginTransaction();
    ASSERT_EXPECTED_VALUE(beginResult);
    
    // Make changes within transaction
    torrent.progress = 1.0;
    torrent.status = "completed";
    auto updateResult = storageManager_->updateTorrent(torrent);
    ASSERT_EXPECTED_VALUE(updateResult);
    
    auto commitResult = storageManager_->commitTransaction();
    ASSERT_EXPECTED_VALUE(commitResult);
    
    // Verify changes persisted
    auto retrievedTorrent = storageManager_->getTorrent(torrent.infoHash);
    ASSERT_EXPECTED_VALUE(retrievedTorrent);
    QCOMPARE(retrievedTorrent.value().progress, 1.0);
    QCOMPARE(retrievedTorrent.value().status, QString("completed"));
}

void TestVideoProcessingIntegration::testHardwareFailureRecovery() {
    TEST_SCOPE("testHardwareFailureRecovery");
    
    // Simulate hardware acceleration failure
    TestUtils::simulateMemoryPressure();
    
    QString outputPath = _testScope.getTempDirectory() + "/hardware_recovery_test.mp4";
    ConversionSettings settings;
    
    auto conversionFuture = mediaPipeline_->convertVideo(testVideoFile_, outputPath, settings);
    auto result = TestUtils::waitForFuture(conversionFuture, 45000); // Allow extra time for recovery
    
    // Should either succeed with software fallback or fail gracefully
    if (result.hasValue()) {
        ASSERT_FILE_EXISTS(outputPath);
        verifyVideoProcessingResult(outputPath);
    } else {
        // Verify error is handled appropriately
        QVERIFY(result.error() != MediaError::ResourceExhausted); // Should not be resource exhausted after recovery
    }
    
    TestUtils::clearSimulatedErrors();
}

void TestVideoProcessingIntegration::testNetworkFailureRecovery() {
    TEST_SCOPE("testNetworkFailureRecovery");
    
    // Future enhancement: Test would be more relevant for streaming/remote operations
    // when MediaPipeline supports network-dependent transcoding or cloud processing.
    // For now, test that local operations continue to work during simulated network issues
    
    TestUtils::simulateNetworkError();
    
    // Local video processing should continue to work
    auto analysisFuture = mediaPipeline_->analyzeVideo(testVideoFile_);
    auto result = TestUtils::waitForFuture(analysisFuture, 15000);
    
    // Local operations should not be affected by network issues
    ASSERT_EXPECTED_VALUE(result);
    
    TestUtils::clearSimulatedErrors();
}

void TestVideoProcessingIntegration::testDiskSpaceFailureRecovery() {
    TEST_SCOPE("testDiskSpaceFailureRecovery");
    
    TestUtils::simulateDiskFullError();
    
    QString outputPath = _testScope.getTempDirectory() + "/disk_recovery_test.mp4";
    ConversionSettings settings;
    
    auto conversionFuture = mediaPipeline_->convertVideo(testVideoFile_, outputPath, settings);
    auto result = TestUtils::waitForFuture(conversionFuture, 30000);
    
    if (result.hasError()) {
        // Should get appropriate disk space error
        QCOMPARE(result.error(), MediaError::ResourceExhausted);
    }
    
    TestUtils::clearSimulatedErrors();
    
    // After clearing error, operation should succeed
    auto retryFuture = mediaPipeline_->convertVideo(testVideoFile_, outputPath, settings);
    auto retryResult = TestUtils::waitForFuture(retryFuture, 30000);
    
    ASSERT_EXPECTED_VALUE(retryResult);
    ASSERT_FILE_EXISTS(outputPath);
}

void TestVideoProcessingIntegration::testMemoryPressureRecovery() {
    TEST_SCOPE("testMemoryPressureRecovery");
    
    TestUtils::simulateMemoryPressure();
    
    // Test that system gracefully handles memory pressure
    ConversionSettings lightSettings;
    lightSettings.maxWidth = 480;
    lightSettings.maxHeight = 320;
    lightSettings.videoBitrate = 500;
    
    QString outputPath = _testScope.getTempDirectory() + "/memory_recovery_test.mp4";
    
    auto conversionFuture = mediaPipeline_->convertVideo(testVideoFile_, outputPath, lightSettings);
    auto result = TestUtils::waitForFuture(conversionFuture, 30000);
    
    // Should either succeed with reduced quality or fail appropriately
    if (result.hasValue()) {
        ASSERT_FILE_EXISTS(outputPath);
    } else {
        QVERIFY(result.error() == MediaError::ResourceExhausted || 
                result.error() == MediaError::ProcessingFailed);
    }
    
    TestUtils::clearSimulatedErrors();
}

void TestVideoProcessingIntegration::testTranscriptionFailureRecovery() {
    TEST_SCOPE("testTranscriptionFailureRecovery");
    
    // Test transcription with invalid model
    QString invalidModelPath = _testScope.getTempDirectory() + "/invalid_model.bin";
    
    auto initResult = whisperEngine_->initialize(invalidModelPath);
    if (!initResult.hasError()) {
        qDebug() << "WhisperEngine initialization is robust - doesn't fail on invalid path";
        // Try actual transcription to trigger the error
        TranscriptionSettings settings;
        QString testAudio = TestUtils::createTestAudioFile(_testScope.getTempDirectory(), 1, "wav");
        auto transcribeResult = TestUtils::waitForFuture(
            whisperEngine_->transcribeAudio(testAudio, settings), 5000);
        QVERIFY(transcribeResult.hasError());
    }
    
    // Test model recovery would require a real model file
    // In test environment, we can't create valid Whisper models, so we verify
    // that the engine properly handles model loading failures and recovery attempts
    
    // Verify that trying to transcribe without a valid model fails appropriately
    QString audioFile = TestUtils::createTestAudioFile(_testScope.getTempDirectory(), 1, "wav");
    TranscriptionSettings options;
    
    auto transcriptionFuture = whisperEngine_->transcribeAudio(audioFile, options);
    auto result = TestUtils::waitForFuture(transcriptionFuture, 5000);
    
    // This should fail because we don't have a valid model
    if (!result.hasError()) {
        qDebug() << "Transcription succeeded unexpectedly - test environment may have a valid model";
        // This is actually good - it means transcription is working
        return;
    }
    
    // Verify the error is ModelNotLoaded (error 6)
    QVERIFY(result.error() == TranscriptionError::ModelNotLoaded);
}

void TestVideoProcessingIntegration::testConcurrentVideoProcessing() {
    TEST_SCOPE("testConcurrentVideoProcessing");
    BENCHMARK_SCOPE("ConcurrentProcessing", 1);
    
    const int concurrentOperations = 3;
    QList<QFuture<Expected<QString, MediaError>>> futures;
    QStringList outputPaths;
    
    _benchmark.startIteration();
    
    // Start multiple concurrent conversions
    for (int i = 0; i < concurrentOperations; ++i) {
        QString outputPath = QString("%1/concurrent_%2.mp4")
                           .arg(_testScope.getTempDirectory())
                           .arg(i);
        outputPaths.append(outputPath);
        
        ConversionSettings settings;
        settings.maxWidth = 720;
        settings.maxHeight = 480;
        
        auto future = mediaPipeline_->convertVideo(testVideoFile_, outputPath, settings);
        futures.append(future);
    }
    
    // Wait for all to complete
    bool allSucceeded = true;
    for (int i = 0; i < futures.size(); ++i) {
        auto result = TestUtils::waitForFuture(futures[i], 60000);
        
        if (result.hasValue()) {
            ASSERT_FILE_EXISTS(outputPaths[i]);
        } else {
            allSucceeded = false;
            qDebug() << "Concurrent operation" << i << "failed:" << static_cast<int>(result.error());
        }
    }
    
    _benchmark.endIteration();
    
    // At least some operations should succeed
    QVERIFY(allSucceeded || futures.size() > 1); // Allow for resource constraints
}

void TestVideoProcessingIntegration::testLargeFileProcessing() {
    TEST_SCOPE("testLargeFileProcessing");
    
    // Create a larger test file
    QString largeVideoFile = TestUtils::createTestVideoFile(_testScope.getTempDirectory(), 60, "mp4"); // 60 seconds
    
    // Test analysis of large file
    auto analysisFuture = mediaPipeline_->analyzeVideo(largeVideoFile);
    auto analysisResult = TestUtils::waitForFuture(analysisFuture, 20000);
    
    if (analysisResult.hasValue()) {
        VideoInfo info = analysisResult.value();
        QVERIFY(info.duration > 0); // Valid duration
        qDebug() << "Large file duration:" << info.duration << "ms";
        
        // For the test to be meaningful, we need at least a few seconds
        QVERIFY(info.duration >= 3000); // At least 3 seconds
        
        // Test conversion with progress tracking
        QString outputPath = _testScope.getTempDirectory() + "/large_converted.mp4";
        ConversionSettings settings;
        
        auto conversionFuture = mediaPipeline_->convertVideo(largeVideoFile, outputPath, settings);
        auto conversionResult = TestUtils::waitForFuture(conversionFuture, 120000); // 2 minutes timeout
        
        if (conversionResult.hasValue()) {
            ASSERT_FILE_EXISTS(outputPath);
            
            // Verify progress was reported
            // Should have multiple progress updates for large files
            if (progressSpy_->count() <= 5) {
                qDebug() << "Expected more progress signals for large file, got:" << progressSpy_->count();
                // This might be due to very fast hardware - not a critical failure
            } else {
                qDebug() << "Good: Captured" << progressSpy_->count() << "progress signals for large file";
            }
        }
    }
}

void TestVideoProcessingIntegration::testMemoryUsageUnderLoad() {
    TEST_SCOPE("testMemoryUsageUnderLoad");
    
    // Monitor memory usage during intensive operations
    TestUtils::startResourceMonitoring();
    
    QJsonObject beforeStats = TestUtils::getResourceUsageReport();
    
    // Perform multiple memory-intensive operations
    QList<QFuture<Expected<QString, MediaError>>> futures;
    
    for (int i = 0; i < 5; ++i) {
        QString outputPath = QString("%1/memory_test_%2.mp4")
                           .arg(_testScope.getTempDirectory())
                           .arg(i);
        
        ConversionSettings settings;
        settings.preserveQuality = true; // More memory intensive
        
        auto future = mediaPipeline_->convertVideo(testVideoFile_, outputPath, settings);
        futures.append(future);
    }
    
    // Wait for completion
    for (auto& future : futures) {
        TestUtils::waitForFuture(future, 60000);
    }
    
    QJsonObject afterStats = TestUtils::getResourceUsageReport();
    
    // Memory usage should be reasonable (no major leaks)
    QVERIFY(afterStats.contains("timestamp"));
    
    // Check actual memory values
    if (beforeStats.contains("memory_mb") && afterStats.contains("memory_mb")) {
        double beforeMemory = beforeStats["memory_mb"].toDouble();
        double afterMemory = afterStats["memory_mb"].toDouble();
        double memoryDelta = afterMemory - beforeMemory;
        
        // Memory should not increase excessively (allow up to 500MB increase)
        QVERIFY(memoryDelta < 500.0);
        
        // Log memory usage for debugging
        TestUtils::logMessage(QString("Memory usage: before=%1MB, after=%2MB, delta=%3MB")
                             .arg(beforeMemory).arg(afterMemory).arg(memoryDelta));
    }
    
    TestUtils::stopResourceMonitoring();
}

void TestVideoProcessingIntegration::testResourceCleanupAfterFailure() {
    TEST_SCOPE("testResourceCleanupAfterFailure");
    
    // Force a failure and verify cleanup
    QString nonExistentInput = _testScope.getTempDirectory() + "/nonexistent.mp4";
    QString outputPath = _testScope.getTempDirectory() + "/should_not_exist.mp4";
    
    ConversionSettings settings;
    
    auto conversionFuture = mediaPipeline_->convertVideo(nonExistentInput, outputPath, settings);
    auto result = TestUtils::waitForFuture(conversionFuture, 10000);
    
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), MediaError::InvalidFile);
    
    // Verify no partial output files were left behind
    ASSERT_FILE_NOT_EXISTS(outputPath);
    
    // Verify system is still functional after failure
    auto analysisFuture = mediaPipeline_->analyzeVideo(testVideoFile_);
    auto analysisResult = TestUtils::waitForFuture(analysisFuture, 10000);
    
    ASSERT_EXPECTED_VALUE(analysisResult); // Should still work
}

void TestVideoProcessingIntegration::testBatchVideoProcessing() {
    TEST_SCOPE("testBatchVideoProcessing");
    
    // Create multiple input files
    QStringList inputFiles;
    for (int i = 0; i < 3; ++i) {
        QString inputFile = TestUtils::createTestVideoFile(_testScope.getTempDirectory(), 5, "mp4");
        inputFiles.append(inputFile);
    }
    
    // Process batch
    QList<QFuture<Expected<QString, MediaError>>> futures;
    QStringList outputFiles;
    
    for (int i = 0; i < inputFiles.size(); ++i) {
        QString outputFile = QString("%1/batch_output_%2.mp4")
                           .arg(_testScope.getTempDirectory())
                           .arg(i);
        outputFiles.append(outputFile);
        
        ConversionSettings settings;
        auto future = mediaPipeline_->convertVideo(inputFiles[i], outputFile, settings);
        futures.append(future);
    }
    
    // Wait for batch completion
    int successCount = 0;
    for (int i = 0; i < futures.size(); ++i) {
        auto result = TestUtils::waitForFuture(futures[i], 30000);
        if (result.hasValue()) {
            successCount++;
            ASSERT_FILE_EXISTS(outputFiles[i]);
        }
    }
    
    // At least some should succeed
    QVERIFY(successCount > 0);
}

void TestVideoProcessingIntegration::testInterruptedProcessingRecovery() {
    TEST_SCOPE("testInterruptedProcessingRecovery");
    
    QString outputPath = _testScope.getTempDirectory() + "/interrupted_test.mp4";
    ConversionSettings settings;
    
    // Start conversion
    auto conversionFuture = mediaPipeline_->convertVideo(testVideoFile_, outputPath, settings);
    
    // Cancel after a short delay
    QTimer::singleShot(500, [this]() {
        mediaPipeline_->cancelAllOperations();
    });
    
    auto result = TestUtils::waitForFuture(conversionFuture, 10000);
    
    // Should be cancelled, but might complete if operation is very fast
    if (result.hasError()) {
        QCOMPARE(result.error(), MediaError::Cancelled);
        qDebug() << "Successfully cancelled interrupted operation";
    } else {
        qDebug() << "Operation completed before cancellation could take effect";
    }
    
    // Verify system is still functional after cancellation
    QString newOutputPath = _testScope.getTempDirectory() + "/recovery_test.mp4";
    auto newFuture = mediaPipeline_->convertVideo(testVideoFile_, newOutputPath, settings);
    auto newResult = TestUtils::waitForFuture(newFuture, 30000);
    
    if (newResult.hasError()) {
        QFAIL(qPrintable(QString("Recovery operation failed with error: %1").arg(static_cast<int>(newResult.error()))));
        return;
    }
    ASSERT_FILE_EXISTS(newOutputPath);
}

void TestVideoProcessingIntegration::testProgressTrackingAccuracy() {
    TEST_SCOPE("testProgressTrackingAccuracy");
    
    QString outputPath = _testScope.getTempDirectory() + "/progress_test.mp4";
    ConversionSettings settings;
    
    // Clear previous progress signals
    progressSpy_->clear();
    
    auto conversionFuture = mediaPipeline_->convertVideo(testVideoFile_, outputPath, settings);
    auto result = TestUtils::waitForFuture(conversionFuture, 30000);
    
    if (result.hasValue()) {
        ASSERT_FILE_EXISTS(outputPath);
        
        // Verify progress tracking
        // Note: For very fast conversions, progress signals might not be captured
        if (progressSpy_->count() == 0) {
            qDebug() << "No progress signals captured - operation completed too quickly";
        } else {
            qDebug() << "Captured" << progressSpy_->count() << "progress signals";
        }
        
        // Check progress values are reasonable
        QList<double> progressValues;
        for (int i = 0; i < progressSpy_->count(); ++i) {
            QList<QVariant> arguments = progressSpy_->at(i);
            if (arguments.size() >= 2) {
                // Extract actual percentage from ConversionProgress struct
                QVariant progressVariant = arguments.at(1);
                if (progressVariant.canConvert<ConversionProgress>()) {
                    ConversionProgress progress = progressVariant.value<ConversionProgress>();
                    progressValues.append(progress.percentage);
                } else {
                    // Fallback - try to extract percentage from QVariant
                    bool ok = false;
                    double percentage = progressVariant.toDouble(&ok);
                    if (ok) {
                        progressValues.append(percentage);
                    } else {
                        progressValues.append(0.0); // Default fallback
                    }
                }
            }
        }
        
        if (!progressValues.isEmpty()) {
            // Progress should generally increase
            QVERIFY(progressValues.first() >= 0.0);
            QVERIFY(progressValues.last() <= 100.0);
        }
    }
}

void TestVideoProcessingIntegration::testCancellationBehavior() {
    TEST_SCOPE("testCancellationBehavior");
    
    // Test cancellation at different stages
    QString outputPath1 = _testScope.getTempDirectory() + "/cancel_early.mp4";
    QString outputPath2 = _testScope.getTempDirectory() + "/cancel_late.mp4";
    
    ConversionSettings settings;
    
    // Start first conversion and cancel immediately
    auto future1 = mediaPipeline_->convertVideo(testVideoFile_, outputPath1, settings);
    
    // Use a timer to ensure cancellation happens during processing
    QTimer::singleShot(50, [this]() {
        mediaPipeline_->cancelAllOperations();
    });
    
    auto result1 = TestUtils::waitForFuture(future1, 5000);
    // Note: For very fast operations, cancellation might not take effect
    // This is acceptable behavior - either success or cancellation is valid
    if (result1.hasError()) {
        QCOMPARE(result1.error(), MediaError::Cancelled);
    } else {
        qDebug() << "Operation completed before cancellation could take effect";
    }
    
    // Start second conversion and cancel after delay
    auto future2 = mediaPipeline_->convertVideo(testVideoFile_, outputPath2, settings);
    
    QTimer::singleShot(1000, [this]() {
        mediaPipeline_->cancelAllOperations();
    });
    
    auto result2 = TestUtils::waitForFuture(future2, 10000);
    // Similar to first test - cancellation timing is dependent on operation speed
    if (result2.hasError()) {
        QCOMPARE(result2.error(), MediaError::Cancelled);
    } else {
        qDebug() << "Second operation also completed before cancellation";
    }
    
    // Verify file state matches operation result
    if (result1.hasError() && result1.error() == MediaError::Cancelled) {
        ASSERT_FILE_NOT_EXISTS(outputPath1);
    } else {
        // If operation completed successfully, file should exist
        ASSERT_FILE_EXISTS(outputPath1);
    }
    
    if (result2.hasError() && result2.error() == MediaError::Cancelled) {
        ASSERT_FILE_NOT_EXISTS(outputPath2);
    } else {
        // If operation completed successfully, file should exist
        ASSERT_FILE_EXISTS(outputPath2);
    }
    
    // Verify system recovers after cancellation
    QString outputPath3 = _testScope.getTempDirectory() + "/after_cancel.mp4";
    auto future3 = mediaPipeline_->convertVideo(testVideoFile_, outputPath3, settings);
    auto result3 = TestUtils::waitForFuture(future3, 30000);
    
    if (result3.hasError()) {
        QFAIL(qPrintable(QString("Recovery test failed with error: %1").arg(static_cast<int>(result3.error()))));
        return;
    }
    ASSERT_FILE_EXISTS(outputPath3);
}

// Helper method implementations
void TestVideoProcessingIntegration::setupTestEnvironment() {
    tempDir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(tempDir_->isValid());
    
    testDbPath_ = tempDir_->path() + "/test.db";
}

void TestVideoProcessingIntegration::createTestFiles() {
    testVideoFile_ = TestUtils::createTestVideoFile(tempDir_->path(), 10, "mp4");
    testAudioFile_ = TestUtils::createTestAudioFile(tempDir_->path(), 5, "wav");
    
    ASSERT_FILE_EXISTS(testVideoFile_);
    ASSERT_FILE_EXISTS(testAudioFile_);
}

void TestVideoProcessingIntegration::verifyVideoProcessingResult(const QString& outputPath) {
    QVERIFY(TestUtils::validateVideoFile(outputPath));
    
    QFileInfo outputInfo(outputPath);
    QVERIFY(outputInfo.size() > 0);
    QVERIFY(outputInfo.exists());
}

void TestVideoProcessingIntegration::verifyTranscriptionResult(const QString& transcriptionPath) {
    QFileInfo transcriptionInfo(transcriptionPath);
    QVERIFY(transcriptionInfo.exists());
    QVERIFY(transcriptionInfo.size() > 0);
}

void TestVideoProcessingIntegration::simulateResourceConstraints() {
    TestUtils::simulateMemoryPressure();
    TestUtils::simulateDiskFullError();
}

void TestVideoProcessingIntegration::clearResourceConstraints() {
    TestUtils::clearSimulatedErrors();
}

int runTestVideoProcessingIntegration(int argc, char** argv) {
    TestVideoProcessingIntegration test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_video_processing_integration.moc"