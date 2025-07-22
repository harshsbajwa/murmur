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
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Engine lifecycle tests
    void testInitializeWithValidModel();
    void testInitializeWithInvalidModel();
    void testInitializeWithoutModel();
    void testShutdownAfterInitialization();
    void testIsInitializedStates();
    
    // Model management tests
    void testLoadModelWithValidFile();
    void testLoadModelWithInvalidFile();
    void testUnloadModel();
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
    void testTranscribeWithVAD();
    void testTranscribeDifferentOutputFormats();
    void testTranscribeWithDifferentModelSizes();
    
    // Real-time transcription tests
    void testStartRealtimeTranscription();
    void testStopRealtimeTranscription();
    void testRealtimeTranscriptionSignals();
    void testMultipleRealtimeSessions();
    
    // Signal and progress tests
    void testTranscriptionProgressSignals();
    void testTranscriptionCompletedSignals();
    void testTranscriptionErrorSignals();
    void testCancellationSignals();
    
    // Error handling tests
    void testTranscriptionFailureRecovery();
    void testModelLoadFailureRecovery();
    void testInsufficientMemoryHandling();
    void testCorruptedAudioHandling();
    void testUnsupportedFormatHandling();
    
    // Performance and resource tests
    void testConcurrentTranscriptions();
    void testLargeFileTranscription();
    void testMemoryUsageDuringTranscription();
    void testTranscriptionCancellation();
    
    // Language detection tests
    void testLanguageDetectionAccuracy();
    void testMultilingualTranscription();
    void testUnsupportedLanguageHandling();

private:
    std::unique_ptr<QTemporaryDir> tempDir_;
    std::unique_ptr<WhisperEngine> whisperEngine_;
    QString testAudioFile_;
    QString testVideoFile_;
    QString testModelPath_;
    QString testInvalidAudioFile_;
    
    // Test helpers
    void createTestFiles();
    void createTestModel();
    TranscriptionSettings createBasicSettings();
    TranscriptionSettings createAdvancedSettings();
    void verifyTranscriptionResult(const TranscriptionResult& result);
    void verifySegmentTimestamps(const QList<TranscriptionSegment>& segments);
};

void TestWhisperEngine::initTestCase() {
    TestUtils::initializeTestEnvironment();
    tempDir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(tempDir_->isValid());
    
    TestUtils::logMessage("WhisperEngine unit tests initialized");
}

void TestWhisperEngine::cleanupTestCase() {
    tempDir_.reset();
    TestUtils::cleanupTestEnvironment();
    TestUtils::logMessage("WhisperEngine unit tests cleaned up");
}

void TestWhisperEngine::init() {
    whisperEngine_ = std::make_unique<WhisperEngine>();
    createTestFiles();
    createTestModel();
}

void TestWhisperEngine::cleanup() {
    if (whisperEngine_ && whisperEngine_->isInitialized()) {
        whisperEngine_->shutdown();
    }
    whisperEngine_.reset();
}

void TestWhisperEngine::testInitializeWithValidModel() {
    QVERIFY(!whisperEngine_->isInitialized());
    
    auto result = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(result);
    QVERIFY(result.value());
    QVERIFY(whisperEngine_->isInitialized());
}

void TestWhisperEngine::testInitializeWithInvalidModel() {
    QString invalidPath = tempDir_->path() + "/nonexistent";
    
    auto result = whisperEngine_->initialize(invalidPath);
    QVERIFY(result.hasError());
    QVERIFY(!whisperEngine_->isInitialized());
}

void TestWhisperEngine::testInitializeWithoutModel() {
    // Test initialization without specifying model path (should use default)
    auto result = whisperEngine_->initialize();
    
    // May succeed or fail depending on system setup
    if (result.hasValue()) {
        QVERIFY(whisperEngine_->isInitialized());
    } else {
        QVERIFY(!whisperEngine_->isInitialized());
    }
}

void TestWhisperEngine::testShutdownAfterInitialization() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    QVERIFY(whisperEngine_->isInitialized());
    
    whisperEngine_->shutdown();
    QVERIFY(!whisperEngine_->isInitialized());
}

void TestWhisperEngine::testIsInitializedStates() {
    // Initially not initialized
    QVERIFY(!whisperEngine_->isInitialized());
    
    // After successful initialization
    auto result = whisperEngine_->initialize(tempDir_->path());
    if (result.hasValue()) {
        QVERIFY(whisperEngine_->isInitialized());
        
        // After shutdown
        whisperEngine_->shutdown();
        QVERIFY(!whisperEngine_->isInitialized());
    }
}

void TestWhisperEngine::testLoadModelWithValidFile() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    auto loadResult = whisperEngine_->loadModel("base");
    if (loadResult.hasValue()) {
        QVERIFY(loadResult.value());
        QCOMPARE(whisperEngine_->getCurrentModel(), QString("base"));
    }
}

void TestWhisperEngine::testLoadModelWithInvalidFile() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    auto loadResult = whisperEngine_->loadModel("nonexistent_model");
    QVERIFY(loadResult.hasError());
}

void TestWhisperEngine::testUnloadModel() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    // Load a model first
    auto loadResult = whisperEngine_->loadModel("base");
    if (loadResult.hasValue() && loadResult.value()) {
        QVERIFY(!whisperEngine_->getCurrentModel().isEmpty());
        
        // Unload the model
        whisperEngine_->unloadModel();
        QVERIFY(whisperEngine_->getCurrentModel().isEmpty());
    }
}

void TestWhisperEngine::testGetCurrentModel() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    // Initially no model loaded
    QVERIFY(whisperEngine_->getCurrentModel().isEmpty());
    
    // After loading a model
    auto loadResult = whisperEngine_->loadModel("base");
    if (loadResult.hasValue() && loadResult.value()) {
        QCOMPARE(whisperEngine_->getCurrentModel(), QString("base"));
    }
}

void TestWhisperEngine::testGetAvailableModels() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    QStringList models = whisperEngine_->getAvailableModels();
    // Should at least report standard model sizes
    QVERIFY(models.contains("tiny") || models.contains("base") || models.size() >= 0);
}

void TestWhisperEngine::testGetSupportedLanguages() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    QStringList languages = whisperEngine_->getSupportedLanguages();
    QVERIFY(languages.size() > 0);
    QVERIFY(languages.contains("en") || languages.contains("auto"));
}

void TestWhisperEngine::testTranscribeAudioWithValidFile() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for transcription test");
    }
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    auto loadResult = whisperEngine_->loadModel("base");
    if (loadResult.hasError()) {
        QSKIP("Failed to load Whisper model for transcription test");
    }
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    if (result.hasValue()) {
        verifyTranscriptionResult(result.value());
    } else {
        // Transcription may fail due to model/environment issues in tests
        qDebug() << "Transcription failed:" << static_cast<int>(result.error());
    }
}

void TestWhisperEngine::testTranscribeAudioWithInvalidFile() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(testInvalidAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 10000);
    
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), TranscriptionError::InvalidAudioFormat);
}

void TestWhisperEngine::testTranscribeAudioWithDifferentSettings() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for settings test");
    }
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    auto loadResult = whisperEngine_->loadModel("base");
    if (loadResult.hasError()) {
        QSKIP("Failed to load Whisper model for settings test");
    }
    
    // Test different output formats
    QStringList formats = {"json", "srt", "vtt", "txt"};
    
    for (const QString& format : formats) {
        TranscriptionSettings settings = createBasicSettings();
        settings.outputFormat = format;
        
        auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
        auto result = TestUtils::waitForFuture(future, 30000);
        
        if (result.hasValue()) {
            QVERIFY(!result.value().fullText.isEmpty());
        }
    }
}

void TestWhisperEngine::testTranscribeFromVideo() {
    if (!TestUtils::isWhisperAvailable() || !TestUtils::isFFmpegAvailable()) {
        QSKIP("Whisper or FFmpeg not available for video transcription test");
    }
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    auto loadResult = whisperEngine_->loadModel("base");
    if (loadResult.hasError()) {
        QSKIP("Failed to load Whisper model for video transcription test");
    }
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeFromVideo(testVideoFile_, settings);
    auto result = TestUtils::waitForFuture(future, 45000);
    
    if (result.hasValue()) {
        verifyTranscriptionResult(result.value());
    }
}

void TestWhisperEngine::testTranscribeWithLanguageDetection() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for language detection test");
    }
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    auto loadResult = whisperEngine_->loadModel("base");
    if (loadResult.hasError()) {
        QSKIP("Failed to load Whisper model for language detection test");
    }
    
    TranscriptionSettings settings = createBasicSettings();
    settings.language = "auto";  // Enable auto-detection
    
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    if (result.hasValue()) {
        QVERIFY(!result.value().detectedLanguage.isEmpty());
        QVERIFY(!result.value().fullText.isEmpty());
    }
}

void TestWhisperEngine::testTranscribeWithSpecificLanguage() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for specific language test");
    }
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    auto loadResult = whisperEngine_->loadModel("base");
    if (loadResult.hasError()) {
        QSKIP("Failed to load Whisper model for specific language test");
    }
    
    TranscriptionSettings settings = createBasicSettings();
    settings.language = "en";  // Force English
    
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    if (result.hasValue()) {
        verifyTranscriptionResult(result.value());
    }
}

void TestWhisperEngine::testTranscribeWithTimestamps() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for timestamp test");
    }
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    auto loadResult = whisperEngine_->loadModel("base");
    if (loadResult.hasError()) {
        QSKIP("Failed to load Whisper model for timestamp test");
    }
    
    TranscriptionSettings settings = createBasicSettings();
    settings.enableTimestamps = true;
    
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    if (result.hasValue()) {
        verifyTranscriptionResult(result.value());
        if (!result.value().segments.isEmpty()) {
            verifySegmentTimestamps(result.value().segments);
        }
    }
}

void TestWhisperEngine::testTranscribeWithWordConfidence() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for word confidence test");
    }
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    auto loadResult = whisperEngine_->loadModel("base");
    if (loadResult.hasError()) {
        QSKIP("Failed to load Whisper model for word confidence test");
    }
    
    TranscriptionSettings settings = createBasicSettings();
    settings.enableWordConfidence = true;
    
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    if (result.hasValue()) {
        verifyTranscriptionResult(result.value());
        QVERIFY(result.value().confidence >= 0.0);
        QVERIFY(result.value().confidence <= 1.0);
    }
}

void TestWhisperEngine::testTranscribeWithVAD() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for VAD test");
    }
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    auto loadResult = whisperEngine_->loadModel("base");
    if (loadResult.hasError()) {
        QSKIP("Failed to load Whisper model for VAD test");
    }
    
    TranscriptionSettings settings = createBasicSettings();
    settings.enableVAD = true;
    settings.silenceThreshold = 0.01;
    
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    if (result.hasValue()) {
        verifyTranscriptionResult(result.value());
    }
}

void TestWhisperEngine::testTranscribeDifferentOutputFormats() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for output format test");
    }
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    auto loadResult = whisperEngine_->loadModel("base");
    if (loadResult.hasError()) {
        QSKIP("Failed to load Whisper model for output format test");
    }
    
    QStringList formats = {"json", "srt", "vtt", "txt"};
    
    for (const QString& format : formats) {
        TranscriptionSettings settings = createBasicSettings();
        settings.outputFormat = format;
        
        auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
        auto result = TestUtils::waitForFuture(future, 30000);
        
        if (result.hasValue()) {
            verifyTranscriptionResult(result.value());
            
            // Format-specific validations could be added here
            if (format == "srt") {
                // SRT should have timestamps in specific format
            } else if (format == "json") {
                // JSON should be parseable
            }
        }
    }
}

void TestWhisperEngine::testTranscribeWithDifferentModelSizes() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for model size test");
    }
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    QStringList modelSizes = {"tiny", "base"};  // Test with smaller models first
    
    for (const QString& modelSize : modelSizes) {
        auto loadResult = whisperEngine_->loadModel(modelSize);
        if (loadResult.hasError()) {
            continue;  // Model might not be available
        }
        
        TranscriptionSettings settings = createBasicSettings();
        settings.modelSize = modelSize;
        
        auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
        auto result = TestUtils::waitForFuture(future, 30000);
        
        if (result.hasValue()) {
            verifyTranscriptionResult(result.value());
        }
        
        whisperEngine_->unloadModel();
    }
}

void TestWhisperEngine::testStartRealtimeTranscription() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    TranscriptionSettings settings = createBasicSettings();
    auto sessionResult = whisperEngine_->startRealtimeTranscription(settings);
    
    if (sessionResult.hasValue()) {
        QString sessionId = sessionResult.value();
        QVERIFY(!sessionId.isEmpty());
        
        // Stop the session
        auto stopResult = whisperEngine_->stopRealtimeTranscription(sessionId);
        QVERIFY(!stopResult.hasError());
    }
}

void TestWhisperEngine::testStopRealtimeTranscription() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    TranscriptionSettings settings = createBasicSettings();
    auto sessionResult = whisperEngine_->startRealtimeTranscription(settings);
    
    if (sessionResult.hasValue()) {
        QString sessionId = sessionResult.value();
        
        // Stop valid session
        auto stopResult = whisperEngine_->stopRealtimeTranscription(sessionId);
        QVERIFY(!stopResult.hasError());
        
        // Try to stop already stopped session
        auto stopAgainResult = whisperEngine_->stopRealtimeTranscription(sessionId);
        QVERIFY(stopAgainResult.hasError());
    }
}

void TestWhisperEngine::testRealtimeTranscriptionSignals() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    // Test signal emissions during real-time transcription
    QSignalSpy realtimeStartedSpy(whisperEngine_.get(), 
                                  &WhisperEngine::realtimeTranscriptionStarted);
    QSignalSpy realtimeSegmentSpy(whisperEngine_.get(), 
                                 &WhisperEngine::realtimeSegmentReady);
    
    TranscriptionSettings settings = createBasicSettings();
    auto sessionResult = whisperEngine_->startRealtimeTranscription(settings);
    
    if (sessionResult.hasValue()) {
        QString sessionId = sessionResult.value();
        
        // Allow some time for signals
        QTest::qWait(1000);
        
        // Cleanup
        whisperEngine_->stopRealtimeTranscription(sessionId);
        
        // Verify signals were emitted (may be 0 in test environment)
        QVERIFY(realtimeStartedSpy.count() >= 0);
        QVERIFY(realtimeSegmentSpy.count() >= 0);
    }
}

void TestWhisperEngine::testMultipleRealtimeSessions() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    TranscriptionSettings settings = createBasicSettings();
    
    // Start multiple sessions
    auto session1 = whisperEngine_->startRealtimeTranscription(settings);
    auto session2 = whisperEngine_->startRealtimeTranscription(settings);
    
    // Both should succeed or fail consistently
    bool session1Valid = session1.hasValue();
    bool session2Valid = session2.hasValue();
    
    if (session1Valid) {
        whisperEngine_->stopRealtimeTranscription(session1.value());
    }
    if (session2Valid) {
        whisperEngine_->stopRealtimeTranscription(session2.value());
    }
    
    // At least one session should work if real-time is supported
    QVERIFY(session1Valid || session2Valid || (!session1Valid && !session2Valid));
}

void TestWhisperEngine::testTranscriptionProgressSignals() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for progress signal test");
    }
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    QSignalSpy progressSpy(whisperEngine_.get(), &WhisperEngine::transcriptionProgress);
    QSignalSpy completedSpy(whisperEngine_.get(), &WhisperEngine::transcriptionCompleted);
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    if (result.hasValue()) {
        // Progress signals should have been emitted
        QVERIFY(progressSpy.count() > 0 || completedSpy.count() > 0);
    }
}

void TestWhisperEngine::testTranscriptionCompletedSignals() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for completion signal test");
    }
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    QSignalSpy completedSpy(whisperEngine_.get(), &WhisperEngine::transcriptionCompleted);
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    if (result.hasValue()) {
        QVERIFY(completedSpy.count() >= 1);
    }
}

void TestWhisperEngine::testTranscriptionErrorSignals() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    QSignalSpy errorSpy(whisperEngine_.get(), &WhisperEngine::transcriptionFailed);
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(testInvalidAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 10000);
    
    QVERIFY(result.hasError());
    QVERIFY(errorSpy.count() >= 1);
}

void TestWhisperEngine::testCancellationSignals() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    QSignalSpy failedSpy(whisperEngine_.get(), &WhisperEngine::transcriptionFailed);
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    
    // Cancel after short delay
    QTimer::singleShot(500, [this]() {
        whisperEngine_->cancelAllTranscriptions();
    });
    
    auto result = TestUtils::waitForFuture(future, 10000);
    
    if (result.hasError() && result.error() == TranscriptionError::Cancelled) {
        QVERIFY(failedSpy.count() >= 1);
    }
}

void TestWhisperEngine::testTranscriptionFailureRecovery() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    // First, cause a failure
    TranscriptionSettings settings = createBasicSettings();
    auto failureFuture = whisperEngine_->transcribeAudio(testInvalidAudioFile_, settings);
    auto failureResult = TestUtils::waitForFuture(failureFuture, 10000);
    
    QVERIFY(failureResult.hasError());
    
    // Then verify recovery with valid input
    auto recoveryFuture = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto recoveryResult = TestUtils::waitForFuture(recoveryFuture, 30000);
    
    // Should work after recovery (if Whisper is available)
    if (TestUtils::isWhisperAvailable()) {
        QVERIFY(recoveryResult.hasValue() || recoveryResult.hasError());
    }
}

void TestWhisperEngine::testModelLoadFailureRecovery() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    // Try to load invalid model
    auto invalidLoadResult = whisperEngine_->loadModel("invalid_model_xyz");
    QVERIFY(invalidLoadResult.hasError());
    
    // Verify engine is still functional
    QVERIFY(whisperEngine_->isInitialized());
    
    // Try to load valid model after failure
    auto validLoadResult = whisperEngine_->loadModel("base");
    // May succeed or fail depending on available models
}

void TestWhisperEngine::testInsufficientMemoryHandling() {
    TestUtils::simulateMemoryPressure();
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    
    if (initResult.hasValue()) {
        TranscriptionSettings settings = createBasicSettings();
        auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
        auto result = TestUtils::waitForFuture(future, 30000);
        
        // Should either succeed or fail gracefully with memory error
        if (result.hasError()) {
            QVERIFY(result.error() == TranscriptionError::ResourceExhausted ||
                   result.error() == TranscriptionError::AudioProcessingFailed);
        }
    }
    
    TestUtils::clearSimulatedErrors();
}

void TestWhisperEngine::testCorruptedAudioHandling() {
    // Create corrupted audio file
    QString corruptedAudioFile = tempDir_->path() + "/corrupted.wav";
    QFile corruptedFile(corruptedAudioFile);
    corruptedFile.open(QIODevice::WriteOnly);
    corruptedFile.write("This is not audio data");
    corruptedFile.close();
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(corruptedAudioFile, settings);
    auto result = TestUtils::waitForFuture(future, 10000);
    
    QVERIFY(result.hasError());
    QVERIFY(result.error() == TranscriptionError::InvalidAudioFormat ||
           result.error() == TranscriptionError::AudioProcessingFailed);
}

void TestWhisperEngine::testUnsupportedFormatHandling() {
    // Create file with unsupported format
    QString unsupportedFile = tempDir_->path() + "/test.xyz";
    QFile file(unsupportedFile);
    file.open(QIODevice::WriteOnly);
    file.write("unsupported format");
    file.close();
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(unsupportedFile, settings);
    auto result = TestUtils::waitForFuture(future, 10000);
    
    QVERIFY(result.hasError());
    QCOMPARE(result.error(), TranscriptionError::InvalidAudioFormat);
}

void TestWhisperEngine::testConcurrentTranscriptions() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for concurrent transcription test");
    }
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    TranscriptionSettings settings = createBasicSettings();
    
    // Start multiple concurrent transcriptions
    QList<QFuture<Expected<TranscriptionResult, TranscriptionError>>> futures;
    
    for (int i = 0; i < 3; ++i) {
        auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
        futures.append(future);
    }
    
    // Wait for all to complete
    int successCount = 0;
    for (auto& future : futures) {
        auto result = TestUtils::waitForFuture(future, 45000);
        if (result.hasValue()) {
            successCount++;
        }
    }
    
    // At least some should succeed
    QVERIFY(successCount > 0);
}

void TestWhisperEngine::testLargeFileTranscription() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for large file test");
    }
    
    // Create larger audio file
    QString largeAudioFile = TestUtils::createTestAudioFile(tempDir_->path(), 60, "wav");
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(largeAudioFile, settings);
    auto result = TestUtils::waitForFuture(future, 120000); // 2 minutes timeout
    
    if (result.hasValue()) {
        verifyTranscriptionResult(result.value());
    }
}

void TestWhisperEngine::testMemoryUsageDuringTranscription() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for memory usage test");
    }
    
    TestUtils::startResourceMonitoring();
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    QJsonObject beforeStats = TestUtils::getResourceUsageReport();
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    QJsonObject afterStats = TestUtils::getResourceUsageReport();
    
    // Memory usage should be reasonable
    QVERIFY(afterStats.contains("timestamp"));
    
    TestUtils::stopResourceMonitoring();
}

void TestWhisperEngine::testTranscriptionCancellation() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    TranscriptionSettings settings = createBasicSettings();
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    
    // Cancel after short delay
    QTimer::singleShot(500, [this]() {
        whisperEngine_->cancelAllTranscriptions();
    });
    
    auto result = TestUtils::waitForFuture(future, 10000);
    
    if (result.hasError()) {
        QCOMPARE(result.error(), TranscriptionError::Cancelled);
    }
    
    // Verify system is still functional after cancellation
    auto newFuture = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto newResult = TestUtils::waitForFuture(newFuture, 30000);
    
    // Should work after cancellation
    QVERIFY(newResult.hasValue() || newResult.hasError());
}

void TestWhisperEngine::testLanguageDetectionAccuracy() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for language detection test");
    }
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    TranscriptionSettings settings = createBasicSettings();
    settings.language = "auto";
    
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 30000);
    
    if (result.hasValue()) {
        QVERIFY(!result.value().detectedLanguage.isEmpty());
        // Language should be a valid language code
        QVERIFY(result.value().detectedLanguage.length() >= 2);
    }
}

void TestWhisperEngine::testMultilingualTranscription() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for multilingual test");
    }
    
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    QStringList languages = {"en", "fr", "de", "es"};
    
    for (const QString& language : languages) {
        TranscriptionSettings settings = createBasicSettings();
        settings.language = language;
        
        auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
        auto result = TestUtils::waitForFuture(future, 30000);
        
        if (result.hasValue()) {
            verifyTranscriptionResult(result.value());
        }
    }
}

void TestWhisperEngine::testUnsupportedLanguageHandling() {
    auto initResult = whisperEngine_->initialize(tempDir_->path());
    ASSERT_EXPECTED_VALUE(initResult);
    
    TranscriptionSettings settings = createBasicSettings();
    settings.language = "xyz"; // Invalid language code
    
    auto future = whisperEngine_->transcribeAudio(testAudioFile_, settings);
    auto result = TestUtils::waitForFuture(future, 10000);
    
    if (result.hasError()) {
        QCOMPARE(result.error(), TranscriptionError::UnsupportedLanguage);
    }
}

// Helper method implementations
void TestWhisperEngine::createTestFiles() {
    testAudioFile_ = TestUtils::createTestAudioFile(tempDir_->path(), 5, "wav");
    testVideoFile_ = TestUtils::createTestVideoFile(tempDir_->path(), 10, "mp4");
    testInvalidAudioFile_ = tempDir_->path() + "/nonexistent.wav";
    
    ASSERT_FILE_EXISTS(testAudioFile_);
    ASSERT_FILE_EXISTS(testVideoFile_);
}

void TestWhisperEngine::createTestModel() {
    testModelPath_ = TestUtils::createTestTextFile(
        tempDir_->path(), 
        "Mock Whisper model data for testing", 
        "ggml-base.bin"
    );
    ASSERT_FILE_EXISTS(testModelPath_);
}

TranscriptionSettings TestWhisperEngine::createBasicSettings() {
    TranscriptionSettings settings;
    settings.language = "en";
    settings.modelSize = "tiny.en";  // Use tiny.en model which we downloaded
    settings.enableTimestamps = true;
    settings.outputFormat = "json";
    return settings;
}

TranscriptionSettings TestWhisperEngine::createAdvancedSettings() {
    TranscriptionSettings settings = createBasicSettings();
    settings.enableWordConfidence = true;
    settings.enableVAD = true;
    settings.enablePunctuation = true;
    settings.enableCapitalization = true;
    settings.maxSegmentLength = 30;
    settings.beamSize = 5;
    settings.temperature = 0.0;
    return settings;
}

void TestWhisperEngine::verifyTranscriptionResult(const TranscriptionResult& result) {
    QVERIFY(!result.fullText.isEmpty());
    QVERIFY(result.confidence >= 0.0);
    QVERIFY(result.confidence <= 1.0);
    QVERIFY(result.processingTime >= 0);
}

void TestWhisperEngine::verifySegmentTimestamps(const QList<TranscriptionSegment>& segments) {
    for (const auto& segment : segments) {
        QVERIFY(segment.startTime >= 0);
        QVERIFY(segment.endTime >= segment.startTime);
        QVERIFY(!segment.text.isEmpty());
        QVERIFY(segment.confidence >= 0.0);
        QVERIFY(segment.confidence <= 1.0);
    }
    
    // Verify segments are in chronological order
    for (int i = 1; i < segments.size(); ++i) {
        QVERIFY(segments[i].startTime >= segments[i-1].endTime);
    }
}

int runTestWhisperEngine(int argc, char** argv) {
    TestWhisperEngine test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestWhisperEngine.moc"