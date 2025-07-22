#pragma once

#include <QtCore/QAbstractListModel>
#include <QtCore/QDateTime>
#include <QtCore/QJsonObject>
#include <QtCore/QTimer>
#include <QtQml/QQmlEngine>
#include <memory>
#include <vector>

#include "core/common/Expected.hpp"
#include "core/torrent/TorrentEngine.hpp"

namespace Murmur {

class TorrentEngine;

// Using TorrentEngine::TorrentInfo struct instead of local definition

class TorrentListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool hasActiveTorrents READ hasActiveTorrents NOTIFY hasActiveTorrentsChanged)
    Q_PROPERTY(int downloadingCount READ downloadingCount NOTIFY downloadingCountChanged)
    Q_PROPERTY(int seedingCount READ seedingCount NOTIFY seedingCountChanged)
    Q_PROPERTY(qint64 totalDownloadSpeed READ totalDownloadSpeed NOTIFY totalDownloadSpeedChanged)
    Q_PROPERTY(qint64 totalUploadSpeed READ totalUploadSpeed NOTIFY totalUploadSpeedChanged)

public:
    enum Roles {
        InfoHashRole = Qt::UserRole + 1,
        NameRole,
        MagnetLinkRole,
        SavePathRole,
        SizeRole,
        DownloadedRole,
        UploadedRole,
        ProgressRole,
        StatusRole,
        StatusStringRole,
        DownloadSpeedRole,
        UploadSpeedRole,
        SeedersRole,
        LeechersRole,
        ConnectionsRole,
        AddedAtRole,
        CompletedAtRole,
        ErrorStringRole,
        MetadataRole,
        PriorityRole,
        SequentialDownloadRole,
        CreatorRole,
        CommentRole,
        IsValidRole,
        IsActiveRole,
        IsCompleteRole
    };
    Q_ENUM(Roles)

    explicit TorrentListModel(QObject* parent = nullptr);
    ~TorrentListModel() override;

    // QAbstractListModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Model management
    Q_INVOKABLE void setTorrentEngine(QObject* torrentEngine);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clear();

    // Torrent operations
    Q_INVOKABLE bool addTorrent(const QString& magnetLink, const QString& savePath = QString());
    Q_INVOKABLE bool addTorrentFile(const QString& filePath, const QString& savePath = QString());
    Q_INVOKABLE bool removeTorrent(const QString& infoHash, bool deleteFiles = false);
    Q_INVOKABLE bool pauseTorrent(const QString& infoHash);
    Q_INVOKABLE bool resumeTorrent(const QString& infoHash);
    Q_INVOKABLE bool recheckTorrent(const QString& infoHash);
    Q_INVOKABLE bool setTorrentPriority(const QString& infoHash, int priority);
    Q_INVOKABLE bool setSequentialDownload(const QString& infoHash, bool sequential);

    // Torrent queries
    Q_INVOKABLE QVariantMap getTorrentInfo(const QString& infoHash) const;
    Q_INVOKABLE QStringList getInfoHashes() const;
    Q_INVOKABLE QString getTorrentName(const QString& infoHash) const;
    Q_INVOKABLE float getTorrentProgress(const QString& infoHash) const;
    Q_INVOKABLE QString getTorrentStatus(const QString& infoHash) const;
    Q_INVOKABLE QVariantList getActiveTorrents() const;
    Q_INVOKABLE QVariantList getTorrentsByStatus(const QString& status) const;

    // Filtering and sorting
    Q_INVOKABLE void setSortField(const QString& field);
    Q_INVOKABLE void setSortOrder(Qt::SortOrder order);
    Q_INVOKABLE void setStatusFilter(const QString& status);
    Q_INVOKABLE void setSearchFilter(const QString& searchText);

    // Statistics
    bool hasActiveTorrents() const;
    int downloadingCount() const;
    int seedingCount() const;
    qint64 totalDownloadSpeed() const;
    qint64 totalUploadSpeed() const;
    Q_INVOKABLE QVariantMap getStatistics() const;

    // Batch operations
    Q_INVOKABLE void pauseAll();
    Q_INVOKABLE void resumeAll();
    Q_INVOKABLE void removeCompleted();
    Q_INVOKABLE void removeErrored();

    // Import/Export
    Q_INVOKABLE bool exportTorrentList(const QString& filePath) const;
    Q_INVOKABLE bool importTorrentList(const QString& filePath);

public slots:
    void onTorrentAdded(const QString& infoHash);
    void onTorrentRemoved(const QString& infoHash);
    void onTorrentUpdated(const QString& infoHash);
    void onTorrentStatusChanged(const QString& infoHash, const QString& status);
    void onTorrentProgressUpdated(const QString& infoHash, float progress);
    void onTorrentError(const QString& infoHash, const QString& error);

signals:
    void countChanged();
    void hasActiveTorrentsChanged();
    void downloadingCountChanged();
    void seedingCountChanged();
    void totalDownloadSpeedChanged();
    void totalUploadSpeedChanged();
    void torrentAdded(const QString& infoHash, const QString& name);
    void torrentRemoved(const QString& infoHash);
    void torrentCompleted(const QString& infoHash, const QString& name);
    void torrentError(const QString& infoHash, const QString& error);
    void statisticsChanged();

private slots:
    void updateStatistics();
    void refreshModel();

private:
    class TorrentListModelPrivate;
    std::unique_ptr<TorrentListModelPrivate> d;

    // Helper methods
    void connectToTorrentEngine();
    void disconnectFromTorrentEngine();
    QVariantMap torrentInfoToVariant(const TorrentEngine::TorrentInfo& info) const;
    int findTorrentIndex(const QString& infoHash) const;
    void sortTorrents();
    void applyFilters();
    bool passesMeta(const TorrentEngine::TorrentInfo& info) const;
    void updateTorrentInfo(int index, const TorrentEngine::TorrentInfo& newInfo);
    void insertTorrentInfo(const TorrentEngine::TorrentInfo& info);
    void removeTorrentInfo(int index);
    void calculateStatistics();
};

} // namespace Murmur