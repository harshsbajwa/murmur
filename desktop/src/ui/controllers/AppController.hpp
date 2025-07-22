#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <memory>
#include "../../core/common/Expected.hpp"

namespace Murmur {

class TorrentEngine;
class MediaPipeline;
class WhisperEngine;
class StorageManager;
class FileManager;
class VideoPlayer;
class PlatformAccelerator;

class AppController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isInitialized READ isInitialized NOTIFY initializedChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool isDarkMode READ isDarkMode WRITE setDarkMode NOTIFY darkModeChanged)
    
public:
    explicit AppController(QObject* parent = nullptr);
    ~AppController();
    
    bool isInitialized() const { return isInitialized_; }
    QString status() const { return status_; }
    bool isDarkMode() const { return isDarkMode_; }
    
    void setDarkMode(bool darkMode);
    
    // Core engine access
    TorrentEngine* torrentEngine() const { return torrentEngine_.get(); }
    MediaPipeline* mediaPipeline() const { return mediaPipeline_.get(); }
    WhisperEngine* whisperEngine() const { return whisperEngine_.get(); }
    StorageManager* storageManager() const { return storageManager_.get(); }
    FileManager* fileManager() const { return fileManager_.get(); }
    VideoPlayer* videoPlayer() const { return videoPlayer_.get(); }
    PlatformAccelerator* platformAccelerator() const { return platformAccelerator_.get(); }
    
public slots:
    void initialize();
    void shutdown();
    void saveSettings();
    void loadSettings();
    void applySettings();
    
    // Hardware information
    QVariantList getAvailableGPUs() const;
    
    // Settings management
    QString getSetting(const QString& key, const QString& defaultValue = QString()) const;
    void setSetting(const QString& key, const QString& value);
    int getSetting(const QString& key, int defaultValue) const;
    void setSetting(const QString& key, int value);
    bool getSetting(const QString& key, bool defaultValue) const;
    void setSetting(const QString& key, bool value);
    
    // UI integration methods
    QString getStatusMessage() const;
    void setStatusMessage(const QString& message);
    Expected<void, QString> loadConfiguration();
    Expected<void, QString> initializeDatabase();
    void updateSettings(const QVariantMap& settings);
    void saveConfiguration();
    
signals:
    void initializedChanged();
    void statusChanged();
    void darkModeChanged();
    void initializationFailed(const QString& error);
    
private slots:
    void handleInitializationComplete();
    void handleInitializationError(const QString& error);
    
private:
    std::unique_ptr<TorrentEngine> torrentEngine_;
    std::unique_ptr<MediaPipeline> mediaPipeline_;
    std::unique_ptr<WhisperEngine> whisperEngine_;
    std::unique_ptr<StorageManager> storageManager_;
    std::unique_ptr<FileManager> fileManager_;
    std::unique_ptr<VideoPlayer> videoPlayer_;
    std::unique_ptr<PlatformAccelerator> platformAccelerator_;
    
    bool isInitialized_ = false;
    QString status_ = "Initializing...";
    bool isDarkMode_ = false;
    
    void setStatus(const QString& status);
    void initializeCoreEngines();
    void connectEngineSignals();
};

} // namespace Murmur