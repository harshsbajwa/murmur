#include "MacOSIPC.hpp"
#include "core/common/Logger.hpp"

#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QRandomGenerator>

// macOS-specific includes
#ifdef Q_OS_MACOS
#include <xpc/xpc.h>
#include <dispatch/dispatch.h>
#include <Security/Security.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace Murmur {

class MacOSIPC::MacOSIPCPrivate {
public:
    MacOSIPCPrivate() = default;
    ~MacOSIPCPrivate() = default;

#ifdef Q_OS_MACOS
    xpc_connection_t xpcConnection = nullptr;
    dispatch_queue_t xpcQueue = nullptr;
    xpc_connection_t xpcService = nullptr;
    
    // Client connection tracking for server-to-client messaging
    struct ClientConnection {
        xpc_connection_t connection;
        QString clientId;
        QDateTime connectedAt;
        QString securityToken;
    };
    QHash<QString, ClientConnection> trackedClients;
    QMutex clientsMutex;
#endif
    
    QString serviceName;
    bool sandboxEnabled = false;
    QStringList requiredEntitlements;
    bool isInitialized = false;
    bool isServerMode = false;
    
    // Security context
    QString securityToken;
    QByteArray encryptionKey;
    
    // Connection management
    QHash<QString, QDateTime> clientConnections;
    QElapsedTimer connectionTimer;
};

MacOSIPC::MacOSIPC(QObject* parent)
    : SecureIPC(parent)
    , d(std::make_unique<MacOSIPCPrivate>())
{
#ifdef Q_OS_MACOS
    d->xpcQueue = dispatch_queue_create("com.murmur.ipc", DISPATCH_QUEUE_SERIAL);
    d->connectionTimer.start();
    
    Logger::instance().info("MacOSIPC: Initialized with XPC support");
#else
    Logger::instance().warn("MacOSIPC: XPC support not available on this platform");
#endif
}

MacOSIPC::~MacOSIPC() {
    shutdown();
}

Expected<bool, IPCError> MacOSIPC::initializeServer(const QString& serverName) {
    d->serviceName = serverName;
    d->isServerMode = true;
    
#ifdef Q_OS_MACOS
    // Create XPC service
    QString xpcServiceName = QString("com.murmur.desktop.%1").arg(serverName);
    
    d->xpcService = xpc_connection_create_mach_service(
        xpcServiceName.toUtf8().constData(),
        d->xpcQueue,
        XPC_CONNECTION_MACH_SERVICE_LISTENER
    );
    
    if (!d->xpcService) {
        Logger::instance().error("MacOSIPC: Failed to create XPC service: {}", xpcServiceName.toStdString());
        return makeUnexpected(IPCError::InitializationFailed);
    }
    
    // Set up event handler
    xpc_connection_set_event_handler(d->xpcService, ^(xpc_object_t event) {
        xpc_type_t type = xpc_get_type(event);
        
        if (type == XPC_TYPE_CONNECTION) {
            // New client connection - track it for server-to-client messaging
            xpc_connection_t client_connection = static_cast<xpc_connection_t>(event);
            QString clientId = QString("client_%1_%2")
                              .arg(QDateTime::currentMSecsSinceEpoch())
                              .arg(reinterpret_cast<quintptr>(client_connection), 16);
            
            QMetaObject::invokeMethod(this, [this, client_connection, clientId]() {
                handleXPCConnection(client_connection, clientId);
            }, Qt::QueuedConnection);
        } else if (type == XPC_TYPE_ERROR) {
            const char* error_desc = xpc_dictionary_get_string(event, XPC_ERROR_KEY_DESCRIPTION);
            QString errorString = QString::fromUtf8(error_desc ? error_desc : "Unknown XPC error");
            QMetaObject::invokeMethod(this, [this, errorString]() {
                handleXPCError(errorString);
            }, Qt::QueuedConnection);
        }
    });
    
    // Resume the connection to start accepting clients
    xpc_connection_resume(d->xpcService);
    
    d->isInitialized = true;
    Logger::instance().info("MacOSIPC: Server initialized successfully: {}", xpcServiceName.toStdString());
    
    return true;
#else
    // Fallback to local socket for non-macOS platforms
    return SecureIPC::initializeServer(serverName);
#endif
}

Expected<bool, IPCError> MacOSIPC::initializeClient(const QString& serverName) {
    d->serviceName = serverName;
    d->isServerMode = false;
    
#ifdef Q_OS_MACOS
    QString xpcServiceName = QString("com.murmur.desktop.%1").arg(serverName);
    
    d->xpcConnection = xpc_connection_create_mach_service(
        xpcServiceName.toUtf8().constData(),
        d->xpcQueue,
        0
    );
    
    if (!d->xpcConnection) {
        Logger::instance().error("MacOSIPC: Failed to create XPC connection to: {}", xpcServiceName.toStdString());
        return makeUnexpected(IPCError::ConnectionFailed);
    }
    
    // Set up event handler
    xpc_connection_set_event_handler(d->xpcConnection, ^(xpc_object_t event) {
        xpc_type_t type = xpc_get_type(event);
        
        if (type == XPC_TYPE_DICTIONARY) {
            // Message received
            size_t length = 0;
            const void* data = xpc_dictionary_get_data(event, "payload", &length);
            if (data && length > 0) {
                QByteArray messageData(static_cast<const char*>(data), static_cast<int>(length));
                QMetaObject::invokeMethod(this, [this, messageData]() {
                    handleXPCMessage(messageData);
                }, Qt::QueuedConnection);
            }
        } else if (type == XPC_TYPE_ERROR) {
            if (event == XPC_ERROR_CONNECTION_INTERRUPTED) {
                QMetaObject::invokeMethod(this, &MacOSIPC::handleXPCDisconnection, Qt::QueuedConnection);
            } else if (event == XPC_ERROR_CONNECTION_INVALID) {
                QString errorMsg = "XPC connection became invalid";
                QMetaObject::invokeMethod(this, [this, errorMsg]() {
                    handleXPCError(errorMsg);
                }, Qt::QueuedConnection);
            }
        }
    });
    
    // Resume the connection
    xpc_connection_resume(d->xpcConnection);
    
    d->isInitialized = true;
    Logger::instance().info("MacOSIPC: Client connected to: {}", xpcServiceName.toStdString());
    
    return true;
#else
    // Fallback to local socket for non-macOS platforms
    return SecureIPC::initializeClient(serverName);
#endif
}

Expected<bool, IPCError> MacOSIPC::sendMessage(const QString& clientId, const IPCMessage& message) {
    if (!d->isInitialized) {
        return makeUnexpected(IPCError::ServerNotRunning);
    }
    
#ifdef Q_OS_MACOS
    if (d->isServerMode) {
        // Server sending to specific client - use tracked client connections
        QMutexLocker locker(&d->clientsMutex);
        auto clientIt = d->trackedClients.find(clientId);
        if (clientIt == d->trackedClients.end()) {
            Logger::instance().warn("MacOSIPC: Client not found: {}", clientId.toStdString());
            return makeUnexpected(IPCError::ClientNotConnected);
        }
        
        xpc_connection_t clientConnection = clientIt->connection;
        if (!clientConnection) {
            Logger::instance().error("MacOSIPC: Invalid client connection for: {}", clientId.toStdString());
            return makeUnexpected(IPCError::ClientNotConnected);
        }
        
        // Serialize message
        auto serializeResult = serializeMessage(message);
        if (serializeResult.hasError()) {
            return makeUnexpected(serializeResult.error());
        }
        QByteArray serialized = serializeResult.value();
        if (serialized.isEmpty()) {
            return makeUnexpected(IPCError::InvalidMessage);
        }
        
        // Create XPC message
        xpc_object_t xpcMsg = xpc_dictionary_create(nullptr, nullptr, 0);
        xpc_dictionary_set_data(xpcMsg, "payload", serialized.constData(), serialized.size());
        xpc_dictionary_set_string(xpcMsg, "sender", "server");
        xpc_dictionary_set_string(xpcMsg, "target", clientId.toUtf8().constData());
        xpc_dictionary_set_uint64(xpcMsg, "timestamp", message.timestamp);
        
        // Send message to specific client
        xpc_connection_send_message(clientConnection, xpcMsg);
        xpc_release(xpcMsg);
        
        Logger::instance().debug("MacOSIPC: Server message sent to client {}: {} bytes", clientId.toStdString(), serialized.size());
        return true;
    } else {
        // Client sending to server
        if (!d->xpcConnection) {
            return makeUnexpected(IPCError::ClientNotConnected);
        }
        
        // Serialize message
        auto serializeResult = serializeMessage(message);
        if (serializeResult.hasError()) {
            return makeUnexpected(serializeResult.error());
        }
        QByteArray serialized = serializeResult.value();
        if (serialized.isEmpty()) {
            return makeUnexpected(IPCError::InvalidMessage);
        }
        
        // Create XPC message
        xpc_object_t xpcMsg = xpc_dictionary_create(nullptr, nullptr, 0);
        xpc_dictionary_set_data(xpcMsg, "payload", serialized.constData(), serialized.size());
        xpc_dictionary_set_string(xpcMsg, "sender", clientId.toUtf8().constData());
        xpc_dictionary_set_uint64(xpcMsg, "timestamp", message.timestamp);
        
        // Send message
        xpc_connection_send_message(d->xpcConnection, xpcMsg);
        xpc_release(xpcMsg);
        
        Logger::instance().debug("MacOSIPC: Message sent via XPC: {} bytes", serialized.size());
        return true;
    }
#else
    // Fallback to base implementation
    return SecureIPC::sendMessage(clientId, message);
#endif
}

Expected<bool, IPCError> MacOSIPC::broadcastMessage(const IPCMessage& message) {
    if (!d->isInitialized || !d->isServerMode) {
        return makeUnexpected(IPCError::ServerNotRunning);
    }
    
#ifdef Q_OS_MACOS
    // Broadcast to all tracked client connections
    QMutexLocker locker(&d->clientsMutex);
    if (d->trackedClients.isEmpty()) {
        Logger::instance().info("MacOSIPC: No clients connected for broadcast");
        return true;
    }
    
    // Serialize message once
    auto serializeResult = serializeMessage(message);
    if (serializeResult.hasError()) {
        return makeUnexpected(serializeResult.error());
    }
    QByteArray serialized = serializeResult.value();
    if (serialized.isEmpty()) {
        return makeUnexpected(IPCError::InvalidMessage);
    }
    
    // Send to all connected clients
    int successCount = 0;
    for (auto it = d->trackedClients.begin(); it != d->trackedClients.end(); ++it) {
        const auto& client = it.value();
        if (client.connection) {
            // Create XPC message
            xpc_object_t xpcMsg = xpc_dictionary_create(nullptr, nullptr, 0);
            xpc_dictionary_set_data(xpcMsg, "payload", serialized.constData(), serialized.size());
            xpc_dictionary_set_string(xpcMsg, "sender", "server");
            xpc_dictionary_set_string(xpcMsg, "target", "broadcast");
            xpc_dictionary_set_uint64(xpcMsg, "timestamp", message.timestamp);
            
            // Send message
            xpc_connection_send_message(client.connection, xpcMsg);
            xpc_release(xpcMsg);
            successCount++;
        }
    }
    
    Logger::instance().info("MacOSIPC: Broadcast sent to {} clients: {} bytes", successCount, serialized.size());
    return true;
#else
    auto serializeResult = serializeMessage(message);
    if (serializeResult.hasError()) {
        return makeUnexpected(serializeResult.error());
    }
    auto broadcastResult = SecureIPC::broadcastMessage(serializeResult.value());
    if (broadcastResult.hasError()) {
        return makeUnexpected(broadcastResult.error());
    }
    return true;
#endif
}

Expected<bool, IPCError> MacOSIPC::shutdown() {
    if (!d->isInitialized) {
        return true;
    }
    
#ifdef Q_OS_MACOS
    cleanupXPCResources();
#endif
    
    d->isInitialized = false;
    Logger::instance().info("MacOSIPC: Shutdown completed");
    
    return true;
}

bool MacOSIPC::isServerRunning() const {
    return d->isInitialized && d->isServerMode;
}

bool MacOSIPC::isConnected() const {
    return d->isInitialized;
}

QStringList MacOSIPC::getConnectedClients() const {
    return d->clientConnections.keys();
}

Expected<bool, IPCError> MacOSIPC::setupXPCService(const QString& serviceName) {
    d->serviceName = serviceName;
    
#ifdef Q_OS_MACOS
    // Validate service name format
    if (!serviceName.startsWith("com.murmur.")) {
        Logger::instance().warn("MacOSIPC: Service name should follow reverse DNS format");
    }
    
    // Check if we have necessary entitlements
    if (d->sandboxEnabled && d->requiredEntitlements.isEmpty()) {
        Logger::instance().warn("MacOSIPC: Sandbox enabled but no entitlements configured");
    }
    
    Logger::instance().info("MacOSIPC: XPC service configured: {}", serviceName.toStdString());
    return true;
#else
    Logger::instance().warn("MacOSIPC: XPC service setup not available on this platform");
    return makeUnexpected(IPCError::InitializationFailed);
#endif
}

Expected<bool, IPCError> MacOSIPC::enableSandboxSupport(bool enabled) {
    d->sandboxEnabled = enabled;
    
#ifdef Q_OS_MACOS
    if (enabled) {
        // Verify we're running in a sandboxed environment
        char* sandbox_check = getenv("APP_SANDBOX_CONTAINER_ID");
        if (!sandbox_check) {
            Logger::instance().warn("MacOSIPC: Sandbox support enabled but app doesn't appear to be sandboxed");
        }
        
        Logger::instance().info("MacOSIPC: Sandbox support enabled");
    } else {
        Logger::instance().info("MacOSIPC: Sandbox support disabled");
    }
    
    return true;
#else
    Logger::instance().warn("MacOSIPC: Sandbox support not available on this platform");
    return enabled ? makeUnexpected(IPCError::InitializationFailed) : Expected<bool, IPCError>(true);
#endif
}

Expected<bool, IPCError> MacOSIPC::configureEntitlements(const QStringList& entitlements) {
    d->requiredEntitlements = entitlements;
    
#ifdef Q_OS_MACOS
    Logger::instance().info("MacOSIPC: Configured {} entitlements", entitlements.size());
    for (const QString& entitlement : entitlements) {
        Logger::instance().debug("MacOSIPC: Required entitlement: {}", entitlement.toStdString());
    }
    
    return true;
#else
    Logger::instance().warn("MacOSIPC: Entitlements not supported on this platform");
    return true;
#endif
}

void MacOSIPC::handleXPCConnection() {
    QString clientId = QString("client_%1").arg(d->clientConnections.size());
    d->clientConnections[clientId] = QDateTime::currentDateTime();
    
    emit clientConnected(clientId);
    Logger::instance().info("MacOSIPC: New XPC client connected: {}", clientId.toStdString());
}

void MacOSIPC::handleXPCConnection(xpc_connection_t client_connection, const QString& clientId) {
#ifdef Q_OS_MACOS
    // Set up event handler for this specific client connection
    xpc_connection_set_event_handler(client_connection, ^(xpc_object_t event) {
        xpc_type_t type = xpc_get_type(event);
        
        if (type == XPC_TYPE_DICTIONARY) {
            // Message received from client
            size_t length = 0;
            const void* data = xpc_dictionary_get_data(event, "payload", &length);
            if (data && length > 0) {
                QByteArray messageData(static_cast<const char*>(data), static_cast<int>(length));
                const char* sender = xpc_dictionary_get_string(event, "sender");
                QString senderId = sender ? QString::fromUtf8(sender) : clientId;
                
                QMetaObject::invokeMethod(this, [this, messageData, senderId]() {
                    handleXPCClientMessage(messageData, senderId);
                }, Qt::QueuedConnection);
            }
        } else if (type == XPC_TYPE_ERROR) {
            if (event == XPC_ERROR_CONNECTION_INTERRUPTED || event == XPC_ERROR_CONNECTION_INVALID) {
                QMetaObject::invokeMethod(this, [this, clientId]() {
                    handleXPCClientDisconnection(clientId);
                }, Qt::QueuedConnection);
            }
        }
    });
    
    // Resume the client connection
    xpc_connection_resume(client_connection);
    
    // Track this client connection
    {
        QMutexLocker locker(&d->clientsMutex);
        MacOSIPCPrivate::ClientConnection clientConn;
        clientConn.connection = client_connection;
        clientConn.clientId = clientId;
        clientConn.connectedAt = QDateTime::currentDateTime();
        clientConn.securityToken = generateClientToken(clientId);
        
        d->trackedClients[clientId] = clientConn;
    }
    
    d->clientConnections[clientId] = QDateTime::currentDateTime();
    emit clientConnected(clientId);
    
    Logger::instance().info("MacOSIPC: Client connection established and tracked: {}", clientId.toStdString());
#endif
}

void MacOSIPC::handleXPCDisconnection() {
    emit clientDisconnected("unknown"); // XPC doesn't easily provide client ID on disconnection
    Logger::instance().info("MacOSIPC: XPC client disconnected");
}

void MacOSIPC::handleXPCMessage(const QByteArray& data) {
    auto parseResult = deserializeMessage(data);
    if (parseResult.hasValue()) {
        IPCMessage msg = parseResult.value();
        emit messageReceived("server", data, IPCMessageType::Data);
        Logger::instance().debug("MacOSIPC: Received XPC message: {} bytes", data.size());
    } else {
        Logger::instance().error("MacOSIPC: Failed to parse XPC message");
    }
}

void MacOSIPC::handleXPCError(const QString& error) {
    Logger::instance().error("MacOSIPC: XPC error: {}", error.toStdString());
    emit messageError(error);
}

Expected<bool, IPCError> MacOSIPC::validateXPCConnection() const {
#ifdef Q_OS_MACOS
    if (!d->xpcConnection && !d->xpcService) {
        return makeUnexpected(IPCError::ClientNotConnected);
    }
    
    // Additional validation could check connection state
    return true;
#else
    return makeUnexpected(IPCError::InitializationFailed);
#endif
}

Expected<QByteArray, IPCError> MacOSIPC::encryptForXPC(const QByteArray& data) {
    // Use base class encryption with session key
    return encryptMessage(data, d->encryptionKey);
}

Expected<QByteArray, IPCError> MacOSIPC::decryptFromXPC(const QByteArray& encryptedData) {
    // Use base class decryption with session key
    return decryptMessage(encryptedData, d->encryptionKey);
}

bool MacOSIPC::setupXPCSecurityContext() {
#ifdef Q_OS_MACOS
    // Set up secure context for XPC communication
    d->securityToken = QString("xpc_token_%1_%2")
                      .arg(QCoreApplication::applicationPid())
                      .arg(QDateTime::currentMSecsSinceEpoch());
    
    // Generate encryption key for this session
    auto keyResult = generateKey();
    if (keyResult.hasValue()) {
        d->encryptionKey = keyResult.value();
    } else {
        // Generate a basic key if the secure method fails
        d->encryptionKey = QByteArray(32, 0);
        for (int i = 0; i < 32; ++i) {
            d->encryptionKey[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
        }
    }
    
    Logger::instance().info("MacOSIPC: Security context established");
    return true;
#else
    return false;
#endif
}

void MacOSIPC::handleXPCClientMessage(const QByteArray& data, const QString& senderId) {
    auto parseResult = deserializeMessage(data);
    if (parseResult.hasValue()) {
        IPCMessage msg = parseResult.value();
        emit messageReceived(senderId, data, IPCMessageType::Data);
        Logger::instance().debug("MacOSIPC: Received message from client {}: {} bytes", senderId.toStdString(), data.size());
    } else {
        Logger::instance().error("MacOSIPC: Failed to parse message from client: {}", senderId.toStdString());
    }
}

void MacOSIPC::handleXPCClientDisconnection(const QString& clientId) {
#ifdef Q_OS_MACOS
    {
        QMutexLocker locker(&d->clientsMutex);
        auto clientIt = d->trackedClients.find(clientId);
        if (clientIt != d->trackedClients.end()) {
            // Clean up the client connection
            if (clientIt->connection) {
                xpc_connection_cancel(clientIt->connection);
                // Note: Don't release here as XPC manages the lifecycle
            }
            d->trackedClients.erase(clientIt);
        }
    }
    
    d->clientConnections.remove(clientId);
    emit clientDisconnected(clientId);
    Logger::instance().info("MacOSIPC: Client disconnected and cleaned up: {}", clientId.toStdString());
#endif
}

QString MacOSIPC::generateClientToken(const QString& clientId) {
    return QString("token_%1_%2_%3")
           .arg(clientId)
           .arg(QDateTime::currentMSecsSinceEpoch())
           .arg(QRandomGenerator::global()->generate(), 8, 16, QChar('0'));
}

void MacOSIPC::cleanupXPCResources() {
#ifdef Q_OS_MACOS
    // Clean up tracked client connections
    {
        QMutexLocker locker(&d->clientsMutex);
        for (auto it = d->trackedClients.begin(); it != d->trackedClients.end(); ++it) {
            if (it->connection) {
                xpc_connection_cancel(it->connection);
                // Note: XPC manages connection lifecycle, we don't release
            }
        }
        d->trackedClients.clear();
    }
    
    if (d->xpcConnection) {
        xpc_connection_cancel(d->xpcConnection);
        xpc_release(d->xpcConnection);
        d->xpcConnection = nullptr;
    }
    
    if (d->xpcService) {
        xpc_connection_cancel(d->xpcService);
        xpc_release(d->xpcService);
        d->xpcService = nullptr;
    }
    
    if (d->xpcQueue) {
        dispatch_release(d->xpcQueue);
        d->xpcQueue = nullptr;
    }
    
    Logger::instance().info("MacOSIPC: XPC resources cleaned up");
#endif
}

} // namespace Murmur