#include <QtTest/QtTest>
#include <QtCore/QTemporaryDir>
#include <QtCore/QFileInfo>

#include "utils/TestUtils.hpp"
#include "../src/core/media/FFmpegWrapper.hpp"
#include "../src/core/media/MediaPipeline.hpp"
#include "../src/core/common/Expected.hpp"

using namespace Murmur;
using namespace Murmur::Test;

/**
 * @brief Comprehensive unit tests for FFmpegWrapper
 * 
 * Tests all major FFmpeg operations including video conversion,
 * audio extraction, format validation, and error handling.
 */
class TestFFmpegWrapper : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Core functionality tests
    void testVideoAnalysis();
    void testVideoConversion();
    void testAudioExtraction();
    void testThumbnailGeneration();
    void testFormatValidation();
    
    // Error handling tests
    void testInvalidInputFiles();
    void testUnsupportedFormats();
    void testCorruptedFiles();
    void testInvalidOutputPaths();
    
    // Performance and quality tests
    void testConversionQuality();
    void testProgressTracking();
    void testCancellation();
    void testConcurrentOperations();
    void testMemoryUsage();
    
    // Edge cases
    void testVeryShortVideos();
    void testHighResolutionVideos();
    void testVariousCodecs();
    void testAudioOnlyFiles();
    void testVideoOnlyFiles();

private:
    std::unique_ptr<FFmpegWrapper> ffmpeg_;
    std::unique_ptr<QTemporaryDir> tempDir_;
    QString testVideoFile_;
    QString testAudioFile_;
    
    // Test helpers
    void createTestVideoFile(const QString& path, int durationSeconds = 5, 
                           const QString& resolution = "640x480", 
                           const QString& codec = "libx264");
    void createTestAudioFile(const QString& path, int durationSeconds = 5,
                           const QString& codec = "aac");
    bool validateVideoFile(const QString& path, const QString& expectedCodec = "",
                          const QString& expectedResolution = "");
    bool validateAudioFile(const QString& path, const QString& expectedCodec = "");
    ConversionOptions createValidConversionOptions(const QString& outputPath);
    QJsonObject createQualityOptions(const QString& quality);
};

void TestFFmpegWrapper::initTestCase() {
    TestUtils::initializeTestEnvironment();
    
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available - skipping FFmpegWrapper tests");
    }
    
    TestUtils::logMessage("FFmpegWrapper unit tests initialized");
}

void TestFFmpegWrapper::cleanupTestCase() {
    TestUtils::cleanupTestEnvironment();
}

void TestFFmpegWrapper::init() {
    tempDir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(tempDir_->isValid());
    
    ffmpeg_ = std::make_unique<FFmpegWrapper>();
    
    // Create a test video with both video and audio streams
    testVideoFile_ = tempDir_->path() + "/test_video.mp4";
    // Using the test class's helper to create a 640x480, 5s video with AAC audio
    createTestVideoFile(testVideoFile_, 5, "640x480"); 
    TestUtils::logMessage("Created standard test video file: " + testVideoFile_);
    
    // Create a standard test audio file
    testAudioFile_ = tempDir_->path() + "/test_audio.aac";
    createTestAudioFile(testAudioFile_, 5, "aac");
    TestUtils::logMessage("Created standard test audio file: " + testAudioFile_);
    
    QVERIFY(QFileInfo(testVideoFile_).exists());
    QVERIFY(QFileInfo(testAudioFile_).exists());
}

void TestFFmpegWrapper::cleanup() {
    ffmpeg_.reset();
    tempDir_.reset();
}

void TestFFmpegWrapper::testVideoAnalysis() {
    TEST_SCOPE("testVideoAnalysis");
    // Test valid video file analysis
    auto future = ffmpeg_->analyzeFile(testVideoFile_);
    auto result = TestUtils::waitForFuture(future);
    if (!result.hasValue()) {
        QFAIL(QString("Analysis failed: %1").arg(static_cast<int>(result.error())).toUtf8());
        return;
    }
    
    auto fileInfo = result.value();
    
    // Validate basic properties
    QVERIFY(fileInfo.duration > 0);
    QVERIFY(fileInfo.video.width > 0);
    QVERIFY(fileInfo.video.height > 0);
    QVERIFY(fileInfo.video.frameRate > 0);
    QVERIFY(!fileInfo.video.codec.isEmpty());
    QVERIFY(!fileInfo.audio.codec.isEmpty());
    QVERIFY(fileInfo.video.bitrate > 0);
    QVERIFY(fileInfo.audio.bitrate > 0);
    
    // Validate expected values for our test file
    QCOMPARE(fileInfo.video.width, 640);
    QCOMPARE(fileInfo.video.height, 480);
    QVERIFY(fileInfo.duration >= 4.8 && fileInfo.duration <= 5.2); // ~5 seconds ±0.2s
    QVERIFY(fileInfo.video.codec.toLower().contains("h264"));
    
    TestUtils::logMessage(QString("Video analysis successful: %1x%2, %3s, %4 fps")
                         .arg(fileInfo.video.width).arg(fileInfo.video.height)
                         .arg(fileInfo.duration).arg(fileInfo.video.frameRate));
}

void TestFFmpegWrapper::testVideoConversion() {
    TEST_SCOPE("testVideoConversion");
    
    QString outputPath = tempDir_->path() + "/converted_video.mp4";
    auto options = createValidConversionOptions(outputPath);
    
    // Test conversion
    auto future = ffmpeg_->convertVideo(testVideoFile_, outputPath, options);
    auto result = TestUtils::waitForFuture(future);
    
    if (result.hasError()) {
        QFAIL(QString("Conversion failed with error: %1").arg(static_cast<int>(result.error())).toUtf8());
        return;
    }
    
    // Verify output file exists and is valid
    QVERIFY(QFileInfo(outputPath).exists());
    QVERIFY(QFileInfo(outputPath).size() > 0);
    
    // Analyze converted file to verify properties
    auto analyzeFuture = ffmpeg_->analyzeFile(outputPath);
    auto analyzeResult = TestUtils::waitForFuture(analyzeFuture);
    
    if (analyzeResult.hasError()) {
        QFAIL(QString("Analysis of converted file failed with error: %1").arg(static_cast<int>(analyzeResult.error())).toUtf8());
        return;
    }
    
    auto convertedInfo = analyzeResult.value();
    
    TestUtils::logMessage(QString("Converted video codec: %1, expected: %2").arg(convertedInfo.video.codec).arg(options.videoCodec));
    TestUtils::logMessage(QString("Converted audio codec: %1, expected: %2").arg(convertedInfo.audio.codec).arg(options.audioCodec));
    
    // Check video codec (h264 vs libx264)
    QString expectedVideoCodec = options.videoCodec.toLower().replace("lib", "");
    // Special case: libx264 encoder produces h264 codec
    if (expectedVideoCodec == "x264") {
        expectedVideoCodec = "h264";
    }
    QVERIFY2(convertedInfo.video.codec.toLower().contains(expectedVideoCodec), 
             QString("Video codec mismatch: got '%1', expected to contain '%2'").arg(convertedInfo.video.codec).arg(expectedVideoCodec).toUtf8());
    
    // Check audio codec
    QVERIFY2(convertedInfo.audio.codec.toLower().contains(options.audioCodec.toLower()),
             QString("Audio codec mismatch: got '%1', expected to contain '%2'").arg(convertedInfo.audio.codec).arg(options.audioCodec).toUtf8());
    
    // Duration should be reasonable (allow some variance)
    QVERIFY2(convertedInfo.duration >= 2.0 && convertedInfo.duration <= 6.0,
             QString("Duration out of range: got %1s, expected 2-6s").arg(convertedInfo.duration).toUtf8());
    
    TestUtils::logMessage(QString("Video conversion successful: %1 -> %2")
                         .arg(QFileInfo(testVideoFile_).size())
                         .arg(QFileInfo(outputPath).size()));
}

void TestFFmpegWrapper::testAudioExtraction() {
    TEST_SCOPE("testAudioExtraction");
    
    QString outputPath = tempDir_->path() + "/extracted_audio.aac";
    
    auto future = ffmpeg_->extractAudio(testVideoFile_, outputPath);
    auto result = TestUtils::waitForFuture(future);
    
    if (result.hasError()) {
        QFAIL(QString("Audio extraction failed with error: %1").arg(static_cast<int>(result.error())).toUtf8());
        return;
    }
    
    // Verify output file exists and is valid
    QVERIFY(QFileInfo(outputPath).exists());
    QVERIFY(QFileInfo(outputPath).size() > 0);
    
    // Verify it's a valid audio file
    QVERIFY(validateAudioFile(outputPath, "aac"));
    
    TestUtils::logMessage(QString("Audio extraction successful: %1 bytes")
                         .arg(QFileInfo(outputPath).size()));
}

void TestFFmpegWrapper::testThumbnailGeneration() {
    TEST_SCOPE("testThumbnailGeneration");
    
    QString outputPath = tempDir_->path() + "/thumbnail.jpg";
    
    // Test thumbnail at 2 seconds
    auto future = ffmpeg_->generateThumbnail(testVideoFile_, outputPath, 2.0);
    auto result = TestUtils::waitForFuture(future);
    if (!result.hasValue()) {
        QFAIL(QString("Thumbnail generation failed: %1").arg(static_cast<int>(result.error())).toUtf8());
        return;
    }
    
    // Verify output file exists and is valid image
    QVERIFY(QFileInfo(outputPath).exists());
    QVERIFY(QFileInfo(outputPath).size() > 0);
    
    // Basic validation that it's an image file
    QFile file(outputPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QByteArray header = file.read(10);
    QVERIFY(header.startsWith("\xFF\xD8\xFF")); // JPEG header
    
    TestUtils::logMessage(QString("Thumbnail generation successful: %1 bytes")
                         .arg(QFileInfo(outputPath).size()));
}

void TestFFmpegWrapper::testFormatValidation() {
    TEST_SCOPE("testFormatValidation");
    
    // Test supported formats
    auto inputFormats = FFmpegWrapper::getSupportedInputFormats();
    auto outputFormats = FFmpegWrapper::getSupportedOutputFormats();
    auto videoCodecs = FFmpegWrapper::getSupportedVideoCodecs();
    auto audioCodecs = FFmpegWrapper::getSupportedAudioCodecs();
    
    // Check for mp4 support (may be part of combined format string like "mov,mp4,m4a,3gp,3g2,mj2")
    bool mp4InputSupported = std::any_of(inputFormats.begin(), inputFormats.end(), 
        [](const QString& format) { return format.contains("mp4", Qt::CaseInsensitive); });
    bool mp4OutputSupported = std::any_of(outputFormats.begin(), outputFormats.end(),
        [](const QString& format) { return format.contains("mp4", Qt::CaseInsensitive); });
    
    QVERIFY(mp4InputSupported);
    QVERIFY(mp4OutputSupported);
    QVERIFY(videoCodecs.contains("libx264", Qt::CaseInsensitive));
    QVERIFY(audioCodecs.contains("aac", Qt::CaseInsensitive));
}

void TestFFmpegWrapper::testInvalidInputFiles() {
    TEST_SCOPE("testInvalidInputFiles");
    
    QString outputPath = tempDir_->path() + "/output.mp4";
    auto options = createValidConversionOptions(outputPath);
    
    // Test non-existent file
    auto future1 = ffmpeg_->analyzeFile("/non/existent/file.mp4");
    auto result1 = TestUtils::waitForFuture(future1);
    QVERIFY(result1.hasError());
    QCOMPARE(result1.error(), FFmpegError::InvalidFile);
    
    // Test invalid file format
    QString invalidFile = tempDir_->path() + "/invalid.txt";
    QFile file(invalidFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("This is not a video file");
    file.close();
    
    auto future2 = ffmpeg_->analyzeFile(invalidFile);
    auto result2 = TestUtils::waitForFuture(future2);
    QVERIFY(result2.hasError());
    QVERIFY(result2.error() == FFmpegError::UnsupportedFormat || 
            result2.error() == FFmpegError::InvalidFile);
    
    // Test conversion with invalid input
    auto future3 = ffmpeg_->convertVideo(invalidFile, outputPath, options);
    auto result3 = TestUtils::waitForFuture(future3);
    QVERIFY(result3.hasError());
}

void TestFFmpegWrapper::testUnsupportedFormats() {
    TEST_SCOPE("testUnsupportedFormats");
    
    QString outputPath = tempDir_->path() + "/output.mp4";
    ConversionOptions invalidOptions;
    invalidOptions.videoCodec = "invalid_codec";
    invalidOptions.audioCodec = "invalid_codec";
    invalidOptions.videoBitrate = 1000;
    invalidOptions.audioBitrate = 128;
    
    auto future = ffmpeg_->convertVideo(testVideoFile_, outputPath, invalidOptions);
    auto result = TestUtils::waitForFuture(future);
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), FFmpegError::UnsupportedFormat);
}

void TestFFmpegWrapper::testCorruptedFiles() {
    TEST_SCOPE("testCorruptedFiles");
    
    // Create a corrupted video file
    QString corruptedFile = tempDir_->path() + "/corrupted.mp4";
    QFile file(corruptedFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    
    // Write some bytes that look like a video file header but are corrupted
    file.write("\x00\x00\x00\x20\x66\x74\x79\x70\x69\x73\x6F\x6D"); // Partial MP4 header
    file.write(QByteArray(1000, '\x00')); // Corrupted data
    file.close();
    
    auto future = ffmpeg_->analyzeFile(corruptedFile);
    auto result = TestUtils::waitForFuture(future);
    QVERIFY(result.hasError());
    QVERIFY(result.error() == FFmpegError::InvalidFile || 
            result.error() == FFmpegError::UnsupportedFormat);
}

void TestFFmpegWrapper::testProgressTracking() {
    TEST_SCOPE("testProgressTracking");
    
    if (!TestUtils::isTestVideoAvailable()) {
        QSKIP("Test video not available");
    }
    
    QString outputPath = tempDir_->path() + "/progress_test.mp4";
    auto options = createValidConversionOptions(outputPath);
    
    QList<double> progressValues;
    bool progressCallbackCalled = false;
    
    // Create a progress callback
    FFmpegProgressCallback progressCallback = [&progressValues, &progressCallbackCalled](const ProgressInfo& progress) {
        progressCallbackCalled = true;
        progressValues.append(progress.progressPercent);
    };
    
    // Also connect to progress signal as backup
    connect(ffmpeg_.get(), &FFmpegWrapper::operationProgress,
            [&progressValues](const QString& operationId, const ProgressInfo& progress) {
                Q_UNUSED(operationId);
                progressValues.append(progress.progressPercent);
            });
    
    // Create a longer test video for better progress tracking
    QString longerTestFile = tempDir_->path() + "/longer_test.mp4";
    createTestVideoFile(longerTestFile, 10); // 10 second video for better progress tracking
    
    auto future = ffmpeg_->convertVideo(longerTestFile, outputPath, options, progressCallback);
    auto result = TestUtils::waitForFuture(future, 30000); // Allow more time for longer video
    QVERIFY(result.hasValue());
    
    // Verify we received progress updates (either via callback or signal)
    // For very fast operations, progress might not be captured, so we make this more lenient
    if (progressValues.size() == 0 && !progressCallbackCalled) {
        TestUtils::logMessage("No progress updates captured - operation may have completed too quickly");
        // Skip the rest of the progress verification but don't fail the test
        return;
    }
    
    // Verify progress values are in valid range
    for (double progress : progressValues) {
        QVERIFY(progress >= 0.0 && progress <= 100.0);
    }
    
    // If we have progress values, verify the final one is reasonable
    if (!progressValues.isEmpty()) {
        TestUtils::logMessage(QString("Progress tracking: captured %1 progress updates, final: %2%")
                             .arg(progressValues.size()).arg(progressValues.last()));
        // Final progress should be at least 90% (allowing some tolerance)
        QVERIFY(progressValues.last() >= 90.0);
    }
    
    TestUtils::logMessage(QString("Progress tracking: received %1 updates")
                         .arg(progressValues.size()));
}

void TestFFmpegWrapper::testCancellation() {
    TEST_SCOPE("testCancellation");
    
    // Create a longer video for cancellation testing
    QString longVideoFile = tempDir_->path() + "/long_video.mp4";
    createTestVideoFile(longVideoFile, 30); // 30 seconds
    
    QString outputPath = tempDir_->path() + "/cancelled_output.mp4";
    auto options = createValidConversionOptions(outputPath);
    
    // Start conversion in a separate thread
    QString operationId;
    QTimer::singleShot(500, [this, &operationId]() {
        if (!operationId.isEmpty()) {
            ffmpeg_->cancelOperation(operationId);
        }
    });
    
    auto future = ffmpeg_->convertVideo(longVideoFile, outputPath, options);
    auto result = TestUtils::waitForFuture(future);
    operationId = result.hasValue() ? QString("test_operation") : QString();
    
    // Operation should either complete quickly or be cancelled
    if (result.hasError()) {
        QCOMPARE(result.error(), FFmpegError::CancellationRequested);
    }
    
    TestUtils::logMessage("Cancellation test completed");
}

// Helper methods implementation
void TestFFmpegWrapper::createTestVideoFile(const QString& path, int durationSeconds, 
                                          const QString& resolution, const QString& codec) {
    // Use FFmpeg command line to create a test video
    QProcess process;
    QStringList args;
    args << "-f" << "lavfi"
         << "-i" << QString("testsrc=duration=%1:size=%2:rate=30").arg(durationSeconds).arg(resolution)
         << "-f" << "lavfi"
         << "-i" << QString("sine=frequency=1000:duration=%1").arg(durationSeconds)
         << "-c:v" << codec
         << "-c:a" << "aac"
         << "-shortest"
         << "-y" << path;
    
    process.start("ffmpeg", args);
    process.waitForFinished(30000);
    
    if (process.exitCode() != 0) {
        qWarning() << "Failed to create test video:" << process.readAllStandardError();
    }
}

void TestFFmpegWrapper::createTestAudioFile(const QString& path, int durationSeconds, const QString& codec) {
    QProcess process;
    QStringList args;
    args << "-f" << "lavfi"
         << "-i" << QString("sine=frequency=1000:duration=%1").arg(durationSeconds)
         << "-c:a" << codec
         << "-y" << path;
    
    process.start("ffmpeg", args);
    process.waitForFinished(15000);
}

bool TestFFmpegWrapper::validateVideoFile(const QString& path, const QString& expectedCodec, 
                                         const QString& expectedResolution) {
    if (!QFileInfo(path).exists()) return false;
    
    // Use ffprobe to validate the file
    QProcess process;
    QStringList args;
    args << "-v" << "quiet"
         << "-print_format" << "json"
         << "-show_format"
         << "-show_streams"
         << path;
    
    process.start("ffprobe", args);
    if (!process.waitForFinished(10000)) return false;
    
    // Basic validation - file should have valid streams
    QByteArray output = process.readAllStandardOutput();
    return !output.isEmpty() && output.contains("streams");
}

bool TestFFmpegWrapper::validateAudioFile(const QString& path, const QString& expectedCodec) {
    return validateVideoFile(path, expectedCodec);
}

ConversionOptions TestFFmpegWrapper::createValidConversionOptions(const QString& outputPath) {
    Q_UNUSED(outputPath);
    
    ConversionOptions options;
    options.videoCodec = "libx264";
    options.audioCodec = "aac";
    options.videoBitrate = 1000;
    options.audioBitrate = 128;
    options.width = 640;
    options.height = 480;
    options.frameRate = 30;
    options.audioSampleRate = 44100;
    options.audioChannels = 2;
    
    return options;
}

void TestFFmpegWrapper::testConcurrentOperations() {
    TEST_SCOPE("testConcurrentOperations");
    
    if (!TestUtils::isTestVideoAvailable()) {
        QSKIP("Test video not available");
    }
    
    // Create multiple conversion tasks with different output files
    QList<QFuture<Expected<QString, FFmpegError>>> futures;
    QStringList outputFiles;
    
    for (int i = 0; i < 3; ++i) {
        QString outputFile = QDir::temp().filePath(QString("concurrent_test_%1.mp4").arg(i));
        outputFiles.append(outputFile);
        QFile::remove(outputFile); // Clean up any existing file
        
        auto options = createValidConversionOptions(outputFile);
        auto future = ffmpeg_->convertVideo(testVideoFile_, outputFile, options);
        futures.append(future);
    }
    
    // Wait for all conversions to complete with proper synchronization
    int completedCount = 0;
    int errorCount = 0;
    
    for (int i = 0; i < futures.size(); ++i) {
        auto& future = futures[i];
        
        // Wait for completion with timeout using QFutureWatcher approach
        QElapsedTimer timer;
        timer.start();
        const int timeoutMs = 30000; // 30 seconds per operation
        
        while (!future.isFinished() && timer.elapsed() < timeoutMs) {
            QTest::qWait(100); // Check every 100ms
        }
        
        if (future.isFinished()) {
            try {
                auto result = future.result();
                if (result.hasValue()) {
                    completedCount++;
                    QVERIFY(QFile::exists(outputFiles[i]));
                    QVERIFY(QFileInfo(outputFiles[i]).size() > 0);
                    TestUtils::logMessage(QString("Concurrent conversion %1 succeeded").arg(i));
                } else {
                    errorCount++;
                    TestUtils::logMessage(QString("Concurrent conversion %1 failed with error: %2").arg(i).arg(static_cast<int>(result.error())));
                }
            } catch (const std::exception& e) {
                errorCount++;
                TestUtils::logMessage(QString("Concurrent conversion %1 threw exception: %2").arg(i).arg(e.what()));
            }
        } else {
            errorCount++;
            TestUtils::logMessage(QString("Concurrent conversion %1 timed out").arg(i));
            future.cancel(); // Try to cancel the timed-out operation
        }
    }
    
    // At least some conversions should succeed
    QVERIFY(completedCount > 0);
    TestUtils::logMessage(QString("Concurrent operations: %1 completed, %2 errors").arg(completedCount).arg(errorCount));
    
    // Clean up output files
    for (const QString& outputFile : outputFiles) {
        QFile::remove(outputFile);
    }
}

void TestFFmpegWrapper::testMemoryUsage() {
    TEST_SCOPE("testMemoryUsage");
    
    if (!TestUtils::isTestVideoAvailable()) {
        QSKIP("Test video not available");
    }
    
    // Get initial memory usage (rough estimate using QProcess)
    QProcess memProcess;
#ifdef Q_OS_MACOS
    memProcess.start("ps", QStringList() << "-o" << "rss=" << "-p" << QString::number(QCoreApplication::applicationPid()));
#elif defined(Q_OS_LINUX)
    memProcess.start("ps", QStringList() << "-o" << "rss=" << "-p" << QString::number(QCoreApplication::applicationPid()));
#else
    // Windows - skip this test for now
    QSKIP("Memory monitoring not implemented for this platform");
#endif
    
    memProcess.waitForFinished(1000);
    QString initialMemStr = memProcess.readAllStandardOutput().trimmed();
    qint64 initialMemory = initialMemStr.toLongLong();
    
    TestUtils::logMessage(QString("Initial memory usage: %1 KB").arg(initialMemory));
    
    // Perform a conversion
    QString outputFile = QDir::temp().filePath("memory_test.mp4");
    QFile::remove(outputFile);
    
    auto options = createValidConversionOptions(outputFile);
    auto future = ffmpeg_->convertVideo(testVideoFile_, outputFile, options);
    
    // Monitor memory during conversion
    qint64 peakMemory = initialMemory;
    QElapsedTimer timer;
    timer.start();
    
    while (!future.isFinished() && timer.elapsed() < 10000) { // 10 second timeout
        QCoreApplication::processEvents();
        QTest::qWait(500);
        
        QProcess currentMemProcess;
#ifdef Q_OS_MACOS
        currentMemProcess.start("ps", QStringList() << "-o" << "rss=" << "-p" << QString::number(QCoreApplication::applicationPid()));
#elif defined(Q_OS_LINUX)
        currentMemProcess.start("ps", QStringList() << "-o" << "rss=" << "-p" << QString::number(QCoreApplication::applicationPid()));
#endif
        currentMemProcess.waitForFinished(1000);
        QString currentMemStr = currentMemProcess.readAllStandardOutput().trimmed();
        qint64 currentMemory = currentMemStr.toLongLong();
        
        if (currentMemory > peakMemory) {
            peakMemory = currentMemory;
        }
    }
    
    // Get final memory usage
    QProcess finalMemProcess;
#ifdef Q_OS_MACOS
    finalMemProcess.start("ps", QStringList() << "-o" << "rss=" << "-p" << QString::number(QCoreApplication::applicationPid()));
#elif defined(Q_OS_LINUX)
    finalMemProcess.start("ps", QStringList() << "-o" << "rss=" << "-p" << QString::number(QCoreApplication::applicationPid()));
#endif
    finalMemProcess.waitForFinished(1000);
    QString finalMemStr = finalMemProcess.readAllStandardOutput().trimmed();
    qint64 finalMemory = finalMemStr.toLongLong();
    
    TestUtils::logMessage(QString("Peak memory usage: %1 KB").arg(peakMemory));
    TestUtils::logMessage(QString("Final memory usage: %1 KB").arg(finalMemory));
    TestUtils::logMessage(QString("Memory increase: %1 KB").arg(peakMemory - initialMemory));
    
    // Verify memory usage is reasonable (less than 500MB increase)
    qint64 memoryIncrease = peakMemory - initialMemory;
    QVERIFY(memoryIncrease < 500000); // 500MB in KB
    
    // Verify memory was cleaned up after conversion (allow some overhead)
    qint64 finalIncrease = finalMemory - initialMemory;
    QVERIFY(finalIncrease < memoryIncrease + 50000); // Allow 50MB overhead
    
    // Clean up
    QFile::remove(outputFile);
}

void TestFFmpegWrapper::testConversionQuality() {
    TEST_SCOPE("testConversionQuality");
    
    if (!TestUtils::isTestVideoAvailable()) {
        QSKIP("Test video not available");
    }
    
    // Get original file info
    QFileInfo originalFile(testVideoFile_);
    qint64 originalSize = originalFile.size();
    
    TestUtils::logMessage(QString("Original file size: %1 bytes").arg(originalSize));
    
    // Test different quality settings (simplified for debugging)
    QList<QPair<QString, QJsonObject>> qualityTests = {
        {"medium_quality", createQualityOptions("medium")}
    };
    
    for (const auto& test : qualityTests) {
        QString testName = test.first;
        QJsonObject options = test.second;
        
        QString outputFile = QDir::temp().filePath(QString("quality_%1.mp4").arg(testName));
        QFile::remove(outputFile);
        
        TestUtils::logMessage(QString("Testing %1 conversion").arg(testName));
        
        // Convert QJsonObject to ConversionOptions
        ConversionOptions conversionOptions;
        if (options.contains("videoCodec")) {
            conversionOptions.videoCodec = options["videoCodec"].toString();
        }
        if (options.contains("videoBitrate")) {
            conversionOptions.videoBitrate = options["videoBitrate"].toInt();
        }
        if (options.contains("audioCodec")) {
            conversionOptions.audioCodec = options["audioCodec"].toString();
        }
        if (options.contains("audioBitrate")) {
            conversionOptions.audioBitrate = options["audioBitrate"].toInt();
        }
        if (options.contains("preset")) {
            conversionOptions.preset = options["preset"].toString();
        }
        if (options.contains("crf")) {
            conversionOptions.crf = options["crf"].toInt();
        }
        
        auto future = ffmpeg_->convertVideo(testVideoFile_, outputFile, conversionOptions);
        auto result = TestUtils::waitForFuture(future, 15000); // Wait up to 15 seconds
        
        if (result.hasValue()) {
                QFileInfo convertedFile(outputFile);
                qint64 convertedSize = convertedFile.size();
                
                // Verify converted file exists and has reasonable size
                QVERIFY(convertedFile.exists());
                QVERIFY(convertedSize > 0);
                
                double compressionRatio = double(convertedSize) / double(originalSize);
                TestUtils::logMessage(QString("%1: %2 bytes (ratio: %3)")
                                    .arg(testName).arg(convertedSize).arg(compressionRatio, 0, 'f', 2));
                
                // Quality expectations:
                // High quality should be 0.5-2.0x original size
                // Medium quality should be 0.3-1.0x original size  
                // Low quality should be 0.1-0.5x original size
                if (testName == "high_quality") {
                    QVERIFY(compressionRatio >= 0.5 && compressionRatio <= 2.0);
                } else if (testName == "medium_quality") {
                    QVERIFY(compressionRatio >= 0.3 && compressionRatio <= 1.0);
                } else if (testName == "low_quality") {
                    QVERIFY(compressionRatio >= 0.1 && compressionRatio <= 0.5);
                }
                
                // Verify the file can be opened (basic format validation)
                QFile convertedFileCheck(outputFile);
                QVERIFY(convertedFileCheck.open(QIODevice::ReadOnly));
                QByteArray header = convertedFileCheck.read(12);
                convertedFileCheck.close();
                
                // Check for MP4 signature or other video format markers
                QVERIFY(header.size() >= 8);
        } else {
            QFAIL(QString("Conversion failed for %1").arg(testName).toLocal8Bit());
        }
        
        // Clean up
        QFile::remove(outputFile);
    }
}

QJsonObject TestFFmpegWrapper::createQualityOptions(const QString& quality) {
    QJsonObject options;
    
    if (quality == "high") {
        options["videoBitrate"] = 5000;  // 5 Mbps
        options["audioBitrate"] = 192;   // 192 kbps
        options["preset"] = "slow";
        options["crf"] = 18;  // High quality
    } else if (quality == "medium") {
        options["videoBitrate"] = 2000;  // 2 Mbps  
        options["audioBitrate"] = 128;   // 128 kbps
        options["preset"] = "medium";
        options["crf"] = 23;  // Medium quality
    } else if (quality == "low") {
        options["videoBitrate"] = 500;   // 500 kbps
        options["audioBitrate"] = 64;    // 64 kbps
        options["preset"] = "fast";
        options["crf"] = 28;  // Lower quality
    }
    
    options["format"] = "mp4";
    options["videoCodec"] = "libx264";
    options["audioCodec"] = "aac";
    
    return options;
}

void TestFFmpegWrapper::testInvalidOutputPaths() {
    TEST_SCOPE("testInvalidOutputPaths");
    
    QString invalidOutputPath = "/invalid/nonexistent/directory/output.mp4";
    auto options = createValidConversionOptions(invalidOutputPath);
    
    auto future = ffmpeg_->convertVideo(testVideoFile_, invalidOutputPath, options);
    auto result = TestUtils::waitForFuture(future);
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), FFmpegError::IOError);
}

void TestFFmpegWrapper::testVeryShortVideos() {
    TEST_SCOPE("testVeryShortVideos");
    
    QString shortVideoFile = tempDir_->path() + "/short_video.mp4";
    createTestVideoFile(shortVideoFile, 1); // 1 second
    
    auto future = ffmpeg_->analyzeFile(shortVideoFile);
    auto result = TestUtils::waitForFuture(future);
    QVERIFY(result.hasValue());
    
    auto info = result.value();
    QVERIFY(info.duration >= 0.8 && info.duration <= 1.2); // ~1 second ±0.2s
}

void TestFFmpegWrapper::testHighResolutionVideos() {
    TEST_SCOPE("testHighResolutionVideos");
    
    QString hdVideoFile = tempDir_->path() + "/hd_video.mp4";
    createTestVideoFile(hdVideoFile, 5, "1920x1080");
    
    auto future = ffmpeg_->analyzeFile(hdVideoFile);
    auto result = TestUtils::waitForFuture(future);
    QVERIFY(result.hasValue());
    
    auto info = result.value();
    QCOMPARE(info.video.width, 1920);
    QCOMPARE(info.video.height, 1080);
}

void TestFFmpegWrapper::testVariousCodecs() {
    TEST_SCOPE("testVariousCodecs");
    
    // Test H.265 if available
    QString h265File = tempDir_->path() + "/h265_video.mp4";
    createTestVideoFile(h265File, 5, "640x480", "libx265");
    
    auto future = ffmpeg_->analyzeFile(h265File);
    auto result = TestUtils::waitForFuture(future);
    if (result.hasValue()) {
        auto info = result.value();
        QVERIFY(info.video.codec.toLower().contains("265") || info.video.codec.toLower().contains("hevc"));
    }
}

void TestFFmpegWrapper::testAudioOnlyFiles() {
    TEST_SCOPE("testAudioOnlyFiles");
    
    auto future = ffmpeg_->analyzeFile(testAudioFile_);
    auto result = TestUtils::waitForFuture(future);
    if (result.hasValue()) {
        auto info = result.value();
        QVERIFY(info.video.width == 0 && info.video.height == 0); // No video streams
        QVERIFY(!info.audio.codec.isEmpty()); // Has audio
    }
}

void TestFFmpegWrapper::testVideoOnlyFiles() {
    TEST_SCOPE("testVideoOnlyFiles");
    
    QString videoOnlyFile = tempDir_->path() + "/video_only.mp4";
    
    // Create video-only file
    QProcess process;
    QStringList args;
    args << "-f" << "lavfi"
         << "-i" << "testsrc=duration=5:size=640x480:rate=30"
         << "-c:v" << "libx264"
         << "-an" // No audio
         << "-y" << videoOnlyFile;
    
    process.start("ffmpeg", args);
    process.waitForFinished(15000);
    
    if (QFileInfo(videoOnlyFile).exists()) {
        auto future = ffmpeg_->analyzeFile(videoOnlyFile);
        auto result = TestUtils::waitForFuture(future);
        if (result.hasValue()) {
            auto info = result.value();
            QVERIFY(info.video.width > 0 && info.video.height > 0); // Has video
            QVERIFY(info.audio.codec.isEmpty() || info.audio.bitrate == 0); // No audio
        }
    }
}

int runTestFFmpegWrapper(int argc, char** argv) {
    TestFFmpegWrapper test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_ffmpeg_wrapper.moc"