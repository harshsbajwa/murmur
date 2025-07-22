#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QByteArray>
#include <QtCore/QTimer>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>
#include <memory>
#include <functional>
#include <unordered_map>

#include "core/common/Expected.hpp"
#include "core/common/Logger.hpp"

namespace Murmur {

enum class IPCError {
    InitializationFailed,
    ConnectionFailed,
    AuthenticationFailed,
    EncryptionFailed,
    MessageTooLarge,
    InvalidMessage,
    ServerNotRunning,
    ClientNotConnected,
    TimeoutError,
    PermissionDenied
};

enum class IPCMessageType {
    Handshake,
    Authentication,
    Data,
    Control,
    Heartbeat,
    Shutdown
};

struct IPCMessage {
    IPCMessageType type;
    QString senderId;
    QString receiverId;
    QByteArray payload;
    QByteArray signature;
    quint64 timestamp;
    quint32 sequenceNumber;
};

struct IPCClientInfo {
    QString clientId;
    QString processName;
    qint64 processId;
    QLocalSocket* socket;
    bool authenticated = false;
    bool encrypted = false;
    QByteArray encryptionKey;
    quint32 lastSequenceNumber = 0;
    std::chrono::steady_clock::time_point lastHeartbeat;
};

class SecureIPC : public QObject {
    Q_OBJECT

public:
    explicit SecureIPC(QObject* parent = nullptr);
    ~SecureIPC() override;

    // Server operations
    Expected<void, IPCError> startServer(const QString& serverName, const QString& keyPath = QString());
    Expected<void, IPCError> stopServer();
    bool isServerRunning() const;

    // Client operations
    Expected<void, IPCError> connectToServer(const QString& serverName, const QString& clientId, const QString& keyPath = QString());
    Expected<void, IPCError> disconnectFromServer();
    bool isConnected() const;

    // Message operations
    Expected<void, IPCError> sendMessage(const QString& receiverId, const QByteArray& data, IPCMessageType type = IPCMessageType::Data);
    Expected<void, IPCError> broadcastMessage(const QByteArray& data, IPCMessageType type = IPCMessageType::Data);
    Expected<void, IPCError> sendEncryptedMessage(const QString& receiverId, const QByteArray& data);

    // Security operations
    Expected<void, IPCError> enableEncryption(const QString& keyPath);
    Expected<void, IPCError> disableEncryption();
    Expected<void, IPCError> authenticateClient(const QString& clientId, const QByteArray& credentials);
    Expected<void, IPCError> revokeClient(const QString& clientId);

    // Configuration
    Expected<void, IPCError> setMessageSizeLimit(quint32 maxSize);
    Expected<void, IPCError> setHeartbeatInterval(int intervalMs);
    Expected<void, IPCError> setConnectionTimeout(int timeoutMs);
    Expected<void, IPCError> setEncryptionAlgorithm(const QString& algorithm);
    
    // File operations
    Expected<void, IPCError> writeToFile(const QString& filePath, const QString& content);

    // Monitoring
    Expected<QStringList, IPCError> getConnectedClients() const;
    Expected<IPCClientInfo, IPCError> getClientInfo(const QString& clientId) const;
    Expected<quint64, IPCError> getMessagesSent() const;
    Expected<quint64, IPCError> getMessagesReceived() const;

signals:
    // Server signals
    void clientConnected(const QString& clientId);
    void clientDisconnected(const QString& clientId);
    void clientAuthenticated(const QString& clientId);
    void authenticationFailed(const QString& clientId, const QString& reason);

    // Message signals
    void messageReceived(const QString& senderId, const QByteArray& data, IPCMessageType type);
    void messageSent(const QString& receiverId, IPCMessageType type);
    void messageError(const QString& error);

    // Security signals
    void encryptionEnabled();
    void encryptionDisabled();
    void securityViolation(const QString& clientId, const QString& violation);

    // Status signals
    void serverStarted(const QString& serverName);
    void serverStopped();
    void connectionEstablished(const QString& serverName);
    void connectionLost(const QString& reason);

private slots:
    void handleNewConnection();
    void handleClientDisconnected();
    void handleClientMessage();
    void handleHeartbeat();
    void handleConnectionTimeout();

private:
    class SecureIPCPrivate;
    std::unique_ptr<SecureIPCPrivate> d;

    // Message handling
    Expected<void, IPCError> processMessage(const IPCMessage& message, IPCClientInfo* client);
    Expected<void, IPCError> handleHandshake(const IPCMessage& message, IPCClientInfo* client);
    Expected<void, IPCError> handleAuthentication(const IPCMessage& message, IPCClientInfo* client);
    Expected<void, IPCError> handleDataMessage(const IPCMessage& message, IPCClientInfo* client);
    Expected<void, IPCError> handleControlMessage(const IPCMessage& message, IPCClientInfo* client);

protected:
    // Serialization - accessible to derived classes
    Expected<QByteArray, IPCError> serializeMessage(const IPCMessage& message);
    Expected<IPCMessage, IPCError> deserializeMessage(const QByteArray& data);
    
    // Security - accessible to derived classes for platform-specific implementations
    Expected<QByteArray, IPCError> encryptMessage(const QByteArray& data, const QByteArray& key);
    Expected<QByteArray, IPCError> decryptMessage(const QByteArray& encryptedData, const QByteArray& key);
    Expected<QByteArray, IPCError> generateKey();

private:
    // Private security methods
    Expected<QByteArray, IPCError> signMessage(const QByteArray& data);
    Expected<bool, IPCError> verifySignature(const QByteArray& data, const QByteArray& signature);
    Expected<QByteArray, IPCError> loadKey(const QString& keyPath);
    Expected<void, IPCError> saveKey(const QString& keyPath, const QByteArray& key);
    
    // Private validation and utilities
    // Validation
    Expected<void, IPCError> validateMessage(const IPCMessage& message);
    Expected<void, IPCError> validateClient(const IPCClientInfo& client);
    Expected<void, IPCError> validateMessageSize(const QByteArray& data);

    // Utilities
    Expected<void, IPCError> cleanupClient(const QString& clientId);
    Expected<void, IPCError> sendHeartbeat(const QString& clientId);
    Expected<void, IPCError> checkClientHealth();
    QString generateClientId();
    quint32 generateSequenceNumber();
};

} // namespace Murmur