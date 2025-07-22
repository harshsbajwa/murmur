#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QFuture>
#include "../../core/transcription/WhisperEngine.hpp"
#include "../../core/storage/StorageManager.hpp"

namespace Murmur {

// Forward declarations
class MediaController;

class TranscriptionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isTranscribing READ isTranscribing NOTIFY transcribingChanged)
    Q_PROPERTY(QString currentTranscription READ currentTranscription NOTIFY transcriptionChanged)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages NOTIFY availableLanguagesChanged)
    Q_PROPERTY(QStringList availableModels READ availableModels NOTIFY availableModelsChanged)
    Q_PROPERTY(QString selectedLanguage READ selectedLanguage WRITE setSelectedLanguage NOTIFY selectedLanguageChanged)
    Q_PROPERTY(QString selectedModel READ selectedModel WRITE setSelectedModel NOTIFY selectedModelChanged)
    
public:
    explicit TranscriptionController(QObject* parent = nullptr);
    
    // Setters for dependencies
    Q_INVOKABLE void setWhisperEngine(WhisperEngine* engine);
    Q_INVOKABLE void setStorageManager(StorageManager* storage);
    Q_INVOKABLE void setMediaController(MediaController* controller);
    
    bool isTranscribing() const { return isTranscribing_; }
    QString currentTranscription() const { return currentTranscription_; }
    QStringList availableLanguages() const { return availableLanguages_; }
    QStringList availableModels() const { return availableModels_; }
    QString selectedLanguage() const { return selectedLanguage_; }
    QString selectedModel() const { return selectedModel_; }
    bool isReady() const { return whisperEngine_ != nullptr; }
    
    void setSelectedLanguage(const QString& language);
    void setSelectedModel(const QString& model);
    
public slots:
    void transcribeCurrentVideo();
    void transcribeFile(const QString& filePath, const QString& mediaId = QString());
    void transcribeAudio(const QString& audioFilePath);
    void downloadModel(const QString& modelSize);
    void cancelTranscription();
    void cancelAllTranscriptions();
    void clearTranscription();
    void loadTranscription(const QString& mediaId);
    void exportTranscription(const QString& format, const QString& outputPath);
    
signals:
    void transcribingChanged();
    void transcriptionChanged();
    void availableLanguagesChanged();
    void availableModelsChanged();
    void selectedLanguageChanged();
    void selectedModelChanged();
    void transcriptionProgress(const QString& taskId, qreal progress);
    void transcriptionCompleted(const QString& taskId, const QString& transcription);
    void transcriptionError(const QString& taskId, const QString& error);
    void modelDownloadProgress(const QString& modelSize, qreal progress);
    void modelDownloadCompleted(const QString& modelSize);
    void modelDownloadFailed(const QString& modelSize, const QString& error);
    void transcriptionExported(const QString& outputPath);
    
private slots:
    void onTranscriptionProgress(const QString& taskId, const TranscriptionProgress& progress);
    void onTranscriptionCompleted(const QString& taskId, const TranscriptionResult& result);
    void onTranscriptionFailed(const QString& taskId, TranscriptionError error, const QString& errorString);
    void onModelDownloadProgress(const QString& modelSize, qint64 bytesReceived, qint64 bytesTotal);
    void onModelDownloadCompleted(const QString& modelSize);
    void onModelDownloadFailed(const QString& modelSize, const QString& error);

private:
    bool isTranscribing_ = false;
    QString currentTranscription_;
    QStringList availableLanguages_;
    QStringList availableModels_;
    QString selectedLanguage_ = "auto";
    QString selectedModel_ = "base";
    QString currentMediaId_;
    
    WhisperEngine* whisperEngine_ = nullptr;
    StorageManager* storageManager_ = nullptr;
    MediaController* mediaController_ = nullptr;
    
    QHash<QString, QString> activeTranscriptions_;  // taskId -> mediaId
    
    void setTranscribing(bool transcribing);
    void setTranscription(const QString& transcription);
    void updateAvailableOptions();
    void connectEngineSignals();
    TranscriptionSettings createTranscriptionSettings() const;
    void storeTranscriptionResult(const QString& mediaId, const TranscriptionResult& result);
};

} // namespace Murmur