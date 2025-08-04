#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QHash>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QSslError>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QFuture>
#include <QMutex>
#include <QCryptographicHash>

#include "../common/Expected.hpp"

namespace Murmur {

enum class DownloadError {
    NetworkError,
    TimeoutError,
    ChecksumMismatch,
    InsufficientDiskSpace,
    PermissionDenied,
    InvalidUrl,
    FileSystemError,
    CancellationRequested,
    ServerError,
    UnknownError
};

struct DownloadInfo {
    QString id;
    QString url;
    QString localPath;
    QString tempPath;
    QString expectedChecksum;
    qint64 totalSize = 0;
    qint64 downloadedSize = 0;
    double percentage = 0.0;
    QString status = "pending";
    QElapsedTimer timer;
    double downloadSpeed = 0.0; // bytes per second
    QDateTime startTime;
    QString errorMessage;
    bool isCancelled = false;
    
    // Resume support
    bool supportsResume = false;
    qint64 resumePosition = 0;
    
    // Retry information
    int retryCount = 0;
    int maxRetries = 3;
    QDateTime lastRetryTime;
};

/**
 * @brief Download manager with resume, retry, and verification
 * 
 * This class handles downloading of large files (like Whisper models) with
 * error handling, automatic retry, resume capability, and integrity verification.
 */
class ModelDownloader : public QObject {
    Q_OBJECT

public:
    explicit ModelDownloader(QObject* parent = nullptr);
    ~ModelDownloader() override;

    /**
     * @brief Synchronously downloads a file. This is a blocking operation.
     * @return The local path on success, or an error.
     */
    Expected<QString, DownloadError> downloadFile(
        const QString& url,
        const QString& localPath,
        const QString& expectedChecksum = QString(),
        bool enableResume = true
    );

    /**
     * @brief Cancel a download
     * @param downloadId Download ID returned by downloadFile
     */
    void cancelDownload(const QString& downloadId);

    /**
     * @brief Cancel all active downloads
     */
    void cancelAllDownloads();

    /**
     * @brief Get information about an active download
     * @param downloadId Download ID
     * @return Download information or empty if not found
     */
    DownloadInfo getDownloadInfo(const QString& downloadId) const;

    /**
     * @brief Get list of active download IDs
     * @return List of active downloads
     */
    QStringList getActiveDownloads() const;

    /**
     * @brief Check if a download is active
     * @param downloadId Download ID
     * @return true if download is in progress
     */
    bool isDownloadActive(const QString& downloadId) const;

    // Configuration
    void setMaxConcurrentDownloads(int maxDownloads);
    void setTimeout(int timeoutSeconds);
    void setRetryAttempts(int maxRetries);
    void setRetryDelay(int delaySeconds);
    void setUserAgent(const QString& userAgent);
    void setMaxRedirects(int maxRedirects);
    void setVerifySSL(bool verify);

    // Statistics
    int getActiveDownloadCount() const;
    double getTotalDownloadSpeed() const;
    qint64 getTotalBytesDownloaded() const;
    QJsonObject getStatistics() const;

signals:
    void downloadStarted(const QString& downloadId, const QString& url);
    void downloadProgress(const QString& downloadId, qint64 bytesReceived, qint64 bytesTotal, double speed);
    void downloadCompleted(const QString& downloadId, const QString& localPath);
    void downloadFailed(const QString& downloadId, DownloadError error, const QString& errorMessage);
    void downloadCancelled(const QString& downloadId);
    void downloadResumed(const QString& downloadId, qint64 resumePosition);
    void checksumVerificationStarted(const QString& downloadId);
    void checksumVerificationCompleted(const QString& downloadId, bool success);

private slots:
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished();
    void onDownloadError(QNetworkReply::NetworkError error);
    void onSSLErrors(const QList<QSslError>& errors);
    void onRetryTimer();

private:
    struct ModelDownloaderPrivate;
    std::unique_ptr<ModelDownloaderPrivate> d;

    // Download management
    Q_INVOKABLE void startDownloadInternal(DownloadInfo info);
    QString generateDownloadId();
    Expected<bool, DownloadError> prepareDownload(DownloadInfo& info);
    Expected<bool, DownloadError> checkDiskSpace(const QString& path, qint64 requiredBytes);
    Expected<bool, DownloadError> checkResumeCapability(const QString& url, DownloadInfo& info);
    void updateDownloadProgress(const QString& downloadId, qint64 bytesReceived, qint64 bytesTotal);
    void completeDownload(const QString& downloadId);
    void failDownload(const QString& downloadId, DownloadError error, const QString& message);
    void cleanupDownload(const QString& downloadId);

    // Retry logic
    void scheduleRetry(const QString& downloadId);
    bool shouldRetry(const DownloadInfo& info, DownloadError error);
    int calculateRetryDelay(int retryCount);

    // File operations
    Expected<bool, DownloadError> moveToFinalLocation(const QString& tempPath, const QString& finalPath);
    Expected<bool, DownloadError> verifyChecksum(const QString& filePath, const QString& expectedChecksum);
    Expected<QString, DownloadError> calculateChecksum(const QString& filePath);

    // Network helpers
    QNetworkRequest buildRequest(const QString& url, const DownloadInfo& info);
    Expected<DownloadError, bool> mapNetworkError(QNetworkReply::NetworkError error);
    
    // Validation
    Expected<bool, DownloadError> validateUrl(const QString& url);
    Expected<bool, DownloadError> validateLocalPath(const QString& localPath);

};

} // namespace Murmur