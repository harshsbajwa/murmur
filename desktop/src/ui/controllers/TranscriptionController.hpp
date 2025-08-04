#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QFuture>
#include "../../core/transcription/WhisperEngine.hpp"
#include "../../core/transcription/TranscriptionTypes.hpp"
#include "../../core/storage/StorageManager.hpp"

namespace Murmur {

// Forward declarations
class MediaController;

class TranscriptionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isTranscribing READ isTranscribing NOTIFY transcribingChanged)
    Q_PROPERTY(QString currentTranscription READ currentTranscription NOTIFY transcriptionChanged)
    Q_PROPERTY(QVariantList currentSegments READ currentSegments NOTIFY segmentsChanged)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages NOTIFY availableLanguagesChanged)
    Q_PROPERTY(QStringList availableModels READ availableModels NOTIFY availableModelsChanged)
    Q_PROPERTY(QString selectedLanguage READ selectedLanguage WRITE setSelectedLanguage NOTIFY selectedLanguageChanged)
    Q_PROPERTY(QString selectedModel READ selectedModel WRITE setSelectedModel NOTIFY selectedModelChanged)
    Q_PROPERTY(qreal transcriptionProgress READ transcriptionProgress NOTIFY transcriptionProgressChanged)
    Q_PROPERTY(bool isReady READ isReady NOTIFY readyChanged)
    
public:
    explicit TranscriptionController(QObject* parent = nullptr);
    
    // Setters for dependencies
    Q_INVOKABLE void setWhisperEngine(WhisperEngine* engine);
    Q_INVOKABLE void setStorageManager(StorageManager* storage);
Q_INVOKABLE void setMediaController(MediaController* controller);

void setReady(bool ready);
void updateReadyState();

bool isTranscribing() const { return isTranscribing_; }
    QString currentTranscription() const { return currentTranscription_; }
    QVariantList currentSegments() const { return currentSegments_; }
    QStringList availableLanguages() const { return availableLanguages_; }
    QStringList availableModels() const { return availableModels_; }
    QString selectedLanguage() const { return selectedLanguage_; }
    QString selectedModel() const { return selectedModel_; }
    qreal transcriptionProgress() const { return transcriptionProgress_; }
bool isReady() const;
    
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
    void segmentsChanged();
    void availableLanguagesChanged();
    void availableModelsChanged();
    void selectedLanguageChanged();
    void selectedModelChanged();
    void transcriptionProgressChanged();
    void readyChanged();
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
    QVariantList currentSegments_;
    QStringList availableLanguages_;
    QStringList availableModels_;
    QString selectedLanguage_ = "auto";
    QString selectedModel_ = "base";
    QString currentMediaId_;
    qreal transcriptionProgress_ = 0.0;
    
WhisperEngine* whisperEngine_ = nullptr;
StorageManager* storageManager_ = nullptr;
MediaController* mediaController_ = nullptr;

bool ready_ = false;

QHash<QString, QString> activeTranscriptions_;  // taskId -> mediaId
    
    void setTranscribing(bool transcribing);
    void setTranscription(const QString& transcription);
    void setTranscriptionResult(const TranscriptionResult& result);
    QVariantList groupSegmentsBySentence(const QList<TranscriptionSegment>& segments);
    void updateAvailableOptions();
    void connectEngineSignals();
    TranscriptionSettings createTranscriptionSettings() const;
    void storeTranscriptionResult(const QString& mediaId, const TranscriptionResult& result);
};

} // namespace Murmur