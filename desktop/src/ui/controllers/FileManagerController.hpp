#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtCore/QFuture>
#include "../../core/storage/FileManager.hpp"
#include "../../core/common/Logger.hpp"

namespace Murmur {

class FileManagerController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString defaultDownloadPath READ defaultDownloadPath NOTIFY pathsChanged)
    Q_PROPERTY(QString defaultExportPath READ defaultExportPath NOTIFY pathsChanged)
    Q_PROPERTY(qreal totalProgress READ totalProgress NOTIFY progressChanged)
    Q_PROPERTY(int activeOperationsCount READ activeOperationsCount NOTIFY operationsChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(qint64 totalSpace READ totalSpace NOTIFY diskSpaceChanged)
    Q_PROPERTY(qint64 usedSpace READ usedSpace NOTIFY diskSpaceChanged)
    Q_PROPERTY(QStringList fileModel READ fileModel NOTIFY fileModelChanged)
    Q_PROPERTY(bool isReady READ isReady NOTIFY readyChanged)

public:
explicit FileManagerController(QObject* parent = nullptr);

void setReady(bool ready);
void updateReadyState();

Q_INVOKABLE void setFileManager(FileManager* fileManager);
    
    QString defaultDownloadPath() const;
    QString defaultExportPath() const;
    qreal totalProgress() const;
    int activeOperationsCount() const;
    bool isBusy() const;
    qint64 totalSpace() const;
    qint64 usedSpace() const;
    QStringList fileModel() const;
bool isReady() const;

public slots:
    // Directory operations
    void analyzeDirectory(const QString& path);
    void findVideoFiles(const QString& path, bool recursive = true);
    void createDownloadDirectory(const QString& basePath, const QString& name);
    
    // File operations
    void copyFile(const QString& source, const QString& destination);
    void moveFile(const QString& source, const QString& destination);
    void deleteFile(const QString& path);
    void deleteDirectory(const QString& path, bool recursive = false);
    
    // Import/Export operations
    void importVideo(const QString& sourcePath, const QString& destinationDir = QString());
    void importVideoDirectory(const QString& sourcePath, const QString& destinationDir = QString());
    void exportVideo(const QString& sourcePath, const QString& destinationPath);
    void importVideosFromUrls(const QStringList& urls, const QString& destinationDir = QString());
    
    // Transcription file operations
    void exportTranscription(const QString& transcriptionData, const QString& format, const QString& outputPath);
    void importTranscription(const QString& filePath);
    
    // Utility functions
    qint64 getAvailableSpace(const QString& path);
    qint64 getFileSize(const QString& path);
    bool isVideoFile(const QString& path);
    bool isAudioFile(const QString& path);
    bool isSubtitleFile(const QString& path);
    QString generateUniqueFileName(const QString& basePath, const QString& fileName);
    
    // Operation management
    void cancelOperation(const QString& operationId);
    void cancelAllOperations();
    QStringList getActiveOperationIds();
    
    // Path management
    void setDefaultDownloadPath(const QString& path);
    void setDefaultExportPath(const QString& path);
    void openInFileManager(const QString& path);
    
    // Disk space management
    Q_INVOKABLE void updateDiskSpace();
    
    // File model management
    Q_INVOKABLE void refreshFileModel();
    Q_INVOKABLE void removeFile(const QString& filePath);

signals:
    // File model changes
    void fileModelChanged();
    void readyChanged();
    
    // Path changes
    void pathsChanged();
    
    // Progress changes
    void progressChanged();
    
    // Operation changes
    void operationsChanged();
    void busyChanged();
    
    // Disk space changes
    void diskSpaceChanged();
    
    // Directory analysis results
    void directoryAnalyzed(const QString& path, int fileCount, int dirCount, qint64 totalSize, const QStringList& videoFiles);
    void videoFilesFound(const QString& path, const QStringList& videoFiles);
    
    // Operation results
    void operationStarted(const QString& operationId, const QString& type, const QString& source, const QString& destination);
    void operationProgress(const QString& operationId, qreal progress);
    void operationCompleted(const QString& operationId, const QString& result);
    void operationFailed(const QString& operationId, const QString& error);
    
    // Import/Export results
    void videoImported(const QString& sourcePath, const QString& destinationPath);
    void videosImported(const QStringList& importedPaths);
    void videoExported(const QString& sourcePath, const QString& destinationPath);
    void transcriptionExported(const QString& outputPath, const QString& format);
    void transcriptionImported(const QString& filePath, const QString& content);
    
    // Errors
    void fileError(const QString& operation, const QString& path, const QString& error);

private slots:
    void onOperationStarted(const QString& operationId, const QString& type, const QString& source, const QString& destination);
    void onOperationProgress(const QString& operationId, qint64 processed, qint64 total);
    void onOperationCompleted(const QString& operationId, const QString& result);
    void onOperationFailed(const QString& operationId, FileError error, const QString& errorMessage);
    
    void updateProgress();

private:
FileManager* fileManager_ = nullptr;

bool ready_ = false;

QHash<QString, QPair<qint64, qint64>> operationProgress_; // operationId -> (processed, total)
qreal totalProgress_ = 0.0;
bool isBusy_ = false;
mutable qint64 totalSpace_ = -1;  // Cached value
mutable qint64 usedSpace_ = -1;   // Cached value
mutable QDateTime lastDiskSpaceUpdate_; // Last update time
QStringList fileModel_;
    
    QString translateFileError(FileError error) const;
    void setBusy(bool busy);
    void calculateTotalProgress();
};

} // namespace Murmur