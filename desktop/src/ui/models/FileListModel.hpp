#pragma once

#include <QtCore/QAbstractListModel>
#include <QtCore/QDateTime>
#include <QtCore/QFileInfo>
#include <QtCore/QFileSystemWatcher>
#include <QtCore/QMimeType>
#include <QtCore/QTimer>
#include <QtQml/QQmlEngine>
#include <memory>
#include <vector>

#include "core/common/Expected.hpp"

namespace Murmur {

class FileManager;

enum class FileType {
    Unknown,
    Video,
    Audio,
    Image,
    Document,
    Archive,
    Torrent,
    Directory,
    Other
};

enum class SortField {
    Name,
    Size,
    Type,
    Modified,
    Created,
    Extension
};

struct FileItemInfo {
    QString fileName;
    QString filePath;
    QString absolutePath;
    QString baseName;
    QString suffix;
    QString parentDir;
    qint64 size = 0;
    FileType type = FileType::Unknown;
    QDateTime created;
    QDateTime modified;
    QDateTime lastAccessed;
    bool isDirectory = false;
    bool isHidden = false;
    bool isReadable = true;
    bool isWritable = true;
    bool isExecutable = false;
    bool isSymLink = false;
    QString mimeType;
    QString iconName;
    QStringList tags;
    QString description;
    
    // Media-specific properties
    qint64 duration = 0;        // for video/audio files (milliseconds)
    QString resolution;         // for video files (e.g., "1920x1080")
    QString codec;              // for media files
    int bitrate = 0;            // for media files (kbps)
    
    // Archive-specific properties
    int fileCount = 0;          // for archives/directories
    qint64 uncompressedSize = 0; // for archives
    
    bool isValid() const {
        return !fileName.isEmpty() && !filePath.isEmpty();
    }
    
    bool isMediaFile() const {
        return type == FileType::Video || type == FileType::Audio;
    }
    
    bool canPlay() const {
        return isMediaFile() && isReadable;
    }
    
    QString sizeString() const {
        return formatSize(size);
    }
    
    QString typeString() const {
        switch (type) {
            case FileType::Video: return "Video";
            case FileType::Audio: return "Audio";
            case FileType::Image: return "Image";
            case FileType::Document: return "Document";
            case FileType::Archive: return "Archive";
            case FileType::Torrent: return "Torrent";
            case FileType::Directory: return "Directory";
            default: return "File";
        }
    }
    
private:
    static QString formatSize(qint64 bytes);
};

class FileListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    
    Q_PROPERTY(QString currentPath READ currentPath WRITE setCurrentPath NOTIFY currentPathChanged)
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY canGoUpChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY canGoBackChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY canGoForwardChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(bool showDirectories READ showDirectories WRITE setShowDirectories NOTIFY showDirectoriesChanged)
    Q_PROPERTY(SortField sortField READ sortField WRITE setSortField NOTIFY sortFieldChanged)
    Q_PROPERTY(Qt::SortOrder sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY fileCountChanged)
    Q_PROPERTY(int directoryCount READ directoryCount NOTIFY directoryCountChanged)
    Q_PROPERTY(qint64 totalSize READ totalSize NOTIFY totalSizeChanged)

public:
    enum Roles {
        FileNameRole = Qt::UserRole + 1,
        FilePathRole,
        AbsolutePathRole,
        BaseNameRole,
        SuffixRole,
        ParentDirRole,
        SizeRole,
        SizeStringRole,
        TypeRole,
        TypeStringRole,
        CreatedRole,
        ModifiedRole,
        LastAccessedRole,
        IsDirectoryRole,
        IsHiddenRole,
        IsReadableRole,
        IsWritableRole,
        IsExecutableRole,
        IsSymLinkRole,
        MimeTypeRole,
        IconNameRole,
        TagsRole,
        DescriptionRole,
        DurationRole,
        ResolutionRole,
        CodecRole,
        BitrateRole,
        FileCountRole,
        UncompressedSizeRole,
        IsValidRole,
        IsMediaFileRole,
        CanPlayRole
    };
    Q_ENUM(Roles)
    Q_ENUM(FileType)
    Q_ENUM(SortField)

    explicit FileListModel(QObject* parent = nullptr);
    ~FileListModel() override;

    // QAbstractListModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Path navigation
    QString currentPath() const;
    void setCurrentPath(const QString& path);
    Q_INVOKABLE bool navigateToPath(const QString& path);
    Q_INVOKABLE bool navigateUp();
    Q_INVOKABLE bool navigateBack();
    Q_INVOKABLE bool navigateForward();
    Q_INVOKABLE bool navigateToDirectory(int index);
    Q_INVOKABLE QString getParentPath() const;
    Q_INVOKABLE QStringList getPathComponents() const;
    Q_INVOKABLE QString getHomeDirectory() const;

    // Navigation state
    bool canGoUp() const;
    bool canGoBack() const;
    bool canGoForward() const;

    // Filtering and display
    QString filter() const;
    void setFilter(const QString& filter);
    bool showHidden() const;
    void setShowHidden(bool show);
    bool showDirectories() const;
    void setShowDirectories(bool show);

    // Sorting
    SortField sortField() const;
    void setSortField(SortField field);
    Qt::SortOrder sortOrder() const;
    void setSortOrder(Qt::SortOrder order);
    Q_INVOKABLE void sortFiles();

    // File operations
    Q_INVOKABLE bool createDirectory(const QString& name);
    Q_INVOKABLE bool deleteFile(int index);
    Q_INVOKABLE bool deleteFile(const QString& fileName);
    Q_INVOKABLE bool renameFile(int index, const QString& newName);
    Q_INVOKABLE bool renameFile(const QString& oldName, const QString& newName);
    Q_INVOKABLE bool copyFile(int index, const QString& destinationPath);
    Q_INVOKABLE bool moveFile(int index, const QString& destinationPath);
    Q_INVOKABLE bool openFile(int index);
    Q_INVOKABLE bool revealInExplorer(int index);

    // File queries
    Q_INVOKABLE QVariantMap getFileInfo(int index) const;
    Q_INVOKABLE QVariantMap getFileInfo(const QString& fileName) const;
    Q_INVOKABLE QString getAbsolutePath(int index) const;
    Q_INVOKABLE bool exists(const QString& fileName) const;
    Q_INVOKABLE int findFile(const QString& fileName) const;
    Q_INVOKABLE QVariantList getSelectedFiles() const;
    Q_INVOKABLE QVariantList getMediaFiles() const;
    Q_INVOKABLE QVariantList getFilesByType(FileType type) const;

    // Selection
    Q_INVOKABLE void selectFile(int index);
    Q_INVOKABLE void deselectFile(int index);
    Q_INVOKABLE void toggleSelection(int index);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void deselectAll();
    Q_INVOKABLE bool isSelected(int index) const;
    Q_INVOKABLE int selectedCount() const;

    // Statistics
    int fileCount() const;
    int directoryCount() const;
    qint64 totalSize() const;
    Q_INVOKABLE QVariantMap getStatistics() const;

    // File watching
    Q_INVOKABLE void enableFileWatching(bool enable);
    Q_INVOKABLE void refresh();

    // Bookmarks
    Q_INVOKABLE void addBookmark(const QString& name = QString());
    Q_INVOKABLE void removeBookmark(const QString& path);
    Q_INVOKABLE QStringList getBookmarks() const;

    // Search
    Q_INVOKABLE void setSearchQuery(const QString& query);
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE bool isSearchActive() const;

    // Thumbnails and previews
    Q_INVOKABLE QString getThumbnail(int index) const;
    Q_INVOKABLE void generateThumbnails(bool enable);

public slots:
    void setFileManager(QObject* fileManager);

signals:
    void currentPathChanged();
    void canGoUpChanged();
    void canGoBackChanged();
    void canGoForwardChanged();
    void filterChanged();
    void showHiddenChanged();
    void showDirectoriesChanged();
    void sortFieldChanged();
    void sortOrderChanged();
    void countChanged();
    void fileCountChanged();
    void directoryCountChanged();
    void totalSizeChanged();
    void selectionChanged();
    void fileOpened(const QString& filePath);
    void directoryChanged(const QString& path);
    void fileSystemError(const QString& error);

private slots:
    void onDirectoryChanged(const QString& path);
    void onFileChanged(const QString& path);
    void refreshModel();

private:
    class FileListModelPrivate;
    std::unique_ptr<FileListModelPrivate> d;

    // Helper methods
    void loadDirectory(const QString& path);
    void updateFileInfo();
    void applyFilters();
    FileItemInfo createFileItemInfo(const QFileInfo& fileInfo) const;
    FileType determineFileType(const QFileInfo& fileInfo) const;
    QString getIconName(const FileItemInfo& info) const;
    bool matchesFilter(const FileItemInfo& info) const;
    bool matchesSearch(const FileItemInfo& info) const;
    void updateStatistics();
    void clearHistory();
    void pushToHistory(const QString& path);
    QString popFromHistory();
    int compareFiles(const FileItemInfo& a, const FileItemInfo& b) const;
    void connectFileWatcher();
    void disconnectFileWatcher();
    QString generateThumbnailPath(const QString& filePath) const;
    void loadBookmarks();
    void saveBookmarks();
};

} // namespace Murmur