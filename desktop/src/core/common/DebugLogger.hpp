#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QDateTime>
#include <QtCore/QTimer>
#include <QtCore/QMutex>
#include <QtCore/QThread>
#include <QtCore/QLoggingCategory>
#include <QtCore/QStandardPaths>
#include <QJsonObject>
#include <QTextStream>
#include <memory>

Q_DECLARE_LOGGING_CATEGORY(murmurCore)
Q_DECLARE_LOGGING_CATEGORY(murmurMedia)
Q_DECLARE_LOGGING_CATEGORY(murmurTorrent)
Q_DECLARE_LOGGING_CATEGORY(murmurTranscription)
Q_DECLARE_LOGGING_CATEGORY(murmurStorage)
Q_DECLARE_LOGGING_CATEGORY(murmurSecurity)
Q_DECLARE_LOGGING_CATEGORY(murmurNetwork)
Q_DECLARE_LOGGING_CATEGORY(murmurPerformance)
Q_DECLARE_LOGGING_CATEGORY(murmurError)
Q_DECLARE_LOGGING_CATEGORY(murmurDebug)

namespace Murmur {

enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Critical = 5,
    Fatal = 6
};

enum class LogOutput {
    Console = 0x01,
    File = 0x02,
    Network = 0x04,
    SystemLog = 0x08,
    Memory = 0x10
};
Q_DECLARE_FLAGS(LogOutputs, LogOutput)

struct LogEntry {
    QDateTime timestamp;
    LogLevel level;
    QString category;
    QString component;
    QString function;
    QString file;
    int line;
    qint64 threadId;
    QString message;
    QJsonObject metadata;
    
    QString toString() const;
    QJsonObject toJson() const;
};

struct PerformanceMetric {
    QString operation;
    QString component;
    QDateTime startTime;
    QDateTime endTime;
    qint64 durationMs;
    qint64 memoryUsedBytes;
    QJsonObject additionalData;
};

/**
 * @brief Comprehensive debugging and logging system
 * 
 * Provides advanced logging capabilities including performance tracking,
 * memory monitoring, thread-safe logging, and real-time debugging features.
 */
class DebugLogger : public QObject {
    Q_OBJECT

public:
    static DebugLogger* instance();
    
    // Configuration
    void setLogLevel(LogLevel level);
    void setLogOutputs(LogOutputs outputs);
    void setLogDirectory(const QString& directory);
    void setMaxLogFileSize(qint64 maxSizeBytes);
    void setMaxLogFiles(int maxFiles);
    void setLogRotationEnabled(bool enabled);
    
    // Component-specific logging
    void enableComponentLogging(const QString& component, bool enabled = true);
    void setComponentLogLevel(const QString& component, LogLevel level);
    QStringList getEnabledComponents() const;
    
    // Logging methods
    void log(LogLevel level, const QString& category, const QString& component,
             const QString& message, const QString& function = QString(),
             const QString& file = QString(), int line = 0);
    
    void logWithMetadata(LogLevel level, const QString& category, const QString& component,
                        const QString& message, const QJsonObject& metadata,
                        const QString& function = QString(), const QString& file = QString(), int line = 0);
    
    // Performance tracking
    QString startPerformanceTracking(const QString& operation, const QString& component);
    void endPerformanceTracking(const QString& trackingId, const QJsonObject& additionalData = QJsonObject{});
    QList<PerformanceMetric> getPerformanceMetrics(const QString& component = QString()) const;
    void clearPerformanceMetrics();
    
    // Memory tracking
    void recordMemoryUsage(const QString& component, qint64 bytesUsed, const QString& context = QString());
    QJsonObject getMemoryStatistics() const;
    void startMemoryMonitoring(int intervalMs = 5000);
    void stopMemoryMonitoring();
    
    // Thread monitoring
    void registerThread(const QString& threadName);
    void unregisterThread();
    QStringList getActiveThreads() const;
    QJsonObject getThreadStatistics() const;
    
    // Error tracking and crash reporting
    void reportError(const QString& component, const QString& error, 
                    const QJsonObject& context = QJsonObject{});
    void reportCrash(const QString& component, const QString& crashInfo,
                    const QStringList& stackTrace = QStringList{});
    QList<LogEntry> getErrorHistory(int maxEntries = 100) const;
    
    // Real-time debugging
    void enableRealTimeDebugging(bool enabled);
    void addDebugWatch(const QString& watchId, const QString& expression);
    void removeDebugWatch(const QString& watchId);
    void updateDebugWatch(const QString& watchId, const QVariant& value);
    
    // Log filtering and search
    QList<LogEntry> filterLogs(LogLevel minLevel = LogLevel::Trace,
                              const QString& component = QString(),
                              const QDateTime& since = QDateTime{},
                              const QString& searchText = QString()) const;
    
    QList<LogEntry> searchLogs(const QString& pattern, bool useRegex = false) const;
    
    // Export and analysis
    QString exportLogs(const QString& format = "json", 
                      const QDateTime& since = QDateTime{},
                      const QDateTime& until = QDateTime{}) const;
    
    QJsonObject generateDiagnosticReport() const;
    QString generatePerformanceReport() const;
    
    // System information
    void logSystemInformation();
    void logApplicationStartup();
    void logApplicationShutdown();
    
    // Network logging (for remote debugging)
    void enableNetworkLogging(const QString& serverUrl, int port = 8080);
    void disableNetworkLogging();

public slots:
    void flush();
    void rotateLogs();
    void clearLogs();

signals:
    void logEntryAdded(const LogEntry& entry);
    void errorReported(const QString& component, const QString& error);
    void performanceMetricRecorded(const PerformanceMetric& metric);
    void memoryUsageChanged(const QString& component, qint64 bytesUsed);
    void logFileRotated(const QString& oldFile, const QString& newFile);

private slots:
    void onMemoryMonitorTimer();
    void onLogRotationTimer();

private:
    explicit DebugLogger(QObject* parent = nullptr);
    ~DebugLogger();
    
    // Internal logging
    void writeToFile(const LogEntry& entry);
    void writeToConsole(const LogEntry& entry);
    void writeToNetwork(const LogEntry& entry);
    void writeToSystemLog(const LogEntry& entry);
    void storeInMemory(const LogEntry& entry);
    
    // File management
    void rotateLogFile();
    QString getCurrentLogFilePath() const;
    void cleanupOldLogFiles();
    
    // Utilities
    QString formatLogEntry(const LogEntry& entry, const QString& format = "text") const;
    QString getCallingFunction() const;
    QJsonObject getCurrentSystemState() const;
    qint64 getCurrentMemoryUsage() const;
    
    // Thread safety
    mutable QMutex logMutex_;
    mutable QMutex performanceMutex_;
    mutable QMutex memoryMutex_;
    
    // Configuration
    LogLevel currentLogLevel_ = LogLevel::Info;
    LogOutputs currentOutputs_ = LogOutput::Console | LogOutput::File;
    QString logDirectory_;
    qint64 maxLogFileSize_ = 10 * 1024 * 1024; // 10MB
    int maxLogFiles_ = 10;
    bool logRotationEnabled_ = true;
    
    // Component settings
    QHash<QString, bool> componentEnabled_;
    QHash<QString, LogLevel> componentLogLevels_;
    
    // Storage
    QList<LogEntry> memoryLog_;
    int maxMemoryEntries_ = 1000;
    QHash<QString, PerformanceMetric> activePerformanceTracking_;
    QList<PerformanceMetric> performanceHistory_;
    QHash<QString, qint64> componentMemoryUsage_;
    QHash<qint64, QString> threadNames_;
    
    // Files
    QString currentLogFile_;
    std::unique_ptr<QTextStream> logFileStream_;
    
    // Monitoring
    QTimer* memoryMonitorTimer_;
    QTimer* logRotationTimer_;
    bool realTimeDebuggingEnabled_ = false;
    QHash<QString, QString> debugWatches_;
    
    // Network logging
    bool networkLoggingEnabled_ = false;
    QString networkServerUrl_;
    int networkPort_ = 8080;
    QNetworkAccessManager* networkLogger_ = nullptr;
    QQueue<QByteArray> networkQueue_;
    static const int MAX_NETWORK_QUEUE_SIZE = 1000;
    int networkErrorCount_ = 0;
    
    // Helper methods
    void processNetworkQueue();
    QJsonObject getSystemMemoryInfo() const;
    
    static DebugLogger* instance_;
    static QMutex instanceMutex_;
};

/**
 * @brief RAII performance tracking helper
 */
class PerformanceTracker {
public:
    explicit PerformanceTracker(const QString& operation, const QString& component);
    ~PerformanceTracker();
    
    void addMetadata(const QString& key, const QVariant& value);
    void addMetadata(const QJsonObject& metadata);

private:
    QString trackingId_;
    QJsonObject metadata_;
};

/**
 * @brief Memory usage tracker for specific scopes
 */
class MemoryTracker {
public:
    explicit MemoryTracker(const QString& component, const QString& context = QString());
    ~MemoryTracker();
    
    void recordCheckpoint(const QString& checkpoint);
    qint64 getCurrentUsage() const;

private:
    QString component_;
    QString context_;
    qint64 initialMemory_;
    QHash<QString, qint64> checkpoints_;
};

/**
 * @brief Debug scope for automatic context logging
 */
class DebugScope {
public:
    explicit DebugScope(const QString& function, const QString& component,
                       const QJsonObject& context = QJsonObject{});
    ~DebugScope();
    
    void log(LogLevel level, const QString& message);
    void addContext(const QString& key, const QVariant& value);

private:
    QString function_;
    QString component_;
    QJsonObject context_;
    QDateTime startTime_;
};

// Convenience macros for logging with automatic function/file/line info
#define LOG_TRACE(category, component, message) \
    DebugLogger::instance()->log(LogLevel::Trace, category, component, message, Q_FUNC_INFO, __FILE__, __LINE__)

#define LOG_DEBUG(category, component, message) \
    DebugLogger::instance()->log(LogLevel::Debug, category, component, message, Q_FUNC_INFO, __FILE__, __LINE__)

#define LOG_INFO(category, component, message) \
    DebugLogger::instance()->log(LogLevel::Info, category, component, message, Q_FUNC_INFO, __FILE__, __LINE__)

#define LOG_WARNING(category, component, message) \
    DebugLogger::instance()->log(LogLevel::Warning, category, component, message, Q_FUNC_INFO, __FILE__, __LINE__)

#define LOG_ERROR(category, component, message) \
    DebugLogger::instance()->log(LogLevel::Error, category, component, message, Q_FUNC_INFO, __FILE__, __LINE__)

#define LOG_CRITICAL(category, component, message) \
    DebugLogger::instance()->log(LogLevel::Critical, category, component, message, Q_FUNC_INFO, __FILE__, __LINE__)

#define LOG_WITH_METADATA(level, category, component, message, metadata) \
    DebugLogger::instance()->logWithMetadata(level, category, component, message, metadata, Q_FUNC_INFO, __FILE__, __LINE__)

// Performance tracking macros
#define TRACK_PERFORMANCE(operation, component) \
    PerformanceTracker _perfTracker(operation, component)

#define TRACK_MEMORY(component, context) \
    MemoryTracker _memTracker(component, context)

#define DEBUG_SCOPE(function, component) \
    DebugScope _debugScope(function, component)

#define DEBUG_SCOPE_WITH_CONTEXT(function, component, context) \
    DebugScope _debugScope(function, component, context)

// Category-specific logging macros
#define LOG_CORE_TRACE(message) LOG_TRACE("murmur.core", "Core", message)
#define LOG_CORE_DEBUG(message) LOG_DEBUG("murmur.core", "Core", message)
#define LOG_CORE_INFO(message) LOG_INFO("murmur.core", "Core", message)
#define LOG_CORE_WARNING(message) LOG_WARNING("murmur.core", "Core", message)
#define LOG_CORE_ERROR(message) LOG_ERROR("murmur.core", "Core", message)

#define LOG_MEDIA_TRACE(message) LOG_TRACE("murmur.media", "Media", message)
#define LOG_MEDIA_DEBUG(message) LOG_DEBUG("murmur.media", "Media", message)
#define LOG_MEDIA_INFO(message) LOG_INFO("murmur.media", "Media", message)
#define LOG_MEDIA_WARNING(message) LOG_WARNING("murmur.media", "Media", message)
#define LOG_MEDIA_ERROR(message) LOG_ERROR("murmur.media", "Media", message)

#define LOG_TORRENT_TRACE(message) LOG_TRACE("murmur.torrent", "Torrent", message)
#define LOG_TORRENT_DEBUG(message) LOG_DEBUG("murmur.torrent", "Torrent", message)
#define LOG_TORRENT_INFO(message) LOG_INFO("murmur.torrent", "Torrent", message)
#define LOG_TORRENT_WARNING(message) LOG_WARNING("murmur.torrent", "Torrent", message)
#define LOG_TORRENT_ERROR(message) LOG_ERROR("murmur.torrent", "Torrent", message)

#define LOG_TRANSCRIPTION_TRACE(message) LOG_TRACE("murmur.transcription", "Transcription", message)
#define LOG_TRANSCRIPTION_DEBUG(message) LOG_DEBUG("murmur.transcription", "Transcription", message)
#define LOG_TRANSCRIPTION_INFO(message) LOG_INFO("murmur.transcription", "Transcription", message)
#define LOG_TRANSCRIPTION_WARNING(message) LOG_WARNING("murmur.transcription", "Transcription", message)
#define LOG_TRANSCRIPTION_ERROR(message) LOG_ERROR("murmur.transcription", "Transcription", message)

#define LOG_STORAGE_TRACE(message) LOG_TRACE("murmur.storage", "Storage", message)
#define LOG_STORAGE_DEBUG(message) LOG_DEBUG("murmur.storage", "Storage", message)
#define LOG_STORAGE_INFO(message) LOG_INFO("murmur.storage", "Storage", message)
#define LOG_STORAGE_WARNING(message) LOG_WARNING("murmur.storage", "Storage", message)
#define LOG_STORAGE_ERROR(message) LOG_ERROR("murmur.storage", "Storage", message)

Q_DECLARE_OPERATORS_FOR_FLAGS(LogOutputs)

} // namespace Murmur