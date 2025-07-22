#include "TranscriptionModel.hpp"
#include "../../core/common/Logger.hpp"
#include "../../core/transcription/WhisperEngine.hpp"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QMutexLocker>
#include <QtCore/QTimer>
#include <algorithm>

namespace Murmur {

class TranscriptionModel::TranscriptionModelPrivate {
public:
    std::vector<TranscriptionSegment> segments;
    WhisperEngine* whisperEngine = nullptr;
    QTimer* statisticsTimer;
    
    // Cached statistics
    QString currentLanguage;
    float averageConfidence = 0.0f;
    qint64 totalDuration = 0;
    bool isLoaded = false;
    bool statisticsValid = false;
    
    mutable QMutex mutex;
};

TranscriptionModel::TranscriptionModel(QObject* parent)
    : QAbstractListModel(parent)
    , d(std::make_unique<TranscriptionModelPrivate>())
{
    d->statisticsTimer = new QTimer(this);
    d->statisticsTimer->setSingleShot(true);
    d->statisticsTimer->setInterval(100); // Debounce statistics calculation
    connect(d->statisticsTimer, &QTimer::timeout, this, &TranscriptionModel::updateStatistics);
}

TranscriptionModel::~TranscriptionModel() = default;

int TranscriptionModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent)
    QMutexLocker locker(&d->mutex);
    return static_cast<int>(d->segments.size());
}

QVariant TranscriptionModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(d->segments.size())) {
        return QVariant();
    }
    
    QMutexLocker locker(&d->mutex);
    const auto& segment = d->segments[index.row()];
    
    switch (role) {
    case IdRole:
        return segment.id;
    case StartTimeRole:
        return segment.startTime;
    case EndTimeRole:
        return segment.endTime;
    case DurationRole:
        return segment.endTime - segment.startTime;
    case TextRole:
        return segment.text;
    case ConfidenceRole:
        return segment.confidence;
    case LanguageRole:
        return segment.language;
    case IsWordLevelRole:
        return segment.isWordLevel;
    case FormattedTimeRole:
        return formatTime(segment.startTime);
    case FormattedDurationRole:
        return formatDuration(segment.endTime - segment.startTime);
    case ConfidencePercentRole:
        return getConfidencePercentage(segment.confidence);
    case HasWordsRole:
        return !segment.words.empty();
    case WordCountRole:
        return static_cast<int>(segment.words.size());
    case MetadataRole:
        return segment.metadata;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> TranscriptionModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[IdRole] = "segmentId";
    roles[StartTimeRole] = "startTime";
    roles[EndTimeRole] = "endTime";
    roles[DurationRole] = "duration";
    roles[TextRole] = "text";
    roles[ConfidenceRole] = "confidence";
    roles[LanguageRole] = "language";
    roles[IsWordLevelRole] = "isWordLevel";
    roles[FormattedTimeRole] = "formattedTime";
    roles[FormattedDurationRole] = "formattedDuration";
    roles[ConfidencePercentRole] = "confidencePercent";
    roles[HasWordsRole] = "hasWords";
    roles[WordCountRole] = "wordCount";
    roles[MetadataRole] = "metadata";
    return roles;
}

bool TranscriptionModel::removeRows(int row, int count, const QModelIndex& parent) {
    if (row < 0 || count <= 0 || row + count > static_cast<int>(d->segments.size())) {
        return false;
    }
    
    beginRemoveRows(parent, row, row + count - 1);
    {
        QMutexLocker locker(&d->mutex);
        d->segments.erase(d->segments.begin() + row, d->segments.begin() + row + count);
    }
    endRemoveRows();
    
    invalidateStatistics();
    emit countChanged();
    return true;
}

int TranscriptionModel::count() const {
    QMutexLocker locker(&d->mutex);
    return static_cast<int>(d->segments.size());
}

bool TranscriptionModel::isEmpty() const {
    QMutexLocker locker(&d->mutex);
    return d->segments.empty();
}

QString TranscriptionModel::currentLanguage() const {
    QMutexLocker locker(&d->mutex);
    return d->currentLanguage;
}

float TranscriptionModel::averageConfidence() const {
    QMutexLocker locker(&d->mutex);
    return d->averageConfidence;
}

qint64 TranscriptionModel::totalDuration() const {
    QMutexLocker locker(&d->mutex);
    return d->totalDuration;
}

bool TranscriptionModel::isLoaded() const {
    QMutexLocker locker(&d->mutex);
    return d->isLoaded;
}

void TranscriptionModel::addSegment(const TranscriptionSegment& segment) {
    if (!validateSegment(segment)) {
        MURMUR_WARN("Invalid transcription segment, skipping");
        return;
    }
    
    insertSegmentSorted(segment);
    invalidateStatistics();
    emit segmentAdded(segment.id);
}

void TranscriptionModel::removeSegment(qint64 segmentId) {
    QMutexLocker locker(&d->mutex);
    auto it = std::find_if(d->segments.begin(), d->segments.end(),
                          [segmentId](const TranscriptionSegment& s) { return s.id == segmentId; });
    
    if (it != d->segments.end()) {
        int index = static_cast<int>(std::distance(d->segments.begin(), it));
        locker.unlock();
        removeRows(index, 1);
        emit segmentRemoved(segmentId);
    }
}

void TranscriptionModel::updateSegment(qint64 segmentId, const TranscriptionSegment& segment) {
    if (!validateSegment(segment)) {
        MURMUR_WARN("Invalid transcription segment update, skipping");
        return;
    }
    
    QMutexLocker locker(&d->mutex);
    auto it = std::find_if(d->segments.begin(), d->segments.end(),
                          [segmentId](const TranscriptionSegment& s) { return s.id == segmentId; });
    
    if (it != d->segments.end()) {
        *it = segment;
        int index = static_cast<int>(std::distance(d->segments.begin(), it));
        locker.unlock();
        
        emitDataChanged(index);
        invalidateStatistics();
        emit segmentUpdated(segmentId);
    }
}

void TranscriptionModel::clear() {
    beginResetModel();
    {
        QMutexLocker locker(&d->mutex);
        d->segments.clear();
        d->isLoaded = false;
        d->statisticsValid = false;
    }
    endResetModel();
    
    emit countChanged();
    emit loadedChanged();
}

void TranscriptionModel::loadFromFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(QString("Cannot open file: %1").arg(filePath));
        return;
    }
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        emit errorOccurred(QString("JSON parse error: %1").arg(error.errorString()));
        return;
    }
    
    QJsonArray segmentsArray = doc.array();
    std::vector<TranscriptionSegment> newSegments;
    
    for (const auto& value : segmentsArray) {
        QJsonObject obj = value.toObject();
        TranscriptionSegment segment;
        segment.id = obj["id"].toVariant().toLongLong();
        segment.startTime = obj["startTime"].toVariant().toLongLong();
        segment.endTime = obj["endTime"].toVariant().toLongLong();
        segment.text = obj["text"].toString();
        segment.confidence = static_cast<float>(obj["confidence"].toDouble());
        segment.language = obj["language"].toString();
        segment.isWordLevel = obj["isWordLevel"].toBool();
        segment.metadata = obj["metadata"].toObject();
        
        if (validateSegment(segment)) {
            newSegments.push_back(segment);
        }
    }
    
    beginResetModel();
    {
        QMutexLocker locker(&d->mutex);
        d->segments = std::move(newSegments);
        d->isLoaded = true;
    }
    endResetModel();
    
    invalidateStatistics();
    emit countChanged();
    emit loadedChanged();
    emit transcriptionLoaded(filePath);
}

void TranscriptionModel::saveToFile(const QString& filePath) {
    QJsonArray segmentsArray;
    
    {
        QMutexLocker locker(&d->mutex);
        for (const auto& segment : d->segments) {
            QJsonObject obj;
            obj["id"] = segment.id;
            obj["startTime"] = segment.startTime;
            obj["endTime"] = segment.endTime;
            obj["text"] = segment.text;
            obj["confidence"] = segment.confidence;
            obj["language"] = segment.language;
            obj["isWordLevel"] = segment.isWordLevel;
            obj["metadata"] = segment.metadata;
            segmentsArray.append(obj);
        }
    }
    
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(QString("Cannot write to file: %1").arg(filePath));
        return;
    }
    
    QJsonDocument doc(segmentsArray);
    file.write(doc.toJson());
    emit transcriptionSaved(filePath);
}

int TranscriptionModel::findSegmentByTime(qint64 timeMs) const {
    QMutexLocker locker(&d->mutex);
    for (size_t i = 0; i < d->segments.size(); ++i) {
        const auto& segment = d->segments[i];
        if (timeMs >= segment.startTime && timeMs <= segment.endTime) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

QVariantList TranscriptionModel::search(const QString& text, bool caseSensitive) const {
    QVariantList results;
    Qt::CaseSensitivity sensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    
    QMutexLocker locker(&d->mutex);
    for (size_t i = 0; i < d->segments.size(); ++i) {
        if (d->segments[i].text.contains(text, sensitivity)) {
            results.append(static_cast<int>(i));
        }
    }
    return results;
}

QString TranscriptionModel::getTextInRange(qint64 startTimeMs, qint64 endTimeMs) const {
    QStringList texts;
    
    QMutexLocker locker(&d->mutex);
    for (const auto& segment : d->segments) {
        if (segment.startTime >= startTimeMs && segment.endTime <= endTimeMs) {
            texts.append(segment.text);
        }
    }
    return texts.join(" ");
}

QString TranscriptionModel::exportAsPlainText() const {
    QStringList lines;
    
    QMutexLocker locker(&d->mutex);
    for (const auto& segment : d->segments) {
        lines.append(segment.text);
    }
    return lines.join("\n");
}

QString TranscriptionModel::exportAsSRT() const {
    QStringList lines;
    int index = 1;
    
    QMutexLocker locker(&d->mutex);
    for (const auto& segment : d->segments) {
        lines.append(QString::number(index++));
        
        QString startTime = formatTime(segment.startTime).replace('.', ',');
        QString endTime = formatTime(segment.endTime).replace('.', ',');
        lines.append(QString("%1 --> %2").arg(startTime, endTime));
        
        lines.append(segment.text);
        lines.append("");
    }
    return lines.join("\n");
}

QString TranscriptionModel::exportAsVTT() const {
    QStringList lines;
    lines.append("WEBVTT");
    lines.append("");
    
    QMutexLocker locker(&d->mutex);
    for (const auto& segment : d->segments) {
        QString startTime = formatTime(segment.startTime);
        QString endTime = formatTime(segment.endTime);
        lines.append(QString("%1 --> %2").arg(startTime, endTime));
        
        lines.append(segment.text);
        lines.append("");
    }
    return lines.join("\n");
}

QString TranscriptionModel::exportAsJSON() const {
    QJsonArray segmentsArray;
    
    QMutexLocker locker(&d->mutex);
    for (const auto& segment : d->segments) {
        QJsonObject obj;
        obj["startTime"] = segment.startTime;
        obj["endTime"] = segment.endTime;
        obj["text"] = segment.text;
        obj["confidence"] = segment.confidence;
        obj["language"] = segment.language;
        segmentsArray.append(obj);
    }
    
    QJsonDocument doc(segmentsArray);
    return doc.toJson();
}

QString TranscriptionModel::formatTime(qint64 timeMs) const {
    qint64 hours = timeMs / 3600000;
    qint64 minutes = (timeMs % 3600000) / 60000;
    qint64 seconds = (timeMs % 60000) / 1000;
    qint64 milliseconds = timeMs % 1000;
    
    return QString("%1:%2:%3.%4")
           .arg(hours, 2, 10, QChar('0'))
           .arg(minutes, 2, 10, QChar('0'))
           .arg(seconds, 2, 10, QChar('0'))
           .arg(milliseconds, 3, 10, QChar('0'));
}

QString TranscriptionModel::formatDuration(qint64 durationMs) const {
    if (durationMs < 1000) {
        return QString("%1ms").arg(durationMs);
    } else if (durationMs < 60000) {
        return QString("%1.%2s").arg(durationMs / 1000).arg((durationMs % 1000) / 100);
    } else {
        qint64 minutes = durationMs / 60000;
        qint64 seconds = (durationMs % 60000) / 1000;
        return QString("%1:%2").arg(minutes).arg(seconds, 2, 10, QChar('0'));
    }
}

float TranscriptionModel::getConfidencePercentage(float confidence) const {
    return confidence * 100.0f;
}

void TranscriptionModel::mergeSegments(const QList<int>& indices) {
    if (indices.size() < 2) return;
    
    QList<int> sortedIndices = indices;
    std::sort(sortedIndices.begin(), sortedIndices.end());
    
    QMutexLocker locker(&d->mutex);
    if (sortedIndices.last() >= static_cast<int>(d->segments.size())) return;
    
    // Create merged segment
    TranscriptionSegment merged = d->segments[sortedIndices.first()];
    merged.endTime = d->segments[sortedIndices.last()].endTime;
    
    QStringList texts;
    float totalConfidence = 0.0f;
    for (int index : sortedIndices) {
        texts.append(d->segments[index].text);
        totalConfidence += d->segments[index].confidence;
    }
    merged.text = texts.join(" ");
    merged.confidence = totalConfidence / indices.size();
    
    // Remove old segments and insert merged one
    for (int i = sortedIndices.size() - 1; i >= 0; --i) {
        d->segments.erase(d->segments.begin() + sortedIndices[i]);
    }
    
    insertSegmentSorted(merged);
    locker.unlock();
    
    beginResetModel();
    endResetModel();
    invalidateStatistics();
}

void TranscriptionModel::splitSegment(int index, qint64 splitTimeMs) {
    if (index < 0 || index >= static_cast<int>(d->segments.size())) return;
    
    QMutexLocker locker(&d->mutex);
    TranscriptionSegment original = d->segments[index];
    
    if (splitTimeMs <= original.startTime || splitTimeMs >= original.endTime) return;
    
    // Create two new segments
    TranscriptionSegment first = original;
    first.endTime = splitTimeMs;
    
    TranscriptionSegment second = original;
    second.startTime = splitTimeMs;
    second.id = original.id + 1; // Simple ID generation
    
    // Split text based on timing if word-level data available
    if (!original.words.empty()) {
        // Clear the original text for both segments
        first.text.clear();
        second.text.clear();
        first.words.clear();
        second.words.clear();
        
        // Distribute words to appropriate segments based on timing
        for (const auto& word : original.words) {
            qint64 wordMidpoint = (word.startTime + word.endTime) / 2;
            if (wordMidpoint < splitTimeMs) {
                first.words.push_back(word);
                if (!first.text.isEmpty()) first.text += " ";
                first.text += word.text;
            } else {
                second.words.push_back(word);
                if (!second.text.isEmpty()) second.text += " ";
                second.text += word.text;
            }
        }
        
        // If no words in first segment, use beginning of original text
        if (first.words.empty() && !original.text.isEmpty()) {
            QStringList words = original.text.split(' ', Qt::SkipEmptyParts);
            int midpoint = words.size() / 2;
            first.text = words.mid(0, midpoint).join(" ");
            second.text = words.mid(midpoint).join(" ");
        }
    } else {
        // No word-level data, do simple text split
        QStringList words = original.text.split(' ', Qt::SkipEmptyParts);
        int midpoint = words.size() / 2;
        first.text = words.mid(0, midpoint).join(" ");
        second.text = words.mid(midpoint).join(" ");
    }
    
    d->segments.erase(d->segments.begin() + index);
    insertSegmentSorted(first);
    insertSegmentSorted(second);
    locker.unlock();
    
    beginResetModel();
    endResetModel();
    invalidateStatistics();
}

void TranscriptionModel::adjustTiming(int index, qint64 startOffset, qint64 endOffset) {
    if (index < 0 || index >= static_cast<int>(d->segments.size())) return;
    
    QMutexLocker locker(&d->mutex);
    d->segments[index].startTime += startOffset;
    d->segments[index].endTime += endOffset;
    locker.unlock();
    
    emitDataChanged(index);
    invalidateStatistics();
}

void TranscriptionModel::setWhisperEngine(WhisperEngine* engine) {
    d->whisperEngine = engine;
    if (engine) {
        // Connect to engine signals if needed
        connect(engine, &WhisperEngine::transcriptionCompleted,
                this, &TranscriptionModel::onTranscriptionUpdated);
    }
}

void TranscriptionModel::loadFromTranscription(const std::vector<TranscriptionSegment>& segments) {
    beginResetModel();
    {
        QMutexLocker locker(&d->mutex);
        d->segments = segments;
        d->isLoaded = !segments.empty();
    }
    endResetModel();
    
    invalidateStatistics();
    emit countChanged();
    emit loadedChanged();
}

std::vector<TranscriptionSegment> TranscriptionModel::getSegments() const {
    QMutexLocker locker(&d->mutex);
    return d->segments;
}

void TranscriptionModel::onTranscriptionUpdated() {
    // Handle updates from transcription engine
    invalidateStatistics();
}

void TranscriptionModel::updateStatistics() {
    QMutexLocker locker(&d->mutex);
    if (d->statisticsValid) return;
    
    calculateStatistics();
    d->statisticsValid = true;
    
    emit confidenceChanged();
    emit durationChanged();
    emit languageChanged();
}

void TranscriptionModel::invalidateStatistics() {
    d->statisticsValid = false;
    d->statisticsTimer->start();
}

void TranscriptionModel::calculateStatistics() {
    if (d->segments.empty()) {
        d->averageConfidence = 0.0f;
        d->totalDuration = 0;
        d->currentLanguage.clear();
        return;
    }
    
    float totalConfidence = 0.0f;
    qint64 minStart = LLONG_MAX;
    qint64 maxEnd = 0;
    QMap<QString, int> languageCounts;
    
    for (const auto& segment : d->segments) {
        totalConfidence += segment.confidence;
        minStart = std::min(minStart, segment.startTime);
        maxEnd = std::max(maxEnd, segment.endTime);
        languageCounts[segment.language]++;
    }
    
    d->averageConfidence = totalConfidence / d->segments.size();
    d->totalDuration = maxEnd - minStart;
    
    // Find most common language
    auto maxLang = std::max_element(languageCounts.begin(), languageCounts.end());
    d->currentLanguage = maxLang != languageCounts.end() ? maxLang.key() : QString();
}

bool TranscriptionModel::validateSegment(const TranscriptionSegment& segment) const {
    return segment.startTime >= 0 && 
           segment.endTime > segment.startTime && 
           !segment.text.isEmpty() &&
           segment.confidence >= 0.0f && segment.confidence <= 1.0f;
}

QString TranscriptionModel::generateId() const {
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

void TranscriptionModel::emitDataChanged(int index) {
    QModelIndex modelIndex = createIndex(index, 0);
    emit dataChanged(modelIndex, modelIndex);
}

void TranscriptionModel::insertSegmentSorted(const TranscriptionSegment& segment) {
    beginInsertRows(QModelIndex(), 0, 0);
    {
        QMutexLocker locker(&d->mutex);
        auto it = std::lower_bound(d->segments.begin(), d->segments.end(), segment,
                                  [](const TranscriptionSegment& a, const TranscriptionSegment& b) {
                                      return a.startTime < b.startTime;
                                  });
        d->segments.insert(it, segment);
    }
    endInsertRows();
    emit countChanged();
}

} // namespace Murmur