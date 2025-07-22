#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlContext>
#include <QtQml/qqml.h>
#include <QtCore/QDir>
#include <QtCore/QStandardPaths>

#include "core/common/Logger.hpp"
#include "core/common/Config.hpp"
#include "ui/controllers/AppController.hpp"
#include "ui/controllers/MediaController.hpp"
#include "ui/controllers/TorrentController.hpp"
#include "ui/controllers/TranscriptionController.hpp"
#include "ui/controllers/FileManagerController.hpp"

int main(int argc, char *argv[])
{
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
        
        engine.rootContext()->setContextProperty("appController", appController.get());
        engine.rootContext()->setContextProperty("mediaController", mediaController.get());
        engine.rootContext()->setContextProperty("torrentController", torrentController.get());
        engine.rootContext()->setContextProperty("transcriptionController", transcriptionController.get());
        engine.rootContext()->setContextProperty("fileManagerController", fileManagerController.get());
        
        // Load main QML file
        const QUrl url(QStringLiteral("qrc:/Murmur/qml/main.qml"));
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