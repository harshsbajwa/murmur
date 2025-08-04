#include "FileManagerController.hpp"
#include "../../core/common/Logger.hpp"
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QSettings>
#include <QStorageInfo>
#include <QDateTime>
#include <QStandardPaths>
#include <QFileInfo>

namespace Murmur {

FileManagerController::FileManagerController(QObject* parent)
    : QObject(parent), ready_(false) {
    Logger::instance().info("FileManagerController created");
}

void FileManagerController::setReady(bool ready) {
    if (ready_ != ready) {
        ready_ = ready;
        Logger::instance().debug("FileManagerController ready state changed to: {}", ready ? "true" : "false");
        emit readyChanged();
    }
}

bool FileManagerController::isReady() const {
    return ready_;
}

void FileManagerController::updateReadyState() {
    bool isReadyNow = fileManager_ != nullptr;
    setReady(isReadyNow);
}

void FileManagerController::setFileManager(FileManager* fileManager) {
    bool wasReady = isReady();
    
    if (fileManager_ != fileManager) {
        if (fileManager_) {
            disconnect(fileManager_, nullptr, this, nullptr);
        }
        
        fileManager_ = fileManager;
        
        if (fileManager_) {
            connect(fileManager_, &FileManager::operationStarted,
                    this, &FileManagerController::onOperationStarted);
            connect(fileManager_, &FileManager::operationProgress,
                    this, &FileManagerController::onOperationProgress);
            connect(fileManager_, &FileManager::operationCompleted,
                    this, &FileManagerController::onOperationCompleted);
            connect(fileManager_, &FileManager::operationFailed,
                    this, &FileManagerController::onOperationFailed);
            
    // Initialize disk space values
    updateDiskSpace();
    
    // Initialize file model
    refreshFileModel();
    
    Logger::instance().info("FileManager connected successfully");
    emit pathsChanged();
} else {
    Logger::instance().warn("FileManager set to null");
}

updateReadyState();
    }
}

QString FileManagerController::defaultDownloadPath() const {
    return fileManager_ ? fileManager_->getDefaultDownloadPath() : QString();
}

QString FileManagerController::defaultExportPath() const {
    return fileManager_ ? fileManager_->getDefaultExportPath() : QString();
}

qreal FileManagerController::totalProgress() const {
    return totalProgress_;
}

int FileManagerController::activeOperationsCount() const {
    return operationProgress_.size();
}

bool FileManagerController::isBusy() const {
    return isBusy_;
}

qint64 FileManagerController::totalSpace() const {
    // Update disk space if it's not initialized or if it's been more than 5 seconds since last update
    if (totalSpace_ == -1 || lastDiskSpaceUpdate_.msecsTo(QDateTime::currentDateTime()) > 5000) {
        const_cast<FileManagerController*>(this)->updateDiskSpace();
    }
    return totalSpace_;
}

qint64 FileManagerController::usedSpace() const {
    // Update disk space if it's not initialized or if it's been more than 5 seconds since last update
    if (usedSpace_ == -1 || lastDiskSpaceUpdate_.msecsTo(QDateTime::currentDateTime()) > 5000) {
        const_cast<FileManagerController*>(this)->updateDiskSpace();
    }
    return usedSpace_;
}

QStringList FileManagerController::fileModel() const {
    return fileModel_;
}

void FileManagerController::analyzeDirectory(const QString& path) {
    if (!fileManager_) {
        emit fileError("analyzeDirectory", path, "FileManager not available");
        return;
    }
    
    setBusy(true);
    
    auto future = fileManager_->analyzeDirectory(path);
    auto watcher = new QFutureWatcher<Expected<DirectoryInfo, FileError>>(this);
    
    connect(watcher, &QFutureWatcher<Expected<DirectoryInfo, FileError>>::finished, [this, path, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        setBusy(false);
        
        if (result.hasValue()) {
            const DirectoryInfo& info = result.value();
            emit directoryAnalyzed(path, info.fileCount, info.dirCount, info.totalSize, info.videoFiles);
        } else {
            emit fileError("analyzeDirectory", path, translateFileError(result.error()));
        }
    });
    
    watcher->setFuture(future);
}

void FileManagerController::findVideoFiles(const QString& path, bool recursive) {
    if (!fileManager_) {
        emit fileError("findVideoFiles", path, "FileManager not available");
        return;
    }
    
    setBusy(true);
    
    auto future = fileManager_->findVideoFiles(path, recursive);
    auto watcher = new QFutureWatcher<Expected<QStringList, FileError>>(this);
    
    connect(watcher, &QFutureWatcher<Expected<QStringList, FileError>>::finished, [this, path, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        setBusy(false);
        
        if (result.hasValue()) {
            emit videoFilesFound(path, result.value());
        } else {
            emit fileError("findVideoFiles", path, translateFileError(result.error()));
        }
    });
    
    watcher->setFuture(future);
}

void FileManagerController::createDownloadDirectory(const QString& basePath, const QString& name) {
    if (!fileManager_) {
        emit fileError("createDownloadDirectory", basePath, "FileManager not available");
        return;
    }
    
    auto future = fileManager_->createDownloadDirectory(basePath, name);
    auto watcher = new QFutureWatcher<Expected<QString, FileError>>(this);
    
    connect(watcher, &QFutureWatcher<Expected<QString, FileError>>::finished, [this, basePath, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        if (result.hasValue()) {
            Logger::instance().info("Directory created: {}", result.value().toStdString());
        } else {
            emit fileError("createDownloadDirectory", basePath, translateFileError(result.error()));
        }
    });
    
    watcher->setFuture(future);
}

void FileManagerController::copyFile(const QString& source, const QString& destination) {
    if (!fileManager_) {
        emit fileError("copyFile", source, "FileManager not available");
        return;
    }
    
    auto future = fileManager_->copyFile(source, destination);
    auto watcher = new QFutureWatcher<Expected<QString, FileError>>(this);
    
    connect(watcher, &QFutureWatcher<Expected<QString, FileError>>::finished, [this, source, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        if (!result.hasValue()) {
            emit fileError("copyFile", source, translateFileError(result.error()));
        }
    });
    
    watcher->setFuture(future);
}

void FileManagerController::moveFile(const QString& source, const QString& destination) {
    if (!fileManager_) {
        emit fileError("moveFile", source, "FileManager not available");
        return;
    }
    
    auto future = fileManager_->moveFile(source, destination);
    auto watcher = new QFutureWatcher<Expected<QString, FileError>>(this);
    
    connect(watcher, &QFutureWatcher<Expected<QString, FileError>>::finished, [this, source, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        if (!result.hasValue()) {
            emit fileError("moveFile", source, translateFileError(result.error()));
        }
    });
    
    watcher->setFuture(future);
}

void FileManagerController::deleteFile(const QString& path) {
    if (!fileManager_) {
        emit fileError("deleteFile", path, "FileManager not available");
        return;
    }
    
    auto future = fileManager_->deleteFile(path);
    auto watcher = new QFutureWatcher<Expected<bool, FileError>>(this);
    
    connect(watcher, &QFutureWatcher<Expected<bool, FileError>>::finished, [this, path, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        if (!result.hasValue()) {
            emit fileError("deleteFile", path, translateFileError(result.error()));
        }
    });
    
    watcher->setFuture(future);
}

void FileManagerController::deleteDirectory(const QString& path, bool recursive) {
    if (!fileManager_) {
        emit fileError("deleteDirectory", path, "FileManager not available");
        return;
    }
    
    auto future = fileManager_->deleteDirectory(path, recursive);
    auto watcher = new QFutureWatcher<Expected<bool, FileError>>(this);
    
    connect(watcher, &QFutureWatcher<Expected<bool, FileError>>::finished, [this, path, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        if (!result.hasValue()) {
            emit fileError("deleteDirectory", path, translateFileError(result.error()));
        }
    });
    
    watcher->setFuture(future);
}

void FileManagerController::importVideo(const QString& sourcePath, const QString& destinationDir) {
    if (!fileManager_) {
        emit fileError("importVideo", sourcePath, "FileManager not available");
        return;
    }
    
    Logger::instance().info("Importing video: {} to {}", sourcePath.toStdString(), 
                           destinationDir.isEmpty() ? "default directory" : destinationDir.toStdString());
    
    // Validate source path
    if (sourcePath.isEmpty()) {
        emit fileError("importVideo", sourcePath, "Source path is empty");
        return;
    }
    
    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists()) {
        emit fileError("importVideo", sourcePath, "Source file does not exist");
        return;
    }
    
    if (!isVideoFile(sourcePath)) {
        emit fileError("importVideo", sourcePath, "File is not a valid video format");
        return;
    }
    
    setBusy(true);
    
    auto future = fileManager_->importVideo(sourcePath, destinationDir);
    auto watcher = new QFutureWatcher<Expected<QString, FileError>>(this);
    
    connect(watcher, &QFutureWatcher<Expected<QString, FileError>>::finished, [this, sourcePath, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        setBusy(false);
        
        if (result.hasValue()) {
            Logger::instance().info("Video imported successfully: {}", result.value().toStdString());
            emit videoImported(sourcePath, result.value());
        } else {
            Logger::instance().error("Video import failed: {}", translateFileError(result.error()).toStdString());
            emit fileError("importVideo", sourcePath, translateFileError(result.error()));
        }
    });
    
    watcher->setFuture(future);
}

void FileManagerController::importVideoDirectory(const QString& sourcePath, const QString& destinationDir) {
    if (!fileManager_) {
        emit fileError("importVideoDirectory", sourcePath, "FileManager not available");
        return;
    }
    
    setBusy(true);
    
    auto future = fileManager_->importVideoDirectory(sourcePath, destinationDir);
    auto watcher = new QFutureWatcher<Expected<QStringList, FileError>>(this);
    
    connect(watcher, &QFutureWatcher<Expected<QStringList, FileError>>::finished, [this, sourcePath, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        setBusy(false);
        
        if (result.hasValue()) {
            emit videosImported(result.value());
        } else {
            emit fileError("importVideoDirectory", sourcePath, translateFileError(result.error()));
        }
    });
    
    watcher->setFuture(future);
}

void FileManagerController::exportVideo(const QString& sourcePath, const QString& destinationPath) {
    if (!fileManager_) {
        emit fileError("exportVideo", sourcePath, "FileManager not available");
        return;
    }
    
    setBusy(true);
    
    auto future = fileManager_->exportVideo(sourcePath, destinationPath);
    auto watcher = new QFutureWatcher<Expected<QString, FileError>>(this);
    
    connect(watcher, &QFutureWatcher<Expected<QString, FileError>>::finished, [this, sourcePath, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        setBusy(false);
        
        if (result.hasValue()) {
            emit videoExported(sourcePath, result.value());
        } else {
            emit fileError("exportVideo", sourcePath, translateFileError(result.error()));
        }
    });
    
    watcher->setFuture(future);
}

void FileManagerController::importVideosFromUrls(const QStringList& urls, const QString& destinationDir) {
    QStringList localPaths;
    
    for (const QString& urlString : urls) {
        QUrl url(urlString);
        if (url.isLocalFile()) {
            localPaths.append(url.toLocalFile());
        }
    }
    
    if (localPaths.isEmpty()) {
        emit fileError("importVideosFromUrls", "", "No valid local file URLs provided");
        return;
    }
    
    // Import each file individually
    for (const QString& path : localPaths) {
        importVideo(path, destinationDir);
    }
}

void FileManagerController::exportTranscription(const QString& transcriptionData, const QString& format, const QString& outputPath) {
    if (!fileManager_) {
        emit fileError("exportTranscription", outputPath, "FileManager not available");
        return;
    }
    
    auto future = fileManager_->exportTranscription(transcriptionData, format, outputPath);
    auto watcher = new QFutureWatcher<Expected<QString, FileError>>(this);
    
    connect(watcher, &QFutureWatcher<Expected<QString, FileError>>::finished, [this, format, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        if (result.hasValue()) {
            emit transcriptionExported(result.value(), format);
        } else {
            emit fileError("exportTranscription", "", translateFileError(result.error()));
        }
    });
    
    watcher->setFuture(future);
}

void FileManagerController::importTranscription(const QString& filePath) {
    if (!fileManager_) {
        emit fileError("importTranscription", filePath, "FileManager not available");
        return;
    }
    
    auto future = fileManager_->importTranscription(filePath);
    auto watcher = new QFutureWatcher<Expected<QString, FileError>>(this);
    
    connect(watcher, &QFutureWatcher<Expected<QString, FileError>>::finished, [this, filePath, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        if (result.hasValue()) {
            emit transcriptionImported(filePath, result.value());
        } else {
            emit fileError("importTranscription", filePath, translateFileError(result.error()));
        }
    });
    
    watcher->setFuture(future);
}

qint64 FileManagerController::getAvailableSpace(const QString& path) {
    return fileManager_ ? fileManager_->getAvailableSpace(path) : 0;
}

qint64 FileManagerController::getFileSize(const QString& path) {
    return fileManager_ ? fileManager_->getFileSize(path) : 0;
}

bool FileManagerController::isVideoFile(const QString& path) {
    return fileManager_ ? fileManager_->isVideoFile(path) : false;
}

bool FileManagerController::isAudioFile(const QString& path) {
    return fileManager_ ? fileManager_->isAudioFile(path) : false;
}

bool FileManagerController::isSubtitleFile(const QString& path) {
    return fileManager_ ? fileManager_->isSubtitleFile(path) : false;
}

QString FileManagerController::generateUniqueFileName(const QString& basePath, const QString& fileName) {
    return fileManager_ ? fileManager_->generateUniqueFileName(basePath, fileName) : fileName;
}

void FileManagerController::cancelOperation(const QString& operationId) {
    if (fileManager_) {
        fileManager_->cancelOperation(operationId);
    }
}

void FileManagerController::cancelAllOperations() {
    if (fileManager_) {
        fileManager_->cancelAllOperations();
    }
    
    operationProgress_.clear();
    calculateTotalProgress();
    emit operationsChanged();
}

QStringList FileManagerController::getActiveOperationIds() {
    return operationProgress_.keys();
}

void FileManagerController::setDefaultDownloadPath(const QString& path) {
    if (!path.isEmpty() && QDir(path).exists()) {
        // Save to application settings
        QSettings settings;
        settings.setValue("FileManager/DefaultDownloadPath", path);
        
        // Update FileManager configuration if available
        if (fileManager_) {
            // Implementation depends on FileManager interface
            // fileManager_->setDefaultDownloadPath(path);
        }
        
        Logger::instance().info("Default download path set to: {}", path.toStdString());
        emit pathsChanged();
    } else {
        Logger::instance().warn("Invalid download path: {}", path.toStdString());
    }
}

void FileManagerController::setDefaultExportPath(const QString& path) {
    if (!path.isEmpty() && QDir(path).exists()) {
        // Save to application settings
        QSettings settings;
        settings.setValue("FileManager/DefaultExportPath", path);
        
        // Update FileManager configuration if available
        if (fileManager_) {
            // Implementation depends on FileManager interface
            // fileManager_->setDefaultExportPath(path);
        }
        
        Logger::instance().info("Default export path set to: {}", path.toStdString());
        emit pathsChanged();
    } else {
        Logger::instance().warn("Invalid export path: {}", path.toStdString());
    }
}

void FileManagerController::openInFileManager(const QString& path) {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(path).absolutePath()));
}

void FileManagerController::onOperationStarted(const QString& operationId, const QString& type, const QString& source, const QString& destination) {
    setBusy(true);
    emit operationStarted(operationId, type, source, destination);
}

void FileManagerController::onOperationProgress(const QString& operationId, qint64 processed, qint64 total) {
    operationProgress_[operationId] = qMakePair(processed, total);
    calculateTotalProgress();
    
    qreal progress = total > 0 ? static_cast<qreal>(processed) / total : 0.0;
    emit operationProgress(operationId, progress);
}

void FileManagerController::onOperationCompleted(const QString& operationId, const QString& result) {
    operationProgress_.remove(operationId);
    calculateTotalProgress();
    
    if (operationProgress_.isEmpty()) {
        setBusy(false);
    }
    
    emit operationCompleted(operationId, result);
    emit operationsChanged();
}

void FileManagerController::onOperationFailed(const QString& operationId, FileError error, const QString& errorMessage) {
    operationProgress_.remove(operationId);
    calculateTotalProgress();
    
    if (operationProgress_.isEmpty()) {
        setBusy(false);
    }
    
    emit operationFailed(operationId, translateFileError(error) + ": " + errorMessage);
    emit operationsChanged();
}

void FileManagerController::updateProgress() {
    calculateTotalProgress();
}

QString FileManagerController::translateFileError(FileError error) const {
    switch (error) {
        case FileError::InvalidPath: return tr("Invalid path");
        case FileError::PermissionDenied: return tr("Permission denied");
        case FileError::NotFound: return tr("File not found");
        case FileError::AlreadyExists: return tr("File already exists");
        case FileError::InsufficientSpace: return tr("Insufficient disk space");
        case FileError::CopyFailed: return tr("Copy operation failed");
        case FileError::MoveFailed: return tr("Move operation failed");
        case FileError::DeleteFailed: return tr("Delete operation failed");
        case FileError::CreateFailed: return tr("Create operation failed");
        default: return tr("Unknown error");
    }
}

void FileManagerController::setBusy(bool busy) {
    if (isBusy_ != busy) {
        isBusy_ = busy;
        emit busyChanged();
    }
}

void FileManagerController::calculateTotalProgress() {
    if (operationProgress_.isEmpty()) {
        totalProgress_ = 0.0;
    } else {
        qint64 totalProcessed = 0;
        qint64 totalSize = 0;
        
        for (auto it = operationProgress_.constBegin(); it != operationProgress_.constEnd(); ++it) {
            totalProcessed += it.value().first;
            totalSize += it.value().second;
        }
        
        totalProgress_ = totalSize > 0 ? static_cast<qreal>(totalProcessed) / totalSize : 0.0;
    }
    
    emit progressChanged();
}

void FileManagerController::updateDiskSpace() {
    if (!fileManager_) {
        totalSpace_ = 0;
        usedSpace_ = 0;
        lastDiskSpaceUpdate_ = QDateTime::currentDateTime();
        emit diskSpaceChanged();
        return;
    }
    
    QString downloadPath = fileManager_->getDefaultDownloadPath();
    if (downloadPath.isEmpty()) {
        downloadPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    }
    
    // Ensure the path exists
    QDir dir(downloadPath);
    if (!dir.exists()) {
        // Try to create it
        if (!dir.mkpath(".")) {
            Logger::instance().warn("Failed to create download directory: {}", downloadPath.toStdString());
            totalSpace_ = 0;
            usedSpace_ = 0;
            lastDiskSpaceUpdate_ = QDateTime::currentDateTime();
            emit diskSpaceChanged();
            return;
        }
    }
    
    QStorageInfo storage(downloadPath);
    if (storage.isValid() && storage.isReady()) {
        totalSpace_ = storage.bytesTotal();
        usedSpace_ = totalSpace_ - storage.bytesAvailable();
    } else {
        // Fallback to getting storage info for the root path
        QStorageInfo rootStorage(QDir::rootPath());
        if (rootStorage.isValid() && rootStorage.isReady()) {
            totalSpace_ = rootStorage.bytesTotal();
            usedSpace_ = totalSpace_ - rootStorage.bytesAvailable();
        } else {
            totalSpace_ = 0;
            usedSpace_ = 0;
        }
    }
    
    lastDiskSpaceUpdate_ = QDateTime::currentDateTime();
    emit diskSpaceChanged();
}

void FileManagerController::refreshFileModel() {
    if (!fileManager_) {
        Logger::instance().warn("FileManager not available for file model refresh");
        return;
    }
    
    fileModel_.clear();
    
    // Get the download directory
    QString downloadPath = fileManager_->getDefaultDownloadPath();
    if (downloadPath.isEmpty()) {
        downloadPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/Murmur";
    }
    
    // Scan for video files in the download directory
    QDir dir(downloadPath);
    if (!dir.exists()) {
        Logger::instance().info("Download directory doesn't exist: {}", downloadPath.toStdString());
        emit fileModelChanged();
        return;
    }
    
    // Set up file filters for common video formats
    QStringList videoExtensions;
    videoExtensions << "*.mp4" << "*.avi" << "*.mkv" << "*.mov" << "*.wmv" 
                   << "*.flv" << "*.webm" << "*.m4v" << "*.3gp" << "*.ogv";
    
    dir.setNameFilters(videoExtensions);
    dir.setFilter(QDir::Files | QDir::Readable);
    dir.setSorting(QDir::Time | QDir::Reversed); // Most recent first
    
    QFileInfoList fileList = dir.entryInfoList();
    
    for (const QFileInfo& fileInfo : fileList) {
        // Add full file path to the model
        fileModel_.append(fileInfo.absoluteFilePath());
    }
    
    Logger::instance().info("File model refreshed with {} video files", fileModel_.size());
    emit fileModelChanged();
}

void FileManagerController::removeFile(const QString& filePath) {
    if (!fileManager_) {
        Logger::instance().error("FileManager not available");
        return;
    }
    
    Logger::instance().info("Removing file from model: {}", filePath.toStdString());
    
    int index = fileModel_.indexOf(filePath);
    if (index >= 0) {
        fileModel_.removeAt(index);
        Logger::instance().info("File removed from model. New size: {}", fileModel_.size());
        emit fileModelChanged();
    } else {
        Logger::instance().warn("File not found in model: {}", filePath.toStdString());
    }
}

} // namespace Murmur