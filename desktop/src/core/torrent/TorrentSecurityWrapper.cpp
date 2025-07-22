#include "TorrentSecurityWrapper.hpp"
#include "../common/Logger.hpp"

namespace Murmur {

TorrentSecurityWrapper::TorrentSecurityWrapper(QObject* parent)
    : QObject(parent)
    , validator_(std::make_unique<InputValidator>())
{
    MURMUR_INFO("TorrentSecurityWrapper initialized");
}

bool TorrentSecurityWrapper::validateTorrentOperation(const QString& operation, const QVariantMap& params) {
    // Basic operation validation
    if (operation.isEmpty()) {
        logSecurityEvent("Invalid Operation", "Empty operation name");
        return false;
    }
    
    // Check parameters based on operation
    if (operation == "addTorrent") {
        QString magnetUri = params.value("magnetUri").toString();
        return validateMagnetUri(magnetUri);
    } else if (operation == "seedFile") {
        QString filePath = params.value("filePath").toString();
        return validateFilePath(filePath);
    }
    
    return true;
}

bool TorrentSecurityWrapper::validateMagnetUri(const QString& uri) {
    bool valid = InputValidator::validateMagnetUri(uri);
    if (!valid) {
        logSecurityEvent("Invalid Magnet URI", uri.left(100));
    }
    return valid;
}

bool TorrentSecurityWrapper::validateFilePath(const QString& path) {
    bool valid = InputValidator::validateFilePath(path) && 
                 InputValidator::isSecurePath(path);
    if (!valid) {
        logSecurityEvent("Invalid File Path", path);
    }
    return valid;
}

bool TorrentSecurityWrapper::checkResourceLimits(qint64 size, const QString& operation) {
    if (!InputValidator::validateFileSize(size)) {
        logSecurityEvent("Resource Limit Exceeded", 
                        QString("Size: %1 bytes, Operation: %2").arg(size).arg(operation));
        return false;
    }
    
    if (!InputValidator::checkMemoryLimit(size)) {
        logSecurityEvent("Memory Limit Exceeded", 
                        QString("Requested: %1 bytes").arg(size));
        return false;
    }
    
    return true;
}

void TorrentSecurityWrapper::logSecurityEvent(const QString& event, const QString& details) {
    MURMUR_WARN("Security Event: {} - {}", event.toStdString(), details.toStdString());
}

} // namespace Murmur