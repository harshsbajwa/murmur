#pragma once

#include "LibTorrentWrapper.hpp"
#include "../common/Expected.hpp"
#include "../security/InputValidator.hpp"
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QReadWriteLock>
#include <QtCore/QHash>
#include <QtConcurrent/QtConcurrent>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <memory>

namespace Murmur {

class TorrentStateModel;
class TorrentSecurityWrapper;

class TorrentEngine : public QObject {
    Q_OBJECT
    
public:
    struct TorrentInfo {
        QString infoHash;
        QString name;
        qint64 size = 0;
        qreal progress = 0.0;
        int peers = 0;
        qint64 downloadRate = 0;
        qint64 uploadRate = 0;
        QStringList files;
        QString savePath;
        QString magnetUri;
        bool isSeeding = false;
        bool isPaused = false;
        QString status = "Unknown";
    };
    
    explicit TorrentEngine(QObject* parent = nullptr);
    ~TorrentEngine();
    
    // Core operations
    QFuture<Expected<TorrentInfo, TorrentError>> addTorrent(const QString& magnetUri);
    QFuture<Expected<TorrentInfo, TorrentError>> addTorrentFromFile(const QString& torrentFilePath);
    QFuture<Expected<TorrentInfo, TorrentError>> addTorrentFromData(const QByteArray& torrentData);
    QFuture<Expected<TorrentInfo, TorrentError>> seedFile(const QString& filePath);
    Expected<void, TorrentError> removeTorrent(const QString& infoHash);
    Expected<void, TorrentError> pauseTorrent(const QString& infoHash);
    Expected<void, TorrentError> resumeTorrent(const QString& infoHash);
    
    // Status and information
    QList<TorrentInfo> getActiveTorrents() const;
    Expected<TorrentInfo, TorrentError> getTorrentInfo(const QString& infoHash) const;
    bool hasTorrent(const QString& infoHash) const;
    
    // Qt model for UI binding
    TorrentStateModel* torrentModel() const { return torrentModel_.get(); }
    
    // Session configuration
    void configureSession(int maxConnections, int uploadRate, int downloadRate);
    void setDownloadPath(const QString& path);
    
    // Session state
    bool isSessionActive() const;
    void startSession();
    void stopSession();
    
    // Test compatibility methods
    Expected<void, TorrentError> initialize();
    bool isInitialized() const;
    
signals:
    void torrentAdded(const QString& infoHash);
    void torrentProgress(const QString& infoHash, qreal progress);
    void torrentCompleted(const QString& infoHash);
    void torrentError(const QString& infoHash, TorrentError error);
    void torrentRemoved(const QString& infoHash);
    void torrentPaused(const QString& infoHash);
    void torrentResumed(const QString& infoHash);
    void torrentUpdated(const QString& infoHash);
    
private slots:
    void handleLibtorrentAlerts();
    void updateTorrentStates();
    
private:
    std::unique_ptr<libtorrent::session> session_;
    std::unique_ptr<TorrentStateModel> torrentModel_;
    std::unique_ptr<TorrentSecurityWrapper> securityWrapper_;
    QTimer* alertTimer_;
    QTimer* updateTimer_;
    
    // Thread-safe torrent storage
    mutable QReadWriteLock torrentsLock_;
    QHash<QString, TorrentInfo> torrents_;
    QHash<QString, libtorrent::torrent_handle> torrentHandles_;
    
    QString downloadPath_;
    bool sessionActive_ = false;
    
    // Helper methods
    QString getInfoHashFromHandle(const libtorrent::torrent_handle& handle) const;
    TorrentInfo createTorrentInfo(const libtorrent::torrent_handle& handle) const;
    void updateTorrentInfo(const QString& infoHash, const libtorrent::torrent_handle& handle);
    void emitTorrentUpdate(const QString& infoHash);
    
    // Session management
    void initializeSession();
    void configureSessionSettings();
    void addDefaultTrackers();
    
    // Error handling
    TorrentError mapLibtorrentError(const libtorrent::error_code& ec) const;
    void handleTorrentAlert(const libtorrent::alert* alert);
};

} // namespace Murmur