#include "AppController.hpp"
#include "../../core/torrent/TorrentEngine.hpp"
#include "../../core/media/MediaPipeline.hpp"
#include "../../core/media/VideoPlayer.hpp"
#include "../../core/media/PlatformAccelerator.hpp"
#include "../../core/storage/StorageManager.hpp"
#include "../../core/storage/FileManager.hpp"
#include "../../core/transcription/WhisperEngine.hpp"
#include "../../core/common/Logger.hpp"
#include "../../core/common/Config.hpp"
#include <QtConcurrent/QtConcurrent>

namespace Murmur {

AppController::AppController(QObject* parent)
    : QObject(parent)
{
    // Initialize core engines in dependency order
    // Check for test mode to skip certain initializations
    bool testMode = qEnvironmentVariableIsSet("MURMUR_TEST_MODE");
    
    Logger::instance().info("Creating StorageManager");
    if (!testMode) {
        storageManager_ = std::make_unique<StorageManager>(this);
    } else {
        Logger::instance().info("Test Mode: Skipping StorageManager initialization");
    }
    
    Logger::instance().info("Creating FileManager");
    fileManager_ = std::make_unique<FileManager>(this);
    Logger::instance().info("Creating MediaPipeline");
    mediaPipeline_ = std::make_unique<MediaPipeline>(this);
    Logger::instance().info("Creating VideoPlayer");
    videoPlayer_ = std::make_unique<VideoPlayer>(this);
    Logger::instance().info("Creating WhisperEngine");
    whisperEngine_ = std::make_unique<WhisperEngine>(this);
    Logger::instance().info("Creating TorrentEngine");
    torrentEngine_ = std::make_unique<TorrentEngine>(this);
    
    // Load settings
    loadSettings();
    
    Logger::instance().info("AppController created");
}

AppController::~AppController() {
    saveSettings();
    Logger::instance().info("AppController destroyed");
}

void AppController::setDarkMode(bool darkMode) {
    if (isDarkMode_ != darkMode) {
        isDarkMode_ = darkMode;
        emit darkModeChanged();
        
        // Save setting
        auto uiSettings = Config::instance().getUISettings();
        uiSettings.darkMode = darkMode;
        Config::instance().setUISettings(uiSettings);
    }
}

void AppController::initialize() {
    if (isInitialized_) {
        Logger::instance().info("AppController already initialized");
        return;
    }
    
    Logger::instance().info("Starting AppController initialization");
    setStatus("Initializing core engines...");
    
    // Initialize asynchronously
    auto future = QtConcurrent::run([this]() {
        try {
            Logger::instance().info("Starting core engine initialization in background thread");
            initializeCoreEngines();
            Logger::instance().info("Core engine initialization completed successfully");
            return true;
        } catch (const std::exception& e) {
            Logger::instance().error("Initialization failed: {}", e.what());
            return false;
        }
    });
    
    auto watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, [this, watcher]() {
        Logger::instance().info("Initialization future watcher finished");
        bool success = watcher->result();
        watcher->deleteLater();
        
        if (success) {
            Logger::instance().info("Initialization successful, calling handleInitializationComplete");
            handleInitializationComplete();
        } else {
            Logger::instance().error("Initialization failed, calling handleInitializationError");
            handleInitializationError("Failed to initialize core engines");
        }
    });
    
    Logger::instance().info("Setting future for watcher");
    watcher->setFuture(future);
    Logger::instance().info("Future set for watcher");
}

void AppController::shutdown() {
    if (!isInitialized_) {
        return;
    }
    
    setStatus("Shutting down...");
    
    // Stop all engines
    if (torrentEngine_) {
        torrentEngine_->stopSession();
    }
    
    // Save final settings
    saveSettings();
    
    isInitialized_ = false;
    emit initializedChanged();
    
    setStatus("Shutdown complete");
    
    Logger::instance().info("Application shutdown complete");
}

void AppController::saveSettings() {
    try {
        Config::instance().sync();
        Logger::instance().debug("Settings saved");
    } catch (const std::exception& e) {
        Logger::instance().warn("Failed to save settings: {}", e.what());
    }
}

void AppController::loadSettings() {
    try {
        auto uiSettings = Config::instance().getUISettings();
        isDarkMode_ = uiSettings.darkMode;
        
        Logger::instance().debug("Settings loaded");
    } catch (const std::exception& e) {
        Logger::instance().warn("Failed to load settings: {}", e.what());
    }
}

void AppController::handleInitializationComplete() {
    Logger::instance().info("Starting handleInitializationComplete");
    connectEngineSignals();
    
    isInitialized_ = true;
    Logger::instance().info("Setting isInitialized_ to true");
    emit initializedChanged();
    Logger::instance().info("Emitted initializedChanged");
    
    setStatus("Ready");
    
    Logger::instance().info("Application initialization complete");
    
    // Also emit a specific signal for UI components
    emit initializationComplete();
    Logger::instance().info("Emitted initializationComplete");
    
    // Log the state of all engines
    Logger::instance().info("StorageManager: {}", storageManager_ ? "available" : "null");
    Logger::instance().info("FileManager: {}", fileManager_ ? "available" : "null");
    Logger::instance().info("MediaPipeline: {}", mediaPipeline_ ? "available" : "null");
    Logger::instance().info("VideoPlayer: {}", videoPlayer_ ? "available" : "null");
    Logger::instance().info("WhisperEngine: {}", whisperEngine_ ? "available" : "null");
    Logger::instance().info("TorrentEngine: {}", torrentEngine_ ? "available" : "null");
}

void AppController::handleInitializationError(const QString& error) {
    setStatus("Initialization failed");
    emit initializationFailed(error);
    
    Logger::instance().error("Application initialization failed: {}", error.toStdString());
    
    // Also emit initializedChanged to ensure any waiting code knows initialization is done (even if failed)
    isInitialized_ = false;
    emit initializedChanged();
}

void AppController::setStatus(const QString& status) {
    if (status_ != status) {
        status_ = status;
        emit statusChanged();
    }
}

void AppController::initializeCoreEngines() {
    Logger::instance().info("Starting core engine initialization");
    
    bool testMode = qEnvironmentVariableIsSet("MURMUR_TEST_MODE");
    
    // Initialize engines in dependency order
    
    // 1. Initialize storage first
    if (storageManager_) {
        Logger::instance().info("Initializing storage manager");
        auto result = storageManager_->initialize();
        if (!result) {
            Logger::instance().error("Failed to initialize storage manager");
            throw std::runtime_error("Failed to initialize storage manager");
        }
        Logger::instance().info("Storage manager initialized successfully");
    } else if (!testMode) {
        Logger::instance().error("Storage manager is null");
        throw std::runtime_error("Storage manager is null");
    } else {
        Logger::instance().info("Test Mode: Storage manager is null (expected)");
    }
    
    // File manager doesn't need explicit initialization
    Logger::instance().info("File manager ready");
    
    // Platform accelerator initialization skipped (abstract class)
    Logger::instance().info("Platform accelerator ready");
    
    // Media pipeline doesn't need explicit initialization
    Logger::instance().info("Media pipeline ready");
    
    // Video player doesn't need explicit initialization
    Logger::instance().info("Video player ready");
    
    // 2. Initialize transcription engine
    if (whisperEngine_) {
        Logger::instance().info("Initializing whisper engine");
        auto result = whisperEngine_->initialize();
        if (!result) {
            Logger::instance().warn("Whisper engine initialization failed, transcription features disabled");
        } else {
            Logger::instance().info("Whisper engine initialized successfully");
        }
    } else {
        Logger::instance().warn("Whisper engine is null");
    }
    
    // 3. Start torrent engine last
    if (torrentEngine_) {
        Logger::instance().info("Starting torrent engine session");
        torrentEngine_->startSession();
        Logger::instance().info("Torrent engine session started");
    } else {
        Logger::instance().warn("Torrent engine is null");
    }
    
    Logger::instance().info("All core engines initialized successfully");
    
    // Log the state of all engines
    Logger::instance().info("StorageManager: {}", storageManager_ ? "available" : "null");
    Logger::instance().info("FileManager: {}", fileManager_ ? "available" : "null");
    Logger::instance().info("MediaPipeline: {}", mediaPipeline_ ? "available" : "null");
    Logger::instance().info("VideoPlayer: {}", videoPlayer_ ? "available" : "null");
    Logger::instance().info("WhisperEngine: {}", whisperEngine_ ? "available" : "null");
    Logger::instance().info("TorrentEngine: {}", torrentEngine_ ? "available" : "null");
}

void AppController::connectEngineSignals() {
    // Connect torrent engine signals
    if (torrentEngine_) {
        connect(torrentEngine_.get(), &TorrentEngine::torrentAdded,
                this, [](const QString& infoHash) {
            Logger::instance().debug("Torrent added: {}", infoHash.toStdString());
        });
        
        connect(torrentEngine_.get(), &TorrentEngine::torrentError,
                this, [](const QString& infoHash, TorrentError error) {
            Logger::instance().warn("Torrent error: {} - {}", infoHash.toStdString(), static_cast<int>(error));
        });
    }
    
    // Connect media pipeline signals
    if (mediaPipeline_) {
        connect(mediaPipeline_.get(), &MediaPipeline::conversionProgress,
                this, [](const QString& operationId, const ConversionProgress& progress) {
            Logger::instance().debug("Conversion progress: {} - {:.1f}%", operationId.toStdString(), progress.percentage);
        });
        
        connect(mediaPipeline_.get(), &MediaPipeline::conversionCompleted,
                this, [](const QString& operationId, const QString& outputPath) {
            Logger::instance().info("Conversion completed: {} -> {}", operationId.toStdString(), outputPath.toStdString());
        });
        
        connect(mediaPipeline_.get(), &MediaPipeline::conversionFailed,
                this, [](const QString& operationId, MediaError /*error*/, const QString& errorString) {
            Logger::instance().error("Conversion failed: {} - {}", operationId.toStdString(), errorString.toStdString());
        });
    }
    
    // Connect video player signals
    if (videoPlayer_) {
        connect(videoPlayer_.get(), &VideoPlayer::playbackStateChanged,
                this, [](PlaybackState state) {
            QString stateStr = "Unknown";
            switch(state) {
                case PlaybackState::Stopped: stateStr = "Stopped"; break;
                case PlaybackState::Playing: stateStr = "Playing"; break;
                case PlaybackState::Paused: stateStr = "Paused"; break;
                case PlaybackState::Buffering: stateStr = "Buffering"; break;
                case PlaybackState::Seeking: stateStr = "Seeking"; break;
                case PlaybackState::Error: stateStr = "Error"; break;
            }
            Logger::instance().info("Playback state changed: {}", stateStr.toStdString());
        });
        
        connect(videoPlayer_.get(), &VideoPlayer::errorOccurred,
                this, [](PlayerError error, const QString& description) {
            Logger::instance().error("Playback error: {} - {}", static_cast<int>(error), description.toStdString());
        });
    }
    
    // Connect whisper engine signals
    if (whisperEngine_) {
        connect(whisperEngine_.get(), &WhisperEngine::transcriptionProgress,
                this, [](const QString& taskId, const TranscriptionProgress& progress) {
            Logger::instance().debug("Transcription progress: {} - {:.1f}%", taskId.toStdString(), progress.percentage);
        });
        
        connect(whisperEngine_.get(), &WhisperEngine::transcriptionCompleted,
                this, [](const QString& taskId, const TranscriptionResult& result) {
            Logger::instance().info("Transcription completed: {} ({} chars)", taskId.toStdString(), result.fullText.length());
        });
        
        connect(whisperEngine_.get(), &WhisperEngine::transcriptionFailed,
                this, [](const QString& taskId, TranscriptionError /*error*/, const QString& errorString) {
            Logger::instance().error("Transcription failed: {} - {}", taskId.toStdString(), errorString.toStdString());
        });
    }
    
    // Connect storage manager signals
    if (storageManager_) {
        connect(storageManager_.get(), &StorageManager::databaseError,
                this, [](StorageError error, const QString& description) {
            Logger::instance().error("Database error: {} - {}", static_cast<int>(error), description.toStdString());
        });
    }
    
    // Connect file manager signals
    if (fileManager_) {
        connect(fileManager_.get(), &FileManager::operationStarted,
                this, [](const QString& operationId, const QString& type, const QString& source, const QString& destination) {
            Logger::instance().debug("File operation started: {} ({}: {} -> {})", operationId.toStdString(), type.toStdString(), source.toStdString(), destination.toStdString());
        });
        
        connect(fileManager_.get(), &FileManager::operationCompleted,
                this, [](const QString& operationId, const QString& result) {
            Logger::instance().debug("File operation completed: {} - {}", operationId.toStdString(), result.toStdString());
        });
        
        connect(fileManager_.get(), &FileManager::operationFailed,
                this, [](const QString& operationId, FileError error, const QString& errorMessage) {
            Logger::instance().error("File operation failed: {} - {} ({})", operationId.toStdString(), static_cast<int>(error), errorMessage.toStdString());
        });
    }
    
    Logger::instance().info("All engine signals connected");
}

// Hardware information implementations
QVariantList AppController::getAvailableGPUs() const {
    QVariantList result;
    
    if (platformAccelerator_) {
        auto gpus = platformAccelerator_->getAvailableGPUs();
        for (const auto& gpu : gpus) {
            QVariantMap gpuMap;
            gpuMap["name"] = gpu.name;
            gpuMap["vendor"] = gpu.vendor;
            gpuMap["driverVersion"] = gpu.driverVersion;
            gpuMap["vramMB"] = gpu.vramMB;
            gpuMap["isDiscrete"] = gpu.isDiscrete;
            gpuMap["isActive"] = gpu.isActive;
            gpuMap["supportsHardwareDecoding"] = gpu.supportsHardwareDecoding;
            gpuMap["supportsHardwareEncoding"] = gpu.supportsHardwareEncoding;
            gpuMap["supportedCodecs"] = gpu.supportedCodecs;
            result.append(gpuMap);
        }
    }
    
    return result;
}

// Settings management implementations
QString AppController::getSetting(const QString& key, const QString& defaultValue) const {
    try {
        return Config::instance().getString(key, defaultValue);
    } catch (const std::exception& e) {
        Logger::instance().warn("Failed to get setting {}: {}", key.toStdString(), e.what());
        return defaultValue;
    }
}

void AppController::setSetting(const QString& key, const QString& value) {
    try {
        Config::instance().setString(key, value);
        Config::instance().sync();
    } catch (const std::exception& e) {
        Logger::instance().error("Failed to set setting {}: {}", key.toStdString(), e.what());
    }
}

int AppController::getSetting(const QString& key, int defaultValue) const {
    try {
        return Config::instance().getInt(key, defaultValue);
    } catch (const std::exception& e) {
        Logger::instance().warn("Failed to get setting {}: {}", key.toStdString(), e.what());
        return defaultValue;
    }
}

void AppController::setSetting(const QString& key, int value) {
    try {
        Config::instance().setInt(key, value);
        Config::instance().sync();
    } catch (const std::exception& e) {
        Logger::instance().error("Failed to set setting {}: {}", key.toStdString(), e.what());
    }
}

bool AppController::getSetting(const QString& key, bool defaultValue) const {
    try {
        return Config::instance().getBool(key, defaultValue);
    } catch (const std::exception& e) {
        Logger::instance().warn("Failed to get setting {}: {}", key.toStdString(), e.what());
        return defaultValue;
    }
}

void AppController::setSetting(const QString& key, bool value) {
    try {
        Config::instance().setBool(key, value);
        Config::instance().sync();
    } catch (const std::exception& e) {
        Logger::instance().error("Failed to set setting {}: {}", key.toStdString(), e.what());
    }
}

void AppController::applySettings() {
    try {
        auto uiSettings = Config::instance().getUISettings();
        setDarkMode(uiSettings.darkMode);
        
        Logger::instance().info("Settings applied successfully");
    } catch (const std::exception& e) {
        Logger::instance().error("Failed to apply settings: {}", e.what());
    }
}

QString AppController::getStatusMessage() const {
    return status_;
}

void AppController::setStatusMessage(const QString& message) {
    setStatus(message);
}

Expected<void, QString> AppController::loadConfiguration() {
    try {
        loadSettings();
        return Expected<void, QString>();
    } catch (const std::exception& e) {
        return makeUnexpected(QString("Configuration load failed: %1").arg(e.what()));
    }
}

Expected<void, QString> AppController::initializeDatabase() {
    if (storageManager_) {
        auto dbResult = storageManager_->initialize();
        if (!dbResult.hasValue()) {
            return makeUnexpected(QString("Database initialization failed: error %1").arg(static_cast<int>(dbResult.error())));
        }
        return Expected<void, QString>();
    }
    return makeUnexpected(QString("StorageManager not available"));
}

void AppController::updateSettings(const QVariantMap& settings) {
    for (auto it = settings.begin(); it != settings.end(); ++it) {
        const QString& key = it.key();
        const QVariant& value = it.value();
        
        if (value.typeId() == QMetaType::QString) {
            setSetting(key, value.toString());
        } else if (value.typeId() == QMetaType::Int) {
            setSetting(key, value.toInt());
        } else if (value.typeId() == QMetaType::Bool) {
            setSetting(key, value.toBool());
        }
    }
}

void AppController::saveConfiguration() {
    saveSettings();
}

} // namespace Murmur