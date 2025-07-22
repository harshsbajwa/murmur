#include "TorrentEngine.hpp"
#include "TorrentStateModel.hpp"
#include "TorrentSecurityWrapper.hpp"
#include "../common/Logger.hpp"
#include "../common/Config.hpp"
#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QMetaObject>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/span.hpp>
#include <fstream>
#include <iomanip>
#include <sstream>


namespace Murmur {

// Helper to convert hash to hex string
static std::string to_hex_str(const libtorrent::sha1_hash& hash) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char i : hash) {
        oss << std::setw(2) << static_cast<int>(i);
    }
    return oss.str();
}

TorrentEngine::TorrentEngine(QObject* parent)
    : QObject(parent)
    , torrentModel_(std::make_unique<TorrentStateModel>(this))
    , securityWrapper_(std::make_unique<TorrentSecurityWrapper>())
    , alertTimer_(new QTimer(this))
    , updateTimer_(new QTimer(this))
{
    // Initialize download path
    downloadPath_ = Config::instance().getTorrentSettings().downloadPath;
    
    // Setup timers
    alertTimer_->setInterval(100); // 100ms for responsive alerts
    updateTimer_->setInterval(1000); // 1s for UI updates
    
    connect(alertTimer_, &QTimer::timeout, this, &TorrentEngine::handleLibtorrentAlerts);
    connect(updateTimer_, &QTimer::timeout, this, &TorrentEngine::updateTorrentStates);
    
    // Initialize session
    initializeSession();
    
    MURMUR_INFO("TorrentEngine initialized");
}

TorrentEngine::~TorrentEngine() {
    stopSession();
    MURMUR_INFO("TorrentEngine destroyed");
}

QFuture<Expected<TorrentEngine::TorrentInfo, TorrentError>> 
TorrentEngine::addTorrent(const QString& magnetUri) {
    return QtConcurrent::run([this, magnetUri]() -> Expected<TorrentInfo, TorrentError> {
        try {
            // Validate input
            if (!InputValidator::validateMagnetUri(magnetUri)) {
                MURMUR_WARN("Invalid magnet URI: {}", magnetUri.toStdString());
                QMetaObject::invokeMethod(this, [this, magnetUri]() {
                    emit torrentError(QString(), TorrentError::InvalidMagnetUri);
                }, Qt::QueuedConnection);
                return makeUnexpected(TorrentError::InvalidMagnetUri);
            }
            
            // Check if torrent already exists
            libtorrent::error_code ec;
            libtorrent::add_torrent_params params = libtorrent::parse_magnet_uri(magnetUri.toStdString(), ec);
            
            if (ec) {
                MURMUR_ERROR("Failed to parse magnet URI: {}", ec.message());
                QMetaObject::invokeMethod(this, [this, magnetUri]() {
                    emit torrentError(QString(), TorrentError::InvalidMagnetUri);
                }, Qt::QueuedConnection);
                return makeUnexpected(TorrentError::InvalidMagnetUri);
            }
            
            QString hashString = QString::fromStdString(to_hex_str(params.info_hashes.v1));
            
            {
                QReadLocker locker(&torrentsLock_);
                if (torrents_.contains(hashString)) {
                    MURMUR_INFO("Torrent already exists: {}", hashString.toStdString());
                    return torrents_[hashString];
                }
            }
            
            // Configure the parsed params
            params.save_path = downloadPath_.toStdString();
            params.flags |= libtorrent::torrent_flags::auto_managed;
            params.flags |= libtorrent::torrent_flags::duplicate_is_error;
            
            // Add to session
            libtorrent::torrent_handle handle = session_->add_torrent(params, ec);
            if (ec) {
                MURMUR_ERROR("Failed to add torrent: {}", ec.message());
                TorrentError error = mapLibtorrentError(ec);
                QMetaObject::invokeMethod(this, [this, hashString, error]() {
                    emit torrentError(hashString, error);
                }, Qt::QueuedConnection);
                return makeUnexpected(error);
            }
            
            // Create torrent info
            TorrentInfo info = createTorrentInfo(handle);
            info.magnetUri = magnetUri;
            
            // If name is empty (common with magnet URIs before metadata is available),
            // try to extract it from the magnet URI's dn parameter
            if (info.name.isEmpty()) {
                QUrl magnetUrl(magnetUri);
                QUrlQuery query(magnetUrl);
                QString displayName = query.queryItemValue("dn");
                if (!displayName.isEmpty()) {
                    // URL decode the display name
                    info.name = QUrl::fromPercentEncoding(displayName.toUtf8()).replace('+', ' ');
                }
            }
            
            // If still empty, provide a fallback name based on info hash
            if (info.name.isEmpty()) {
                info.name = QString("Torrent %1").arg(hashString.left(8));
            }
            
            // Store torrent
            {
                QWriteLocker locker(&torrentsLock_);
                torrents_[hashString] = info;
                torrentHandles_[hashString] = handle;
            }
            
            // Update model
            torrentModel_->addTorrent(info);
            
            emit torrentAdded(hashString);
            
            MURMUR_INFO("Torrent added successfully: {}", hashString.toStdString());
            return info;
            
        } catch (const std::exception& e) {
            MURMUR_ERROR("Exception in addTorrent: {}", e.what());
            return makeUnexpected(TorrentError::LibtorrentError);
        }
    });
}

QFuture<Expected<TorrentEngine::TorrentInfo, TorrentError>> 
TorrentEngine::addTorrentFromFile(const QString& torrentFilePath) {
    return QtConcurrent::run([this, torrentFilePath]() -> Expected<TorrentInfo, TorrentError> {
        try {
            // Validate input
            if (!InputValidator::validateFilePath(torrentFilePath) || 
                !InputValidator::isSecurePath(torrentFilePath)) {
                MURMUR_WARN("Invalid torrent file path: {}", torrentFilePath.toStdString());
                return makeUnexpected(TorrentError::SecurityViolation);
            }
            
            QFileInfo fileInfo(torrentFilePath);
            if (!fileInfo.exists() || !fileInfo.isFile()) {
                MURMUR_ERROR("Torrent file does not exist: {}", torrentFilePath.toStdString());
                return makeUnexpected(TorrentError::InvalidTorrentFile);
            }
            
            if (!InputValidator::validateFileSize(fileInfo.size())) {
                return makeUnexpected(TorrentError::SecurityViolation);
            }
            
            // Read the torrent file
            QFile file(torrentFilePath);
            if (!file.open(QIODevice::ReadOnly)) {
                MURMUR_ERROR("Failed to open torrent file: {}", torrentFilePath.toStdString());
                return makeUnexpected(TorrentError::FileSystemError);
            }
            
            QByteArray torrentData = file.readAll();
            file.close();
            
            // Delegate to addTorrentFromData
            return addTorrentFromData(torrentData).result();
            
        } catch (const std::exception& e) {
            MURMUR_ERROR("Exception in addTorrentFromFile: {}", e.what());
            return makeUnexpected(TorrentError::LibtorrentError);
        }
    });
}

QFuture<Expected<TorrentEngine::TorrentInfo, TorrentError>> 
TorrentEngine::addTorrentFromData(const QByteArray& torrentData) {
    return QtConcurrent::run([this, torrentData]() -> Expected<TorrentInfo, TorrentError> {
        try {
            // Validate input
            if (torrentData.isEmpty()) {
                MURMUR_WARN("Empty torrent data");
                return makeUnexpected(TorrentError::InvalidTorrentFile);
            }
            
            if (!InputValidator::validateFileSize(torrentData.size())) {
                return makeUnexpected(TorrentError::SecurityViolation);
            }
            
            // Parse torrent data
            libtorrent::error_code ec;
            auto ti = std::make_shared<libtorrent::torrent_info>(torrentData.constData(), torrentData.size(), ec);
            if (ec) {
                MURMUR_ERROR("Failed to parse torrent file: {}", ec.message());
                return makeUnexpected(TorrentError::InvalidTorrentFile);
            }
            
            libtorrent::add_torrent_params params;
            params.ti = ti;
            
            QString hashString = QString::fromStdString(to_hex_str(params.ti->info_hashes().v1));
            
            {
                QReadLocker locker(&torrentsLock_);
                if (torrents_.contains(hashString)) {
                    MURMUR_INFO("Torrent already exists: {}", hashString.toStdString());
                    return torrents_[hashString];
                }
            }
            
            // Configure the parsed params
            params.save_path = downloadPath_.toStdString();
            params.flags |= libtorrent::torrent_flags::auto_managed;
            
            // Add to session
            libtorrent::torrent_handle handle = session_->add_torrent(params, ec);
            if (ec) {
                MURMUR_ERROR("Failed to add torrent to session: {}", ec.message());
                return makeUnexpected(TorrentError::LibtorrentError);
            }
            
            // Create torrent info
            TorrentInfo info;
            info.infoHash = hashString;
            info.name = QString::fromStdString(params.ti->name());
            info.size = params.ti->total_size();
            info.progress = 0.0;
            info.peers = 0;
            info.downloadRate = 0;
            info.uploadRate = 0;
            info.savePath = downloadPath_;
            info.isSeeding = false;
            info.isPaused = false;
            info.status = "Downloading";
            
            // Extract file list
            auto& files = params.ti->files();
            for (int i = 0; i < files.num_files(); ++i) {
                info.files.append(QString::fromStdString(files.file_path(libtorrent::file_index_t(i))));
            }
            
            // Store in internal data structures
            {
                QWriteLocker locker(&torrentsLock_);
                torrents_[hashString] = info;
                torrentHandles_[hashString] = handle;
            }
            
            // Update model
            torrentModel_->addTorrent(info);
            
            emit torrentAdded(hashString);
            
            MURMUR_INFO("Torrent added from data successfully: {}", hashString.toStdString());
            return info;
            
        } catch (const std::exception& e) {
            MURMUR_ERROR("Exception in addTorrentFromData: {}", e.what());
            return makeUnexpected(TorrentError::LibtorrentError);
        }
    });
}

QFuture<Expected<TorrentEngine::TorrentInfo, TorrentError>> 
TorrentEngine::seedFile(const QString& filePath) {
    return QtConcurrent::run([this, filePath]() -> Expected<TorrentInfo, TorrentError> {
        try {
            // Validate input
            if (!InputValidator::validateFilePath(filePath) || 
                !InputValidator::isSecurePath(filePath)) {
                MURMUR_WARN("Invalid file path: {}", filePath.toStdString());
                return makeUnexpected(TorrentError::SecurityViolation);
            }
            
            QFileInfo fileInfo(filePath);
            if (!fileInfo.exists() || !fileInfo.isFile()) {
                MURMUR_ERROR("File does not exist: {}", filePath.toStdString());
                return makeUnexpected(TorrentError::PermissionDenied);
            }
            
            if (!InputValidator::validateFileSize(fileInfo.size())) {
                return makeUnexpected(TorrentError::SecurityViolation);
            }
            
            // Create torrent
            libtorrent::file_storage fs;
            libtorrent::add_files(fs, fileInfo.filePath().toStdString());
            if (fs.num_files() == 0) {
                 return makeUnexpected(TorrentError::InvalidTorrentFile);
            }
            
            libtorrent::create_torrent creator(fs);
            
            // Add trackers
            auto trackers = Config::instance().getTorrentSettings().trackers;
            for (const QString& tracker : trackers) {
                creator.add_tracker(tracker.toStdString());
            }
            
            creator.set_creator("Murmur Desktop");
            creator.set_comment("Created by Murmur Desktop");
            
            // Generate piece hashes
            libtorrent::error_code ec_hash;
            libtorrent::set_piece_hashes(creator, fileInfo.path().toStdString(), ec_hash);
            if (ec_hash) {
                MURMUR_ERROR("Failed to set piece hashes for {}: {}", filePath.toStdString(), ec_hash.message());
                return makeUnexpected(TorrentError::DiskError);
            }
            
            // Add torrent to session
            libtorrent::add_torrent_params params;

            // Bencode the entry into a buffer first
            libtorrent::entry torrent_entry = creator.generate();
            std::vector<char> buffer;
            libtorrent::bencode(std::back_inserter(buffer), torrent_entry);

            // Use the (char*, size) constructor to avoid template deduction issues
            libtorrent::error_code ec;
            params.ti = std::make_shared<libtorrent::torrent_info>(buffer.data(), buffer.size(), ec);
            if (ec) {
                MURMUR_ERROR("Failed to create torrent_info from buffer: {}", ec.message());
                return makeUnexpected(TorrentError::InvalidTorrentFile);
            }
            
            params.save_path = fileInfo.absolutePath().toStdString();
            params.flags |= libtorrent::torrent_flags::seed_mode;
            params.flags |= libtorrent::torrent_flags::auto_managed;
            
            libtorrent::torrent_handle handle = session_->add_torrent(params, ec);
            if (ec) {
                MURMUR_ERROR("Failed to add seeding torrent: {}", ec.message());
                return makeUnexpected(mapLibtorrentError(ec));
            }
            
            // Create torrent info
            TorrentInfo info = createTorrentInfo(handle);
            info.isSeeding = true;
            
            QString hashString = getInfoHashFromHandle(handle);
            
            // Store torrent
            {
                QWriteLocker locker(&torrentsLock_);
                torrents_[hashString] = info;
                torrentHandles_[hashString] = handle;
            }
            
            // Update model
            torrentModel_->addTorrent(info);
            
            emit torrentAdded(hashString);
            
            MURMUR_INFO("File seeding started: {}", filePath.toStdString());
            return info;
            
        } catch (const std::exception& e) {
            MURMUR_ERROR("Exception in seedFile: {}", e.what());
            return makeUnexpected(TorrentError::LibtorrentError);
        }
    });
}

Expected<void, TorrentError> TorrentEngine::removeTorrent(const QString& infoHash) {
    try {
        QWriteLocker locker(&torrentsLock_);
        
        auto it = torrentHandles_.find(infoHash);
        if (it == torrentHandles_.end()) {
            return makeUnexpected(TorrentError::TorrentNotFound);
        }
        
        // Remove from session
        session_->remove_torrent(it.value());
        
        // Remove from internal storage
        torrents_.remove(infoHash);
        torrentHandles_.erase(it);
        
        // Update model
        torrentModel_->removeTorrent(infoHash);
        
        emit torrentRemoved(infoHash);
        
        MURMUR_INFO("Torrent removed: {}", infoHash.toStdString());
        return {};
        
    } catch (const std::exception& e) {
        MURMUR_ERROR("Exception in removeTorrent: {}", e.what());
        return makeUnexpected(TorrentError::LibtorrentError);
    }
}

Expected<void, TorrentError> TorrentEngine::pauseTorrent(const QString& infoHash) {
    try {
        QReadLocker locker(&torrentsLock_);
        
        auto it = torrentHandles_.find(infoHash);
        if (it == torrentHandles_.end()) {
            return makeUnexpected(TorrentError::TorrentNotFound);
        }
        
        it.value().pause();
        emit torrentPaused(infoHash);
        
        MURMUR_INFO("Torrent paused: {}", infoHash.toStdString());
        return {};
        
    } catch (const std::exception& e) {
        MURMUR_ERROR("Exception in pauseTorrent: {}", e.what());
        return makeUnexpected(TorrentError::LibtorrentError);
    }
}

Expected<void, TorrentError> TorrentEngine::resumeTorrent(const QString& infoHash) {
    try {
        QReadLocker locker(&torrentsLock_);
        
        auto it = torrentHandles_.find(infoHash);
        if (it == torrentHandles_.end()) {
            return makeUnexpected(TorrentError::TorrentNotFound);
        }
        
        it.value().resume();
        emit torrentResumed(infoHash);
        
        MURMUR_INFO("Torrent resumed: {}", infoHash.toStdString());
        return {};
        
    } catch (const std::exception& e) {
        MURMUR_ERROR("Exception in resumeTorrent: {}", e.what());
        return makeUnexpected(TorrentError::LibtorrentError);
    }
}

QList<TorrentEngine::TorrentInfo> TorrentEngine::getActiveTorrents() const {
    QReadLocker locker(&torrentsLock_);
    return torrents_.values();
}

Expected<TorrentEngine::TorrentInfo, TorrentError> 
TorrentEngine::getTorrentInfo(const QString& infoHash) const {
    QReadLocker locker(&torrentsLock_);
    
    auto it = torrents_.find(infoHash);
    if (it == torrents_.end()) {
        return makeUnexpected(TorrentError::TorrentNotFound);
    }
    
    return it.value();
}

bool TorrentEngine::hasTorrent(const QString& infoHash) const {
    QReadLocker locker(&torrentsLock_);
    return torrents_.contains(infoHash);
}

void TorrentEngine::configureSession(int maxConnections, int uploadRate, int downloadRate) {
    if (!session_) return;
    
    libtorrent::settings_pack settings;
    settings.set_int(libtorrent::settings_pack::connections_limit, maxConnections);
    settings.set_int(libtorrent::settings_pack::upload_rate_limit, uploadRate > 0 ? uploadRate * 1024 : -1);
    settings.set_int(libtorrent::settings_pack::download_rate_limit, downloadRate > 0 ? downloadRate * 1024 : -1);
    
    session_->apply_settings(settings);
    
    MURMUR_INFO("Session configured: connections={}, upload={}KB/s, download={}KB/s",
                maxConnections, uploadRate, downloadRate);
}

void TorrentEngine::setDownloadPath(const QString& path) {
    if (InputValidator::validateFilePath(path) && InputValidator::isSecurePath(path)) {
        downloadPath_ = path;
        QDir().mkpath(downloadPath_);
        MURMUR_INFO("Download path set to: {}", path.toStdString());
    } else {
        MURMUR_WARN("Invalid download path: {}", path.toStdString());
    }
}

bool TorrentEngine::isSessionActive() const {
    return sessionActive_ && session_ != nullptr;
}

void TorrentEngine::startSession() {
    if (!sessionActive_) {
        initializeSession();
        alertTimer_->start();
        updateTimer_->start();
        sessionActive_ = true;
        MURMUR_INFO("Torrent session started");
    }
}

void TorrentEngine::stopSession() {
    if (sessionActive_) {
        alertTimer_->stop();
        updateTimer_->stop();
        
        if (session_) {
            session_->pause();
            session_.reset();
        }
        
        sessionActive_ = false;
        MURMUR_INFO("Torrent session stopped");
    }
}

Expected<void, TorrentError> TorrentEngine::initialize() {
    // Wrapper for test compatibility - use existing session initialization
    try {
        if (!isInitialized()) {
            initializeSession();
            startSession();
        }
        return Expected<void, TorrentError>();
    } catch (const std::exception& e) {
        MURMUR_ERROR("TorrentEngine initialization failed: {}", e.what());
        return makeUnexpected(TorrentError::InitializationFailed);
    }
}

bool TorrentEngine::isInitialized() const {
    return session_ != nullptr && sessionActive_;
}

void TorrentEngine::handleLibtorrentAlerts() {
    if (!session_) return;
    
    std::vector<libtorrent::alert*> alerts;
    session_->pop_alerts(&alerts);
    
    for (libtorrent::alert* alert : alerts) {
        handleTorrentAlert(alert);
    }
}

void TorrentEngine::updateTorrentStates() {
    QWriteLocker locker(&torrentsLock_);
    
    for (auto it = torrentHandles_.begin(); it != torrentHandles_.end(); ++it) {
        const QString& infoHash = it.key();
        const libtorrent::torrent_handle& handle = it.value();
        
        if (handle.is_valid()) {
            updateTorrentInfo(infoHash, handle);
            emitTorrentUpdate(infoHash);
        }
    }
}

QString TorrentEngine::getInfoHashFromHandle(const libtorrent::torrent_handle& handle) const {
    if (!handle.is_valid()) return {};
    return QString::fromStdString(to_hex_str(handle.info_hashes().v1));
}

TorrentEngine::TorrentInfo TorrentEngine::createTorrentInfo(const libtorrent::torrent_handle& handle) const {
    TorrentInfo info;
    
    if (!handle.is_valid()) {
        return info;
    }
    
    info.infoHash = getInfoHashFromHandle(handle);
    
    libtorrent::torrent_status status = handle.status();
    
    if (status.has_metadata) {
        auto torrentFile = handle.torrent_file();
        if (torrentFile) {
            info.name = QString::fromStdString(torrentFile->name());
            info.size = torrentFile->total_size();
            
            // Get file list
            for (int i = 0; i < torrentFile->num_files(); ++i) {
                info.files.append(QString::fromStdString(torrentFile->files().file_path(libtorrent::file_index_t(i))));
            }
        }
    }
    
    info.progress = status.progress;
    info.peers = status.num_peers;
    info.downloadRate = status.download_payload_rate;
    info.uploadRate = status.upload_payload_rate;
    info.isPaused = (status.flags & libtorrent::torrent_flags::paused) != 0;
    info.savePath = QString::fromStdString(status.save_path);
    
    // Determine status
    if (status.flags & libtorrent::torrent_flags::paused) {
        info.status = "Paused";
    } else if (status.state == libtorrent::torrent_status::seeding) {
        info.status = "Seeding";
        info.isSeeding = true;
    } else if (status.state == libtorrent::torrent_status::downloading) {
        info.status = "Downloading";
    } else if (status.state == libtorrent::torrent_status::checking_files) {
        info.status = "Checking";
    } else {
        info.status = "Connecting";
    }
    
    return info;
}

void TorrentEngine::updateTorrentInfo(const QString& infoHash, const libtorrent::torrent_handle& handle) {
    auto it = torrents_.find(infoHash);
    if (it != torrents_.end()) {
        TorrentInfo updated = createTorrentInfo(handle);
        updated.magnetUri = it.value().magnetUri; // Preserve magnet URI
        torrents_[infoHash] = updated;
    }
}

void TorrentEngine::emitTorrentUpdate(const QString& infoHash) {
    auto it = torrents_.find(infoHash);
    if (it != torrents_.end()) {
        emit torrentProgress(infoHash, it.value().progress);
        torrentModel_->updateTorrent(it.value());
    }
}

void TorrentEngine::initializeSession() {
    try {
        libtorrent::settings_pack settings;
        
        // Basic settings
        settings.set_str(libtorrent::settings_pack::user_agent, "Murmur Desktop/1.0");
        settings.set_bool(libtorrent::settings_pack::enable_dht, true);
        settings.set_bool(libtorrent::settings_pack::enable_lsd, true);
        settings.set_bool(libtorrent::settings_pack::enable_upnp, true);
        settings.set_bool(libtorrent::settings_pack::enable_natpmp, true);
        
        // Performance settings
        settings.set_int(libtorrent::settings_pack::alert_mask, 
                        libtorrent::alert_category::error |
                        libtorrent::alert_category::status |
                        libtorrent::alert_category::storage |
                        libtorrent::alert_category::stats);
        
        session_ = std::make_unique<libtorrent::session>(settings);
        
        configureSessionSettings();
        addDefaultTrackers();
        
        MURMUR_INFO("LibTorrent session initialized");
        
    } catch (const std::exception& e) {
        MURMUR_ERROR("Failed to initialize session: {}", e.what());
    }
}

void TorrentEngine::configureSessionSettings() {
    auto config = Config::instance().getTorrentSettings();
    configureSession(config.maxConnections, config.uploadRateLimit, config.downloadRateLimit);
}

void TorrentEngine::addDefaultTrackers() {
    // Default trackers are added per-torrent in add_torrent_params
}

TorrentError TorrentEngine::mapLibtorrentError(const libtorrent::error_code& ec) const {
    if (ec == libtorrent::errors::invalid_torrent_handle) {
        return TorrentError::TorrentNotFound;
    } else if (ec.category() == libtorrent::system_category()) {
        return TorrentError::NetworkFailure;
    } else {
        return TorrentError::LibtorrentError;
    }
}

void TorrentEngine::handleTorrentAlert(const libtorrent::alert* alert) {
    switch (alert->type()) {
        case libtorrent::torrent_finished_alert::alert_type: {
            auto* finished = libtorrent::alert_cast<libtorrent::torrent_finished_alert>(alert);
            if (finished) {
                QString infoHash = getInfoHashFromHandle(finished->handle);
                emit torrentCompleted(infoHash);
                MURMUR_INFO("Torrent completed: {}", infoHash.toStdString());
            }
            break;
        }
        
        case libtorrent::torrent_error_alert::alert_type: {
            auto* error = libtorrent::alert_cast<libtorrent::torrent_error_alert>(alert);
            if (error) {
                QString infoHash = getInfoHashFromHandle(error->handle);
                TorrentError errorType = mapLibtorrentError(error->error);
                emit torrentError(infoHash, errorType);
                MURMUR_ERROR("Torrent error: {} - {}", infoHash.toStdString(), error->error.message());
            }
            break;
        }
        
        default:
            break;
    }
}

} // namespace Murmur