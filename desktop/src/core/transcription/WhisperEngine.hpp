#pragma once

#include <QObject>
#include <QFuture>
#include <QMutex>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QTimer>
#include <QBuffer>
#include <QAudioSource>
#include <QAudioFormat>
#include <QProcess>
#include "../common/Expected.hpp"
#include "TranscriptionTypes.hpp"
#include <unordered_map>
#include <qhashfunctions.h>


namespace Murmur {

// Forward declarations
class WhisperWrapper;
class ModelDownloader;
struct WhisperResult;
struct WhisperSegment;
enum class WhisperError;

struct TranscriptionSettings {
    QString language = "auto";           // Language code or "auto" for detection
    QString modelSize = "base";          // tiny, base, small, medium, large
    bool enableTimestamps = true;        // Generate word-level timestamps
    bool enableWordConfidence = true;    // Calculate word-level confidence
    bool enableVAD = true;              // Voice Activity Detection
    double silenceThreshold = 0.02;     // Silence detection threshold
    int maxSegmentLength = 30;          // Maximum segment length in seconds
    bool enableDiarization = false;     // Speaker diarization (future)
    bool enablePunctuation = true;      // Add punctuation
    bool enableCapitalization = true;   // Capitalize sentences
    QString outputFormat = "json";      // json, srt, vtt, txt
    int beamSize = 5;                   // Beam search size
    double temperature = 0.0;           // Sampling temperature
    bool enableGPU = true;              // Use GPU acceleration if available
};

struct TranscriptionProgress {
    QString taskId;
    QString audioFile;
    double percentage;
    qint64 processedDuration;    // milliseconds
    qint64 totalDuration;        // milliseconds
    QString currentSegment;
    qint64 elapsedTime;          // milliseconds
    qint64 estimatedTimeRemaining; // milliseconds
    bool isCompleted;
    bool isCancelled;
};

/**
 * @brief High-performance speech-to-text engine using whisper.cpp
 * 
 * Provides audio transcription with multiple model sizes, language detection,
 * and comprehensive output formats with Qt integration.
 */
class WhisperEngine : public QObject {
    Q_OBJECT

public:
    explicit WhisperEngine(QObject* parent = nullptr);
    ~WhisperEngine();

    // Engine lifecycle
    Expected<bool, TranscriptionError> initialize(const QString& modelsPath = QString());
    void shutdown();
    bool isInitialized() const;
    bool isReady() const; // Test compatibility method
    
    // Model management
    Expected<bool, TranscriptionError> downloadModel(const QString& modelSize);
    Expected<bool, TranscriptionError> loadModel(const QString& modelSize);
    void unloadModel();
    QString getCurrentModel() const;
    QStringList getAvailableModels() const;
    QStringList getSupportedLanguages() const;
    
    // Transcription operations
    QFuture<Expected<TranscriptionResult, TranscriptionError>> transcribeAudio(
        const QString& audioFilePath,
        const TranscriptionSettings& settings = TranscriptionSettings()
    );
    
    QFuture<Expected<TranscriptionResult, TranscriptionError>> transcribeFromVideo(
        const QString& videoFilePath,
        const TranscriptionSettings& settings = TranscriptionSettings()
    );
    
    // Real-time transcription (streaming)
    Expected<QString, TranscriptionError> startRealtimeTranscription(
        const TranscriptionSettings& settings = TranscriptionSettings()
    );
    Expected<bool, TranscriptionError> feedAudioData(const QString& sessionId, const QByteArray& audioData);
    Expected<bool, TranscriptionError> stopRealtimeTranscription(const QString& sessionId);
    
    // Audio capture integration
    Expected<QString, TranscriptionError> startMicrophoneTranscription(
        const TranscriptionSettings& settings = TranscriptionSettings()
    );
    Expected<bool, TranscriptionError> stopMicrophoneTranscription(const QString& sessionId);
    
    // Language detection
    QFuture<Expected<QString, TranscriptionError>> detectLanguage(const QString& audioFilePath);
    
    // Format conversion
    Expected<QString, TranscriptionError> convertToSRT(const TranscriptionResult& result);
    Expected<QString, TranscriptionError> convertToVTT(const TranscriptionResult& result);
    Expected<QString, TranscriptionError> convertToPlainText(const TranscriptionResult& result);
    
    // Operation management
    void cancelTranscription(const QString& taskId);
    void cancelAllTranscriptions();
    QStringList getActiveTranscriptions() const;
    
    // Resource management
    void setMaxConcurrentTranscriptions(int maxTasks);
    void setMemoryLimit(qint64 maxMemoryMB);
    void setGPUEnabled(bool enabled);
    void setModelCacheSize(int maxModels);
    
    // Performance monitoring
    QJsonObject getPerformanceStats() const;
    void clearPerformanceStats();

signals:
    void transcriptionProgress(const QString& taskId, const TranscriptionProgress& progress);
    void transcriptionCompleted(const QString& taskId, const TranscriptionResult& result);
    void transcriptionFailed(const QString& taskId, TranscriptionError error, const QString& errorString);
    void modelDownloadProgress(const QString& modelSize, qint64 bytesReceived, qint64 bytesTotal);
    void modelDownloadCompleted(const QString& modelSize);
    void modelDownloadFailed(const QString& modelSize, const QString& error);
    void realtimeSegmentReady(const QString& sessionId, const TranscriptionSegment& segment);
    void realtimeTranscriptionStarted(const QString& sessionId);
    void realtimeTranscriptionStopped(const QString& sessionId);
    void microphoneVolumeChanged(const QString& sessionId, double volume);
    void audioBufferStatus(const QString& sessionId, qint64 bufferSize, qint64 maxBuffer);

private slots:
    void onRealtimeAudioReady();
    void onRealtimeProcessingTimer();
    void onMicrophoneStateChanged(QAudio::State state);
    void onWhisperProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onWhisperProcessError(QProcess::ProcessError error);
    void onWhisperProcessOutput();

private:
    struct TranscriptionTask {
        QString taskId;
        QString audioFile;
        TranscriptionSettings settings;
        qint64 startTime;
        qint64 audioDuration;
        bool isCancelled;
        QString tempDir;
        QProcess* process = nullptr;
    };
    
    struct RealtimeSession {
        QString sessionId;
        TranscriptionSettings settings;
        QString tempDir;
        QByteArray audioBuffer;
        QBuffer* audioBufferDevice;
        QAudioSource* audioInput;
        QAudioFormat audioFormat;
        QTimer* processingTimer;
        std::vector<float> processingBuffer;
        qint64 lastProcessedPosition;
        qint64 segmentStartTime;
        bool isActive;
        bool isMicrophoneSession;
        double currentVolume;
        qint64 totalAudioProcessed;
        QDateTime sessionStartTime;
    };
    
    // Core whisper.cpp integration
    std::unique_ptr<WhisperWrapper> whisperWrapper_;
    std::unique_ptr<ModelDownloader> modelDownloader_;
    Expected<bool, TranscriptionError> initializeWhisperCpp();
    
    // Real-time transcription methods
    Expected<bool, TranscriptionError> setupRealtimeSession(RealtimeSession* session);
    Expected<bool, TranscriptionError> setupMicrophoneCapture(RealtimeSession* session);
    Expected<bool, TranscriptionError> processRealtimeAudio(RealtimeSession* session);
    std::vector<float> convertBytesToFloat(const QByteArray& audioData);
    double calculateVolumeLevel(const QByteArray& audioData);
    bool shouldProcessSegment(RealtimeSession* session, qint64 currentTime);
    
    // Audio preprocessing
    Expected<QString, TranscriptionError> preprocessAudio(
        const QString& inputFile, 
        const QString& outputFile
    );
    Expected<qint64, TranscriptionError> getAudioDuration(const QString& audioFile);
    Expected<bool, TranscriptionError> validateAudioFormat(const QString& audioFile);
    
    // Model management
    Expected<bool, TranscriptionError> downloadModelFromHub(const QString& modelSize);
    Expected<bool, TranscriptionError> verifyModelIntegrity(const QString& modelPath);
    QString getModelPath(const QString& modelSize) const;
    QString getModelUrl(const QString& modelSize) const;
    
    // Process handling (for command-line whisper)
    QString getWhisperExecutablePath() const;
    QStringList buildTranscriptionCommand(const TranscriptionTask& task);
    QProcess* createWhisperProcess(const QString& workingDir);

    // Result parsing and conversion
    Expected<TranscriptionResult, TranscriptionError> parseWhisperOutput(
        const QString& output,
        const TranscriptionSettings& settings);
    Expected<QJsonObject, TranscriptionError> parseJsonOutput(const QString& jsonStr);
    TranscriptionResult convertWhisperResult(const WhisperResult& whisperResult, const TranscriptionSettings& settings);
    TranscriptionSegment convertWhisperSegment(const WhisperSegment& whisperSegment);
    TranscriptionError convertWhisperError(WhisperError error);
    
    // Progress tracking
    TranscriptionProgress createProgressInfo(const TranscriptionTask& task, double percentage);
    void updateTaskProgress(const QString& taskId, double percentage);
    
    // Session management
    QString generateTaskId();
    QString generateSessionId();
    void cleanupTask(const QString& taskId);
    void cleanupRealtimeSession(const QString& sessionId);
    
    // File utilities
    Expected<QString, TranscriptionError> createTempDirectory();
    void cleanupTempDirectory(const QString& tempDir);
    Expected<bool, TranscriptionError> extractAudioFromVideo(
        const QString& videoPath, 
        const QString& audioPath
    );
    
    // Configuration and state
    mutable QMutex tasksMutex_;
    std::unordered_map<QString, std::unique_ptr<TranscriptionTask>> activeTasks_;
    std::unordered_map<QString, std::unique_ptr<RealtimeSession>> realtimeSessions_;
    
    QString modelsPath_;
    QString currentModel_;
    bool isInitialized_ = false;
    bool gpuEnabled_ = true;
    
    // Resource limits
    int maxConcurrentTranscriptions_ = 2;
    qint64 maxMemoryMB_ = 4096;
    int maxModelCache_ = 3;
    bool checkResourceLimits() const;
    
    // Memory monitoring helpers
    qint64 getCurrentMemoryUsage() const;
    qint64 getModelMemoryRequirement(const QString& modelSize) const;
    
    // Performance tracking
    struct PerformanceStats {
        qint64 totalTranscriptions = 0;
        qint64 totalProcessingTime = 0;
        qint64 totalAudioDuration = 0;
        double averageRealTimeFactor = 0.0;
        QDateTime lastReset;
    };
    PerformanceStats performanceStats_;
    
    // Model information
    static const QStringList AVAILABLE_MODELS;
    static const QHash<QString, qint64> MODEL_SIZES;
    static const QStringList SUPPORTED_LANGUAGES;
    static const QString WHISPER_CPP_REPO_URL;
    
    // Audio format constants
    static const int SAMPLE_RATE = 16000;
    static const int CHANNELS = 1;
    static const QString AUDIO_FORMAT;
    
    // Real-time transcription constants
    static const int REALTIME_BUFFER_SIZE = 8192;      // Audio buffer size in samples
    static const int REALTIME_PROCESSING_INTERVAL = 500; // Processing interval in ms
    static const int REALTIME_SEGMENT_LENGTH = 5000;   // Segment length in ms
    static const int MIN_AUDIO_LENGTH = 1000;          // Minimum audio length for processing (ms)
    static constexpr double SILENCE_THRESHOLD = 0.01;     // Volume threshold for silence detection
    static const int MAX_BUFFER_SIZE = 32 * 1024 * 1024; // Maximum audio buffer size (32MB)
    
    // Progress parsing patterns
    static const QString PROGRESS_PATTERN;
    static const QString SEGMENT_PATTERN;
    static const QString TIMESTAMP_PATTERN;

    mutable QMutex whisperMutex_; 
};

} // namespace Murmur