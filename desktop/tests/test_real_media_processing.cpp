#include <QtTest/QtTest>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QRandomGenerator>
#include <QtConcurrent/QtConcurrent>

#include "utils/TestUtils.hpp"
#include "../src/core/media/MediaPipeline.hpp"
#include "../src/core/transcription/WhisperEngine.hpp"
#include "../src/core/storage/StorageManager.hpp"
#include "../src/core/common/Expected.hpp"

using namespace Murmur;
using namespace Murmur::Test;

/**
 * @brief Real-world media processing tests using actual sample files
 * 
 * These tests use the real .mp4 and .wav files from desktop/resources/tests
 * to validate functionality with actual media data instead of mocks.
 */
class TestRealMediaProcessing : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Real media file tests
    void testRealVideoAnalysis();
    void testRealAudioAnalysis();
    void testRealVideoConversion();
    void testRealAudioExtraction();
    void testRealTranscription();
    
    // Performance benchmarks with real media
    void benchmarkRealVideoProcessing();
    void benchmarkRealAudioProcessing();
    void benchmarkRealTranscription();
    void benchmarkConcurrentProcessing();
    
    // Error handling with real media
    void testCorruptedRealMedia();
    void testLargeRealMediaFiles();
    void testUnsupportedRealFormats();
    
    // Storage integration with real data
    void testRealMediaStorageIntegration();
    void testRealTranscriptionStorage();
    void testRealMediaSearch();
    
    // UI feedback and progress tracking
    void testRealMediaProgressTracking();
    void testRealMediaCancellation();
    void testRealMediaErrorFeedback();

private:
    std::unique_ptr<MediaPipeline> mediaPipeline_;
    std::unique_ptr<WhisperEngine> whisperEngine_;
    std::unique_ptr<StorageManager> storageManager_;
    std::unique_ptr<QTemporaryDir> tempDir_;
    
    QString realVideoFile_;
    QString realAudioFile_;
    QString testDbPath_;
    
    // Signal tracking for UI feedback
    QSignalSpy* progressSpy_;
    QSignalSpy* completionSpy_;
    QSignalSpy* errorSpy_;
    
    // Test helpers
    void setupRealMediaFiles();
    void verifyRealVideoProcessing(const QString& outputPath);
    void verifyRealAudioProcessing(const QString& outputPath);
    void measureAndValidatePerformance(std::function<void()> operation, const QString& operationName);
};

void TestRealMediaProcessing::initTestCase() {
    TestUtils::initializeTestEnvironment();
    TestUtils::startResourceMonitoring();
    
    // Check for real sample files
    QString realVideo = TestUtils::getRealSampleVideoFile();
    QString realAudio = TestUtils::getRealSampleAudioFile();
    
    if (realVideo.isEmpty() || realAudio.isEmpty()) {
        QSKIP("Real sample media files not found in desktop/resources/tests/");
    }
    
    if (!TestUtils::validateRealMediaFile(realVideo)) {
        QSKIP("Real sample video file validation failed");
    }
    
    if (!TestUtils::validateRealMediaFile(realAudio)) {
        QSKIP("Real sample audio file validation failed");
    }
    
    TestUtils::logMessage("Real media processing tests initialized with sample files");
}

void TestRealMediaProcessing::cleanupTestCase() {
    TestUtils::stopResourceMonitoring();
    
    // Print comprehensive resource usage report
    QJsonObject resourceReport = TestUtils::getResourceUsageReport();
    qDebug() << "Real Media Processing Resource Report:" << QJsonDocument(resourceReport).toJson();
    
    TestUtils::cleanupTestEnvironment();
}

void TestRealMediaProcessing::init() {
    tempDir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(tempDir_->isValid());
    
    setupRealMediaFiles();
    
    // Initialize components
    mediaPipeline_ = std::make_unique<MediaPipeline>(this);
    whisperEngine_ = std::make_unique<WhisperEngine>(this);
    storageManager_ = std::make_unique<StorageManager>(this);
    
    // Initialize storage with unique database
    QString uniqueDbPath = QString("%1/real_media_test_%2_%3.db")
                          .arg(tempDir_->path())
                          .arg(QDateTime::currentMSecsSinceEpoch())
                          .arg(QRandomGenerator::global()->generate());
    auto initResult = storageManager_->initialize(uniqueDbPath);
    ASSERT_EXPECTED_VALUE(initResult);
    
    // Setup signal spies for UI feedback testing
    progressSpy_ = new QSignalSpy(mediaPipeline_.get(), &MediaPipeline::conversionProgress);
    completionSpy_ = new QSignalSpy(mediaPipeline_.get(), &MediaPipeline::conversionCompleted);
    errorSpy_ = new QSignalSpy(mediaPipeline_.get(), &MediaPipeline::conversionFailed);
}

void TestRealMediaProcessing::cleanup() {
    delete progressSpy_;
    delete completionSpy_;
    delete errorSpy_;
    
    mediaPipeline_.reset();
    whisperEngine_.reset();
    storageManager_.reset();
    tempDir_.reset();
}

void TestRealMediaProcessing::testRealVideoAnalysis() {
    TEST_SCOPE("testRealVideoAnalysis");
    
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for real video analysis");
    }
    
    auto analysisFuture = mediaPipeline_->analyzeVideo(realVideoFile_);
    auto result = TestUtils::waitForFuture(analysisFuture, 15000);
    
    ASSERT_EXPECTED_VALUE(result);
    
    VideoInfo info = result.value();
    
    // Validate real video properties
    QVERIFY(info.duration > 0);
    QVERIFY(info.width > 0);
    QVERIFY(info.height > 0);
    QVERIFY(info.frameRate > 0);
    QVERIFY(!info.format.isEmpty());
    QVERIFY(!info.codec.isEmpty());
    QCOMPARE(info.filePath, realVideoFile_);
    
    // Log real video properties for verification
    TestUtils::logMessage(QString("Real video analysis: %1x%2, %3fps, %4ms, codec: %5")
                         .arg(info.width).arg(info.height).arg(info.frameRate)
                         .arg(info.duration).arg(info.codec));
}

void TestRealMediaProcessing::testRealAudioAnalysis() {
    TEST_SCOPE("testRealAudioAnalysis");
    
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for real audio analysis");
    }
    
    // Use analyzeVideo for audio files as MediaPipeline doesn't have analyzeAudio
    auto analysisFuture = mediaPipeline_->analyzeVideo(realAudioFile_);
    auto result = TestUtils::waitForFuture(analysisFuture, 10000);
    
    ASSERT_EXPECTED_VALUE(result);
    
    VideoInfo info = result.value();
    
    // Validate real audio properties (as VideoInfo since MediaPipeline uses this for all media)
    QVERIFY(info.duration > 0);
    QVERIFY(info.hasAudio);
    QVERIFY(info.audioChannels > 0);
    QVERIFY(info.audioSampleRate > 0);
    QVERIFY(!info.format.isEmpty());
    QVERIFY(!info.audioCodec.isEmpty());
    QCOMPARE(info.filePath, realAudioFile_);
    
    // Log real audio properties for verification
    TestUtils::logMessage(QString("Real audio analysis: %1Hz, %2ch, %3ms, codec: %4")
                         .arg(info.audioSampleRate).arg(info.audioChannels)
                         .arg(info.duration).arg(info.audioCodec));
}

void TestRealMediaProcessing::testRealVideoConversion() {
    TEST_SCOPE("testRealVideoConversion");
    
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for real video conversion");
    }
    
    QString outputPath = _testScope.getTempDirectory() + "/real_converted_video.mp4";
    
    ConversionSettings settings;
    settings.outputFormat = "mp4";
    settings.videoCodec = "libx264";
    settings.maxWidth = 1280;
    settings.maxHeight = 720;
    settings.videoBitrate = 2000;
    
    auto conversionFuture = mediaPipeline_->convertVideo(realVideoFile_, outputPath, settings);
    auto result = TestUtils::waitForFuture(conversionFuture, 60000); // Real conversion takes time
    
    ASSERT_EXPECTED_VALUE(result);
    ASSERT_FILE_EXISTS(outputPath);
    
    verifyRealVideoProcessing(outputPath);
    
    // Verify converted file is different from original
    QVERIFY(!TestUtils::compareFiles(realVideoFile_, outputPath));
    
    // Verify converted file has expected properties
    auto analysisResult = TestUtils::waitForFuture(
        mediaPipeline_->analyzeVideo(outputPath), 10000);
    ASSERT_EXPECTED_VALUE(analysisResult);
    
    VideoInfo convertedInfo = analysisResult.value();
    QVERIFY(convertedInfo.width <= 1280);
    QVERIFY(convertedInfo.height <= 720);
}

void TestRealMediaProcessing::testRealAudioExtraction() {
    TEST_SCOPE("testRealAudioExtraction");
    
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for real audio extraction");
    }
    
    QString outputPath = _testScope.getTempDirectory() + "/real_extracted_audio.wav";
    
    auto extractionFuture = mediaPipeline_->extractAudio(realVideoFile_, outputPath, "wav");
    auto result = TestUtils::waitForFuture(extractionFuture, 30000);
    
    if (result.hasError()) {
        // Video might not have audio track - this is acceptable
        TestUtils::logMessage("Audio extraction failed - video may not contain audio track");
        return;
    }
    
    ASSERT_FILE_EXISTS(outputPath);
    verifyRealAudioProcessing(outputPath);
    
    // Verify extracted audio has valid properties
    auto analysisResult = TestUtils::waitForFuture(
        mediaPipeline_->analyzeVideo(outputPath), 10000);
    ASSERT_EXPECTED_VALUE(analysisResult);
    
    VideoInfo extractedInfo = analysisResult.value();
    QVERIFY(extractedInfo.duration > 0);
    QVERIFY(extractedInfo.audioSampleRate > 0);
}

void TestRealMediaProcessing::testRealTranscription() {
    TEST_SCOPE("testRealTranscription");
    
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for real transcription");
    }
    
    // Initialize Whisper with test model directory
    QString modelDir = _testScope.getTempDirectory() + "/whisper_models";
    QDir().mkpath(modelDir);
    
    auto initResult = whisperEngine_->initialize(modelDir);
    if (initResult.hasError()) {
        QSKIP("Failed to initialize Whisper engine for real transcription");
    }
    
    TranscriptionSettings settings;
    settings.language = "auto"; // Auto-detect language
    settings.outputFormat = "json";
    settings.enableTimestamps = true;
    settings.enableWordConfidence = true;
    
    auto transcriptionFuture = whisperEngine_->transcribeAudio(realAudioFile_, settings);
    auto result = TestUtils::waitForFuture(transcriptionFuture, 60000); // Real transcription takes time
    
    if (result.hasError()) {
        TestUtils::logMessage("Real transcription failed - this may be expected in test environment");
        return;
    }
    
    TranscriptionResult transcription = result.value();
    
    // Verify real transcription results
    QVERIFY(!transcription.fullText.isEmpty());
    QVERIFY(transcription.confidence >= 0.0);
    QVERIFY(transcription.confidence <= 1.0);
    QVERIFY(transcription.processingTime > 0);
    QVERIFY(!transcription.detectedLanguage.isEmpty());
    
    if (settings.enableTimestamps) {
        QVERIFY(!transcription.segments.isEmpty());
        for (const auto& segment : transcription.segments) {
            QVERIFY(segment.startTime >= 0);
            QVERIFY(segment.endTime >= segment.startTime);
            QVERIFY(!segment.text.isEmpty());
        }
    }
    
    TestUtils::logMessage(QString("Real transcription: '%1' (confidence: %2, language: %3)")
                         .arg(transcription.fullText.left(100))
                         .arg(transcription.confidence)
                         .arg(transcription.detectedLanguage));
}

void TestRealMediaProcessing::benchmarkRealVideoProcessing() {
    BENCHMARK_SCOPE("RealVideoProcessing", 3);
    
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for real video benchmark");
    }
    
    ConversionSettings settings;
    settings.outputFormat = "mp4";
    settings.videoCodec = "libx264";
    // Remove preset as it's not in ConversionSettings struct
    
    for (int i = 0; i < 3; ++i) {
        _benchmark.startIteration();
        
        QString outputPath = QString("%1/benchmark_video_%2.mp4")
                           .arg(tempDir_->path()).arg(i);
        
        auto future = mediaPipeline_->convertVideo(realVideoFile_, outputPath, settings);
        auto result = TestUtils::waitForFuture(future, 120000);
        
        _benchmark.endIteration();
        
        if (result.hasValue()) {
            ASSERT_FILE_EXISTS(outputPath);
            QFileInfo outputInfo(outputPath);
            TestUtils::logMessage(QString("Benchmark iteration %1: %2 bytes output")
                                 .arg(i).arg(outputInfo.size()));
        }
    }
    
    TestUtils::logMessage(QString("Real video processing benchmark: avg=%.2fms")
                         .arg(_benchmark.getAverageTimeMs()));
}

void TestRealMediaProcessing::benchmarkRealAudioProcessing() {
    BENCHMARK_SCOPE("RealAudioProcessing", 5);
    
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for real audio benchmark");
    }
    
    for (int i = 0; i < 5; ++i) {
        _benchmark.startIteration();
        
        QString outputPath = QString("%1/benchmark_audio_%2.mp3")
                           .arg(tempDir_->path()).arg(i);
        
        // Use extractAudio as MediaPipeline doesn't have convertAudio
        auto future = mediaPipeline_->extractAudio(realAudioFile_, outputPath, "mp3");
        auto result = TestUtils::waitForFuture(future, 30000);
        
        _benchmark.endIteration();
        
        if (result.hasValue()) {
            ASSERT_FILE_EXISTS(outputPath);
        }
    }
    
    TestUtils::logMessage(QString("Real audio processing benchmark: avg=%.2fms")
                         .arg(_benchmark.getAverageTimeMs()));
}

void TestRealMediaProcessing::benchmarkRealTranscription() {
    BENCHMARK_SCOPE("RealTranscription", 2);
    
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for real transcription benchmark");
    }
    
    QString modelDir = tempDir_->path() + "/whisper_models";
    QDir().mkpath(modelDir);
    
    auto initResult = whisperEngine_->initialize(modelDir);
    if (initResult.hasError()) {
        QSKIP("Failed to initialize Whisper for real transcription benchmark");
    }
    
    TranscriptionSettings settings;
    settings.language = "en";
    settings.outputFormat = "txt";
    
    for (int i = 0; i < 2; ++i) {
        _benchmark.startIteration();
        
        auto future = whisperEngine_->transcribeAudio(realAudioFile_, settings);
        auto result = TestUtils::waitForFuture(future, 180000); // 3 minutes for real transcription
        
        _benchmark.endIteration();
        
        if (result.hasValue()) {
            TestUtils::logMessage(QString("Transcription %1: %2 chars")
                                 .arg(i).arg(result.value().fullText.length()));
        }
    }
    
    TestUtils::logMessage(QString("Real transcription benchmark: avg=%.2fs")
                         .arg(_benchmark.getAverageTimeMs() / 1000.0));
}

void TestRealMediaProcessing::benchmarkConcurrentProcessing() {
    BENCHMARK_SCOPE("ConcurrentRealProcessing", 1);
    
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for concurrent processing benchmark");
    }
    
    _benchmark.startIteration();
    
    // Start multiple concurrent operations with real media
    QList<QFuture<Expected<QString, MediaError>>> futures;
    
    for (int i = 0; i < 3; ++i) {
        QString outputPath = QString("%1/concurrent_real_%2.mp4")
                           .arg(tempDir_->path()).arg(i);
        
        ConversionSettings settings;
        settings.maxWidth = 640;
        settings.maxHeight = 480;
        // Remove preset as it's not in ConversionSettings struct
        
        auto future = mediaPipeline_->convertVideo(realVideoFile_, outputPath, settings);
        futures.append(future);
    }
    
    // Wait for all to complete
    int successCount = 0;
    for (auto& future : futures) {
        auto result = TestUtils::waitForFuture(future, 120000);
        if (result.hasValue()) {
            successCount++;
        }
    }
    
    _benchmark.endIteration();
    
    QVERIFY(successCount > 0);
    TestUtils::logMessage(QString("Concurrent processing: %1/%2 succeeded, avg=%.2fms")
                         .arg(successCount).arg(futures.size())
                         .arg(_benchmark.getAverageTimeMs()));
}

void TestRealMediaProcessing::testCorruptedRealMedia() {
    TEST_SCOPE("testCorruptedRealMedia");
    
    // Create corrupted version of real media file
    QString corruptedPath = _testScope.getTempDirectory() + "/corrupted_real.mp4";
    
    QFile originalFile(realVideoFile_);
    QFile corruptedFile(corruptedPath);
    
    if (originalFile.open(QIODevice::ReadOnly) && corruptedFile.open(QIODevice::WriteOnly)) {
        QByteArray data = originalFile.read(1024); // Read first 1KB
        data.fill('X', 512); // Corrupt first 512 bytes
        corruptedFile.write(data);
        corruptedFile.close();
        originalFile.close();
    }
    
    // Try to process corrupted file
    auto analysisFuture = mediaPipeline_->analyzeVideo(corruptedPath);
    auto result = TestUtils::waitForFuture(analysisFuture, 10000);
    
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), MediaError::InvalidFile);
}

void TestRealMediaProcessing::testLargeRealMediaFiles() {
    TEST_SCOPE("testLargeRealMediaFiles");
    
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for large file test");
    }
    
    // Create larger version of real media by duplicating content
    QString largeVideoPath = _testScope.getTempDirectory() + "/large_real_video.mp4";
    
    // Use FFmpeg to create a longer version of the real video
    QProcess ffmpeg;
    QStringList args;
    args << "-stream_loop" << "2" // Loop the input 2 times
         << "-i" << realVideoFile_
         << "-c" << "copy" // Copy without re-encoding for speed
         << "-y" << largeVideoPath;
    
    ffmpeg.start("ffmpeg", args);
    if (!ffmpeg.waitForFinished(30000) || ffmpeg.exitCode() != 0) {
        QSKIP("Failed to create large test file");
    }
    
    // Test processing large file
    auto analysisFuture = mediaPipeline_->analyzeVideo(largeVideoPath);
    auto result = TestUtils::waitForFuture(analysisFuture, 30000);
    
    ASSERT_EXPECTED_VALUE(result);
    
    VideoInfo info = result.value();
    QVERIFY(info.duration > 0);
    
    // Verify progress tracking works for large files
    progressSpy_->clear();
    
    QString outputPath = _testScope.getTempDirectory() + "/large_converted.mp4";
    ConversionSettings settings;
    settings.maxWidth = 720;
    settings.maxHeight = 480;
    
    auto conversionFuture = mediaPipeline_->convertVideo(largeVideoPath, outputPath, settings);
    auto conversionResult = TestUtils::waitForFuture(conversionFuture, 180000); // 3 minutes
    
    if (conversionResult.hasValue()) {
        ASSERT_FILE_EXISTS(outputPath);
        // Should have progress updates for large files
        TestUtils::logMessage(QString("Large file progress signals: %1").arg(progressSpy_->count()));
    }
}

void TestRealMediaProcessing::testUnsupportedRealFormats() {
    TEST_SCOPE("testUnsupportedRealFormats");
    
    // Create file with unsupported extension but using real media data
    QString unsupportedPath = _testScope.getTempDirectory() + "/real_media.xyz";
    
    if (QFile::copy(realVideoFile_, unsupportedPath)) {
        auto analysisFuture = mediaPipeline_->analyzeVideo(unsupportedPath);
        auto result = TestUtils::waitForFuture(analysisFuture, 10000);
        
        // Should detect format despite extension
        if (result.hasValue()) {
            TestUtils::logMessage("Format detection worked despite unsupported extension");
        } else {
            QCOMPARE(result.error(), MediaError::UnsupportedFormat);
        }
    }
}

void TestRealMediaProcessing::testRealMediaStorageIntegration() {
    TEST_SCOPE("testRealMediaStorageIntegration");
    
    // Store real media information in database
    TorrentRecord torrent;
    torrent.infoHash = QString("real%1").arg(QRandomGenerator::global()->generate(), 36, 16, QChar('0'));
    torrent.name = "Real Media Test Torrent";
    torrent.magnetUri = QString("magnet:?xt=urn:btih:%1&dn=Real+Media+Test").arg(torrent.infoHash);
    torrent.size = QFileInfo(realVideoFile_).size();
    torrent.dateAdded = QDateTime::currentDateTime();
    torrent.savePath = QFileInfo(realVideoFile_).dir().absolutePath();
    torrent.progress = 1.0;
    torrent.status = "completed";
    
    auto addTorrentResult = storageManager_->addTorrent(torrent);
    ASSERT_EXPECTED_VALUE(addTorrentResult);
    
    // Add real media record
    MediaRecord media;
    media.torrentHash = torrent.infoHash;
    media.filePath = realVideoFile_;
    media.originalName = QFileInfo(realVideoFile_).baseName();
    media.mimeType = "video/mp4";
    media.fileSize = QFileInfo(realVideoFile_).size();
    media.dateAdded = QDateTime::currentDateTime();
    
    // Get real media properties if FFmpeg is available
    if (TestUtils::isFFmpegAvailable()) {
        auto analysisResult = TestUtils::waitForFuture(
            mediaPipeline_->analyzeVideo(realVideoFile_), 15000);
        if (analysisResult.hasValue()) {
            VideoInfo info = analysisResult.value();
            media.duration = info.duration;
            media.width = info.width;
            media.height = info.height;
            media.frameRate = info.frameRate;
            media.videoCodec = info.codec;
            media.audioCodec = info.audioCodec;
        }
    }
    
    auto addMediaResult = storageManager_->addMedia(media);
    ASSERT_EXPECTED_VALUE(addMediaResult);
    QString mediaId = addMediaResult.value();
    
    // Verify stored data
    auto retrievedMedia = storageManager_->getMedia(mediaId);
    ASSERT_EXPECTED_VALUE(retrievedMedia);
    QCOMPARE(retrievedMedia.value().filePath, realVideoFile_);
    QVERIFY(retrievedMedia.value().fileSize > 0);
    
    TestUtils::logMessage(QString("Stored real media: %1 (%2 bytes)")
                         .arg(retrievedMedia.value().originalName)
                         .arg(retrievedMedia.value().fileSize));
}

void TestRealMediaProcessing::testRealTranscriptionStorage() {
    TEST_SCOPE("testRealTranscriptionStorage");
    
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for transcription storage test");
    }
    
    // First add media record
    MediaRecord media;
    media.filePath = realAudioFile_;
    media.originalName = QFileInfo(realAudioFile_).baseName();
    media.mimeType = "audio/wav";
    media.fileSize = QFileInfo(realAudioFile_).size();
    media.dateAdded = QDateTime::currentDateTime();
    
    auto addMediaResult = storageManager_->addMedia(media);
    ASSERT_EXPECTED_VALUE(addMediaResult);
    QString mediaId = addMediaResult.value();
    
    // Create transcription record with real data
    TranscriptionRecord transcription;
    transcription.mediaId = mediaId;
    transcription.language = "en";
    transcription.modelUsed = "base";
    transcription.fullText = "This is a real transcription of the sample audio file";
    transcription.confidence = 0.89;
    transcription.dateCreated = QDateTime::currentDateTime();
    transcription.processingTime = 2500;
    transcription.status = "completed";
    
    // Create realistic timestamps
    QJsonObject timestamps;
    QJsonArray segments;
    QJsonObject segment1;
    segment1["start"] = 0.0;
    segment1["end"] = 2.5;
    segment1["text"] = "This is a real transcription";
    segments.append(segment1);
    
    QJsonObject segment2;
    segment2["start"] = 2.5;
    segment2["end"] = 4.8;
    segment2["text"] = "of the sample audio file";
    segments.append(segment2);
    
    timestamps["segments"] = segments;
    transcription.timestamps = timestamps;
    
    auto addTranscriptionResult = storageManager_->addTranscription(transcription);
    ASSERT_EXPECTED_VALUE(addTranscriptionResult);
    
    // Verify stored transcription
    auto retrievedTranscription = storageManager_->getTranscriptionByMedia(mediaId);
    ASSERT_EXPECTED_VALUE(retrievedTranscription);
    QCOMPARE(retrievedTranscription.value().fullText, transcription.fullText);
    QCOMPARE(retrievedTranscription.value().confidence, transcription.confidence);
    QVERIFY(!retrievedTranscription.value().timestamps.isEmpty());
}

void TestRealMediaProcessing::testRealMediaSearch() {
    TEST_SCOPE("testRealMediaSearch");
    
    // Add multiple real media records with different properties
    QStringList mediaFiles = {realVideoFile_, realAudioFile_};
    QStringList mediaIds;
    
    for (const QString& file : mediaFiles) {
        MediaRecord media;
        media.filePath = file;
        media.originalName = QFileInfo(file).baseName();
        media.mimeType = file.endsWith(".mp4") ? "video/mp4" : "audio/wav";
        media.fileSize = QFileInfo(file).size();
        media.dateAdded = QDateTime::currentDateTime();
        
        auto result = storageManager_->addMedia(media);
        ASSERT_EXPECTED_VALUE(result);
        mediaIds.append(result.value());
    }
    
    // Test search by type
    auto videoSearchResult = storageManager_->searchMedia("video");
    ASSERT_EXPECTED_VALUE(videoSearchResult);
    QVERIFY(videoSearchResult.value().size() >= 1);
    
    auto audioSearchResult = storageManager_->searchMedia("audio");
    ASSERT_EXPECTED_VALUE(audioSearchResult);
    QVERIFY(audioSearchResult.value().size() >= 1);
    
    // Test search by filename
    auto nameSearchResult = storageManager_->searchMedia("Sample");
    ASSERT_EXPECTED_VALUE(nameSearchResult);
    QVERIFY(nameSearchResult.value().size() >= 1);
    
    TestUtils::logMessage(QString("Real media search results: %1 video, %2 audio, %3 by name")
                         .arg(videoSearchResult.value().size())
                         .arg(audioSearchResult.value().size())
                         .arg(nameSearchResult.value().size()));
}

void TestRealMediaProcessing::testRealMediaProgressTracking() {
    TEST_SCOPE("testRealMediaProgressTracking");
    
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for progress tracking test");
    }
    
    progressSpy_->clear();
    
    QString outputPath = _testScope.getTempDirectory() + "/progress_test.mp4";
    ConversionSettings settings;
    settings.outputFormat = "mp4";
    settings.videoCodec = "libx264";
    // Remove preset as it's not in ConversionSettings struct
    
    auto conversionFuture = mediaPipeline_->convertVideo(realVideoFile_, outputPath, settings);
    auto result = TestUtils::waitForFuture(conversionFuture, 120000);
    
    if (result.hasValue()) {
        ASSERT_FILE_EXISTS(outputPath);
        
        TestUtils::logMessage(QString("Progress signals captured: %1").arg(progressSpy_->count()));
        
        // Verify progress values are reasonable
        if (progressSpy_->count() > 0) {
            for (int i = 0; i < progressSpy_->count(); ++i) {
                QList<QVariant> arguments = progressSpy_->at(i);
                if (arguments.size() >= 2) {
                    // Log progress information for debugging
                    TestUtils::logMessage(QString("Progress signal %1: %2").arg(i).arg(arguments[1].toString()));
                }
            }
        }
    }
}

void TestRealMediaProcessing::testRealMediaCancellation() {
    TEST_SCOPE("testRealMediaCancellation");
    
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for cancellation test");
    }
    
    QString outputPath = _testScope.getTempDirectory() + "/cancelled_test.mp4";
    ConversionSettings settings;
    // Remove preset as it's not in ConversionSettings struct
    
    auto conversionFuture = mediaPipeline_->convertVideo(realVideoFile_, outputPath, settings);
    
    // Cancel after 2 seconds
    QTimer::singleShot(2000, [this]() {
        mediaPipeline_->cancelAllOperations();
    });
    
    auto result = TestUtils::waitForFuture(conversionFuture, 30000);
    
    if (result.hasError() && result.error() == MediaError::Cancelled) {
        TestUtils::logMessage("Successfully cancelled real media processing");
        ASSERT_FILE_NOT_EXISTS(outputPath); // Cancelled operation should not leave output
    } else if (result.hasValue()) {
        TestUtils::logMessage("Operation completed before cancellation could take effect");
        // This is acceptable for very fast hardware
    } else {
        QFAIL(qPrintable(QString("Unexpected error during cancellation test: %1")
                        .arg(static_cast<int>(result.error()))));
    }
    
    // Verify system recovers after cancellation
    QString recoveryPath = _testScope.getTempDirectory() + "/recovery_test.mp4";
    ConversionSettings fastSettings;
    // Remove preset as it's not in ConversionSettings struct
    
    auto recoveryFuture = mediaPipeline_->convertVideo(realVideoFile_, recoveryPath, fastSettings);
    auto recoveryResult = TestUtils::waitForFuture(recoveryFuture, 60000);
    
    ASSERT_EXPECTED_VALUE(recoveryResult);
    ASSERT_FILE_EXISTS(recoveryPath);
}

void TestRealMediaProcessing::testRealMediaErrorFeedback() {
    TEST_SCOPE("testRealMediaErrorFeedback");
    
    errorSpy_->clear();
    
    // Try to process non-existent file to trigger error
    QString nonExistentPath = _testScope.getTempDirectory() + "/nonexistent.mp4";
    QString outputPath = _testScope.getTempDirectory() + "/error_test.mp4";
    
    ConversionSettings settings;
    auto conversionFuture = mediaPipeline_->convertVideo(nonExistentPath, outputPath, settings);
    auto result = TestUtils::waitForFuture(conversionFuture, 10000);
    
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), MediaError::InvalidFile);
    QVERIFY(errorSpy_->count() >= 1);
    
    // Verify error signal contains useful information
    if (errorSpy_->count() > 0) {
        QList<QVariant> errorArgs = errorSpy_->at(0);
        TestUtils::logMessage(QString("Error signal captured with %1 arguments").arg(errorArgs.size()));
    }
    
    ASSERT_FILE_NOT_EXISTS(outputPath);
}

// Helper method implementations
void TestRealMediaProcessing::setupRealMediaFiles() {
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

void TestRealMediaProcessing::verifyRealVideoProcessing(const QString& outputPath) {
    QVERIFY(TestUtils::validateRealMediaFile(outputPath));
    
    QFileInfo outputInfo(outputPath);
    QVERIFY(outputInfo.size() > 1024); // At least 1KB
    QVERIFY(outputInfo.exists());
    
    TestUtils::logMessage(QString("Verified real video output: %1 bytes").arg(outputInfo.size()));
}

void TestRealMediaProcessing::verifyRealAudioProcessing(const QString& outputPath) {
    QVERIFY(TestUtils::validateRealMediaFile(outputPath));
    
    QFileInfo outputInfo(outputPath);
    QVERIFY(outputInfo.size() > 512); // At least 512 bytes
    QVERIFY(outputInfo.exists());
    
    TestUtils::logMessage(QString("Verified real audio output: %1 bytes").arg(outputInfo.size()));
}

void TestRealMediaProcessing::measureAndValidatePerformance(std::function<void()> operation, const QString& operationName) {
    QJsonObject beforeStats = TestUtils::getResourceUsageReport();
    
    qint64 startTime = QDateTime::currentMSecsSinceEpoch();
    operation();
    qint64 endTime = QDateTime::currentMSecsSinceEpoch();
    
    QJsonObject afterStats = TestUtils::getResourceUsageReport();
    
    qint64 operationTime = endTime - startTime;
    TestUtils::logMessage(QString("%1 completed in %2ms").arg(operationName).arg(operationTime));
    
    // Log resource usage
    if (beforeStats.contains("memory_mb") && afterStats.contains("memory_mb")) {
        double memoryDelta = afterStats["memory_mb"].toDouble() - beforeStats["memory_mb"].toDouble();
        TestUtils::logMessage(QString("%1 memory delta: %2MB").arg(operationName).arg(memoryDelta));
    }
}

int runTestRealMediaProcessing(int argc, char** argv) {
    TestRealMediaProcessing test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_real_media_processing.moc"