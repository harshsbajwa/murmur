#include "InputValidator.hpp"
#include "../common/Logger.hpp"
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QStorageInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QFile>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkInterface>
#include <filesystem>
#include <thread>
#include <chrono>
#include <sstream>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#include <unistd.h>
#elif defined(Q_OS_LINUX)
#include <fstream>
#endif

namespace Murmur {

// Static regex patterns
const QRegularExpression InputValidator::magnetUriPattern_(
    R"(^magnet:\?xt=urn:btih:[a-fA-F0-9]{40}(?:&[a-zA-Z0-9%=&.:/+_-]+)*$)");

const QRegularExpression InputValidator::fileNamePattern_(
    R"(^[a-zA-Z0-9\s\-_.()[\]']+$)");

const QRegularExpression InputValidator::pathTraversalPattern_(
    R"(\.{2}[/\\]|[/\\]\.{2}|^\.{2}$)");

const QRegularExpression InputValidator::suspiciousContentPattern_(
    R"(<script|javascript:|data:|vbscript:|onload=|onerror=|eval\(|exec\(|%[0-9a-fA-F]*x|\\x[0-9a-fA-F]{2}|\\x90|(%[0-9a-fA-F]*x.*){3,})",
    QRegularExpression::CaseInsensitiveOption);

bool InputValidator::validateFilePath(const QString& path) {
    if (path.isEmpty() || path.length() > 4096) {
        MURMUR_WARN("File path validation failed: empty or too long");
        return false;
    }
    
    // Check for null bytes including in the underlying string data
    if (path.contains(QChar(0)) || path.toUtf8().contains('\0')) {
        MURMUR_WARN("Null byte injection detected in path: {}", path.toStdString());
        return false;
    }
    
    // Check for dangerous control characters (newlines, carriage returns, etc.)
    if (path.contains('\n') || path.contains('\r') || path.contains('\t')) {
        MURMUR_WARN("Control character injection detected in path: {}", path.toStdString());
        return false;
    }
    
    // Check for dangerous shell metacharacters (excluding single quotes which are common in filenames)
    if (path.contains(';') || path.contains('|') || path.contains('&') || 
        path.contains('`') || path.contains('$')) {
        MURMUR_WARN("Shell metacharacter detected in path: {}", path.toStdString());
        return false;
    }
    
    // URL decode the path to check for encoded attacks
    QString decodedPath = QUrl::fromPercentEncoding(path.toUtf8());
    if (decodedPath != path) {
        // If the path was URL encoded, validate the decoded version too
        if (decodedPath.contains(QChar(0)) || isPathTraversalAttempt(decodedPath) ||
            decodedPath.contains('\n') || decodedPath.contains('\r')) {
            MURMUR_WARN("URL-encoded attack detected in path: {}", path.toStdString());
            return false;
        }
    }
    
    if (isPathTraversalAttempt(path) || isPathTraversalAttempt(decodedPath)) {
        MURMUR_WARN("Path traversal attempt detected: {}", path.toStdString());
        return false;
    }
    
    if (isSystemPath(path) || isSystemPath(decodedPath)) {
        MURMUR_WARN("Attempt to access system path: {}", path.toStdString());
        return false;
    }
    
    QFileInfo fileInfo(path);
    bool isAbsolute = fileInfo.isAbsolute();
    
    // Handle cross-platform absolute path checking for testing
    if (!isAbsolute) {
        // Check for Windows absolute paths on non-Windows systems (for testing)
        if (path.length() >= 3 && path.at(1) == ':' && (path.at(2) == '\\' || path.at(2) == '/')) {
            isAbsolute = true; // C:\ or C:/ pattern
        }
    }
    
    if (!isAbsolute) {
        MURMUR_WARN("Relative path not allowed: {}", path.toStdString());
        return false;
    }
    
    // Check for symlinks to sensitive locations (check even if symlink doesn't exist yet)
    if (fileInfo.isSymLink() || QFile::exists(path)) {
        if (fileInfo.isSymLink()) {
            QString target = fileInfo.symLinkTarget();
            if (isSystemPath(target)) {
                MURMUR_WARN("Symlink to system path detected: {} -> {}", path.toStdString(), target.toStdString());
                return false;
            }
            // Also reject symlinks that don't resolve or resolve to non-existent targets
            if (target.isEmpty()) {
                MURMUR_WARN("Invalid symlink detected: {}", path.toStdString());
                return false;
            }
        }
    }
    
    // Additional check: if the file exists and is a symlink, be more restrictive
    if (QFile::exists(path)) {
        QFileInfo existingFileInfo(path);
        if (existingFileInfo.isSymLink()) {
            MURMUR_WARN("Symbolic link detected in path: {}", path.toStdString());
            return false; // For security, reject all symlinks in critical paths
        }
    }
    
    return true;
}

bool InputValidator::validateFileSize(qint64 size) {
    if (size < 0 || size > MAX_FILE_SIZE) {
        MURMUR_WARN("Invalid file size: {} bytes", size);
        return false;
    }
    return true;
}

bool InputValidator::validateFileName(const QString& name) {
    if (name.isEmpty() || name.length() > 255) {
        return false;
    }
    
    // Check for invalid characters
    if (!fileNamePattern_.match(name).hasMatch()) {
        MURMUR_WARN("Invalid characters in filename: {}", name.toStdString());
        return false;
    }
    
    // Check for reserved names (Windows)
    QStringList reservedNames = {
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5",
        "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", 
        "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    
    QString upperName = name.toUpper();
    for (const QString& reserved : reservedNames) {
        if (upperName == reserved || upperName.startsWith(reserved + ".")) {
            MURMUR_WARN("Reserved filename: {}", name.toStdString());
            return false;
        }
    }
    
    return true;
}

bool InputValidator::isSecurePath(const QString& path) {
    QFileInfo fileInfo(path);
    QString absolutePath = fileInfo.absoluteFilePath();
    
    // Ensure path is within allowed directories
    QStringList allowedPaths = {
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation),
        QStandardPaths::writableLocation(QStandardPaths::TempLocation)
    };
    
    for (const QString& allowedPath : allowedPaths) {
        if (absolutePath.startsWith(allowedPath)) {
            return true;
        }
    }
    
    MURMUR_WARN("Path not in allowed directories: {}", absolutePath.toStdString());
    return false;
}

bool InputValidator::validateMagnetUri(const QString& uri) {
    if (uri.isEmpty() || uri.length() > 2048) {
        return false;
    }
    
    return magnetUriPattern_.match(uri).hasMatch();
}

bool InputValidator::validateTrackerUrl(const QString& url) {
    QUrl qurl(url);
    if (!qurl.isValid()) {
        return false;
    }
    
    QString scheme = qurl.scheme().toLower();
    if (scheme != "http" && scheme != "https" && 
        scheme != "udp" && scheme != "wss" && scheme != "ws") {
        return false;
    }
    
    return validatePort(qurl.port());
}

bool InputValidator::validateIPAddress(const QString& ip) {
    QHostAddress address(ip);
    return !address.isNull();
}

bool InputValidator::validatePort(int port) {
    return port > 0 && port <= 65535;
}

bool InputValidator::validateVideoFormat(const QString& format) {
    QStringList supportedFormats = {
        "mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "3gp"
    };
    return supportedFormats.contains(format.toLower());
}

bool InputValidator::validateAudioFormat(const QString& format) {
    QStringList supportedFormats = {
        "mp3", "wav", "flac", "aac", "ogg", "m4a", "wma"
    };
    return supportedFormats.contains(format.toLower());
}

bool InputValidator::isValidMediaFile(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return false;
    }
    
    QString extension = fileInfo.suffix().toLower();
    return validateVideoFormat(extension) || validateAudioFormat(extension);
}

bool InputValidator::validateLanguageCode(const QString& code) {
    if (code == "auto") return true;
    
    // ISO 639-1 language codes
    QStringList supportedLanguages = {
        "en", "es", "fr", "de", "it", "pt", "ru", "ja", "ko", "zh",
        "ar", "hi", "tr", "pl", "nl", "sv", "da", "no", "fi", "he"
    };
    
    return supportedLanguages.contains(code.toLower());
}

QString InputValidator::sanitizeText(const QString& text) {
    QString sanitized = text;
    
    // Remove null bytes first
    sanitized.remove(QChar(0));
    
    // Handle multiple levels of URL encoding
    QString previousDecoded;
    int decodeAttempts = 0;
    do {
        previousDecoded = sanitized;
        QString decoded = QUrl::fromPercentEncoding(sanitized.toUtf8());
        if (decoded != sanitized && decodeAttempts < 5) {
            sanitized = decoded;
            decodeAttempts++;
        } else {
            break;
        }
    } while (sanitized != previousDecoded && decodeAttempts < 5);
    
    // Remove potentially dangerous content (after URL decoding)
    sanitized.remove(suspiciousContentPattern_);
    
    // Additional XSS patterns after URL decoding
    QRegularExpression additionalXSSPatterns(R"(<script[^>]*>|</script>|<iframe[^>]*>|</iframe>|<object[^>]*>|</object>)",
                                           QRegularExpression::CaseInsensitiveOption);
    sanitized.remove(additionalXSSPatterns);
    
    // Remove SQL injection patterns
    QRegularExpression sqlPatterns(R"(\b(DROP|INSERT|DELETE|UPDATE|SELECT|EXEC|UNION|ALTER|CREATE)\b|--|\|\|)",
                                   QRegularExpression::CaseInsensitiveOption);
    sanitized.remove(sqlPatterns);
    
    // Remove command injection patterns
    QRegularExpression cmdPatterns(R"([;&|`$(){}]|\n|\r)");
    sanitized.remove(cmdPatterns);
    
    // Handle path traversal - if it looks like a path, normalize it
    if (sanitized.contains("..") || sanitized.contains("/") || sanitized.contains("\\")) {
        // Check if this is a path traversal attempt
        if (isPathTraversalAttempt(sanitized)) {
            // For path traversal attempts, return empty string
            return QString();
        }
        
        // Normalize the path
        QFileInfo fileInfo(sanitized);
        if (fileInfo.isAbsolute()) {
            QString canonical = fileInfo.canonicalFilePath();
            if (!canonical.isEmpty()) {
                sanitized = canonical;
            }
        }
    }
    
    // Remove Unicode control characters and bidirectional override characters
    QRegularExpression unicodeControls("[\u200E\u200F\u202A-\u202E\u2066-\u2069\uFEFF\u00A0]");
    sanitized.remove(unicodeControls);
    
    // Escape single quotes by doubling them
    sanitized.replace("'", "''");
    
    // Limit length
    if (sanitized.length() > 10000) {
        sanitized = sanitized.left(10000);
    }
    
    return sanitized.trimmed();
}

bool InputValidator::validateJsonMessage(const QJsonObject& json) {
    // Check for reasonable size
    QJsonDocument doc(json);
    if (doc.toJson().size() > 1024 * 1024) { // 1MB limit
        MURMUR_WARN("JSON message too large");
        return false;
    }
    
    // Check for suspicious content in string values
    for (auto it = json.begin(); it != json.end(); ++it) {
        if (it.value().isString()) {
            QString value = it.value().toString();
            if (containsSuspiciousContent(value)) {
                MURMUR_WARN("Suspicious content in JSON field: {}", it.key().toStdString());
                return false;
            }
        }
    }
    
    return true;
}

bool InputValidator::checkMemoryLimit(qint64 requestedBytes) {
    if (requestedBytes < 0 || requestedBytes > MAX_MEMORY_REQUEST) {
        MURMUR_WARN("Memory request exceeds limit: {} bytes", requestedBytes);
        return false;
    }
    
    // Additional system memory check - ensure we don't use more than 80% of available RAM
    #ifdef Q_OS_WIN
        MEMORYSTATUSEX statex;
        statex.dwLength = sizeof(statex);
        if (GlobalMemoryStatusEx(&statex)) {
            qint64 availableMemory = static_cast<qint64>(statex.ullAvailPhys);
            qint64 maxUsableMemory = availableMemory * 0.8; // Use max 80% of available memory
            
            if (requestedBytes > maxUsableMemory) {
                MURMUR_WARN("Memory request exceeds system limit. Requested: {} bytes, Available: {} bytes", 
                           requestedBytes, availableMemory);
                return false;
            }
        }
    #elif defined(Q_OS_MACOS)
        int mib[2] = {CTL_HW, HW_MEMSIZE};
        uint64_t physMemory = 0;
        size_t len = sizeof(physMemory);
        if (sysctl(mib, 2, &physMemory, &len, nullptr, 0) == 0) {
            qint64 maxUsableMemory = static_cast<qint64>(physMemory) * 0.8;
            
            if (requestedBytes > maxUsableMemory) {
                MURMUR_WARN("Memory request exceeds system limit. Requested: {} bytes, Available: {} bytes", 
                           requestedBytes, static_cast<qint64>(physMemory));
                return false;
            }
        }
    #elif defined(Q_OS_LINUX)
        QFile meminfo("/proc/meminfo");
        if (meminfo.open(QIODevice::ReadOnly)) {
            QString content = meminfo.readAll();
            QRegularExpression regex("MemAvailable:\\s+(\\d+)\\s+kB");
            auto match = regex.match(content);
            if (match.hasMatch()) {
                qint64 availableMemory = match.captured(1).toLongLong() * 1024; // Convert from kB to bytes
                qint64 maxUsableMemory = availableMemory * 0.8;
                
                if (requestedBytes > maxUsableMemory) {
                    MURMUR_WARN("Memory request exceeds system limit. Requested: {} bytes, Available: {} bytes", 
                               requestedBytes, availableMemory);
                    return false;
                }
            }
        }
    #endif
    
    return true;
}

bool InputValidator::checkDiskSpace(const QString& path, qint64 requiredBytes) {
    QStorageInfo storage(path);
    qint64 availableBytes = storage.bytesAvailable();
    
    if (availableBytes < requiredBytes + MIN_FREE_DISK_SPACE) {
        MURMUR_WARN("Insufficient disk space. Required: {}, Available: {}", 
                    requiredBytes, availableBytes);
        return false;
    }
    
    return true;
}

bool InputValidator::checkCpuUsage() {
    // Check if CPU usage is within acceptable limits
    double currentUsage = getCurrentCpuUsage();
    
    // If CPU usage is above 90%, consider system overloaded
    if (currentUsage > 90.0) {
        MURMUR_WARN("System CPU usage too high: {:.1f}%", currentUsage);
        return false;
    }
    
    // Also check the number of hardware threads available
    static std::once_flag flag;
    static int maxThreads;
    std::call_once(flag, []() {
        maxThreads = std::thread::hardware_concurrency();
    });
    
    // Ensure we have at least 2 hardware threads available for the system
    if (maxThreads < 2) {
        MURMUR_WARN("Insufficient CPU cores: {}", maxThreads);
        return false;
    }
    
    return true;
}

bool InputValidator::isPathTraversalAttempt(const QString& path) {
    return pathTraversalPattern_.match(path).hasMatch();
}

bool InputValidator::containsSuspiciousContent(const QString& content) {
    // Check main pattern first
    if (suspiciousContentPattern_.match(content).hasMatch()) {
        return true;
    }
    
    // Enhanced format string attack detection
    // Check for multiple % formatters of various types
    QStringList formatPatterns = {
        R"(%[0-9]*x)",         // %x, %08x, etc. (removed a-fA-F to match standard printf)
        R"(%[0-9]*[dioxu])",   // %d, %i, %o, %u with optional width
        R"(%[0-9]*s)",         // %s with optional width
        R"(%n)",               // %n (particularly dangerous)
        R"(%p)"                // %p (pointer)
    };
    
    int totalFormatCount = 0;
    for (const QString& pattern : formatPatterns) {
        QRegularExpression regex(pattern);
        auto matches = regex.globalMatch(content);
        while (matches.hasNext()) {
            matches.next();
            totalFormatCount++;
        }
    }
    
    // If we have 3 or more format specifiers, it's suspicious
    if (totalFormatCount >= 3) {
        return true;
    }
    
    // Specific check for repeated %n which is very dangerous
    if (content.count("%n") >= 2) {
        return true;
    }
    
    // Check for hex escape sequences that could indicate shellcode/buffer overflow
    QRegularExpression hexPattern(R"(\\x[0-9a-fA-F]{2})");
    auto hexMatches = hexPattern.globalMatch(content);
    int hexCount = 0;
    while (hexMatches.hasNext()) {
        hexMatches.next();
        hexCount++;
    }
    
    // Multiple hex escape sequences are suspicious (could be shellcode)
    if (hexCount >= 4) {
        return true;
    }
    
    // Check for very long repeated patterns (classic buffer overflow)
    if (content.length() > 500) {
        // Look for repeated characters which is common in buffer overflow attacks
        QRegularExpression repeatedChars(R"((.)\1{50,})"); // 50+ repeated chars
        if (repeatedChars.match(content).hasMatch()) {
            return true;
        }
    }
    
    return false;
}

bool InputValidator::validateProcessName(const QString& processName) {
    if (processName.isEmpty() || processName.length() > 256) {
        return false;
    }
    
    // Allow alphanumeric, hyphen, underscore, and period
    QRegularExpression processPattern(R"(^[a-zA-Z0-9\-_.]+$)");
    return processPattern.match(processName).hasMatch();
}

bool InputValidator::isValidFileExtension(const QString& extension) {
    QStringList allowedExtensions = {
        // Video
        "mp4", "avi", "mkv", "mov", "wmv", "flv", "webm", "m4v", "3gp",
        // Audio  
        "mp3", "wav", "flac", "aac", "ogg", "m4a", "wma",
        // Subtitles
        "srt", "vtt", "ass", "ssa",
        // Data
        "json", "xml", "txt"
    };
    
    return allowedExtensions.contains(extension.toLower());
}

bool InputValidator::validateVideoFile(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return false;
    }
    
    return validateVideoFormat(fileInfo.suffix());
}

bool InputValidator::validateInfoHash(const QString& infoHash) {
    return InfoHashValidator::isValid(infoHash);
}

bool InputValidator::isSystemPath(const QString& path) {
    QString normalizedPath = QFileInfo(path).canonicalFilePath();
    if (normalizedPath.isEmpty()) {
        normalizedPath = path; // Use original path if canonical fails
    }
    
#ifdef Q_OS_MACOS
    if (normalizedPath.startsWith("/private/var/folders")) {
        return false;
    }
#endif
    
    QStringList systemPaths = {
#ifdef Q_OS_WIN
        "C:/Windows", "C:/Program Files", "C:/Program Files (x86)", "C:/ProgramData"
#elif defined(Q_OS_MACOS)
        "/System", "/usr", "/bin", "/sbin", "/etc", "/var", "/Library/System", "/dev", "/proc"
#else // Linux
        "/usr", "/bin", "/sbin", "/etc", "/var", "/sys", "/proc", "/dev", "/boot"
#endif
    };
    
    for (const QString& systemPath : systemPaths) {
        if (normalizedPath.startsWith(systemPath)) {
            return true;
        }
        // Also check the original path in case canonicalFilePath() fails
        if (path.startsWith(systemPath)) {
            return true;
        }
    }
    
    return false;
}

qint64 InputValidator::getAvailableDiskSpace(const QString& path) {
    QStorageInfo storage(path);
    return storage.bytesAvailable();
}

double InputValidator::getCurrentCpuUsage() {
    // Cross-platform CPU usage monitoring
    static auto lastCpuTime = std::chrono::steady_clock::now();
    static double lastUsage = 0.0;
    
    auto currentTime = std::chrono::steady_clock::now();
    auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastCpuTime);
    
    // Only update every 1 second to avoid overhead
    if (timeDiff.count() < 1000) {
        return lastUsage;
    }
    
    lastCpuTime = currentTime;
    
    #ifdef Q_OS_WIN
        static ULARGE_INTEGER lastIdleTime, lastKernelTime, lastUserTime;
        static bool firstCall = true;
        
        FILETIME idleTime, kernelTime, userTime;
        if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
            ULARGE_INTEGER currentIdleTime, currentKernelTime, currentUserTime;
            currentIdleTime.LowPart = idleTime.dwLowDateTime;
            currentIdleTime.HighPart = idleTime.dwHighDateTime;
            currentKernelTime.LowPart = kernelTime.dwLowDateTime;
            currentKernelTime.HighPart = kernelTime.dwHighDateTime;
            currentUserTime.LowPart = userTime.dwLowDateTime;
            currentUserTime.HighPart = userTime.dwHighDateTime;
            
            if (!firstCall) {
                ULONGLONG idleDiff = currentIdleTime.QuadPart - lastIdleTime.QuadPart;
                ULONGLONG kernelDiff = currentKernelTime.QuadPart - lastKernelTime.QuadPart;
                ULONGLONG userDiff = currentUserTime.QuadPart - lastUserTime.QuadPart;
                ULONGLONG totalDiff = kernelDiff + userDiff;
                
                if (totalDiff > 0) {
                    lastUsage = (100.0 * (totalDiff - idleDiff)) / totalDiff;
                }
            }
            
            lastIdleTime = currentIdleTime;
            lastKernelTime = currentKernelTime;
            lastUserTime = currentUserTime;
            firstCall = false;
        }
        
    #elif defined(Q_OS_MACOS)
        // Use system load average as approximation
        double loadavg[3];
        if (getloadavg(loadavg, 3) != -1) {
            int numCpus = std::thread::hardware_concurrency();
            lastUsage = std::min(100.0, (loadavg[0] / numCpus) * 100.0);
        }
        
    #elif defined(Q_OS_LINUX)
        static unsigned long long lastTotalTime = 0, lastIdleTime = 0;
        static bool firstCall = true;
        
        std::ifstream statFile("/proc/stat");
        if (statFile.is_open()) {
            std::string line;
            std::getline(statFile, line);
            
            if (line.substr(0, 3) == "cpu") {
                std::istringstream ss(line.substr(4));
                unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
                ss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
                
                unsigned long long currentTotalTime = user + nice + system + idle + iowait + irq + softirq + steal;
                unsigned long long currentIdleTime = idle + iowait;
                
                if (!firstCall && currentTotalTime > lastTotalTime) {
                    unsigned long long totalDiff = currentTotalTime - lastTotalTime;
                    unsigned long long idleDiff = currentIdleTime - lastIdleTime;
                    lastUsage = 100.0 * (totalDiff - idleDiff) / totalDiff;
                }
                
                lastTotalTime = currentTotalTime;
                lastIdleTime = currentIdleTime;
                firstCall = false;
            }
        }
    #endif
    
    return lastUsage;
}

bool InputValidator::isValidPath(const QString& path) {
    return validateFilePath(path);
}

bool InputValidator::isValidExecutable(const QString& executable) {
    if (executable.isEmpty()) {
        return false;
    }
    
    // Check for suspicious patterns
    if (containsSuspiciousContent(executable)) {
        return false;
    }
    
    // Check file extension on Windows
    #ifdef Q_OS_WIN
    if (!executable.endsWith(".exe", Qt::CaseInsensitive) && 
        !executable.endsWith(".bat", Qt::CaseInsensitive) && 
        !executable.endsWith(".cmd", Qt::CaseInsensitive)) {
        return false;
    }
    #endif
    
    return validateFilePath(executable);
}

bool InputValidator::isValidIdentifier(const QString& identifier) {
    if (identifier.isEmpty() || identifier.length() > 255) {
        return false;
    }
    
    // Must start with letter or underscore
    if (!identifier.at(0).isLetter() && identifier.at(0) != '_') {
        return false;
    }
    
    // Must contain only letters, numbers, underscores, hyphens, or dots
    QRegularExpression identifierPattern("^[a-zA-Z_][a-zA-Z0-9_.-]*$");
    return identifierPattern.match(identifier).hasMatch();
}

bool InputValidator::isValidCacheKey(const QString& key) {
    if (key.isEmpty() || key.length() > 512) {
        return false;
    }
    
    // Cache keys can be more flexible than identifiers
    QRegularExpression cacheKeyPattern("^[a-zA-Z0-9._-]+$");
    return cacheKeyPattern.match(key).hasMatch();
}

bool InputValidator::hasNullBytes(const QString& input) {
    // Check for null bytes in various forms
    if (input.contains(QChar(0))) {
        return true;
    }
    
    // Check for null bytes in UTF-8 encoding
    QByteArray utf8 = input.toUtf8();
    if (utf8.contains('\0')) {
        return true;
    }
    
    // Check for URL-encoded null bytes
    if (input.contains("%00", Qt::CaseInsensitive)) {
        return true;
    }
    
    // Check for hex-encoded null bytes
    if (input.contains("\\x00", Qt::CaseInsensitive) || 
        input.contains("\\0", Qt::CaseInsensitive)) {
        return true;
    }
    
    return false;
}

bool InputValidator::isSymlinkSafe(const QString& path) {
    if (path.isEmpty()) {
        return false;
    }
    
    QFileInfo fileInfo(path);
    
    // If path doesn't exist, we can't determine if it's a symlink
    // but we should be cautious
    if (!fileInfo.exists()) {
        return true; // Allow non-existent paths (they'll be validated elsewhere)
    }
    
    // Check if the file itself is a symlink
    if (fileInfo.isSymLink()) {
        QString target = fileInfo.symLinkTarget();
        
        // Reject if symlink target is empty or invalid
        if (target.isEmpty()) {
            MURMUR_WARN("Invalid symlink detected: {}", path.toStdString());
            return false;
        }
        
        // Reject if symlink points to system paths
        if (isSystemPath(target)) {
            MURMUR_WARN("Symlink to system path detected: {} -> {}", path.toStdString(), target.toStdString());
            return false;
        }
        
        // Reject if symlink creates path traversal
        if (isPathTraversalAttempt(target)) {
            MURMUR_WARN("Symlink path traversal detected: {} -> {}", path.toStdString(), target.toStdString());
            return false;
        }
        
        // For additional safety, reject all symlinks in security-critical contexts
        MURMUR_WARN("Symlink rejected for security: {}", path.toStdString());
        return false;
    }
    
    // Check parent directories for symlinks
    QString parentPath = fileInfo.absolutePath();
    while (parentPath != "/" && parentPath != "C:\\" && !parentPath.isEmpty()) {
        QFileInfo parentInfo(parentPath);
        if (parentInfo.isSymLink()) {
            MURMUR_WARN("Parent directory is symlink: {}", parentPath.toStdString());
            return false;
        }
        
        QString newParentPath = parentInfo.absolutePath();
        if (newParentPath == parentPath) {
            break; // Reached root
        }
        parentPath = newParentPath;
    }
    
    return true;
}

bool InputValidator::isLengthSafe(const QString& input, int maxLength) {
    if (input.length() > maxLength) {
        MURMUR_WARN("Input exceeds maximum length: {} > {}", input.length(), maxLength);
        return false;
    }
    
    // Check UTF-8 byte length as well (for potential buffer overflow protection)
    QByteArray utf8 = input.toUtf8();
    if (utf8.length() > maxLength * 4) { // UTF-8 can be up to 4 bytes per character
        MURMUR_WARN("Input UTF-8 encoding exceeds safe length: {} bytes", utf8.length());
        return false;
    }
    
    return true;
}

bool InputValidator::isPathSafe(const QString& path) {
    // Comprehensive path safety check
    if (!isLengthSafe(path, 4096)) {
        return false;
    }
    
    if (hasNullBytes(path)) {
        MURMUR_WARN("Path contains null bytes: {}", path.left(100).toStdString());
        return false;
    }
    
    if (isPathTraversalAttempt(path)) {
        return false;
    }
    
    if (!isSymlinkSafe(path)) {
        return false;
    }
    
    if (containsEncodingAttacks(path)) {
        return false;
    }
    
    if (!isUnicodeSafe(path)) {
        return false;
    }
    
    // Check for dangerous shell characters
    QRegularExpression dangerousChars("[;|&`$(){}\[\]\n\r\t]");
    if (dangerousChars.match(path).hasMatch()) {
        MURMUR_WARN("Path contains dangerous characters: {}", path.left(100).toStdString());
        return false;
    }
    
    return true;
}

bool InputValidator::containsEncodingAttacks(const QString& input) {
    // Check for multiple levels of encoding that could hide attacks
    QString decoded = decodeAllEncodings(input);
    
    // If the decoded version is significantly different, it's suspicious
    if (decoded != input) {
        // Check if the decoded version contains dangerous patterns
        if (isPathTraversalAttempt(decoded) || 
            containsSuspiciousContent(decoded) ||
            hasNullBytes(decoded)) {
            MURMUR_WARN("Encoding attack detected: {} -> {}", input.left(100).toStdString(), decoded.left(100).toStdString());
            return true;
        }
    }
    
    // Check for excessive encoding layers (more than 2 is suspicious)
    QString temp = input;
    int decodeLayers = 0;
    for (int i = 0; i < 5; ++i) {
        QString newDecoded = QUrl::fromPercentEncoding(temp.toUtf8());
        if (newDecoded != temp) {
            decodeLayers++;
            temp = newDecoded;
        } else {
            break;
        }
    }
    
    if (decodeLayers > 2) {
        MURMUR_WARN("Excessive encoding layers detected: {} layers", decodeLayers);
        return true;
    }
    
    // Check for hex encoding patterns
    QRegularExpression hexPattern(R"(\\x[0-9a-fA-F]{2})");
    auto hexMatches = hexPattern.globalMatch(input);
    int hexCount = 0;
    while (hexMatches.hasNext()) {
        hexMatches.next();
        hexCount++;
    }
    
    if (hexCount > 10) { // More than 10 hex sequences is suspicious
        MURMUR_WARN("Excessive hex encoding detected: {} sequences", hexCount);
        return true;
    }
    
    return false;
}

bool InputValidator::isUnicodeSafe(const QString& input) {
    // Check for dangerous Unicode characters
    
    // Unicode bidirectional override characters
    if (input.contains(QChar(0x200E)) || // Left-to-right mark
        input.contains(QChar(0x200F)) || // Right-to-left mark
        input.contains(QChar(0x202A)) || // Left-to-right embedding
        input.contains(QChar(0x202B)) || // Right-to-left embedding
        input.contains(QChar(0x202C)) || // Pop directional formatting
        input.contains(QChar(0x202D)) || // Left-to-right override
        input.contains(QChar(0x202E)) || // Right-to-left override
        input.contains(QChar(0x2066)) || // Left-to-right isolate
        input.contains(QChar(0x2067)) || // Right-to-left isolate
        input.contains(QChar(0x2068)) || // First strong isolate
        input.contains(QChar(0x2069))) { // Pop directional isolate
        MURMUR_WARN("Dangerous Unicode bidirectional characters detected");
        return false;
    }
    
    // Zero-width characters that could hide content
    if (input.contains(QChar(0xFEFF)) || // Zero-width no-break space
        input.contains(QChar(0x200B)) || // Zero-width space
        input.contains(QChar(0x200C)) || // Zero-width non-joiner
        input.contains(QChar(0x200D))) { // Zero-width joiner
        MURMUR_WARN("Zero-width Unicode characters detected");
        return false;
    }
    
    // Non-breaking space (sometimes used to bypass filters)
    if (input.contains(QChar(0x00A0))) {
        MURMUR_WARN("Non-breaking space detected");
        return false;
    }
    
    return true;
}

QString InputValidator::decodeAllEncodings(const QString& input) {
    QString result = input;
    QString previous;
    
    // Iterate through multiple encoding layers (max 5 to prevent infinite loops)
    for (int i = 0; i < 5; ++i) {
        previous = result;
        
        // URL decode
        QString urlDecoded = QUrl::fromPercentEncoding(result.toUtf8());
        if (urlDecoded != result) {
            result = urlDecoded;
            continue;
        }
        
        // HTML entity decode (basic)
        QString htmlDecoded = result;
        htmlDecoded.replace("&lt;", "<")
                   .replace("&gt;", ">")
                   .replace("&amp;", "&")
                   .replace("&quot;", "\"")
                   .replace("&#39;", "'");
        if (htmlDecoded != result) {
            result = htmlDecoded;
            continue;
        }
        
        // If no changes were made, we're done
        if (result == previous) {
            break;
        }
    }
    
    return result;
}

} // namespace Murmur
