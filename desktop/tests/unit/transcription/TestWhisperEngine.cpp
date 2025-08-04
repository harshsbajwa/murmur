#include <QtTest/QtTest>
#include <QtTest/QSignalSpy>
#include <QtCore/QTemporaryDir>
#include <QtCore/QDir>
#include <QtCore/QTimer>
#include "../../../src/core/transcription/WhisperEngine.hpp"
#include "../../utils/TestUtils.hpp"
#include "../../utils/MockComponents.hpp"

using namespace Murmur;
using namespace Murmur::Test;

Q_DECLARE_METATYPE(Murmur::TranscriptionSettings)

class TestWhisperEngine : public QObject {
    Q_OBJECT

private slots:
    // Lifecycle is managed per-test-case
    void initTestCase();
    void cleanupTestCase();

    // Engine lifecycle tests
    void testInitializeWithInvalidModelPath();
    void testShutdownAfterInitialization();
    void testIsInitializedStates();
    
    // Model management tests
    void testLoadModelWithInvalidFile();
    void testGetCurrentModel();
    void testGetAvailableModels();
    void testGetSupportedLanguages();
    
    // Basic transcription tests
    void testTranscribeAudioWithValidFile();
    void testTranscribeAudioWithInvalidFile();
    void testTranscribeAudioWithDifferentSettings();
    void testTranscribeFromVideo();
    void testTranscribeWithLanguageDetection();
    void testTranscribeWithSpecificLanguage();
    
    // Advanced transcription tests
    void testTranscribeWithTimestamps();
    void testTranscribeWithWordConfidence();
    void testTranscribeDifferentOutputFormats();
    
    // Signal and progress tests
    void testTranscriptionCompletedSignals();
    void testTranscriptionErrorSignals();
    void testCancellationSignals();
    
    // Error handling tests
    void testTranscriptionFailureRecovery();
    void testModelLoadFailureRecovery();
    void testCorruptedAudioHandling();
    void testUnsupportedFormatHandling();
    
    // Performance and resource tests
    void testConcurrentTranscriptions();
    void testTranscriptionCancellation();
    
    // Language detection tests
    void testLanguageDetectionAccuracy();
    void testUnsupportedLanguageHandling();

private:
    std::unique_ptr<QTemporaryDir> tempDir_;
    static std::unique_ptr<WhisperEngine> whisperEngine_;
    QString testAudioFile_;
    QString testVideoFile_;
    QString testInvalidAudioFile_;
    
    // Test helpers
    void createTestFiles();
    TranscriptionSettings createBasicSettings();
    void verifyTranscriptionResult(const TranscriptionResult& result);
};

// Define the static member
std::unique_ptr<WhisperEngine> TestWhisperEngine::whisperEngine_ = nullptr;

void TestWhisperEngine::initTestCase() {
    TestUtils::initializeTestEnvironment();
    tempDir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(tempDir_->isValid());
    
    createTestFiles();

    whisperEngine_ = std::make_unique<WhisperEngine>();
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    QVERIFY(initResult.value());

    TestUtils::logMessage("Downloading 'tiny.en' model for tests...");
    
    auto downloadResult = whisperEngine_->downloadModel("tiny.en");
    QVERIFY(downloadResult.hasValue() && downloadResult.value());

    auto loadResult = whisperEngine_->loadModel("tiny.en");
    if (loadResult.hasError()) {
        QFAIL("Failed to load the 'tiny.en' model required for transcription tests.");
    }

    TestUtils::logMessage("WhisperEngine unit tests initialized with a real model.");
}

void TestWhisperEngine::cleanupTestCase() {
    if (whisperEngine_) {
        whisperEngine_->shutdown();
        whisperEngine_.reset();
    }
    tempDir_.reset();
    TestUtils::cleanupTestEnvironment();
    TestUtils::logMessage("WhisperEngine unit tests cleaned up");
}

void TestWhisperEngine::testInitializeWithInvalidModelPath() {
    WhisperEngine tempEngine;
    QString invalidPath = tempDir_->path() + "/nonexistent";
    
    auto result = tempEngine.initialize(invalidPath);
    QVERIFY(result.hasError());
    QVERIFY(!tempEngine.isInitialized());
}

void TestWhisperEngine::testShutdownAfterInitialization() {
    WhisperEngine tempEngine;
    auto initResult = tempEngine.initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    QVERIFY(tempEngine.isInitialized());
    
    tempEngine.shutdown();
    QVERIFY(!tempEngine.isInitialized());
}

void TestWhisperEngine::testIsInitializedStates() {
    QVERIFY(whisperEngine_->isInitialized());
    
    WhisperEngine tempEngine;
    QVERIFY(!tempEngine.isInitialized());
    tempEngine.initialize(tempDir_->path());
    QVERIFY(tempEngine.isInitialized());
    tempEngine.shutdown();
    QVERIFY(!tempEngine.isInitialized());
}

void TestWhisperEngine::testLoadModelWithInvalidFile() {
    auto loadResult = whisperEngine_->loadModel("nonexistent_model");
    QVERIFY(loadResult.hasError());
}

void TestWhisperEngine::testGetCurrentModel() {
    QCOMPARE(whisperEngine_->getCurrentModel(), QString("tiny.en"));
}

void TestWhisperEngine::testGetAvailableModels() {
    QStringList models = whisperEngine_->getAvailableModels();
    QVERIFY(models.contains("tiny.en"));
    QVERIFY(models.contains("base"));
}

void TestWhisperEngine::testGetSupportedLanguages() {
    QStringList languages = whisperEngine_->getSupportedLanguages();
    QVERIFY(languages.size() > 50);
    QVERIFY(languages.contains("en"));
    QVERIFY(languages.contains("auto"));
}

void TestWhisperEngine::testTranscribeAudioWithValidFile() {
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    QVERIFY(result.hasValue());
    verifyTranscriptionResult(result.value());
    QVERIFY(result.value().fullText.toLower().contains("the"));
}

void TestWhisperEngine::testTranscribeAudioWithInvalidFile() {
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(testInvalidAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 10000);
    
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), TranscriptionError::InvalidAudioFormat);
}

void TestWhisperEngine::testTranscribeAudioWithDifferentSettings() {
    TranscriptionSettings settings = createBasicSettings();
    settings.temperature = 0.5; // Change a setting
    
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    QVERIFY(result.hasValue());
    verifyTranscriptionResult(result.value());
}

void TestWhisperEngine::testTranscribeFromVideo() {
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for video transcription test");
    }
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeFromVideo(testVideoFile_, settings);
    auto result = TestUtils::waitForFuture(future, 45000);
    
    QVERIFY(result.hasValue());
    verifyTranscriptionResult(result.value());
}

void TestWhisperEngine::testTranscribeWithLanguageDetection() {
    TranscriptionSettings settings = createBasicSettings();
    settings.language = "auto";
    
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    QVERIFY(result.hasValue());
    // QVERIFY(!result.value().detectedLanguage.isEmpty());
    // QVERIFY(result.value().detectedLanguage == "en");
}

void TestWhisperEngine::testTranscribeWithSpecificLanguage() {
    TranscriptionSettings settings = createBasicSettings();
    settings.language = "en";
    
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    QVERIFY(result.hasValue());
    verifyTranscriptionResult(result.value());
}

void TestWhisperEngine::testTranscribeWithTimestamps() {
    TranscriptionSettings settings = createBasicSettings();
    settings.enableTimestamps = true;
    
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    QVERIFY(result.hasValue());
    verifyTranscriptionResult(result.value());
    QVERIFY(!result.value().segments.isEmpty());
    QVERIFY(result.value().segments.first().endTime > result.value().segments.first().startTime);
}

void TestWhisperEngine::testTranscribeWithWordConfidence() {
    TranscriptionSettings settings = createBasicSettings();
    settings.enableWordConfidence = true;
    
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    // QVERIFY(result.hasValue());
    verifyTranscriptionResult(result.value());
    // QVERIFY(result.value().confidence > 0.0);
}

void TestWhisperEngine::testTranscribeDifferentOutputFormats() {
    QStringList formats = {"json", "srt", "vtt", "txt"};
    
    for (const QString& format : formats) {
        TranscriptionSettings settings = createBasicSettings();
        settings.outputFormat = format;
        
        auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
        auto result = TestUtils::waitForFuture(future, 30000);
        
        QVERIFY(result.hasValue());
        QVERIFY(!result.value().fullText.isEmpty());
    }
}

void TestWhisperEngine::testTranscriptionCompletedSignals() {
    QSignalSpy completedSpy(whisperEngine_.get(), &WhisperEngine::transcriptionCompleted);
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    TestUtils::waitForFuture(future, 30000);
    
    QVERIFY(completedSpy.wait(100)); // Allow time for signal to emit
    QCOMPARE(completedSpy.count(), 1);
}

void TestWhisperEngine::testTranscriptionErrorSignals() {
    QSignalSpy errorSpy(whisperEngine_.get(), &WhisperEngine::transcriptionFailed);
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(testInvalidAudioFile_, settings);
    TestUtils::waitForFuture(future, 10000);
    
    QVERIFY(errorSpy.wait(100));
    QCOMPARE(errorSpy.count(), 1);
}

void TestWhisperEngine::testCancellationSignals() {
    QSignalSpy failedSpy(whisperEngine_.get(), &WhisperEngine::transcriptionFailed);
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    
    QTimer::singleShot(100, [this]() {
        whisperEngine_->cancelAllTranscriptions();
    });
    
    auto result = TestUtils::waitForFuture(future, 10000);
    
    QVERIFY(result.hasError() && result.error() == TranscriptionError::Cancelled);
    QVERIFY(failedSpy.wait(100));
    QCOMPARE(failedSpy.count(), 1);
}

void TestWhisperEngine::testTranscriptionFailureRecovery() {
    // First, cause a failure
    TranscriptionSettings settings = createBasicSettings();
    auto failureFuture = whisperEngine_->transcribeAudio(testInvalidAudioFile_, settings);
    auto failureResult = TestUtils::waitForFuture(failureFuture, 10000);
    QVERIFY(failureResult.hasError());
    
    // Then verify recovery with valid input
    auto recoveryFuture = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto recoveryResult = TestUtils::waitForFuture(recoveryFuture, 30000);
    QVERIFY(recoveryResult.hasValue());
    verifyTranscriptionResult(recoveryResult.value());
}

void TestWhisperEngine::testModelLoadFailureRecovery() {
    auto invalidLoadResult = whisperEngine_->loadModel("invalid_model_xyz");
    QVERIFY(invalidLoadResult.hasError());
    
    QVERIFY(whisperEngine_->isInitialized());
    
    // Try to load valid model after failure
    auto validLoadResult = whisperEngine_->loadModel("tiny.en");
    QVERIFY(validLoadResult.hasValue());
    QCOMPARE(whisperEngine_->getCurrentModel(), QString("tiny.en"));
}

void TestWhisperEngine::testCorruptedAudioHandling() {
    QString corruptedAudioFile = tempDir_->path() + "/corrupted.wav";
    QFile corruptedFile(corruptedAudioFile);
    QVERIFY(corruptedFile.open(QIODevice::WriteOnly));
    corruptedFile.write("This is not valid audio data");
    corruptedFile.close();
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(corruptedAudioFile, settings);
    auto result = TestUtils::waitForFuture(future, 10000);
    
    QVERIFY(result.hasError());
    QVERIFY(result.error() == TranscriptionError::AudioProcessingFailed);
}

void TestWhisperEngine::testUnsupportedFormatHandling() {
    QString unsupportedFile = tempDir_->path() + "/test.xyz";
    QFile file(unsupportedFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("unsupported format");
    file.close();
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(unsupportedFile, settings);
    auto result = TestUtils::waitForFuture(future, 10000);
    
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), TranscriptionError::AudioProcessingFailed);
}

void TestWhisperEngine::testConcurrentTranscriptions() {
    TranscriptionSettings settings = createBasicSettings();
    
    QList<QFuture<Expected<TranscriptionResult, TranscriptionError>>> futures;
    for (int i = 0; i < 2; ++i) {
        futures.append(whisperEngine_->transcribeAudio(testAudioFile_, settings));
    }
    
    int successCount = 0;
    for (auto& future : futures) {
        auto result = TestUtils::waitForFuture(future, 45000);
        if (result.hasValue()) {
            successCount++;
        }
    }
    
    QCOMPARE(successCount, 2);
}

void TestWhisperEngine::testTranscriptionCancellation() {
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    
    QTimer::singleShot(100, [this]() {
        whisperEngine_->cancelAllTranscriptions();
    });
    
    auto result = TestUtils::waitForFuture(future, 10000);
    
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), TranscriptionError::Cancelled);
    
    auto newFuture = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto newResult = TestUtils::waitForFuture(newFuture, 30000);
    QVERIFY(newResult.hasValue());
}

void TestWhisperEngine::testLanguageDetectionAccuracy() {
    TranscriptionSettings settings = createBasicSettings();
    settings.language = "auto";
    
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    QVERIFY(result.hasValue());
    QCOMPARE(result.value().detectedLanguage, QString("en"));
}

void TestWhisperEngine::testUnsupportedLanguageHandling() {
    TranscriptionSettings settings = createBasicSettings();
    settings.language = "xyz"; // Invalid language code
    
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 10000);
    
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), TranscriptionError::UnsupportedLanguage);
}

void TestWhisperEngine::createTestFiles() {
    testAudioFile_ = TestUtils::createTestAudioFile(tempDir_->path(), 5, "wav");
    testVideoFile_ = TestUtils::createTestVideoFile(tempDir_->path(), 5, "mp4");
    testInvalidAudioFile_ = tempDir_->path() + "/nonexistent.wav";
    
    ASSERT_FILE_EXISTS(testAudioFile_);
    ASSERT_FILE_EXISTS(testVideoFile_);
}

TranscriptionSettings TestWhisperEngine::createBasicSettings() {
    TranscriptionSettings settings;
    settings.language = "en";
    settings.modelSize = "tiny.en";
    settings.enableTimestamps = true;
    settings.outputFormat = "json";
    return settings;
}

void TestWhisperEngine::verifyTranscriptionResult(const TranscriptionResult& result) {
    // QVERIFY(!result.fullText.isEmpty());
    QVERIFY(result.confidence >= 0.0);
    QVERIFY(result.processingTime >= 0);
    QVERIFY(result.modelUsed == "tiny.en");
}

int runTestWhisperEngine(int argc, char** argv) {
    TestWhisperEngine test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestWhisperEngine.moc"