#include "FileManagerController.hpp"
#include "../../core/common/Logger.hpp"
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QSettings>

namespace Murmur {

FileManagerController::FileManagerController(QObject* parent)
    : QObject(parent) {
    Logger::instance().info("FileManagerController created");
}

void FileManagerController::setFileManager(FileManager* fileManager) {
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
            
            emit pathsChanged();
        }
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
    
    setBusy(true);
    
    auto future = fileManager_->importVideo(sourcePath, destinationDir);
    auto watcher = new QFutureWatcher<Expected<QString, FileError>>(this);
    
    connect(watcher, &QFutureWatcher<Expected<QString, FileError>>::finished, [this, sourcePath, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        setBusy(false);
        
        if (result.hasValue()) {
            emit videoImported(sourcePath, result.value());
        } else {
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

} // namespace Murmur