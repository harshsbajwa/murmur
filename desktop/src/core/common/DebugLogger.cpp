#include "DebugLogger.hpp"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QProcess>
#include <QtCore/QRandomGenerator>
#include <QtCore/QFileInfo>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QRegularExpression>
#include <QTextStream>
#include <QSysInfo>
#include <QTimer>
#include <QQueue>
#include <QThread>

// Platform-specific includes
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
#include <sys/resource.h>
#include <syslog.h>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#include <mach/vm_statistics.h>
#include <sys/sysctl.h>
#endif
#endif

// Define logging categories
Q_LOGGING_CATEGORY(murmurCore, "murmur.core")
Q_LOGGING_CATEGORY(murmurMedia, "murmur.media")
Q_LOGGING_CATEGORY(murmurTorrent, "murmur.torrent")
Q_LOGGING_CATEGORY(murmurTranscription, "murmur.transcription")
Q_LOGGING_CATEGORY(murmurStorage, "murmur.storage")
Q_LOGGING_CATEGORY(murmurSecurity, "murmur.security")
Q_LOGGING_CATEGORY(murmurNetwork, "murmur.network")
Q_LOGGING_CATEGORY(murmurPerformance, "murmur.performance")
Q_LOGGING_CATEGORY(murmurError, "murmur.error")
Q_LOGGING_CATEGORY(murmurDebug, "murmur.debug")

namespace Murmur {

// Static members
DebugLogger* DebugLogger::instance_ = nullptr;
QMutex DebugLogger::instanceMutex_;

QString LogEntry::toString() const {
    return QString("[%1] [%2] [%3:%4] %5 - %6")
           .arg(timestamp.toString(Qt::ISODateWithMs))
           .arg(QMetaEnum::fromType<LogLevel>().valueToKey(static_cast<int>(level)))
           .arg(category)
           .arg(component)
           .arg(threadId)
           .arg(message);
}

QJsonObject LogEntry::toJson() const {
    QJsonObject obj;
    obj["timestamp"] = timestamp.toString(Qt::ISODateWithMs);
    obj["level"] = static_cast<int>(level);
    obj["category"] = category;
    obj["component"] = component;
    obj["function"] = function;
    obj["file"] = file;
    obj["line"] = line;
    obj["threadId"] = threadId;
    obj["message"] = message;
    obj["metadata"] = metadata;
    return obj;
}

DebugLogger* DebugLogger::instance() {
    QMutexLocker locker(&instanceMutex_);
    if (!instance_) {
        instance_ = new DebugLogger();
        
        // Set up default log directory
        QString defaultLogDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/logs";
        instance_->setLogDirectory(defaultLogDir);
        
        // Enable all components by default
        instance_->enableComponentLogging("Core");
        instance_->enableComponentLogging("Media");
        instance_->enableComponentLogging("Torrent");
        instance_->enableComponentLogging("Transcription");
        instance_->enableComponentLogging("Storage");
        instance_->enableComponentLogging("Security");
        instance_->enableComponentLogging("Network");
        instance_->enableComponentLogging("Performance");
        instance_->enableComponentLogging("Error");
        instance_->enableComponentLogging("Debug");
        
        instance_->logApplicationStartup();
    }
    return instance_;
}

DebugLogger::DebugLogger(QObject* parent)
    : QObject(parent)
    , memoryMonitorTimer_(new QTimer(this))
    , logRotationTimer_(new QTimer(this)) {
    
    // Setup timers
    connect(memoryMonitorTimer_, &QTimer::timeout, this, &DebugLogger::onMemoryMonitorTimer);
    connect(logRotationTimer_, &QTimer::timeout, this, &DebugLogger::onLogRotationTimer);
    
    // Start log rotation timer (check every hour)
    logRotationTimer_->start(3600000);
    
    // Register main thread
    registerThread("MainThread");
}

DebugLogger::~DebugLogger() {
    logApplicationShutdown();
    flush();
    
    if (logFileStream_) {
        logFileStream_.reset();
    }
}

void DebugLogger::setLogLevel(LogLevel level) {
    QMutexLocker locker(&logMutex_);
    currentLogLevel_ = level;
    
    LOG_DEBUG("murmur.debug", "DebugLogger", 
              QString("Log level changed to: %1").arg(static_cast<int>(level)));
}

void DebugLogger::setLogOutputs(LogOutputs outputs) {
    QMutexLocker locker(&logMutex_);
    currentOutputs_ = outputs;
    
    LOG_DEBUG("murmur.debug", "DebugLogger", 
              QString("Log outputs changed to: 0x%1").arg(static_cast<int>(outputs), 0, 16));
}

void DebugLogger::setLogDirectory(const QString& directory) {
    QMutexLocker locker(&logMutex_);
    
    QDir dir;
    if (!dir.exists(directory)) {
        if (!dir.mkpath(directory)) {
            qWarning() << "Failed to create log directory:" << directory;
            return;
        }
    }
    
    logDirectory_ = directory;
    currentLogFile_ = getCurrentLogFilePath();
    
    // Close existing stream
    if (logFileStream_) {
        logFileStream_.reset();
    }
    
    LOG_DEBUG("murmur.debug", "DebugLogger", 
              QString("Log directory changed to: %1").arg(directory));
}

void DebugLogger::setMaxLogFileSize(qint64 maxSizeBytes) {
    QMutexLocker locker(&logMutex_);
    maxLogFileSize_ = maxSizeBytes;
    
    LOG_DEBUG("murmur.debug", "DebugLogger", 
              QString("Max log file size set to: %1 bytes").arg(maxSizeBytes));
}

void DebugLogger::setMaxLogFiles(int maxFiles) {
    QMutexLocker locker(&logMutex_);
    maxLogFiles_ = maxFiles;
    
    LOG_DEBUG("murmur.debug", "DebugLogger", 
              QString("Max log files set to: %1").arg(maxFiles));
}

void DebugLogger::setLogRotationEnabled(bool enabled) {
    QMutexLocker locker(&logMutex_);
    logRotationEnabled_ = enabled;
    
    if (enabled) {
        logRotationTimer_->start(3600000); // Check every hour
    } else {
        logRotationTimer_->stop();
    }
    
    LOG_DEBUG("murmur.debug", "DebugLogger", 
              QString("Log rotation %1").arg(enabled ? "enabled" : "disabled"));
}

void DebugLogger::enableComponentLogging(const QString& component, bool enabled) {
    QMutexLocker locker(&logMutex_);
    componentEnabled_[component] = enabled;
    
    LOG_DEBUG("murmur.debug", "DebugLogger", 
              QString("Component '%1' logging %2").arg(component).arg(enabled ? "enabled" : "disabled"));
}

void DebugLogger::setComponentLogLevel(const QString& component, LogLevel level) {
    QMutexLocker locker(&logMutex_);
    componentLogLevels_[component] = level;
    
    LOG_DEBUG("murmur.debug", "DebugLogger", 
              QString("Component '%1' log level set to %2").arg(component).arg(static_cast<int>(level)));
}

QStringList DebugLogger::getEnabledComponents() const {
    QMutexLocker locker(&logMutex_);
    QStringList enabled;
    
    for (auto it = componentEnabled_.begin(); it != componentEnabled_.end(); ++it) {
        if (it.value()) {
            enabled.append(it.key());
        }
    }
    
    return enabled;
}

void DebugLogger::log(LogLevel level, const QString& category, const QString& component,
                     const QString& message, const QString& function,
                     const QString& file, int line) {
    
    // Quick level check without lock
    if (level < currentLogLevel_) {
        return;
    }
    
    QMutexLocker locker(&logMutex_);
    
    // Check component enablement
    if (!componentEnabled_.value(component, true)) {
        return;
    }
    
    // Check component-specific log level
    LogLevel componentLevel = componentLogLevels_.value(component, currentLogLevel_);
    if (level < componentLevel) {
        return;
    }
    
    // Create log entry
    LogEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.level = level;
    entry.category = category;
    entry.component = component;
    entry.function = function;
    entry.file = file;
    entry.line = line;
    entry.threadId = reinterpret_cast<qint64>(QThread::currentThreadId());
    entry.message = message;
    
    // Write to configured outputs
    if (currentOutputs_ & LogOutput::Console) {
        writeToConsole(entry);
    }
    
    if (currentOutputs_ & LogOutput::File) {
        writeToFile(entry);
    }
    
    if (currentOutputs_ & LogOutput::Memory) {
        storeInMemory(entry);
    }
    
    if (currentOutputs_ & LogOutput::Network && networkLoggingEnabled_) {
        writeToNetwork(entry);
    }
    
    if (currentOutputs_ & LogOutput::SystemLog) {
        writeToSystemLog(entry);
    }
    
    // Emit signal for real-time monitoring
    emit logEntryAdded(entry);
    
    // Special handling for errors
    if (level >= LogLevel::Error) {
        QJsonObject errorContext;
        errorContext["function"] = function;
        errorContext["file"] = file;
        errorContext["line"] = line;
        emit errorReported(component, message);
    }
}

void DebugLogger::logWithMetadata(LogLevel level, const QString& category, const QString& component,
                                 const QString& message, const QJsonObject& metadata,
                                 const QString& function, const QString& file, int line) {
    
    // Quick level check
    if (level < currentLogLevel_) {
        return;
    }
    
    QMutexLocker locker(&logMutex_);
    
    // Check component enablement
    if (!componentEnabled_.value(component, true)) {
        return;
    }
    
    // Create enhanced log entry
    LogEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.level = level;
    entry.category = category;
    entry.component = component;
    entry.function = function;
    entry.file = file;
    entry.line = line;
    entry.threadId = reinterpret_cast<qint64>(QThread::currentThreadId());
    entry.message = message;
    entry.metadata = metadata;
    
    // Write to outputs (same as regular log)
    if (currentOutputs_ & LogOutput::Console) {
        writeToConsole(entry);
    }
    
    if (currentOutputs_ & LogOutput::File) {
        writeToFile(entry);
    }
    
    if (currentOutputs_ & LogOutput::Memory) {
        storeInMemory(entry);
    }
    
    emit logEntryAdded(entry);
}

QString DebugLogger::startPerformanceTracking(const QString& operation, const QString& component) {
    QMutexLocker locker(&performanceMutex_);
    
    QString trackingId = QString("%1_%2_%3")
                        .arg(component)
                        .arg(operation)
                        .arg(QRandomGenerator::global()->generate());
    
    PerformanceMetric metric;
    metric.operation = operation;
    metric.component = component;
    metric.startTime = QDateTime::currentDateTime();
    metric.memoryUsedBytes = getCurrentMemoryUsage();
    
    activePerformanceTracking_[trackingId] = metric;
    
    LOG_TRACE("murmur.performance", component, 
              QString("Started performance tracking: %1 (ID: %2)").arg(operation).arg(trackingId));
    
    return trackingId;
}

void DebugLogger::endPerformanceTracking(const QString& trackingId, const QJsonObject& additionalData) {
    QMutexLocker locker(&performanceMutex_);
    
    auto it = activePerformanceTracking_.find(trackingId);
    if (it == activePerformanceTracking_.end()) {
        LOG_WARNING("murmur.performance", "DebugLogger", 
                   QString("Performance tracking ID not found: %1").arg(trackingId));
        return;
    }
    
    PerformanceMetric metric = it.value();
    metric.endTime = QDateTime::currentDateTime();
    metric.durationMs = metric.startTime.msecsTo(metric.endTime);
    metric.additionalData = additionalData;
    
    // Calculate memory difference
    qint64 endMemory = getCurrentMemoryUsage();
    metric.memoryUsedBytes = endMemory - metric.memoryUsedBytes;
    
    performanceHistory_.append(metric);
    activePerformanceTracking_.erase(it);
    
    LOG_DEBUG("murmur.performance", metric.component,
              QString("Performance tracking completed: %1 took %2ms (Memory: %3 bytes)")
              .arg(metric.operation).arg(metric.durationMs).arg(metric.memoryUsedBytes));
    
    emit performanceMetricRecorded(metric);
}

QList<PerformanceMetric> DebugLogger::getPerformanceMetrics(const QString& component) const {
    QMutexLocker locker(&performanceMutex_);
    
    if (component.isEmpty()) {
        return performanceHistory_;
    }
    
    QList<PerformanceMetric> filtered;
    for (const auto& metric : performanceHistory_) {
        if (metric.component == component) {
            filtered.append(metric);
        }
    }
    
    return filtered;
}

void DebugLogger::clearPerformanceMetrics() {
    QMutexLocker locker(&performanceMutex_);
    performanceHistory_.clear();
    
    LOG_DEBUG("murmur.performance", "DebugLogger", "Performance metrics cleared");
}

void DebugLogger::recordMemoryUsage(const QString& component, qint64 bytesUsed, const QString& context) {
    QMutexLocker locker(&memoryMutex_);
    
    componentMemoryUsage_[component] = bytesUsed;
    
    LOG_TRACE("murmur.performance", component,
              QString("Memory usage recorded: %1 bytes%2")
              .arg(bytesUsed)
              .arg(context.isEmpty() ? "" : QString(" (%1)").arg(context)));
    
    emit memoryUsageChanged(component, bytesUsed);
}

QJsonObject DebugLogger::getMemoryStatistics() const {
    QMutexLocker locker(&memoryMutex_);
    
    QJsonObject stats;
    QJsonObject componentUsage;
    
    qint64 totalUsage = 0;
    for (auto it = componentMemoryUsage_.begin(); it != componentMemoryUsage_.end(); ++it) {
        componentUsage[it.key()] = it.value();
        totalUsage += it.value();
    }
    
    stats["totalMemoryUsage"] = totalUsage;
    stats["componentUsage"] = componentUsage;
    stats["systemMemoryUsage"] = getCurrentMemoryUsage();
    stats["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    
    return stats;
}

void DebugLogger::startMemoryMonitoring(int intervalMs) {
    memoryMonitorTimer_->start(intervalMs);
    
    LOG_DEBUG("murmur.performance", "DebugLogger", 
              QString("Memory monitoring started with %1ms interval").arg(intervalMs));
}

void DebugLogger::stopMemoryMonitoring() {
    memoryMonitorTimer_->stop();
    
    LOG_DEBUG("murmur.performance", "DebugLogger", "Memory monitoring stopped");
}

void DebugLogger::registerThread(const QString& threadName) {
    QMutexLocker locker(&logMutex_);
    
    qint64 threadId = reinterpret_cast<qint64>(QThread::currentThreadId());
    threadNames_[threadId] = threadName;
    
    LOG_DEBUG("murmur.debug", "DebugLogger", 
              QString("Thread registered: %1 (ID: %2)").arg(threadName).arg(threadId));
}

void DebugLogger::unregisterThread() {
    QMutexLocker locker(&logMutex_);
    
    qint64 threadId = reinterpret_cast<qint64>(QThread::currentThreadId());
    QString threadName = threadNames_.value(threadId, "Unknown");
    threadNames_.remove(threadId);
    
    LOG_DEBUG("murmur.debug", "DebugLogger", 
              QString("Thread unregistered: %1 (ID: %2)").arg(threadName).arg(threadId));
}

QStringList DebugLogger::getActiveThreads() const {
    QMutexLocker locker(&logMutex_);
    return threadNames_.values();
}

QJsonObject DebugLogger::getThreadStatistics() const {
    QMutexLocker locker(&logMutex_);
    
    QJsonObject stats;
    QJsonObject threads;
    
    for (auto it = threadNames_.begin(); it != threadNames_.end(); ++it) {
        threads[QString::number(it.key())] = it.value();
    }
    
    stats["activeThreads"] = threads;
    stats["threadCount"] = threadNames_.size();
    stats["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    
    return stats;
}

void DebugLogger::reportError(const QString& component, const QString& error, const QJsonObject& context) {
    LOG_ERROR("murmur.error", component, error);
    
    if (!context.isEmpty()) {
        LOG_WITH_METADATA(LogLevel::Error, "murmur.error", component, 
                          QString("Error context: %1").arg(error), context);
    }
    
    emit errorReported(component, error);
}

void DebugLogger::reportCrash(const QString& component, const QString& crashInfo, const QStringList& stackTrace) {
    QJsonObject crashContext;
    crashContext["crashInfo"] = crashInfo;
    crashContext["stackTrace"] = QJsonArray::fromStringList(stackTrace);
    crashContext["systemState"] = getCurrentSystemState();
    
    LOG_WITH_METADATA(LogLevel::Fatal, "murmur.error", component, 
                      QString("Application crash: %1").arg(crashInfo), crashContext);
    
    // Force immediate flush for crash logs
    flush();
}

QList<LogEntry> DebugLogger::getErrorHistory(int maxEntries) const {
    QMutexLocker locker(&logMutex_);
    
    QList<LogEntry> errors;
    
    for (int i = memoryLog_.size() - 1; i >= 0 && errors.size() < maxEntries; --i) {
        const LogEntry& entry = memoryLog_[i];
        if (entry.level >= LogLevel::Error) {
            errors.prepend(entry);
        }
    }
    
    return errors;
}

void DebugLogger::enableRealTimeDebugging(bool enabled) {
    QMutexLocker locker(&logMutex_);
    realTimeDebuggingEnabled_ = enabled;
    
    LOG_DEBUG("murmur.debug", "DebugLogger", 
              QString("Real-time debugging %1").arg(enabled ? "enabled" : "disabled"));
}

void DebugLogger::addDebugWatch(const QString& watchId, const QString& expression) {
    QMutexLocker locker(&logMutex_);
    debugWatches_[watchId] = expression;
    
    LOG_DEBUG("murmur.debug", "DebugLogger", 
              QString("Debug watch added: %1 -> %2").arg(watchId).arg(expression));
}

void DebugLogger::removeDebugWatch(const QString& watchId) {
    QMutexLocker locker(&logMutex_);
    debugWatches_.remove(watchId);
    
    LOG_DEBUG("murmur.debug", "DebugLogger", 
              QString("Debug watch removed: %1").arg(watchId));
}

void DebugLogger::updateDebugWatch(const QString& watchId, const QVariant& value) {
    if (!realTimeDebuggingEnabled_) {
        return;
    }
    
    LOG_TRACE("murmur.debug", "DebugLogger", 
              QString("Watch '%1' = %2").arg(watchId).arg(value.toString()));
}

QList<LogEntry> DebugLogger::filterLogs(LogLevel minLevel, const QString& component,
                                       const QDateTime& since, const QString& searchText) const {
    QMutexLocker locker(&logMutex_);
    
    QList<LogEntry> filtered;
    
    for (const LogEntry& entry : memoryLog_) {
        // Level filter
        if (entry.level < minLevel) {
            continue;
        }
        
        // Component filter
        if (!component.isEmpty() && entry.component != component) {
            continue;
        }
        
        // Time filter
        if (since.isValid() && entry.timestamp < since) {
            continue;
        }
        
        // Text search
        if (!searchText.isEmpty() && !entry.message.contains(searchText, Qt::CaseInsensitive)) {
            continue;
        }
        
        filtered.append(entry);
    }
    
    return filtered;
}

QList<LogEntry> DebugLogger::searchLogs(const QString& pattern, bool useRegex) const {
    QMutexLocker locker(&logMutex_);
    
    QList<LogEntry> results;
    
    if (useRegex) {
        QRegularExpression regex(pattern);
        if (!regex.isValid()) {
            return results;
        }
        
        for (const LogEntry& entry : memoryLog_) {
            if (regex.match(entry.message).hasMatch()) {
                results.append(entry);
            }
        }
    } else {
        for (const LogEntry& entry : memoryLog_) {
            if (entry.message.contains(pattern, Qt::CaseInsensitive)) {
                results.append(entry);
            }
        }
    }
    
    return results;
}

QString DebugLogger::exportLogs(const QString& format, const QDateTime& since, const QDateTime& until) const {
    QMutexLocker locker(&logMutex_);
    
    QList<LogEntry> filtered;
    
    for (const LogEntry& entry : memoryLog_) {
        if (since.isValid() && entry.timestamp < since) {
            continue;
        }
        
        if (until.isValid() && entry.timestamp > until) {
            continue;
        }
        
        filtered.append(entry);
    }
    
    if (format.toLower() == "json") {
        QJsonArray logArray;
        for (const LogEntry& entry : filtered) {
            logArray.append(entry.toJson());
        }
        
        QJsonObject exportObj;
        exportObj["logs"] = logArray;
        exportObj["exportTime"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
        exportObj["entryCount"] = filtered.size();
        
        return QJsonDocument(exportObj).toJson();
    } else {
        // Default to text format
        QString result;
        for (const LogEntry& entry : filtered) {
            result += entry.toString() + "\n";
        }
        return result;
    }
}

QJsonObject DebugLogger::generateDiagnosticReport() const {
    QJsonObject report;
    
    // System information
    report["systemInfo"] = getCurrentSystemState();
    
    // Thread statistics
    report["threadStats"] = getThreadStatistics();
    
    // Memory statistics
    report["memoryStats"] = getMemoryStatistics();
    
    // Performance metrics summary
    QJsonArray perfMetrics;
    auto metrics = getPerformanceMetrics();
    for (const auto& metric : metrics) {
        QJsonObject metricObj;
        metricObj["operation"] = metric.operation;
        metricObj["component"] = metric.component;
        metricObj["durationMs"] = metric.durationMs;
        metricObj["memoryUsedBytes"] = metric.memoryUsedBytes;
        perfMetrics.append(metricObj);
    }
    report["performanceMetrics"] = perfMetrics;
    
    // Error summary
    auto errorHistory = getErrorHistory(50);
    QJsonArray errors;
    for (const auto& error : errorHistory) {
        errors.append(error.toJson());
    }
    report["recentErrors"] = errors;
    
    // Configuration
    QJsonObject config;
    config["logLevel"] = static_cast<int>(currentLogLevel_);
    config["logOutputs"] = static_cast<int>(currentOutputs_);
    config["logDirectory"] = logDirectory_;
    config["enabledComponents"] = QJsonArray::fromStringList(getEnabledComponents());
    report["configuration"] = config;
    
    report["generatedAt"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    
    return report;
}

void DebugLogger::logSystemInformation() {
    QJsonObject systemInfo = getCurrentSystemState();
    
    LOG_WITH_METADATA(LogLevel::Info, "murmur.core", "System", 
                      "System information logged", systemInfo);
}

void DebugLogger::logApplicationStartup() {
    QJsonObject startupInfo;
    startupInfo["applicationName"] = QCoreApplication::applicationName();
    startupInfo["applicationVersion"] = QCoreApplication::applicationVersion();
    startupInfo["qtVersion"] = QT_VERSION_STR;
    startupInfo["startupTime"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    
    LOG_WITH_METADATA(LogLevel::Info, "murmur.core", "Application", 
                      "Application startup", startupInfo);
}

void DebugLogger::logApplicationShutdown() {
    QJsonObject shutdownInfo;
    shutdownInfo["shutdownTime"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    shutdownInfo["totalLogEntries"] = memoryLog_.size();
    shutdownInfo["performanceMetrics"] = performanceHistory_.size();
    
    LOG_WITH_METADATA(LogLevel::Info, "murmur.core", "Application", 
                      "Application shutdown", shutdownInfo);
}

void DebugLogger::flush() {
    QMutexLocker locker(&logMutex_);
    
    if (logFileStream_) {
        logFileStream_->flush();
    }
}

void DebugLogger::rotateLogs() {
    QMutexLocker locker(&logMutex_);
    
    if (!logRotationEnabled_) {
        return;
    }
    
    rotateLogFile();
}

void DebugLogger::clearLogs() {
    QMutexLocker locker(&logMutex_);
    
    memoryLog_.clear();
    
    LOG_DEBUG("murmur.debug", "DebugLogger", "Memory logs cleared");
}

void DebugLogger::onMemoryMonitorTimer() {
    qint64 currentMemory = getCurrentMemoryUsage();
    recordMemoryUsage("System", currentMemory, "MemoryMonitor");
}

void DebugLogger::onLogRotationTimer() {
    if (logRotationEnabled_) {
        QFileInfo fileInfo(currentLogFile_);
        if (fileInfo.exists() && fileInfo.size() > maxLogFileSize_) {
            rotateLogs();
        }
    }
}

// Private helper methods
void DebugLogger::writeToFile(const LogEntry& entry) {
    if (logDirectory_.isEmpty()) {
        return;
    }
    
    if (!logFileStream_) {
        QFile* logFile = new QFile(currentLogFile_);
        if (!logFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
            delete logFile;
            return;
        }
        logFileStream_ = std::make_unique<QTextStream>(logFile);
    }
    
    *logFileStream_ << formatLogEntry(entry, "text") << "\n";
    logFileStream_->flush();
    
    // Check for rotation
    QFileInfo fileInfo(currentLogFile_);
    if (fileInfo.size() > maxLogFileSize_) {
        rotateLogFile();
    }
}

void DebugLogger::writeToConsole(const LogEntry& entry) {
    QString formatted = formatLogEntry(entry, "console");
    
    if (entry.level >= LogLevel::Warning) {
        fprintf(stderr, "%s\n", formatted.toLocal8Bit().constData());
    } else {
        printf("%s\n", formatted.toLocal8Bit().constData());
    }
    
    fflush(stdout);
    fflush(stderr);
}

void DebugLogger::writeToNetwork(const LogEntry& entry) {
    // Send log entry to network endpoint (e.g., centralized logging service)
    if (!networkLogger_) {
        return; // Network logging not configured
    }
    
    // Create JSON payload
    QJsonObject logPayload;
    logPayload["timestamp"] = entry.timestamp.toString(Qt::ISODate);
    logPayload["level"] = toString(entry.level);
    logPayload["category"] = entry.category;
    logPayload["component"] = entry.component;
    logPayload["message"] = entry.message;
    logPayload["filename"] = entry.filename;
    logPayload["line"] = entry.line;
    logPayload["function"] = entry.function;
    logPayload["thread"] = QString::number(reinterpret_cast<quintptr>(entry.thread)));
    
    if (!entry.metadata.isEmpty()) {
        logPayload["metadata"] = entry.metadata;
    }
    
    // Add host information
    logPayload["hostname"] = QSysInfo::machineHostName();
    logPayload["application"] = QCoreApplication::applicationName();
    logPayload["version"] = QCoreApplication::applicationVersion();
    
    QJsonDocument doc(logPayload);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    
    // Queue for network transmission
    if (networkQueue_.size() < MAX_NETWORK_QUEUE_SIZE) {
        networkQueue_.enqueue(data);
        QTimer::singleShot(0, this, &DebugLogger::processNetworkQueue);
    }
}

void DebugLogger::writeToSystemLog(const LogEntry& entry) {
    // Write to platform-specific system log
    QString message = formatLogEntry(entry);
    
#ifdef Q_OS_WIN
    // Windows Event Log
    HANDLE hEventLog = RegisterEventSourceA(nullptr, "Murmur");
    if (hEventLog) {
        WORD eventType = EVENTLOG_INFORMATION_TYPE;
        switch (entry.level) {
            case LogLevel::Fatal:
            case LogLevel::Critical:
                eventType = EVENTLOG_ERROR_TYPE;
                break;
            case LogLevel::Warning:
                eventType = EVENTLOG_WARNING_TYPE;
                break;
            default:
                eventType = EVENTLOG_INFORMATION_TYPE;
                break;
        }
        
        LPCSTR messageStr = message.toLocal8Bit().constData();
        ReportEventA(hEventLog, eventType, 0, 0, nullptr, 1, 0, &messageStr, nullptr);
        DeregisterEventSource(hEventLog);
    }
    
#elif defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    // Unix syslog
    int priority = LOG_INFO;
    switch (entry.level) {
        case LogLevel::Fatal:
            priority = LOG_CRIT;
            break;
        case LogLevel::Critical:
            priority = LOG_ERR;
            break;
        case LogLevel::Warning:
            priority = LOG_WARNING;
            break;
        case LogLevel::Info:
            priority = LOG_INFO;
            break;
        case LogLevel::Debug:
        case LogLevel::Trace:
            priority = LOG_DEBUG;
            break;
    }
    
    openlog("murmur", LOG_PID | LOG_CONS, LOG_USER);
    syslog(priority, "%s", message.toLocal8Bit().constData());
    closelog();
    
#else
    // Fallback to standard output for unsupported platforms
    fprintf(stderr, "SYSLOG: %s\n", message.toLocal8Bit().constData());
#endif
}

void DebugLogger::storeInMemory(const LogEntry& entry) {
    memoryLog_.append(entry);
    
    // Trim to max entries
    while (memoryLog_.size() > maxMemoryEntries_) {
        memoryLog_.removeFirst();
    }
}

void DebugLogger::rotateLogFile() {
    if (logFileStream_) {
        logFileStream_.reset();
    }
    
    QString oldFile = currentLogFile_;
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString rotatedFile = QString("%1/murmur_%2.log").arg(logDirectory_).arg(timestamp);
    
    QFile::rename(currentLogFile_, rotatedFile);
    
    currentLogFile_ = getCurrentLogFilePath();
    
    emit logFileRotated(oldFile, rotatedFile);
    
    // Cleanup old files
    cleanupOldLogFiles();
}

QString DebugLogger::getCurrentLogFilePath() const {
    return QString("%1/murmur_current.log").arg(logDirectory_);
}

void DebugLogger::cleanupOldLogFiles() {
    QDir logDir(logDirectory_);
    QStringList logFiles = logDir.entryList(QStringList() << "murmur_*.log", QDir::Files, QDir::Time);
    
    while (logFiles.size() > maxLogFiles_) {
        QString oldestFile = logFiles.takeLast();
        logDir.remove(oldestFile);
    }
}

QString DebugLogger::formatLogEntry(const LogEntry& entry, const QString& format) const {
    if (format == "json") {
        return QJsonDocument(entry.toJson()).toJson(QJsonDocument::Compact);
    } else if (format == "console") {
        // Add color codes for console output
        QString levelStr;
        switch (entry.level) {
            case LogLevel::Trace: levelStr = "\033[37mTRACE\033[0m"; break;
            case LogLevel::Debug: levelStr = "\033[36mDEBUG\033[0m"; break;
            case LogLevel::Info: levelStr = "\033[32mINFO\033[0m"; break;
            case LogLevel::Warning: levelStr = "\033[33mWARN\033[0m"; break;
            case LogLevel::Error: levelStr = "\033[31mERROR\033[0m"; break;
            case LogLevel::Critical: levelStr = "\033[35mCRIT\033[0m"; break;
            case LogLevel::Fatal: levelStr = "\033[41mFATAL\033[0m"; break;
        }
        
        QString threadName = threadNames_.value(entry.threadId, QString::number(entry.threadId));
        
        return QString("[%1] [%2] [%3:%4@%5] %6")
               .arg(entry.timestamp.toString("hh:mm:ss.zzz"))
               .arg(levelStr)
               .arg(entry.category)
               .arg(entry.component)
               .arg(threadName)
               .arg(entry.message);
    } else {
        // Default text format
        return entry.toString();
    }
}

QJsonObject DebugLogger::getCurrentSystemState() const {
    QJsonObject state;
    
    // Basic system info
    state["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    state["applicationName"] = QCoreApplication::applicationName();
    state["applicationVersion"] = QCoreApplication::applicationVersion();
    state["qtVersion"] = QT_VERSION_STR;
    
    // Platform info
#ifdef Q_OS_MACOS
    state["platform"] = "macOS";
#elif defined(Q_OS_WIN)
    state["platform"] = "Windows";
#elif defined(Q_OS_LINUX)
    state["platform"] = "Linux";
#else
    state["platform"] = "Unknown";
#endif
    
    // Memory info - comprehensive system and process information
    QJsonObject memoryInfo;
    memoryInfo["processMemoryMB"] = static_cast<double>(getCurrentMemoryUsage()) / (1024.0 * 1024.0);
    memoryInfo["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    // Add system memory info if available
    QJsonObject systemMemory = getSystemMemoryInfo();
    if (!systemMemory.isEmpty()) {
        memoryInfo["system"] = systemMemory;
    }
    
    state["memoryInfo"] = memoryInfo;
    
    return state;
}

qint64 DebugLogger::getCurrentMemoryUsage() const {
    // Get current process memory usage using platform-specific APIs
#ifdef Q_OS_WIN
    // Windows implementation using GetProcessMemoryInfo
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<qint64>(pmc.WorkingSetSize);
    }
#elif defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    // Unix-like systems: read from /proc/self/status or use getrusage
    QFile statusFile("/proc/self/status");
    if (statusFile.open(QIODevice::ReadOnly)) {
        QTextStream stream(&statusFile);
        QString line;
        while (stream.readLineInto(&line)) {
            if (line.startsWith("VmRSS:")) {
                // Extract memory value in kB and convert to bytes
                QStringList parts = line.split(QRegExp("\\s+"));
                if (parts.size() >= 2) {
                    bool ok;
                    qint64 memoryKB = parts[1].toLongLong(&ok);
                    if (ok) {
                        return memoryKB * 1024; // Convert kB to bytes
                    }
                }
                break;
            }
        }
    }
    
    // Fallback: use rusage for Unix systems
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef Q_OS_MACOS
        return static_cast<qint64>(usage.ru_maxrss); // macOS returns bytes
#else
        return static_cast<qint64>(usage.ru_maxrss * 1024); // Linux returns kB
#endif
    }
#endif
    
    // Fallback: return 0 if unable to determine memory usage
    return 0;
}

// RAII Helper implementations
PerformanceTracker::PerformanceTracker(const QString& operation, const QString& component) {
    trackingId_ = DebugLogger::instance()->startPerformanceTracking(operation, component);
}

PerformanceTracker::~PerformanceTracker() {
    DebugLogger::instance()->endPerformanceTracking(trackingId_, metadata_);
}

void PerformanceTracker::addMetadata(const QString& key, const QVariant& value) {
    metadata_[key] = QJsonValue::fromVariant(value);
}

void PerformanceTracker::addMetadata(const QJsonObject& metadata) {
    for (auto it = metadata.begin(); it != metadata.end(); ++it) {
        metadata_[it.key()] = it.value();
    }
}

MemoryTracker::MemoryTracker(const QString& component, const QString& context)
    : component_(component)
    , context_(context)
    , initialMemory_(DebugLogger::instance()->getCurrentMemoryUsage()) {
    
    DebugLogger::instance()->recordMemoryUsage(component_, initialMemory_, 
                                              context_.isEmpty() ? "MemoryTracker" : context_);
}

MemoryTracker::~MemoryTracker() {
    qint64 finalMemory = getCurrentUsage();
    qint64 difference = finalMemory - initialMemory_;
    
    QString context = QString("%1 (diff: %2%3 bytes)")
                     .arg(context_.isEmpty() ? "MemoryTracker" : context_)
                     .arg(difference >= 0 ? "+" : "")
                     .arg(difference);
    
    DebugLogger::instance()->recordMemoryUsage(component_, finalMemory, context);
}

void MemoryTracker::recordCheckpoint(const QString& checkpoint) {
    qint64 currentMemory = getCurrentUsage();
    checkpoints_[checkpoint] = currentMemory;
    
    DebugLogger::instance()->recordMemoryUsage(component_, currentMemory, 
                                              QString("%1:%2").arg(context_).arg(checkpoint));
}

qint64 MemoryTracker::getCurrentUsage() const {
    // Reuse the same memory tracking implementation from DebugLogger
    return DebugLogger::instance()->getCurrentMemoryUsage();
}

void DebugLogger::processNetworkQueue() {
    // Process queued network log entries
    if (!networkLogger_ || networkQueue_.isEmpty()) {
        return;
    }
    
    while (!networkQueue_.isEmpty() && networkLogger_) {
        QByteArray data = networkQueue_.dequeue();
        
        QNetworkRequest request(QUrl(QString("%1:%2/logs").arg(networkServerUrl_).arg(networkPort_)));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setHeader(QNetworkRequest::UserAgentHeader, "Murmur-Logger/1.0");
        
        // Send POST request with log data
        QNetworkReply* reply = networkLogger_->post(request, data);
        
        // Handle response with comprehensive error handling
        connect(reply, &QNetworkReply::finished, [this, reply]() {
            if (reply->error() != QNetworkReply::NoError) {
                // Log network error details
                QString errorDetails = QString("Network logging failed: %1 (HTTP %2)")
                                     .arg(reply->errorString())
                                     .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
                
                qWarning() << errorDetails;
                
                // Increment error count and disable network logging if too many failures
                networkErrorCount_++;
                if (networkErrorCount_ > 5) {
                    qWarning() << "Too many network logging errors, disabling network logging";
                    networkLogger_->deleteLater();
                    networkLogger_ = nullptr;
                    networkQueue_.clear();
                }
            } else {
                // Reset error count on successful transmission
                networkErrorCount_ = 0;
            }
            reply->deleteLater();
        });
        
        // Rate limiting
        if (networkQueue_.size() > 10) {
            QThread::msleep(10); // Small delay between requests
        }
    }
}

DebugScope::DebugScope(const QString& function, const QString& component, const QJsonObject& context)
    : function_(function)
    , component_(component)
    , context_(context)
    , startTime_(QDateTime::currentDateTime()) {
    
    QJsonObject entryContext = context_;
    entryContext["scopeType"] = "entry";
    
    DebugLogger::instance()->logWithMetadata(LogLevel::Trace, "murmur.debug", component_, 
                                           QString("Entering %1").arg(function_), entryContext);
}

DebugScope::~DebugScope() {
    qint64 duration = startTime_.msecsTo(QDateTime::currentDateTime());
    
    QJsonObject exitContext = context_;
    exitContext["scopeType"] = "exit";
    exitContext["durationMs"] = duration;
    
    DebugLogger::instance()->logWithMetadata(LogLevel::Trace, "murmur.debug", component_, 
                                           QString("Exiting %1 (took %2ms)").arg(function_).arg(duration), 
                                           exitContext);
}

void DebugScope::log(LogLevel level, const QString& message) {
    QJsonObject logContext = context_;
    logContext["scopeFunction"] = function_;
    
    DebugLogger::instance()->logWithMetadata(level, "murmur.debug", component_, message, logContext);
}

void DebugScope::addContext(const QString& key, const QVariant& value) {
    context_[key] = QJsonValue::fromVariant(value);
}

QJsonObject DebugLogger::getSystemMemoryInfo() const {
    QJsonObject systemInfo;
    
#ifdef Q_OS_MACOS
    // macOS system memory information
    size_t size = sizeof(vm_size_t);
    vm_size_t pageSize;
    if (sysctlbyname("hw.pagesize", &pageSize, &size, nullptr, 0) == 0) {
        systemInfo["pageSize"] = static_cast<qint64>(pageSize);
    }
    
    // Physical memory
    size = sizeof(uint64_t);
    uint64_t physicalMemory;
    if (sysctlbyname("hw.memsize", &physicalMemory, &size, nullptr, 0) == 0) {
        systemInfo["totalPhysicalMB"] = static_cast<double>(physicalMemory) / (1024.0 * 1024.0);
    }
    
    // VM statistics
    vm_statistics64_data_t vmStats;
    mach_msg_type_number_t vmStatsCount = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vmStats, &vmStatsCount) == KERN_SUCCESS) {
        systemInfo["freePagesMB"] = static_cast<double>(vmStats.free_count * pageSize) / (1024.0 * 1024.0);
        systemInfo["activePagesMB"] = static_cast<double>(vmStats.active_count * pageSize) / (1024.0 * 1024.0);
        systemInfo["inactivePagesMB"] = static_cast<double>(vmStats.inactive_count * pageSize) / (1024.0 * 1024.0);
        systemInfo["wiredPagesMB"] = static_cast<double>(vmStats.wire_count * pageSize) / (1024.0 * 1024.0);
    }
#endif
    
    return systemInfo;
}

} // namespace Murmur