#include "LinuxIPC.hpp"
#include "core/common/Logger.hpp"

#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QSocketNotifier>

// Linux-specific includes
#ifdef Q_OS_LINUX
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <fcntl.h>
#include <poll.h>
#ifdef HAVE_SELINUX
#include <selinux/selinux.h>
#include <selinux/context.h>
#endif
#endif

namespace Murmur {

class LinuxIPC::LinuxIPCPrivate {
public:
    LinuxIPCPrivate() = default;
    ~LinuxIPCPrivate() = default;

#ifdef Q_OS_LINUX
    int serverSocket = -1;
    int clientSocket = -1;
    struct sockaddr_un serverAddr = {};
    struct sockaddr_un clientAddr = {};
#endif
    
    QString socketPath;
    mode_t socketPermissions = 0770;  // rwxrwx---
    QString socketGroup;
    bool selinuxEnabled = false;
    bool credentialPassingEnabled = true;
    bool isInitialized = false;
    bool isServerMode = false;
    
    // Security context
    QString securityToken;
    QByteArray encryptionKey;
    
    // Connection management
    QHash<QString, QDateTime> clientConnections;
    QHash<int, QString> socketToClientId;
    QElapsedTimer connectionTimer;
    
    // Socket notification
    QSocketNotifier* socketNotifier = nullptr;
};

LinuxIPC::LinuxIPC(QObject* parent)
    : SecureIPC(parent)
    , d(std::make_unique<LinuxIPCPrivate>())
{
#ifdef Q_OS_LINUX
    d->connectionTimer.start();
    
    // Check for SELinux availability
#ifdef HAVE_SELINUX
    if (is_selinux_enabled()) {
        d->selinuxEnabled = true;
        Logger::instance().info("LinuxIPC: SELinux detected and available");
    }
#endif
    
    Logger::instance().info("LinuxIPC: Initialized with Unix Domain Sockets support");
#else
    Logger::instance().warning("LinuxIPC: Unix Domain Sockets support not available on this platform");
#endif
}

LinuxIPC::~LinuxIPC() {
    shutdown();
}

Expected<bool, IPCError> LinuxIPC::initializeServer(const QString& serverName) {
    QString socketsDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (socketsDir.isEmpty()) {
        socketsDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    
    d->socketPath = QString("%1/murmur_%2.sock").arg(socketsDir, serverName);
    d->isServerMode = true;
    
#ifdef Q_OS_LINUX
    auto setupResult = createUnixSocketServer();
    if (setupResult.hasError()) {
        return setupResult;
    }
    
    d->isInitialized = true;
    Logger::instance().info("LinuxIPC: Server initialized successfully: {}", d->socketPath.toStdString());
    
    return true;
#else
    // Fallback to local socket for non-Linux platforms
    return SecureIPC::initializeServer(serverName);
#endif
}

Expected<bool, IPCError> LinuxIPC::initializeClient(const QString& serverName) {
    QString socketsDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (socketsDir.isEmpty()) {
        socketsDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    
    d->socketPath = QString("%1/murmur_%2.sock").arg(socketsDir, serverName);
    d->isServerMode = false;
    
#ifdef Q_OS_LINUX
    auto connectResult = connectToUnixSocket();
    if (connectResult.hasError()) {
        return connectResult;
    }
    
    d->isInitialized = true;
    Logger::instance().info("LinuxIPC: Client connected to: {}", d->socketPath.toStdString());
    
    return true;
#else
    // Fallback to local socket for non-Linux platforms
    return SecureIPC::initializeClient(serverName);
#endif
}

Expected<bool, IPCError> LinuxIPC::sendMessage(const QString& clientId, const IPCMessage& message) {
    if (!d->isInitialized) {
        return makeUnexpected(IPCError::ServerNotRunning);
    }
    
#ifdef Q_OS_LINUX
    int targetSocket = d->isServerMode ? -1 : d->clientSocket;
    
    if (d->isServerMode) {
        // Find client socket by ID
        for (auto it = d->socketToClientId.begin(); it != d->socketToClientId.end(); ++it) {
            if (it.value() == clientId) {
                targetSocket = it.key();
                break;
            }
        }
        
        if (targetSocket == -1) {
            return makeUnexpected(IPCError::ClientNotConnected);
        }
    }
    
    if (targetSocket == -1) {
        return makeUnexpected(IPCError::ClientNotConnected);
    }
    
    // Serialize message
    QByteArray serialized = serializeMessage(message);
    if (serialized.isEmpty()) {
        return makeUnexpected(IPCError::InvalidMessage);
    }
    
    // Encrypt message for socket transmission
    auto encryptResult = encryptForSocket(serialized);
    if (encryptResult.hasError()) {
        return makeUnexpected(encryptResult.error());
    }
    
    QByteArray encrypted = encryptResult.value();
    
    // Send message size first
    uint32_t messageSize = static_cast<uint32_t>(encrypted.size());
    ssize_t sent = send(targetSocket, &messageSize, sizeof(messageSize), MSG_NOSIGNAL);
    if (sent != sizeof(messageSize)) {
        Logger::instance().error("LinuxIPC: Failed to send message size: {}", errno);
        return makeUnexpected(IPCError::ConnectionFailed);
    }
    
    // Send message data
    sent = send(targetSocket, encrypted.constData(), messageSize, MSG_NOSIGNAL);
    if (sent != static_cast<ssize_t>(messageSize)) {
        Logger::instance().error("LinuxIPC: Failed to send message data: {}", errno);
        return makeUnexpected(IPCError::ConnectionFailed);
    }
    
    Logger::instance().debug("LinuxIPC: Message sent via Unix socket: {} bytes", encrypted.size());
    return true;
#else
    // Fallback to base implementation
    return SecureIPC::sendMessage(clientId, message);
#endif
}

Expected<bool, IPCError> LinuxIPC::broadcastMessage(const IPCMessage& message) {
    if (!d->isInitialized || !d->isServerMode) {
        return makeUnexpected(IPCError::ServerNotRunning);
    }
    
#ifdef Q_OS_LINUX
    // Send to all connected clients
    bool allSucceeded = true;
    
    for (const QString& clientId : d->socketToClientId.values()) {
        auto result = sendMessage(clientId, message);
        if (result.hasError()) {
            allSucceeded = false;
            Logger::instance().warning("LinuxIPC: Failed to broadcast to client: {}", clientId.toStdString());
        }
    }
    
    return allSucceeded;
#else
    return SecureIPC::broadcastMessage(message);
#endif
}

Expected<bool, IPCError> LinuxIPC::shutdown() {
    if (!d->isInitialized) {
        return true;
    }
    
#ifdef Q_OS_LINUX
    cleanupSocketResources();
#endif
    
    d->isInitialized = false;
    Logger::instance().info("LinuxIPC: Shutdown completed");
    
    return true;
}

bool LinuxIPC::isServerRunning() const {
    return d->isInitialized && d->isServerMode;
}

bool LinuxIPC::isConnected() const {
    return d->isInitialized;
}

QStringList LinuxIPC::getConnectedClients() const {
    return d->clientConnections.keys();
}

Expected<bool, IPCError> LinuxIPC::setupUnixSocket(const QString& socketPath) {
    d->socketPath = socketPath;
    
#ifdef Q_OS_LINUX
    // Validate socket path
    QFileInfo pathInfo(socketPath);
    if (!pathInfo.dir().exists()) {
        Logger::instance().error("LinuxIPC: Socket directory does not exist: {}", 
                                pathInfo.dir().path().toStdString());
        return makeUnexpected(IPCError::InitializationFailed);
    }
    
    Logger::instance().info("LinuxIPC: Unix socket configured: {}", socketPath.toStdString());
    return true;
#else
    Q_UNUSED(socketPath)
    Logger::instance().warning("LinuxIPC: Unix socket setup not available on this platform");
    return makeUnexpected(IPCError::InitializationFailed);
#endif
}

Expected<bool, IPCError> LinuxIPC::configureFilePermissions(mode_t permissions) {
    d->socketPermissions = permissions;
    
#ifdef Q_OS_LINUX
    Logger::instance().info("LinuxIPC: Socket permissions configured: {:o}", permissions);
    return true;
#else
    Q_UNUSED(permissions)
    Logger::instance().warning("LinuxIPC: File permissions not available on this platform");
    return true;
#endif
}

Expected<bool, IPCError> LinuxIPC::enableSELinuxSupport(bool enabled) {
    d->selinuxEnabled = enabled;
    
#ifdef Q_OS_LINUX
#ifdef HAVE_SELINUX
    if (enabled && !is_selinux_enabled()) {
        Logger::instance().warning("LinuxIPC: SELinux support requested but SELinux is not enabled");
        return makeUnexpected(IPCError::InitializationFailed);
    }
    
    if (enabled) {
        Logger::instance().info("LinuxIPC: SELinux support enabled");
    } else {
        Logger::instance().info("LinuxIPC: SELinux support disabled");
    }
    
    return true;
#else
    if (enabled) {
        Logger::instance().warning("LinuxIPC: SELinux support not compiled in");
        return makeUnexpected(IPCError::InitializationFailed);
    }
    return true;
#endif
#else
    Q_UNUSED(enabled)
    Logger::instance().warning("LinuxIPC: SELinux support not available on this platform");
    return enabled ? makeUnexpected(IPCError::InitializationFailed) : Expected<bool, IPCError>(true);
#endif
}

Expected<bool, IPCError> LinuxIPC::setSocketGroup(const QString& groupName) {
    d->socketGroup = groupName;
    
#ifdef Q_OS_LINUX
    // Validate that group exists
    struct group* grp = getgrnam(groupName.toLocal8Bit().constData());
    if (!grp) {
        Logger::instance().error("LinuxIPC: Group does not exist: {}", groupName.toStdString());
        return makeUnexpected(IPCError::PermissionDenied);
    }
    
    Logger::instance().info("LinuxIPC: Socket group configured: {} (gid: {})", 
                           groupName.toStdString(), grp->gr_gid);
    return true;
#else
    Q_UNUSED(groupName)
    Logger::instance().warning("LinuxIPC: Group configuration not available on this platform");
    return true;
#endif
}

Expected<bool, IPCError> LinuxIPC::enableCredentialPassing(bool enabled) {
    d->credentialPassingEnabled = enabled;
    
#ifdef Q_OS_LINUX
    Logger::instance().info("LinuxIPC: Credential passing {}", enabled ? "enabled" : "disabled");
    return true;
#else
    Logger::instance().warning("LinuxIPC: Credential passing not available on this platform");
    return true;
#endif
}

Expected<bool, IPCError> LinuxIPC::createUnixSocketServer() {
#ifdef Q_OS_LINUX
    // Remove existing socket file
    unlink(d->socketPath.toLocal8Bit().constData());
    
    // Create socket
    d->serverSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (d->serverSocket == -1) {
        Logger::instance().error("LinuxIPC: Failed to create socket: {}", errno);
        return makeUnexpected(IPCError::InitializationFailed);
    }
    
    // Enable credential passing if requested
    if (d->credentialPassingEnabled) {
        int optval = 1;
        if (setsockopt(d->serverSocket, SOL_SOCKET, SO_PASSCRED, &optval, sizeof(optval)) == -1) {
            Logger::instance().warning("LinuxIPC: Failed to enable credential passing: {}", errno);
        }
    }
    
    // Set up socket address
    memset(&d->serverAddr, 0, sizeof(d->serverAddr));
    d->serverAddr.sun_family = AF_UNIX;
    strncpy(d->serverAddr.sun_path, d->socketPath.toLocal8Bit().constData(), 
            sizeof(d->serverAddr.sun_path) - 1);
    
    // Bind socket
    if (bind(d->serverSocket, reinterpret_cast<struct sockaddr*>(&d->serverAddr), 
             sizeof(d->serverAddr)) == -1) {
        Logger::instance().error("LinuxIPC: Failed to bind socket: {}", errno);
        close(d->serverSocket);
        d->serverSocket = -1;
        return makeUnexpected(IPCError::InitializationFailed);
    }
    
    // Set up security
    auto securityResult = setupSocketSecurity();
    if (securityResult.hasError()) {
        return securityResult;
    }
    
    // Listen for connections
    if (listen(d->serverSocket, 5) == -1) {
        Logger::instance().error("LinuxIPC: Failed to listen on socket: {}", errno);
        close(d->serverSocket);
        d->serverSocket = -1;
        return makeUnexpected(IPCError::InitializationFailed);
    }
    
    // Set up socket notifier for accepting connections
    d->socketNotifier = new QSocketNotifier(d->serverSocket, QSocketNotifier::Read, this);
    connect(d->socketNotifier, &QSocketNotifier::activated, this, &LinuxIPC::handleSocketConnection);
    
    Logger::instance().info("LinuxIPC: Unix socket server created successfully");
    return true;
#else
    return makeUnexpected(IPCError::InitializationFailed);
#endif
}

Expected<bool, IPCError> LinuxIPC::connectToUnixSocket() {
#ifdef Q_OS_LINUX
    // Create socket
    d->clientSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (d->clientSocket == -1) {
        Logger::instance().error("LinuxIPC: Failed to create client socket: {}", errno);
        return makeUnexpected(IPCError::ConnectionFailed);
    }
    
    // Set up socket address
    memset(&d->clientAddr, 0, sizeof(d->clientAddr));
    d->clientAddr.sun_family = AF_UNIX;
    strncpy(d->clientAddr.sun_path, d->socketPath.toLocal8Bit().constData(), 
            sizeof(d->clientAddr.sun_path) - 1);
    
    // Connect to server
    if (connect(d->clientSocket, reinterpret_cast<struct sockaddr*>(&d->clientAddr), 
                sizeof(d->clientAddr)) == -1) {
        Logger::instance().error("LinuxIPC: Failed to connect to socket: {}", errno);
        close(d->clientSocket);
        d->clientSocket = -1;
        return makeUnexpected(IPCError::ConnectionFailed);
    }
    
    // Validate peer credentials if enabled
    if (d->credentialPassingEnabled) {
        auto credResult = validatePeerCredentials(d->clientSocket);
        if (credResult.hasError()) {
            return credResult;
        }
    }
    
    Logger::instance().info("LinuxIPC: Connected to Unix socket successfully");
    return true;
#else
    return makeUnexpected(IPCError::ConnectionFailed);
#endif
}

Expected<bool, IPCError> LinuxIPC::setupSocketSecurity() {
#ifdef Q_OS_LINUX
    // Set file permissions
    if (chmod(d->socketPath.toLocal8Bit().constData(), d->socketPermissions) == -1) {
        Logger::instance().error("LinuxIPC: Failed to set socket permissions: {}", errno);
        return makeUnexpected(IPCError::PermissionDenied);
    }
    
    // Set group ownership if specified
    if (!d->socketGroup.isEmpty()) {
        struct group* grp = getgrnam(d->socketGroup.toLocal8Bit().constData());
        if (grp) {
            if (chown(d->socketPath.toLocal8Bit().constData(), -1, grp->gr_gid) == -1) {
                Logger::instance().warning("LinuxIPC: Failed to set socket group: {}", errno);
            }
        }
    }
    
#ifdef HAVE_SELINUX
    // Set SELinux context if enabled
    if (d->selinuxEnabled) {
        const char* context = "unconfined_u:object_r:user_tmp_t:s0";
        if (setfilecon(d->socketPath.toLocal8Bit().constData(), context) == -1) {
            Logger::instance().warning("LinuxIPC: Failed to set SELinux context: {}", errno);
        }
    }
#endif
    
    Logger::instance().info("LinuxIPC: Socket security configured");
    return true;
#else
    return true;
#endif
}

Expected<bool, IPCError> LinuxIPC::validatePeerCredentials(int socketFd) {
#ifdef Q_OS_LINUX
    struct ucred cred;
    socklen_t len = sizeof(cred);
    
    if (getsockopt(socketFd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == -1) {
        Logger::instance().error("LinuxIPC: Failed to get peer credentials: {}", errno);
        return makeUnexpected(IPCError::AuthenticationFailed);
    }
    
    // Basic validation - could be enhanced with more sophisticated checks
    if (cred.uid == getuid() || cred.uid == 0) {  // Same user or root
        Logger::instance().debug("LinuxIPC: Peer credentials validated: uid={}, gid={}, pid={}", 
                                cred.uid, cred.gid, cred.pid);
        return true;
    } else {
        Logger::instance().warning("LinuxIPC: Peer credentials rejected: uid={}", cred.uid);
        return makeUnexpected(IPCError::AuthenticationFailed);
    }
#else
    Q_UNUSED(socketFd)
    return true;
#endif
}

Expected<QByteArray, IPCError> LinuxIPC::encryptForSocket(const QByteArray& data) {
    // Use base class encryption, could add Linux-specific encryption here
    return encryptMessage(data);
}

Expected<QByteArray, IPCError> LinuxIPC::decryptFromSocket(const QByteArray& encryptedData) {
    // Use base class decryption, could add Linux-specific decryption here
    return decryptMessage(encryptedData);
}

void LinuxIPC::cleanupSocketResources() {
#ifdef Q_OS_LINUX
    if (d->socketNotifier) {
        d->socketNotifier->setEnabled(false);
        delete d->socketNotifier;
        d->socketNotifier = nullptr;
    }
    
    if (d->serverSocket != -1) {
        close(d->serverSocket);
        d->serverSocket = -1;
    }
    
    if (d->clientSocket != -1) {
        close(d->clientSocket);
        d->clientSocket = -1;
    }
    
    // Remove socket file if we're the server
    if (d->isServerMode && !d->socketPath.isEmpty()) {
        unlink(d->socketPath.toLocal8Bit().constData());
    }
    
    Logger::instance().info("LinuxIPC: Unix socket resources cleaned up");
#endif
}

bool LinuxIPC::validateSocketPermissions() const {
#ifdef Q_OS_LINUX
    if (d->socketPath.isEmpty()) {
        return false;
    }
    
    struct stat st;
    if (stat(d->socketPath.toLocal8Bit().constData(), &st) == -1) {
        return false;
    }
    
    // Check if permissions match what we set
    return (st.st_mode & 0777) == d->socketPermissions;
#else
    return true;
#endif
}

void LinuxIPC::handleSocketConnection() {
#ifdef Q_OS_LINUX
    if (d->serverSocket == -1) {
        return;
    }
    
    int clientSocket = accept(d->serverSocket, nullptr, nullptr);
    if (clientSocket == -1) {
        Logger::instance().error("LinuxIPC: Failed to accept connection: {}", errno);
        return;
    }
    
    // Validate peer credentials if enabled
    if (d->credentialPassingEnabled) {
        auto credResult = validatePeerCredentials(clientSocket);
        if (credResult.hasError()) {
            close(clientSocket);
            return;
        }
    }
    
    QString clientId = QString("client_%1").arg(d->clientConnections.size());
    d->clientConnections[clientId] = QDateTime::currentDateTime();
    d->socketToClientId[clientSocket] = clientId;
    
    emit clientConnected(clientId);
    Logger::instance().info("LinuxIPC: New Unix socket client connected: {}", clientId.toStdString());
#endif
}

void LinuxIPC::handleSocketDisconnection() {
    emit clientDisconnected("unknown");
    Logger::instance().info("LinuxIPC: Unix socket client disconnected");
}

void LinuxIPC::handleSocketMessage(const QByteArray& data) {
    auto parseResult = deserializeMessage(data);
    if (parseResult.hasValue()) {
        emit messageReceived(parseResult.value());
        Logger::instance().debug("LinuxIPC: Received Unix socket message: {} bytes", data.size());
    } else {
        Logger::instance().error("LinuxIPC: Failed to parse Unix socket message");
    }
}

void LinuxIPC::handleSocketError(const QString& error) {
    Logger::instance().error("LinuxIPC: Unix socket error: {}", error.toStdString());
    emit errorOccurred(error);
}

} // namespace Murmur

#include "LinuxIPC.moc"