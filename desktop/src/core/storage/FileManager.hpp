#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QFuture>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include "../common/Expected.hpp"

namespace Murmur {

enum class FileError {
    InvalidPath,
    PermissionDenied,
    NotFound,
    AlreadyExists,
    InsufficientSpace,
    CopyFailed,
    MoveFailed,
    DeleteFailed,
    CreateFailed,
    Unknown
};

struct FileOperation {
    QString id;
    QString source;
    QString destination;
    QString type; // "copy", "move", "delete", "create"
    qint64 totalSize = 0;
    qint64 processedSize = 0;
    bool completed = false;
    bool cancelled = false;
    FileError error = FileError::Unknown;
    QString errorMessage;
};

struct DirectoryInfo {
    QString path;
    qint64 totalSize = 0;
    int fileCount = 0;
    int dirCount = 0;
    QStringList videoFiles;
    QStringList audioFiles;
    QStringList subtitleFiles;
};

class FileManager : public QObject {
    Q_OBJECT

public:
    explicit FileManager(QObject* parent = nullptr);
    ~FileManager();

    // Directory operations
    QFuture<Expected<DirectoryInfo, FileError>> analyzeDirectory(const QString& path);
    QFuture<Expected<QStringList, FileError>> findVideoFiles(const QString& path, bool recursive = true);
    QFuture<Expected<QString, FileError>> createDownloadDirectory(const QString& basePath, const QString& name);
    
    // File operations
    QFuture<Expected<QString, FileError>> copyFile(const QString& source, const QString& destination);
    QFuture<Expected<QString, FileError>> moveFile(const QString& source, const QString& destination);
    QFuture<Expected<bool, FileError>> deleteFile(const QString& path);
    QFuture<Expected<bool, FileError>> deleteDirectory(const QString& path, bool recursive = false);
    
    // Import/Export operations
    QFuture<Expected<QString, FileError>> importVideo(const QString& sourcePath, const QString& destinationDir = QString());
    QFuture<Expected<QStringList, FileError>> importVideoDirectory(const QString& sourcePath, const QString& destinationDir = QString());
    QFuture<Expected<QString, FileError>> exportVideo(const QString& sourcePath, const QString& destinationPath);
    
    // Transcription file operations
    QFuture<Expected<QString, FileError>> exportTranscription(const QString& transcriptionData, 
                                                             const QString& format, 
                                                             const QString& outputPath);
    QFuture<Expected<QString, FileError>> importTranscription(const QString& filePath);
    
    // Utility functions
    QString getDefaultDownloadPath() const;
    QString getDefaultExportPath() const;
    QString getAppDataPath() const;
    QString getCachePath() const;
    QString getConfigPath() const;
    
    qint64 getAvailableSpace(const QString& path) const;
    qint64 getFileSize(const QString& path) const;
    bool isVideoFile(const QString& path) const;
    bool isAudioFile(const QString& path) const;
    bool isSubtitleFile(const QString& path) const;
    QString generateUniqueFileName(const QString& basePath, const QString& fileName) const;
    
    // Operation management
    void cancelOperation(const QString& operationId);
    void cancelAllOperations();
    QList<FileOperation> getActiveOperations() const;

signals:
    void operationStarted(const QString& operationId, const QString& type, const QString& source, const QString& destination);
    void operationProgress(const QString& operationId, qint64 processed, qint64 total);
    void operationCompleted(const QString& operationId, const QString& result);
    void operationFailed(const QString& operationId, FileError error, const QString& errorMessage);

private slots:
    void onFileOperationProgress(const QString& operationId, qint64 processed, qint64 total);

private:
    struct FileManagerPrivate;
    std::unique_ptr<FileManagerPrivate> d;
    
    QString formatTranscriptionToSRT(const QString& transcriptionData) const;
    QString formatTranscriptionToVTT(const QString& transcriptionData) const;
    QString formatTranscriptionToTXT(const QString& transcriptionData) const;
    
    Expected<DirectoryInfo, FileError> analyzeDirectorySync(const QString& path);
    Expected<QStringList, FileError> findVideoFilesSync(const QString& path, bool recursive);
    Expected<QString, FileError> copyFileSync(const QString& source, const QString& destination, const QString& operationId);
    Expected<QString, FileError> moveFileSync(const QString& source, const QString& destination, const QString& operationId);
    
    void initializePaths();
    bool ensureDirectoryExists(const QString& path);
};

} // namespace Murmur