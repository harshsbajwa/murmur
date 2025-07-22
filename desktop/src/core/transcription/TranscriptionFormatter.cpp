#include "TranscriptionFormatter.hpp"
#include "../common/Logger.hpp"

#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QDateTime>
#include <QtCore/QTextStream>
#include <QtCore/QStringBuilder>
#include <cmath>

namespace Murmur {

Expected<QString, FormatError> TranscriptionFormatter::convertToSRT(
    const TranscriptionResult& result,
    const QJsonObject& options) {
    
    auto validateResult = validateTranscriptionResult(result);
    if (validateResult.hasError()) {
        return makeUnexpected(validateResult.error());
    }

    if (result.segments.isEmpty()) {
        return makeUnexpected(FormatError::EmptyTranscription);
    }

    QString srt;
    QTextStream stream(&srt);
    
    // Process segments with optional merging
    QList<TranscriptionSegment> segments = result.segments;
    
    if (options.value("mergeSegments").toBool(false)) {
        int maxGap = options.value("maxGapMs").toInt(500);
        int maxLength = options.value("maxLengthMs").toInt(10000);
        segments = mergeSegments(segments, maxGap, maxLength);
    }
    
    if (options.value("splitLongSegments").toBool(true)) {
        int maxLength = options.value("maxSegmentLengthMs").toInt(10000);
        bool splitOnWords = options.value("splitOnWords").toBool(true);
        segments = splitLongSegments(segments, maxLength, splitOnWords);
    }

    int segmentNumber = 1;
    for (const auto& segment : segments) {
        if (!isValidTimestamp(segment.startTime) || !isValidTimestamp(segment.endTime)) {
            Logger::instance().warn("Invalid timestamp in segment, skipping");
            continue;
        }

        if (segment.text.trimmed().isEmpty()) {
            continue; // Skip empty segments
        }

        // Segment number
        stream << segmentNumber << "\n";

        // Timestamps
        QString startTime = formatSRTTimestamp(segment.startTime);
        QString endTime = formatSRTTimestamp(segment.endTime);
        stream << startTime << " --> " << endTime << "\n";

        // Text with post-processing
        QString text = segment.text.trimmed();
        if (options.value("postProcessText").toBool(true)) {
            text = postProcessText(text, options);
        }
        text = escapeSRTText(text);
        
        stream << text << "\n\n";
        segmentNumber++;
    }

    Logger::instance().info("Generated SRT with {} segments", segmentNumber - 1);
    return srt;
}

Expected<QString, FormatError> TranscriptionFormatter::convertToVTT(
    const TranscriptionResult& result,
    const QJsonObject& options) {
    
    auto validateResult = validateTranscriptionResult(result);
    if (validateResult.hasError()) {
        return makeUnexpected(validateResult.error());
    }

    if (result.segments.isEmpty()) {
        return makeUnexpected(FormatError::EmptyTranscription);
    }

    QString vtt;
    QTextStream stream(&vtt);
    
    // VTT header
    stream << "WEBVTT\n";
    
    // Optional VTT metadata
    if (options.contains("title")) {
        stream << "Title: " << options.value("title").toString() << "\n";
    }
    if (options.contains("language")) {
        stream << "Language: " << options.value("language").toString() << "\n";
    }
    if (!result.modelUsed.isEmpty()) {
        stream << "X-Model: " << result.modelUsed << "\n";
    }
    if (result.confidence > 0) {
        stream << "X-Confidence: " << QString::number(result.confidence, 'f', 3) << "\n";
    }
    
    stream << "\n";

    // Process segments
    QList<TranscriptionSegment> segments = result.segments;
    
    if (options.value("mergeSegments").toBool(false)) {
        int maxGap = options.value("maxGapMs").toInt(500);
        int maxLength = options.value("maxLengthMs").toInt(10000);
        segments = mergeSegments(segments, maxGap, maxLength);
    }

    for (const auto& segment : segments) {
        if (!isValidTimestamp(segment.startTime) || !isValidTimestamp(segment.endTime)) {
            continue;
        }

        if (segment.text.trimmed().isEmpty()) {
            continue;
        }

        // Timestamps
        QString startTime = formatVTTTimestamp(segment.startTime);
        QString endTime = formatVTTTimestamp(segment.endTime);
        stream << startTime << " --> " << endTime;
        
        // Optional positioning and styling
        if (options.contains("position")) {
            stream << " position:" << options.value("position").toString();
        }
        if (options.contains("align")) {
            stream << " align:" << options.value("align").toString();
        }
        
        stream << "\n";

        // Text with post-processing
        QString text = segment.text.trimmed();
        if (options.value("postProcessText").toBool(true)) {
            text = postProcessText(text, options);
        }
        text = escapeVTTText(text);
        
        stream << text << "\n\n";
    }

    Logger::instance().info("Generated VTT with {} segments", segments.size());
    return vtt;
}

Expected<QString, FormatError> TranscriptionFormatter::convertToPlainText(
    const TranscriptionResult& result,
    const QJsonObject& options) {
    
    auto validateResult = validateTranscriptionResult(result);
    if (validateResult.hasError()) {
        return makeUnexpected(validateResult.error());
    }

    if (result.fullText.isEmpty() && result.segments.isEmpty()) {
        return makeUnexpected(FormatError::EmptyTranscription);
    }

    QString text;
    
    // Use full text if available and preferred
    if (!result.fullText.isEmpty() && options.value("useFullText").toBool(true)) {
        text = result.fullText;
    } else {
        // Concatenate segments
        QStringList textParts;
        for (const auto& segment : result.segments) {
            QString segmentText = segment.text.trimmed();
            if (!segmentText.isEmpty()) {
                textParts << segmentText;
            }
        }
        text = textParts.join(" ");
    }

    // Post-processing options
    if (options.value("postProcessText").toBool(true)) {
        text = postProcessText(text, options);
    }
    
    // Add timestamps if requested
    if (options.value("includeTimestamps").toBool(false)) {
        QString timestampedText;
        QTextStream stream(&timestampedText);
        
        QString timestampFormat = options.value("timestampFormat").toString("[mm:ss]");
        
        for (const auto& segment : result.segments) {
            if (segment.text.trimmed().isEmpty()) continue;
            
            QString timestamp;
            if (timestampFormat == "[mm:ss]") {
                int minutes = static_cast<int>(segment.startTime / 1000) / 60;
                int seconds = static_cast<int>(segment.startTime / 1000) % 60;
                timestamp = QString("[%1:%2]").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
            } else if (timestampFormat == "[hh:mm:ss]") {
                timestamp = formatSRTTimestamp(segment.startTime).split(',').first();
                timestamp = "[" + timestamp + "]";
            } else {
                timestamp = QString("[%1s]").arg(segment.startTime / 1000.0, 0, 'f', 1);
            }
            
            stream << timestamp << " " << segment.text.trimmed() << "\n";
        }
        
        return timestampedText;
    }
    
    // Add metadata if requested
    if (options.value("includeMetadata").toBool(false)) {
        QString metadata;
        QTextStream metaStream(&metadata);
        
        metaStream << "=== Transcription Metadata ===\n";
        metaStream << "Language: " << result.language << "\n";
        metaStream << "Model: " << result.modelUsed << "\n";
        metaStream << "Processing Time: " << QString::number(result.processingTime / 1000.0, 'f', 2) << "s\n";
        metaStream << "Average Confidence: " << QString::number(result.confidence, 'f', 3) << "\n";
        metaStream << "Segments: " << result.segments.size() << "\n";
        metaStream << "Generated: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
        metaStream << "==============================\n\n";
        
        text = metadata + text;
    }

    Logger::instance().info("Generated plain text ({} characters)", text.length());
    return text.trimmed();
}

Expected<QString, FormatError> TranscriptionFormatter::convertToJSON(
    const TranscriptionResult& result,
    const QJsonObject& options) {
    
    auto validateResult = validateTranscriptionResult(result);
    if (validateResult.hasError()) {
        return makeUnexpected(validateResult.error());
    }

    QJsonObject json;
    
    // Basic information
    json["language"] = result.language;
    json["text"] = result.fullText;
    json["model"] = result.modelUsed;
    json["processingTime"] = result.processingTime;
    json["averageConfidence"] = result.confidence;
    
    // Segments array
    QJsonArray segmentsArray;
    for (const auto& segment : result.segments) {
        QJsonObject segmentObj;
        segmentObj["start"] = segment.startTime;
        segmentObj["end"] = segment.endTime;
        segmentObj["text"] = segment.text;
        segmentObj["confidence"] = segment.confidence;
        
        // Include word-level timestamps if available
        if (options.value("includeWords").toBool(true) && !segment.tokens.isEmpty()) {
            QJsonArray wordsArray;
            for(int i=0; i < segment.tokens.size(); ++i) {
                QJsonObject wordObj;
                wordObj["word"] = segment.tokens[i];
                wordObj["prob"] = segment.tokenProbabilities[i];
                wordsArray.append(wordObj);
            }
            segmentObj["words"] = wordsArray;
        }
        
        segmentsArray.append(segmentObj);
    }
    json["segments"] = segmentsArray;
    
    // Metadata
    QJsonObject metadata = result.metadata;
    metadata["generatedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    metadata["formatVersion"] = "1.0";
    
    if (options.value("includeOptions").toBool()) {
        metadata["formatOptions"] = options;
    }
    
    json["metadata"] = metadata;

    // Convert to string
    QJsonDocument doc(json);
    QString jsonString;
    
    if (options.value("compact").toBool(false)) {
        jsonString = doc.toJson(QJsonDocument::Compact);
    } else {
        jsonString = doc.toJson(QJsonDocument::Indented);
    }

    Logger::instance().info("Generated JSON ({} characters)", jsonString.length());
    return jsonString;
}

Expected<QString, FormatError> TranscriptionFormatter::convertToCSV(
    const TranscriptionResult& result,
    const QJsonObject& options) {
    
    auto validateResult = validateTranscriptionResult(result);
    if (validateResult.hasError()) {
        return makeUnexpected(validateResult.error());
    }

    if (result.segments.isEmpty()) {
        return makeUnexpected(FormatError::EmptyTranscription);
    }

    QString csv;
    QTextStream stream(&csv);
    
    QString delimiter = options.value("delimiter").toString(",");
    bool includeHeader = options.value("includeHeader").toBool(true);
    bool includeConfidence = options.value("includeConfidence").toBool(true);
    
    // Header
    if (includeHeader) {
        QStringList headers = {"Start (ms)", "End (ms)", "Duration (ms)", "Text"};
        if (includeConfidence) {
            headers << "Confidence";
        }
        stream << headers.join(delimiter) << "\n";
    }

    // Segments
    for (const auto& segment : result.segments) {
        if (segment.text.trimmed().isEmpty()) continue;
        
        QStringList fields;
        fields << QString::number(segment.startTime);
        fields << QString::number(segment.endTime);
        fields << QString::number(segment.endTime - segment.startTime);
        
        // Escape text for CSV
        QString text = segment.text.trimmed();
        text.replace("\"", "\"\""); // Escape quotes
        if (text.contains(delimiter) || text.contains("\n") || text.contains("\"")) {
            text = "\"" + text + "\"";
        }
        fields << text;
        
        if (includeConfidence) {
            fields << QString::number(segment.confidence, 'f', 3);
        }
        
        stream << fields.join(delimiter) << "\n";
    }

    Logger::instance().info("Generated CSV with {} rows", result.segments.size());
    return csv;
}

QStringList TranscriptionFormatter::getSupportedFormats() {
    return {"srt", "vtt", "txt", "json", "csv", "ass", "lrc"};
}

Expected<bool, FormatError> TranscriptionFormatter::validateTranscriptionResult(const TranscriptionResult& result) {
    if (result.language.isEmpty()) {
        return makeUnexpected(FormatError::InvalidInput);
    }
    
    if (result.fullText.isEmpty() && result.segments.isEmpty()) {
        return makeUnexpected(FormatError::EmptyTranscription);
    }
    
    // Validate segments
    for (const auto& segment : result.segments) {
        if (segment.startTime < 0 || segment.endTime < 0) {
            return makeUnexpected(FormatError::InvalidTimestamp);
        }
        
        if (segment.endTime < segment.startTime) {
            return makeUnexpected(FormatError::InvalidTimestamp);
        }
        
        // Check for reasonable maximum duration (24 hours)
        if (segment.endTime > 24 * 3600 * 1000) {
            return makeUnexpected(FormatError::InvalidTimestamp);
        }
    }
    
    return true;
}

// Private helper implementations

QString TranscriptionFormatter::formatSRTTimestamp(qint64 milliseconds) {
    if (milliseconds < 0) milliseconds = 0;
    
    int ms = milliseconds % 1000;
    int totalSeconds = milliseconds / 1000;
    int secs = totalSeconds % 60;
    int totalMinutes = totalSeconds / 60;
    int mins = totalMinutes % 60;
    int hours = totalMinutes / 60;
    
    return QString("%1:%2:%3,%4")
           .arg(hours, 2, 10, QChar('0'))
           .arg(mins, 2, 10, QChar('0'))
           .arg(secs, 2, 10, QChar('0'))
           .arg(ms, 3, 10, QChar('0'));
}

QString TranscriptionFormatter::formatVTTTimestamp(qint64 milliseconds) {
    if (milliseconds < 0) milliseconds = 0;
    
    int ms = milliseconds % 1000;
    int totalSeconds = milliseconds / 1000;
    int secs = totalSeconds % 60;
    int totalMinutes = totalSeconds / 60;
    int mins = totalMinutes % 60;
    int hours = totalMinutes / 60;
    
    return QString("%1:%2:%3.%4")
           .arg(hours, 2, 10, QChar('0'))
           .arg(mins, 2, 10, QChar('0'))
           .arg(secs, 2, 10, QChar('0'))
           .arg(ms, 3, 10, QChar('0'));
}

QString TranscriptionFormatter::escapeSRTText(const QString& text) {
    QString escaped = text;
    
    // Remove or replace problematic characters
    escaped.replace(QRegularExpression(R"(\r\n|\r|\n)"), " ");
    escaped.replace(QRegularExpression(R"(\s+)"), " ");
    
    // Remove control characters except basic formatting
    escaped.remove(QRegularExpression(R"([\x00-\x08\x0B\x0C\x0E-\x1F\x7F])"));
    
    return escaped.trimmed();
}

QString TranscriptionFormatter::escapeVTTText(const QString& text) {
    QString escaped = text;
    
    // VTT allows some HTML-like tags, but we'll keep it simple
    escaped.replace("&", "&");
    escaped.replace("<", "<");
    escaped.replace(">", ">");
    
    // Handle line breaks
    escaped.replace(QRegularExpression(R"(\r\n|\r|\n)"), " ");
    escaped.replace(QRegularExpression(R"(\s+)"), " ");
    
    return escaped.trimmed();
}

QString TranscriptionFormatter::postProcessText(const QString& text, const QJsonObject& options) {
    QString processed = text;
    
    // Remove extra whitespace
    processed = removeExtraSpaces(processed);
    
    // Capitalization
    if (options.value("capitalizeFirst").toBool(true)) {
        processed = capitalizeFirstLetter(processed);
    }
    
    // Add punctuation
    if (options.value("addPunctuation").toBool(false)) {
        processed = addPunctuation(processed);
    }
    
    // Remove filler words if requested
    if (options.value("removeFillers").toBool(false)) {
        QStringList fillers = {"um", "uh", "er", "ah", "like", "you know"};
        for (const QString& filler : fillers) {
            QRegularExpression regex(R"(\b)" + QRegularExpression::escape(filler) + R"(\b)", 
                                   QRegularExpression::CaseInsensitiveOption);
            processed.replace(regex, "");
        }
        processed = removeExtraSpaces(processed);
    }
    
    return processed.trimmed();
}

QString TranscriptionFormatter::removeExtraSpaces(const QString& text) {
    QString cleaned = text;
    cleaned.replace(QRegularExpression(R"(\s+)"), " ");
    return cleaned.trimmed();
}

QString TranscriptionFormatter::capitalizeFirstLetter(const QString& text) {
    if (text.isEmpty()) return text;
    
    QString capitalized = text;
    if (capitalized[0].isLetter()) {
        capitalized[0] = capitalized[0].toUpper();
    }
    
    // Also capitalize after sentence endings
    QRegularExpression sentenceEnd(R"([.!?]\s+[a-z])");
    QRegularExpressionMatchIterator matches = sentenceEnd.globalMatch(capitalized);
    
    while (matches.hasNext()) {
        QRegularExpressionMatch match = matches.next();
        int pos = match.capturedStart() + match.capturedLength() - 1;
        if (pos < capitalized.length()) {
            capitalized[pos] = capitalized[pos].toUpper();
        }
    }
    
    return capitalized;
}

QString TranscriptionFormatter::addPunctuation(const QString& text) {
    QString punctuated = text.trimmed();
    
    if (punctuated.isEmpty()) return punctuated;
    
    // Add period at the end if no punctuation exists
    QChar lastChar = punctuated[punctuated.length() - 1];
    if (!lastChar.isPunct()) {
        punctuated += ".";
    }
    
    return punctuated;
}

bool TranscriptionFormatter::isValidTimestamp(qint64 milliseconds) {
    return milliseconds >= 0 && milliseconds <= (24LL * 3600 * 1000);
}

QList<TranscriptionSegment> TranscriptionFormatter::mergeSegments(
    const QList<TranscriptionSegment>& segments,
    int maxGapMs,
    int maxLengthMs) {
    
    if (segments.isEmpty()) return segments;
    
    QList<TranscriptionSegment> merged;
    TranscriptionSegment current = segments.first();
    
    for (int i = 1; i < segments.size(); ++i) {
        const TranscriptionSegment& next = segments[i];
        
        qint64 gapMs = next.startTime - current.endTime;
        qint64 currentLengthMs = current.endTime - current.startTime;
        
        // Merge if gap is small and resulting segment isn't too long
        if (gapMs <= maxGapMs && (currentLengthMs + gapMs + (next.endTime - next.startTime)) <= maxLengthMs) {
            current.endTime = next.endTime;
            current.text += " " + next.text;
            current.confidence = (current.confidence + next.confidence) / 2.0;
        } else {
            merged.append(current);
            current = next;
        }
    }
    
    merged.append(current);
    return merged;
}

QList<TranscriptionSegment> TranscriptionFormatter::splitLongSegments(
    const QList<TranscriptionSegment>& segments,
    int maxLengthMs,
    bool splitOnWords) {
    
    QList<TranscriptionSegment> result;
    
    for (const auto& segment : segments) {
        qint64 lengthMs = segment.endTime - segment.startTime;
        
        if (lengthMs <= maxLengthMs) {
            result.append(segment);
            continue;
        }
        
        // Split long segment
        QStringList words = segment.text.split(' ', Qt::SkipEmptyParts);
        if (words.isEmpty() || !splitOnWords) {
            // Simple time-based split
            int numParts = static_cast<int>(std::ceil(static_cast<double>(lengthMs) / maxLengthMs));
            double partDuration = static_cast<double>(lengthMs) / numParts;
            
            for (int i = 0; i < numParts; ++i) {
                TranscriptionSegment part;
                part.startTime = segment.startTime + static_cast<qint64>(i * partDuration);
                part.endTime = segment.startTime + static_cast<qint64>((i + 1) * partDuration);
                part.text = QString("Part %1 of %2: %3").arg(i + 1).arg(numParts).arg(segment.text);
                part.confidence = segment.confidence;
                result.append(part);
            }
        } else {
            // Split on words, trying to keep reasonable timing
            int wordsPerPart = std::max(1, static_cast<int>(words.size() * maxLengthMs / lengthMs));
            double timePerWord = static_cast<double>(lengthMs) / words.size();
            
            for (int i = 0; i < words.size(); i += wordsPerPart) {
                TranscriptionSegment part;
                part.startTime = segment.startTime + static_cast<qint64>(i * timePerWord);
                part.endTime = segment.startTime + static_cast<qint64>(std::min(static_cast<qsizetype>(i + wordsPerPart), words.size()) * timePerWord);
                
                QStringList partWords = words.mid(i, wordsPerPart);
                part.text = partWords.join(' ');
                part.confidence = segment.confidence;
                
                result.append(part);
            }
        }
    }
    
    return result;
}

// ASS and LRC format implementations
Expected<QString, FormatError> TranscriptionFormatter::convertToASS(
    const TranscriptionResult& result,
    const QJsonObject& options) {
    
    if (result.segments.isEmpty()) {
        return makeUnexpected(FormatError::EmptyTranscription);
    }
    
    QStringList lines;
    
    // ASS header
    lines << "[Script Info]";
    lines << "Title: Murmur Transcription";
    lines << "ScriptType: v4.00+";
    lines << "Collisions: Normal";
    lines << "PlayDepth: 0";
    lines << "";
    
    // Styles section
    lines << "[V4+ Styles]";
    lines << "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding";
    
    // Get style options
    QString fontName = options.value("fontName").toString("Arial");
    int fontSize = options.value("fontSize").toInt(20);
    QString primaryColor = options.value("primaryColor").toString("&H00FFFFFF"); // White
    QString outlineColor = options.value("outlineColor").toString("&H00000000"); // Black
    
    lines << QString("Style: Default,%1,%2,%3,&H000000FF,%4,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1")
             .arg(fontName)
             .arg(fontSize)
             .arg(primaryColor)
             .arg(outlineColor);
    lines << "";
    
    // Events section
    lines << "[Events]";
    lines << "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text";
    
    // Convert segments to ASS dialogue events
    for (const auto& segment : result.segments) {
        QString startTime = formatASSTime(segment.startTime);
        QString endTime = formatASSTime(segment.endTime);
        QString text = segment.text.trimmed();
        
        // Escape special characters for ASS
        text.replace("\\", "\\\\");
        text.replace("{", "\\{");
        text.replace("}", "\\}");
        text.replace("\n", "\\N");
        
        lines << QString("Dialogue: 0,%1,%2,Default,,0,0,0,,%3")
                 .arg(startTime)
                 .arg(endTime)
                 .arg(text);
    }
    
    QString output = lines.join("\n");
    Logger::instance().info("TranscriptionFormatter: Converted to ASS format ({} segments)", result.segments.size());
    
    return output;
}

Expected<QString, FormatError> TranscriptionFormatter::convertToLRC(
    const TranscriptionResult& result,
    const QJsonObject& options) {
    
    if (result.segments.isEmpty()) {
        return makeUnexpected(FormatError::EmptyTranscription);
    }
    
    QStringList lines;
    
    // LRC metadata headers
    lines << "[ar:Murmur]";
    lines << "[ti:Transcription]";
    lines << "[al:Audio Transcription]";
    lines << "[by:Whisper AI]";
    
    QString author = options.value("author").toString("");
    QString title = options.value("title").toString("Transcription");
    QString album = options.value("album").toString("");
    
    if (!author.isEmpty()) {
        lines << QString("[ar:%1]").arg(author);
    }
    if (!title.isEmpty()) {
        lines << QString("[ti:%1]").arg(title);
    }
    if (!album.isEmpty()) {
        lines << QString("[al:%1]").arg(album);
    }
    
    lines << ""; // Empty line after headers
    
    // Convert segments to LRC format
    for (const auto& segment : result.segments) {
        QString timeTag = formatLRCTime(segment.startTime);
        QString text = segment.text.trimmed();
        
        // LRC format: [mm:ss.xx]lyrics
        lines << QString("%1%2").arg(timeTag).arg(text);
    }
    
    QString output = lines.join("\n");
    Logger::instance().info("TranscriptionFormatter: Converted to LRC format ({} segments)", result.segments.size());
    
    return output;
}

QString TranscriptionFormatter::formatASSTime(qint64 milliseconds) {
    // ASS time format: H:MM:SS.CC (where CC is centiseconds)
    qint64 totalCentiseconds = milliseconds / 10; // Convert ms to centiseconds
    
    int hours = totalCentiseconds / 360000;
    int minutes = (totalCentiseconds % 360000) / 6000;
    int seconds = (totalCentiseconds % 6000) / 100;
    int centiseconds = totalCentiseconds % 100;
    
    return QString("%1:%2:%3.%4")
           .arg(hours)
           .arg(minutes, 2, 10, QChar('0'))
           .arg(seconds, 2, 10, QChar('0'))
           .arg(centiseconds, 2, 10, QChar('0'));
}

QString TranscriptionFormatter::formatLRCTime(qint64 milliseconds) {
    // LRC time format: [MM:SS.XX] where XX is centiseconds
    qint64 totalCentiseconds = milliseconds / 10; // Convert ms to centiseconds
    
    int minutes = totalCentiseconds / 6000;
    int seconds = (totalCentiseconds % 6000) / 100;
    int centiseconds = totalCentiseconds % 100;
    
    return QString("[%1:%2.%3]")
           .arg(minutes, 2, 10, QChar('0'))
           .arg(seconds, 2, 10, QChar('0'))
           .arg(centiseconds, 2, 10, QChar('0'));
}

} // namespace Murmur