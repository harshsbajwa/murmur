#include "TranscriptionController.hpp"
#include "MediaController.hpp"
#include "../../core/common/Logger.hpp"
#include <QtConcurrent>
#include <QFileInfo>
#include <QUuid>
#include <QFutureWatcher>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

namespace Murmur {

TranscriptionController::TranscriptionController(QObject* parent)
    : QObject(parent) {
    Logger::instance().info("TranscriptionController created");
}

void TranscriptionController::setWhisperEngine(WhisperEngine* engine) {
    if (whisperEngine_ != engine) {
        if (whisperEngine_) {
            disconnect(whisperEngine_, nullptr, this, nullptr);
        }
        
        whisperEngine_ = engine;
        
        if (whisperEngine_) {
            connectEngineSignals();
            updateAvailableOptions();
        }
    }
}

void TranscriptionController::setStorageManager(StorageManager* storage) {
    storageManager_ = storage;
}

void TranscriptionController::setMediaController(MediaController* controller) {
    mediaController_ = controller;
}

void TranscriptionController::setSelectedLanguage(const QString& language) {
    if (selectedLanguage_ != language) {
        selectedLanguage_ = language;
        emit selectedLanguageChanged();
    }
}

void TranscriptionController::setSelectedModel(const QString& model) {
    if (selectedModel_ != model) {
        selectedModel_ = model;
        emit selectedModelChanged();
        
        // Load the model if whisper engine is available
        if (whisperEngine_) {
            whisperEngine_->loadModel(model);
        }
    }
}

void TranscriptionController::transcribeCurrentVideo() {
    Logger::instance().info("Transcribing current video");
    
    if (!whisperEngine_) {
        Logger::instance().error("WhisperEngine not available");
        emit transcriptionError("", "Transcription engine not available");
        return;
    }
    
    // Get current video file from media controller if available
    if (mediaController_) {
        QUrl currentSource = mediaController_->currentVideoSource();
        if (!currentSource.isEmpty() && currentSource.isLocalFile()) {
            QString filePath = currentSource.toLocalFile();
            transcribeFile(filePath);
            return;
        }
    }
    
    // No video loaded, emit error
    emit transcriptionError("", "No video loaded. Please load a video file first.");
}

void TranscriptionController::transcribeFile(const QString& filePath, const QString& mediaId) {
    Logger::instance().info("Transcribing file: {}", filePath.toStdString());
    
    if (!whisperEngine_) {
        Logger::instance().error("WhisperEngine not available");
        emit transcriptionError("", "Transcription engine not available");
        return;
    }
    
    if (!QFileInfo::exists(filePath)) {
        emit transcriptionError("", "File not found: " + filePath);
        return;
    }
    
    setTranscribing(true);
    currentMediaId_ = mediaId;
    
    TranscriptionSettings settings = createTranscriptionSettings();
    
    // Check if it's a video file (transcribe from video) or audio file
    QFileInfo fileInfo(filePath);
    QString extension = fileInfo.suffix().toLower();
    QStringList videoExtensions = {"mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v"};
    
    QFuture<Expected<TranscriptionResult, TranscriptionError>> future;
    
    if (videoExtensions.contains(extension)) {
        future = whisperEngine_->transcribeFromVideo(filePath, settings);
    } else {
        future = whisperEngine_->transcribeAudio(filePath, settings);
    }
    
    auto watcher = new QFutureWatcher<Expected<TranscriptionResult, TranscriptionError>>(this);
    connect(watcher, &QFutureWatcher<Expected<TranscriptionResult, TranscriptionError>>::finished, 
            [this, watcher, filePath]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        setTranscribing(false);
        
        if (result.hasValue()) {
            TranscriptionResult transcriptionResult = result.value();
            setTranscription(transcriptionResult.fullText);
            
            // Store in database if storage manager is available
            if (storageManager_ && !currentMediaId_.isEmpty()) {
                storeTranscriptionResult(currentMediaId_, transcriptionResult);
            }
            
            emit transcriptionCompleted("", transcriptionResult.fullText);
        } else {
            QString errorMessage = QString("Transcription failed (error %1)")
                                 .arg(static_cast<int>(result.error()));
            emit transcriptionError("", errorMessage);
        }
    });
    
    watcher->setFuture(future);
}

void TranscriptionController::transcribeAudio(const QString& audioFilePath) {
    Logger::instance().info("Transcribing audio: {}", audioFilePath.toStdString());
    
    if (!whisperEngine_) {
        Logger::instance().error("WhisperEngine not available");
        emit transcriptionError("", "Transcription engine not available");
        return;
    }
    
    setTranscribing(true);
    
    TranscriptionSettings settings = createTranscriptionSettings();
    auto future = whisperEngine_->transcribeAudio(audioFilePath, settings);
    
    auto watcher = new QFutureWatcher<Expected<TranscriptionResult, TranscriptionError>>(this);
    connect(watcher, &QFutureWatcher<Expected<TranscriptionResult, TranscriptionError>>::finished,
            [this, watcher]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        setTranscribing(false);
        
        if (result.hasValue()) {
            TranscriptionResult transcriptionResult = result.value();
            setTranscription(transcriptionResult.fullText);
            emit transcriptionCompleted("", transcriptionResult.fullText);
        } else {
            QString errorMessage = QString("Audio transcription failed (error %1)")
                                 .arg(static_cast<int>(result.error()));
            emit transcriptionError("", errorMessage);
        }
    });
    
    watcher->setFuture(future);
}

void TranscriptionController::downloadModel(const QString& modelSize) {
    Logger::instance().info("Downloading model: {}", modelSize.toStdString());
    
    if (!whisperEngine_) {
        Logger::instance().error("WhisperEngine not available");
        emit modelDownloadFailed(modelSize, "Transcription engine not available");
        return;
    }
    
    auto future = whisperEngine_->downloadModel(modelSize);
    
    auto watcher = new QFutureWatcher<Expected<bool, TranscriptionError>>(this);
    connect(watcher, &QFutureWatcher<Expected<bool, TranscriptionError>>::finished,
            [this, watcher, modelSize]() {
        auto result = watcher->result();
        watcher->deleteLater();
        
        if (result.hasValue() && result.value()) {
            emit modelDownloadCompleted(modelSize);
            updateAvailableOptions();
        } else {
            emit modelDownloadFailed(modelSize, "Download failed");
        }
    });
    
    watcher->setFuture(future);
}

void TranscriptionController::cancelTranscription() {
    Logger::instance().info("Cancelling transcription");
    
    if (whisperEngine_) {
        whisperEngine_->cancelAllTranscriptions();
    }
    
    setTranscribing(false);
}

void TranscriptionController::cancelAllTranscriptions() {
    Logger::instance().info("Cancelling all transcriptions");
    
    if (whisperEngine_) {
        whisperEngine_->cancelAllTranscriptions();
    }
    
    activeTranscriptions_.clear();
    setTranscribing(false);
}

void TranscriptionController::clearTranscription() {
    setTranscription("");
    currentMediaId_.clear();
}

void TranscriptionController::loadTranscription(const QString& mediaId) {
    Logger::instance().info("Loading transcription for media: {}", mediaId.toStdString());
    
    if (!storageManager_) {
        Logger::instance().error("StorageManager not available");
        return;
    }
    
    auto future = QtConcurrent::run([this, mediaId]() {
        auto result = storageManager_->getTranscriptionByMedia(mediaId);
        if (result.hasValue()) {
            TranscriptionRecord transcription = result.value();
            
            // Update on main thread
            QMetaObject::invokeMethod(this, [this, transcription, mediaId]() {
                setTranscription(transcription.fullText);
                currentMediaId_ = mediaId;
            }, Qt::QueuedConnection);
        } else {
            Logger::instance().warn("No transcription found for media: {}", mediaId.toStdString());
        }
    });
}

void TranscriptionController::exportTranscription(const QString& format, const QString& outputPath) {
    Logger::instance().info("Exporting transcription to: {}", outputPath.toStdString());
    
    if (currentTranscription_.isEmpty()) {
        emit transcriptionError("", "No transcription to export");
        return;
    }
    
    auto future = QtConcurrent::run([this, format, outputPath]() {
        QFile file(outputPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMetaObject::invokeMethod(this, [this, outputPath]() {
                emit transcriptionError("", "Failed to create output file: " + outputPath);
            }, Qt::QueuedConnection);
            return;
        }
        
        QTextStream out(&file);
        
        if (format.toLower() == "txt") {
            out << currentTranscription_;
        } else if (format.toLower() == "json") {
            QJsonObject json;
            json["transcription"] = currentTranscription_;
            json["language"] = selectedLanguage_;
            json["model"] = selectedModel_;
            json["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
            
            QJsonDocument doc(json);
            out << doc.toJson();
        } else {
            // Default to plain text
            out << currentTranscription_;
        }
        
        file.close();
        
        QMetaObject::invokeMethod(this, [this, outputPath]() {
            emit transcriptionExported(outputPath);
        }, Qt::QueuedConnection);
    });
}

void TranscriptionController::onTranscriptionProgress(const QString& taskId, const TranscriptionProgress& progress) {
    emit transcriptionProgress(taskId, progress.percentage / 100.0);
}

void TranscriptionController::onTranscriptionCompleted(const QString& taskId, const TranscriptionResult& result) {
    QString mediaId = activeTranscriptions_.value(taskId);
    activeTranscriptions_.remove(taskId);
    
    setTranscription(result.fullText);
    
    if (storageManager_ && !mediaId.isEmpty()) {
        storeTranscriptionResult(mediaId, result);
    }
    
    if (activeTranscriptions_.isEmpty()) {
        setTranscribing(false);
    }
    
    emit transcriptionCompleted(taskId, result.fullText);
}

void TranscriptionController::onTranscriptionFailed(const QString& taskId, TranscriptionError error, const QString& errorString) {
    activeTranscriptions_.remove(taskId);
    
    if (activeTranscriptions_.isEmpty()) {
        setTranscribing(false);
    }
    
    QString fullError = QString("Transcription failed (error %1): %2")
                       .arg(static_cast<int>(error))
                       .arg(errorString);
    emit transcriptionError(taskId, fullError);
}

void TranscriptionController::onModelDownloadProgress(const QString& modelSize, qint64 bytesReceived, qint64 bytesTotal) {
    if (bytesTotal > 0) {
        double progress = static_cast<double>(bytesReceived) / bytesTotal;
        emit modelDownloadProgress(modelSize, progress);
    }
}

void TranscriptionController::onModelDownloadCompleted(const QString& modelSize) {
    emit modelDownloadCompleted(modelSize);
    updateAvailableOptions();
}

void TranscriptionController::onModelDownloadFailed(const QString& modelSize, const QString& error) {
    emit modelDownloadFailed(modelSize, error);
}

void TranscriptionController::setTranscribing(bool transcribing) {
    if (isTranscribing_ != transcribing) {
        isTranscribing_ = transcribing;
        emit transcribingChanged();
    }
}

void TranscriptionController::setTranscription(const QString& transcription) {
    if (currentTranscription_ != transcription) {
        currentTranscription_ = transcription;
        emit transcriptionChanged();
    }
}

void TranscriptionController::updateAvailableOptions() {
    if (!whisperEngine_) return;
    
    QStringList newLanguages = whisperEngine_->getSupportedLanguages();
    if (availableLanguages_ != newLanguages) {
        availableLanguages_ = newLanguages;
        emit availableLanguagesChanged();
    }
    
    QStringList newModels = whisperEngine_->getAvailableModels();
    if (availableModels_ != newModels) {
        availableModels_ = newModels;
        emit availableModelsChanged();
    }
}

void TranscriptionController::connectEngineSignals() {
    if (!whisperEngine_) return;
    
    connect(whisperEngine_, &WhisperEngine::transcriptionProgress,
            this, &TranscriptionController::onTranscriptionProgress);
    connect(whisperEngine_, &WhisperEngine::transcriptionCompleted,
            this, &TranscriptionController::onTranscriptionCompleted);
    connect(whisperEngine_, &WhisperEngine::transcriptionFailed,
            this, &TranscriptionController::onTranscriptionFailed);
    connect(whisperEngine_, &WhisperEngine::modelDownloadProgress,
            this, &TranscriptionController::onModelDownloadProgress);
    connect(whisperEngine_, &WhisperEngine::modelDownloadCompleted,
            this, &TranscriptionController::onModelDownloadCompleted);
    connect(whisperEngine_, &WhisperEngine::modelDownloadFailed,
            this, &TranscriptionController::onModelDownloadFailed);
}

TranscriptionSettings TranscriptionController::createTranscriptionSettings() const {
    TranscriptionSettings settings;
    settings.language = selectedLanguage_;
    settings.modelSize = selectedModel_;
    settings.enableTimestamps = true;
    settings.enableWordConfidence = true;
    settings.enableVAD = true;
    settings.enablePunctuation = true;
    settings.enableCapitalization = true;
    settings.outputFormat = "json";
    settings.enableGPU = true;
    return settings;
}

void TranscriptionController::storeTranscriptionResult(const QString& mediaId, const TranscriptionResult& result) {
    if (!storageManager_) return;
    
    auto future = QtConcurrent::run([this, mediaId, result]() {
        TranscriptionRecord record;
        record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        record.mediaId = mediaId;
        record.language = result.language;
        record.modelUsed = result.modelUsed;
        record.fullText = result.fullText;
        record.confidence = result.confidence;
        record.dateCreated = QDateTime::currentDateTime();
        record.processingTime = result.processingTime;
        record.status = "completed";
        
        // Convert segments to JSON
        QJsonArray segmentsArray;
        for (const auto& segment : result.segments) {
            QJsonObject segmentObj;
            segmentObj["startTime"] = static_cast<qint64>(segment.startTime);
            segmentObj["endTime"] = static_cast<qint64>(segment.endTime);
            segmentObj["text"] = segment.text;
            segmentObj["confidence"] = segment.confidence;
            segmentsArray.append(segmentObj);
        }
        
        QJsonObject timestamps;
        timestamps["segments"] = segmentsArray;
        record.timestamps = timestamps;
        
        auto addResult = storageManager_->addTranscription(record);
        if (addResult.hasError()) {
            Logger::instance().error("Failed to store transcription in database");
        } else {
            // Update media record to indicate it has transcription
            auto mediaResult = storageManager_->getMedia(mediaId);
            if (mediaResult.hasValue()) {
                MediaRecord media = mediaResult.value();
                media.hasTranscription = true;
                storageManager_->updateMedia(media);
            }
        }
    });
}

} // namespace Murmur