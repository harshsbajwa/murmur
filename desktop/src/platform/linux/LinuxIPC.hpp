#pragma once

#include "core/security/SecureIPC.hpp"
#include <QtCore/QObject>
#include <memory>

namespace Murmur {

/**
 * @brief Linux-specific IPC implementation using Unix Domain Sockets
 * 
 * Provides secure inter-process communication for Linux using Unix domain sockets
 * with file permissions and SELinux integration for enhanced security.
 */
class LinuxIPC : public SecureIPC {
    Q_OBJECT

public:
    explicit LinuxIPC(QObject* parent = nullptr);
    ~LinuxIPC() override;

    // SecureIPC interface implementation
    Expected<bool, IPCError> initializeServer(const QString& serverName) override;
    Expected<bool, IPCError> initializeClient(const QString& serverName) override;
    Expected<bool, IPCError> sendMessage(const QString& clientId, const IPCMessage& message) override;
    Expected<bool, IPCError> broadcastMessage(const IPCMessage& message) override;
    Expected<bool, IPCError> shutdown() override;
    
    bool isServerRunning() const override;
    bool isConnected() const override;
    QStringList getConnectedClients() const override;

    // Linux-specific methods
    Expected<bool, IPCError> setupUnixSocket(const QString& socketPath);
    Expected<bool, IPCError> configureFilePermissions(mode_t permissions);
    Expected<bool, IPCError> enableSELinuxSupport(bool enabled);
    Expected<bool, IPCError> setSocketGroup(const QString& groupName);
    Expected<bool, IPCError> enableCredentialPassing(bool enabled);

private slots:
    void handleSocketConnection();
    void handleSocketDisconnection();
    void handleSocketMessage(const QByteArray& data);
    void handleSocketError(const QString& error);

private:
    class LinuxIPCPrivate;
    std::unique_ptr<LinuxIPCPrivate> d;

    // Platform-specific helpers
    Expected<bool, IPCError> createUnixSocketServer();
    Expected<bool, IPCError> connectToUnixSocket();
    Expected<bool, IPCError> setupSocketSecurity();
    Expected<bool, IPCError> validatePeerCredentials(int socketFd);
    Expected<QByteArray, IPCError> encryptForSocket(const QByteArray& data);
    Expected<QByteArray, IPCError> decryptFromSocket(const QByteArray& encryptedData);
    void cleanupSocketResources();
    bool validateSocketPermissions() const;
};

} // namespace Murmur