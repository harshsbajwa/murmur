#pragma once

#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QString>
#include <QtCore/QVariant>
#include <memory>

namespace Murmur {

class Config {
public:
    static Config& instance();
    
    void initialize(const QString& organizationName = "Murmur",
                   const QString& applicationName = "MurmurDesktop");
    
    // General settings
    QVariant getValue(const QString& key, const QVariant& defaultValue = QVariant()) const;
    void setValue(const QString& key, const QVariant& value);
    
    // Typed convenience methods
    QString getString(const QString& key, const QString& defaultValue = QString()) const;
    int getInt(const QString& key, int defaultValue = 0) const;
    bool getBool(const QString& key, bool defaultValue = false) const;
    double getDouble(const QString& key, double defaultValue = 0.0) const;
    
    void setString(const QString& key, const QString& value);
    void setInt(const QString& key, int value);
    void setBool(const QString& key, bool value);
    void setDouble(const QString& key, double value);
    
    // Application-specific settings
    struct TorrentSettings {
        QString downloadPath;
        int maxConnections = 100;
        int uploadRateLimit = -1;  // -1 = unlimited
        int downloadRateLimit = -1;
        bool enableDHT = true;
        QStringList trackers;
    };
    
    struct MediaSettings {
        QString tempPath;
        int maxConcurrentJobs = 2;
        bool useHardwareAcceleration = true;
        QString defaultOutputFormat = "mp4";
    };
    
    struct TranscriptionSettings {
        QString modelSize = "base";
        QString defaultLanguage = "auto";
        bool cacheResults = true;
        QString modelsPath;
    };
    
    struct UISettings {
        bool darkMode = false;
        double windowOpacity = 1.0;
        QString lastWindowGeometry;
        QString lastWindowState;
    };
    
    TorrentSettings getTorrentSettings() const;
    MediaSettings getMediaSettings() const;
    TranscriptionSettings getTranscriptionSettings() const;
    UISettings getUISettings() const;
    
    void setTorrentSettings(const TorrentSettings& settings);
    void setMediaSettings(const MediaSettings& settings);
    void setTranscriptionSettings(const TranscriptionSettings& settings);
    void setUISettings(const UISettings& settings);
    
    // Paths
    QString getDataPath() const;
    QString getCachePath() const;
    QString getConfigPath() const;
    QString getTempPath() const;
    
    void sync();
    
private:
    Config() = default;
    std::unique_ptr<QSettings> settings_;
    
    void ensureDirectoriesExist();
};

} // namespace Murmur