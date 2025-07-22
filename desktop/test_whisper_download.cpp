#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>
#include <QEventLoop>
#include <QFileInfo>
#include "src/core/transcription/WhisperEngine.hpp"
#include "src/core/common/Logger.hpp"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("MurmurDesktop");
    app.setOrganizationName("MurmurDesktop");
    
    qDebug() << "=== Testing Whisper Model Download ===";
    
    // Initialize logger
    Murmur::Logger::instance().info("Starting Whisper model download test");
    
    // Create WhisperEngine
    Murmur::WhisperEngine engine;
    
    // Initialize the engine
    qDebug() << "Initializing WhisperEngine...";
    auto initResult = engine.initialize();
    
    if (initResult.hasError()) {
        qDebug() << "❌ Failed to initialize WhisperEngine:" << static_cast<int>(initResult.error());
        return 1;
    }
    
    qDebug() << "✅ WhisperEngine initialized successfully";
    
    // Check if base model already exists
    QString modelsPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/models";
    QString baseModelPath = modelsPath + "/ggml-base.bin";
    
    qDebug() << "Models directory:" << modelsPath;
    qDebug() << "Expected base model path:" << baseModelPath;
    
    QFileInfo modelFileInfo(baseModelPath);
    if (modelFileInfo.exists()) {
        qDebug() << "📁 Base model already exists, size:" << modelFileInfo.size() << "bytes";
        if (modelFileInfo.size() < 100 * 1024 * 1024) { // Less than 100MB is suspicious
            qDebug() << "⚠️  Model file seems too small, removing and re-downloading...";
            QFile::remove(baseModelPath);
        }
    } else {
        qDebug() << "📁 Base model does not exist, will trigger download";
    }
    
    // Try to load the base model - this should trigger download if needed
    qDebug() << "Attempting to load base model (this may trigger download)...";
    auto loadResult = engine.loadModel("base");
    
    if (loadResult.hasError()) {
        qDebug() << "❌ Failed to load base model:" << static_cast<int>(loadResult.error());
        
        // Check if file was created but corrupted
        QFileInfo finalFileInfo(baseModelPath);
        if (finalFileInfo.exists()) {
            qDebug() << "📁 Model file exists after failed load, size:" << finalFileInfo.size() << "bytes";
        }
        
        return 1;
    }
    
    qDebug() << "✅ Base model loaded successfully!";
    qDebug() << "Current model:" << engine.getCurrentModel();
    
    // Verify the downloaded file
    QFileInfo finalModelInfo(baseModelPath);
    if (finalModelInfo.exists()) {
        qDebug() << "📁 Final model file size:" << finalModelInfo.size() << "bytes";
        qDebug() << "📁 Expected size: ~142MB (148,000,000 bytes)";
        
        if (finalModelInfo.size() > 100 * 1024 * 1024) {
            qDebug() << "✅ Model file size looks correct!";
        } else {
            qDebug() << "⚠️  Model file might be corrupted (too small)";
        }
    }
    
    qDebug() << "=== Test completed successfully ===";
    return 0;
}