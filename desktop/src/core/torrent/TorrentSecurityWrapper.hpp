#pragma once

#include "../security/InputValidator.hpp"
#include <QtCore/QObject>
#include <memory>

namespace Murmur {

class TorrentSecurityWrapper : public QObject {
    Q_OBJECT
    
public:
    explicit TorrentSecurityWrapper(QObject* parent = nullptr);
    
    // Security validation for torrent operations
    bool validateTorrentOperation(const QString& operation, const QVariantMap& params);
    bool validateMagnetUri(const QString& uri);
    bool validateFilePath(const QString& path);
    bool checkResourceLimits(qint64 size, const QString& operation);
    
    // Sandboxed operations
    template<typename Func>
    auto executeSandboxed(Func&& func) -> decltype(func());
    
private:
    std::unique_ptr<InputValidator> validator_;
    
    void logSecurityEvent(const QString& event, const QString& details);
};

} // namespace Murmur