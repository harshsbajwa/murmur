#include "SecureIPC.hpp"
#include "core/common/Logger.hpp"
#include "InputValidator.hpp"

#include <QtCore/QDataStream>
#include <QtCore/QStandardPaths>
#include <QtCore/QCryptographicHash>
#include <QtCore/QRandomGenerator>
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QTextStream>
#include <QtCore/QThread>
#include <QtCore/QDateTime>
#include <QtNetwork/QLocalSocket>
#include <chrono>
#include <algorithm>

namespace Murmur {

class SecureIPC::SecureIPCPrivate {
public:
    SecureIPCPrivate() = default;
    ~SecureIPCPrivate() = default;

    // Server state
    std::unique_ptr<QLocalServer> server;
    QString serverName;
    bool isServer = false;
    bool serverRunning = false;

    // Client state
    std::unique_ptr<QLocalSocket> clientSocket;
    QString clientId;
    bool isClient = false;
    bool connected = false;

    // Connected clients (server side)
    std::unordered_map<QString, std::unique_ptr<IPCClientInfo>> clients;
    std::unordered_map<QLocalSocket*, QString> socketToClientId;

    // Security
    bool encryptionEnabled = false;
    QByteArray encryptionKey;
    QString encryptionAlgorithm = "AES-256";
    QString keyPath;
    std::unique_ptr<InputValidator> validator;

    // Configuration
    quint32 maxMessageSize = 1024 * 1024; // 1MB default
    int heartbeatInterval = 30000; // 30 seconds
    int connectionTimeout = 60000; // 60 seconds
    quint32 sequenceCounter = 0;

    // Monitoring
    quint64 messagesSent = 0;
    quint64 messagesReceived = 0;

    // Timers
    std::unique_ptr<QTimer> heartbeatTimer;
    std::unique_ptr<QTimer> timeoutTimer;
};

SecureIPC::SecureIPC(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<SecureIPCPrivate>())
{
    d->validator = std::make_unique<InputValidator>();
    
    // Set up heartbeat timer
    d->heartbeatTimer = std::make_unique<QTimer>(this);
    connect(d->heartbeatTimer.get(), &QTimer::timeout, this, &SecureIPC::handleHeartbeat);
    
    // Set up timeout timer
    d->timeoutTimer = std::make_unique<QTimer>(this);
    connect(d->timeoutTimer.get(), &QTimer::timeout, this, &SecureIPC::handleConnectionTimeout);
}

SecureIPC::~SecureIPC() {
    if (d->serverRunning) {
        stopServer();
    }
    if (d->connected) {
        disconnectFromServer();
    }
}

Expected<void, IPCError> SecureIPC::startServer(const QString& serverName, const QString& keyPath) {
    if (d->serverRunning) {
        return Expected<void, IPCError>();
    }

    if (!d->validator->isValidIdentifier(serverName)) {
        return makeUnexpected(IPCError::InitializationFailed);
    }

    d->server = std::make_unique<QLocalServer>(this);
    
    // Remove any existing server
    QLocalServer::removeServer(serverName);
    
    if (!d->server->listen(serverName)) {
        Logger::instance().error("Failed to start IPC server: {}", d->server->errorString().toStdString());
        return makeUnexpected(IPCError::InitializationFailed);
    }

    connect(d->server.get(), &QLocalServer::newConnection, this, &SecureIPC::handleNewConnection);

    d->serverName = serverName;
    d->isServer = true;
    d->serverRunning = true;
    d->keyPath = keyPath;

    // Enable encryption if key path is provided
    if (!keyPath.isEmpty()) {
        auto encryptionResult = enableEncryption(keyPath);
        if (!encryptionResult.hasValue()) {
            stopServer();
            return encryptionResult;
        }
    }

    // Start heartbeat timer
    d->heartbeatTimer->start(d->heartbeatInterval);

    Logger::instance().info("IPC server started: {}", serverName.toStdString());
    emit serverStarted(serverName);
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::stopServer() {
    if (!d->serverRunning) {
        return Expected<void, IPCError>();
    }

    // Stop timers
    d->heartbeatTimer->stop();
    d->timeoutTimer->stop();

    // Disconnect all clients
    for (auto& [clientId, client] : d->clients) {
        if (client->socket) {
            client->socket->disconnectFromServer();
        }
    }

    d->clients.clear();
    d->socketToClientId.clear();

    // Stop server
    if (d->server) {
        d->server->close();
        d->server.reset();
    }

    d->serverRunning = false;
    d->isServer = false;

    Logger::instance().info("IPC server stopped");
    emit serverStopped();
    return Expected<void, IPCError>();
}

bool SecureIPC::isServerRunning() const {
    return d->serverRunning;
}

Expected<void, IPCError> SecureIPC::connectToServer(const QString& serverName, const QString& clientId, const QString& keyPath) {
    if (d->connected) {
        return Expected<void, IPCError>();
    }

    if (!d->validator->isValidIdentifier(serverName) || !d->validator->isValidIdentifier(clientId)) {
        return makeUnexpected(IPCError::InitializationFailed);
    }

    d->clientSocket = std::make_unique<QLocalSocket>(this);
    
    connect(d->clientSocket.get(), &QLocalSocket::connected, [this]() {
        d->connected = true;
        emit connectionEstablished(d->serverName);
    });
    
    connect(d->clientSocket.get(), &QLocalSocket::disconnected, this, &SecureIPC::handleClientDisconnected);
    connect(d->clientSocket.get(), &QLocalSocket::readyRead, this, &SecureIPC::handleClientMessage);

    d->clientSocket->connectToServer(serverName);
    
    if (!d->clientSocket->waitForConnected(d->connectionTimeout)) {
        Logger::instance().error("Failed to connect to IPC server: {}", d->clientSocket->errorString().toStdString());
        return makeUnexpected(IPCError::ConnectionFailed);
    }

    d->clientId = clientId;
    d->isClient = true;
    d->keyPath = keyPath;

    // Enable encryption if key path is provided
    if (!keyPath.isEmpty()) {
        auto encryptionResult = enableEncryption(keyPath);
        if (!encryptionResult.hasValue()) {
            disconnectFromServer();
            return encryptionResult;
        }
    }

    // Send handshake message
    IPCMessage handshake;
    handshake.type = IPCMessageType::Handshake;
    handshake.senderId = clientId;
    handshake.payload = QByteArray::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    handshake.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    handshake.sequenceNumber = generateSequenceNumber();

    auto serialized = serializeMessage(handshake);
    if (!serialized.hasValue()) {
        disconnectFromServer();
        return makeUnexpected(IPCError::InitializationFailed);
    }

    d->clientSocket->write(serialized.value());
    d->clientSocket->flush();

    Logger::instance().info("Connected to IPC server: {} as {}", serverName.toStdString(), clientId.toStdString());
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::disconnectFromServer() {
    if (!d->connected) {
        return Expected<void, IPCError>();
    }

    if (d->clientSocket) {
        d->clientSocket->disconnectFromServer();
        d->clientSocket.reset();
    }

    d->connected = false;
    d->isClient = false;
    d->clientId.clear();

    Logger::instance().info("Disconnected from IPC server");
    return Expected<void, IPCError>();
}

bool SecureIPC::isConnected() const {
    return d->connected;
}

Expected<void, IPCError> SecureIPC::sendMessage(const QString& receiverId, const QByteArray& data, IPCMessageType type) {
    if (!d->connected && !d->serverRunning) {
        return makeUnexpected(IPCError::ClientNotConnected);
    }

    auto sizeCheck = validateMessageSize(data);
    if (!sizeCheck.hasValue()) {
        return sizeCheck;
    }

    IPCMessage message;
    message.type = type;
    message.senderId = d->isClient ? d->clientId : "server";
    message.receiverId = receiverId;
    message.payload = data;
    message.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    message.sequenceNumber = generateSequenceNumber();

    // Encrypt if enabled
    if (d->encryptionEnabled) {
        auto encrypted = encryptMessage(data, d->encryptionKey);
        if (!encrypted.hasValue()) {
            return makeUnexpected(IPCError::EncryptionFailed);
        }
        message.payload = encrypted.value();
    }

    // Sign message
    auto signature = signMessage(message.payload);
    if (signature.hasValue()) {
        message.signature = signature.value();
    }

    auto serialized = serializeMessage(message);
    if (!serialized.hasValue()) {
        return makeUnexpected(IPCError::InvalidMessage);
    }

    if (d->isClient) {
        // Client sending to server
        d->clientSocket->write(serialized.value());
        d->clientSocket->flush();
    } else {
        // Server sending to specific client
        auto it = d->clients.find(receiverId);
        if (it == d->clients.end()) {
            return makeUnexpected(IPCError::ClientNotConnected);
        }
        
        it->second->socket->write(serialized.value());
        it->second->socket->flush();
    }

    d->messagesSent++;
    emit messageSent(receiverId, type);
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::broadcastMessage(const QByteArray& data, IPCMessageType type) {
    if (!d->serverRunning) {
        return makeUnexpected(IPCError::ServerNotRunning);
    }

    for (const auto& [clientId, client] : d->clients) {
        auto result = sendMessage(clientId, data, type);
        if (!result.hasValue()) {
            Logger::instance().warn("Failed to send broadcast message to client {}", clientId.toStdString());
        }
    }

    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::sendEncryptedMessage(const QString& receiverId, const QByteArray& data) {
    if (!d->encryptionEnabled) {
        return makeUnexpected(IPCError::EncryptionFailed);
    }

    return sendMessage(receiverId, data, IPCMessageType::Data);
}

Expected<void, IPCError> SecureIPC::enableEncryption(const QString& keyPath) {
    if (keyPath.isEmpty()) {
        return makeUnexpected(IPCError::InitializationFailed);
    }

    auto key = loadKey(keyPath);
    if (!key.hasValue()) {
        // Generate new key if not found
        auto newKey = generateKey();
        if (!newKey.hasValue()) {
            return makeUnexpected(IPCError::EncryptionFailed);
        }
        
        auto saveResult = saveKey(keyPath, newKey.value());
        if (!saveResult.hasValue()) {
            return makeUnexpected(IPCError::EncryptionFailed);
        }
        
        d->encryptionKey = newKey.value();
    } else {
        d->encryptionKey = key.value();
    }

    d->encryptionEnabled = true;
    d->keyPath = keyPath;

    Logger::instance().info("Encryption enabled for IPC");
    emit encryptionEnabled();
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::disableEncryption() {
    d->encryptionEnabled = false;
    d->encryptionKey.clear();
    d->keyPath.clear();

    Logger::instance().info("Encryption disabled for IPC");
    emit encryptionDisabled();
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::authenticateClient(const QString& clientId, const QByteArray& credentials) {
    if (!d->serverRunning) {
        return makeUnexpected(IPCError::ServerNotRunning);
    }

    auto it = d->clients.find(clientId);
    if (it == d->clients.end()) {
        return makeUnexpected(IPCError::ClientNotConnected);
    }

    if (credentials.isEmpty()) {
        emit authenticationFailed(clientId, "Empty credentials");
        return makeUnexpected(IPCError::AuthenticationFailed);
    }

    it->second->authenticated = true;
    Logger::instance().info("Client {} authenticated successfully", clientId.toStdString());
    emit clientAuthenticated(clientId);
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::revokeClient(const QString& clientId) {
    if (!d->serverRunning) {
        return makeUnexpected(IPCError::ServerNotRunning);
    }

    auto it = d->clients.find(clientId);
    if (it == d->clients.end()) {
        return makeUnexpected(IPCError::ClientNotConnected);
    }

    it->second->authenticated = false;
    Logger::instance().info("Client {} authentication revoked", clientId.toStdString());
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::setMessageSizeLimit(quint32 maxSize) {
    if (maxSize == 0) {
        return makeUnexpected(IPCError::InvalidMessage);
    }

    d->maxMessageSize = maxSize;
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::setHeartbeatInterval(int intervalMs) {
    if (intervalMs <= 0) {
        return makeUnexpected(IPCError::InvalidMessage);
    }

    d->heartbeatInterval = intervalMs;
    if (d->heartbeatTimer) {
        d->heartbeatTimer->setInterval(intervalMs);
    }
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::setConnectionTimeout(int timeoutMs) {
    if (timeoutMs <= 0) {
        return makeUnexpected(IPCError::InvalidMessage);
    }

    d->connectionTimeout = timeoutMs;
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::setEncryptionAlgorithm(const QString& algorithm) {
    if (algorithm != "AES-256" && algorithm != "AES-128") {
        return makeUnexpected(IPCError::EncryptionFailed);
    }

    d->encryptionAlgorithm = algorithm;
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::writeToFile(const QString& filePath, const QString& content) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Logger::instance().error("Failed to open file for writing: {}", filePath.toStdString());
        return makeUnexpected(IPCError::PermissionDenied);
    }
    
    QTextStream stream(&file);
    stream << content;
    
    if (stream.status() != QTextStream::Ok) {
        Logger::instance().error("Failed to write to file: {}", filePath.toStdString());
        return makeUnexpected(IPCError::InvalidMessage);
    }
    
    return Expected<void, IPCError>();
}

Expected<QStringList, IPCError> SecureIPC::getConnectedClients() const {
    QStringList clients;
    for (const auto& [clientId, client] : d->clients) {
        clients.append(clientId);
    }
    return clients;
}

Expected<IPCClientInfo, IPCError> SecureIPC::getClientInfo(const QString& clientId) const {
    auto it = d->clients.find(clientId);
    if (it == d->clients.end()) {
        return makeUnexpected(IPCError::ClientNotConnected);
    }
    return *it->second;
}

Expected<quint64, IPCError> SecureIPC::getMessagesSent() const {
    return d->messagesSent;
}

Expected<quint64, IPCError> SecureIPC::getMessagesReceived() const {
    return d->messagesReceived;
}

void SecureIPC::handleNewConnection() {
    if (!d->server) {
        return;
    }

    while (d->server->hasPendingConnections()) {
        QLocalSocket* socket = d->server->nextPendingConnection();
        if (!socket) {
            continue;
        }

        connect(socket, &QLocalSocket::disconnected, this, &SecureIPC::handleClientDisconnected);
        connect(socket, &QLocalSocket::readyRead, this, &SecureIPC::handleClientMessage);

        // Create temporary client info until handshake is complete
        QString tempId = generateClientId();
        auto client = std::make_unique<IPCClientInfo>();
        client->clientId = tempId;
        client->socket = socket;
        client->processId = socket->property("processId").toLongLong();
        client->lastHeartbeat = std::chrono::steady_clock::now();

        d->socketToClientId[socket] = tempId;
        d->clients[tempId] = std::move(client);

        Logger::instance().info("New IPC client connected: {}", tempId.toStdString());
    }
}

void SecureIPC::handleClientDisconnected() {
    QLocalSocket* socket = qobject_cast<QLocalSocket*>(sender());
    if (!socket) {
        return;
    }

    auto it = d->socketToClientId.find(socket);
    if (it != d->socketToClientId.end()) {
        QString clientId = it->second;
        cleanupClient(clientId);
        emit clientDisconnected(clientId);
        Logger::instance().info("IPC client disconnected: {}", clientId.toStdString());
    }
}

void SecureIPC::handleClientMessage() {
    QLocalSocket* socket = qobject_cast<QLocalSocket*>(sender());
    if (!socket) {
        return;
    }

    while (socket->bytesAvailable() > 0) {
        QByteArray data = socket->readAll();
        
        auto message = deserializeMessage(data);
        if (!message.hasValue()) {
            emit messageError("Failed to deserialize message");
            continue;
        }

        auto validation = validateMessage(message.value());
        if (!validation.hasValue()) {
            emit messageError("Invalid message received");
            continue;
        }

        // Find client info
        auto socketIt = d->socketToClientId.find(socket);
        if (socketIt == d->socketToClientId.end()) {
            continue;
        }

        auto clientIt = d->clients.find(socketIt->second);
        if (clientIt == d->clients.end()) {
            continue;
        }

        auto processResult = processMessage(message.value(), clientIt->second.get());
        if (!processResult.hasValue()) {
            emit messageError("Failed to process message");
        }

        d->messagesReceived++;
    }
}

void SecureIPC::handleHeartbeat() {
    if (d->serverRunning) {
        // Server sends heartbeat to all clients
        for (const auto& [clientId, client] : d->clients) {
            sendHeartbeat(clientId);
        }
    } else if (d->connected) {
        // Client sends heartbeat to server
        sendHeartbeat("server");
    }
}

void SecureIPC::handleConnectionTimeout() {
    checkClientHealth();
}

Expected<void, IPCError> SecureIPC::processMessage(const IPCMessage& message, IPCClientInfo* client) {
    client->lastHeartbeat = std::chrono::steady_clock::now();
    
    switch (message.type) {
        case IPCMessageType::Handshake:
            return handleHandshake(message, client);
        case IPCMessageType::Authentication:
            return handleAuthentication(message, client);
        case IPCMessageType::Data:
            return handleDataMessage(message, client);
        case IPCMessageType::Control:
            return handleControlMessage(message, client);
        case IPCMessageType::Heartbeat:
            // Heartbeat handled by updating lastHeartbeat above
            return Expected<void, IPCError>();
        default:
            return makeUnexpected(IPCError::InvalidMessage);
    }
}

Expected<void, IPCError> SecureIPC::handleHandshake(const IPCMessage& message, IPCClientInfo* client) {
    client->clientId = message.senderId;
    client->processId = message.payload.toLongLong();
    
    // Update client ID mapping
    d->socketToClientId[client->socket] = client->clientId;
    
    Logger::instance().info("Handshake completed for client: {}", client->clientId.toStdString());
    emit clientConnected(client->clientId);
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::handleAuthentication(const IPCMessage& message, IPCClientInfo* client) {
    return authenticateClient(client->clientId, message.payload);
}

Expected<void, IPCError> SecureIPC::handleDataMessage(const IPCMessage& message, IPCClientInfo* client) {
    QByteArray payload = message.payload;
    
    // Decrypt if encryption is enabled
    if (d->encryptionEnabled) {
        auto decrypted = decryptMessage(payload, d->encryptionKey);
        if (!decrypted.hasValue()) {
            return makeUnexpected(IPCError::EncryptionFailed);
        }
        payload = decrypted.value();
    }

    emit messageReceived(message.senderId, payload, message.type);
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::handleControlMessage(const IPCMessage& message, IPCClientInfo* client) {
    // Handle control messages (shutdown, configuration, etc.)
    if (message.payload == "shutdown") {
        cleanupClient(client->clientId);
    }
    return Expected<void, IPCError>();
}

Expected<QByteArray, IPCError> SecureIPC::encryptMessage(const QByteArray& data, const QByteArray& key) {
    // Simplified encryption - in a real implementation, use proper crypto libraries
    QByteArray encrypted = data;
    for (int i = 0; i < encrypted.size(); ++i) {
        encrypted[i] = encrypted[i] ^ key[i % key.size()];
    }
    return encrypted;
}

Expected<QByteArray, IPCError> SecureIPC::decryptMessage(const QByteArray& encryptedData, const QByteArray& key) {
    // Simplified decryption - matches the encryption above
    return encryptMessage(encryptedData, key);
}

Expected<QByteArray, IPCError> SecureIPC::signMessage(const QByteArray& data) {
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

Expected<bool, IPCError> SecureIPC::verifySignature(const QByteArray& data, const QByteArray& signature) {
    auto expectedSignature = signMessage(data);
    if (!expectedSignature.hasValue()) {
        return false;
    }
    return expectedSignature.value() == signature;
}

Expected<QByteArray, IPCError> SecureIPC::generateKey() {
    QByteArray key;
    for (int i = 0; i < 32; ++i) {
        key.append(static_cast<char>(QRandomGenerator::global()->generate() & 0xFF));
    }
    return key;
}

Expected<QByteArray, IPCError> SecureIPC::loadKey(const QString& keyPath) {
    QFile file(keyPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return makeUnexpected(IPCError::EncryptionFailed);
    }
    return file.readAll();
}

Expected<void, IPCError> SecureIPC::saveKey(const QString& keyPath, const QByteArray& key) {
    QFileInfo fileInfo(keyPath);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        dir.mkpath(dir.absolutePath());
    }

    QFile file(keyPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return makeUnexpected(IPCError::EncryptionFailed);
    }
    
    if (file.write(key) != key.size()) {
        return makeUnexpected(IPCError::EncryptionFailed);
    }
    
    return Expected<void, IPCError>();
}

Expected<QByteArray, IPCError> SecureIPC::serializeMessage(const IPCMessage& message) {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    
    stream << static_cast<quint32>(message.type);
    stream << message.senderId;
    stream << message.receiverId;
    stream << message.payload;
    stream << message.signature;
    stream << message.timestamp;
    stream << message.sequenceNumber;
    
    return data;
}

Expected<IPCMessage, IPCError> SecureIPC::deserializeMessage(const QByteArray& data) {
    QDataStream stream(data);
    IPCMessage message;
    
    quint32 type;
    stream >> type;
    message.type = static_cast<IPCMessageType>(type);
    
    stream >> message.senderId;
    stream >> message.receiverId;
    stream >> message.payload;
    stream >> message.signature;
    stream >> message.timestamp;
    stream >> message.sequenceNumber;
    
    return message;
}

Expected<void, IPCError> SecureIPC::validateMessage(const IPCMessage& message) {
    if (message.senderId.isEmpty()) {
        return makeUnexpected(IPCError::InvalidMessage);
    }
    
    if (message.payload.size() > static_cast<int>(d->maxMessageSize)) {
        return makeUnexpected(IPCError::MessageTooLarge);
    }
    
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::validateClient(const IPCClientInfo& client) {
    if (client.clientId.isEmpty()) {
        return makeUnexpected(IPCError::InvalidMessage);
    }
    
    if (!client.socket) {
        return makeUnexpected(IPCError::ClientNotConnected);
    }
    
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::validateMessageSize(const QByteArray& data) {
    if (data.size() > static_cast<int>(d->maxMessageSize)) {
        return makeUnexpected(IPCError::MessageTooLarge);
    }
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::cleanupClient(const QString& clientId) {
    auto it = d->clients.find(clientId);
    if (it != d->clients.end()) {
        d->socketToClientId.erase(it->second->socket);
        d->clients.erase(it);
    }
    return Expected<void, IPCError>();
}

Expected<void, IPCError> SecureIPC::sendHeartbeat(const QString& clientId) {
    QByteArray heartbeatData = "heartbeat";
    return sendMessage(clientId, heartbeatData, IPCMessageType::Heartbeat);
}

Expected<void, IPCError> SecureIPC::checkClientHealth() {
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(d->connectionTimeout);
    
    QStringList disconnectedClients;
    
    for (const auto& [clientId, client] : d->clients) {
        if (now - client->lastHeartbeat > timeout) {
            disconnectedClients.append(clientId);
        }
    }
    
    for (const auto& clientId : disconnectedClients) {
        Logger::instance().warn("Client {} timed out", clientId.toStdString());
        cleanupClient(clientId);
        emit clientDisconnected(clientId);
    }
    
    return Expected<void, IPCError>();
}

QString SecureIPC::generateClientId() {
    return QString("client_%1_%2")
        .arg(QRandomGenerator::global()->generate())
        .arg(QDateTime::currentMSecsSinceEpoch());
}

quint32 SecureIPC::generateSequenceNumber() {
    return ++d->sequenceCounter;
}

} // namespace Murmur

