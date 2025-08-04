#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQml/qqml.h>
#include <QtCore/QDir>
#include <QtCore/QStandardPaths>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtCore/QThread>

#include "core/common/Logger.hpp"
#include "core/common/Config.hpp"
#include "ui/controllers/AppController.hpp"
#include "ui/controllers/MediaController.hpp"
#include "ui/controllers/TorrentController.hpp"
#include "ui/controllers/TranscriptionController.hpp"
#include "ui/controllers/FileManagerController.hpp"
#include "ui/qt_metatypes.hpp"

int main(int argc, char *argv[])
{
    // Set the style before creating the application
    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
    
    QGuiApplication app(argc, argv);
    
    // Set application properties
    app.setApplicationName("MurmurDesktop");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("Murmur");
    app.setOrganizationDomain("murmur.app");
    
    try {
        // Initialize core systems
        Murmur::Logger::instance().initialize();
        Murmur::Config::instance().initialize();
        
        Murmur::Logger::instance().info("Starting Murmur Desktop v{}", 
                    app.applicationVersion().toStdString());
        
        // Register QML types
        qmlRegisterType<Murmur::AppController>("Murmur", 1, 0, "AppController");
        qmlRegisterType<Murmur::MediaController>("Murmur", 1, 0, "MediaController");
        qmlRegisterType<Murmur::TorrentController>("Murmur", 1, 0, "TorrentController");
        qmlRegisterType<Murmur::TranscriptionController>("Murmur", 1, 0, "TranscriptionController");
        qmlRegisterType<Murmur::FileManagerController>("Murmur", 1, 0, "FileManagerController");
        
        // Create QML engine
        QQmlApplicationEngine engine;
        
        // Create and expose controllers
        auto appController = std::make_unique<Murmur::AppController>();
        auto mediaController = std::make_unique<Murmur::MediaController>();
        auto torrentController = std::make_unique<Murmur::TorrentController>();
        auto transcriptionController = std::make_unique<Murmur::TranscriptionController>();
        auto fileManagerController = std::make_unique<Murmur::FileManagerController>();
        
        Murmur::Logger::instance().info("Initializing AppController");
        // Initialize the app controller which sets up all dependencies
        appController->initialize();
        Murmur::Logger::instance().info("AppController initialization started");
        
        // Wait for initialization to complete using a proper event loop
        QEventLoop loop;
        QTimer timeoutTimer;
        timeoutTimer.setSingleShot(true);
        timeoutTimer.setInterval(30000); // 30 second timeout
        QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(appController.get(), &Murmur::AppController::initializedChanged, &loop, &QEventLoop::quit);
        QObject::connect(appController.get(), &Murmur::AppController::initializationFailed, &loop, &QEventLoop::quit);
        QObject::connect(appController.get(), &Murmur::AppController::initializationComplete, &loop, &QEventLoop::quit);
        
        Murmur::Logger::instance().info("Starting event loop for AppController initialization");
        timeoutTimer.start();
        loop.exec();
        Murmur::Logger::instance().info("Event loop finished");
        
        // Check the AppController state
        if (appController && appController->isInitialized()) {
            Murmur::Logger::instance().info("AppController is initialized");
        } else {
            Murmur::Logger::instance().error("AppController is not initialized");
            if (timeoutTimer.isActive()) {
                Murmur::Logger::instance().error("AppController initialization failed with error signal");
            } else {
                Murmur::Logger::instance().error("AppController initialization timed out");
            }
            return -1;
        }
        
        timeoutTimer.stop();
        Murmur::Logger::instance().info("AppController initialization completed successfully");
        
        // Add a small delay to ensure all controllers are properly set up
        QThread::msleep(1000);
        
        // Verify initialization completed
        if (!appController->isInitialized()) {
            Murmur::Logger::instance().error("AppController initialization timed out");
            return -1;
        }
        
        // Set up controller dependencies
        Murmur::Logger::instance().info("Setting TorrentEngine");
        if (appController->torrentEngine()) {
            torrentController->setTorrentEngine(appController->torrentEngine());
            Murmur::Logger::instance().info("TorrentEngine connected successfully");
        } else {
            Murmur::Logger::instance().error("TorrentEngine is null");
        }
        
        Murmur::Logger::instance().info("Setting MediaPipeline");
        if (appController->mediaPipeline()) {
            mediaController->setMediaPipeline(appController->mediaPipeline());
            Murmur::Logger::instance().info("MediaPipeline connected successfully");
        } else {
            Murmur::Logger::instance().error("MediaPipeline is null");
        }
        
        Murmur::Logger::instance().info("Setting VideoPlayer");
        if (appController->videoPlayer()) {
            mediaController->setVideoPlayer(appController->videoPlayer());
            Murmur::Logger::instance().info("VideoPlayer connected successfully");
        } else {
            Murmur::Logger::instance().error("VideoPlayer is null");
        }
        
        Murmur::Logger::instance().info("Setting StorageManager");
        if (appController->storageManager()) {
            mediaController->setStorageManager(appController->storageManager());
            transcriptionController->setStorageManager(appController->storageManager());
            Murmur::Logger::instance().info("StorageManager connected successfully");
        } else {
            Murmur::Logger::instance().error("StorageManager is null");
        }
        
        Murmur::Logger::instance().info("Setting WhisperEngine");
        if (appController->whisperEngine()) {
            transcriptionController->setWhisperEngine(appController->whisperEngine());
            Murmur::Logger::instance().info("WhisperEngine connected successfully");
        } else {
            Murmur::Logger::instance().error("WhisperEngine is null");
        }
        
        Murmur::Logger::instance().info("Setting FileManager");
        if (appController->fileManager()) {
            fileManagerController->setFileManager(appController->fileManager());
            Murmur::Logger::instance().info("FileManager connected successfully");
        } else {
            Murmur::Logger::instance().error("FileManager is null");
        }
        
        // Connect media controller to transcription controller
        transcriptionController->setMediaController(mediaController.get());
        Murmur::Logger::instance().info("Media controller connected to transcription controller");
        
        engine.rootContext()->setContextProperty("appController", appController.get());
        engine.rootContext()->setContextProperty("mediaController", mediaController.get());
        engine.rootContext()->setContextProperty("torrentController", torrentController.get());
        engine.rootContext()->setContextProperty("transcriptionController", transcriptionController.get());
        engine.rootContext()->setContextProperty("fileManagerController", fileManagerController.get());
        
        // Load main QML file
        const QUrl url(QStringLiteral("qrc:/qt/qml/Murmur/qml/main.qml"));
        Murmur::Logger::instance().info("Loading QML file: {}", url.toString().toStdString());
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                         &app, [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(-1);
            }
        }, Qt::QueuedConnection);
        
        engine.load(url);
        
        if (engine.rootObjects().isEmpty()) {
            Murmur::Logger::instance().error("Failed to load QML interface");
            return -1;
        }
        
        Murmur::Logger::instance().info("Application started successfully");
        
        // Run application
        int result = app.exec();
        
        // Cleanup
        Murmur::Config::instance().sync();
        Murmur::Logger::instance().info("Application shutdown complete");
        
        return result;
        
    } catch (const std::exception& e) {
        Murmur::Logger::instance().critical("Fatal error: {}", e.what());
        return -1;
    }
}