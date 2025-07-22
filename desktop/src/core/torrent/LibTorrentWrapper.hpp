#pragma once

#include <memory>
#include <functional>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QUrl>
#include <QtCore/QTimer>
#include <QFuture>
#include <QMutex>

#include "../common/Expected.hpp"

namespace libtorrent {
    class session;
    class torrent_handle;
    class torrent_info;
    class add_torrent_params;
    struct torrent_status;
    struct session_stats_alert;
    struct alert;
}

namespace Murmur {
    class StorageManager;
}

namespace Murmur {

enum class TorrentError {
    InitializationFailed,
    InvalidMagnetLink,
    InvalidMagnetUri,
    InvalidTorrentFile,
    DuplicateTorrent,
    TorrentNotFound,
    NetworkError,
    DiskError,
    ParseError,
    SessionError,
    PermissionDenied,
    InsufficientSpace,
    TrackerError,
    TimeoutError,
    LibtorrentError,
    SecurityViolation,
    NetworkFailure,
    CancellationRequested,
    FileSystemError,
    UnknownError
};

enum class TorrentState {
    Queued,
    CheckingFiles,
    DownloadingMetadata,
    Downloading,
    Finished,
    Seeding,
    Allocating,
    CheckingResumeData,
    Paused,
    Error
};

struct TorrentStats {
    QString infoHash;
    QString name;
    TorrentState state = TorrentState::Queued;
    qint64 totalSize = 0;
    qint64 downloadedBytes = 0;
    qint64 uploadedBytes = 0;
    double progress = 0.0;          // 0.0 to 1.0
    int downloadRate = 0;           // bytes/sec
    int uploadRate = 0;             // bytes/sec
    int seeders = 0;
    int leechers = 0;
    int peers = 0;
    double ratio = 0.0;
    QDateTime addedTime;
    QDateTime completedTime;
    QString savePath;
    QString errorString;
    bool isPaused = false;
    bool isFinished = false;
    bool isSeeding = false;
    
    // File information
    QStringList files;
    QList<qint64> fileSizes;
    QList<double> fileProgress;
    QList<int> filePriorities;
};

struct SessionStats {
    int totalTorrents = 0;
    int activeTorrents = 0;
    int seedingTorrents = 0;
    int downloadingTorrents = 0;
    int pausedTorrents = 0;
    qint64 totalDownloaded = 0;
    qint64 totalUploaded = 0;
    int globalDownloadRate = 0;
    int globalUploadRate = 0;
    int totalPeers = 0;
    double globalRatio = 0.0;
    QString dhtState;
    int dhtNodes = 0;
};

struct TorrentSettings {
    QString downloadPath;
    int maxDownloadRate = 0;       // 0 = unlimited
    int maxUploadRate = 0;         // 0 = unlimited
    int maxConnections = 200;
    int maxSeeds = 50;
    bool enableDHT = true;
    bool enablePEX = true;
    bool enableLSD = true;
    bool enableUPnP = true;
    bool enableNATPMP = true;
    bool sequentialDownload = false;
    bool autoManaged = true;
    bool seedWhenComplete = true;
    double shareRatioLimit = 2.0;  // Auto-remove when ratio reached
    int seedTimeLimit = 0;         // Auto-remove after seeding time (hours)
    QStringList trackers;
    QString userAgent = "MurmurDesktop/1.0";
};

// Progress callback function types
using TorrentProgressCallback = std::function<void(const QString&, const TorrentStats&)>;
using StateChangeCallback = std::function<void(const QString&, TorrentState, TorrentState)>;
using ErrorCallback = std::function<void(const QString&, TorrentError, const QString&)>;

/**
 * @brief LibTorrent wrapper for P2P file sharing
 * 
 * This class provides a complete C++ interface to libtorrent-rasterbar,
 * handling torrent creation, downloading, seeding, and peer management
 * with error handling and progress tracking.
 */
class LibTorrentWrapper : public QObject {
    Q_OBJECT

public:
    explicit LibTorrentWrapper(QObject* parent = nullptr);
    ~LibTorrentWrapper() override;

    // Non-copyable, non-movable
    LibTorrentWrapper(const LibTorrentWrapper&) = delete;
    LibTorrentWrapper& operator=(const LibTorrentWrapper&) = delete;
    LibTorrentWrapper(LibTorrentWrapper&&) = delete;
    LibTorrentWrapper& operator=(LibTorrentWrapper&&) = delete;

    /**
     * @brief Initialize the torrent session
     * @param settings Initial torrent settings
     * @return true if successful, false otherwise
     */
    Expected<bool, TorrentError> initialize(const TorrentSettings& settings = TorrentSettings{});

    /**
     * @brief Set the storage manager for persistence
     * @param storage Pointer to StorageManager instance
     */
    void setStorageManager(StorageManager* storage);

    /**
     * @brief Shutdown the torrent session
     */
    void shutdown();

    /**
     * @brief Check if session is initialized and running
     * @return true if initialized
     */
    bool isInitialized() const;

    /**
     * @brief Add torrent from magnet link
     * @param magnetLink Magnet URI
     * @param savePath Download directory (optional, uses default if empty)
     * @param settings Torrent-specific settings (optional)
     * @return Info hash of added torrent or error
     */
    Expected<QString, TorrentError> addMagnetLink(
        const QString& magnetLink,
        const QString& savePath = QString(),
        const TorrentSettings& settings = TorrentSettings{}
    );

    /**
     * @brief Add torrent from .torrent file
     * @param torrentFile Path to .torrent file
     * @param savePath Download directory (optional, uses default if empty)
     * @param settings Torrent-specific settings (optional)
     * @return Info hash of added torrent or error
     */
    Expected<QString, TorrentError> addTorrentFile(
        const QString& torrentFile,
        const QString& savePath = QString(),
        const TorrentSettings& settings = TorrentSettings{}
    );

    /**
     * @brief Add torrent from raw .torrent data
     * @param torrentData Raw .torrent file content
     * @param savePath Download directory (optional, uses default if empty)
     * @param settings Torrent-specific settings (optional)
     * @return Info hash of added torrent or error
     */
    Expected<QString, TorrentError> addTorrentData(
        const QByteArray& torrentData,
        const QString& savePath = QString(),
        const TorrentSettings& settings = TorrentSettings{}
    );

    /**
     * @brief Create torrent file from directory or file
     * @param sourcePath Path to file or directory to create torrent from
     * @param trackers List of tracker URLs
     * @param comment Optional comment
     * @param creator Optional creator name
     * @param isPrivate Whether torrent should be private
     * @return Raw .torrent file data or error
     */
    Expected<QByteArray, TorrentError> createTorrent(
        const QString& sourcePath,
        const QStringList& trackers,
        const QString& comment = QString(),
        const QString& creator = QString(),
        bool isPrivate = false
    );

    /**
     * @brief Remove torrent from session
     * @param infoHash Torrent info hash
     * @param deleteFiles Whether to delete downloaded files
     * @return true if successful, false otherwise
     */
    Expected<bool, TorrentError> removeTorrent(const QString& infoHash, bool deleteFiles = false);

    /**
     * @brief Pause torrent
     * @param infoHash Torrent info hash
     * @return true if successful, false otherwise
     */
    Expected<bool, TorrentError> pauseTorrent(const QString& infoHash);

    /**
     * @brief Resume torrent
     * @param infoHash Torrent info hash
     * @return true if successful, false otherwise
     */
    Expected<bool, TorrentError> resumeTorrent(const QString& infoHash);

    /**
     * @brief Force recheck of torrent files
     * @param infoHash Torrent info hash
     * @return true if successful, false otherwise
     */
    Expected<bool, TorrentError> recheckTorrent(const QString& infoHash);

    /**
     * @brief Move torrent to different directory
     * @param infoHash Torrent info hash
     * @param newPath New save path
     * @return true if successful, false otherwise
     */
    Expected<bool, TorrentError> moveTorrent(const QString& infoHash, const QString& newPath);

    /**
     * @brief Set file priorities for selective downloading
     * @param infoHash Torrent info hash
     * @param priorities List of priorities (0=skip, 1-7=normal to high)
     * @return true if successful, false otherwise
     */
    Expected<bool, TorrentError> setFilePriorities(const QString& infoHash, const QList<int>& priorities);

    /**
     * @brief Get torrent statistics
     * @param infoHash Torrent info hash
     * @return Torrent statistics or error
     */
    Expected<TorrentStats, TorrentError> getTorrentStats(const QString& infoHash) const;

    /**
     * @brief Get statistics for all torrents
     * @return List of all torrent statistics
     */
    QList<TorrentStats> getAllTorrentStats() const;

    /**
     * @brief Get session statistics
     * @return Session-wide statistics
     */
    SessionStats getSessionStats() const;

    /**
     * @brief Get list of all torrent info hashes
     * @return List of info hashes
     */
    QStringList getTorrentList() const;

    /**
     * @brief Check if torrent exists in session
     * @param infoHash Torrent info hash
     * @return true if torrent exists
     */
    bool hasTorrent(const QString& infoHash) const;

    /**
     * @brief Update global session settings
     * @param settings New settings to apply
     * @return true if successful, false otherwise
     */
    Expected<bool, TorrentError> updateSettings(const TorrentSettings& settings);

    /**
     * @brief Get current session settings
     * @return Current settings
     */
    TorrentSettings getCurrentSettings() const;

    /**
     * @brief Save session state to file
     * @param filePath Path to save state file
     * @return true if successful, false otherwise
     */
    Expected<bool, TorrentError> saveSessionState(const QString& filePath);

    /**
     * @brief Load session state from file
     * @param filePath Path to state file
     * @return true if successful, false otherwise
     */
    Expected<bool, TorrentError> loadSessionState(const QString& filePath);

    /**
     * @brief Parse magnet link to extract info hash and other data
     * @param magnetLink Magnet URI
     * @return Parsed magnet info or error
     */
    static Expected<QJsonObject, TorrentError> parseMagnetLink(const QString& magnetLink);

    /**
     * @brief Parse .torrent file to extract metadata
     * @param torrentData Raw .torrent file content
     * @return Parsed torrent info or error
     */
    static Expected<QJsonObject, TorrentError> parseTorrentData(const QByteArray& torrentData);

    /**
     * @brief Calculate info hash from torrent data
     * @param torrentData Raw .torrent file content
     * @return Info hash or error
     */
    static Expected<QString, TorrentError> calculateInfoHash(const QByteArray& torrentData);

    /**
     * @brief Validate magnet link format
     * @param magnetLink Magnet URI to validate
     * @return true if valid, false otherwise
     */
    static bool isValidMagnetLink(const QString& magnetLink);

    /**
     * @brief Get libtorrent version information
     * @return Version string
     */
    static QString getLibTorrentVersion();

signals:
    // Torrent lifecycle signals
    void torrentAdded(const QString& infoHash, const QString& name);
    void torrentRemoved(const QString& infoHash);
    void torrentStateChanged(const QString& infoHash, TorrentState oldState, TorrentState newState);
    void torrentProgress(const QString& infoHash, const TorrentStats& stats);
    void torrentFinished(const QString& infoHash);
    void torrentError(const QString& infoHash, TorrentError error, const QString& errorMessage);
    
    // File-level signals
    void fileCompleted(const QString& infoHash, const QString& filePath);
    void pieceCompleted(const QString& infoHash, int pieceIndex);
    
    // Session signals
    void sessionStatsUpdate(const SessionStats& stats);
    void dhtStateChanged(const QString& state, int nodes);
    void trackerWarning(const QString& infoHash, const QString& tracker, const QString& message);
    void trackerError(const QString& infoHash, const QString& tracker, const QString& error);
    
    // Network signals
    void peerConnected(const QString& infoHash, const QString& peerAddress);
    void peerDisconnected(const QString& infoHash, const QString& peerAddress);

private slots:
    void processAlerts();
    void updateStatistics();

private:
    struct LibTorrentWrapperPrivate;
    std::unique_ptr<LibTorrentWrapperPrivate> d;

    // Internal helper methods
    Expected<bool, TorrentError> initializeSession(const TorrentSettings& settings);
    Expected<bool, TorrentError> configureSession(const TorrentSettings& settings);
    void setupAlertHandling();
    void processAlert(const libtorrent::alert* alert);
    
    // Torrent management helpers
    libtorrent::torrent_handle* findTorrent(const QString& infoHash) const;
    QString extractInfoHash(const libtorrent::torrent_handle& handle) const;
    TorrentStats extractTorrentStats(const libtorrent::torrent_handle& handle) const;
    TorrentState mapTorrentState(const libtorrent::torrent_status& status) const;
    TorrentState mapTorrentState(int state) const;
    
    // File and path helpers
    Expected<QString, TorrentError> validateAndPreparePath(const QString& path);
    Expected<bool, TorrentError> checkDiskSpace(const QString& path, qint64 requiredBytes);
    QString generateSavePath(const QString& basePath, const QString& torrentName);
    
    // Settings helpers
    void applySessionSettings(const TorrentSettings& settings);
    void applyTorrentSettings(libtorrent::add_torrent_params& params, const TorrentSettings& settings);
    
    // Error handling
    TorrentError mapLibTorrentError(const std::error_code& ec);
    QString translateTorrentError(TorrentError error) const;
    
    // Validation
    Expected<bool, TorrentError> validateMagnetLink(const QString& magnetLink);
    Expected<bool, TorrentError> validateTorrentData(const QByteArray& data);
    
    // Cleanup
    void cleanupTorrent(const QString& infoHash);
    void cleanupSession();
};

} // namespace Murmur

Q_DECLARE_METATYPE(Murmur::TorrentError)