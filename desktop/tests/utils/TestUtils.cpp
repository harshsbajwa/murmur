#include "TestUtils.hpp"
#include <QtCore/QDir>
#include <QtCore/QStandardPaths>
#include <QtCore/QCoreApplication>
#include <QtCore/QThread>
#include <QtCore/QRandomGenerator>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QCryptographicHash>
#include <QtCore/QProcess>
#include <QtCore/QUuid>
#include <QtCore/QRegularExpression>
#include <QtCore/QPointer>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QHostAddress>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QtTest/QTest>
#include <QtConcurrent/QtConcurrent>
#include <QtGui/QImage>

#ifdef Q_OS_MACOS
#include <mach/mach.h>
#include <mach/task_info.h>
#endif

namespace Murmur {
namespace Test {

// Static member initialization
QTemporaryDir* TestUtils::tempDir_ = nullptr;
QStringList TestUtils::testLogs_;
QTimer* TestUtils::resourceMonitorTimer_ = nullptr;
QJsonObject TestUtils::resourceBaseline_;

TestUtils::TestUtils(QObject* parent) : QObject(parent) {
    // Constructor implementation
}

TestUtils::~TestUtils() {
    // Destructor implementation
}

void TestUtils::initializeTestEnvironment() {
    // Initialize logging for tests
    enableTestLogging();
    
    // Create main temporary directory
    if (!tempDir_) {
        tempDir_ = new QTemporaryDir();
        if (!tempDir_->isValid()) {
            qFatal("Failed to create temporary directory for tests");
        }
    }
    
    // Set up test environment variables
    qputenv("MURMUR_TEST_MODE", "1");
    qputenv("QT_LOGGING_RULES", "murmur.*=true");
    
    logTestMessage("Test environment initialized");
}

void TestUtils::cleanupTestEnvironment() {
    // Stop resource monitoring
    stopResourceMonitoring();
    
    // Cleanup temporary directory
    if (tempDir_) {
        delete tempDir_;
        tempDir_ = nullptr;
    }
    
    // Clear simulated errors
    clearSimulatedErrors();
    
    // Clear test logs
    clearTestLogs();
    
    logTestMessage("Test environment cleaned up");
}

QString TestUtils::createTempDirectory(const QString& prefix) {
    if (!tempDir_) {
        initializeTestEnvironment();
    }
    
    QString basePath = tempDir_->path();
    QString dirName = QString("%1_%2_%3")
                     .arg(prefix)
                     .arg(QDateTime::currentMSecsSinceEpoch())
                     .arg(QRandomGenerator::global()->generate());
    
    QString fullPath = basePath + "/" + dirName;
    
    QDir dir;
    if (!dir.mkpath(fullPath)) {
        return QString();
    }
    
    return fullPath;
}

void TestUtils::cleanupTempDirectory(const QString& path) {
    QDir dir(path);
    if (dir.exists()) {
        dir.removeRecursively();
    }
}

QString TestUtils::getTempPath() {
    if (!tempDir_) {
        initializeTestEnvironment();
    }
    return tempDir_->path();
}

QString TestUtils::createTestVideoFile(const QString& directory, int durationSeconds, const QString& format) {
    // First try to use real sample media files from resources
    QString realSamplePath = getRealSampleVideoFile();
    if (!realSamplePath.isEmpty() && QFileInfo(realSamplePath).exists()) {
        QString filename = QString("real_video_%1s.%2")
                          .arg(durationSeconds)
                          .arg(format);
        QString targetPath = directory + "/" + filename;
        
        if (QFile::copy(realSamplePath, targetPath)) {
            logTestMessage(QString("Using real sample video: %1").arg(targetPath));
            return targetPath;
        }
    }
    
    QString filename = QString("test_video_%1s.%2")
                      .arg(durationSeconds)
                      .arg(format);
    QString filePath = directory + "/" + filename;
    
    // Create a minimal test video using FFmpeg if available, otherwise generate placeholder
    if (isFFmpegAvailable()) {
        QProcess ffmpeg;
        QStringList args;
        args << "-f" << "lavfi"
             << "-i" << QString("testsrc=duration=%1:size=320x240:rate=30").arg(durationSeconds)
             << "-f" << "lavfi"
             << "-i" << QString("sine=frequency=1000:duration=%1").arg(durationSeconds)
             << "-c:v" << "libx264"
             << "-c:a" << "aac"
             << "-preset" << "ultrafast"
             << "-pix_fmt" << "yuv420p"
             << "-y" // Overwrite output file
             << filePath;
        
        QString ffmpegPath = qgetenv("MURMUR_TEST_FFMPEG_PATH");
        if (ffmpegPath.isEmpty()) {
            ffmpegPath = "ffmpeg"; // fallback
        }
        ffmpeg.start(ffmpegPath, args);
        if (ffmpeg.waitForFinished(30000) && ffmpeg.exitCode() == 0) {
            logTestMessage(QString("Created test video: %1").arg(filePath));
            return filePath;
        } else {
            logTestMessage("FFmpeg failed, falling back to placeholder file");
        }
    }
    
    // Fallback: create a placeholder file with proper video container headers
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        // Write minimal MP4/AVI header depending on format
        if (format.toLower() == "mp4") {
            // Basic MP4 header (ftyp + mdat boxes)
            QByteArray mp4Header;
            mp4Header.append(QByteArray::fromHex("0000002066747970")); // ftyp box header
            mp4Header.append("mp42"); // major brand
            mp4Header.append(QByteArray::fromHex("00000000")); // minor version
            mp4Header.append("mp42isom"); // compatible brands
            mp4Header.append(QByteArray::fromHex("00000400")); // mdat box size
            mp4Header.append("mdat"); // mdat box type
            file.write(mp4Header);
        }
        // Write test data
        QByteArray videoData = generateRandomData(1024 * durationSeconds);
        file.write(videoData);
        file.close();
    }
    
    return filePath;
}

QString TestUtils::createTestAudioFile(const QString& directory, int durationSeconds, const QString& format) {
    // First try to use real sample audio files from resources
    QString realSamplePath = getRealSampleAudioFile();
    if (!realSamplePath.isEmpty() && QFileInfo(realSamplePath).exists()) {
        QString filename = QString("real_audio_%1s.%2")
                          .arg(durationSeconds)
                          .arg(format);
        QString targetPath = directory + "/" + filename;
        
        if (QFile::copy(realSamplePath, targetPath)) {
            logTestMessage(QString("Using real sample audio: %1").arg(targetPath));
            return targetPath;
        }
    }
    
    QString filename = QString("test_audio_%1s.%2")
                      .arg(durationSeconds)
                      .arg(format);
    QString filePath = directory + "/" + filename;
    
    // Try to generate real audio with FFmpeg if available
    if (isFFmpegAvailable()) {
        QProcess ffmpeg;
        QStringList args;
        args << "-f" << "lavfi"
             << "-i" << QString("sine=frequency=440:duration=%1").arg(durationSeconds)
             << "-c:a" << "pcm_s16le"
             << "-ar" << "44100"
             << "-y" // Overwrite output file
             << filePath;
        
        QString ffmpegPath = qgetenv("MURMUR_TEST_FFMPEG_PATH");
        if (ffmpegPath.isEmpty()) {
            ffmpegPath = "ffmpeg"; // fallback
        }
        ffmpeg.start(ffmpegPath, args);
        if (ffmpeg.waitForFinished(30000) && ffmpeg.exitCode() == 0) {
            logTestMessage(QString("Created real audio: %1").arg(filePath));
            return filePath;
        } else {
            logTestMessage("FFmpeg audio generation failed, falling back to placeholder");
        }
    }
    
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        // Create minimal audio file with WAV header if format is wav
        if (format.toLower() == "wav") {
            // Basic WAV header
            QByteArray wavHeader;
            wavHeader.append("RIFF");
            wavHeader.append(QByteArray(4, 0)); // File size placeholder
            wavHeader.append("WAVE");
            wavHeader.append("fmt ");
            wavHeader.append(QByteArray::fromHex("10000000")); // fmt chunk size
            wavHeader.append(QByteArray::fromHex("0100")); // audio format (PCM)
            wavHeader.append(QByteArray::fromHex("0100")); // number of channels
            wavHeader.append(QByteArray::fromHex("44AC0000")); // sample rate (44100)
            wavHeader.append(QByteArray::fromHex("88580100")); // byte rate
            wavHeader.append(QByteArray::fromHex("0200")); // block align
            wavHeader.append(QByteArray::fromHex("1000")); // bits per sample
            wavHeader.append("data");
            wavHeader.append(QByteArray(4, 0)); // data chunk size placeholder
            file.write(wavHeader);
        }
        // Write test data
        QByteArray audioData = generateRandomData(512 * durationSeconds); // 512B per second
        file.write(audioData);
        file.close();
    }
    
    return filePath;
}

QString TestUtils::createTestTextFile(const QString& directory, const QString& content, const QString& filename) {
    QString filePath = directory + "/" + filename;
    
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << content;
        file.close();
    }
    
    return filePath;
}

QByteArray TestUtils::createTestImageData(int width, int height) {
    // Create test image using QImage for proper format handling
    QImage image(width, height, QImage::Format_RGB888);
    
    // Fill with gradient test pattern
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int r = (x * 255) / width;
            int g = (y * 255) / height;
            int b = ((x + y) * 255) / (width + height);
            image.setPixel(x, y, qRgb(r, g, b));
        }
    }
    
    // Convert to raw RGB data
    QByteArray imageData;
    imageData.resize(width * height * 3);
    
    int index = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            QRgb pixel = image.pixel(x, y);
            imageData[index++] = static_cast<char>(qRed(pixel));
            imageData[index++] = static_cast<char>(qGreen(pixel));
            imageData[index++] = static_cast<char>(qBlue(pixel));
        }
    }
    
    return imageData;
}

QString TestUtils::createTestTorrentFile(const QString& directory, const QStringList& fileNames) {
    QString torrentPath = directory + "/test.torrent";
    
    // Create basic torrent file structure
    QJsonObject torrentData;
    torrentData["announce"] = "http://test.tracker.com/announce";
    torrentData["creation date"] = QDateTime::currentSecsSinceEpoch();
    torrentData["created by"] = "Murmur Test Suite";
    
    QJsonObject info;
    info["name"] = "Test Torrent";
    info["piece length"] = 32768;
    
    QJsonArray files;
    for (const QString& fileName : fileNames) {
        QJsonObject fileInfo;
        fileInfo["path"] = QJsonArray{fileName};
        fileInfo["length"] = 1024; // 1KB per file
        files.append(fileInfo);
    }
    info["files"] = files;
    
    torrentData["info"] = info;
    
    // Write to file
    QFile file(torrentPath);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(torrentData);
        file.write(doc.toJson(QJsonDocument::Compact));
        file.close();
    }
    
    return torrentPath;
}

QString TestUtils::createTestMagnetLink(const QString& name) {
    // Generate a valid info hash using InfoHashValidator
    QString infoHash = InfoHashValidator::generateTestHash(name.length() + 1000);
    
    return QString("magnet:?xt=urn:btih:%1&dn=%2&tr=http://test.tracker.com/announce")
           .arg(infoHash)
           .arg(QString(name).replace(' ', '+'));
}

QString TestUtils::createTestDatabase(const QString& directory) {
    QString dbDir = directory.isEmpty() ? createTempDirectory("test_db") : directory;
    QString dbPath = dbDir + "/test.db";
    
    // Create empty database file
    QFile file(dbPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.close();
    }
    
    return dbPath;
}

void TestUtils::populateTestDatabase(const QString& dbPath) {
    logTestMessage(QString("Populating test database: %1").arg(dbPath));
    
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "test_populate_" + QUuid::createUuid().toString());
    db.setDatabaseName(dbPath);
    
    if (!db.open()) {
        logTestMessage("Failed to open database for population: " + db.lastError().text());
        return;
    }
    
    QSqlQuery query(db);
    
    // Create test tables
    query.exec("CREATE TABLE IF NOT EXISTS torrents ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "info_hash TEXT NOT NULL UNIQUE,"
               "name TEXT NOT NULL,"
               "size INTEGER,"
               "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
               "status TEXT DEFAULT 'inactive'"
               ")");
    
    query.exec("CREATE TABLE IF NOT EXISTS files ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "torrent_id INTEGER,"
               "path TEXT NOT NULL,"
               "size INTEGER,"
               "FOREIGN KEY(torrent_id) REFERENCES torrents(id)"
               ")");
    
    // Insert test data
    query.prepare("INSERT INTO torrents (info_hash, name, size, status) VALUES (?, ?, ?, ?)");
    
    QStringList testTorrents = {
        "test_video_1080p.mp4",
        "test_audio_album.zip",
        "test_document_collection.pdf"
    };
    
    for (int i = 0; i < testTorrents.size(); ++i) {
        const QString& name = testTorrents[i];
        QString hash = InfoHashValidator::generateTestHash(i + 100); // Use deterministic seed
        query.addBindValue(hash);
        query.addBindValue(name);
        query.addBindValue(QRandomGenerator::global()->bounded(1000000, 10000000)); // Random size
        query.addBindValue("active");
        query.exec();
    }
    
    db.close();
    QSqlDatabase::removeDatabase(db.connectionName());
    
    logTestMessage("Test database populated successfully");
}

bool TestUtils::verifyDatabaseIntegrity(const QString& dbPath) {
    // Basic file existence check
    QFileInfo fileInfo(dbPath);
    return fileInfo.exists() && fileInfo.size() > 0;
}

bool TestUtils::waitForSignal(QObject* sender, const char* signal, int timeoutMs) {
    QSignalSpy spy(sender, signal);
    return spy.wait(timeoutMs);
}

bool TestUtils::waitForCondition(std::function<bool()> condition, int timeoutMs, int checkIntervalMs) {
    QElapsedTimer timer;
    timer.start();
    
    while (timer.elapsed() < timeoutMs) {
        if (condition()) {
            return true;
        }
        QTest::qWait(checkIntervalMs);
    }
    
    return false;
}

qint64 TestUtils::measureExecutionTime(std::function<void()> operation) {
    QElapsedTimer timer;
    timer.start();
    operation();
    return timer.elapsed();
}

QPair<qint64, double> TestUtils::measureMemoryUsage(std::function<void()> operation) {
    qint64 startTime = QDateTime::currentMSecsSinceEpoch();
    
#ifdef Q_OS_MACOS
    // macOS memory measurement using task_info
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    size_t startMemory = 0;
    
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) == KERN_SUCCESS) {
        startMemory = info.resident_size;
    }
#elif defined(Q_OS_LINUX)
    // Linux memory measurement using /proc/self/status
    QFile statusFile("/proc/self/status");
    size_t startMemory = 0;
    if (statusFile.open(QIODevice::ReadOnly)) {
        QTextStream stream(&statusFile);
        QString line;
        while (stream.readLineInto(&line)) {
            if (line.startsWith("VmRSS:")) {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    startMemory = parts[1].toULongLong() * 1024; // Convert KB to bytes
                }
                break;
            }
        }
    }
#else
    size_t startMemory = 0; // Fallback for other platforms
#endif
    
    // Execute operation
    operation();
    
    qint64 endTime = QDateTime::currentMSecsSinceEpoch();
    
#ifdef Q_OS_MACOS
    size_t endMemory = 0;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) == KERN_SUCCESS) {
        endMemory = info.resident_size;
    }
#elif defined(Q_OS_LINUX)
    size_t endMemory = 0;
    if (statusFile.seek(0) && statusFile.open(QIODevice::ReadOnly)) {
        QTextStream stream(&statusFile);
        QString line;
        while (stream.readLineInto(&line)) {
            if (line.startsWith("VmRSS:")) {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    endMemory = parts[1].toULongLong() * 1024;
                }
                break;
            }
        }
    }
#else
    size_t endMemory = 0;
#endif
    
    double memoryDelta = static_cast<double>(endMemory) - static_cast<double>(startMemory);
    return qMakePair(endTime - startTime, memoryDelta / (1024.0 * 1024.0)); // Return MB
}

QStringList TestUtils::generateRandomStrings(int count, int minLength, int maxLength) {
    QStringList strings;
    const QString charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    
    for (int i = 0; i < count; ++i) {
        int length = QRandomGenerator::global()->bounded(minLength, maxLength + 1);
        QString randomString;
        
        for (int j = 0; j < length; ++j) {
            int index = QRandomGenerator::global()->bounded(charset.length());
            randomString.append(charset.at(index));
        }
        
        strings.append(randomString);
    }
    
    return strings;
}

QByteArray TestUtils::generateRandomData(int size) {
    QByteArray data;
    data.resize(size);
    
    for (int i = 0; i < size; ++i) {
        data[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    
    return data;
}

QJsonObject TestUtils::generateTestTorrentMetadata() {
    QJsonObject metadata;
    metadata["name"] = "Test Torrent";
    metadata["size"] = 1048576; // 1MB
    metadata["files"] = QJsonArray{"file1.txt", "file2.txt"};
    metadata["creation_date"] = QDateTime::currentSecsSinceEpoch();
    metadata["created_by"] = "Murmur Test";
    
    return metadata;
}

QJsonObject TestUtils::generateTestMediaMetadata() {
    QJsonObject metadata;
    metadata["duration"] = 120; // 2 minutes
    metadata["format"] = "mp4";
    metadata["width"] = 1920;
    metadata["height"] = 1080;
    metadata["fps"] = 30.0;
    metadata["bitrate"] = 5000;
    metadata["codec"] = "h264";
    
    return metadata;
}

bool TestUtils::isNetworkAvailable() {
    // Check network connectivity by attempting to connect to a reliable host
    QTcpSocket socket;
    socket.connectToHost("8.8.8.8", 53); // Google DNS
    
    if (socket.waitForConnected(3000)) {
        socket.disconnectFromHost();
        if (socket.state() != QAbstractSocket::UnconnectedState) {
            socket.waitForDisconnected(1000);
        }
        return true;
    }
    
    // Try alternative connectivity check
    socket.connectToHost("1.1.1.1", 53); // Cloudflare DNS
    if (socket.waitForConnected(3000)) {
        socket.disconnectFromHost();
        if (socket.state() != QAbstractSocket::UnconnectedState) {
            socket.waitForDisconnected(1000);
        }
        return true;
    }
    
    return false;
}

static QPointer<QTcpServer> testHttpServer;

QString TestUtils::startTestHttpServer(int port) {
    if (testHttpServer) {
        stopTestHttpServer();
    }
    
    testHttpServer = new QTcpServer();
    int actualPort = port > 0 ? port : 8080;
    
    if (!testHttpServer->listen(QHostAddress::LocalHost, actualPort)) {
        logTestMessage(QString("Failed to start test HTTP server on port %1: %2")
                      .arg(actualPort).arg(testHttpServer->errorString()));
        delete testHttpServer;
        testHttpServer = nullptr;
        return QString();
    }
    
    actualPort = testHttpServer->serverPort();
    QString serverUrl = QString("http://localhost:%1").arg(actualPort);
    
    // Simple HTTP response handler
    QObject::connect(testHttpServer, &QTcpServer::newConnection, [=]() {
        QTcpSocket* client = testHttpServer->nextPendingConnection();
        QObject::connect(client, &QTcpSocket::readyRead, [=]() {
            QByteArray request = client->readAll();
            
            // Simple HTTP 200 response
            QByteArray response = "HTTP/1.1 200 OK\r\n"
                                 "Content-Type: text/plain\r\n"
                                 "Content-Length: 13\r\n"
                                 "\r\n"
                                 "Test response";
            
            client->write(response);
            client->disconnectFromHost();
        });
        
        QObject::connect(client, &QTcpSocket::disconnected, client, &QTcpSocket::deleteLater);
    });
    
    logTestMessage(QString("Test HTTP server started on %1").arg(serverUrl));
    return serverUrl;
}

void TestUtils::stopTestHttpServer() {
    if (testHttpServer) {
        testHttpServer->close();
        testHttpServer->deleteLater();
        testHttpServer = nullptr;
        logTestMessage("Test HTTP server stopped");
    }
}

void TestUtils::enableTestLogging() {
    testLogs_.clear();
}

void TestUtils::disableTestLogging() {
    // Disable test logging
}

QStringList TestUtils::getTestLogs() {
    return testLogs_;
}

void TestUtils::clearTestLogs() {
    testLogs_.clear();
}

bool TestUtils::compareFiles(const QString& file1, const QString& file2) {
    QFile f1(file1);
    QFile f2(file2);
    
    if (!f1.open(QIODevice::ReadOnly) || !f2.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    if (f1.size() != f2.size()) {
        return false;
    }
    
    const int bufferSize = 8192;
    while (!f1.atEnd()) {
        QByteArray data1 = f1.read(bufferSize);
        QByteArray data2 = f2.read(bufferSize);
        
        if (data1 != data2) {
            return false;
        }
    }
    
    return true;
}

bool TestUtils::validateVideoFile(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    return fileInfo.exists() && fileInfo.size() > 0;
}

bool TestUtils::validateAudioFile(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    return fileInfo.exists() && fileInfo.size() > 0;
}

bool TestUtils::validateDatabaseFile(const QString& filePath) {
    return verifyDatabaseIntegrity(filePath);
}

void TestUtils::assertFileExists(const QString& filePath, const QString& context) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        QString message = QString("File does not exist: %1").arg(filePath);
        if (!context.isEmpty()) {
            message += QString(" (context: %1)").arg(context);
        }
        QFAIL(qPrintable(message));
    }
}

void TestUtils::assertDirectoryExists(const QString& dirPath, const QString& context) {
    QDir dir(dirPath);
    if (!dir.exists()) {
        QString message = QString("Directory does not exist: %1").arg(dirPath);
        if (!context.isEmpty()) {
            message += QString(" (context: %1)").arg(context);
        }
        QFAIL(qPrintable(message));
    }
}

void TestUtils::assertFileNotExists(const QString& filePath, const QString& context) {
    QFileInfo fileInfo(filePath);
    if (fileInfo.exists()) {
        QString message = QString("File should not exist: %1").arg(filePath);
        if (!context.isEmpty()) {
            message += QString(" (context: %1)").arg(context);
        }
        QFAIL(qPrintable(message));
    }
}

bool TestUtils::isFFmpegAvailable() {
    // Try to find FFmpeg in various locations
    QStringList possiblePaths = {
        // Conan-provided FFmpeg paths (these change based on package hash, so check multiple)
        "/Users/harshbajwa/.conan2/p/b/ffmpe709c7b5e15ee8/p/bin/ffmpeg",
        "/Users/harshbajwa/.conan2/p/b/ffmpee468f75b72de9/p/bin/ffmpeg",
        // System FFmpeg as fallback
        "ffmpeg"
    };
    
    // Also try to find FFmpeg dynamically in the Conan cache
    QString conanCache = QDir::homePath() + "/.conan2";
    QDir conanDir(conanCache);
    if (conanDir.exists()) {
        QProcess findCmd;
        findCmd.start("find", QStringList() << conanCache << "-name" << "ffmpeg" << "-type" << "f" << "-executable");
        if (findCmd.waitForFinished(5000)) {
            QString output = findCmd.readAllStandardOutput();
            QStringList foundPaths = output.split('\n', Qt::SkipEmptyParts);
            for (const QString& path : foundPaths) {
                if (path.contains("/bin/ffmpeg")) {
                    possiblePaths.prepend(path);
                }
            }
        }
    }
    
    for (const QString& ffmpegPath : possiblePaths) {
        QProcess ffmpeg;
        ffmpeg.start(ffmpegPath, QStringList() << "-version");
        if (ffmpeg.waitForFinished(3000) && ffmpeg.exitCode() == 0) {
            QString output = ffmpeg.readAllStandardOutput();
            if (output.contains("ffmpeg version")) {
                logTestMessage("FFmpeg available at " + ffmpegPath + ": " + output.split('\n').first());
                
                // Store the working path for later use
                qputenv("MURMUR_TEST_FFMPEG_PATH", ffmpegPath.toUtf8());
                return true;
            }
        }
    }
    
    logTestMessage("FFmpeg not found in any location");
    return false;
}

bool TestUtils::isWhisperAvailable() {
    // Check if Whisper models directory exists and contains at least one model
    QString modelsDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models";
    QDir dir(modelsDir);
    
    if (!dir.exists()) {
        // Create models directory for testing
        if (!dir.mkpath(modelsDir)) {
            logTestMessage("Failed to create Whisper models directory: " + modelsDir);
            return false;
        }
        logTestMessage("Created Whisper models directory: " + modelsDir);
    }
    
    // Look for any .bin model files
    QStringList modelFiles = dir.entryList(QStringList() << "*.bin", QDir::Files);
    
    // Check if we have real models (file size > 10MB indicates a real model)
    bool hasRealModel = false;
    for (const QString& modelFile : modelFiles) {
        QFileInfo fileInfo(dir.filePath(modelFile));
        if (fileInfo.size() > 10 * 1024 * 1024) { // 10MB threshold
            hasRealModel = true;
            logTestMessage(QString("Found real Whisper model: %1 (%2 MB)").arg(modelFile).arg(fileInfo.size() / (1024*1024)));
            break;
        }
    }
    
    if (hasRealModel) {
        logTestMessage(QString("Whisper available with real models (%1 total files)").arg(modelFiles.size()));
        return true;
    }
    
    if (modelFiles.isEmpty()) {
        logTestMessage("No Whisper models found. Real models required for tests.");
        logTestMessage("To fix: Download ggml-tiny.en.bin to " + modelsDir);
        return false;
    }
    
    logTestMessage(QString("Only mock models found (%1 files), but real models required").arg(modelFiles.size()));
    return false;
}

bool TestUtils::isSQLiteAvailable() {
    // Check if SQLite is available via Qt
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "test_connection");
    QString tempDbPath = getTempPath() + "/test_sqlite.db";
    db.setDatabaseName(tempDbPath);
    
    if (!db.open()) {
        logTestMessage("SQLite not available: " + db.lastError().text());
        QSqlDatabase::removeDatabase("test_connection");
        return false;
    }
    
    db.close();
    QSqlDatabase::removeDatabase("test_connection");
    QFile::remove(tempDbPath);
    
    logTestMessage("SQLite available");
    return true;
}

bool TestUtils::isTestVideoAvailable() {
    // Check if test video files are available in resources
    QString testVideoPath = ":/resources/tests/video/test_1280x720_1mb.mp4";
    QFile testFile(testVideoPath);
    
    if (testFile.exists()) {
        logTestMessage("Test video available: " + testVideoPath);
        return true;
    }
    
    // Fallback: check if we can create a test video
    if (isFFmpegAvailable()) {
        QString tempDir = getTempPath();
        QString testVideoFile = createTestVideoFile(tempDir, 1, "mp4");
        bool available = QFile::exists(testVideoFile);
        
        if (available) {
            logTestMessage("Test video created successfully");
            QFile::remove(testVideoFile); // Clean up
            return true;
        }
    }
    
    logTestMessage("Test video not available");
    return false;
}

void TestUtils::testThreadSafety(std::function<void()> operation, int threadCount, int iterationsPerThread) {
    QList<QFuture<void>> futures;
    
    for (int i = 0; i < threadCount; ++i) {
        QFuture<void> future = QtConcurrent::run([operation, iterationsPerThread]() {
            for (int j = 0; j < iterationsPerThread; ++j) {
                operation();
            }
        });
        futures.append(future);
    }
    
    // Wait for all threads to complete
    for (auto& future : futures) {
        future.waitForFinished();
    }
}

void TestUtils::startResourceMonitoring() {
    if (!resourceMonitorTimer_) {
        resourceMonitorTimer_ = new QTimer();
        QObject::connect(resourceMonitorTimer_, &QTimer::timeout, &TestUtils::monitorResources);
        resourceMonitorTimer_->start(1000); // Monitor every second
        
        // Record baseline
        monitorResources();
        resourceBaseline_ = getResourceUsageReport();
    }
}

void TestUtils::stopResourceMonitoring() {
    if (resourceMonitorTimer_) {
        resourceMonitorTimer_->stop();
        delete resourceMonitorTimer_;
        resourceMonitorTimer_ = nullptr;
    }
}

QJsonObject TestUtils::getResourceUsageReport() {
    QJsonObject report;
    report["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    report["thread_count"] = QThread::idealThreadCount();
    
#ifdef Q_OS_MACOS
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) == KERN_SUCCESS) {
        report["memory_mb"] = static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
        report["virtual_memory_mb"] = static_cast<double>(info.virtual_size) / (1024.0 * 1024.0);
    }
#elif defined(Q_OS_LINUX)
    QFile statusFile("/proc/self/status");
    if (statusFile.open(QIODevice::ReadOnly)) {
        QTextStream stream(&statusFile);
        QString line;
        while (stream.readLineInto(&line)) {
            if (line.startsWith("VmRSS:")) {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    report["memory_mb"] = parts[1].toDouble() / 1024.0;
                }
            } else if (line.startsWith("VmSize:")) {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    report["virtual_memory_mb"] = parts[1].toDouble() / 1024.0;
                }
            }
        }
    }
#endif
    
    // Add baseline comparison if available
    if (!resourceBaseline_.isEmpty()) {
        double baselineMemory = resourceBaseline_["memory_mb"].toDouble();
        double currentMemory = report["memory_mb"].toDouble();
        report["memory_delta_mb"] = currentMemory - baselineMemory;
    }
    
    return report;
}

void TestUtils::logTestMessage(const QString& message) {
    QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    QString logEntry = QString("[%1] %2").arg(timestamp, message);
    testLogs_.append(logEntry);
}

void TestUtils::logMessage(const QString& message) {
    logTestMessage(message);
}

void TestUtils::monitorResources() {
    QJsonObject currentUsage;
    currentUsage["timestamp"] = QDateTime::currentMSecsSinceEpoch();
    
#ifdef Q_OS_MACOS
    // macOS-specific resource monitoring
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &infoCount) == KERN_SUCCESS) {
        currentUsage["memory_mb"] = static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
        currentUsage["virtual_memory_mb"] = static_cast<double>(info.virtual_size) / (1024.0 * 1024.0);
    }
#elif defined(Q_OS_LINUX)
    // Linux-specific resource monitoring
    QFile statusFile("/proc/self/status");
    if (statusFile.open(QIODevice::ReadOnly)) {
        QTextStream stream(&statusFile);
        QString line;
        while (stream.readLineInto(&line)) {
            if (line.startsWith("VmRSS:")) {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    currentUsage["memory_mb"] = parts[1].toDouble() / 1024.0;
                }
            } else if (line.startsWith("VmSize:")) {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    currentUsage["virtual_memory_mb"] = parts[1].toDouble() / 1024.0;
                }
            }
        }
    }
#endif
    
    // Common metrics
    currentUsage["cpu_count"] = QThread::idealThreadCount();
    currentUsage["active_thread_count"] = QThread::currentThread()->loopLevel();
    
    QString logMessage = QString("Resource usage - Memory: %1 MB, Virtual: %2 MB, CPU cores: %3")
                        .arg(currentUsage["memory_mb"].toDouble(), 0, 'f', 2)
                        .arg(currentUsage["virtual_memory_mb"].toDouble(), 0, 'f', 2)
                        .arg(currentUsage["cpu_count"].toInt());
    
    logTestMessage(logMessage);
}

// TestScope implementation
TestScope::TestScope(const QString& testName) 
    : testName_(testName) {
    tempDirectory_ = TestUtils::createTempDirectory("test_" + testName);
    TestUtils::logMessage(QString("Starting test scope: %1").arg(testName));
}

TestScope::~TestScope() {
    // Execute cleanup callbacks
    for (auto& callback : cleanupCallbacks_) {
        try {
            callback();
        } catch (...) {
            // Log error but continue cleanup
        }
    }
    
    // Cleanup temporary directory
    if (!tempDirectory_.isEmpty()) {
        TestUtils::cleanupTempDirectory(tempDirectory_);
    }
    
    TestUtils::logMessage(QString("Finished test scope: %1").arg(testName_));
}

QString TestScope::getTempDirectory() const {
    return tempDirectory_;
}

void TestScope::addCleanupCallback(std::function<void()> callback) {
    cleanupCallbacks_.push_back(callback);
}

// BenchmarkScope implementation
BenchmarkScope::BenchmarkScope(const QString& operationName, int iterations)
    : operationName_(operationName)
    , totalIterations_(iterations)
    , currentIteration_(0) {
    measurements_.reserve(iterations);
    TestUtils::logMessage(QString("Starting benchmark: %1 (%2 iterations)").arg(operationName, QString::number(iterations)));
}

BenchmarkScope::~BenchmarkScope() {
    if (!measurements_.isEmpty()) {
        double avg = getAverageTimeMs();
        double min = getMinTimeMs();
        double max = getMaxTimeMs();
        double stddev = getStandardDeviation();
        
        QString report = QString("Benchmark %1 (%2/%3 iterations completed): avg=%4ms, min=%5ms, max=%6ms, stddev=%7ms")
                         .arg(operationName_)
                         .arg(currentIteration_)
                         .arg(totalIterations_)
                         .arg(avg, 0, 'f', 2)
                         .arg(min, 0, 'f', 2)
                         .arg(max, 0, 'f', 2)
                         .arg(stddev, 0, 'f', 2);
                               
        TestUtils::logMessage(report);
    }
}

void BenchmarkScope::startIteration() {
    iterationStart_ = timer_.nsecsElapsed();
}

void BenchmarkScope::endIteration() {
    qint64 elapsed = timer_.nsecsElapsed() - iterationStart_;
    measurements_.append(elapsed);
    currentIteration_++;
}

double BenchmarkScope::getAverageTimeMs() const {
    if (measurements_.isEmpty()) return 0.0;
    
    qint64 total = 0;
    for (qint64 measurement : measurements_) {
        total += measurement;
    }
    
    return static_cast<double>(total) / measurements_.size() / 1000000.0; // Convert to ms
}

double BenchmarkScope::getMinTimeMs() const {
    if (measurements_.isEmpty()) return 0.0;
    
    qint64 min = *std::min_element(measurements_.begin(), measurements_.end());
    return static_cast<double>(min) / 1000000.0;
}

double BenchmarkScope::getMaxTimeMs() const {
    if (measurements_.isEmpty()) return 0.0;
    
    qint64 max = *std::max_element(measurements_.begin(), measurements_.end());
    return static_cast<double>(max) / 1000000.0;
}

double BenchmarkScope::getStandardDeviation() const {
    if (measurements_.size() < 2) return 0.0;
    
    double mean = getAverageTimeMs() * 1000000.0; // Convert back to ns
    double variance = 0.0;
    
    for (qint64 measurement : measurements_) {
        double diff = static_cast<double>(measurement) - mean;
        variance += diff * diff;
    }
    
    variance /= measurements_.size() - 1;
    return std::sqrt(variance) / 1000000.0; // Convert to ms
}

// Error simulation methods
static bool networkErrorSimulated = false;
static bool diskFullErrorSimulated = false;
static bool memoryPressureSimulated = false;

void TestUtils::simulateNetworkError() {
    logTestMessage("Simulating network error");
    networkErrorSimulated = true;
    // Set environment variable that can be checked by network components
    qputenv("MURMUR_TEST_NETWORK_ERROR", "1");
}

void TestUtils::simulateDiskFullError() {
    logTestMessage("Simulating disk full error");
    diskFullErrorSimulated = true;
    // Set environment variable that can be checked by storage components
    qputenv("MURMUR_TEST_DISK_FULL_ERROR", "1");
}

void TestUtils::simulateMemoryPressure() {
    logTestMessage("Simulating memory pressure");
    memoryPressureSimulated = true;
    // Set environment variable that can be checked by memory management components
    qputenv("MURMUR_TEST_MEMORY_PRESSURE", "1");
    
    // Actually allocate some memory to create pressure
    static QList<QByteArray> memoryHogs;
    for (int i = 0; i < 10; ++i) {
        memoryHogs.append(QByteArray(1024 * 1024 * 10, 'x')); // 10MB chunks
    }
}

void TestUtils::clearSimulatedErrors() {
    logTestMessage("Clearing simulated errors");
    networkErrorSimulated = false;
    diskFullErrorSimulated = false;
    memoryPressureSimulated = false;
    
    // Clear environment variables
    qunsetenv("MURMUR_TEST_NETWORK_ERROR");
    qunsetenv("MURMUR_TEST_DISK_FULL_ERROR");
    qunsetenv("MURMUR_TEST_MEMORY_PRESSURE");
    
    // Clear memory pressure simulation
    static QList<QByteArray> memoryHogs;
    memoryHogs.clear();
}

QString TestUtils::getRealSampleVideoFile() {
    // Calculate the absolute path to the resources directory based on the source tree
    QString sourceDir = QFileInfo(__FILE__).absolutePath(); // tests/utils/
    QString projectDir = QDir(sourceDir).absoluteFilePath("../../");
    QString resourcesDir = QDir(projectDir).absoluteFilePath("resources/tests/video/");
    QString videoFile = QDir(resourcesDir).absoluteFilePath("test_1280x720_1mb.mp4");
    
    // First try the calculated absolute path
    if (QFileInfo(videoFile).exists()) {
        logTestMessage(QString("Found real sample video (calculated): %1").arg(videoFile));
        return QFileInfo(videoFile).absoluteFilePath();
    }
    
    // Fallback to relative paths as before
    QStringList possiblePaths = {
        QCoreApplication::applicationDirPath() + "/../../resources/tests/video/test_1280x720_1mb.mp4",
        QCoreApplication::applicationDirPath() + "/../../../resources/tests/video/test_1280x720_1mb.mp4",
        QCoreApplication::applicationDirPath() + "/../../../../resources/tests/video/test_1280x720_1mb.mp4",
        "./resources/tests/video/test_1280x720_1mb.mp4",
        "../resources/tests/video/test_1280x720_1mb.mp4",
        "../../resources/tests/video/test_1280x720_1mb.mp4",
        "../../../resources/tests/video/test_1280x720_1mb.mp4",
        "../../../../resources/tests/video/test_1280x720_1mb.mp4"
    };
    
    // Debug logging to see current working directory and app dir
    logTestMessage(QString("Current working directory: %1").arg(QDir::currentPath()));
    logTestMessage(QString("Application directory: %1").arg(QCoreApplication::applicationDirPath()));
    logTestMessage(QString("Calculated video file path: %1").arg(videoFile));
    
    for (const QString& path : possiblePaths) {
        QFileInfo fileInfo(path);
        logTestMessage(QString("Checking path: %1 (exists: %2)").arg(path).arg(fileInfo.exists()));
        if (fileInfo.exists() && fileInfo.isFile()) {
            logTestMessage(QString("Found real sample video: %1").arg(path));
            return fileInfo.absoluteFilePath();
        }
    }
    
    logTestMessage("No real sample video file found");
    return QString();
}

QString TestUtils::getRealSampleAudioFile() {
    // Calculate the absolute path to the resources directory based on the source tree
    QString sourceDir = QFileInfo(__FILE__).absolutePath(); // tests/utils/
    QString projectDir = QDir(sourceDir).absoluteFilePath("../../");
    QString resourcesDir = QDir(projectDir).absoluteFilePath("resources/tests/audio/");
    QString audioFile = QDir(resourcesDir).absoluteFilePath("test.wav");
    
    // First try the calculated absolute path
    if (QFileInfo(audioFile).exists()) {
        logTestMessage(QString("Found real sample audio (calculated): %1").arg(audioFile));
        return QFileInfo(audioFile).absoluteFilePath();
    }
    
    // Fallback to relative paths as before
    QStringList possiblePaths = {
        QCoreApplication::applicationDirPath() + "/../../resources/tests/audio/test.wav",
        QCoreApplication::applicationDirPath() + "/../../../resources/tests/audio/test.wav",
        QCoreApplication::applicationDirPath() + "/../../../../resources/tests/audio/test.wav",
        "./resources/tests/audio/test.wav",
        "../resources/tests/audio/test.wav",
        "../../resources/tests/audio/test.wav",
        "../../../resources/tests/audio/test.wav",
        "../../../../resources/tests/audio/test.wav"
    };
    
    // Debug logging to see current working directory
    logTestMessage(QString("Looking for audio file from: %1").arg(QDir::currentPath()));
    logTestMessage(QString("Calculated audio file path: %1").arg(audioFile));
    
    for (const QString& path : possiblePaths) {
        QFileInfo fileInfo(path);
        logTestMessage(QString("Checking audio path: %1 (exists: %2)").arg(path).arg(fileInfo.exists()));
        if (fileInfo.exists() && fileInfo.isFile()) {
            logTestMessage(QString("Found real sample audio: %1").arg(path));
            return fileInfo.absoluteFilePath();
        }
    }
    
    logTestMessage("No real sample audio file found");
    return QString();
}

bool TestUtils::validateRealMediaFile(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return false;
    }
    
    // Use FFprobe to validate real media files if available
    if (isFFmpegAvailable()) {
        QProcess ffprobe;
        QStringList args;
        args << "-v" << "error"
             << "-show_entries" << "stream=codec_type"
             << "-of" << "csv=p=0"
             << filePath;
        
        QString ffmpegPath = qgetenv("MURMUR_TEST_FFMPEG_PATH");
        QString ffprobePath = ffmpegPath.replace("ffmpeg", "ffprobe");
        if (ffprobePath.isEmpty()) {
            ffprobePath = "ffprobe"; // fallback
        }
        ffprobe.start(ffprobePath, args);
        if (ffprobe.waitForFinished(10000) && ffprobe.exitCode() == 0) {
            QString output = ffprobe.readAllStandardOutput().trimmed();
            return output.contains("video") || output.contains("audio");
        }
    }
    
    // Fallback to basic file validation
    return fileInfo.size() > 1024; // At least 1KB
}

} // namespace Test
} // namespace Murmur