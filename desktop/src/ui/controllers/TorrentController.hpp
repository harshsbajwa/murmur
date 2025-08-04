#pragma once

#include "../../core/torrent/TorrentEngine.hpp"
#include "../../core/torrent/TorrentStateModel.hpp"
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QUrl>

namespace Murmur {

class TorrentStateModel;

} // namespace Murmur

Q_DECLARE_OPAQUE_POINTER(Murmur::TorrentStateModel*)

namespace Murmur {

class TorrentController : public QObject {
    Q_OBJECT
    Q_PROPERTY(TorrentStateModel* torrentModel READ torrentModel NOTIFY torrentModelChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY busyChanged)
Q_PROPERTY(int activeTorrentsCount READ activeTorrentsCount NOTIFY torrentsCountChanged)
Q_PROPERTY(int seedingTorrentsCount READ seedingTorrentsCount NOTIFY torrentsCountChanged)
Q_PROPERTY(bool isReady READ isReady NOTIFY readyChanged)
    
public:
    explicit TorrentController(QObject* parent = nullptr);
    
    TorrentStateModel* torrentModel() const;
    bool isBusy() const { return isBusy_; }
    int activeTorrentsCount() const;
    int seedingTorrentsCount() const;
bool isReady() const;

Q_INVOKABLE void setTorrentEngine(TorrentEngine* engine);

void setReady(bool ready);
void updateReadyState();
    
public slots:
    void addTorrent(const QString& magnetUri);
    void addTorrentFromFile(const QUrl& torrentFile);
    void seedFile(const QUrl& filePath);
    void removeTorrent(const QString& infoHash);
    void pauseTorrent(const QString& infoHash);
    void resumeTorrent(const QString& infoHash);
    void pauseAllTorrents();
    void resumeAllTorrents();
    
    // Configuration
    void setDownloadPath(const QString& path);
    void configureSession(int maxConnections, int uploadRate, int downloadRate);
    
signals:
    void readyChanged();
    void busyChanged();
    void torrentsCountChanged();
    void torrentModelChanged();
    void torrentAdded(const QString& infoHash);
    void torrentRemoved(const QString& infoHash);
    void torrentError(const QString& infoHash, const QString& error);
    void operationCompleted(const QString& message);
    
private slots:
    void handleTorrentAdded(const QString& infoHash);
    void handleTorrentRemoved(const QString& infoHash);
    void handleTorrentError(const QString& infoHash, TorrentError error);
    void handleModelCountChanged();
    
private:
TorrentEngine* torrentEngine_ = nullptr;

bool ready_ = false;
bool isBusy_ = false;

void setBusy(bool busy);
QString errorToString(TorrentError error) const;
void handleAsyncOperation(QFuture<Expected<TorrentEngine::TorrentInfo, TorrentError>> future,
                         const QString& operation);
};

} // namespace Murmur