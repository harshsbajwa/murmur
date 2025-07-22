#pragma once

#include <QtCore/QAbstractListModel>
#include <QtCore/QDateTime>
#include <QtCore/QTimer>
#include <QtQml/QQmlEngine>
#include <memory>
#include <vector>

#include "../../core/common/Expected.hpp"
#include "../../core/transcription/TranscriptionTypes.hpp"

namespace Murmur {

class WhisperEngine;

/**
 * @brief Qt model for managing transcription segments in the UI
 * 
 * This model provides a QML-compatible interface for displaying and
 * interacting with transcription data, including segments, timestamps,
 * and confidence scores.
 */
class TranscriptionModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool isEmpty READ isEmpty NOTIFY countChanged)
    Q_PROPERTY(QString currentLanguage READ currentLanguage NOTIFY languageChanged)
    Q_PROPERTY(float averageConfidence READ averageConfidence NOTIFY confidenceChanged)
    Q_PROPERTY(qint64 totalDuration READ totalDuration NOTIFY durationChanged)
    Q_PROPERTY(bool isLoaded READ isLoaded NOTIFY loadedChanged)

public:
    enum TranscriptionRoles {
        IdRole = Qt::UserRole + 1,
        StartTimeRole,
        EndTimeRole,
        DurationRole,
        TextRole,
        ConfidenceRole,
        LanguageRole,
        IsWordLevelRole,
        FormattedTimeRole,
        FormattedDurationRole,
        ConfidencePercentRole,
        HasWordsRole,
        WordCountRole,
        MetadataRole
    };
    Q_ENUM(TranscriptionRoles)

    explicit TranscriptionModel(QObject* parent = nullptr);
    ~TranscriptionModel() override;

    // QAbstractListModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool removeRows(int row, int count, const QModelIndex& parent = QModelIndex()) override;

    // Properties
    int count() const;
    bool isEmpty() const;
    QString currentLanguage() const;
    float averageConfidence() const;
    qint64 totalDuration() const;
    bool isLoaded() const;

    // Data management
    Q_INVOKABLE void addSegment(const TranscriptionSegment& segment);
    Q_INVOKABLE void removeSegment(qint64 segmentId);
    Q_INVOKABLE void updateSegment(qint64 segmentId, const TranscriptionSegment& segment);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void loadFromFile(const QString& filePath);
    Q_INVOKABLE void saveToFile(const QString& filePath);

    // Search and navigation
    Q_INVOKABLE int findSegmentByTime(qint64 timeMs) const;
    Q_INVOKABLE QVariantList search(const QString& text, bool caseSensitive = false) const;
    Q_INVOKABLE QString getTextInRange(qint64 startTimeMs, qint64 endTimeMs) const;

    // Export functionality
    Q_INVOKABLE QString exportAsPlainText() const;
    Q_INVOKABLE QString exportAsSRT() const;
    Q_INVOKABLE QString exportAsVTT() const;
    Q_INVOKABLE QString exportAsJSON() const;

    // Utility methods
    Q_INVOKABLE QString formatTime(qint64 timeMs) const;
    Q_INVOKABLE QString formatDuration(qint64 durationMs) const;
    Q_INVOKABLE float getConfidencePercentage(float confidence) const;

    // Advanced features
    Q_INVOKABLE void mergeSegments(const QList<int>& indices);
    Q_INVOKABLE void splitSegment(int index, qint64 splitTimeMs);
    Q_INVOKABLE void adjustTiming(int index, qint64 startOffset, qint64 endOffset);

    // Integration with core transcription system
    void setWhisperEngine(WhisperEngine* engine);
    void loadFromTranscription(const std::vector<TranscriptionSegment>& segments);
    std::vector<TranscriptionSegment> getSegments() const;

signals:
    void countChanged();
    void languageChanged();
    void confidenceChanged();
    void durationChanged();
    void loadedChanged();
    void segmentAdded(qint64 segmentId);
    void segmentRemoved(qint64 segmentId);
    void segmentUpdated(qint64 segmentId);
    void transcriptionLoaded(const QString& source);
    void transcriptionSaved(const QString& destination);
    void errorOccurred(const QString& error);

private slots:
    void onTranscriptionUpdated();
    void updateStatistics();

private:
    class TranscriptionModelPrivate;
    std::unique_ptr<TranscriptionModelPrivate> d;

    void invalidateStatistics();
    void calculateStatistics();
    bool validateSegment(const TranscriptionSegment& segment) const;
    QString generateId() const;
    void emitDataChanged(int index);
    void insertSegmentSorted(const TranscriptionSegment& segment);
};

} // namespace Murmur