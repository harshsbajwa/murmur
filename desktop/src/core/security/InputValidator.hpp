#pragma once

#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <filesystem>
#include "InfoHashValidator.hpp"

namespace Murmur {

class InputValidator {
public:
    // File and path validation
    static bool validateFilePath(const QString& path);
    static bool validateFileSize(qint64 size);
    static bool validateFileName(const QString& name);
    static bool isSecurePath(const QString& path);
    
    // Network validation
    static bool validateMagnetUri(const QString& uri);
    static bool validateTrackerUrl(const QString& url);
    static bool validateIPAddress(const QString& ip);
    static bool validatePort(int port);
    
    // Media validation
    static bool validateVideoFormat(const QString& format);
    static bool validateAudioFormat(const QString& format);
    static bool validateVideoFile(const QString& filePath);
    static bool isValidMediaFile(const QString& filePath);
    static bool validateInfoHash(const QString& infoHash);
    
    // Text validation
    static bool validateLanguageCode(const QString& code);
    static QString sanitizeText(const QString& text);
    static bool validateJsonMessage(const QJsonObject& json);
    
    // Resource limits
    static bool checkMemoryLimit(qint64 requestedBytes);
    static bool checkDiskSpace(const QString& path, qint64 requiredBytes);
    static bool checkCpuUsage();
    
    // Security checks
    static bool isPathTraversalAttempt(const QString& path);
    static bool containsSuspiciousContent(const QString& content);
    static bool validateProcessName(const QString& processName);
    
    // Additional validation methods
    static bool isValidPath(const QString& path);
    static bool isValidExecutable(const QString& executable);
    static bool isValidIdentifier(const QString& identifier);
    static bool isValidCacheKey(const QString& key);
    
    // Enhanced security checks
    static bool hasNullBytes(const QString& input);
    static bool isSymlinkSafe(const QString& path);
    static bool isLengthSafe(const QString& input, int maxLength = 4096);
    static bool isPathSafe(const QString& path);
    static bool containsEncodingAttacks(const QString& input);
    static bool isUnicodeSafe(const QString& input);
    static QString decodeAllEncodings(const QString& input);
    
private:
    // Constants
    static constexpr qint64 MAX_FILE_SIZE = 50LL * 1024 * 1024 * 1024; // 50GB maximum file size
    static constexpr qint64 MAX_MEMORY_REQUEST = 4LL * 1024 * 1024 * 1024; // 4GB
    static constexpr qint64 MIN_FREE_DISK_SPACE = 1LL * 1024 * 1024 * 1024; // 1GB
    
    // Regex patterns
    static const QRegularExpression magnetUriPattern_;
    static const QRegularExpression fileNamePattern_;
    static const QRegularExpression pathTraversalPattern_;
    static const QRegularExpression suspiciousContentPattern_;
    
    // Helper methods
    static bool isValidFileExtension(const QString& extension);
    static bool isSystemPath(const QString& path);
    static qint64 getAvailableDiskSpace(const QString& path);
    static double getCurrentCpuUsage();
};

} // namespace Murmur