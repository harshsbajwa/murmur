#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <QtCore/QUrl>
#include <QtCore/QFuture>
#include <memory>
#include "../../core/common/Expected.hpp"
#include "../../core/media/MediaPipeline.hpp"
#include "../../core/media/VideoPlayer.hpp"
#include "../../core/storage/StorageManager.hpp"
#include "../../core/common/Logger.hpp"

namespace Murmur {

class MediaController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl currentVideoSource READ currentVideoSource NOTIFY sourceChanged)
    Q_PROPERTY(qreal playbackPosition READ playbackPosition NOTIFY positionChanged)
    Q_PROPERTY(bool isProcessing READ isProcessing NOTIFY processingChanged)
    Q_PROPERTY(QString currentMediaFile READ currentMediaFile NOTIFY currentMediaFileChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY outputPathChanged)
    Q_PROPERTY(bool isReady READ isReady NOTIFY readyChanged)
    
public:
explicit MediaController(QObject* parent = nullptr);

void setReady(bool ready);
void updateReadyState();

// Setters for dependencies
Q_INVOKABLE void setMediaPipeline(MediaPipeline* pipeline);
    Q_INVOKABLE void setVideoPlayer(VideoPlayer* player);
    Q_INVOKABLE void setStorageManager(StorageManager* storage);
    
    QUrl currentVideoSource() const { return currentVideoSource_; }
    qreal playbackPosition() const { return playbackPosition_; }
    bool isProcessing() const { return isProcessing_; }
    QString currentMediaFile() const { return currentMediaFile_; }
    QString outputPath() const { return outputPath_; }
bool isReady() const;
    
public slots:
    void loadTorrent(const QString& infoHash);
    void loadLocalFile(const QUrl& filePath);
    void savePosition(qreal position);
    void convertVideo(const QString& inputPath, const QString& outputPath, const QString& format);
    void extractAudio(const QString& videoPath, const QString& outputPath);
    void generateThumbnail(const QString& videoPath, const QString& outputPath, int timeOffset = 0);
    void cancelOperation(const QString& operationId);
    void cancelOperation(); // Cancel current operation
    void cancelAllOperations();
    
    // QML-friendly conversion method
    Q_INVOKABLE void convertVideo(const QString& format);
    Q_INVOKABLE void generateThumbnailForCurrentVideo();
    
    // Additional methods for UI integration
    void startConversion(const QString& outputPath, const QVariantMap& settings = {});
    void setConversionSettings(const QVariantMap& settings);
    QString getCurrentMediaFile() const;
    QString getOutputPath() const;
    QStringList getActiveOperations() const;
    
signals:
    void sourceChanged();
    void positionChanged();
    void processingChanged();
    void currentMediaFileChanged();
    void outputPathChanged();
    void readyChanged();
    void conversionProgress(const QString& operationId, qreal progress);
    void conversionCompleted(const QString& operationId, const QString& outputPath);
    void conversionError(const QString& operationId, const QString& error);
    void videoAnalyzed(const QString& filePath, const VideoInfo& info);
    void thumbnailGenerated(const QString& videoPath, const QString& thumbnailPath);
    
    // Additional signals for UI integration
    void progressUpdated(const QVariantMap& progress);
    void errorOccurred(const QString& error);
    void operationCompleted(const QString& operation);
    void operationCancelled(const QString& operation);
    
private slots:
    void onConversionProgress(const QString& operationId, const ConversionProgress& progress);
    void onConversionCompleted(const QString& operationId, const QString& outputPath);
    void onConversionFailed(const QString& operationId, MediaError error, const QString& errorString);

private:
    QUrl currentVideoSource_;
    qreal playbackPosition_ = 0.0;
    bool isProcessing_ = false;
    QString currentMediaFile_;
    QString outputPath_;
    
MediaPipeline* mediaPipeline_ = nullptr;
VideoPlayer* videoPlayer_ = nullptr;
StorageManager* storageManager_ = nullptr;

bool ready_ = false;

    QHash<QString, QFuture<void>> activeOperations_;
QVariantMap conversionSettings_;
QString currentOperationId_;
    
    void setProcessing(bool processing);
    void updateVideoSource(const QUrl& source);
    void connectPipelineSignals();
};

} // namespace Murmur