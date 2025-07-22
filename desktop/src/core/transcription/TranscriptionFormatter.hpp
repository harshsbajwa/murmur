#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QTextStream>
#include "../common/Expected.hpp"
#include "WhisperEngine.hpp"

namespace Murmur {

enum class FormatError {
    InvalidInput,
    EmptyTranscription,
    InvalidTimestamp,
    FormatNotSupported,
    GenerationFailed
};

/**
 * @brief Transcription format converter
 * 
 * This class provides comprehensive format conversion for transcription results,
 * supporting multiple subtitle formats with proper validation and error handling.
 * Note: The TranscriptionResult and TranscriptionSegment structs are defined in WhisperEngine.hpp
 */
class TranscriptionFormatter {
public:
    TranscriptionFormatter() = default;
    ~TranscriptionFormatter() = default;

    /**
     * @brief Convert transcription to SRT (SubRip) format
     * @param result Transcription result
     * @param options Additional formatting options
     * @return SRT formatted string or error
     */
    static Expected<QString, FormatError> convertToSRT(
        const TranscriptionResult& result,
        const QJsonObject& options = QJsonObject()
    );

    /**
     * @brief Convert transcription to VTT (WebVTT) format
     * @param result Transcription result
     * @param options Additional formatting options
     * @return VTT formatted string or error
     */
    static Expected<QString, FormatError> convertToVTT(
        const TranscriptionResult& result,
        const QJsonObject& options = QJsonObject()
    );

    /**
     * @brief Convert transcription to plain text
     * @param result Transcription result
     * @param options Text formatting options
     * @return Plain text string or error
     */
    static Expected<QString, FormatError> convertToPlainText(
        const TranscriptionResult& result,
        const QJsonObject& options = QJsonObject()
    );

    /**
     * @brief Convert transcription to JSON format
     * @param result Transcription result
     * @param options JSON formatting options
     * @return JSON formatted string or error
     */
    static Expected<QString, FormatError> convertToJSON(
        const TranscriptionResult& result,
        const QJsonObject& options = QJsonObject()
    );

    /**
     * @brief Convert transcription to ASS (Advanced SubStation Alpha) format
     * @param result Transcription result
     * @param options ASS formatting options
     * @return ASS formatted string or error
     */
    static Expected<QString, FormatError> convertToASS(
        const TranscriptionResult& result,
        const QJsonObject& options = QJsonObject()
    );

    /**
     * @brief Convert transcription to LRC (Lyric) format
     * @param result Transcription result
     * @param options LRC formatting options
     * @return LRC formatted string or error
     */
    static Expected<QString, FormatError> convertToLRC(
        const TranscriptionResult& result,
        const QJsonObject& options = QJsonObject()
    );

    /**
     * @brief Convert transcription to CSV format for analysis
     * @param result Transcription result
     * @param options CSV formatting options
     * @return CSV formatted string or error
     */
    static Expected<QString, FormatError> convertToCSV(
        const TranscriptionResult& result,
        const QJsonObject& options = QJsonObject()
    );

    /**
     * @brief Get list of supported output formats
     * @return List of format identifiers
     */
    static QStringList getSupportedFormats();

    /**
     * @brief Validate transcription result before conversion
     * @param result Transcription result to validate
     * @return true if valid, error otherwise
     */
    static Expected<bool, FormatError> validateTranscriptionResult(const TranscriptionResult& result);

    /**
     * @brief Merge overlapping segments with configurable thresholds
     * @param segments Input segments
     * @param maxGapMs Maximum gap to merge (milliseconds)
     * @param maxLengthMs Maximum segment length (milliseconds)
     * @return Merged segments
     */
    static QList<TranscriptionSegment> mergeSegments(
        const QList<TranscriptionSegment>& segments,
        int maxGapMs = 500,
        int maxLengthMs = 10000
    );

    /**
     * @brief Split long segments into smaller ones
     * @param segments Input segments
     * @param maxLengthMs Maximum segment length (milliseconds)
     * @param splitOnWords Try to split on word boundaries
     * @return Split segments
     */
    static QList<TranscriptionSegment> splitLongSegments(
        const QList<TranscriptionSegment>& segments,
        int maxLengthMs = 10000,
        bool splitOnWords = true
    );

    /**
     * @brief Apply text post-processing (capitalization, punctuation, etc.)
     * @param text Input text
     * @param options Processing options
     * @return Processed text
     */
    static QString postProcessText(const QString& text, const QJsonObject& options = QJsonObject());

private:
    // SRT specific helpers
    static QString formatSRTTimestamp(qint64 milliseconds);
    static QString escapeSRTText(const QString& text);
    
    // VTT specific helpers
    static QString formatVTTTimestamp(qint64 milliseconds);
    static QString escapeVTTText(const QString& text);
    
    // ASS specific helpers
    static QString formatASSTime(qint64 milliseconds);
    
    // LRC specific helpers
    static QString formatLRCTime(qint64 milliseconds);
    
    // Common helpers
    static bool isValidTimestamp(qint64 milliseconds);
    static QString capitalizeFirstLetter(const QString& text);
    static QString addPunctuation(const QString& text);
    static QString removeExtraSpaces(const QString& text);
};

} // namespace Murmur