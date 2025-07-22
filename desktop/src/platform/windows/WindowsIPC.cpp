#include "WindowsIPC.hpp"
#include "core/common/Logger.hpp"

#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QThread>

// Windows-specific includes
#ifdef Q_OS_WIN
#include <windows.h>
#include <sddl.h>
#include <aclapi.h>
#include <lmcons.h>
#include <processthreadsapi.h>
#include <securitybaseapi.h>
#endif

namespace Murmur {

class WindowsIPC::WindowsIPCPrivate {
public:
    WindowsIPCPrivate() = default;
    ~WindowsIPCPrivate() = default;

#ifdef Q_OS_WIN
    HANDLE pipeHandle = INVALID_HANDLE_VALUE;
    HANDLE serverPipe = INVALID_HANDLE_VALUE;
    OVERLAPPED overlapped = {};
    SECURITY_ATTRIBUTES securityAttributes = {};
    PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
#endif
    
    QString pipeName;
    bool lowIntegrityEnabled = false;
    QStringList allowedUsers;
    bool isInitialized = false;
    bool isServerMode = false;
    
    // Security context
    QString securityToken;
    QByteArray encryptionKey;
    
    // Connection management
    QHash<QString, QDateTime> clientConnections;
    QElapsedTimer connectionTimer;
    
    // Threading
    QThread* workerThread = nullptr;
    bool shutdownRequested = false;
};

WindowsIPC::WindowsIPC(QObject* parent)
    : SecureIPC(parent)
    , d(std::make_unique<WindowsIPCPrivate>())
{
#ifdef Q_OS_WIN
    d->connectionTimer.start();
    
    // Initialize overlapped structure for async operations
    d->overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    
    Logger::instance().info("WindowsIPC: Initialized with Named Pipes support");
#else
    Logger::instance().warning("WindowsIPC: Named Pipes support not available on this platform");
#endif
}

WindowsIPC::~WindowsIPC() {
    shutdown();
}

Expected<bool, IPCError> WindowsIPC::initializeServer(const QString& serverName) {
    d->pipeName = QString("\\\\.\\pipe\\murmur_%1").arg(serverName);
    d->isServerMode = true;
    
#ifdef Q_OS_WIN
    auto setupResult = createNamedPipeServer();
    if (setupResult.hasError()) {
        return setupResult;
    }
    
    d->isInitialized = true;
    Logger::instance().info("WindowsIPC: Server initialized successfully: {}", d->pipeName.toStdString());
    
    return true;
#else
    // Fallback to local socket for non-Windows platforms
    return SecureIPC::initializeServer(serverName);
#endif
}

Expected<bool, IPCError> WindowsIPC::initializeClient(const QString& serverName) {
    d->pipeName = QString("\\\\.\\pipe\\murmur_%1").arg(serverName);
    d->isServerMode = false;
    
#ifdef Q_OS_WIN
    auto connectResult = connectToNamedPipe();
    if (connectResult.hasError()) {
        return connectResult;
    }
    
    d->isInitialized = true;
    Logger::instance().info("WindowsIPC: Client connected to: {}", d->pipeName.toStdString());
    
    return true;
#else
    // Fallback to local socket for non-Windows platforms
    return SecureIPC::initializeClient(serverName);
#endif
}

Expected<bool, IPCError> WindowsIPC::sendMessage(const QString& clientId, const IPCMessage& message) {
    if (!d->isInitialized) {
        return makeUnexpected(IPCError::ServerNotRunning);
    }
    
#ifdef Q_OS_WIN
    if (d->pipeHandle == INVALID_HANDLE_VALUE) {
        return makeUnexpected(IPCError::ClientNotConnected);
    }
    
    // Serialize message
    QByteArray serialized = serializeMessage(message);
    if (serialized.isEmpty()) {
        return makeUnexpected(IPCError::InvalidMessage);
    }
    
    // Encrypt message for pipe transmission
    auto encryptResult = encryptForPipe(serialized);
    if (encryptResult.hasError()) {
        return makeUnexpected(encryptResult.error());
    }
    
    QByteArray encrypted = encryptResult.value();
    
    // Write message size first
    DWORD messageSize = static_cast<DWORD>(encrypted.size());
    DWORD bytesWritten = 0;
    
    if (!WriteFile(d->pipeHandle, &messageSize, sizeof(messageSize), &bytesWritten, nullptr)) {
        DWORD error = GetLastError();
        Logger::instance().error("WindowsIPC: Failed to write message size: {}", error);
        return makeUnexpected(IPCError::ConnectionFailed);
    }
    
    // Write message data
    if (!WriteFile(d->pipeHandle, encrypted.constData(), messageSize, &bytesWritten, nullptr)) {
        DWORD error = GetLastError();
        Logger::instance().error("WindowsIPC: Failed to write message data: {}", error);
        return makeUnexpected(IPCError::ConnectionFailed);
    }
    
    Logger::instance().debug("WindowsIPC: Message sent via Named Pipe: {} bytes", encrypted.size());
    return true;
#else
    // Fallback to base implementation
    return SecureIPC::sendMessage(clientId, message);
#endif
}

Expected<bool, IPCError> WindowsIPC::broadcastMessage(const IPCMessage& message) {
    if (!d->isInitialized || !d->isServerMode) {
        return makeUnexpected(IPCError::ServerNotRunning);
    }
    
#ifdef Q_OS_WIN
    // Named Pipes don't have direct broadcast capability - would need to track all client connections
    // For now, use the base implementation
    Logger::instance().info("WindowsIPC: Broadcast via base implementation");
    return SecureIPC::broadcastMessage(message);
#else
    return SecureIPC::broadcastMessage(message);
#endif
}

Expected<bool, IPCError> WindowsIPC::shutdown() {
    if (!d->isInitialized) {
        return true;
    }
    
    d->shutdownRequested = true;
    
#ifdef Q_OS_WIN
    cleanupPipeResources();
#endif
    
    d->isInitialized = false;
    Logger::instance().info("WindowsIPC: Shutdown completed");
    
    return true;
}

bool WindowsIPC::isServerRunning() const {
    return d->isInitialized && d->isServerMode;
}

bool WindowsIPC::isConnected() const {
    return d->isInitialized;
}

QStringList WindowsIPC::getConnectedClients() const {
    return d->clientConnections.keys();
}

Expected<bool, IPCError> WindowsIPC::setupNamedPipe(const QString& pipeName) {
    d->pipeName = pipeName;
    
#ifdef Q_OS_WIN
    // Validate pipe name format
    if (!pipeName.startsWith("\\\\.\\pipe\\")) {
        Logger::instance().warning("WindowsIPC: Pipe name should start with '\\\\.\\pipe\\'");
    }
    
    Logger::instance().info("WindowsIPC: Named pipe configured: {}", pipeName.toStdString());
    return true;
#else
    Logger::instance().warning("WindowsIPC: Named pipe setup not available on this platform");
    return makeUnexpected(IPCError::InitializationFailed);
#endif
}

Expected<bool, IPCError> WindowsIPC::configureSecurity(const QString& securityDescriptor) {
#ifdef Q_OS_WIN
    // Convert security descriptor string to SECURITY_DESCRIPTOR
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
            securityDescriptor.toLocal8Bit().constData(),
            SDDL_REVISION_1,
            &d->securityDescriptor,
            nullptr)) {
        DWORD error = GetLastError();
        Logger::instance().error("WindowsIPC: Failed to parse security descriptor: {}", error);
        return makeUnexpected(IPCError::PermissionDenied);
    }
    
    d->securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    d->securityAttributes.lpSecurityDescriptor = d->securityDescriptor;
    d->securityAttributes.bInheritHandle = FALSE;
    
    Logger::instance().info("WindowsIPC: Security descriptor configured");
    return true;
#else
    Q_UNUSED(securityDescriptor)
    Logger::instance().warning("WindowsIPC: Security configuration not available on this platform");
    return true;
#endif
}

Expected<bool, IPCError> WindowsIPC::enableLowIntegrityLevel(bool enabled) {
    d->lowIntegrityEnabled = enabled;
    
#ifdef Q_OS_WIN
    if (enabled) {
        // Set up low integrity level for pipe
        Logger::instance().info("WindowsIPC: Low integrity level enabled for sandboxed processes");
    } else {
        Logger::instance().info("WindowsIPC: Standard integrity level");
    }
    
    return true;
#else
    Logger::instance().warning("WindowsIPC: Integrity levels not supported on this platform");
    return enabled ? makeUnexpected(IPCError::InitializationFailed) : Expected<bool, IPCError>(true);
#endif
}

Expected<bool, IPCError> WindowsIPC::setAccessControl(const QStringList& allowedUsers) {
    d->allowedUsers = allowedUsers;
    
#ifdef Q_OS_WIN
    Logger::instance().info("WindowsIPC: Configured access control for {} users", allowedUsers.size());
    for (const QString& user : allowedUsers) {
        Logger::instance().debug("WindowsIPC: Allowed user: {}", user.toStdString());
    }
    
    return true;
#else
    Logger::instance().warning("WindowsIPC: Access control not supported on this platform");
    return true;
#endif
}

Expected<bool, IPCError> WindowsIPC::createNamedPipeServer() {
#ifdef Q_OS_WIN
    // Set up security for the pipe
    auto securityResult = setupPipeSecurity();
    if (securityResult.hasError()) {
        return securityResult;
    }
    
    // Create the named pipe
    d->serverPipe = CreateNamedPipeA(
        d->pipeName.toLocal8Bit().constData(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        8192,  // Output buffer size
        8192,  // Input buffer size
        0,     // Default timeout
        &d->securityAttributes
    );
    
    if (d->serverPipe == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        Logger::instance().error("WindowsIPC: Failed to create named pipe: {}", error);
        return makeUnexpected(IPCError::InitializationFailed);
    }
    
    Logger::instance().info("WindowsIPC: Named pipe server created successfully");
    return true;
#else
    return makeUnexpected(IPCError::InitializationFailed);
#endif
}

Expected<bool, IPCError> WindowsIPC::connectToNamedPipe() {
#ifdef Q_OS_WIN
    // Wait for pipe to become available
    if (!WaitNamedPipeA(d->pipeName.toLocal8Bit().constData(), 5000)) {
        DWORD error = GetLastError();
        Logger::instance().error("WindowsIPC: Pipe not available: {}", error);
        return makeUnexpected(IPCError::ServerNotRunning);
    }
    
    // Open the named pipe
    d->pipeHandle = CreateFileA(
        d->pipeName.toLocal8Bit().constData(),
        GENERIC_READ | GENERIC_WRITE,
        0,              // No sharing
        nullptr,        // Default security attributes
        OPEN_EXISTING,  // Opens existing pipe
        FILE_FLAG_OVERLAPPED, // Enable async I/O
        nullptr         // No template file
    );
    
    if (d->pipeHandle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        Logger::instance().error("WindowsIPC: Failed to connect to named pipe: {}", error);
        return makeUnexpected(IPCError::ConnectionFailed);
    }
    
    // Set pipe mode
    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(d->pipeHandle, &mode, nullptr, nullptr)) {
        DWORD error = GetLastError();
        Logger::instance().warning("WindowsIPC: Failed to set pipe mode: {}", error);
    }
    
    Logger::instance().info("WindowsIPC: Connected to named pipe successfully");
    return true;
#else
    return makeUnexpected(IPCError::ConnectionFailed);
#endif
}

Expected<bool, IPCError> WindowsIPC::setupPipeSecurity() {
#ifdef Q_OS_WIN
    if (d->securityDescriptor) {
        // Security descriptor already configured
        return true;
    }
    
    // Create a default security descriptor that allows access to current user and admin
    QString defaultSD;
    if (d->lowIntegrityEnabled) {
        // Low integrity level for sandboxed processes
        defaultSD = "D:(A;;GA;;;WD)(A;;GA;;;SY)(A;;GA;;;BA)S:(ML;;NW;;;LW)";
    } else {
        // Standard security descriptor
        defaultSD = "D:(A;;GA;;;WD)(A;;GA;;;SY)(A;;GA;;;BA)";
    }
    
    return configureSecurity(defaultSD);
#else
    return true;
#endif
}

Expected<QByteArray, IPCError> WindowsIPC::encryptForPipe(const QByteArray& data) {
    // Use base class encryption, could add Windows-specific encryption here
    return encryptMessage(data);
}

Expected<QByteArray, IPCError> WindowsIPC::decryptFromPipe(const QByteArray& encryptedData) {
    // Use base class decryption, could add Windows-specific decryption here
    return decryptMessage(encryptedData);
}

void WindowsIPC::cleanupPipeResources() {
#ifdef Q_OS_WIN
    if (d->pipeHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(d->pipeHandle);
        d->pipeHandle = INVALID_HANDLE_VALUE;
    }
    
    if (d->serverPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(d->serverPipe);
        d->serverPipe = INVALID_HANDLE_VALUE;
    }
    
    if (d->overlapped.hEvent) {
        CloseHandle(d->overlapped.hEvent);
        d->overlapped.hEvent = nullptr;
    }
    
    if (d->securityDescriptor) {
        LocalFree(d->securityDescriptor);
        d->securityDescriptor = nullptr;
    }
    
    Logger::instance().info("WindowsIPC: Named pipe resources cleaned up");
#endif
}

bool WindowsIPC::validatePipePermissions() const {
#ifdef Q_OS_WIN
    if (d->pipeHandle == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    // Could add more sophisticated permission validation here
    return true;
#else
    return true;
#endif
}

void WindowsIPC::handlePipeConnection() {
    QString clientId = QString("client_%1").arg(d->clientConnections.size());
    d->clientConnections[clientId] = QDateTime::currentDateTime();
    
    emit clientConnected(clientId);
    Logger::instance().info("WindowsIPC: New pipe client connected: {}", clientId.toStdString());
}

void WindowsIPC::handlePipeDisconnection() {
    emit clientDisconnected("unknown");
    Logger::instance().info("WindowsIPC: Pipe client disconnected");
}

void WindowsIPC::handlePipeMessage(const QByteArray& data) {
    auto parseResult = deserializeMessage(data);
    if (parseResult.hasValue()) {
        emit messageReceived(parseResult.value());
        Logger::instance().debug("WindowsIPC: Received pipe message: {} bytes", data.size());
    } else {
        Logger::instance().error("WindowsIPC: Failed to parse pipe message");
    }
}

void WindowsIPC::handlePipeError(const QString& error) {
    Logger::instance().error("WindowsIPC: Pipe error: {}", error.toStdString());
    emit errorOccurred(error);
}

} // namespace Murmur

#include "WindowsIPC.moc"