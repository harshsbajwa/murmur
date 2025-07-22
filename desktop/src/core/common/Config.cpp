#include "Config.hpp"
#include "Logger.hpp"
#include <QtCore/QDir>
#include <QtCore/QStringList>

namespace Murmur {

Config& Config::instance() {
    static Config instance;
    return instance;
}

void Config::initialize(const QString& organizationName, const QString& applicationName) {
    settings_ = std::make_unique<QSettings>(organizationName, applicationName);
    ensureDirectoriesExist();
    MURMUR_INFO("Config initialized for {}/{}", 
                organizationName.toStdString(), applicationName.toStdString());
}

QVariant Config::getValue(const QString& key, const QVariant& defaultValue) const {
    if (!settings_) return defaultValue;
    return settings_->value(key, defaultValue);
}

void Config::setValue(const QString& key, const QVariant& value) {
    if (settings_) {
        settings_->setValue(key, value);
    }
}

QString Config::getString(const QString& key, const QString& defaultValue) const {
    return getValue(key, defaultValue).toString();
}

int Config::getInt(const QString& key, int defaultValue) const {
    return getValue(key, defaultValue).toInt();
}

bool Config::getBool(const QString& key, bool defaultValue) const {
    return getValue(key, defaultValue).toBool();
}

double Config::getDouble(const QString& key, double defaultValue) const {
    return getValue(key, defaultValue).toDouble();
}

void Config::setString(const QString& key, const QString& value) {
    setValue(key, value);
}

void Config::setInt(const QString& key, int value) {
    setValue(key, value);
}

void Config::setBool(const QString& key, bool value) {
    setValue(key, value);
}

void Config::setDouble(const QString& key, double value) {
    setValue(key, value);
}

Config::TorrentSettings Config::getTorrentSettings() const {
    TorrentSettings settings;
    settings.downloadPath = getString("torrent/downloadPath", 
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    settings.maxConnections = getInt("torrent/maxConnections", 100);
    settings.uploadRateLimit = getInt("torrent/uploadRateLimit", -1);
    settings.downloadRateLimit = getInt("torrent/downloadRateLimit", -1);
    settings.enableDHT = getBool("torrent/enableDHT", true);
    
    // Default trackers
    QStringList defaultTrackers = {
        "wss://tracker.webtorrent.dev",
        "wss://tracker.openwebtorrent.com",
        "wss://tracker.btorrent.xyz"
    };
    settings.trackers = getValue("torrent/trackers", defaultTrackers).toStringList();
    
    return settings;
}

Config::MediaSettings Config::getMediaSettings() const {
    MediaSettings settings;
    settings.tempPath = getString("media/tempPath", getTempPath());
    settings.maxConcurrentJobs = getInt("media/maxConcurrentJobs", 2);
    settings.useHardwareAcceleration = getBool("media/useHardwareAcceleration", true);
    settings.defaultOutputFormat = getString("media/defaultOutputFormat", "mp4");
    return settings;
}

Config::TranscriptionSettings Config::getTranscriptionSettings() const {
    TranscriptionSettings settings;
    settings.modelSize = getString("transcription/modelSize", "base");
    settings.defaultLanguage = getString("transcription/defaultLanguage", "auto");
    settings.cacheResults = getBool("transcription/cacheResults", true);
    settings.modelsPath = getString("transcription/modelsPath", 
        getDataPath() + "/models");
    return settings;
}

Config::UISettings Config::getUISettings() const {
    UISettings settings;
    settings.darkMode = getBool("ui/darkMode", false);
    settings.windowOpacity = getDouble("ui/windowOpacity", 1.0);
    settings.lastWindowGeometry = getString("ui/lastWindowGeometry");
    settings.lastWindowState = getString("ui/lastWindowState");
    return settings;
}

void Config::setTorrentSettings(const TorrentSettings& settings) {
    setValue("torrent/downloadPath", settings.downloadPath);
    setValue("torrent/maxConnections", settings.maxConnections);
    setValue("torrent/uploadRateLimit", settings.uploadRateLimit);
    setValue("torrent/downloadRateLimit", settings.downloadRateLimit);
    setValue("torrent/enableDHT", settings.enableDHT);
    setValue("torrent/trackers", settings.trackers);
}

void Config::setMediaSettings(const MediaSettings& settings) {
    setValue("media/tempPath", settings.tempPath);
    setValue("media/maxConcurrentJobs", settings.maxConcurrentJobs);
    setValue("media/useHardwareAcceleration", settings.useHardwareAcceleration);
    setValue("media/defaultOutputFormat", settings.defaultOutputFormat);
}

void Config::setTranscriptionSettings(const TranscriptionSettings& settings) {
    setValue("transcription/modelSize", settings.modelSize);
    setValue("transcription/defaultLanguage", settings.defaultLanguage);
    setValue("transcription/cacheResults", settings.cacheResults);
    setValue("transcription/modelsPath", settings.modelsPath);
}

void Config::setUISettings(const UISettings& settings) {
    setValue("ui/darkMode", settings.darkMode);
    setValue("ui/windowOpacity", settings.windowOpacity);
    setValue("ui/lastWindowGeometry", settings.lastWindowGeometry);
    setValue("ui/lastWindowState", settings.lastWindowState);
}

QString Config::getDataPath() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString Config::getCachePath() const {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

QString Config::getConfigPath() const {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
}

QString Config::getTempPath() const {
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/Murmur";
}

void Config::sync() {
    if (settings_) {
        settings_->sync();
    }
}

void Config::ensureDirectoriesExist() {
    QStringList paths = {
        getDataPath(),
        getCachePath(),
        getTempPath(),
        getString("transcription/modelsPath", getDataPath() + "/models")
    };
    
    for (const QString& path : paths) {
        QDir dir;
        if (!dir.mkpath(path)) {
            MURMUR_WARN("Failed to create directory: {}", path.toStdString());
        }
    }
}

} // namespace Murmur