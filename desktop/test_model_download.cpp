#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include "src/core/transcription/WhisperEngine.hpp"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("MurmurDesktop");
    app.setOrganizationName("MurmurDesktop");
    
    qDebug() << "Testing Whisper model download...";
    
    Murmur::WhisperEngine engine;
    auto initResult = engine.initialize();
    
    if (initResult.hasError()) {
        qDebug() << "Failed to initialize WhisperEngine";
        return 1;
    }
    
    qDebug() << "WhisperEngine initialized successfully";
    
    // Try to load the base model - this should trigger download
    auto loadResult = engine.loadModel("base");
    
    if (loadResult.hasError()) {
        qDebug() << "Failed to load base model:" << static_cast<int>(loadResult.error());
        return 1;
    }
    
    qDebug() << "Base model loaded successfully!";
    qDebug() << "Current model:" << engine.getCurrentModel();
    
    return 0;
}