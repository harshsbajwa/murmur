#include "TorrentStateModel.hpp"
#include "../common/Logger.hpp"

namespace Murmur {

TorrentStateModel::TorrentStateModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int TorrentStateModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent)
    return torrents_.size();
}

QVariant TorrentStateModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= torrents_.size()) {
        return QVariant();
    }
    
    const TorrentEngine::TorrentInfo& torrent = torrents_.at(index.row());
    
    switch (role) {
        case InfoHashRole:
            return torrent.infoHash;
        case NameRole:
            return torrent.name.isEmpty() ? torrent.infoHash : torrent.name;
        case SizeRole:
            return formatFileSize(torrent.size);
        case ProgressRole:
            return torrent.progress;
        case PeersRole:
            return torrent.peers;
        case DownloadRateRole:
            return formatSpeed(torrent.downloadRate);
        case UploadRateRole:
            return formatSpeed(torrent.uploadRate);
        case DownloadSpeedRole:
            return torrent.downloadRate;
        case UploadSpeedRole:
            return torrent.uploadRate;
        case SeedersRole:
            return torrent.seeders;
        case LeechersRole:
            return torrent.leechers;
        case FilesRole:
            return torrent.files;
        case SavePathRole:
            return torrent.savePath;
        case MagnetUriRole:
            return torrent.magnetUri;
        case IsSeedingRole:
            return torrent.isSeeding;
        case IsPausedRole:
            return torrent.isPaused;
        case StatusRole:
            return torrent.status;
        case Qt::DisplayRole:
            return torrent.name.isEmpty() ? torrent.infoHash : torrent.name;
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> TorrentStateModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[InfoHashRole] = "infoHash";
    roles[NameRole] = "name";
    roles[SizeRole] = "size";
    roles[ProgressRole] = "progress";
    roles[PeersRole] = "peers";
    roles[DownloadRateRole] = "downloadRate";
    roles[UploadRateRole] = "uploadRate";
    roles[DownloadSpeedRole] = "downloadSpeed";
    roles[UploadSpeedRole] = "uploadSpeed";
    roles[SeedersRole] = "seeders";
    roles[LeechersRole] = "leechers";
    roles[FilesRole] = "files";
    roles[SavePathRole] = "savePath";
    roles[MagnetUriRole] = "magnetUri";
    roles[IsSeedingRole] = "isSeeding";
    roles[IsPausedRole] = "isPaused";
    roles[StatusRole] = "status";
    return roles;
}

void TorrentStateModel::addTorrent(const TorrentEngine::TorrentInfo& info) {
    // Check if torrent already exists
    if (torrentIndexMap_.contains(info.infoHash)) {
        updateTorrent(info);
        return;
    }
    
    beginInsertRows(QModelIndex(), torrents_.size(), torrents_.size());
    torrents_.append(info);
    updateIndexMap();
    endInsertRows();
    
    emit torrentCountChanged();
    
    MURMUR_DEBUG("Torrent added to model: {}", info.name.toStdString());
}

void TorrentStateModel::updateTorrent(const TorrentEngine::TorrentInfo& info) {
    auto it = torrentIndexMap_.find(info.infoHash);
    if (it == torrentIndexMap_.end()) {
        addTorrent(info);
        return;
    }
    
    int index = it.value();
    if (index >= 0 && index < torrents_.size()) {
        torrents_[index] = info;
        
        QModelIndex modelIndex = createIndex(index, 0);
        emit dataChanged(modelIndex, modelIndex);
        emit torrentUpdated(info.infoHash);
    }
}

void TorrentStateModel::removeTorrent(const QString& infoHash) {
    auto it = torrentIndexMap_.find(infoHash);
    if (it == torrentIndexMap_.end()) {
        return;
    }
    
    int index = it.value();
    if (index >= 0 && index < torrents_.size()) {
        beginRemoveRows(QModelIndex(), index, index);
        torrents_.removeAt(index);
        updateIndexMap();
        endRemoveRows();
        
        emit torrentCountChanged();
        
        MURMUR_DEBUG("Torrent removed from model: {}", infoHash.toStdString());
    }
}

void TorrentStateModel::clear() {
    if (torrents_.isEmpty()) {
        return;
    }
    
    beginResetModel();
    torrents_.clear();
    torrentIndexMap_.clear();
    endResetModel();
    
    emit torrentCountChanged();
    
    MURMUR_DEBUG("Torrent model cleared");
}

int TorrentStateModel::getTorrentIndex(const QString& infoHash) const {
    auto it = torrentIndexMap_.find(infoHash);
    return it != torrentIndexMap_.end() ? it.value() : -1;
}

TorrentEngine::TorrentInfo TorrentStateModel::getTorrentInfo(int index) const {
    if (index >= 0 && index < torrents_.size()) {
        return torrents_.at(index);
    }
    return TorrentEngine::TorrentInfo{};
}

TorrentEngine::TorrentInfo TorrentStateModel::getTorrentInfo(const QString& infoHash) const {
    int index = getTorrentIndex(infoHash);
    return getTorrentInfo(index);
}

int TorrentStateModel::activeTorrentsCount() const {
    return torrents_.size();
}

int TorrentStateModel::seedingTorrentsCount() const {
    int count = 0;
    for (const auto& torrent : torrents_) {
        if (torrent.isSeeding) {
            count++;
        }
    }
    return count;
}

int TorrentStateModel::downloadingTorrentsCount() const {
    int count = 0;
    for (const auto& torrent : torrents_) {
        if (!torrent.isSeeding && !torrent.isPaused && torrent.progress < 1.0) {
            count++;
        }
    }
    return count;
}

void TorrentStateModel::updateIndexMap() {
    torrentIndexMap_.clear();
    for (int i = 0; i < torrents_.size(); ++i) {
        torrentIndexMap_[torrents_[i].infoHash] = i;
    }
}

QString TorrentStateModel::formatFileSize(qint64 bytes) const {
    if (bytes == 0) return "0 B";
    
    const QStringList units = {"B", "KiB", "MiB", "GiB", "TiB"};
    int unitIndex = 0;
    double size = bytes;
    
    while (size >= 1024 && unitIndex < units.size() - 1) {
        size /= 1024;
        unitIndex++;
    }
    
    return QString("%1 %2").arg(QString::number(size, 'f', unitIndex > 0 ? 1 : 0), units[unitIndex]);
}

QString TorrentStateModel::formatSpeed(qint64 bytesPerSecond) const {
    if (bytesPerSecond == 0) return "0 B/s";
    
    const QStringList units = {"B/s", "KiB/s", "MiB/s", "GiB/s"};
    int unitIndex = 0;
    double speed = bytesPerSecond;
    
    while (speed >= 1024 && unitIndex < units.size() - 1) {
        speed /= 1024;
        unitIndex++;
    }
    
    return QString("%1 %2").arg(QString::number(speed, 'f', unitIndex > 0 ? 1 : 0), units[unitIndex]);
}

} // namespace Murmur