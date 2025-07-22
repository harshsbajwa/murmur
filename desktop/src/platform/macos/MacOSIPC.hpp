#pragma once

#include "core/security/SecureIPC.hpp"
#include <QtCore/QObject>
#include <memory>

#ifdef Q_OS_MACOS
// Forward declaration for XPC types
typedef struct _xpc_connection_s* xpc_connection_t;
#endif

namespace Murmur {

/**
 * @brief macOS-specific IPC implementation using XPC
 * 
 * Provides secure inter-process communication for macOS using the XPC framework
 * with additional security layers for sandboxed environments.
 */
class MacOSIPC : public SecureIPC {
    Q_OBJECT

public:
    explicit MacOSIPC(QObject* parent = nullptr);
    ~MacOSIPC() override;

    // Platform-specific IPC interface
    Expected<bool, IPCError> initializeServer(const QString& serverName);
    Expected<bool, IPCError> initializeClient(const QString& serverName);
    Expected<bool, IPCError> sendMessage(const QString& clientId, const IPCMessage& message);
    Expected<bool, IPCError> broadcastMessage(const IPCMessage& message);
    Expected<bool, IPCError> shutdown();
    
    bool isServerRunning() const;
    bool isConnected() const;
    QStringList getConnectedClients() const;

    // macOS-specific methods
    Expected<bool, IPCError> setupXPCService(const QString& serviceName);
    Expected<bool, IPCError> enableSandboxSupport(bool enabled);
    Expected<bool, IPCError> configureEntitlements(const QStringList& entitlements);

private slots:
    void handleXPCConnection();
    void handleXPCDisconnection();
    void handleXPCMessage(const QByteArray& data);
    void handleXPCError(const QString& error);

#ifdef Q_OS_MACOS
    void handleXPCConnection(xpc_connection_t client_connection, const QString& clientId);
    void handleXPCClientMessage(const QByteArray& data, const QString& senderId);
    void handleXPCClientDisconnection(const QString& clientId);
#endif

private:
    class MacOSIPCPrivate;
    std::unique_ptr<MacOSIPCPrivate> d;

    // Platform-specific helpers
    Expected<bool, IPCError> validateXPCConnection() const;
    Expected<QByteArray, IPCError> encryptForXPC(const QByteArray& data);
    Expected<QByteArray, IPCError> decryptFromXPC(const QByteArray& encryptedData);
    bool setupXPCSecurityContext();
    void cleanupXPCResources();
    QString generateClientToken(const QString& clientId);
};

} // namespace Murmur