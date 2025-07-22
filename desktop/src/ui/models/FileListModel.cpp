#include "FileListModel.hpp"
#include "core/storage/FileManager.hpp"
#include "core/common/Logger.hpp"

#include <QtCore/QDir>
#include <QtCore/QStandardPaths>
#include <QtCore/QMimeDatabase>
#include <QtCore/QCollator>
#include <QtCore/QSettings>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QProcess>
#include <QtCore/QRegularExpression>
#include <QtGui/QDesktopServices>
#include <algorithm>

namespace Murmur {

QString FileItemInfo::formatSize(qint64 bytes) {
    if (bytes == 0) return "0 B";
    
    const int unit = 1024;
    const QStringList units = {"B", "KB", "MB", "GB", "TB", "PB"};
    
    int digitGroups = static_cast<int>(qLn(qAbs(bytes)) / qLn(unit));
    return QString("%1 %2")
           .arg(static_cast<double>(bytes) / qPow(unit, digitGroups), 0, 'f', 1)
           .arg(units[digitGroups]);
}

class FileListModel::FileListModelPrivate {
public:
    FileListModelPrivate() = default;
    ~FileListModelPrivate() = default;

    FileManager* fileManager = nullptr;
    QString currentPath;
    QStringList navigationHistory;
    int historyIndex = -1;
    
    std::vector<FileItemInfo> files;
    std::vector<int> filteredIndices;
    bool filtersApplied = false;
    
    // Display settings
    QString filter;
    bool showHidden = false;
    bool showDirectories = true;
    SortField sortField = SortField::Name;
    Qt::SortOrder sortOrder = Qt::AscendingOrder;
    
    // Selection
    std::vector<bool> selection;
    
    // Statistics
    int fileCount = 0;
    int directoryCount = 0;
    qint64 totalSize = 0;
    
    // File watching
    std::unique_ptr<QFileSystemWatcher> fileWatcher;
    bool watchingEnabled = true;
    
    // Search
    QString searchQuery;
    bool searchActive = false;
    
    // Thumbnails
    bool thumbnailsEnabled = false;
    QString thumbnailCacheDir;
    
    // Bookmarks
    QStringList bookmarks;
    
    // MIME database for file type detection
    QMimeDatabase mimeDatabase;
};

FileListModel::FileListModel(QObject* parent)
    : QAbstractListModel(parent)
    , d(std::make_unique<FileListModelPrivate>())
{
    // Initialize thumbnail cache directory
    d->thumbnailCacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
    QDir().mkpath(d->thumbnailCacheDir);
    
    // Load bookmarks
    loadBookmarks();
    
    // Set up file watcher
    d->fileWatcher = std::make_unique<QFileSystemWatcher>(this);
    connect(d->fileWatcher.get(), &QFileSystemWatcher::directoryChanged,
            this, &FileListModel::onDirectoryChanged);
    connect(d->fileWatcher.get(), &QFileSystemWatcher::fileChanged,
            this, &FileListModel::onFileChanged);
    
    // Set initial path to home directory
    setCurrentPath(getHomeDirectory());
}

FileListModel::~FileListModel() {
    saveBookmarks();
}

int FileListModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent)
    
    if (d->filtersApplied) {
        return static_cast<int>(d->filteredIndices.size());
    }
    return static_cast<int>(d->files.size());
}

QVariant FileListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return QVariant();
    }
    
    int fileIndex = index.row();
    if (d->filtersApplied) {
        if (fileIndex >= static_cast<int>(d->filteredIndices.size())) {
            return QVariant();
        }
        fileIndex = d->filteredIndices[fileIndex];
    }
    
    if (fileIndex >= static_cast<int>(d->files.size())) {
        return QVariant();
    }
    
    const FileItemInfo& file = d->files[fileIndex];
    
    switch (role) {
        case FileNameRole:
            return file.fileName;
        case FilePathRole:
            return file.filePath;
        case AbsolutePathRole:
            return file.absolutePath;
        case BaseNameRole:
            return file.baseName;
        case SuffixRole:
            return file.suffix;
        case ParentDirRole:
            return file.parentDir;
        case SizeRole:
            return file.size;
        case SizeStringRole:
            return file.sizeString();
        case TypeRole:
            return static_cast<int>(file.type);
        case TypeStringRole:
            return file.typeString();
        case CreatedRole:
            return file.created;
        case ModifiedRole:
            return file.modified;
        case LastAccessedRole:
            return file.lastAccessed;
        case IsDirectoryRole:
            return file.isDirectory;
        case IsHiddenRole:
            return file.isHidden;
        case IsReadableRole:
            return file.isReadable;
        case IsWritableRole:
            return file.isWritable;
        case IsExecutableRole:
            return file.isExecutable;
        case IsSymLinkRole:
            return file.isSymLink;
        case MimeTypeRole:
            return file.mimeType;
        case IconNameRole:
            return file.iconName;
        case TagsRole:
            return QVariant::fromValue(file.tags);
        case DescriptionRole:
            return file.description;
        case DurationRole:
            return file.duration;
        case ResolutionRole:
            return file.resolution;
        case CodecRole:
            return file.codec;
        case BitrateRole:
            return file.bitrate;
        case FileCountRole:
            return file.fileCount;
        case UncompressedSizeRole:
            return file.uncompressedSize;
        case IsValidRole:
            return file.isValid();
        case IsMediaFileRole:
            return file.isMediaFile();
        case CanPlayRole:
            return file.canPlay();
        default:
            return QVariant();
    }
}

QHash<int, QByteArray> FileListModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[FileNameRole] = "fileName";
    roles[FilePathRole] = "filePath";
    roles[AbsolutePathRole] = "absolutePath";
    roles[BaseNameRole] = "baseName";
    roles[SuffixRole] = "suffix";
    roles[ParentDirRole] = "parentDir";
    roles[SizeRole] = "size";
    roles[SizeStringRole] = "sizeString";
    roles[TypeRole] = "type";
    roles[TypeStringRole] = "typeString";
    roles[CreatedRole] = "created";
    roles[ModifiedRole] = "modified";
    roles[LastAccessedRole] = "lastAccessed";
    roles[IsDirectoryRole] = "isDirectory";
    roles[IsHiddenRole] = "isHidden";
    roles[IsReadableRole] = "isReadable";
    roles[IsWritableRole] = "isWritable";
    roles[IsExecutableRole] = "isExecutable";
    roles[IsSymLinkRole] = "isSymLink";
    roles[MimeTypeRole] = "mimeType";
    roles[IconNameRole] = "iconName";
    roles[TagsRole] = "tags";
    roles[DescriptionRole] = "description";
    roles[DurationRole] = "duration";
    roles[ResolutionRole] = "resolution";
    roles[CodecRole] = "codec";
    roles[BitrateRole] = "bitrate";
    roles[FileCountRole] = "fileCount";
    roles[UncompressedSizeRole] = "uncompressedSize";
    roles[IsValidRole] = "isValid";
    roles[IsMediaFileRole] = "isMediaFile";
    roles[CanPlayRole] = "canPlay";
    return roles;
}

QString FileListModel::currentPath() const {
    return d->currentPath;
}

void FileListModel::setCurrentPath(const QString& path) {
    navigateToPath(path);
}

bool FileListModel::navigateToPath(const QString& path) {
    QDir dir(path);
    if (!dir.exists()) {
        emit fileSystemError(tr("Directory does not exist: %1").arg(path));
        return false;
    }
    
    QString canonicalPath = dir.canonicalPath();
    if (canonicalPath == d->currentPath) {
        return true;
    }
    
    // Disconnect old path from watcher
    if (d->watchingEnabled && !d->currentPath.isEmpty()) {
        d->fileWatcher->removePath(d->currentPath);
    }
    
    d->currentPath = canonicalPath;
    pushToHistory(canonicalPath);
    
    // Connect new path to watcher
    if (d->watchingEnabled) {
        d->fileWatcher->addPath(canonicalPath);
    }
    
    loadDirectory(canonicalPath);
    
    emit currentPathChanged();
    emit canGoUpChanged();
    emit canGoBackChanged();
    emit canGoForwardChanged();
    emit directoryChanged(canonicalPath);
    
    return true;
}

bool FileListModel::navigateUp() {
    QDir currentDir(d->currentPath);
    if (currentDir.cdUp()) {
        return navigateToPath(currentDir.absolutePath());
    }
    return false;
}

bool FileListModel::navigateBack() {
    if (d->historyIndex > 0) {
        d->historyIndex--;
        QString path = d->navigationHistory[d->historyIndex];
        
        // Temporarily disable history updates
        int oldIndex = d->historyIndex;
        bool result = navigateToPath(path);
        d->historyIndex = oldIndex;
        
        emit canGoBackChanged();
        emit canGoForwardChanged();
        return result;
    }
    return false;
}

bool FileListModel::navigateForward() {
    if (d->historyIndex < d->navigationHistory.size() - 1) {
        d->historyIndex++;
        QString path = d->navigationHistory[d->historyIndex];
        
        // Temporarily disable history updates
        int oldIndex = d->historyIndex;
        bool result = navigateToPath(path);
        d->historyIndex = oldIndex;
        
        emit canGoBackChanged();
        emit canGoForwardChanged();
        return result;
    }
    return false;
}

bool FileListModel::navigateToDirectory(int index) {
    QVariantMap fileInfo = getFileInfo(index);
    if (fileInfo["isDirectory"].toBool()) {
        return navigateToPath(fileInfo["absolutePath"].toString());
    }
    return false;
}

QString FileListModel::getParentPath() const {
    QDir currentDir(d->currentPath);
    if (currentDir.cdUp()) {
        return currentDir.absolutePath();
    }
    return QString();
}

QStringList FileListModel::getPathComponents() const {
    return d->currentPath.split('/', Qt::SkipEmptyParts);
}

QString FileListModel::getHomeDirectory() const {
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
}

bool FileListModel::canGoUp() const {
    QDir currentDir(d->currentPath);
    return currentDir.cdUp();
}

bool FileListModel::canGoBack() const {
    return d->historyIndex > 0;
}

bool FileListModel::canGoForward() const {
    return d->historyIndex < d->navigationHistory.size() - 1;
}

QString FileListModel::filter() const {
    return d->filter;
}

void FileListModel::setFilter(const QString& filter) {
    if (d->filter != filter) {
        d->filter = filter;
        
        beginResetModel();
        applyFilters();
        endResetModel();
        
        emit filterChanged();
        emit countChanged();
    }
}

bool FileListModel::showHidden() const {
    return d->showHidden;
}

void FileListModel::setShowHidden(bool show) {
    if (d->showHidden != show) {
        d->showHidden = show;
        
        beginResetModel();
        applyFilters();
        endResetModel();
        
        emit showHiddenChanged();
        emit countChanged();
    }
}

bool FileListModel::showDirectories() const {
    return d->showDirectories;
}

void FileListModel::setShowDirectories(bool show) {
    if (d->showDirectories != show) {
        d->showDirectories = show;
        
        beginResetModel();
        applyFilters();
        endResetModel();
        
        emit showDirectoriesChanged();
        emit countChanged();
    }
}

SortField FileListModel::sortField() const {
    return d->sortField;
}

void FileListModel::setSortField(SortField field) {
    if (d->sortField != field) {
        d->sortField = field;
        sortFiles();
        emit sortFieldChanged();
    }
}

Qt::SortOrder FileListModel::sortOrder() const {
    return d->sortOrder;
}

void FileListModel::setSortOrder(Qt::SortOrder order) {
    if (d->sortOrder != order) {
        d->sortOrder = order;
        sortFiles();
        emit sortOrderChanged();
    }
}

void FileListModel::sortFiles() {
    beginResetModel();
    std::sort(d->files.begin(), d->files.end(),
              [this](const FileItemInfo& a, const FileItemInfo& b) {
        return compareFiles(a, b) < 0;
    });
    applyFilters();
    endResetModel();
}

bool FileListModel::createDirectory(const QString& name) {
    if (name.isEmpty() || d->currentPath.isEmpty()) {
        return false;
    }
    
    QDir currentDir(d->currentPath);
    bool success = currentDir.mkdir(name);
    
    if (success) {
        refresh();
    } else {
        emit fileSystemError(tr("Failed to create directory: %1").arg(name));
    }
    
    return success;
}

bool FileListModel::deleteFile(int index) {
    QVariantMap fileInfo = getFileInfo(index);
    if (fileInfo.isEmpty()) {
        return false;
    }
    
    return deleteFile(fileInfo["fileName"].toString());
}

bool FileListModel::deleteFile(const QString& fileName) {
    if (fileName.isEmpty() || d->currentPath.isEmpty()) {
        return false;
    }
    
    QString filePath = QDir(d->currentPath).absoluteFilePath(fileName);
    QFileInfo fileInfo(filePath);
    
    bool success = false;
    if (fileInfo.isDir()) {
        QDir dir(filePath);
        success = dir.removeRecursively();
    } else {
        success = QFile::remove(filePath);
    }
    
    if (success) {
        refresh();
    } else {
        emit fileSystemError(tr("Failed to delete: %1").arg(fileName));
    }
    
    return success;
}

bool FileListModel::renameFile(int index, const QString& newName) {
    QVariantMap fileInfo = getFileInfo(index);
    if (fileInfo.isEmpty()) {
        return false;
    }
    
    return renameFile(fileInfo["fileName"].toString(), newName);
}

bool FileListModel::renameFile(const QString& oldName, const QString& newName) {
    if (oldName.isEmpty() || newName.isEmpty() || d->currentPath.isEmpty()) {
        return false;
    }
    
    QDir currentDir(d->currentPath);
    QString oldPath = currentDir.absoluteFilePath(oldName);
    QString newPath = currentDir.absoluteFilePath(newName);
    
    bool success = QFile::rename(oldPath, newPath);
    
    if (success) {
        refresh();
    } else {
        emit fileSystemError(tr("Failed to rename %1 to %2").arg(oldName, newName));
    }
    
    return success;
}

bool FileListModel::copyFile(int index, const QString& destinationPath) {
    QVariantMap fileInfo = getFileInfo(index);
    if (fileInfo.isEmpty()) {
        return false;
    }
    
    QString sourcePath = fileInfo["absolutePath"].toString();
    bool success = QFile::copy(sourcePath, destinationPath);
    
    if (!success) {
        emit fileSystemError(tr("Failed to copy %1 to %2").arg(sourcePath, destinationPath));
    }
    
    return success;
}

bool FileListModel::moveFile(int index, const QString& destinationPath) {
    QVariantMap fileInfo = getFileInfo(index);
    if (fileInfo.isEmpty()) {
        return false;
    }
    
    QString sourcePath = fileInfo["absolutePath"].toString();
    bool success = QFile::rename(sourcePath, destinationPath);
    
    if (success) {
        refresh();
    } else {
        emit fileSystemError(tr("Failed to move %1 to %2").arg(sourcePath, destinationPath));
    }
    
    return success;
}

bool FileListModel::openFile(int index) {
    QVariantMap fileInfo = getFileInfo(index);
    if (fileInfo.isEmpty()) {
        return false;
    }
    
    QString filePath = fileInfo["absolutePath"].toString();
    bool success = QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
    
    if (success) {
        emit fileOpened(filePath);
    } else {
        emit fileSystemError(tr("Failed to open: %1").arg(filePath));
    }
    
    return success;
}

bool FileListModel::revealInExplorer(int index) {
    QVariantMap fileInfo = getFileInfo(index);
    if (fileInfo.isEmpty()) {
        return false;
    }
    
    QString filePath = fileInfo["absolutePath"].toString();
    
#ifdef Q_OS_WIN
    QStringList args;
    args << "/select," << QDir::toNativeSeparators(filePath);
    return QProcess::startDetached("explorer", args);
#elif defined(Q_OS_MAC)
    QStringList args;
    args << "-e" << "tell application \"Finder\"" << "-e" << "activate" << "-e" 
         << QString("select POSIX file \"%1\"").arg(filePath) << "-e" << "end tell";
    return QProcess::startDetached("osascript", args);
#else
    // Linux - open parent directory
    return QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).dir().absolutePath()));
#endif
}

QVariantMap FileListModel::getFileInfo(int index) const {
    if (index < 0 || index >= rowCount()) {
        return QVariantMap();
    }
    
    int fileIndex = index;
    if (d->filtersApplied) {
        if (fileIndex >= static_cast<int>(d->filteredIndices.size())) {
            return QVariantMap();
        }
        fileIndex = d->filteredIndices[fileIndex];
    }
    
    if (fileIndex >= static_cast<int>(d->files.size())) {
        return QVariantMap();
    }
    
    const FileItemInfo& file = d->files[fileIndex];
    QVariantMap map;
    
    map["fileName"] = file.fileName;
    map["filePath"] = file.filePath;
    map["absolutePath"] = file.absolutePath;
    map["baseName"] = file.baseName;
    map["suffix"] = file.suffix;
    map["parentDir"] = file.parentDir;
    map["size"] = file.size;
    map["sizeString"] = file.sizeString();
    map["type"] = static_cast<int>(file.type);
    map["typeString"] = file.typeString();
    map["created"] = file.created;
    map["modified"] = file.modified;
    map["lastAccessed"] = file.lastAccessed;
    map["isDirectory"] = file.isDirectory;
    map["isHidden"] = file.isHidden;
    map["isReadable"] = file.isReadable;
    map["isWritable"] = file.isWritable;
    map["isExecutable"] = file.isExecutable;
    map["isSymLink"] = file.isSymLink;
    map["mimeType"] = file.mimeType;
    map["iconName"] = file.iconName;
    map["tags"] = file.tags;
    map["description"] = file.description;
    map["duration"] = file.duration;
    map["resolution"] = file.resolution;
    map["codec"] = file.codec;
    map["bitrate"] = file.bitrate;
    map["fileCount"] = file.fileCount;
    map["uncompressedSize"] = file.uncompressedSize;
    map["isValid"] = file.isValid();
    map["isMediaFile"] = file.isMediaFile();
    map["canPlay"] = file.canPlay();
    
    return map;
}

QVariantMap FileListModel::getFileInfo(const QString& fileName) const {
    int index = findFile(fileName);
    if (index >= 0) {
        return getFileInfo(index);
    }
    return QVariantMap();
}

QString FileListModel::getAbsolutePath(int index) const {
    QVariantMap fileInfo = getFileInfo(index);
    return fileInfo["absolutePath"].toString();
}

bool FileListModel::exists(const QString& fileName) const {
    return findFile(fileName) >= 0;
}

int FileListModel::findFile(const QString& fileName) const {
    for (size_t i = 0; i < d->files.size(); ++i) {
        if (d->files[i].fileName == fileName) {
            // Convert to display index if filters are applied
            if (d->filtersApplied) {
                auto it = std::find(d->filteredIndices.begin(), d->filteredIndices.end(), static_cast<int>(i));
                if (it != d->filteredIndices.end()) {
                    return static_cast<int>(std::distance(d->filteredIndices.begin(), it));
                }
                return -1;
            }
            return static_cast<int>(i);
        }
    }
    return -1;
}

QVariantList FileListModel::getSelectedFiles() const {
    QVariantList selected;
    
    for (size_t i = 0; i < d->files.size() && i < d->selection.size(); ++i) {
        if (d->selection[i]) {
            // Convert to FileItemInfo variant
            QVariantMap fileMap;
            const FileItemInfo& file = d->files[i];
            
            fileMap["fileName"] = file.fileName;
            fileMap["absolutePath"] = file.absolutePath;
            fileMap["isDirectory"] = file.isDirectory;
            fileMap["size"] = file.size;
            fileMap["type"] = static_cast<int>(file.type);
            
            selected.append(fileMap);
        }
    }
    
    return selected;
}

QVariantList FileListModel::getMediaFiles() const {
    return getFilesByType(FileType::Video);  // This could be expanded to include audio
}

QVariantList FileListModel::getFilesByType(FileType type) const {
    QVariantList filtered;
    
    for (const auto& file : d->files) {
        if (file.type == type) {
            QVariantMap fileMap;
            fileMap["fileName"] = file.fileName;
            fileMap["absolutePath"] = file.absolutePath;
            fileMap["size"] = file.size;
            fileMap["duration"] = file.duration;
            fileMap["resolution"] = file.resolution;
            
            filtered.append(fileMap);
        }
    }
    
    return filtered;
}

void FileListModel::selectFile(int index) {
    if (index >= 0 && index < static_cast<int>(d->selection.size())) {
        if (!d->selection[index]) {
            d->selection[index] = true;
            emit selectionChanged();
        }
    }
}

void FileListModel::deselectFile(int index) {
    if (index >= 0 && index < static_cast<int>(d->selection.size())) {
        if (d->selection[index]) {
            d->selection[index] = false;
            emit selectionChanged();
        }
    }
}

void FileListModel::toggleSelection(int index) {
    if (index >= 0 && index < static_cast<int>(d->selection.size())) {
        d->selection[index] = !d->selection[index];
        emit selectionChanged();
    }
}

void FileListModel::selectAll() {
    bool changed = false;
    for (size_t i = 0; i < d->selection.size(); ++i) {
        if (!d->selection[i]) {
            d->selection[i] = true;
            changed = true;
        }
    }
    
    if (changed) {
        emit selectionChanged();
    }
}

void FileListModel::deselectAll() {
    bool changed = false;
    for (size_t i = 0; i < d->selection.size(); ++i) {
        if (d->selection[i]) {
            d->selection[i] = false;
            changed = true;
        }
    }
    
    if (changed) {
        emit selectionChanged();
    }
}

bool FileListModel::isSelected(int index) const {
    if (index >= 0 && index < static_cast<int>(d->selection.size())) {
        return d->selection[index];
    }
    return false;
}

int FileListModel::selectedCount() const {
    return static_cast<int>(std::count(d->selection.begin(), d->selection.end(), true));
}

int FileListModel::fileCount() const {
    return d->fileCount;
}

int FileListModel::directoryCount() const {
    return d->directoryCount;
}

qint64 FileListModel::totalSize() const {
    return d->totalSize;
}

QVariantMap FileListModel::getStatistics() const {
    QVariantMap stats;
    stats["totalFiles"] = static_cast<int>(d->files.size());
    stats["fileCount"] = d->fileCount;
    stats["directoryCount"] = d->directoryCount;
    stats["totalSize"] = d->totalSize;
    stats["selectedCount"] = selectedCount();
    stats["filteredCount"] = rowCount();
    stats["showingHidden"] = d->showHidden;
    stats["searchActive"] = d->searchActive;
    return stats;
}

void FileListModel::enableFileWatching(bool enable) {
    if (d->watchingEnabled != enable) {
        d->watchingEnabled = enable;
        
        if (enable) {
            connectFileWatcher();
        } else {
            disconnectFileWatcher();
        }
    }
}

void FileListModel::refresh() {
    loadDirectory(d->currentPath);
}

void FileListModel::addBookmark(const QString& name) {
    QString bookmarkName = name.isEmpty() ? QFileInfo(d->currentPath).baseName() : name;
    QString bookmark = QString("%1|%2").arg(bookmarkName, d->currentPath);
    
    if (!d->bookmarks.contains(bookmark)) {
        d->bookmarks.append(bookmark);
        saveBookmarks();
    }
}

void FileListModel::removeBookmark(const QString& path) {
    for (int i = 0; i < d->bookmarks.size(); ++i) {
        if (d->bookmarks[i].endsWith("|" + path)) {
            d->bookmarks.removeAt(i);
            saveBookmarks();
            break;
        }
    }
}

QStringList FileListModel::getBookmarks() const {
    return d->bookmarks;
}

void FileListModel::setSearchQuery(const QString& query) {
    if (d->searchQuery != query) {
        d->searchQuery = query;
        d->searchActive = !query.isEmpty();
        
        beginResetModel();
        applyFilters();
        endResetModel();
        
        emit countChanged();
    }
}

void FileListModel::clearSearch() {
    setSearchQuery(QString());
}

bool FileListModel::isSearchActive() const {
    return d->searchActive;
}

QString FileListModel::getThumbnail(int index) const {
    QVariantMap fileInfo = getFileInfo(index);
    if (fileInfo.isEmpty() || !d->thumbnailsEnabled) {
        return QString();
    }
    
    QString filePath = fileInfo["absolutePath"].toString();
    return generateThumbnailPath(filePath);
}

void FileListModel::generateThumbnails(bool enable) {
    d->thumbnailsEnabled = enable;
}

void FileListModel::setFileManager(QObject* fileManager) {
    d->fileManager = qobject_cast<FileManager*>(fileManager);
    if (d->fileManager) {
        Logger::instance().info("FileListModel connected to FileManager");
    }
}

// Slots

void FileListModel::onDirectoryChanged(const QString& path) {
    if (path == d->currentPath) {
        refreshModel();
    }
}

void FileListModel::onFileChanged(const QString& path) {
    Q_UNUSED(path)
    // Individual file changes - could implement more granular updates here
    refreshModel();
}

void FileListModel::refreshModel() {
    refresh();
}

// Private methods

void FileListModel::loadDirectory(const QString& path) {
    beginResetModel();
    
    d->files.clear();
    d->selection.clear();
    
    QDir dir(path);
    if (!dir.exists()) {
        endResetModel();
        return;
    }
    
    QFileInfoList fileInfoList = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
    
    d->files.reserve(fileInfoList.size());
    d->selection.reserve(fileInfoList.size());
    
    for (const QFileInfo& fileInfo : fileInfoList) {
        FileItemInfo item = createFileItemInfo(fileInfo);
        if (item.isValid()) {
            d->files.push_back(item);
            d->selection.push_back(false);
        }
    }
    
    sortFiles();
    applyFilters();
    updateStatistics();
    
    endResetModel();
    
    emit countChanged();
    emit fileCountChanged();
    emit directoryCountChanged();
    emit totalSizeChanged();
}

void FileListModel::updateFileInfo() {
    // Update file information for the current directory
    // This could be called periodically to refresh file sizes, dates, etc.
}

void FileListModel::applyFilters() {
    d->filteredIndices.clear();
    d->filtersApplied = false;
    
    bool hasNameFilter = !d->filter.isEmpty();
    bool hasSearchFilter = d->searchActive;
    
    if (!hasNameFilter && !hasSearchFilter && d->showHidden && d->showDirectories) {
        return; // No filters to apply
    }
    
    d->filtersApplied = true;
    
    for (size_t i = 0; i < d->files.size(); ++i) {
        const FileItemInfo& file = d->files[i];
        bool passes = true;
        
        // Hidden files filter
        if (!d->showHidden && file.isHidden) {
            passes = false;
        }
        
        // Directory filter
        if (passes && !d->showDirectories && file.isDirectory) {
            passes = false;
        }
        
        // Name filter
        if (passes && hasNameFilter && !matchesFilter(file)) {
            passes = false;
        }
        
        // Search filter
        if (passes && hasSearchFilter && !matchesSearch(file)) {
            passes = false;
        }
        
        if (passes) {
            d->filteredIndices.push_back(static_cast<int>(i));
        }
    }
}

FileItemInfo FileListModel::createFileItemInfo(const QFileInfo& fileInfo) const {
    FileItemInfo item;
    
    item.fileName = fileInfo.fileName();
    item.filePath = fileInfo.filePath();
    item.absolutePath = fileInfo.absoluteFilePath();
    item.baseName = fileInfo.baseName();
    item.suffix = fileInfo.suffix();
    item.parentDir = fileInfo.dir().absolutePath();
    item.size = fileInfo.size();
    item.created = fileInfo.birthTime();
    item.modified = fileInfo.lastModified();
    item.lastAccessed = fileInfo.lastRead();
    item.isDirectory = fileInfo.isDir();
    item.isHidden = fileInfo.isHidden();
    item.isReadable = fileInfo.isReadable();
    item.isWritable = fileInfo.isWritable();
    item.isExecutable = fileInfo.isExecutable();
    item.isSymLink = fileInfo.isSymLink();
    
    // Determine file type and MIME type
    item.type = determineFileType(fileInfo);
    
    QMimeType mimeType = d->mimeDatabase.mimeTypeForFile(fileInfo);
    item.mimeType = mimeType.name();
    
    // Set icon name based on type
    item.iconName = getIconName(item);
    
    // For directories, count child items
    if (item.isDirectory) {
        QDir childDir(item.absolutePath);
        item.fileCount = childDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();
    }
    
    return item;
}

FileType FileListModel::determineFileType(const QFileInfo& fileInfo) const {
    if (fileInfo.isDir()) {
        return FileType::Directory;
    }
    
    QString suffix = fileInfo.suffix().toLower();
    
    // Video files
    QStringList videoExtensions = {"mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "3gp"};
    if (videoExtensions.contains(suffix)) {
        return FileType::Video;
    }
    
    // Audio files
    QStringList audioExtensions = {"mp3", "wav", "flac", "aac", "ogg", "wma", "m4a"};
    if (audioExtensions.contains(suffix)) {
        return FileType::Audio;
    }
    
    // Image files
    QStringList imageExtensions = {"jpg", "jpeg", "png", "gif", "bmp", "svg", "webp", "tiff"};
    if (imageExtensions.contains(suffix)) {
        return FileType::Image;
    }
    
    // Document files
    QStringList documentExtensions = {"pdf", "doc", "docx", "txt", "rtf", "odt", "ppt", "pptx", "xls", "xlsx"};
    if (documentExtensions.contains(suffix)) {
        return FileType::Document;
    }
    
    // Archive files
    QStringList archiveExtensions = {"zip", "rar", "7z", "tar", "gz", "bz2", "xz"};
    if (archiveExtensions.contains(suffix)) {
        return FileType::Archive;
    }
    
    // Torrent files
    if (suffix == "torrent") {
        return FileType::Torrent;
    }
    
    return FileType::Other;
}

QString FileListModel::getIconName(const FileItemInfo& info) const {
    switch (info.type) {
        case FileType::Directory: return "folder";
        case FileType::Video: return "video";
        case FileType::Audio: return "audio";
        case FileType::Image: return "image";
        case FileType::Document: return "document";
        case FileType::Archive: return "archive";
        case FileType::Torrent: return "torrent";
        default: return "file";
    }
}

bool FileListModel::matchesFilter(const FileItemInfo& info) const {
    if (d->filter.isEmpty()) {
        return true;
    }
    
    // Simple wildcard matching
    QRegularExpression regex(QRegularExpression::wildcardToRegularExpression(d->filter), QRegularExpression::CaseInsensitiveOption);
    return regex.match(info.fileName).hasMatch();
}

bool FileListModel::matchesSearch(const FileItemInfo& info) const {
    if (d->searchQuery.isEmpty()) {
        return true;
    }
    
    QString query = d->searchQuery.toLower();
    return info.fileName.toLower().contains(query) ||
           info.baseName.toLower().contains(query);
}

void FileListModel::updateStatistics() {
    d->fileCount = 0;
    d->directoryCount = 0;
    d->totalSize = 0;
    
    for (const auto& file : d->files) {
        if (file.isDirectory) {
            d->directoryCount++;
        } else {
            d->fileCount++;
            d->totalSize += file.size;
        }
    }
}

void FileListModel::clearHistory() {
    d->navigationHistory.clear();
    d->historyIndex = -1;
}

void FileListModel::pushToHistory(const QString& path) {
    // Remove everything after current index (when going back and then navigating somewhere new)
    if (d->historyIndex < d->navigationHistory.size() - 1) {
        d->navigationHistory.erase(d->navigationHistory.begin() + d->historyIndex + 1, 
                                  d->navigationHistory.end());
    }
    
    // Don't add duplicate consecutive entries
    if (d->navigationHistory.isEmpty() || d->navigationHistory.last() != path) {
        d->navigationHistory.append(path);
        d->historyIndex = d->navigationHistory.size() - 1;
        
        // Limit history size
        const int maxHistorySize = 100;
        if (d->navigationHistory.size() > maxHistorySize) {
            d->navigationHistory.removeFirst();
            d->historyIndex--;
        }
    }
}

QString FileListModel::popFromHistory() {
    if (d->historyIndex > 0) {
        d->historyIndex--;
        return d->navigationHistory[d->historyIndex];
    }
    return QString();
}


int FileListModel::compareFiles(const FileItemInfo& a, const FileItemInfo& b) const {
    // Directories always come first
    if (a.isDirectory != b.isDirectory) {
        return a.isDirectory ? -1 : 1;
    }
    
    bool ascending = (d->sortOrder == Qt::AscendingOrder);
    int result = 0;
    
    switch (d->sortField) {
        case SortField::Name:
            result = QString::compare(a.fileName, b.fileName, Qt::CaseInsensitive);
            break;
        case SortField::Size:
            result = (a.size < b.size) ? -1 : (a.size > b.size) ? 1 : 0;
            break;
        case SortField::Type:
            result = QString::compare(a.typeString(), b.typeString(), Qt::CaseInsensitive);
            break;
        case SortField::Modified:
            result = (a.modified < b.modified) ? -1 : (a.modified > b.modified) ? 1 : 0;
            break;
        case SortField::Created:
            result = (a.created < b.created) ? -1 : (a.created > b.created) ? 1 : 0;
            break;
        case SortField::Extension:
            result = QString::compare(a.suffix, b.suffix, Qt::CaseInsensitive);
            break;
    }
    
    return ascending ? result : -result;
}

void FileListModel::connectFileWatcher() {
    if (!d->currentPath.isEmpty()) {
        d->fileWatcher->addPath(d->currentPath);
    }
}

void FileListModel::disconnectFileWatcher() {
    d->fileWatcher->removePaths(d->fileWatcher->directories());
    d->fileWatcher->removePaths(d->fileWatcher->files());
}

QString FileListModel::generateThumbnailPath(const QString& filePath) const {
    if (!d->thumbnailsEnabled) {
        return QString();
    }
    
    // Generate a unique thumbnail filename based on file path hash
    QString hash = QString::number(qHash(filePath), 16);
    return QDir(d->thumbnailCacheDir).absoluteFilePath(hash + ".png");
}

void FileListModel::loadBookmarks() {
    QSettings settings;
    d->bookmarks = settings.value("fileListModel/bookmarks").toStringList();
}

void FileListModel::saveBookmarks() {
    QSettings settings;
    settings.setValue("fileListModel/bookmarks", d->bookmarks);
}

} // namespace Murmur