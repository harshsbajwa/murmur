#pragma once

#include "core/security/SecureIPC.hpp"
#include <QtCore/QObject>
#include <memory>

namespace Murmur {

/**
 * @brief Windows-specific IPC implementation using Named Pipes
 * 
 * Provides secure inter-process communication for Windows using Named Pipes
 * with DACL security and encryption for sandboxed environments.
 */
class WindowsIPC : public SecureIPC {
    Q_OBJECT

public:
    explicit WindowsIPC(QObject* parent = nullptr);
    ~WindowsIPC() override;

    // SecureIPC interface implementation
    Expected<bool, IPCError> initializeServer(const QString& serverName) override;
    Expected<bool, IPCError> initializeClient(const QString& serverName) override;
    Expected<bool, IPCError> sendMessage(const QString& clientId, const IPCMessage& message) override;
    Expected<bool, IPCError> broadcastMessage(const IPCMessage& message) override;
    Expected<bool, IPCError> shutdown() override;
    
    bool isServerRunning() const override;
    bool isConnected() const override;
    QStringList getConnectedClients() const override;

    // Windows-specific methods
    Expected<bool, IPCError> setupNamedPipe(const QString& pipeName);
    Expected<bool, IPCError> configureSecurity(const QString& securityDescriptor);
    Expected<bool, IPCError> enableLowIntegrityLevel(bool enabled);
    Expected<bool, IPCError> setAccessControl(const QStringList& allowedUsers);

private slots:
    void handlePipeConnection();
    void handlePipeDisconnection();
    void handlePipeMessage(const QByteArray& data);
    void handlePipeError(const QString& error);

private:
    class WindowsIPCPrivate;
    std::unique_ptr<WindowsIPCPrivate> d;

    // Platform-specific helpers
    Expected<bool, IPCError> createNamedPipeServer();
    Expected<bool, IPCError> connectToNamedPipe();
    Expected<bool, IPCError> setupPipeSecurity();
    Expected<QByteArray, IPCError> encryptForPipe(const QByteArray& data);
    Expected<QByteArray, IPCError> decryptFromPipe(const QByteArray& encryptedData);
    void cleanupPipeResources();
    bool validatePipePermissions() const;
};

} // namespace Murmur