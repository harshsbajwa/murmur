#include "TorrentController.hpp"
#include "../../core/torrent/TorrentStateModel.hpp"
#include "../../core/common/Logger.hpp"
#include <QtCore/QFutureWatcher>
#include <QtCore/QFileInfo>

namespace Murmur {

TorrentController::TorrentController(QObject* parent)
    : QObject(parent), ready_(false) {
}

void TorrentController::setReady(bool ready) {
    if (ready_ != ready) {
        ready_ = ready;
        Logger::instance().debug("TorrentController ready state changed to: {}", ready ? "true" : "false");
        emit readyChanged();
    }
}

bool TorrentController::isReady() const {
    return ready_;
}

void TorrentController::updateReadyState() {
    bool isReadyNow = torrentEngine_ != nullptr;
    setReady(isReadyNow);
}

TorrentStateModel* TorrentController::torrentModel() const {
    return torrentEngine_ ? torrentEngine_->torrentModel() : nullptr;
}

int TorrentController::activeTorrentsCount() const {
    auto model = torrentModel();
    return model ? model->activeTorrentsCount() : 0;
}

int TorrentController::seedingTorrentsCount() const {
    auto model = torrentModel();
    return model ? model->seedingTorrentsCount() : 0;
}

void TorrentController::setTorrentEngine(TorrentEngine* engine) {
    if (torrentEngine_ != engine) {
        // Disconnect old engine
        if (torrentEngine_) {
            disconnect(torrentEngine_, nullptr, this, nullptr);
        }
        
        torrentEngine_ = engine;
        
        // Connect new engine
        if (torrentEngine_) {
            connect(torrentEngine_, &TorrentEngine::torrentAdded,
                    this, &TorrentController::handleTorrentAdded);
            connect(torrentEngine_, &TorrentEngine::torrentRemoved,
                    this, &TorrentController::handleTorrentRemoved);
            connect(torrentEngine_, &TorrentEngine::torrentError,
                    this, &TorrentController::handleTorrentError);
            
            auto model = torrentEngine_->torrentModel();
            if (model) {
                connect(model, &TorrentStateModel::torrentCountChanged,
                        this, &TorrentController::handleModelCountChanged);
            }
            
        Logger::instance().info("TorrentEngine connected successfully");
    } else {
        Logger::instance().warn("TorrentEngine set to null");
    }
    
    updateReadyState();
    emit busyChanged(); // Emit signal to update UI
    emit torrentModelChanged(); // Notify QML that the model has changed
}
}

void TorrentController::addTorrent(const QString& magnetUri) {
    if (!torrentEngine_) {
        Logger::instance().warn("Torrent engine not available");
        emit torrentError("", "Torrent engine not available");
        return;
    }
    
    if (magnetUri.isEmpty()) {
        Logger::instance().warn("Magnet URI is empty");
        emit torrentError("", "Magnet URI is empty");
        return;
    }
    
    // Validate magnet URI format
    if (!magnetUri.startsWith("magnet:?")) {
        Logger::instance().warn("Invalid magnet URI format: {}", magnetUri.toStdString());
        emit torrentError("", "Invalid magnet URI format");
        return;
    }
    
    Logger::instance().info("Adding torrent: {}", magnetUri.left(50).toStdString() + "...");
    
    setBusy(true);
    auto future = torrentEngine_->addTorrent(magnetUri);
    handleAsyncOperation(future, "add torrent: " + magnetUri);
}

void TorrentController::addTorrentFromFile(const QUrl& torrentFile) {
    if (!torrentEngine_) {
        Logger::instance().warn("Torrent engine not available");
        emit torrentError("", "Torrent engine not available");
        return;
    }
    
    QString localPath = torrentFile.toLocalFile();
    if (localPath.isEmpty()) {
        // Try to get the path directly from QUrl if toLocalFile() fails
        localPath = torrentFile.path();
    }
    
    // Handle file:// prefix if present
    if (localPath.startsWith("file://")) {
        localPath = localPath.mid(7); // Remove "file://" prefix
    }
    
    QFileInfo fileInfo(localPath);
    
    if (localPath.isEmpty()) {
        emit torrentError("", "Invalid torrent file path");
        return;
    }
    
    if (!fileInfo.exists()) {
        emit torrentError("", "Torrent file does not exist: " + localPath);
        return;
    }
    
    if (!fileInfo.suffix().toLower().contains("torrent")) {
        emit torrentError("", "Invalid torrent file extension: " + localPath);
        return;
    }
    
    Logger::instance().info("Adding torrent from file: {}", localPath.toStdString());
    
    setBusy(true);
    auto future = torrentEngine_->addTorrentFromFile(localPath);
    handleAsyncOperation(future, "Adding torrent from file: " + fileInfo.fileName());
}

void TorrentController::seedFile(const QUrl& filePath) {
    if (!torrentEngine_ || isBusy_) {
        return;
    }
    
    QString localPath = filePath.toLocalFile();
    QFileInfo fileInfo(localPath);
    
    if (!fileInfo.exists()) {
        emit torrentError("", "File does not exist: " + localPath);
        return;
    }
    
    Logger::instance().info("Seeding file: {}", localPath.toStdString());
    
    setBusy(true);
    auto future = torrentEngine_->seedFile(localPath);
    handleAsyncOperation(future, "seed file");
}

void TorrentController::removeTorrent(const QString& infoHash) {
    if (!torrentEngine_) {
        return;
    }
    
    Logger::instance().info("Removing torrent: {}", infoHash.toStdString());
    
    auto result = torrentEngine_->removeTorrent(infoHash);
    if (result.hasError()) {
        emit torrentError(infoHash, errorToString(result.error()));
    } else {
        emit operationCompleted("Torrent removed successfully");
    }
}

void TorrentController::pauseTorrent(const QString& infoHash) {
    if (!torrentEngine_) {
        return;
    }
    
    auto result = torrentEngine_->pauseTorrent(infoHash);
    if (result.hasError()) {
        emit torrentError(infoHash, errorToString(result.error()));
    } else {
        emit operationCompleted("Torrent paused");
    }
}

void TorrentController::resumeTorrent(const QString& infoHash) {
    if (!torrentEngine_) {
        return;
    }
    
    auto result = torrentEngine_->resumeTorrent(infoHash);
    if (result.hasError()) {
        emit torrentError(infoHash, errorToString(result.error()));
    } else {
        emit operationCompleted("Torrent resumed");
    }
}

void TorrentController::pauseAllTorrents() {
    if (!torrentEngine_) {
        return;
    }
    
    auto torrents = torrentEngine_->getActiveTorrents();
    for (const auto& torrent : torrents) {
        if (!torrent.isPaused) {
            pauseTorrent(torrent.infoHash);
        }
    }
}

void TorrentController::resumeAllTorrents() {
    if (!torrentEngine_) {
        return;
    }
    
    auto torrents = torrentEngine_->getActiveTorrents();
    for (const auto& torrent : torrents) {
        if (torrent.isPaused) {
            resumeTorrent(torrent.infoHash);
        }
    }
}

void TorrentController::setDownloadPath(const QString& path) {
    if (torrentEngine_) {
        torrentEngine_->setDownloadPath(path);
        emit operationCompleted("Download path updated");
    }
}

void TorrentController::configureSession(int maxConnections, int uploadRate, int downloadRate) {
    if (torrentEngine_) {
        torrentEngine_->configureSession(maxConnections, uploadRate, downloadRate);
        emit operationCompleted("Session configuration updated");
    }
}

void TorrentController::handleTorrentAdded(const QString& infoHash) {
    setBusy(false);
    
    // Force model refresh
    if (auto model = torrentModel()) {
        emit model->layoutChanged(); // Force ListView update
    }
    
    emit torrentModelChanged();
    emit torrentAdded(infoHash);
    emit operationCompleted("Torrent added successfully");
}

void TorrentController::handleTorrentRemoved(const QString& infoHash) {
    emit torrentRemoved(infoHash);
}

void TorrentController::handleTorrentError(const QString& infoHash, TorrentError error) {
    setBusy(false);
    emit torrentError(infoHash, errorToString(error));
}

void TorrentController::handleModelCountChanged() {
    emit torrentsCountChanged();
}

void TorrentController::setBusy(bool busy) {
    if (isBusy_ != busy) {
        isBusy_ = busy;
        emit busyChanged();
    }
}

QString TorrentController::errorToString(TorrentError error) const {
    switch (error) {
        case TorrentError::InvalidMagnetUri:
            return "Invalid magnet URI";
        case TorrentError::NetworkFailure:
            return "Network failure";
        case TorrentError::InsufficientSpace:
            return "Insufficient disk space";
        case TorrentError::PermissionDenied:
            return "Permission denied";
        case TorrentError::LibtorrentError:
            return "LibTorrent error";
        case TorrentError::SecurityViolation:
            return "Security violation";
        case TorrentError::TorrentNotFound:
            return "Torrent not found";
        case TorrentError::UnknownError:
            return "Unknown error";
        default:
            return "Unknown error";
    }
}

void TorrentController::handleAsyncOperation(
    QFuture<Expected<TorrentEngine::TorrentInfo, TorrentError>> future,
    const QString& operation) {
    
    auto watcher = new QFutureWatcher<Expected<TorrentEngine::TorrentInfo, TorrentError>>(this);
    
    connect(watcher, &QFutureWatcher<Expected<TorrentEngine::TorrentInfo, TorrentError>>::finished,
            [this, watcher, operation]() {
        
        auto result = watcher->result();
        watcher->deleteLater();
        
        setBusy(false);
        
        if (result.hasError()) {
            emit torrentError("", QString("Failed to %1: %2")
                           .arg(operation, errorToString(result.error())));
        } else {
            emit operationCompleted(QString("Successfully completed: %1").arg(operation));
        }
    });
    
    watcher->setFuture(future);
}

} // namespace Murmur