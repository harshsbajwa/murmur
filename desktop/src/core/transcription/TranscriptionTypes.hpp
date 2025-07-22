#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QJsonObject>
#include <QtCore/QDateTime>
#include <QtCore/QUrl>
#include <vector>

namespace Murmur {

enum class TranscriptionError {
    InitializationFailed,
    InvalidData,
    SerializationError,
    DeserializationError,
    ValidationError,
    ModelNotFound,
    ModelNotLoaded,
    SegmentNotFound,
    InvalidTimestamp,
    InvalidLanguage,
    MemoryError,
    AudioProcessingFailed,
    InferenceError,
    InvalidAudioFormat,
    ResourceExhausted,
    ModelDownloadFailed,
    Cancelled,
    UnsupportedLanguage
};

struct TranscriptionSegment {
    qint64 id = 0;
    qint64 startTime = 0;  // milliseconds
    qint64 endTime = 0;    // milliseconds
    QString text;
    float confidence = 0.0f;
    QString language;
    bool isWordLevel = false;
    QJsonObject metadata;
    
    // Word-level segments (for detailed transcription)
    std::vector<TranscriptionSegment> words;
    
    // Additional fields for compatibility with WhisperEngine
    QStringList tokens;
    QList<double> tokenProbabilities;
    
    // Validation
    bool isValid() const {
        return startTime >= 0 && endTime > startTime && !text.isEmpty() && 
               confidence >= 0.0f && confidence <= 1.0f;
    }
    
    // Duration in milliseconds
    qint64 duration() const {
        return endTime - startTime;
    }
};

struct TranscriptionMetadata {
    QString fileName;
    QString filePath;
    QUrl sourceUrl;
    QString language;
    QString detectedLanguage;
    QString modelName;
    QString modelVersion;
    qint64 duration = 0;        // Total duration in milliseconds
    qint64 fileSize = 0;        // Original file size in bytes
    QDateTime createdAt;
    QDateTime modifiedAt;
    float averageConfidence = 0.0f;
    int segmentCount = 0;
    int wordCount = 0;
    QString format;         // "segments", "words", "both"
    QJsonObject customData;
    
    // Processing stats
    qint64 processingTime = 0;  // milliseconds
    QString processingEngine;
    bool hardwareAccelerated = false;
};

// For backward compatibility with WhisperEngine
struct TranscriptionResult {
    QString language;
    QString detectedLanguage;
    QList<TranscriptionSegment> segments;
    QDateTime processedAt;
    qint64 processingTime = 0;
    float averageConfidence = 0.0f;
    QJsonObject metadata;
    
    // Additional fields for compatibility
    QString fullText;
    double confidence = 0.0;
    QString modelUsed;
    
    // Convert to new format
    TranscriptionMetadata toMetadata() const {
        TranscriptionMetadata meta;
        meta.language = language;
        meta.detectedLanguage = detectedLanguage;
        meta.averageConfidence = averageConfidence;
        meta.segmentCount = segments.size();
        meta.processingTime = processingTime;
        meta.createdAt = processedAt;
        meta.modifiedAt = processedAt;
        meta.customData = metadata;
        return meta;
    }
    
    std::vector<TranscriptionSegment> toSegments() const {
        std::vector<TranscriptionSegment> result;
        result.reserve(segments.size());
        for (const auto& segment : segments) {
            result.push_back(segment);
        }
        return result;
    }
};

} // namespace Murmur