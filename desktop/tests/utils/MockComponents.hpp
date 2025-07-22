#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QFuture>
#include <QtConcurrent/QtConcurrent>
#include <QtNetwork/QNetworkReply>

#include "../../src/core/common/Expected.hpp"
#include "../../src/core/media/FFmpegWrapper.hpp"
#include "../../src/core/media/HardwareAccelerator.hpp"
#include "../../src/core/media/MediaPipeline.hpp"
#include "../../src/core/torrent/LibTorrentWrapper.hpp"
#include "../../src/core/torrent/TorrentEngine.hpp"
#include "../../src/core/transcription/WhisperWrapper.hpp"
#include "../../src/core/transcription/WhisperEngine.hpp"
#include "../../src/core/storage/FileManager.hpp"

namespace Murmur {
namespace Test {

/**
 * @brief Mock FFmpeg wrapper for testing
 */
class MockFFmpegWrapper : public QObject {
    Q_OBJECT

public:
    explicit MockFFmpegWrapper(QObject* parent = nullptr);

    // Mock configuration
    void setSimulateError(bool enabled) { simulateError_ = enabled; }
    void setProcessingDelayMs(int delayMs) { processingDelayMs_ = delayMs; }
    void setFailureRate(double rate) { failureRate_ = rate; } // 0.0 to 1.0
    
    // Mock operations
    QFuture<Expected<QString, FFmpegError>> convertVideo(
        const QString& inputPath,
        const QString& outputPath,
        const ConversionOptions& options,
        FFmpegProgressCallback progressCallback = nullptr
    );
    
    QFuture<Expected<MediaFileInfo, FFmpegError>> analyzeFile(const QString& filePath);
    QFuture<Expected<QString, FFmpegError>> extractAudio(const QString& inputPath, const QString& outputPath);
    
    Expected<bool, FFmpegError> initialize();
    bool isInitialized() const { return initialized_; }
    void cleanup();
    
    // Test utilities
    int getConversionCount() const { return conversionCount_; }
    int getAnalysisCount() const { return analysisCount_; }
    void resetCounters();

signals:
    void operationStarted(const QString& operationId, const QString& inputPath);
    void operationProgress(const QString& operationId, const ProgressInfo& progress);
    void operationCompleted(const QString& operationId, const QString& outputPath);
    void operationFailed(const QString& operationId, FFmpegError error, const QString& errorMessage);
    void operationCancelled(const QString& operationId);

private slots:
    void simulateProgress();
    void simulateCompletion();

private:
    bool shouldSimulateFailure() const;
    QString generateOperationId() const;
    ProgressInfo createProgressInfo(double percentage) const;
    
    bool simulateError_ = false;
    int processingDelayMs_ = 1000;
    double failureRate_ = 0.0;
    bool initialized_ = false;
    
    int conversionCount_ = 0;
    int analysisCount_ = 0;
    
    struct MockOperation {
        QString id;
        QString inputPath;
        QString outputPath;
        QTimer* progressTimer;
        QTimer* completionTimer;
        FFmpegProgressCallback progressCallback;
        double currentProgress = 0.0;
    };
    
    QHash<QString, MockOperation> activeOperations_;
};

/**
 * @brief Mock hardware accelerator for testing
 */
class MockHardwareAccelerator : public QObject {
    Q_OBJECT

public:
    explicit MockHardwareAccelerator(QObject* parent = nullptr);
    
    // Mock configuration
    void setHardwareAvailable(bool available) { hardwareAvailable_ = available; }
    void setAccelerationEnabled(bool enabled) { accelerationEnabled_ = enabled; }
    void setSupportedCodecs(const QStringList& codecs) { supportedCodecs_ = codecs; }
    
    // Mock operations
    Expected<bool, AcceleratorError> initialize();
    bool isHardwareAccelerationEnabled() const { return accelerationEnabled_; }
    bool isHardwareAvailable() const { return hardwareAvailable_; }
    
    QStringList getSupportedCodecs() const { return supportedCodecs_; }
    bool isCodecSupported(const QString& codec) const;
    
    Expected<HardwareCapabilities, AcceleratorError> getCapabilities();
    
    // Test utilities
    void simulateHardwareFailure() { hardwareAvailable_ = false; }
    void restoreHardware() { hardwareAvailable_ = true; }

signals:
    void hardwareStatusChanged(bool available);
    void accelerationToggled(bool enabled);

private:
    bool hardwareAvailable_ = true;
    bool accelerationEnabled_ = true;
    QStringList supportedCodecs_ = {"h264", "h265", "vp9"};
    bool initialized_ = false;
};

/**
 * @brief Mock LibTorrent wrapper for testing
 */
class MockLibTorrentWrapper : public QObject {
    Q_OBJECT

public:
    explicit MockLibTorrentWrapper(QObject* parent = nullptr);
    
    // Mock configuration
    void setSimulateNetworkIssues(bool enabled) { simulateNetworkIssues_ = enabled; }
    void setDownloadSpeed(int bytesPerSecond) { downloadSpeed_ = bytesPerSecond; }
    void setUploadSpeed(int bytesPerSecond) { uploadSpeed_ = bytesPerSecond; }
    
    // Mock operations
    Expected<bool, TorrentError> initialize();
    Expected<QString, TorrentError> addMagnetLink(const QString& magnetUri, const QString& savePath);
    Expected<QString, TorrentError> addTorrentFile(const QString& torrentPath, const QString& savePath);
    
    Expected<bool, TorrentError> removeTorrent(const QString& infoHash, bool deleteFiles = false);
    Expected<bool, TorrentError> pauseTorrent(const QString& infoHash);
    Expected<bool, TorrentError> resumeTorrent(const QString& infoHash);
    
    Expected<TorrentStats, TorrentError> getTorrentStatus(const QString& infoHash);
    Expected<QList<TorrentStats>, TorrentError> getAllTorrents();
    
    // Test utilities
    void simulateDownload(const QString& infoHash);
    void setTorrentProgress(const QString& infoHash, double progress);
    int getActiveTorrentCount() const { return activeTorrents_.size(); }

signals:
    void torrentAdded(const QString& infoHash);
    void torrentRemoved(const QString& infoHash);
    void torrentStatusChanged(const QString& infoHash, const TorrentStats& status);
    void downloadProgress(const QString& infoHash, qint64 downloaded, qint64 total);

private slots:
    void updateTorrentProgress();

private:
    bool simulateNetworkIssues_ = false;
    int downloadSpeed_ = 1024 * 1024; // 1 MB/s
    int uploadSpeed_ = 256 * 1024;    // 256 KB/s
    bool initialized_ = false;
    
    struct MockTorrent {
        QString infoHash;
        QString name;
        QString savePath;
        qint64 totalSize = 100 * 1024 * 1024; // 100MB default
        qint64 downloaded = 0;
        double progress = 0.0;
        TorrentState state = TorrentState::Downloading;
        QTimer* progressTimer;
    };
    
    QHash<QString, MockTorrent> activeTorrents_;
    QTimer* globalUpdateTimer_;
};

/**
 * @brief Mock Whisper wrapper for testing
 */
class MockWhisperWrapper : public QObject {
    Q_OBJECT

public:
    explicit MockWhisperWrapper(QObject* parent = nullptr);
    
    // Mock configuration
    void setProcessingDelayMs(int delayMs) { processingDelayMs_ = delayMs; }
    void setSimulateError(bool enabled) { simulateError_ = enabled; }
    void setAccuracyLevel(double accuracy) { accuracyLevel_ = accuracy; } // 0.0 to 1.0
    
    // Mock operations
    Expected<bool, WhisperError> initialize(const QString& modelPath);
    
    QFuture<Expected<TranscriptionResult, WhisperError>> transcribeFile(
        const QString& audioPath,
        const TranscriptionSettings& options = TranscriptionSettings{}
    );
    
    QFuture<Expected<TranscriptionResult, WhisperError>> transcribeAudioData(
        const QByteArray& audioData,
        const TranscriptionSettings& options = TranscriptionSettings{}
    );
    
    bool isModelLoaded() const { return modelLoaded_; }
    QString getModelInfo() const { return modelInfo_; }
    
    // Test utilities
    void setMockTranscriptionText(const QString& text) { mockTranscriptionText_ = text; }
    int getTranscriptionCount() const { return transcriptionCount_; }

signals:
    void transcriptionStarted(const QString& taskId);
    void transcriptionProgress(const QString& taskId, int progressPercent);
    void transcriptionCompleted(const QString& taskId, const TranscriptionResult& result);
    void transcriptionFailed(const QString& taskId, WhisperError error, const QString& errorMessage);

private:
    QString generateTaskId() const;
    TranscriptionResult createMockResult(const QString& audioPath) const;
    
    int processingDelayMs_ = 2000;
    bool simulateError_ = false;
    double accuracyLevel_ = 0.95;
    bool modelLoaded_ = false;
    QString modelInfo_ = "Mock Whisper Model v1.0";
    QString mockTranscriptionText_ = "This is a mock transcription result for testing purposes.";
    
    int transcriptionCount_ = 0;
};

/**
 * @brief Mock network manager for testing network operations
 */
class MockNetworkManager : public QObject {
    Q_OBJECT

public:
    explicit MockNetworkManager(QObject* parent = nullptr);
    
    // Mock configuration
    void setNetworkAvailable(bool available) { networkAvailable_ = available; }
    void setLatencyMs(int latencyMs) { latencyMs_ = latencyMs; }
    void setBandwidthBytesPerSecond(int bandwidth) { bandwidthBps_ = bandwidth; }
    void setErrorRate(double rate) { errorRate_ = rate; }
    
    // Mock operations
    QFuture<Expected<QByteArray, QNetworkReply::NetworkError>> downloadFile(const QString& url);
    QFuture<Expected<bool, QNetworkReply::NetworkError>> uploadFile(const QString& url, const QByteArray& data);
    
    bool isNetworkAvailable() const { return networkAvailable_; }
    
    // Test utilities
    void simulateNetworkOutage(int durationMs);
    void restoreNetwork();

signals:
    void networkStatusChanged(bool available);
    void downloadProgress(const QString& url, qint64 downloaded, qint64 total);
    void uploadProgress(const QString& url, qint64 uploaded, qint64 total);

private:
    bool networkAvailable_ = true;
    int latencyMs_ = 100;
    int bandwidthBps_ = 1024 * 1024; // 1 MB/s
    double errorRate_ = 0.0;
    
    QTimer* outageTimer_ = nullptr;
};

/**
 * @brief Factory for creating mock components
 */
class MockComponentFactory {
public:
    static std::unique_ptr<MockFFmpegWrapper> createMockFFmpegWrapper();
    static std::unique_ptr<MockHardwareAccelerator> createMockHardwareAccelerator();
    static std::unique_ptr<MockLibTorrentWrapper> createMockLibTorrentWrapper();
    static std::unique_ptr<MockWhisperWrapper> createMockWhisperWrapper();
    static std::unique_ptr<MockNetworkManager> createMockNetworkManager();
    
    // Preset configurations
    static std::unique_ptr<MockFFmpegWrapper> createSlowFFmpegWrapper();
    static std::unique_ptr<MockFFmpegWrapper> createUnreliableFFmpegWrapper();
    static std::unique_ptr<MockHardwareAccelerator> createNoHardwareAccelerator();
    static std::unique_ptr<MockLibTorrentWrapper> createSlowTorrentWrapper();
    static std::unique_ptr<MockWhisperWrapper> createInaccurateWhisperWrapper();
};

} // namespace Test
} // namespace Murmur