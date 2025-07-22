#include "FileManager.hpp"
#include "../common/Logger.hpp"
#include <QtConcurrent>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QTimer>
#include <QMutex>
#include <QMutexLocker>

namespace Murmur {

struct FileManager::FileManagerPrivate {
    mutable QMutex operationsMutex;
    QHash<QString, FileOperation> activeOperations;
    QStringList videoExtensions = {"mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "mpg", "mpeg", "3gp", "ogv"};
    QStringList audioExtensions = {"mp3", "wav", "flac", "aac", "ogg", "wma", "m4a"};
    QStringList subtitleExtensions = {"srt", "vtt", "ass", "ssa", "sub", "sbv"};
    
    QString defaultDownloadPath;
    QString defaultExportPath;
    QString appDataPath;
    QString cachePath;
    QString configPath;
};

FileManager::FileManager(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<FileManagerPrivate>()) {
    Logger::instance().info("FileManager created");
    initializePaths();
}

FileManager::~FileManager() {
    cancelAllOperations();
}

QFuture<Expected<DirectoryInfo, FileError>> FileManager::analyzeDirectory(const QString& path) {
    return QtConcurrent::run([this, path]() {
        return analyzeDirectorySync(path);
    });
}

QFuture<Expected<QStringList, FileError>> FileManager::findVideoFiles(const QString& path, bool recursive) {
    return QtConcurrent::run([this, path, recursive]() {
        return findVideoFilesSync(path, recursive);
    });
}

QFuture<Expected<QString, FileError>> FileManager::createDownloadDirectory(const QString& basePath, const QString& name) {
    return QtConcurrent::run([this, basePath, name]() -> Expected<QString, FileError> {
        QString cleanName = name;
        // Remove invalid characters
        cleanName.replace(QRegularExpression("[<>:\"/\\\\|?*]"), "_");
        
        QString fullPath = QDir(basePath).absoluteFilePath(cleanName);
        
        if (!ensureDirectoryExists(fullPath)) {
            return makeUnexpected(FileError::CreateFailed);
        }
        
        return fullPath;
    });
}

QFuture<Expected<QString, FileError>> FileManager::copyFile(const QString& source, const QString& destination) {
    QString operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    return QtConcurrent::run([this, source, destination, operationId]() {
        return copyFileSync(source, destination, operationId);
    });
}

QFuture<Expected<QString, FileError>> FileManager::moveFile(const QString& source, const QString& destination) {
    QString operationId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    
    return QtConcurrent::run([this, source, destination, operationId]() {
        return moveFileSync(source, destination, operationId);
    });
}

QFuture<Expected<bool, FileError>> FileManager::deleteFile(const QString& path) {
    return QtConcurrent::run([this, path]() -> Expected<bool, FileError> {
        QFileInfo fileInfo(path);
        if (!fileInfo.exists()) {
            return makeUnexpected(FileError::NotFound);
        }
        
        if (!QFile::remove(path)) {
            return makeUnexpected(FileError::DeleteFailed);
        }
        
        return true;
    });
}

QFuture<Expected<bool, FileError>> FileManager::deleteDirectory(const QString& path, bool recursive) {
    return QtConcurrent::run([this, path, recursive]() -> Expected<bool, FileError> {
        QDir dir(path);
        if (!dir.exists()) {
            return makeUnexpected(FileError::NotFound);
        }
        
        if (recursive) {
            if (!dir.removeRecursively()) {
                return makeUnexpected(FileError::DeleteFailed);
            }
        } else {
            if (!dir.rmdir(path)) {
                return makeUnexpected(FileError::DeleteFailed);
            }
        }
        
        return true;
    });
}

QFuture<Expected<QString, FileError>> FileManager::importVideo(const QString& sourcePath, const QString& destinationDir) {
    return QtConcurrent::run([this, sourcePath, destinationDir]() -> Expected<QString, FileError> {
        QFileInfo sourceInfo(sourcePath);
        if (!sourceInfo.exists() || !isVideoFile(sourcePath)) {
            return makeUnexpected(FileError::NotFound);
        }
        
        QString destDir = destinationDir.isEmpty() ? getDefaultDownloadPath() : destinationDir;
        if (!ensureDirectoryExists(destDir)) {
            return makeUnexpected(FileError::CreateFailed);
        }
        
        QString destPath = generateUniqueFileName(destDir, sourceInfo.fileName());
        
        auto copyResult = copyFileSync(sourcePath, destPath, QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (!copyResult.hasValue()) {
            return makeUnexpected(copyResult.error());
        }
        
        return destPath;
    });
}

QFuture<Expected<QStringList, FileError>> FileManager::importVideoDirectory(const QString& sourcePath, const QString& destinationDir) {
    return QtConcurrent::run([this, sourcePath, destinationDir]() -> Expected<QStringList, FileError> {
        auto videoFilesResult = findVideoFilesSync(sourcePath, true);
        if (!videoFilesResult.hasValue()) {
            return makeUnexpected(videoFilesResult.error());
        }
        
        QStringList importedFiles;
        QString destDir = destinationDir.isEmpty() ? getDefaultDownloadPath() : destinationDir;
        
        for (const QString& videoFile : videoFilesResult.value()) {
            auto importResult = copyFileSync(videoFile, 
                generateUniqueFileName(destDir, QFileInfo(videoFile).fileName()),
                QUuid::createUuid().toString(QUuid::WithoutBraces));
            
            if (importResult.hasValue()) {
                importedFiles.append(importResult.value());
            }
        }
        
        return importedFiles;
    });
}

QFuture<Expected<QString, FileError>> FileManager::exportVideo(const QString& sourcePath, const QString& destinationPath) {
    return copyFile(sourcePath, destinationPath);
}

QFuture<Expected<QString, FileError>> FileManager::exportTranscription(const QString& transcriptionData, 
                                                                       const QString& format, 
                                                                       const QString& outputPath) {
    return QtConcurrent::run([this, transcriptionData, format, outputPath]() -> Expected<QString, FileError> {
        QString content;
        
        if (format.toLower() == "srt") {
            content = formatTranscriptionToSRT(transcriptionData);
        } else if (format.toLower() == "vtt") {
            content = formatTranscriptionToVTT(transcriptionData);
        } else if (format.toLower() == "txt") {
            content = formatTranscriptionToTXT(transcriptionData);
        } else if (format.toLower() == "json") {
            content = transcriptionData; // Assume already in JSON format
        } else {
            return makeUnexpected(FileError::InvalidPath);
        }
        
        QFile file(outputPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return makeUnexpected(FileError::CreateFailed);
        }
        
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream << content;
        
        return outputPath;
    });
}

QFuture<Expected<QString, FileError>> FileManager::importTranscription(const QString& filePath) {
    return QtConcurrent::run([this, filePath]() -> Expected<QString, FileError> {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return makeUnexpected(FileError::NotFound);
        }
        
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        QString content = stream.readAll();
        
        return content;
    });
}

QString FileManager::getDefaultDownloadPath() const {
    return d->defaultDownloadPath;
}

QString FileManager::getDefaultExportPath() const {
    return d->defaultExportPath;
}

QString FileManager::getAppDataPath() const {
    return d->appDataPath;
}

QString FileManager::getCachePath() const {
    return d->cachePath;
}

QString FileManager::getConfigPath() const {
    return d->configPath;
}

qint64 FileManager::getAvailableSpace(const QString& path) const {
    QStorageInfo storage(path);
    return storage.bytesAvailable();
}

qint64 FileManager::getFileSize(const QString& path) const {
    QFileInfo info(path);
    return info.size();
}

bool FileManager::isVideoFile(const QString& path) const {
    QFileInfo info(path);
    return d->videoExtensions.contains(info.suffix().toLower());
}

bool FileManager::isAudioFile(const QString& path) const {
    QFileInfo info(path);
    return d->audioExtensions.contains(info.suffix().toLower());
}

bool FileManager::isSubtitleFile(const QString& path) const {
    QFileInfo info(path);
    return d->subtitleExtensions.contains(info.suffix().toLower());
}

QString FileManager::generateUniqueFileName(const QString& basePath, const QString& fileName) const {
    QDir dir(basePath);
    QFileInfo info(fileName);
    QString baseName = info.completeBaseName();
    QString extension = info.suffix();
    
    QString uniqueName = fileName;
    int counter = 1;
    
    while (dir.exists(uniqueName)) {
        uniqueName = QString("%1 (%2).%3").arg(baseName).arg(counter).arg(extension);
        counter++;
    }
    
    return dir.absoluteFilePath(uniqueName);
}

void FileManager::cancelOperation(const QString& operationId) {
    QMutexLocker locker(&d->operationsMutex);
    if (d->activeOperations.contains(operationId)) {
        d->activeOperations[operationId].cancelled = true;
    }
}

void FileManager::cancelAllOperations() {
    QMutexLocker locker(&d->operationsMutex);
    for (auto& operation : d->activeOperations) {
        operation.cancelled = true;
    }
}

QList<FileOperation> FileManager::getActiveOperations() const {
    QMutexLocker locker(&d->operationsMutex);
    return d->activeOperations.values();
}

void FileManager::onFileOperationProgress(const QString& operationId, qint64 processed, qint64 total) {
    {
        QMutexLocker locker(&d->operationsMutex);
        if (d->activeOperations.contains(operationId)) {
            d->activeOperations[operationId].processedSize = processed;
            d->activeOperations[operationId].totalSize = total;
        }
    }
    
    emit operationProgress(operationId, processed, total);
}

Expected<DirectoryInfo, FileError> FileManager::analyzeDirectorySync(const QString& path) {
    QDir dir(path);
    if (!dir.exists()) {
        return makeUnexpected(FileError::NotFound);
    }
    
    DirectoryInfo info;
    info.path = path;
    
    QDirIterator iterator(path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    
    while (iterator.hasNext()) {
        QString filePath = iterator.next();
        QFileInfo fileInfo(filePath);
        
        if (fileInfo.isFile()) {
            info.fileCount++;
            info.totalSize += fileInfo.size();
            
            if (isVideoFile(filePath)) {
                info.videoFiles.append(filePath);
            } else if (isAudioFile(filePath)) {
                info.audioFiles.append(filePath);
            } else if (isSubtitleFile(filePath)) {
                info.subtitleFiles.append(filePath);
            }
        } else if (fileInfo.isDir()) {
            info.dirCount++;
        }
    }
    
    return info;
}

Expected<QStringList, FileError> FileManager::findVideoFilesSync(const QString& path, bool recursive) {
    QDir dir(path);
    if (!dir.exists()) {
        return makeUnexpected(FileError::NotFound);
    }
    
    QStringList videoFiles;
    QStringList nameFilters;
    
    for (const QString& ext : d->videoExtensions) {
        nameFilters << QString("*.%1").arg(ext);
    }
    
    QDirIterator::IteratorFlag flags = recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
    QDirIterator iterator(path, nameFilters, QDir::Files, flags);
    
    while (iterator.hasNext()) {
        videoFiles.append(iterator.next());
    }
    
    return videoFiles;
}

Expected<QString, FileError> FileManager::copyFileSync(const QString& source, const QString& destination, const QString& operationId) {
    QFileInfo sourceInfo(source);
    if (!sourceInfo.exists()) {
        return makeUnexpected(FileError::NotFound);
    }
    
    // Register operation
    {
        QMutexLocker locker(&d->operationsMutex);
        FileOperation operation;
        operation.id = operationId;
        operation.source = source;
        operation.destination = destination;
        operation.type = "copy";
        operation.totalSize = sourceInfo.size();
        d->activeOperations[operationId] = operation;
    }
    
    emit operationStarted(operationId, "copy", source, destination);
    
    QFile sourceFile(source);
    QFile destFile(destination);
    
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        emit operationFailed(operationId, FileError::PermissionDenied, "Cannot read source file");
        return makeUnexpected(FileError::PermissionDenied);
    }
    
    if (!destFile.open(QIODevice::WriteOnly)) {
        emit operationFailed(operationId, FileError::PermissionDenied, "Cannot write destination file");
        return makeUnexpected(FileError::PermissionDenied);
    }
    
    const qint64 bufferSize = 64 * 1024; // 64KB buffer
    qint64 totalProcessed = 0;
    qint64 totalSize = sourceInfo.size();
    
    while (!sourceFile.atEnd()) {
        // Check for cancellation
        {
            QMutexLocker locker(&d->operationsMutex);
            if (d->activeOperations.contains(operationId) && d->activeOperations[operationId].cancelled) {
                destFile.remove();
                return makeUnexpected(FileError::Unknown); // Cancelled
            }
        }
        
        QByteArray data = sourceFile.read(bufferSize);
        if (data.isEmpty()) {
            break;
        }
        
        qint64 written = destFile.write(data);
        if (written != data.size()) {
            destFile.remove();
            emit operationFailed(operationId, FileError::CopyFailed, "Write failed");
            return makeUnexpected(FileError::CopyFailed);
        }
        
        totalProcessed += written;
        onFileOperationProgress(operationId, totalProcessed, totalSize);
    }
    
    // Mark operation as completed
    {
        QMutexLocker locker(&d->operationsMutex);
        if (d->activeOperations.contains(operationId)) {
            d->activeOperations[operationId].completed = true;
            d->activeOperations.remove(operationId);
        }
    }
    
    emit operationCompleted(operationId, destination);
    return destination;
}

Expected<QString, FileError> FileManager::moveFileSync(const QString& source, const QString& destination, const QString& operationId) {
    // Try quick rename first
    if (QFile::rename(source, destination)) {
        emit operationCompleted(operationId, destination);
        return destination;
    }
    
    // Fall back to copy + delete
    auto copyResult = copyFileSync(source, destination, operationId);
    if (!copyResult.hasValue()) {
        return copyResult;
    }
    
    if (!QFile::remove(source)) {
        // Copy succeeded but delete failed - log warning but don't fail
        Logger::instance().warn("Move operation: failed to delete source file after copy");
    }
    
    return destination;
}

QString FileManager::formatTranscriptionToSRT(const QString& transcriptionData) const {
    // Parse JSON transcription data and convert to SRT format
    QJsonDocument doc = QJsonDocument::fromJson(transcriptionData.toUtf8());
    if (!doc.isObject()) {
        return transcriptionData; // Return as-is if not JSON
    }
    
    QJsonObject obj = doc.object();
    QJsonArray segments = obj["segments"].toArray();
    
    QString srt;
    int index = 1;
    
    for (const QJsonValue& segmentValue : segments) {
        QJsonObject segment = segmentValue.toObject();
        
        double start = segment["start"].toDouble();
        double end = segment["end"].toDouble();
        QString text = segment["text"].toString().trimmed();
        
        if (text.isEmpty()) continue;
        
        // Format timestamps (HH:MM:SS,mmm)
        auto formatTime = [](double seconds) {
            int hours = static_cast<int>(seconds) / 3600;
            int minutes = (static_cast<int>(seconds) % 3600) / 60;
            int secs = static_cast<int>(seconds) % 60;
            int ms = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);
            return QString("%1:%2:%3,%4")
                   .arg(hours, 2, 10, QChar('0'))
                   .arg(minutes, 2, 10, QChar('0'))
                   .arg(secs, 2, 10, QChar('0'))
                   .arg(ms, 3, 10, QChar('0'));
        };
        
        srt += QString("%1\n%2 --> %3\n%4\n\n")
               .arg(index++)
               .arg(formatTime(start))
               .arg(formatTime(end))
               .arg(text);
    }
    
    return srt;
}

QString FileManager::formatTranscriptionToVTT(const QString& transcriptionData) const {
    QString vtt = "WEBVTT\n\n";
    
    // Parse JSON and convert to VTT format
    QJsonDocument doc = QJsonDocument::fromJson(transcriptionData.toUtf8());
    if (!doc.isObject()) {
        return transcriptionData;
    }
    
    QJsonObject obj = doc.object();
    QJsonArray segments = obj["segments"].toArray();
    
    for (const QJsonValue& segmentValue : segments) {
        QJsonObject segment = segmentValue.toObject();
        
        double start = segment["start"].toDouble();
        double end = segment["end"].toDouble();
        QString text = segment["text"].toString().trimmed();
        
        if (text.isEmpty()) continue;
        
        // Format timestamps (HH:MM:SS.mmm)
        auto formatTime = [](double seconds) {
            int hours = static_cast<int>(seconds) / 3600;
            int minutes = (static_cast<int>(seconds) % 3600) / 60;
            int secs = static_cast<int>(seconds) % 60;
            int ms = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);
            return QString("%1:%2:%3.%4")
                   .arg(hours, 2, 10, QChar('0'))
                   .arg(minutes, 2, 10, QChar('0'))
                   .arg(secs, 2, 10, QChar('0'))
                   .arg(ms, 3, 10, QChar('0'));
        };
        
        vtt += QString("%1 --> %2\n%3\n\n")
               .arg(formatTime(start))
               .arg(formatTime(end))
               .arg(text);
    }
    
    return vtt;
}

QString FileManager::formatTranscriptionToTXT(const QString& transcriptionData) const {
    // Parse JSON and extract just the text
    QJsonDocument doc = QJsonDocument::fromJson(transcriptionData.toUtf8());
    if (!doc.isObject()) {
        return transcriptionData;
    }
    
    QJsonObject obj = doc.object();
    QJsonArray segments = obj["segments"].toArray();
    
    QStringList textParts;
    for (const QJsonValue& segmentValue : segments) {
        QJsonObject segment = segmentValue.toObject();
        QString text = segment["text"].toString().trimmed();
        if (!text.isEmpty()) {
            textParts.append(text);
        }
    }
    
    return textParts.join(" ");
}

void FileManager::initializePaths() {
    // Set up default paths
    d->appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    d->cachePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    d->configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    d->defaultDownloadPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/Murmur";
    d->defaultExportPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Murmur";
    
    // Ensure directories exist
    ensureDirectoryExists(d->appDataPath);
    ensureDirectoryExists(d->cachePath);
    ensureDirectoryExists(d->configPath);
    ensureDirectoryExists(d->defaultDownloadPath);
    ensureDirectoryExists(d->defaultExportPath);
}

bool FileManager::ensureDirectoryExists(const QString& path) {
    QDir dir;
    if (!dir.exists(path)) {
        return dir.mkpath(path);
    }
    return true;
}

} // namespace Murmur