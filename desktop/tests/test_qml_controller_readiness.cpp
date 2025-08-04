#include <QtQuickTest/QtQuickTest>
#include <QtQml/QQmlEngine>
#include <QtQml/QQmlContext>
#include <QSignalSpy>
#include <QTimer>
#include <QEventLoop>
#include <QProcessEnvironment>
#include <QDir>

// Need to register our types before running QML tests
#include "../src/ui/controllers/AppController.hpp"
#include "../src/ui/controllers/MediaController.hpp" 
#include "../src/ui/controllers/TorrentController.hpp"
#include "../src/ui/controllers/TranscriptionController.hpp"
#include "../src/ui/controllers/FileManagerController.hpp"
#include "../src/ui/qt_metatypes.hpp"
#include "../src/core/common/Logger.hpp"

class Setup : public QObject
{
    Q_OBJECT

public:
    Setup() {
        // Initialize Logger for testing
        QString logPath = QDir::temp().absoluteFilePath("murmur_test.log");
        Murmur::Logger::instance().initialize(logPath.toStdString(), Murmur::Logger::Level::Debug);
    }

public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        // Register Murmur types if not already registered
        qmlRegisterType<Murmur::AppController>("Murmur", 1, 0, "AppController");
        qmlRegisterType<Murmur::MediaController>("Murmur", 1, 0, "MediaController");
        qmlRegisterType<Murmur::TorrentController>("Murmur", 1, 0, "TorrentController");
        qmlRegisterType<Murmur::TranscriptionController>("Murmur", 1, 0, "TranscriptionController");
        qmlRegisterType<Murmur::FileManagerController>("Murmur", 1, 0, "FileManagerController");
        
        // Create a global AppController instance for the tests
        auto appController = new Murmur::AppController(engine);
        
        // Don't initialize here - let the QML test control initialization
        engine->rootContext()->setContextProperty("testAppController", appController);
        
        // Also create standalone controllers for testing
        auto mediaController = new Murmur::MediaController(engine);
        auto torrentController = new Murmur::TorrentController(engine);
        auto transcriptionController = new Murmur::TranscriptionController(engine);
        auto fileManagerController = new Murmur::FileManagerController(engine);
        
        engine->rootContext()->setContextProperty("testMediaController", mediaController);
        engine->rootContext()->setContextProperty("testTorrentController", torrentController);
        engine->rootContext()->setContextProperty("testTranscriptionController", transcriptionController);
        engine->rootContext()->setContextProperty("testFileManagerController", fileManagerController);
    }
};

QUICK_TEST_MAIN_WITH_SETUP(MurmurQMLTests, Setup)

#include "test_qml_controller_readiness.moc"
