# Controller Properties and Signals Summary

This document summarizes all Q_PROPERTY declarations, signals, and controller setters found in the Murmur Desktop application.

## AppController

### Q_PROPERTY declarations:
- `bool isInitialized` (READ isInitialized NOTIFY initializedChanged)
- `QString status` (READ status NOTIFY statusChanged)
- `bool isDarkMode` (READ isDarkMode WRITE setDarkMode NOTIFY darkModeChanged)

### Signals:
- `initializedChanged()`
- `initializationComplete()`
- `statusChanged()`
- `darkModeChanged()`
- `initializationFailed(const QString& error)`

### Core engine access methods:
- `torrentEngine()` → TorrentEngine*
- `mediaPipeline()` → MediaPipeline*
- `whisperEngine()` → WhisperEngine*
- `storageManager()` → StorageManager*
- `fileManager()` → FileManager*
- `videoPlayer()` → VideoPlayer*
- `platformAccelerator()` → PlatformAccelerator*

## TorrentController

### Q_PROPERTY declarations:
- `TorrentStateModel* torrentModel` (READ torrentModel CONSTANT)
- `bool isBusy` (READ isBusy NOTIFY busyChanged)
- `int activeTorrentsCount` (READ activeTorrentsCount NOTIFY torrentsCountChanged)
- `int seedingTorrentsCount` (READ seedingTorrentsCount NOTIFY torrentsCountChanged)

### Signals:
- `busyChanged()`
- `torrentsCountChanged()`
- `torrentAdded(const QString& infoHash)`
- `torrentRemoved(const QString& infoHash)`
- `torrentError(const QString& infoHash, const QString& error)`
- `operationCompleted(const QString& message)`

### Controller setter:
- `setTorrentEngine(TorrentEngine* engine)`

### isReady implementation:
- Returns `torrentEngine_ != nullptr`

## MediaController

### Q_PROPERTY declarations:
- `QUrl currentVideoSource` (READ currentVideoSource NOTIFY sourceChanged)
- `qreal playbackPosition` (READ playbackPosition NOTIFY positionChanged)
- `bool isProcessing` (READ isProcessing NOTIFY processingChanged)
- `QString currentMediaFile` (READ currentMediaFile NOTIFY currentMediaFileChanged)
- `QString outputPath` (READ outputPath NOTIFY outputPathChanged)
- `bool isReady` (READ isReady NOTIFY readyChanged)

### Signals:
- `sourceChanged()`
- `positionChanged()`
- `processingChanged()`
- `currentMediaFileChanged()`
- `outputPathChanged()`
- `readyChanged()`
- `conversionProgress(const QString& operationId, qreal progress)`
- `conversionCompleted(const QString& operationId, const QString& outputPath)`
- `conversionError(const QString& operationId, const QString& error)`
- `videoAnalyzed(const QString& filePath, const VideoInfo& info)`
- `thumbnailGenerated(const QString& videoPath, const QString& thumbnailPath)`
- `progressUpdated(const QVariantMap& progress)`
- `errorOccurred(const QString& error)`
- `operationCompleted(const QString& operation)`
- `operationCancelled(const QString& operation)`

### Controller setters:
- `setMediaPipeline(MediaPipeline* pipeline)`
- `setVideoPlayer(VideoPlayer* player)`
- `setStorageManager(StorageManager* storage)`

### isReady implementation:
- Returns `mediaPipeline_ != nullptr && videoPlayer_ != nullptr && storageManager_ != nullptr`

## TranscriptionController

### Q_PROPERTY declarations:
- `bool isTranscribing` (READ isTranscribing NOTIFY transcribingChanged)
- `QString currentTranscription` (READ currentTranscription NOTIFY transcriptionChanged)
- `QStringList availableLanguages` (READ availableLanguages NOTIFY availableLanguagesChanged)
- `QStringList availableModels` (READ availableModels NOTIFY availableModelsChanged)
- `QString selectedLanguage` (READ selectedLanguage WRITE setSelectedLanguage NOTIFY selectedLanguageChanged)
- `QString selectedModel` (READ selectedModel WRITE setSelectedModel NOTIFY selectedModelChanged)
- `bool isReady` (READ isReady NOTIFY readyChanged)

### Signals:
- `transcribingChanged()`
- `transcriptionChanged()`
- `availableLanguagesChanged()`
- `availableModelsChanged()`
- `selectedLanguageChanged()`
- `selectedModelChanged()`
- `readyChanged()`
- `transcriptionProgress(const QString& taskId, qreal progress)`
- `transcriptionCompleted(const QString& taskId, const QString& transcription)`
- `transcriptionError(const QString& taskId, const QString& error)`
- `modelDownloadProgress(const QString& modelSize, qreal progress)`
- `modelDownloadCompleted(const QString& modelSize)`
- `modelDownloadFailed(const QString& modelSize, const QString& error)`
- `transcriptionExported(const QString& outputPath)`

### Controller setters:
- `setWhisperEngine(WhisperEngine* engine)`
- `setStorageManager(StorageManager* storage)`
- `setMediaController(MediaController* controller)`

### isReady implementation:
- Returns `whisperEngine_ != nullptr`

## FileManagerController

### Q_PROPERTY declarations:
- `QString defaultDownloadPath` (READ defaultDownloadPath NOTIFY pathsChanged)
- `QString defaultExportPath` (READ defaultExportPath NOTIFY pathsChanged)
- `qreal totalProgress` (READ totalProgress NOTIFY progressChanged)
- `int activeOperationsCount` (READ activeOperationsCount NOTIFY operationsChanged)
- `bool isBusy` (READ isBusy NOTIFY busyChanged)
- `qint64 totalSpace` (READ totalSpace NOTIFY diskSpaceChanged)
- `qint64 usedSpace` (READ usedSpace NOTIFY diskSpaceChanged)
- `bool isReady` (READ isReady NOTIFY readyChanged)

### Signals:
- `fileModelChanged()`
- `readyChanged()`
- `pathsChanged()`
- `progressChanged()`
- `operationsChanged()`
- `busyChanged()`
- `diskSpaceChanged()`
- `directoryAnalyzed(const QString& path, int fileCount, int dirCount, qint64 totalSize, const QStringList& videoFiles)`
- `videoFilesFound(const QString& path, const QStringList& videoFiles)`
- `operationStarted(const QString& operationId, const QString& type, const QString& source, const QString& destination)`
- `operationProgress(const QString& operationId, qreal progress)`
- `operationCompleted(const QString& operationId, const QString& result)`
- `operationFailed(const QString& operationId, const QString& error)`
- `videoImported(const QString& sourcePath, const QString& destinationPath)`
- `videosImported(const QStringList& importedPaths)`
- `videoExported(const QString& sourcePath, const QString& destinationPath)`
- `transcriptionExported(const QString& outputPath, const QString& format)`
- `transcriptionImported(const QString& filePath, const QString& content)`
- `fileError(const QString& operation, const QString& path, const QString& error)`

### Controller setter:
- `setFileManager(FileManager* fileManager)`

### isReady implementation:
- Returns `fileManager_ != nullptr`

## Main QML Initialization Process

The main.qml file initializes controllers in the following sequence:

1. AppController initialization
2. Wait for `initializationComplete` signal
3. Set dependencies for each controller:
   - TorrentController: `setTorrentEngine(appController.torrentEngine)`
   - MediaController: `setMediaPipeline()`, `setVideoPlayer()`, `setStorageManager()`
   - TranscriptionController: `setWhisperEngine()`, `setStorageManager()`, `setMediaController()`
   - FileManagerController: `setFileManager()`

## Test Requirements

The end-to-end QML test checks that all controllers report `isReady == true` within 10 seconds:
- AppController: `isInitialized == true`
- TorrentController: `isReady == true`
- MediaController: `isReady == true`
- TranscriptionController: `isReady == true`
- FileManagerController: `isReady == true`
