#include "TorrentListModel.hpp"
#include "core/torrent/TorrentEngine.hpp"
#include "core/common/Logger.hpp"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QTimer>
#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <algorithm>

namespace Murmur {

class TorrentListModel::TorrentListModelPrivate {
public:
    TorrentListModelPrivate() = default;
    ~TorrentListModelPrivate() = default;

    TorrentEngine* torrentEngine = nullptr;
    std::vector<TorrentEngine::TorrentInfo> torrents;
    
    // Filtering and sorting
    QString sortField = "addedAt";
    Qt::SortOrder sortOrder = Qt::DescendingOrder;
    QString statusFilter;
    QString searchFilter;
    
    // Statistics
    bool hasActiveTorrents = false;
    int downloadingCount = 0;
    int seedingCount = 0;
    qint64 totalDownloadSpeed = 0;
    qint64 totalUploadSpeed = 0;
    
    // Update timer
    std::unique_ptr<QTimer> updateTimer;
    
    // Cached filtered torrents
    std::vector<int> filteredIndices;
    bool filtersApplied = false;
};

TorrentListModel::TorrentListModel(QObject* parent)
    : QAbstractListModel(parent)
    , d(std::make_unique<TorrentListModelPrivate>())
{
    // Set up update timer
    d->updateTimer = std::make_unique<QTimer>(this);
    connect(d->updateTimer.get(), &QTimer::timeout, this, &TorrentListModel::updateStatistics);
    d->updateTimer->start(1000); // Update every second
}

TorrentListModel::~TorrentListModel() {
    disconnectFromTorrentEngine();
}

int TorrentListModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent)
    
    if (d->filtersApplied) {
        return static_cast<int>(d->filteredIndices.size());
    }
    return static_cast<int>(d->torrents.size());
}

QVariant TorrentListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return QVariant();
    }
    
    int torrentIndex = index.row();
    if (d->filtersApplied) {
        if (torrentIndex >= static_cast<int>(d->filteredIndices.size())) {
            return QVariant();
        }
        torrentIndex = d->filteredIndices[torrentIndex];
    }
    
    if (torrentIndex >= static_cast<int>(d->torrents.size())) {
        return QVariant();
    }
    
    const TorrentEngine::TorrentInfo& torrent = d->torrents[torrentIndex];
    
    switch (role) {
        case InfoHashRole:
            return torrent.infoHash;
        case NameRole:
            return torrent.name;
        case MagnetLinkRole:
            return torrent.magnetUri;
        case SavePathRole:
            return torrent.savePath;
        case SizeRole:
            return torrent.size;
        case DownloadedRole:
            return static_cast<qint64>(torrent.progress * torrent.size); // estimate
        case UploadedRole:
            return qint64(0); // not available in TorrentEngine::TorrentInfo
        case ProgressRole:
            return torrent.progress;
        case StatusRole: {
            // Map status string to enum value
            const QString& status = torrent.status;
            if (status == "Downloading") return 1;
            if (status == "Seeding") return 2;
            if (status == "Paused") return 3;
            if (status == "Checking") return 4;
            if (status == "Connecting") return 5;
            return 0; // Unknown
        }
        case StatusStringRole:
            return torrent.status;
        case DownloadSpeedRole:
            return torrent.downloadRate;
        case UploadSpeedRole:
            return torrent.uploadRate;
        case SeedersRole:
            return torrent.peers; // approximate
        case LeechersRole:
            return 0; // not available
        case ConnectionsRole:
            return torrent.peers;
        case AddedAtRole:
            return QDateTime(); // not available
        case CompletedAtRole:
            return QDateTime(); // not available
        case ErrorStringRole:
            return QString(); // not available
        case MetadataRole:
            return QJsonObject(); // not available
        case PriorityRole:
            return 1; // default priority
        case SequentialDownloadRole:
            return false; // not available
        case CreatorRole:
            return QString(); // not available
        case CommentRole:
            return QString(); // not available
        case IsValidRole:
            return !torrent.infoHash.isEmpty() && !torrent.name.isEmpty();
        case IsActiveRole:
            return !torrent.isPaused && (torrent.isSeeding || torrent.progress < 1.0);
        case IsCompleteRole:
            return torrent.progress >= 1.0;
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> TorrentListModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[InfoHashRole] = "infoHash";
    roles[NameRole] = "name";
    roles[MagnetLinkRole] = "magnetLink";
    roles[SavePathRole] = "savePath";
    roles[SizeRole] = "size";
    roles[DownloadedRole] = "downloaded";
    roles[UploadedRole] = "uploaded";
    roles[ProgressRole] = "progress";
    roles[StatusRole] = "status";
    roles[StatusStringRole] = "statusString";
    roles[DownloadSpeedRole] = "downloadSpeed";
    roles[UploadSpeedRole] = "uploadSpeed";
    roles[SeedersRole] = "seeders";
    roles[LeechersRole] = "leechers";
    roles[ConnectionsRole] = "connections";
    roles[AddedAtRole] = "addedAt";
    roles[CompletedAtRole] = "completedAt";
    roles[ErrorStringRole] = "errorString";
    roles[MetadataRole] = "metadata";
    roles[PriorityRole] = "priority";
    roles[SequentialDownloadRole] = "sequentialDownload";
    roles[CreatorRole] = "creator";
    roles[CommentRole] = "comment";
    roles[IsValidRole] = "isValid";
    roles[IsActiveRole] = "isActive";
    roles[IsCompleteRole] = "isComplete";
    return roles;
}

void TorrentListModel::setTorrentEngine(QObject* torrentEngine) {
    disconnectFromTorrentEngine();
    
    d->torrentEngine = qobject_cast<TorrentEngine*>(torrentEngine);
    if (d->torrentEngine) {
        connectToTorrentEngine();
        refresh();
        Logger::instance().info("TorrentListModel connected to TorrentEngine");
    }
}

void TorrentListModel::refresh() {
    if (!d->torrentEngine) {
        return;
    }
    
    beginResetModel();
    d->torrents.clear();
    
    // Get torrent list from engine
    const auto& torrentList = d->torrentEngine->getActiveTorrents();
    d->torrents.reserve(torrentList.size());
    
    for (const auto& torrentInfo : torrentList) {
        d->torrents.push_back(torrentInfo);
    }
    
    sortTorrents();
    applyFilters();
    endResetModel();
    
    calculateStatistics();
    emit countChanged();
}

void TorrentListModel::clear() {
    beginResetModel();
    d->torrents.clear();
    d->filteredIndices.clear();
    d->filtersApplied = false;
    endResetModel();
    
    calculateStatistics();
    emit countChanged();
}

bool TorrentListModel::addTorrent(const QString& magnetLink, const QString& /*savePath*/) {
    if (!d->torrentEngine || magnetLink.isEmpty()) {
        return false;
    }
    
    auto result = d->torrentEngine->addTorrent(magnetLink);
    // TorrentEngine methods return QFuture, we can't check immediately
    // The torrent will be added via the signal when it's actually added
    return true;
}

bool TorrentListModel::addTorrentFile(const QString& filePath, const QString& /*savePath*/) {
    if (!d->torrentEngine || filePath.isEmpty()) {
        return false;
    }
    
    auto result = d->torrentEngine->addTorrentFromFile(filePath);
    // TorrentEngine methods return QFuture, we can't check immediately
    return true;
}

bool TorrentListModel::removeTorrent(const QString& infoHash, bool /*deleteFiles*/) {
    if (!d->torrentEngine || infoHash.isEmpty()) {
        return false;
    }
    
    auto result = d->torrentEngine->removeTorrent(infoHash);
    return result.hasValue();
}

bool TorrentListModel::pauseTorrent(const QString& infoHash) {
    if (!d->torrentEngine || infoHash.isEmpty()) {
        return false;
    }
    
    auto result = d->torrentEngine->pauseTorrent(infoHash);
    return result.hasValue();
}

bool TorrentListModel::resumeTorrent(const QString& infoHash) {
    if (!d->torrentEngine || infoHash.isEmpty()) {
        return false;
    }
    
    auto result = d->torrentEngine->resumeTorrent(infoHash);
    return result.hasValue();
}

bool TorrentListModel::recheckTorrent(const QString& infoHash) {
    if (!d->torrentEngine || infoHash.isEmpty()) {
        return false;
    }
    
    // Assuming TorrentEngine has a recheck method
    // auto result = d->torrentEngine->recheckTorrent(infoHash);
    // return result.hasValue();
    
    // For now, return false as the method doesn't exist
    return false;
}

bool TorrentListModel::setTorrentPriority(const QString& infoHash, int /*priority*/) {
    if (!d->torrentEngine || infoHash.isEmpty()) {
        return false;
    }
    
    // Update local model
    int index = findTorrentIndex(infoHash);
    if (index >= 0) {
        // priority field not available in TorrentEngine::TorrentInfo
        
        QModelIndex modelIndex = createIndex(index, 0);
        emit dataChanged(modelIndex, modelIndex, {PriorityRole});
    }
    
    return true;
}

bool TorrentListModel::setSequentialDownload(const QString& infoHash, bool /*sequential*/) {
    if (!d->torrentEngine || infoHash.isEmpty()) {
        return false;
    }
    
    // Update local model
    int index = findTorrentIndex(infoHash);
    if (index >= 0) {
        // sequentialDownload field not available in TorrentEngine::TorrentInfo
        
        QModelIndex modelIndex = createIndex(index, 0);
        emit dataChanged(modelIndex, modelIndex, {SequentialDownloadRole});
    }
    
    return true;
}

QVariantMap TorrentListModel::getTorrentInfo(const QString& infoHash) const {
    int index = findTorrentIndex(infoHash);
    if (index >= 0) {
        return torrentInfoToVariant(d->torrents[index]);
    }
    return QVariantMap();
}

QStringList TorrentListModel::getInfoHashes() const {
    QStringList hashes;
    for (const auto& torrent : d->torrents) {
        hashes.append(torrent.infoHash);
    }
    return hashes;
}

QString TorrentListModel::getTorrentName(const QString& infoHash) const {
    int index = findTorrentIndex(infoHash);
    if (index >= 0) {
        return d->torrents[index].name;
    }
    return QString();
}

float TorrentListModel::getTorrentProgress(const QString& infoHash) const {
    int index = findTorrentIndex(infoHash);
    if (index >= 0) {
        return d->torrents[index].progress;
    }
    return 0.0f;
}

QString TorrentListModel::getTorrentStatus(const QString& infoHash) const {
    int index = findTorrentIndex(infoHash);
    if (index >= 0) {
        return d->torrents[index].status;
    }
    return "unknown";
}

QVariantList TorrentListModel::getActiveTorrents() const {
    QVariantList active;
    for (const auto& torrent : d->torrents) {
        if (!torrent.isPaused && (torrent.isSeeding || torrent.progress < 1.0)) {
            active.append(torrentInfoToVariant(torrent));
        }
    }
    return active;
}

QVariantList TorrentListModel::getTorrentsByStatus(const QString& status) const {
    QString targetStatus = status.toLower();
    QVariantList filtered;
    
    for (const auto& torrent : d->torrents) {
        if (torrent.status.toLower() == targetStatus) {
            filtered.append(torrentInfoToVariant(torrent));
        }
    }
    return filtered;
}

void TorrentListModel::setSortField(const QString& field) {
    if (d->sortField != field) {
        d->sortField = field;
        sortTorrents();
        
        beginResetModel();
        applyFilters();
        endResetModel();
    }
}

void TorrentListModel::setSortOrder(Qt::SortOrder order) {
    if (d->sortOrder != order) {
        d->sortOrder = order;
        sortTorrents();
        
        beginResetModel();
        applyFilters();
        endResetModel();
    }
}

void TorrentListModel::setStatusFilter(const QString& status) {
    if (d->statusFilter != status) {
        d->statusFilter = status;
        
        beginResetModel();
        applyFilters();
        endResetModel();
        
        emit countChanged();
    }
}

void TorrentListModel::setSearchFilter(const QString& searchText) {
    if (d->searchFilter != searchText) {
        d->searchFilter = searchText;
        
        beginResetModel();
        applyFilters();
        endResetModel();
        
        emit countChanged();
    }
}

bool TorrentListModel::hasActiveTorrents() const {
    return d->hasActiveTorrents;
}

int TorrentListModel::downloadingCount() const {
    return d->downloadingCount;
}

int TorrentListModel::seedingCount() const {
    return d->seedingCount;
}

qint64 TorrentListModel::totalDownloadSpeed() const {
    return d->totalDownloadSpeed;
}

qint64 TorrentListModel::totalUploadSpeed() const {
    return d->totalUploadSpeed;
}

QVariantMap TorrentListModel::getStatistics() const {
    QVariantMap stats;
    stats["totalTorrents"] = static_cast<int>(d->torrents.size());
    stats["downloadingCount"] = d->downloadingCount;
    stats["seedingCount"] = d->seedingCount;
    stats["totalDownloadSpeed"] = d->totalDownloadSpeed;
    stats["totalUploadSpeed"] = d->totalUploadSpeed;
    stats["hasActiveTorrents"] = d->hasActiveTorrents;
    
    // Calculate additional stats
    int completedCount = 0;
    int pausedCount = 0;
    int errorCount = 0;
    qint64 totalSize = 0;
    qint64 totalDownloaded = 0;
    
    for (const auto& torrent : d->torrents) {
        QString status = torrent.status.toLower();
        if (status == "completed") {
            completedCount++;
        } else if (status == "paused") {
            pausedCount++;
        } else if (status == "error") {
            errorCount++;
        }
        
        totalSize += torrent.size;
        totalDownloaded += static_cast<qint64>(torrent.progress * torrent.size);
    }
    
    stats["completedCount"] = completedCount;
    stats["pausedCount"] = pausedCount;
    stats["errorCount"] = errorCount;
    stats["totalSize"] = totalSize;
    stats["totalDownloaded"] = totalDownloaded;
    
    return stats;
}

void TorrentListModel::pauseAll() {
    if (!d->torrentEngine) {
        return;
    }
    
    for (const auto& torrent : d->torrents) {
        if (torrent.status.toLower() == "downloading") {
            pauseTorrent(torrent.infoHash);
        }
    }
}

void TorrentListModel::resumeAll() {
    if (!d->torrentEngine) {
        return;
    }
    
    for (const auto& torrent : d->torrents) {
        if (torrent.status.toLower() == "paused") {
            resumeTorrent(torrent.infoHash);
        }
    }
}

void TorrentListModel::removeCompleted() {
    if (!d->torrentEngine) {
        return;
    }
    
    QStringList toRemove;
    for (const auto& torrent : d->torrents) {
        if (torrent.status.toLower() == "completed") {
            toRemove.append(torrent.infoHash);
        }
    }
    
    for (const QString& infoHash : toRemove) {
        removeTorrent(infoHash, false);
    }
}

void TorrentListModel::removeErrored() {
    if (!d->torrentEngine) {
        return;
    }
    
    QStringList toRemove;
    for (const auto& torrent : d->torrents) {
        if (torrent.status.toLower() == "error") {
            toRemove.append(torrent.infoHash);
        }
    }
    
    for (const QString& infoHash : toRemove) {
        removeTorrent(infoHash, false);
    }
}

bool TorrentListModel::exportTorrentList(const QString& filePath) const {
    QJsonArray torrentArray;
    
    for (const auto& torrent : d->torrents) {
        QJsonObject torrentObj;
        torrentObj["infoHash"] = torrent.infoHash;
        torrentObj["name"] = torrent.name;
        torrentObj["magnetUri"] = torrent.magnetUri;
        torrentObj["savePath"] = torrent.savePath;
        torrentObj["size"] = torrent.size;
        torrentObj["addedAt"] = QDateTime().toString(Qt::ISODate); // not available
        torrentObj["priority"] = 1; // default
        torrentObj["sequentialDownload"] = false; // not available
        torrentObj["metadata"] = QJsonObject(); // not available
        
        torrentArray.append(torrentObj);
    }
    
    QJsonObject root;
    root["version"] = "1.0";
    root["exportedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["torrents"] = torrentArray;
    
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool TorrentListModel::importTorrentList(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        return false;
    }
    
    QJsonObject root = doc.object();
    QJsonArray torrentArray = root["torrents"].toArray();
    
    for (const auto& torrentValue : torrentArray) {
        QJsonObject torrentObj = torrentValue.toObject();
        QString magnetLink = torrentObj["magnetLink"].toString();
        QString savePath = torrentObj["savePath"].toString();
        
        if (!magnetLink.isEmpty()) {
            addTorrent(magnetLink, savePath);
        }
    }
    
    return true;
}

// Slots

void TorrentListModel::onTorrentAdded(const QString& infoHash) {
    if (!d->torrentEngine) {
        return;
    }
    
    auto torrentInfoResult = d->torrentEngine->getTorrentInfo(infoHash);
    if (torrentInfoResult.hasValue()) {
        const auto& info = torrentInfoResult.value();
        insertTorrentInfo(info);
        emit torrentAdded(infoHash, info.name);
    }
}

void TorrentListModel::onTorrentRemoved(const QString& infoHash) {
    int index = findTorrentIndex(infoHash);
    if (index >= 0) {
        removeTorrentInfo(index);
        emit torrentRemoved(infoHash);
    }
}

void TorrentListModel::onTorrentUpdated(const QString& infoHash) {
    if (!d->torrentEngine) {
        return;
    }
    
    int index = findTorrentIndex(infoHash);
    if (index >= 0) {
        auto torrentInfoResult = d->torrentEngine->getTorrentInfo(infoHash);
        if (torrentInfoResult.hasValue()) {
            const auto& newInfo = torrentInfoResult.value();
            updateTorrentInfo(index, newInfo);
        }
    }
}

void TorrentListModel::onTorrentStatusChanged(const QString& infoHash, const QString& status) {
    int index = findTorrentIndex(infoHash);
    if (index >= 0) {
        QString newStatus = status.toLower();
        if (d->torrents[index].status.toLower() != newStatus) {
            QString oldStatus = d->torrents[index].status;
            d->torrents[index].status = status;
            
            QModelIndex modelIndex = createIndex(index, 0);
            emit dataChanged(modelIndex, modelIndex, {StatusRole, StatusStringRole, IsActiveRole, IsCompleteRole});
            
            // Check for completion
            if (newStatus == "completed" && oldStatus.toLower() != "completed") {
                emit torrentCompleted(infoHash, d->torrents[index].name);
            }
            
            calculateStatistics();
        }
    }
}

void TorrentListModel::onTorrentProgressUpdated(const QString& infoHash, float progress) {
    int index = findTorrentIndex(infoHash);
    if (index >= 0) {
        d->torrents[index].progress = progress;
        
        QModelIndex modelIndex = createIndex(index, 0);
        emit dataChanged(modelIndex, modelIndex, {ProgressRole, IsCompleteRole});
    }
}

void TorrentListModel::onTorrentError(const QString& infoHash, const QString& error) {
    int index = findTorrentIndex(infoHash);
    if (index >= 0) {
        d->torrents[index].status = "error";
        // errorString field not available in TorrentEngine::TorrentInfo
        
        QModelIndex modelIndex = createIndex(index, 0);
        emit dataChanged(modelIndex, modelIndex, {StatusRole, StatusStringRole, ErrorStringRole, IsActiveRole});
        
        emit torrentError(infoHash, error);
        calculateStatistics();
    }
}

void TorrentListModel::updateStatistics() {
    calculateStatistics();
}

void TorrentListModel::refreshModel() {
    refresh();
}

// Private methods

void TorrentListModel::connectToTorrentEngine() {
    if (!d->torrentEngine) {
        return;
    }
    
    connect(d->torrentEngine, &TorrentEngine::torrentAdded, this, &TorrentListModel::onTorrentAdded);
    connect(d->torrentEngine, &TorrentEngine::torrentRemoved, this, &TorrentListModel::onTorrentRemoved);
    connect(d->torrentEngine, &TorrentEngine::torrentUpdated, this, &TorrentListModel::onTorrentUpdated);
    // Note: These signals may need to be added to TorrentEngine
    // connect(d->torrentEngine, &TorrentEngine::torrentStatusChanged, this, &TorrentListModel::onTorrentStatusChanged);
    // connect(d->torrentEngine, &TorrentEngine::torrentProgressUpdated, this, &TorrentListModel::onTorrentProgressUpdated);
    // connect(d->torrentEngine, &TorrentEngine::torrentError, this, &TorrentListModel::onTorrentError);
}

void TorrentListModel::disconnectFromTorrentEngine() {
    if (d->torrentEngine) {
        disconnect(d->torrentEngine, nullptr, this, nullptr);
        d->torrentEngine = nullptr;
    }
}

// createTorrentInfo method removed - using TorrentEngine::TorrentInfo directly

QVariantMap TorrentListModel::torrentInfoToVariant(const TorrentEngine::TorrentInfo& info) const {
    QVariantMap map;
    map["infoHash"] = info.infoHash;
    map["name"] = info.name;
    map["magnetUri"] = info.magnetUri;
    map["savePath"] = info.savePath;
    map["size"] = info.size;
    map["downloaded"] = static_cast<qint64>(info.progress * info.size); // estimate
    map["uploaded"] = qint64(0); // not available
    map["progress"] = info.progress;
    map["status"] = info.status;
    map["downloadSpeed"] = info.downloadRate;
    map["uploadSpeed"] = info.uploadRate;
    map["seeders"] = info.peers; // approximate
    map["leechers"] = 0; // not available
    map["connections"] = info.peers;
    map["addedAt"] = QDateTime(); // not available
    map["completedAt"] = QDateTime(); // not available
    map["errorString"] = QString(); // not available
    map["priority"] = 1; // default
    map["sequentialDownload"] = false; // not available
    map["creator"] = QString(); // not available
    map["comment"] = QString(); // not available
    map["metadata"] = QJsonObject(); // not available
    map["isValid"] = !info.infoHash.isEmpty() && !info.name.isEmpty();
    map["isActive"] = !info.isPaused && (info.isSeeding || info.progress < 1.0);
    map["isComplete"] = info.progress >= 1.0;
    return map;
}

// Status methods removed - using QString status directly from TorrentEngine

int TorrentListModel::findTorrentIndex(const QString& infoHash) const {
    for (size_t i = 0; i < d->torrents.size(); ++i) {
        if (d->torrents[i].infoHash == infoHash) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void TorrentListModel::sortTorrents() {
    std::sort(d->torrents.begin(), d->torrents.end(), 
              [this](const TorrentEngine::TorrentInfo& a, const TorrentEngine::TorrentInfo& b) -> bool {
        bool ascending = (d->sortOrder == Qt::AscendingOrder);
        
        if (d->sortField == "name") {
            return ascending ? (a.name < b.name) : (a.name > b.name);
        } else if (d->sortField == "size") {
            return ascending ? (a.size < b.size) : (a.size > b.size);
        } else if (d->sortField == "progress") {
            return ascending ? (a.progress < b.progress) : (a.progress > b.progress);
        } else if (d->sortField == "status") {
            return ascending ? (a.status < b.status) : (a.status > b.status);
        } else if (d->sortField == "downloadSpeed") {
            return ascending ? (a.downloadRate < b.downloadRate) : (a.downloadRate > b.downloadRate);
        }
        
        // Default sort by name if field not found
        return ascending ? (a.name < b.name) : (a.name > b.name);
    });
}

void TorrentListModel::applyFilters() {
    d->filteredIndices.clear();
    d->filtersApplied = false;
    
    bool hasStatusFilter = !d->statusFilter.isEmpty();
    bool hasSearchFilter = !d->searchFilter.isEmpty();
    
    if (!hasStatusFilter && !hasSearchFilter) {
        return; // No filters to apply
    }
    
    d->filtersApplied = true;
    QString statusFilter = d->statusFilter.toLower();
    QString searchLower = d->searchFilter.toLower();
    
    for (size_t i = 0; i < d->torrents.size(); ++i) {
        const TorrentEngine::TorrentInfo& torrent = d->torrents[i];
        bool passes = true;
        
        if (hasStatusFilter && torrent.status.toLower() != statusFilter) {
            passes = false;
        }
        
        if (passes && hasSearchFilter) {
            bool matchesSearch = torrent.name.toLower().contains(searchLower) ||
                               torrent.infoHash.toLower().contains(searchLower) ||
                               false; // creator field not available
            if (!matchesSearch) {
                passes = false;
            }
        }
        
        if (passes) {
            d->filteredIndices.push_back(static_cast<int>(i));
        }
    }
}

bool TorrentListModel::passesMeta(const TorrentEngine::TorrentInfo& info) const {
    Q_UNUSED(info)
    // Additional filtering logic could go here
    return true;
}

void TorrentListModel::updateTorrentInfo(int index, const TorrentEngine::TorrentInfo& newInfo) {
    if (index >= 0 && index < static_cast<int>(d->torrents.size())) {
        d->torrents[index] = newInfo;
        
        QModelIndex modelIndex = createIndex(index, 0);
        emit dataChanged(modelIndex, modelIndex);
        
        calculateStatistics();
    }
}

void TorrentListModel::insertTorrentInfo(const TorrentEngine::TorrentInfo& info) {
    beginInsertRows(QModelIndex(), rowCount(), rowCount());
    d->torrents.push_back(info);
    endInsertRows();
    
    sortTorrents();
    
    beginResetModel();
    applyFilters();
    endResetModel();
    
    calculateStatistics();
    emit countChanged();
}

void TorrentListModel::removeTorrentInfo(int index) {
    if (index >= 0 && index < static_cast<int>(d->torrents.size())) {
        beginRemoveRows(QModelIndex(), index, index);
        d->torrents.erase(d->torrents.begin() + index);
        endRemoveRows();
        
        beginResetModel();
        applyFilters();
        endResetModel();
        
        calculateStatistics();
        emit countChanged();
    }
}

void TorrentListModel::calculateStatistics() {
    bool oldHasActive = d->hasActiveTorrents;
    int oldDownloading = d->downloadingCount;
    int oldSeeding = d->seedingCount;
    qint64 oldDownSpeed = d->totalDownloadSpeed;
    qint64 oldUpSpeed = d->totalUploadSpeed;
    
    d->hasActiveTorrents = false;
    d->downloadingCount = 0;
    d->seedingCount = 0;
    d->totalDownloadSpeed = 0;
    d->totalUploadSpeed = 0;
    
    for (const auto& torrent : d->torrents) {
        bool isActive = !torrent.isPaused && (torrent.isSeeding || torrent.progress < 1.0);
        if (isActive) {
            d->hasActiveTorrents = true;
        }
        
        QString status = torrent.status.toLower();
        if (status == "downloading") {
            d->downloadingCount++;
            d->totalDownloadSpeed += torrent.downloadRate;
        } else if (status == "seeding") {
            d->seedingCount++;
            d->totalUploadSpeed += torrent.uploadRate;
        }
    }
    
    // Emit signals if values changed
    if (oldHasActive != d->hasActiveTorrents) {
        emit hasActiveTorrentsChanged();
    }
    if (oldDownloading != d->downloadingCount) {
        emit downloadingCountChanged();
    }
    if (oldSeeding != d->seedingCount) {
        emit seedingCountChanged();
    }
    if (oldDownSpeed != d->totalDownloadSpeed) {
        emit totalDownloadSpeedChanged();
    }
    if (oldUpSpeed != d->totalUploadSpeed) {
        emit totalUploadSpeedChanged();
    }
    
    emit statisticsChanged();
}

} // namespace Murmur