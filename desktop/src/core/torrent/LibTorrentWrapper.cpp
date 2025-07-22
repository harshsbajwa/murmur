#include "LibTorrentWrapper.hpp"
#include "../common/Logger.hpp"
#include "../storage/StorageManager.hpp"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QStorageInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QDirIterator>
#include <QCryptographicHash>

// LibTorrent includes
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/bdecode.hpp> 
#include <libtorrent/bencode.hpp> 
#include <libtorrent/alert_types.hpp>
#include <libtorrent/session_stats.hpp> 
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/sha1_hash.hpp>
#include <libtorrent/aux_/session_settings.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/session.hpp>
#include <iomanip>
#include <sstream>

namespace Murmur {

// Helper function to convert SHA1 hash to hex string
std::string to_hex(const libtorrent::sha1_hash& hash) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char i : hash) {
        oss << std::setw(2) << static_cast<int>(i);
    }
    return oss.str();
}

struct LibTorrentWrapper::LibTorrentWrapperPrivate {
    std::unique_ptr<libtorrent::session> session;
    QHash<QString, libtorrent::torrent_handle> torrents;
    QTimer* alertTimer = nullptr;
    QTimer* statsTimer = nullptr;
    mutable QMutex torrentsMutex;
    
    // Storage manager for persistence
    StorageManager* storageManager = nullptr;
    
    // Configuration
    TorrentSettings currentSettings;
    bool initialized = false;
    
    // Statistics
    SessionStats lastStats;
    QDateTime sessionStartTime;
    // Indices for the stats API
    int stats_idx_total_download = -1;
    int stats_idx_total_upload = -1;
    int stats_idx_dht_nodes = -1;
};

LibTorrentWrapper::LibTorrentWrapper(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<LibTorrentWrapperPrivate>()) {
    
    d->sessionStartTime = QDateTime::currentDateTime();
    
    // Create alert processing timer
    d->alertTimer = new QTimer(this);
    d->alertTimer->setInterval(100); // Process alerts every 100ms
    connect(d->alertTimer, &QTimer::timeout, this, &LibTorrentWrapper::processAlerts);
    
    // Create statistics update timer
    d->statsTimer = new QTimer(this);
    d->statsTimer->setInterval(1000); // Update stats every second
    connect(d->statsTimer, &QTimer::timeout, this, &LibTorrentWrapper::updateStatistics);
    
    Logger::instance().info( "LibTorrentWrapper initialized");
}

LibTorrentWrapper::~LibTorrentWrapper() {
    shutdown();
    Logger::instance().info( "LibTorrentWrapper destroyed");
}

void LibTorrentWrapper::setStorageManager(StorageManager* storage) {
    d->storageManager = storage;
}

Expected<bool, TorrentError> LibTorrentWrapper::initialize(const TorrentSettings& settings) {
    if (d->initialized) {
        Logger::instance().warn( "Session already initialized");
        return true;
    }
    
    auto sessionResult = initializeSession(settings);
    if (sessionResult.hasError()) {
        return sessionResult;
    }
    
    auto configResult = configureSession(settings);
    if (configResult.hasError()) {
        return configResult;
    }
    
    setupAlertHandling();
    
    d->currentSettings = settings;
    d->initialized = true;
    
    // Start timers
    d->alertTimer->start();
    d->statsTimer->start();
    
    Logger::instance().info( "LibTorrent session initialized successfully");
    return true;
}

void LibTorrentWrapper::shutdown() {
    if (!d->initialized) {
        return;
    }
    
    // Stop timers
    d->alertTimer->stop();
    d->statsTimer->stop();
    
    // Save session state if needed
    if (d->session) {
        cleanupSession();
        d->session.reset();
    }
    
    d->initialized = false;
    Logger::instance().info( "LibTorrent session shutdown");
}

bool LibTorrentWrapper::isInitialized() const {
    return d->initialized;
}

Expected<QString, TorrentError> LibTorrentWrapper::addMagnetLink(
    const QString& magnetLink,
    const QString& savePath,
    const TorrentSettings& settings) {
    
    if (!d->initialized) {
        return makeUnexpected(TorrentError::SessionError);
    }
    
    auto validateResult = validateMagnetLink(magnetLink);
    if (validateResult.hasError()) {
        return makeUnexpected(validateResult.error());
    }
    
    try {
        libtorrent::add_torrent_params params;
        libtorrent::error_code ec;
        
        // Parse magnet link
        libtorrent::parse_magnet_uri(magnetLink.toStdString(), params, ec);
        if (ec) {
            Logger::instance().error("Failed to parse magnet link: {}", ec.message());
            return makeUnexpected(TorrentError::InvalidMagnetLink);
        }
        
        // Set save path
        QString finalSavePath = savePath.isEmpty() ? d->currentSettings.downloadPath : savePath;
        if (finalSavePath.isEmpty()) {
            finalSavePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        }
        
        auto pathResult = validateAndPreparePath(finalSavePath);
        if (pathResult.hasError()) {
            return makeUnexpected(pathResult.error());
        }
        
        params.save_path = finalSavePath.toStdString();
        
        // Apply torrent settings
        applyTorrentSettings(params, settings);
        
        // Add torrent to session
        d->session->async_add_torrent(params);
        
        // Extract info hash
        QString infoHash = QString::fromStdString(to_hex(params.info_hashes.v1));
        if (infoHash.isEmpty()) {
            Logger::instance().error( "Failed to extract info hash from magnet link");
            return makeUnexpected(TorrentError::UnknownError);
        }
        
        Logger::instance().info("Adding magnet link: {}", infoHash.toStdString());
        
        return infoHash;
        
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in addMagnetLink: {}", e.what());
        return makeUnexpected(TorrentError::UnknownError);
    }
}

Expected<QString, TorrentError> LibTorrentWrapper::addTorrentFile(
    const QString& torrentFile,
    const QString& savePath,
    const TorrentSettings& settings) {
    
    if (!d->initialized) {
        return makeUnexpected(TorrentError::SessionError);
    }
    
    // Read torrent file
    QFile file(torrentFile);
    if (!file.open(QIODevice::ReadOnly)) {
        Logger::instance().error("Cannot open torrent file: {}", torrentFile.toStdString());
        return makeUnexpected(TorrentError::InvalidTorrentFile);
    }
    
    QByteArray torrentData = file.readAll();
    file.close();
    
    return addTorrentData(torrentData, savePath, settings);
}

Expected<QString, TorrentError> LibTorrentWrapper::addTorrentData(
    const QByteArray& torrentData,
    const QString& savePath,
    const TorrentSettings& settings) {
    
    if (!d->initialized) {
        return makeUnexpected(TorrentError::SessionError);
    }
    
    auto validateResult = validateTorrentData(torrentData);
    if (validateResult.hasError()) {
        return makeUnexpected(validateResult.error());
    }
    
    try {
        libtorrent::error_code ec;
        
        // Create torrent info from data
        auto torrentInfo = std::make_shared<libtorrent::torrent_info>(
            torrentData.constData(), torrentData.size(), ec);
        
        if (ec) {
            Logger::instance().error("Failed to parse torrent data: {}", ec.message());
            return makeUnexpected(TorrentError::InvalidTorrentFile);
        }
        
        // Set up add_torrent_params
        libtorrent::add_torrent_params params;
        params.ti = torrentInfo;
        
        // Set save path
        QString finalSavePath = savePath.isEmpty() ? d->currentSettings.downloadPath : savePath;
        if (finalSavePath.isEmpty()) {
            finalSavePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        }
        
        auto pathResult = validateAndPreparePath(finalSavePath);
        if (pathResult.hasError()) {
            return makeUnexpected(pathResult.error());
        }
        
        params.save_path = finalSavePath.toStdString();
        
        // Apply torrent settings
        applyTorrentSettings(params, settings);
        
        // Check if torrent already exists
        QString infoHash = QString::fromStdString(to_hex(torrentInfo->info_hashes().v1));
        if (hasTorrent(infoHash)) {
            Logger::instance().warn("Torrent already exists: {}", infoHash.toStdString());
            return makeUnexpected(TorrentError::DuplicateTorrent);
        }
        
        // Check disk space
        auto spaceResult = checkDiskSpace(finalSavePath, torrentInfo->total_size());
        if (spaceResult.hasError()) {
            return makeUnexpected(spaceResult.error());
        }
        
        // Add torrent to session asynchronously
        d->session->async_add_torrent(params);
        
        Logger::instance().info("Adding torrent: {} ({})", infoHash.toStdString(), torrentInfo->name());
        
        return infoHash;
        
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in addTorrentData: {}", e.what());
        return makeUnexpected(TorrentError::UnknownError);
    }
}

Expected<QByteArray, TorrentError> LibTorrentWrapper::createTorrent(
    const QString& sourcePath,
    const QStringList& trackers,
    const QString& comment,
    const QString& creator,
    bool isPrivate) {
    
    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists()) {
        return makeUnexpected(TorrentError::InvalidTorrentFile);
    }
    
    try {
        libtorrent::file_storage fs;
        
        // Use member function on fs, and add_files for directories
        if (sourceInfo.isFile()) {
            fs.add_file(sourcePath.toStdString(), sourceInfo.size());
        } else if (sourceInfo.isDir()) {
            libtorrent::add_files(fs, sourcePath.toStdString());
        } else {
            return makeUnexpected(TorrentError::InvalidTorrentFile);
        }
        
        if (fs.num_files() == 0) {
            Logger::instance().error("No files found to create torrent from {}", sourcePath.toStdString());
            return makeUnexpected(TorrentError::InvalidTorrentFile);
        }
        
        libtorrent::create_torrent ct(fs);
        
        // Add trackers
        for (const QString& tracker : trackers) {
            ct.add_tracker(tracker.toStdString());
        }
        
        // Set metadata
        if (!comment.isEmpty()) {
            ct.set_comment(comment.toStdString().c_str());
        }
        if (!creator.isEmpty()) {
            ct.set_creator(creator.toStdString().c_str());
        }
        ct.set_priv(isPrivate);
        
        // Generate piece hashes
        libtorrent::error_code ec;
        set_piece_hashes(ct, sourceInfo.isDir() ? sourceInfo.filePath().toStdString() : sourceInfo.path().toStdString(), ec);
        if (ec) {
            Logger::instance().error("Failed to set piece hashes: {}", ec.message());
            return makeUnexpected(TorrentError::DiskError);
        }
        
        // Generate torrent data
        std::vector<char> torrentData;
        libtorrent::bencode(std::back_inserter(torrentData), ct.generate());
        
        QByteArray result(torrentData.data(), torrentData.size());
        
        Logger::instance().info("Created torrent for: {} ({} bytes)", sourcePath.toStdString(), result.size());
        return result;
        
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in createTorrent: {}", e.what());
        return makeUnexpected(TorrentError::UnknownError);
    }
}

Expected<bool, TorrentError> LibTorrentWrapper::removeTorrent(const QString& infoHash, bool deleteFiles) {
    if (!d->initialized) {
        return makeUnexpected(TorrentError::SessionError);
    }
    
    libtorrent::torrent_handle* handle = findTorrent(infoHash);
    if (!handle) {
        return makeUnexpected(TorrentError::TorrentNotFound);
    }
    
    try {
        // Get torrent name before removing
        QString torrentName = QString::fromStdString(handle->status().name);
        
        // Remove from session
        libtorrent::remove_flags_t flags = deleteFiles ? libtorrent::session_handle::delete_files : libtorrent::remove_flags_t{};
        d->session->remove_torrent(*handle, flags);
        
        // Remove from our tracking
        {
            QMutexLocker locker(&d->torrentsMutex);
            d->torrents.remove(infoHash);
        }
        
        emit torrentRemoved(infoHash);
        Logger::instance().info("Removed torrent: {} ({})", infoHash.toStdString(), torrentName.toStdString());
        
        return true;
        
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in removeTorrent: {}", e.what());
        return makeUnexpected(TorrentError::UnknownError);
    }
}

Expected<bool, TorrentError> LibTorrentWrapper::pauseTorrent(const QString& infoHash) {
    libtorrent::torrent_handle* handle = findTorrent(infoHash);
    if (!handle) {
        return makeUnexpected(TorrentError::TorrentNotFound);
    }
    
    try {
        handle->pause();
        Logger::instance().info("Paused torrent: {}", infoHash.toStdString());
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in pauseTorrent: {}", e.what());
        return makeUnexpected(TorrentError::UnknownError);
    }
}

Expected<bool, TorrentError> LibTorrentWrapper::resumeTorrent(const QString& infoHash) {
    libtorrent::torrent_handle* handle = findTorrent(infoHash);
    if (!handle) {
        return makeUnexpected(TorrentError::TorrentNotFound);
    }
    
    try {
        handle->resume();
        Logger::instance().info("Resumed torrent: {}", infoHash.toStdString());
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in resumeTorrent: {}", e.what());
        return makeUnexpected(TorrentError::UnknownError);
    }
}

Expected<bool, TorrentError> LibTorrentWrapper::recheckTorrent(const QString& infoHash) {
    libtorrent::torrent_handle* handle = findTorrent(infoHash);
    if (!handle) {
        return makeUnexpected(TorrentError::TorrentNotFound);
    }
    
    try {
        handle->force_recheck();
        Logger::instance().info("Force recheck torrent: {}", infoHash.toStdString());
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in recheckTorrent: {}", e.what());
        return makeUnexpected(TorrentError::UnknownError);
    }
}

Expected<bool, TorrentError> LibTorrentWrapper::moveTorrent(const QString& infoHash, const QString& newPath) {
    libtorrent::torrent_handle* handle = findTorrent(infoHash);
    if (!handle) {
        return makeUnexpected(TorrentError::TorrentNotFound);
    }
    
    auto pathResult = validateAndPreparePath(newPath);
    if (pathResult.hasError()) {
        return makeUnexpected(pathResult.error());
    }
    
    try {
        handle->move_storage(newPath.toStdString());
        Logger::instance().info("Moving torrent {} to: {}", infoHash.toStdString(), newPath.toStdString());
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in moveTorrent: {}", e.what());
        return makeUnexpected(TorrentError::UnknownError);
    }
}

Expected<bool, TorrentError> LibTorrentWrapper::setFilePriorities(const QString& infoHash, const QList<int>& priorities) {
    libtorrent::torrent_handle* handle = findTorrent(infoHash);
    if (!handle) {
        return makeUnexpected(TorrentError::TorrentNotFound);
    }
    
    try {
        std::vector<libtorrent::download_priority_t> ltPriorities;
        for (int priority : priorities) {
            ltPriorities.push_back(libtorrent::download_priority_t(qBound(0, priority, 7)));
        }
        
        handle->prioritize_files(ltPriorities);
        Logger::instance().info("Set file priorities for torrent: {}", infoHash.toStdString());
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in setFilePriorities: {}", e.what());
        return makeUnexpected(TorrentError::UnknownError);
    }
}

Expected<TorrentStats, TorrentError> LibTorrentWrapper::getTorrentStats(const QString& infoHash) const {
    libtorrent::torrent_handle* handle = findTorrent(infoHash);
    if (!handle) {
        return makeUnexpected(TorrentError::TorrentNotFound);
    }
    
    try {
        return extractTorrentStats(*handle);
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in getTorrentStats: {}", e.what());
        return makeUnexpected(TorrentError::UnknownError);
    }
}

QList<TorrentStats> LibTorrentWrapper::getAllTorrentStats() const {
    QList<TorrentStats> allStats;
    
    QMutexLocker locker(&d->torrentsMutex);
    for (auto it = d->torrents.begin(); it != d->torrents.end(); ++it) {
        try {
            TorrentStats stats = extractTorrentStats(it.value());
            allStats.append(stats);
        } catch (const std::exception& e) {
            Logger::instance().warn("Failed to get stats for torrent {}: {}", it.key().toStdString(), e.what());
        }
    }
    
    return allStats;
}

SessionStats LibTorrentWrapper::getSessionStats() const {
    return d->lastStats;
}

QStringList LibTorrentWrapper::getTorrentList() const {
    QMutexLocker locker(&d->torrentsMutex);
    return d->torrents.keys();
}

bool LibTorrentWrapper::hasTorrent(const QString& infoHash) const {
    QMutexLocker locker(&d->torrentsMutex);
    return d->torrents.contains(infoHash);
}

Expected<bool, TorrentError> LibTorrentWrapper::updateSettings(const TorrentSettings& settings) {
    if (!d->initialized) {
        return makeUnexpected(TorrentError::SessionError);
    }
    
    auto configResult = configureSession(settings);
    if (configResult.hasError()) {
        return configResult;
    }
    
    d->currentSettings = settings;
    Logger::instance().info( "Session settings updated");
    return true;
}

TorrentSettings LibTorrentWrapper::getCurrentSettings() const {
    return d->currentSettings;
}

Expected<bool, TorrentError> LibTorrentWrapper::saveSessionState(const QString& filePath) {
    if (!d->initialized) {
        return makeUnexpected(TorrentError::SessionError);
    }
    
    try {
        // Save session state
        libtorrent::session_params params = d->session->session_state();
        
        std::vector<char> state_buf = libtorrent::write_session_params_buf(params);
        
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            return makeUnexpected(TorrentError::PermissionDenied);
        }
        
        file.write(state_buf.data(), state_buf.size());
        file.close();
        
        Logger::instance().info("Session state saved to: {}", filePath.toStdString());
        return true;
        
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in saveSessionState: {}", e.what());
        return makeUnexpected(TorrentError::UnknownError);
    }
}

Expected<bool, TorrentError> LibTorrentWrapper::loadSessionState(const QString& filePath) {
    if (!d->initialized) {
        return makeUnexpected(TorrentError::SessionError);
    }
    
    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return makeUnexpected(TorrentError::FileSystemError);
    }
    
    try {
        QByteArray state_buf = file.readAll();
        file.close();

        libtorrent::error_code ec;
        libtorrent::bdecode_node n;
        
        int ret = libtorrent::bdecode(state_buf.constData(), state_buf.constData() + state_buf.size(), n, ec);
        if (ret != 0) {
            Logger::instance().error("Failed to decode session state: {}", ec.message());
            return makeUnexpected(TorrentError::ParseError);
        }

        libtorrent::session_params params = libtorrent::read_session_params(n);
        d->session->apply_settings(params.settings);
        d->session->set_dht_state(params.dht_state);
        
        // Restore torrents from storage manager
        if (d->storageManager) {
            auto torrentListResult = d->storageManager->getAllTorrents();
            if (torrentListResult) {
                const auto& torrents = torrentListResult.value();
                Logger::instance().info("Restoring {} torrents from storage", torrents.size());
                
                for (const auto& torrentRecord : torrents) {
                    try {
                        // Reconstruct add_torrent_params from stored data
                        libtorrent::add_torrent_params atp;
                        
                        if (!torrentRecord.magnetUri.isEmpty()) {
                            // Restore from magnet link
                            libtorrent::error_code ec;
                            atp = libtorrent::parse_magnet_uri(torrentRecord.magnetUri.toStdString(), ec);
                            if (ec) {
                                Logger::instance().warn("Failed to parse stored magnet link for {}: {}", 
                                                       torrentRecord.infoHash.toStdString(), ec.message());
                                continue;
                            }
                        } else {
                            // Try to reconstruct from metadata if available
                            auto metadataDoc = QJsonDocument::fromJson(QJsonDocument(torrentRecord.metadata).toJson());
                            if (metadataDoc.isObject()) {
                                auto metadataObj = metadataDoc.object();
                                if (metadataObj.contains("torrent_data")) {
                                    QByteArray torrentData = QByteArray::fromBase64(metadataObj["torrent_data"].toString().toUtf8());
                                    libtorrent::error_code ec;
                                    atp.ti = std::make_shared<libtorrent::torrent_info>(
                                        torrentData.constData(), torrentData.size(), ec);
                                    if (ec) {
                                        Logger::instance().warn("Failed to parse stored torrent data for {}: {}", 
                                                               torrentRecord.infoHash.toStdString(), ec.message());
                                        continue;
                                    }
                                }
                            }
                        }
                        
                        // Set save path
                        atp.save_path = torrentRecord.savePath.toStdString();
                        
                        // Restore state from torrent record
                        if (torrentRecord.status == "paused") {
                            atp.flags |= libtorrent::torrent_flags::paused;
                        }
                        
                        // Add torrent to session
                        libtorrent::torrent_handle handle = d->session->add_torrent(atp);
                        if (handle.is_valid()) {
                            QMutexLocker locker(&d->torrentsMutex);
                            d->torrents[torrentRecord.infoHash] = handle;
                            Logger::instance().debug("Restored torrent: {}", torrentRecord.name.toStdString());
                        }
                        
                    } catch (const std::exception& e) {
                        Logger::instance().warn("Failed to restore torrent {}: {}", 
                                               torrentRecord.infoHash.toStdString(), e.what());
                    }
                }
            } else {
                Logger::instance().warn("Failed to load torrents from storage: {}", 
                                       static_cast<int>(torrentListResult.error()));
            }
        } else {
            Logger::instance().info("StorageManager not set, skipping torrent restoration");
        }

        Logger::instance().info("Session state (settings, DHT) loaded from: {}", filePath.toStdString());
        return true;
        
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in loadSessionState: {}", e.what());
        return makeUnexpected(TorrentError::UnknownError);
    }
}

// Static utility methods

Expected<QJsonObject, TorrentError> LibTorrentWrapper::parseMagnetLink(const QString& magnetLink) {
    if (!isValidMagnetLink(magnetLink)) {
        return makeUnexpected(TorrentError::InvalidMagnetLink);
    }
    
    QUrl url(magnetLink);
    QUrlQuery query(url);
    
    QJsonObject result;
    result["urn"] = query.queryItemValue("xt");
    result["name"] = query.queryItemValue("dn");
    result["trackers"] = QJsonArray::fromStringList(query.allQueryItemValues("tr"));
    
    return result;
}

Expected<QJsonObject, TorrentError> LibTorrentWrapper::parseTorrentData(const QByteArray& torrentData) {
    try {
        libtorrent::error_code ec;
        auto torrentInfo = std::make_shared<libtorrent::torrent_info>(
            torrentData.constData(), torrentData.size(), ec);
        
        if (ec) {
            return makeUnexpected(TorrentError::ParseError);
        }
        
        QJsonObject result;
        result["name"] = QString::fromStdString(torrentInfo->name());
        result["infoHash"] = QString::fromStdString(to_hex(torrentInfo->info_hashes().v1));
        result["totalSize"] = static_cast<qint64>(torrentInfo->total_size());
        result["numFiles"] = torrentInfo->num_files();
        result["numPieces"] = torrentInfo->num_pieces();
        result["pieceLength"] = torrentInfo->piece_length();
        
        // Add file list
        QJsonArray files;
        for (int i = 0; i < torrentInfo->num_files(); ++i) {
            QJsonObject fileObj;
            fileObj["path"] = QString::fromStdString(torrentInfo->files().file_path(libtorrent::file_index_t(i)));
            fileObj["size"] = static_cast<qint64>(torrentInfo->files().file_size(libtorrent::file_index_t(i)));
            files.append(fileObj);
        }
        result["files"] = files;
        
        return result;
        
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in parseTorrentData: {}", e.what());
        return makeUnexpected(TorrentError::ParseError);
    }
}

Expected<QString, TorrentError> LibTorrentWrapper::calculateInfoHash(const QByteArray& torrentData) {
    try {
        libtorrent::error_code ec;
        auto torrentInfo = std::make_shared<libtorrent::torrent_info>(
            torrentData.constData(), torrentData.size(), ec);
        
        if (ec) {
            return makeUnexpected(TorrentError::ParseError);
        }
        
        return QString::fromStdString(to_hex(torrentInfo->info_hashes().v1));
        
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in calculateInfoHash: {}", e.what());
        return makeUnexpected(TorrentError::ParseError);
    }
}

bool LibTorrentWrapper::isValidMagnetLink(const QString& magnetLink) {
    QRegularExpression regex(R"(^magnet:\?xt=urn:btih:[a-fA-F0-9]{40}.*$)");
    return regex.match(magnetLink).hasMatch();
}

QString LibTorrentWrapper::getLibTorrentVersion() {
    return QString::fromStdString(libtorrent::version());
}

// Private implementation methods

Expected<bool, TorrentError> LibTorrentWrapper::initializeSession(const TorrentSettings& settings) {
    try {
        // Find and cache the metric indices for performance counters
        d->stats_idx_total_download = libtorrent::find_metric_idx("net.recv_payload_bytes");
        d->stats_idx_total_upload = libtorrent::find_metric_idx("net.sent_payload_bytes");
        d->stats_idx_dht_nodes = libtorrent::find_metric_idx("dht.nodes");

        libtorrent::settings_pack pack;
        
        // Basic settings
        pack.set_str(libtorrent::settings_pack::user_agent, settings.userAgent.toStdString());
        pack.set_bool(libtorrent::settings_pack::enable_dht, settings.enableDHT);
        pack.set_bool(libtorrent::settings_pack::enable_lsd, settings.enableLSD);
        pack.set_bool(libtorrent::settings_pack::enable_upnp, settings.enableUPnP);
        pack.set_bool(libtorrent::settings_pack::enable_natpmp, settings.enableNATPMP);
        
        // Rate limits
        pack.set_int(libtorrent::settings_pack::download_rate_limit, settings.maxDownloadRate > 0 ? settings.maxDownloadRate * 1024 : -1);
        pack.set_int(libtorrent::settings_pack::upload_rate_limit, settings.maxUploadRate > 0 ? settings.maxUploadRate * 1024 : -1);
        
        // Connection limits
        pack.set_int(libtorrent::settings_pack::connections_limit, settings.maxConnections);
        pack.set_int(libtorrent::settings_pack::active_downloads, settings.maxSeeds);
        
        // Create session
        d->session = std::make_unique<libtorrent::session>(pack);
        
        Logger::instance().info( "LibTorrent session created");
        return true;
        
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in initializeSession: {}", e.what());
        return makeUnexpected(TorrentError::SessionError);
    }
}

Expected<bool, TorrentError> LibTorrentWrapper::configureSession(const TorrentSettings& settings) {
    if (!d->session) {
        return makeUnexpected(TorrentError::SessionError);
    }
    
    try {
        applySessionSettings(settings);
        Logger::instance().info( "Session configuration applied");
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in configureSession: {}", e.what());
        return makeUnexpected(TorrentError::SessionError);
    }
}

void LibTorrentWrapper::setupAlertHandling() {
    if (!d->session) {
        return;
    }
    
    // Set alert mask to receive all relevant alerts
    libtorrent::settings_pack pack;
    pack.set_int(libtorrent::settings_pack::alert_mask,
        libtorrent::alert_category::error |
        libtorrent::alert_category::peer |
        libtorrent::alert_category::port_mapping |
        libtorrent::alert_category::storage |
        libtorrent::alert_category::tracker |
        libtorrent::alert_category::connect |
        libtorrent::alert_category::status |
        libtorrent::alert_category::stats
    );
    d->session->apply_settings(pack);
}

void LibTorrentWrapper::processAlerts() {
    if (!d->session) {
        return;
    }
    
    std::vector<libtorrent::alert*> alerts;
    d->session->pop_alerts(&alerts);
    
    for (auto* alert : alerts) {
        processAlert(alert);
    }
}

void LibTorrentWrapper::processAlert(const libtorrent::alert* alert) {
    if (!alert) return;
    
    try {
        switch (alert->type()) {
            case libtorrent::add_torrent_alert::alert_type: {
                auto* addedAlert = libtorrent::alert_cast<libtorrent::add_torrent_alert>(alert);
                if (addedAlert) {
                    QString infoHash = extractInfoHash(addedAlert->handle);
                    if (addedAlert->handle.is_valid()) {
                        {
                            QMutexLocker locker(&d->torrentsMutex);
                            d->torrents[infoHash] = addedAlert->handle;
                        }
                        
                        // Save torrent to storage manager
                        if (d->storageManager) {
                            try {
                                auto torrentStatus = addedAlert->handle.status();
                                auto torrentInfo = addedAlert->handle.torrent_file();
                                
                                TorrentRecord record;
                                record.infoHash = infoHash;
                                record.name = QString::fromStdString(torrentStatus.name);
                                record.size = torrentInfo ? torrentInfo->total_size() : 0;
                                record.dateAdded = QDateTime::currentDateTime();
                                record.lastActive = QDateTime::currentDateTime();
                                record.savePath = QString::fromStdString(torrentStatus.save_path);
                                record.progress = 0.0; // Will be updated by progress alerts
                                record.status = "downloading";
                                record.seeders = 0;
                                record.leechers = 0;
                                record.downloaded = 0;
                                record.uploaded = 0;
                                record.ratio = 0.0;
                                
                                // Store metadata including torrent data if available
                                QJsonObject metadata;
                                if (torrentInfo) {
                                    // Save torrent data for later restoration
                                    std::vector<char> torrentBuffer;
                                    libtorrent::bencode(std::back_inserter(torrentBuffer), 
                                                      libtorrent::create_torrent(*torrentInfo).generate());
                                    QByteArray torrentData(torrentBuffer.data(), torrentBuffer.size());
                                    metadata["torrent_data"] = QString::fromUtf8(torrentData.toBase64());
                                    
                                    // Store file list
                                    QStringList files;
                                    for (int i = 0; i < torrentInfo->num_files(); ++i) {
                                        files.append(QString::fromStdString(torrentInfo->files().file_path(i)));
                                    }
                                    record.files = files;
                                }
                                
                                // Check if this is a magnet link (no torrent_info initially)
                                if (!torrentInfo) {
                                    // This is likely a magnet link, try to get the magnet URI
                                    auto magnetUri = libtorrent::make_magnet_uri(addedAlert->handle);
                                    if (!magnetUri.empty()) {
                                        record.magnetUri = QString::fromStdString(magnetUri);
                                    }
                                }
                                
                                record.metadata = metadata;
                                
                                auto saveResult = d->storageManager->addTorrent(record);
                                if (!saveResult) {
                                    Logger::instance().warn("Failed to save torrent to storage: {}", 
                                                           static_cast<int>(saveResult.error()));
                                } else {
                                    Logger::instance().debug("Torrent {} saved to storage", record.name.toStdString());
                                }
                                
                            } catch (const std::exception& e) {
                                Logger::instance().warn("Exception while saving torrent to storage: {}", e.what());
                            }
                        }
                        
                        QString name = QString::fromStdString(addedAlert->handle.status().name);
                        emit torrentAdded(infoHash, name);
                    }
                }
                break;
            }
            
            case libtorrent::torrent_removed_alert::alert_type: {
                auto* removedAlert = libtorrent::alert_cast<libtorrent::torrent_removed_alert>(alert);
                if (removedAlert) {
                    QString infoHash = QString::fromStdString(to_hex(removedAlert->info_hashes.v1));
                    emit torrentRemoved(infoHash);
                }
                break;
            }
            
            case libtorrent::state_changed_alert::alert_type: {
                auto* stateAlert = libtorrent::alert_cast<libtorrent::state_changed_alert>(alert);
                if (stateAlert) {
                    QString infoHash = extractInfoHash(stateAlert->handle);
                    TorrentState oldState = mapTorrentState(stateAlert->prev_state);
                    TorrentState newState = mapTorrentState(stateAlert->state);
                    emit torrentStateChanged(infoHash, oldState, newState);
                }
                break;
            }
            
            case libtorrent::torrent_finished_alert::alert_type: {
                auto* finishedAlert = libtorrent::alert_cast<libtorrent::torrent_finished_alert>(alert);
                if (finishedAlert) {
                    QString infoHash = extractInfoHash(finishedAlert->handle);
                    emit torrentFinished(infoHash);
                }
                break;
            }
            
            case libtorrent::tracker_error_alert::alert_type: {
                auto* trackerAlert = libtorrent::alert_cast<libtorrent::tracker_error_alert>(alert);
                if (trackerAlert) {
                    QString infoHash = extractInfoHash(trackerAlert->handle);
                    QString tracker = QString::fromStdString(trackerAlert->tracker_url());
                    QString error = QString::fromStdString(trackerAlert->error.message());
                    emit trackerError(infoHash, tracker, error);
                }
                break;
            }
            
            case libtorrent::session_stats_alert::alert_type: {
                auto* stats_alert = libtorrent::alert_cast<libtorrent::session_stats_alert>(alert);
                if (stats_alert) {
                    auto counters = stats_alert->counters();
                    // Use cached indices to access stats counters
                    if (d->stats_idx_total_download != -1)
                        d->lastStats.totalDownloaded = counters[d->stats_idx_total_download];
                    if (d->stats_idx_total_upload != -1)
                        d->lastStats.totalUploaded = counters[d->stats_idx_total_upload];
                    if (d->stats_idx_dht_nodes != -1)
                        d->lastStats.dhtNodes = counters[d->stats_idx_dht_nodes];
                }
                break;
            }

            default:
                break;
        }
    } catch (const std::exception& e) {
        Logger::instance().warn("Exception processing alert: {}", e.what());
    }
}

void LibTorrentWrapper::updateStatistics() {
    if (!d->session) {
        return;
    }
    
    d->session->post_session_stats();
    
    try {
        SessionStats newStats;
        // These are updated asynchronously via alerts, so we copy them from the last known state
        newStats.totalDownloaded = d->lastStats.totalDownloaded;
        newStats.totalUploaded = d->lastStats.totalUploaded;
        newStats.dhtNodes = d->lastStats.dhtNodes;
        
        // Count torrents by state and sum up rates
        QMutexLocker locker(&d->torrentsMutex);
        for (auto it = d->torrents.begin(); it != d->torrents.end(); ++it) {
            if (!it.value().is_valid()) continue;
            try {
                auto status = it.value().status();
                newStats.totalTorrents++;
                
                if (status.flags & libtorrent::torrent_flags::paused) {
                    newStats.pausedTorrents++;
                } else {
                    newStats.activeTorrents++;
                    
                    if (status.state == libtorrent::torrent_status::downloading ||
                        status.state == libtorrent::torrent_status::downloading_metadata) {
                        newStats.downloadingTorrents++;
                    } else if (status.state == libtorrent::torrent_status::seeding) {
                        newStats.seedingTorrents++;
                    }
                }
                
                newStats.globalDownloadRate += status.download_payload_rate;
                newStats.globalUploadRate += status.upload_payload_rate;
                newStats.totalPeers += status.num_peers;
            } catch (const std::exception& e) {
                Logger::instance().warn("Failed to get torrent status for {}: {}", it.key().toStdString(), e.what());
            }
        }
        locker.unlock();
        
        // Calculate global ratio
        if (newStats.totalDownloaded > 0) {
            newStats.globalRatio = static_cast<double>(newStats.totalUploaded) / newStats.totalDownloaded;
        }
        
        d->lastStats = newStats;
        emit sessionStatsUpdate(d->lastStats);
        
    } catch (const std::exception& e) {
        Logger::instance().warn("Exception in updateStatistics: {}", e.what());
    }
}

// Helper method implementations

libtorrent::torrent_handle* LibTorrentWrapper::findTorrent(const QString& infoHash) const {
    QMutexLocker locker(&d->torrentsMutex);
    auto it = d->torrents.find(infoHash);
    return (it != d->torrents.end()) ? &it.value() : nullptr;
}

QString LibTorrentWrapper::extractInfoHash(const libtorrent::torrent_handle& handle) const {
    try {
        return QString::fromStdString(to_hex(handle.info_hashes().v1));
    } catch (const std::exception& e) {
        Logger::instance().warn("Failed to extract info hash: {}", e.what());
        return QString();
    }
}

TorrentStats LibTorrentWrapper::extractTorrentStats(const libtorrent::torrent_handle& handle) const {
    TorrentStats stats;
    
    try {
        auto status = handle.status();
        
        stats.infoHash = QString::fromStdString(to_hex(handle.info_hashes().v1));
        stats.name = QString::fromStdString(status.name);
        stats.state = mapTorrentState(status.state);
        stats.totalSize = status.total_wanted;
        stats.downloadedBytes = status.total_wanted_done;
        stats.uploadedBytes = status.all_time_upload;
        stats.progress = status.progress;
        stats.downloadRate = status.download_payload_rate;
        stats.uploadRate = status.upload_payload_rate;
        stats.seeders = status.num_seeds;
        stats.leechers = status.num_peers - status.num_seeds;
        stats.peers = status.num_peers;
        stats.isPaused = (status.flags & libtorrent::torrent_flags::paused) != 0;
        stats.isFinished = status.is_finished;
        stats.isSeeding = status.is_seeding;
        stats.savePath = QString::fromStdString(status.save_path);
        
        if (stats.downloadedBytes > 0 && stats.uploadedBytes > 0) {
            stats.ratio = static_cast<double>(stats.uploadedBytes) / stats.downloadedBytes;
        }
        
        // File information
        if (status.has_metadata) {
            auto torrentFile = handle.torrent_file();
            if (torrentFile) {
                std::vector<std::int64_t> fileProgress;
                handle.file_progress(fileProgress);
                auto filePriorities = handle.get_file_priorities();
                
                for (int i = 0; i < torrentFile->num_files(); ++i) {
                    libtorrent::file_index_t fileIndex(i);
                    stats.files << QString::fromStdString(torrentFile->files().file_path(fileIndex));
                    stats.fileSizes << torrentFile->files().file_size(fileIndex);
                    
                    if (i < static_cast<int>(fileProgress.size())) {
                        qint64 fileSize = torrentFile->files().file_size(fileIndex);
                        stats.fileProgress << (fileSize > 0 ? static_cast<double>(fileProgress[i]) / fileSize : 1.0);
                    } else {
                        stats.fileProgress << 0.0;
                    }
                    
                    if (i < static_cast<int>(filePriorities.size())) {
                        stats.filePriorities << static_cast<int>(filePriorities[i]);
                    } else {
                        stats.filePriorities << 0;
                    }
                }
            }
        }
        
    } catch (const std::exception& e) {
        Logger::instance().warn("Exception in extractTorrentStats: {}", e.what());
    }
    
    return stats;
}

TorrentState LibTorrentWrapper::mapTorrentState(const libtorrent::torrent_status& status) const {
    return mapTorrentState(status.state);
}

TorrentState LibTorrentWrapper::mapTorrentState(int state) const {
    switch (static_cast<libtorrent::torrent_status::state_t>(state)) {
        case libtorrent::torrent_status::checking_files:
        case libtorrent::torrent_status::checking_resume_data:
            return TorrentState::CheckingFiles;
        case libtorrent::torrent_status::downloading_metadata:
            return TorrentState::DownloadingMetadata;
        case libtorrent::torrent_status::downloading:
            return TorrentState::Downloading;
        case libtorrent::torrent_status::finished:
            return TorrentState::Finished;
        case libtorrent::torrent_status::seeding:
            return TorrentState::Seeding;
        default:
            return TorrentState::Error;
    }
}

Expected<QString, TorrentError> LibTorrentWrapper::validateAndPreparePath(const QString& path) {
    QFileInfo pathInfo(path);
    QDir dir = pathInfo.isDir() ? QDir(path) : pathInfo.dir();
    
    if (!dir.exists() && !dir.mkpath(dir.absolutePath())) {
        Logger::instance().error("Failed to create directory: {}", dir.absolutePath().toStdString());
        return makeUnexpected(TorrentError::PermissionDenied);
    }
    
    return dir.absolutePath();
}

Expected<bool, TorrentError> LibTorrentWrapper::checkDiskSpace(const QString& path, qint64 requiredBytes) {
    QStorageInfo storage(path);
    if (!storage.isValid()) {
        return makeUnexpected(TorrentError::DiskError);
    }
    
    if (storage.bytesAvailable() < requiredBytes) {
        Logger::instance().error("Insufficient disk space: need {} MB, have {} MB", requiredBytes / (1024 * 1024), storage.bytesAvailable() / (1024 * 1024));
        return makeUnexpected(TorrentError::InsufficientSpace);
    }
    
    return true;
}

QString LibTorrentWrapper::generateSavePath(const QString& basePath, const QString& torrentName) {
    QDir baseDir(basePath);
    QString safeName = torrentName;
    
    // Remove invalid characters
    safeName.replace(QRegularExpression(R"([<>:"/\\|?*])"), "_");
    
    return baseDir.absoluteFilePath(safeName);
}

void LibTorrentWrapper::applySessionSettings(const TorrentSettings& settings) {
    if (!d->session) return;
    
    libtorrent::settings_pack pack;
    
    // Update rate limits
    pack.set_int(libtorrent::settings_pack::download_rate_limit, settings.maxDownloadRate > 0 ? settings.maxDownloadRate * 1024 : -1);
    pack.set_int(libtorrent::settings_pack::upload_rate_limit, settings.maxUploadRate > 0 ? settings.maxUploadRate * 1024 : -1);
    
    // Update connection limits
    pack.set_int(libtorrent::settings_pack::connections_limit, settings.maxConnections);
    
    // Update DHT/PEX/LSD settings
    pack.set_bool(libtorrent::settings_pack::enable_dht, settings.enableDHT);
    pack.set_bool(libtorrent::settings_pack::enable_lsd, settings.enableLSD);
    
    d->session->apply_settings(pack);
}

void LibTorrentWrapper::applyTorrentSettings(libtorrent::add_torrent_params& params, const TorrentSettings& settings) {
    if (settings.sequentialDownload) {
        params.flags |= libtorrent::torrent_flags::sequential_download;
    }
    
    if (settings.autoManaged) {
        params.flags |= libtorrent::torrent_flags::auto_managed;
    } else {
        params.flags &= ~libtorrent::torrent_flags::auto_managed;
    }
    
    if (settings.seedWhenComplete) {
        params.flags |= libtorrent::torrent_flags::seed_mode;
    }
    
    // Add custom trackers
    for (const QString& tracker : settings.trackers) {
        params.trackers.push_back(tracker.toStdString());
    }
}

TorrentError LibTorrentWrapper::mapLibTorrentError(const std::error_code& ec) {
    // Map libtorrent error codes to our TorrentError enum
    if (ec.category() == libtorrent::libtorrent_category()) {
        switch (ec.value()) {
            case libtorrent::errors::invalid_torrent_handle:
                return TorrentError::TorrentNotFound;
            case libtorrent::errors::invalid_entry_type:
            case libtorrent::errors::missing_info_hash_in_uri:
                return TorrentError::ParseError;
            case libtorrent::errors::duplicate_torrent:
                return TorrentError::DuplicateTorrent;
            default:
                return TorrentError::UnknownError;
        }
    }
    
    return TorrentError::UnknownError;
}

QString LibTorrentWrapper::translateTorrentError(TorrentError error) const {
    switch (error) {
        case TorrentError::InvalidMagnetLink:
            return "Invalid magnet link format";
        case TorrentError::InvalidTorrentFile:
            return "Invalid torrent file";
        case TorrentError::DuplicateTorrent:
            return "Torrent already exists";
        case TorrentError::TorrentNotFound:
            return "Torrent not found";
        case TorrentError::NetworkError:
            return "Network error";
        case TorrentError::DiskError:
            return "Disk error";
        case TorrentError::ParseError:
            return "Parse error";
        case TorrentError::SessionError:
            return "Session error";
        case TorrentError::PermissionDenied:
            return "Permission denied";
        case TorrentError::InsufficientSpace:
            return "Insufficient disk space";
        case TorrentError::TrackerError:
            return "Tracker error";
        case TorrentError::TimeoutError:
            return "Timeout error";
        case TorrentError::CancellationRequested:
            return "Operation cancelled";
        case TorrentError::UnknownError:
        default:
            return "Unknown error";
    }
}

Expected<bool, TorrentError> LibTorrentWrapper::validateMagnetLink(const QString& magnetLink) {
    if (!isValidMagnetLink(magnetLink)) {
        return makeUnexpected(TorrentError::InvalidMagnetLink);
    }
    return true;
}

Expected<bool, TorrentError> LibTorrentWrapper::validateTorrentData(const QByteArray& data) {
    if (data.isEmpty()) {
        return makeUnexpected(TorrentError::InvalidTorrentFile);
    }
    
    // Check if it looks like bencode
    if (!data.startsWith('d') || !data.endsWith('e')) {
        return makeUnexpected(TorrentError::InvalidTorrentFile);
    }
    
    return true;
}

void LibTorrentWrapper::cleanupTorrent(const QString& infoHash) {
    QMutexLocker locker(&d->torrentsMutex);
    d->torrents.remove(infoHash);
}

void LibTorrentWrapper::cleanupSession() {
    if (!d->session) return;
    
    try {
        // Pause all torrents
        QMutexLocker locker(&d->torrentsMutex);
        for (auto it = d->torrents.begin(); it != d->torrents.end(); ++it) {
            try {
                if (it.value().is_valid()) {
                    it.value().pause();
                }
            } catch (const std::exception& e) {
                Logger::instance().warn("Failed to pause torrent during cleanup: {}", e.what());
            }
        }
        locker.unlock();
        
        // Wait for session to finish
        d->session->wait_for_alert(std::chrono::seconds(5));
        
    } catch (const std::exception& e) {
        Logger::instance().warn("Exception during session cleanup: {}", e.what());
    }
}

} // namespace Murmur