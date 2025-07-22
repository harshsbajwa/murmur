#pragma once

#include "TorrentEngine.hpp"
#include <QtCore/QAbstractListModel>
#include <QtCore/QHash>

namespace Murmur {

class TorrentStateModel : public QAbstractListModel {
    Q_OBJECT
    
public:
    enum TorrentRole {
        InfoHashRole = Qt::UserRole + 1,
        NameRole,
        SizeRole,
        ProgressRole,
        PeersRole,
        DownloadRateRole,
        UploadRateRole,
        FilesRole,
        SavePathRole,
        MagnetUriRole,
        IsSeedingRole,
        IsPausedRole,
        StatusRole
    };
    Q_ENUM(TorrentRole)
    
    explicit TorrentStateModel(QObject* parent = nullptr);
    
    // QAbstractListModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    
    // Torrent management
    void addTorrent(const TorrentEngine::TorrentInfo& info);
    void updateTorrent(const TorrentEngine::TorrentInfo& info);
    void removeTorrent(const QString& infoHash);
    void clear();
    
    // Convenience methods
    int getTorrentIndex(const QString& infoHash) const;
    TorrentEngine::TorrentInfo getTorrentInfo(int index) const;
    TorrentEngine::TorrentInfo getTorrentInfo(const QString& infoHash) const;
    
    // Statistics
    int activeTorrentsCount() const;
    int seedingTorrentsCount() const;
    int downloadingTorrentsCount() const;
    
signals:
    void torrentCountChanged();
    void torrentUpdated(const QString& infoHash);
    
private:
    QList<TorrentEngine::TorrentInfo> torrents_;
    QHash<QString, int> torrentIndexMap_;
    
    void updateIndexMap();
    QString formatFileSize(qint64 bytes) const;
    QString formatSpeed(qint64 bytesPerSecond) const;
};

} // namespace Murmur