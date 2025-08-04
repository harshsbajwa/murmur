#include "ModelDownloader.hpp"
#include "../common/Logger.hpp"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QStorageInfo>
#include <QtCore/QUuid>
#include <QtCore/QUrlQuery>
#include <QtCore/QThread>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkProxy>
#include <QtConcurrent>
#include <QCryptographicHash>
#include <QPromise>
#include <QFutureInterface>
#include <QEventLoop> 

namespace Murmur {

struct ModelDownloader::ModelDownloaderPrivate {
    QNetworkAccessManager* networkManager = nullptr;
    QHash<QString, DownloadInfo> activeDownloads;
    QHash<QNetworkReply*, QString> replyToDownloadId;
    QHash<QString, QTimer*> retryTimers;
    mutable QMutex downloadsMutex;
    
    // Configuration
    int maxConcurrentDownloads = 3;
    int timeoutSeconds = 300; // 5 minutes
    int maxRetries = 3;
    int retryDelaySeconds = 5;
    QString userAgent = "MurmurDesktop/1.0";
    int maxRedirects = 5;
    bool verifySSL = true;
    
    // Statistics
    qint64 totalBytesDownloaded = 0;
    QDateTime sessionStartTime;
    QHash<QString, qint64> downloadSizes;
    QHash<QString, double> downloadTimes;
};

ModelDownloader::ModelDownloader(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<ModelDownloaderPrivate>()) {
    
    d->networkManager = new QNetworkAccessManager(this);
    d->sessionStartTime = QDateTime::currentDateTime();
    
    d->networkManager->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    
    Logger::instance().info("ModelDownloader initialized");
}

ModelDownloader::~ModelDownloader() {
    cancelAllDownloads();
    
    // Clean up retry timers
    for (auto timer : d->retryTimers) {
        timer->stop();
        timer->deleteLater();
    }
    d->retryTimers.clear();
    
    Logger::instance().info("ModelDownloader destroyed");
}

Expected<QString, DownloadError> ModelDownloader::downloadFile(
    const QString& url,
    const QString& localPath,
    const QString& expectedChecksum,
    bool enableResume) {
    
    Q_UNUSED(enableResume);

    auto urlValidation = validateUrl(url);
    if (urlValidation.hasError()) {
        return makeUnexpected(urlValidation.error());
    }
    
    auto pathValidation = validateLocalPath(localPath);
    if (pathValidation.hasError()) {
        return makeUnexpected(pathValidation.error());
    }

    QString tempPath = localPath + ".tmp";
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "MurmurDesktop/1.0");

    std::unique_ptr<QNetworkReply> reply(d->networkManager->get(request));

    QEventLoop loop;
    QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeoutTimer.start(d->timeoutSeconds * 1000); // 5-minute timeout

    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        Logger::instance().error("Network timeout for URL: {}", url.toStdString());
        return makeUnexpected(DownloadError::TimeoutError);
    }

    if (reply->error() != QNetworkReply::NoError) {
        Logger::instance().error("Network error for URL {}: {}", url.toStdString(), reply->errorString().toStdString());
        return makeUnexpected(DownloadError::NetworkError);
    }
    
    QVariant redirectionTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    if (redirectionTarget.isValid()) {
        QUrl newUrl = reply->url().resolved(redirectionTarget.toUrl());
        Logger::instance().info("Redirecting download to: {}", newUrl.toString().toStdString());
        return downloadFile(newUrl.toString(), localPath, expectedChecksum, enableResume);
    }

    QFile file(tempPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return makeUnexpected(DownloadError::FileSystemError);
    }
    file.write(reply->readAll());
    file.close();

    if (!expectedChecksum.isEmpty()) {
        auto checksumResult = verifyChecksum(tempPath, expectedChecksum);
        if (checksumResult.hasError() || !checksumResult.value()) {
             return makeUnexpected(DownloadError::ChecksumMismatch);
        }
    }

    if (QFile::exists(localPath)) {
        if (!QFile::remove(localPath)) {
             return makeUnexpected(DownloadError::FileSystemError);
        }
    }
    if (!QFile::rename(tempPath, localPath)) {
        return makeUnexpected(DownloadError::FileSystemError);
    }
    
    return localPath;
}

void ModelDownloader::cancelDownload(const QString& downloadId) {
    QMutexLocker locker(&d->downloadsMutex);
    
    auto it = d->activeDownloads.find(downloadId);
    if (it != d->activeDownloads.end()) {
        it.value().isCancelled = true;
        
        // Find and abort the network reply
        for (auto replyIt = d->replyToDownloadId.begin(); replyIt != d->replyToDownloadId.end(); ++replyIt) {
            if (replyIt.value() == downloadId) {
                replyIt.key()->abort();
                break;
            }
        }
        
        // Cancel retry timer if exists
        auto timerIt = d->retryTimers.find(downloadId);
        if (timerIt != d->retryTimers.end()) {
            timerIt.value()->stop();
            timerIt.value()->deleteLater();
            d->retryTimers.erase(timerIt);
        }

        locker.unlock(); // Unlock before calling to avoid re-locking
        failDownload(downloadId, DownloadError::CancellationRequested, "Download cancelled by user.");
        Logger::instance().info("Download cancelled: {}", downloadId.toStdString());
    }
}

void ModelDownloader::cancelAllDownloads() {
    QMutexLocker locker(&d->downloadsMutex);
    QStringList downloadIds = d->activeDownloads.keys();
    locker.unlock(); // Unlock before iterating

    for (const QString& id : downloadIds) {
        cancelDownload(id);
    }
    
    Logger::instance().info("Cancelled {} active downloads", downloadIds.size());
}

DownloadInfo ModelDownloader::getDownloadInfo(const QString& downloadId) const {
    QMutexLocker locker(&d->downloadsMutex);
    return d->activeDownloads.value(downloadId);
}

QStringList ModelDownloader::getActiveDownloads() const {
    QMutexLocker locker(&d->downloadsMutex);
    return d->activeDownloads.keys();
}

bool ModelDownloader::isDownloadActive(const QString& downloadId) const {
    QMutexLocker locker(&d->downloadsMutex);
    return d->activeDownloads.contains(downloadId);
}

void ModelDownloader::startDownloadInternal(DownloadInfo info) {
    if (info.resumePosition > 0) {
        emit downloadResumed(info.id, info.resumePosition);
    }

    QNetworkRequest request = buildRequest(info.url, info);
    
    // Handle resume
    if (info.resumePosition > 0) {
        QString rangeHeader = QString("bytes=%1-").arg(info.resumePosition);
        request.setRawHeader("Range", rangeHeader.toUtf8());
    }
    
    Logger::instance().info("ModelDownloader: Starting download from URL: {}", info.url.toStdString());
    QNetworkReply* reply = d->networkManager->get(request);
    
    if (!reply) {
        Logger::instance().error("ModelDownloader: Failed to create network reply for URL: {}", info.url.toStdString());
        failDownload(info.id, DownloadError::NetworkError, "Failed to create network reply.");
        return;
    }
    
    // Store reply mapping
    d->replyToDownloadId[reply] = info.id;
    
    // Connect signals
    connect(reply, &QNetworkReply::downloadProgress,
            this, &ModelDownloader::onDownloadProgress);
    connect(reply, &QNetworkReply::finished,
            this, &ModelDownloader::onDownloadFinished);
    connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
            this, &ModelDownloader::onDownloadError);
    connect(reply, &QNetworkReply::sslErrors,
            this, &ModelDownloader::onSSLErrors);
    
    // Set timeout
    QTimer::singleShot(d->timeoutSeconds * 1000, reply, [reply]() {
        if (reply && reply->isRunning()) {
            Logger::instance().warn("Download timeout");
            reply->abort();
        }
    });
    
    emit downloadStarted(info.id, info.url);
    Logger::instance().info("Download started: {} -> {}", info.url.toStdString(), info.localPath.toStdString());
}

Expected<bool, DownloadError> ModelDownloader::prepareDownload(DownloadInfo& info) {
    Logger::instance().info("ModelDownloader: Preparing download for: {}", info.localPath.toStdString());
    
    // Create directory if needed
    QFileInfo fileInfo(info.localPath);
    QDir dir = fileInfo.dir();
    Logger::instance().info("ModelDownloader: Target directory: {}", dir.absolutePath().toStdString());
    Logger::instance().info("ModelDownloader: Directory exists: {}", dir.exists());
    
    if (!dir.exists()) {
        Logger::instance().info("ModelDownloader: Attempting to create directory: {}", dir.absolutePath().toStdString());
        if (!dir.mkpath(dir.absolutePath())) {
            Logger::instance().error("ModelDownloader: Failed to create download directory: {}", dir.absolutePath().toStdString());
            Logger::instance().error("ModelDownloader: Directory path: {}", dir.absolutePath().toStdString());
            Logger::instance().error("ModelDownloader: Directory exists after mkpath: {}", dir.exists());
            Logger::instance().error("ModelDownloader: Parent directory exists: {}", dir.absolutePath().isEmpty() ? false : QFileInfo(dir.absolutePath()).exists());
            return makeUnexpected(DownloadError::PermissionDenied);
        }
        Logger::instance().info("ModelDownloader: Created download directory: {}", dir.absolutePath().toStdString());
    }
    
    // Check disk space (estimate 2GB for large models)
    qint64 estimatedSize = 2LL * 1024 * 1024 * 1024; // 2GB
    auto spaceCheck = checkDiskSpace(dir.absolutePath(), estimatedSize);
    if (spaceCheck.hasError()) {
        return spaceCheck;
    }
    
    // Remove existing temp file if not resuming
    if (QFile::exists(info.tempPath) && !info.supportsResume) {
        if (!QFile::remove(info.tempPath)) {
            Logger::instance().warn("Failed to remove existing temp file: {}", info.tempPath.toStdString());
        }
    }
    
    return true;
}

Expected<bool, DownloadError> ModelDownloader::checkDiskSpace(const QString& path, qint64 requiredBytes) {
    QStorageInfo storage(path);
    if (!storage.isValid()) {
        return makeUnexpected(DownloadError::FileSystemError);
    }
    
    qint64 availableBytes = storage.bytesAvailable();
    if (availableBytes < requiredBytes) {
        Logger::instance().error("Insufficient disk space: need {} MB, have {} MB",
                                 requiredBytes / (1024 * 1024),
                                 availableBytes / (1024 * 1024));
        return makeUnexpected(DownloadError::InsufficientDiskSpace);
    }
    
    return true;
}

Expected<bool, DownloadError> ModelDownloader::checkResumeCapability(const QString& url, DownloadInfo& info) {
    // Use a local QNetworkAccessManager to avoid cross-thread issues with the main manager
    QNetworkAccessManager localManager;
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", d->userAgent.toUtf8());
    
    QNetworkReply* reply = localManager.head(request);
    
    if (!reply) {
        return false; // Assume no resume support
    }
    
    // Wait for reply (synchronous for simplicity as this is in a worker thread)
    QEventLoop loop;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit); // 10 second timeout
    loop.exec();
    
    bool supportsResume = false;
    if (reply->error() == QNetworkReply::NoError) {
        QString acceptRanges = reply->rawHeader("Accept-Ranges");
        supportsResume = acceptRanges.toLower() == "bytes";
        
        // Also get total file size
        bool ok;
        qint64 contentLength = reply->rawHeader("Content-Length").toLongLong(&ok);
        if (ok && contentLength > 0) {
            info.totalSize = contentLength;
        }
    }
    
    reply->deleteLater();
    return supportsResume;
}

void ModelDownloader::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    
    QString downloadId = d->replyToDownloadId.value(reply);
    if (downloadId.isEmpty()) return;
    
    updateDownloadProgress(downloadId, bytesReceived, bytesTotal);
}

void ModelDownloader::updateDownloadProgress(const QString& downloadId, qint64 bytesReceived, qint64 bytesTotal) {
    QMutexLocker locker(&d->downloadsMutex);
    
    auto it = d->activeDownloads.find(downloadId);
    if (it == d->activeDownloads.end()) return;
    
    DownloadInfo& info = it.value();
    if (info.isCancelled) return;
    
    // Update progress
    qint64 totalReceived = info.resumePosition + bytesReceived;
    qint64 totalSize = (bytesTotal > 0) ? (info.resumePosition + bytesTotal) : info.totalSize;
    
    info.downloadedSize = totalReceived;
    if (totalSize > 0) {
        info.percentage = (double)totalReceived / totalSize * 100.0;
        info.totalSize = totalSize;
    }
    
    // Calculate download speed
    if (info.timer.isValid()) {
        double elapsedSeconds = info.timer.elapsed() / 1000.0;
        if (elapsedSeconds > 0.1) { // Avoid division by zero
            info.downloadSpeed = bytesReceived / elapsedSeconds;
        }
    }
    
    info.status = "downloading";
    
    locker.unlock();
    
    emit downloadProgress(downloadId, totalReceived, totalSize, info.downloadSpeed);
}

void ModelDownloader::onDownloadFinished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    
    QString downloadId = d->replyToDownloadId.value(reply);
    if (downloadId.isEmpty()) {
        reply->deleteLater();
        return;
    }
    
    // Remove from tracking
    d->replyToDownloadId.remove(reply);
    
    QMutexLocker locker(&d->downloadsMutex);
    auto it = d->activeDownloads.find(downloadId);
    if (it == d->activeDownloads.end()) {
        reply->deleteLater();
        return;
    }
    
    DownloadInfo info = it.value();
    locker.unlock();
    
    if (info.isCancelled) {
        failDownload(downloadId, DownloadError::CancellationRequested, "Download cancelled");
        reply->deleteLater();
        cleanupDownload(downloadId);
        return;
    }
    
    if (reply->error() != QNetworkReply::NoError) {
        QString errorString = reply->errorString();
        auto netError = reply->error();
        reply->deleteLater();
        
        DownloadError downloadError = mapNetworkError(netError).valueOr(DownloadError::NetworkError);
        if (shouldRetry(info, downloadError)) {
            scheduleRetry(downloadId);
        } else {
            failDownload(downloadId, downloadError, errorString);
        }
        return;
    }
    
    // Check HTTP status code for redirects
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    Logger::instance().info("ModelDownloader: HTTP status: {}", httpStatus);
    
    // Handle redirects
    QVariant redirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
    Logger::instance().info("ModelDownloader: RedirectionTargetAttribute: {}", 
                          redirectUrl.isValid() ? redirectUrl.toString().toStdString() : "not set");
    
    // Check both the redirect attribute and HTTP status codes
    if ((redirectUrl.isValid() && !redirectUrl.toUrl().isEmpty()) || 
        (httpStatus >= 300 && httpStatus < 400)) {
        
        QUrl newUrl;
        if (redirectUrl.isValid() && !redirectUrl.toUrl().isEmpty()) {
            newUrl = redirectUrl.toUrl();
        } else {
            // Try to extract redirect URL from Location header
            QByteArray locationHeader = reply->rawHeader("Location");
            if (!locationHeader.isEmpty()) {
                newUrl = QUrl(QString::fromUtf8(locationHeader));
            } else {
                failDownload(downloadId, DownloadError::NetworkError, "Redirect detected but no location found");
                reply->deleteLater();
                return;
            }
        }
        
        if (newUrl.isRelative()) {
            newUrl = reply->url().resolved(newUrl);
        }
        
        // Prevent infinite redirect loops
        info.retryCount++;
        if (info.retryCount > d->maxRedirects) {
            failDownload(downloadId, DownloadError::NetworkError, "Too many redirects");
            reply->deleteLater();
            return;
        }
        
        Logger::instance().info("ModelDownloader: Following redirect to: {}", newUrl.toString().toStdString());
        
        // Update the download info with new URL and reset download position
        info.url = newUrl.toString();
        info.downloadedSize = 0;
        info.resumePosition = 0;
        
        // Update the active downloads
        QMutexLocker updateLocker(&d->downloadsMutex);
        d->activeDownloads[downloadId] = info;
        updateLocker.unlock();
        
        reply->deleteLater();
        
        // Start new download with redirected URL
        startDownloadInternal(info);
        return;
    }
    
    // Save downloaded data
    QByteArray data = reply->readAll();
    reply->deleteLater();
    
    QFile tempFile(info.tempPath);
    if (!tempFile.open(QIODevice::WriteOnly)) {
        failDownload(downloadId, DownloadError::FileSystemError, "Cannot open temp file for writing");
        return;
    }
    tempFile.write(data);
    tempFile.close();
    
    // Verify checksum
    if (!info.expectedChecksum.isEmpty()) {
        auto checksumResult = verifyChecksum(info.tempPath, info.expectedChecksum);
        if (checksumResult.hasError() || !checksumResult.value()) {
            failDownload(downloadId, DownloadError::ChecksumMismatch, "Checksum verification failed");
            return;
        }
    }
    
    // Move to final location
    auto moveResult = moveToFinalLocation(info.tempPath, info.localPath);
    if (moveResult.hasError()) {
        failDownload(downloadId, moveResult.error(), "Failed to move file to final location");
        return;
    }
    
    completeDownload(downloadId);
}

void ModelDownloader::completeDownload(const QString& downloadId) {
    QMutexLocker locker(&d->downloadsMutex);
    
    auto it = d->activeDownloads.find(downloadId);
    if (it == d->activeDownloads.end()) return;
    
    DownloadInfo info = it.value();
    
    // Update statistics
    d->totalBytesDownloaded += QFileInfo(info.localPath).size();
    
    locker.unlock();

    emit downloadCompleted(downloadId, info.localPath);
    cleanupDownload(downloadId);
}

void ModelDownloader::failDownload(const QString& downloadId, DownloadError error, const QString& message) {
    emit downloadFailed(downloadId, error, message);
    Logger::instance().error("Download failed: {} - {}", downloadId.toStdString(), message.toStdString());
    cleanupDownload(downloadId);
}

void ModelDownloader::cleanupDownload(const QString& downloadId) {
    QMutexLocker locker(&d->downloadsMutex);
    d->activeDownloads.remove(downloadId);
    
    // Clean up retry timer if exists
    auto timerIt = d->retryTimers.find(downloadId);
    if (timerIt != d->retryTimers.end()) {
        timerIt.value()->stop();
        timerIt.value()->deleteLater();
        d->retryTimers.erase(timerIt);
    }
}

// Helper method implementations

QString ModelDownloader::generateDownloadId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QNetworkRequest ModelDownloader::buildRequest(const QString& url, const DownloadInfo& info) {
    Q_UNUSED(info);
    QNetworkRequest request(url);
    
    request.setRawHeader("User-Agent", d->userAgent.toUtf8());
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    
    if (d->verifySSL) {
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
    }
    
    return request;
}

Expected<bool, DownloadError> ModelDownloader::verifyChecksum(const QString& filePath, const QString& expectedChecksum) {
    auto checksumResult = calculateChecksum(filePath);
    if (checksumResult.hasError()) {
        return makeUnexpected(checksumResult.error());
    }
    
    QString actualChecksum = checksumResult.value().toLower();
    QString expected = expectedChecksum.toLower();
    
    return actualChecksum == expected;
}

Expected<QString, DownloadError> ModelDownloader::calculateChecksum(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return makeUnexpected(DownloadError::FileSystemError);
    }
    
    QCryptographicHash hash(QCryptographicHash::Sha256);
    
    if (!hash.addData(&file)) {
        return makeUnexpected(DownloadError::FileSystemError);
    }
    
    return hash.result().toHex();
}

Expected<bool, DownloadError> ModelDownloader::moveToFinalLocation(const QString& tempPath, const QString& finalPath) {
    // Remove existing final file if it exists
    if (QFile::exists(finalPath)) {
        if (!QFile::remove(finalPath)) {
            return makeUnexpected(DownloadError::PermissionDenied);
        }
    }
    
    // Move temp file to final location
    if (!QFile::rename(tempPath, finalPath)) {
        // Fallback to copy and delete if rename fails (e.g., across filesystems)
        if (QFile::copy(tempPath, finalPath)) {
            QFile::remove(tempPath);
        } else {
            return makeUnexpected(DownloadError::FileSystemError);
        }
    }
    
    return true;
}

// Validation methods

Expected<bool, DownloadError> ModelDownloader::validateUrl(const QString& url) {
    QUrl qurl(url);
    if (!qurl.isValid() || qurl.scheme().isEmpty()) {
        return makeUnexpected(DownloadError::InvalidUrl);
    }
    
    if (qurl.scheme() != "http" && qurl.scheme() != "https") {
        return makeUnexpected(DownloadError::InvalidUrl);
    }
    
    return true;
}

Expected<bool, DownloadError> ModelDownloader::validateLocalPath(const QString& localPath) {
    QFileInfo fileInfo(localPath);
    QDir dir = fileInfo.dir();
    
    if (!dir.exists() && !dir.mkpath(dir.absolutePath())) {
        return makeUnexpected(DownloadError::PermissionDenied);
    }
    
    return true;
}

// Configuration methods

void ModelDownloader::setMaxConcurrentDownloads(int maxDownloads) {
    d->maxConcurrentDownloads = qMax(1, maxDownloads);
}

void ModelDownloader::setTimeout(int timeoutSeconds) {
    d->timeoutSeconds = qMax(30, timeoutSeconds);
}

void ModelDownloader::setRetryAttempts(int maxRetries) {
    d->maxRetries = qMax(0, maxRetries);
}

void ModelDownloader::setRetryDelay(int delaySeconds) {
    d->retryDelaySeconds = qMax(1, delaySeconds);
}

void ModelDownloader::setUserAgent(const QString& userAgent) {
    d->userAgent = userAgent;
}

void ModelDownloader::setMaxRedirects(int maxRedirects) {
    d->maxRedirects = qMax(0, maxRedirects);
}

void ModelDownloader::setVerifySSL(bool verify) {
    d->verifySSL = verify;
}

// Statistics methods

int ModelDownloader::getActiveDownloadCount() const {
    QMutexLocker locker(&d->downloadsMutex);
    return d->activeDownloads.size();
}

double ModelDownloader::getTotalDownloadSpeed() const {
    QMutexLocker locker(&d->downloadsMutex);
    
    double totalSpeed = 0.0;
    for (const auto& info : d->activeDownloads) {
        totalSpeed += info.downloadSpeed;
    }
    
    return totalSpeed;
}

qint64 ModelDownloader::getTotalBytesDownloaded() const {
    return d->totalBytesDownloaded;
}

QJsonObject ModelDownloader::getStatistics() const {
    QJsonObject stats;
    stats["totalBytesDownloaded"] = static_cast<qint64>(d->totalBytesDownloaded);
    stats["activeDownloads"] = getActiveDownloadCount();
    stats["totalDownloadSpeed"] = getTotalDownloadSpeed();
    stats["sessionStartTime"] = d->sessionStartTime.toString(Qt::ISODate);
    stats["completedDownloads"] = d->downloadSizes.size();
    
    return stats;
}

// Stub implementations

void ModelDownloader::onDownloadError(QNetworkReply::NetworkError error) {
    Q_UNUSED(error);
    // The main error handling is in onDownloadFinished to avoid race conditions
}

void ModelDownloader::onSSLErrors(const QList<QSslError>& errors) {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    
    if (!d->verifySSL) {
        reply->ignoreSslErrors();
        return;
    }
    
    QStringList errorStrings;
    for (const auto& error : errors) {
        errorStrings << error.errorString();
    }
    Logger::instance().error("SSL Errors: {}", errorStrings.join("; ").toStdString());
    // Let onDownloadFinished handle the failure
}

void ModelDownloader::onRetryTimer() {
    auto* timer = qobject_cast<QTimer*>(sender());
    if (!timer) return;
    
    // Find the download ID for this timer
    QString downloadId;
    for (auto it = d->retryTimers.begin(); it != d->retryTimers.end(); ++it) {
        if (it.value() == timer) {
            downloadId = it.key();
            break;
        }
    }
    
    if (downloadId.isEmpty()) {
        timer->deleteLater();
        return;
    }
    
    // Remove timer
    d->retryTimers.remove(downloadId);
    timer->deleteLater();
    
    // Restart download
    QMutexLocker locker(&d->downloadsMutex);
    auto it = d->activeDownloads.find(downloadId);
    if (it != d->activeDownloads.end()) {
        DownloadInfo info = it.value();
        info.retryCount++;
        it.value() = info; // Update retry count in map
        locker.unlock();
        
        Logger::instance().info("Retrying download (attempt {}): {}", info.retryCount, info.url.toStdString());
        
        startDownloadInternal(info);
    }
}

void ModelDownloader::scheduleRetry(const QString& downloadId) {
    QMutexLocker locker(&d->downloadsMutex);
    
    auto it = d->activeDownloads.find(downloadId);
    if (it == d->activeDownloads.end()) return;
    
    DownloadInfo& info = it.value();
    int delay = calculateRetryDelay(info.retryCount);
    
    auto* timer = new QTimer();
    timer->setSingleShot(true);
    timer->setInterval(delay * 1000);
    
    connect(timer, &QTimer::timeout, this, &ModelDownloader::onRetryTimer);
    connect(timer, &QTimer::timeout, timer, &QTimer::deleteLater);
    
    d->retryTimers[downloadId] = timer;
    timer->start();
    
    Logger::instance().info("Scheduling retry in {} seconds for: {}", delay, downloadId.toStdString());
}

bool ModelDownloader::shouldRetry(const DownloadInfo& info, DownloadError error) {
    if (info.isCancelled || info.retryCount >= info.maxRetries) {
        return false;
    }
    
    // Don't retry certain errors
    switch (error) {
        case DownloadError::ChecksumMismatch:
        case DownloadError::InsufficientDiskSpace:
        case DownloadError::PermissionDenied:
        case DownloadError::InvalidUrl:
        case DownloadError::CancellationRequested:
            return false;
        default:
            return true;
    }
}

int ModelDownloader::calculateRetryDelay(int retryCount) {
    // Exponential backoff with jitter
    int baseDelay = d->retryDelaySeconds;
    int delay = baseDelay * (1 << retryCount); // 2^retryCount
    
    // Add some jitter (±25%)
    int jitter = delay / 4;
    delay += (QRandomGenerator::global()->bounded(2 * jitter)) - jitter;
    
    // Cap at 5 minutes
    return qMin(delay, 300);
}

Expected<DownloadError, bool> ModelDownloader::mapNetworkError(QNetworkReply::NetworkError error) {
    switch (error) {
        case QNetworkReply::NoError:
            return makeUnexpected(false); // Should not happen...
        case QNetworkReply::TimeoutError:
            return DownloadError::TimeoutError;
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::NetworkSessionFailedError:
            return DownloadError::NetworkError;
        case QNetworkReply::ContentNotFoundError:
        case QNetworkReply::InternalServerError:
        case QNetworkReply::ServiceUnavailableError:
            return DownloadError::ServerError;
        default:
            return DownloadError::NetworkError;
    }
}

} // namespace Murmur