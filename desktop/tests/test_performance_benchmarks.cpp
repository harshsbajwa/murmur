#include <QtTest/QtTest>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#include <QtCore/QRandomGenerator>
#include <QtConcurrent/QtConcurrent>

#include "utils/TestUtils.hpp"
#include "../src/core/media/MediaPipeline.hpp"
#include "../src/core/transcription/WhisperEngine.hpp"
#include "../src/core/storage/StorageManager.hpp"
#include "../src/core/torrent/TorrentEngine.hpp"
#include "../src/core/common/Expected.hpp"

using namespace Murmur;
using namespace Murmur::Test;

/**
 * @brief Comprehensive performance benchmarking tests
 * 
 * Measures real-world performance of all major components using
 * actual sample media files and realistic workloads.
 */
class TestPerformanceBenchmarks : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Core component benchmarks
    void benchmarkVideoAnalysisPerformance();
    void benchmarkVideoConversionPerformance();
    void benchmarkAudioProcessingPerformance();
    void benchmarkTranscriptionPerformance();
    void benchmarkStorageOperationsPerformance();
    void benchmarkTorrentOperationsPerformance();
    
    // Scalability benchmarks
    void benchmarkConcurrentVideoProcessing();
    void benchmarkConcurrentAudioProcessing();
    void benchmarkConcurrentTranscription();
    void benchmarkLargeFileProcessing();
    void benchmarkBatchProcessing();
    
    // Memory and resource benchmarks
    void benchmarkMemoryUsageUnderLoad();
    void benchmarkResourceCleanupEfficiency();
    void benchmarkLongRunningOperations();
    void benchmarkResourceLeakDetection();
    
    // Real-world scenario benchmarks
    void benchmarkCompleteWorkflowPerformance();
    void benchmarkSystemUnderStress();
    void benchmarkDegradationUnderLoad();

private:
    std::unique_ptr<MediaPipeline> mediaPipeline_;
    std::unique_ptr<WhisperEngine> whisperEngine_;
    std::unique_ptr<StorageManager> storageManager_;
    std::unique_ptr<TorrentEngine> torrentEngine_;
    std::unique_ptr<QTemporaryDir> tempDir_;
    
    QString realVideoFile_;
    QString realAudioFile_;
    
    struct BenchmarkResult {
        QString operationName;
        double averageTimeMs;
        double minTimeMs;
        double maxTimeMs;
        double standardDeviation;
        qint64 memoryUsedMB;
        int successfulOperations;
        int totalOperations;
        QJsonObject additionalMetrics;
    };
    
    QList<BenchmarkResult> benchmarkResults_;
    
    // Benchmark helpers
    BenchmarkResult runBenchmark(const QString& name, int iterations, std::function<bool()> operation);
    void logBenchmarkResult(const BenchmarkResult& result);
    void generateBenchmarkReport();
    QJsonObject measureResourceUsage(std::function<void()> operation);
    void stressTestSystem(int concurrency, int duration);
};

void TestPerformanceBenchmarks::initTestCase() {
    TestUtils::initializeTestEnvironment();
    TestUtils::startResourceMonitoring();
    
    // Verify real sample files are available
    QString realVideo = TestUtils::getRealSampleVideoFile();
    QString realAudio = TestUtils::getRealSampleAudioFile();
    
    if (realVideo.isEmpty() || realAudio.isEmpty()) {
        QSKIP("Real sample media files required for performance benchmarks");
    }
    
    TestUtils::logMessage("Performance benchmarking initialized with real media files");
}

void TestPerformanceBenchmarks::cleanupTestCase() {
    TestUtils::stopResourceMonitoring();
    
    // Generate comprehensive benchmark report
    generateBenchmarkReport();
    
    TestUtils::cleanupTestEnvironment();
}

void TestPerformanceBenchmarks::init() {
    tempDir_ = std::make_unique<QTemporaryDir>();
    QVERIFY(tempDir_->isValid());
    
    realVideoFile_ = TestUtils::getRealSampleVideoFile();
    realAudioFile_ = TestUtils::getRealSampleAudioFile();
    
    // Initialize components
    mediaPipeline_ = std::make_unique<MediaPipeline>(this);
    whisperEngine_ = std::make_unique<WhisperEngine>(this);
    storageManager_ = std::make_unique<StorageManager>(this);
    torrentEngine_ = std::make_unique<TorrentEngine>(this);
    
    // Initialize storage
    QString dbPath = QString("%1/benchmark_%2.db")
                    .arg(tempDir_->path())
                    .arg(QDateTime::currentMSecsSinceEpoch());
    auto initResult = storageManager_->initialize(dbPath);
    ASSERT_EXPECTED_VALUE(initResult);
}

void TestPerformanceBenchmarks::cleanup() {
    mediaPipeline_.reset();
    whisperEngine_.reset();
    storageManager_.reset();
    torrentEngine_.reset();
    tempDir_.reset();
}

void TestPerformanceBenchmarks::benchmarkVideoAnalysisPerformance() {
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for video analysis benchmark");
    }
    
    auto result = runBenchmark("Video Analysis", 10, [this]() -> bool {
        auto future = mediaPipeline_->analyzeVideo(realVideoFile_);
        auto analysisResult = TestUtils::waitForFuture(future, 20000);
        return analysisResult.hasValue();
    });
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkVideoConversionPerformance() {
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for video conversion benchmark");
    }
    
    auto result = runBenchmark("Video Conversion", 5, [this]() -> bool {
        QString outputPath = QString("%1/benchmark_convert_%2.mp4")
                           .arg(tempDir_->path())
                           .arg(QRandomGenerator::global()->generate());
        
        ConversionSettings settings;
        settings.outputFormat = "mp4";
        settings.videoCodec = "libx264";
        // Remove preset as it's not in ConversionSettings struct
        settings.maxWidth = 1280;
        settings.maxHeight = 720;
        
        auto future = mediaPipeline_->convertVideo(realVideoFile_, outputPath, settings);
        auto conversionResult = TestUtils::waitForFuture(future, 120000);
        
        if (conversionResult.hasValue()) {
            QFile::remove(outputPath); // Cleanup
            return true;
        }
        return false;
    });
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkAudioProcessingPerformance() {
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for audio processing benchmark");
    }
    
    auto result = runBenchmark("Audio Processing", 10, [this]() -> bool {
        QString outputPath = QString("%1/benchmark_audio_%2.mp3")
                           .arg(tempDir_->path())
                           .arg(QRandomGenerator::global()->generate());
        
        // Use extractAudio as MediaPipeline doesn't have convertAudio
        auto future = mediaPipeline_->extractAudio(realAudioFile_, outputPath, "mp3");
        auto conversionResult = TestUtils::waitForFuture(future, 30000);
        
        if (conversionResult.hasValue()) {
            QFile::remove(outputPath); // Cleanup
            return true;
        }
        return false;
    });
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkTranscriptionPerformance() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for transcription benchmark");
    }
    
    QString modelDir = tempDir_->path() + "/whisper_models";
    QDir().mkpath(modelDir);
    
    auto initResult = whisperEngine_->initialize(modelDir);
    if (initResult.hasError()) {
        QSKIP("Failed to initialize Whisper for transcription benchmark");
    }
    
    auto result = runBenchmark("Transcription", 3, [this]() -> bool {
        TranscriptionSettings settings;
        settings.language = "en";
        settings.outputFormat = "txt";
        
        auto future = whisperEngine_->transcribeAudio(realAudioFile_, settings);
        auto transcriptionResult = TestUtils::waitForFuture(future, 180000);
        
        return transcriptionResult.hasValue();
    });
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkStorageOperationsPerformance() {
    auto result = runBenchmark("Storage Operations", 1000, [this]() -> bool {
        // Create test torrent record
        TorrentRecord torrent;
        torrent.infoHash = QString("bench%1").arg(QRandomGenerator::global()->generate(), 36, 16, QChar('0'));
        torrent.name = QString("Benchmark Torrent %1").arg(QRandomGenerator::global()->generate());
        torrent.magnetUri = QString("magnet:?xt=urn:btih:%1&dn=Benchmark").arg(torrent.infoHash);
        torrent.size = QRandomGenerator::global()->bounded(1000000, 100000000);
        torrent.dateAdded = QDateTime::currentDateTime();
        torrent.progress = QRandomGenerator::global()->generateDouble();
        torrent.status = "downloading";
        
        auto addResult = storageManager_->addTorrent(torrent);
        if (addResult.hasError()) {
            return false;
        }
        
        // Update the torrent
        torrent.progress = 1.0;
        torrent.status = "completed";
        auto updateResult = storageManager_->updateTorrent(torrent);
        if (updateResult.hasError()) {
            return false;
        }
        
        // Query the torrent
        auto getResult = storageManager_->getTorrent(torrent.infoHash);
        if (getResult.hasError()) {
            return false;
        }
        
        // Clean up
        storageManager_->removeTorrent(torrent.infoHash);
        return true;
    });
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkTorrentOperationsPerformance() {
    // TorrentEngine doesn't need explicit initialization
    
    auto result = runBenchmark("Torrent Operations", 50, [this]() -> bool {
        // Create test magnet link
        QString magnetLink = TestUtils::createTestMagnetLink(QString("Benchmark %1").arg(QRandomGenerator::global()->generate()));
        QString savePath = tempDir_->path() + "/torrent_downloads";
        QDir().mkpath(savePath);
        
        auto addResultFuture = torrentEngine_->addTorrent(magnetLink);
        auto addResult = TestUtils::waitForFuture(addResultFuture, 5000);
        if (addResult.hasError()) {
            return false;
        }
        
        TorrentEngine::TorrentInfo torrentInfo = addResult.value();
        QString torrentId = torrentInfo.infoHash;
        
        // Get torrent status
        auto statusResult = torrentEngine_->getTorrentInfo(torrentId);
        if (statusResult.hasError()) {
            return false;
        }
        
        // Remove torrent
        auto removeResult = torrentEngine_->removeTorrent(torrentId);
        return removeResult.hasValue();
    });
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkConcurrentVideoProcessing() {
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for concurrent video processing benchmark");
    }
    
    QElapsedTimer timer;
    timer.start();
    
    const int concurrency = 3;
    QList<QFuture<bool>> futures;
    
    for (int i = 0; i < concurrency; ++i) {
        auto future = QtConcurrent::run([this, i]() -> bool {
            QString outputPath = QString("%1/concurrent_video_%2.mp4")
                               .arg(tempDir_->path()).arg(i);
            
            ConversionSettings settings;
            settings.outputFormat = "mp4";
            settings.videoCodec = "libx264";
            // Remove preset as it's not in ConversionSettings struct
            settings.maxWidth = 640;
            settings.maxHeight = 480;
            
            auto conversionFuture = mediaPipeline_->convertVideo(realVideoFile_, outputPath, settings);
            auto result = TestUtils::waitForFuture(conversionFuture, 120000);
            
            if (result.hasValue()) {
                QFile::remove(outputPath);
                return true;
            }
            return false;
        });
        futures.append(future);
    }
    
    int successCount = 0;
    for (auto& future : futures) {
        if (future.result()) {
            successCount++;
        }
    }
    
    qint64 totalTime = timer.elapsed();
    
    BenchmarkResult result;
    result.operationName = "Concurrent Video Processing";
    result.averageTimeMs = static_cast<double>(totalTime);
    result.successfulOperations = successCount;
    result.totalOperations = concurrency;
    result.additionalMetrics["concurrency"] = concurrency;
    result.additionalMetrics["throughput_ops_per_sec"] = (successCount * 1000.0) / totalTime;
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkConcurrentAudioProcessing() {
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for concurrent audio processing benchmark");
    }
    
    QElapsedTimer timer;
    timer.start();
    
    const int concurrency = 5;
    QList<QFuture<bool>> futures;
    
    for (int i = 0; i < concurrency; ++i) {
        auto future = QtConcurrent::run([this, i]() -> bool {
            QString outputPath = QString("%1/concurrent_audio_%2.mp3")
                               .arg(tempDir_->path()).arg(i);
            
            // Use extractAudio as MediaPipeline doesn't have convertAudio
            auto conversionFuture = mediaPipeline_->extractAudio(realAudioFile_, outputPath, "mp3");
            auto result = TestUtils::waitForFuture(conversionFuture, 30000);
            
            if (result.hasValue()) {
                QFile::remove(outputPath);
                return true;
            }
            return false;
        });
        futures.append(future);
    }
    
    int successCount = 0;
    for (auto& future : futures) {
        if (future.result()) {
            successCount++;
        }
    }
    
    qint64 totalTime = timer.elapsed();
    
    BenchmarkResult result;
    result.operationName = "Concurrent Audio Processing";
    result.averageTimeMs = static_cast<double>(totalTime);
    result.successfulOperations = successCount;
    result.totalOperations = concurrency;
    result.additionalMetrics["concurrency"] = concurrency;
    result.additionalMetrics["throughput_ops_per_sec"] = (successCount * 1000.0) / totalTime;
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkConcurrentTranscription() {
    if (!TestUtils::isWhisperAvailable()) {
        QSKIP("Whisper not available for concurrent transcription benchmark");
    }
    
    QString modelDir = tempDir_->path() + "/whisper_models";
    QDir().mkpath(modelDir);
    
    auto initResult = whisperEngine_->initialize(modelDir);
    if (initResult.hasError()) {
        QSKIP("Failed to initialize Whisper for concurrent transcription benchmark");
    }
    
    QElapsedTimer timer;
    timer.start();
    
    const int concurrency = 2; // Transcription is CPU intensive
    QList<QFuture<bool>> futures;
    
    for (int i = 0; i < concurrency; ++i) {
        auto future = QtConcurrent::run([this]() -> bool {
            TranscriptionSettings settings;
            settings.language = "en";
            settings.outputFormat = "txt";
            
            auto transcriptionFuture = whisperEngine_->transcribeAudio(realAudioFile_, settings);
            auto result = TestUtils::waitForFuture(transcriptionFuture, 300000); // 5 minutes
            
            return result.hasValue();
        });
        futures.append(future);
    }
    
    int successCount = 0;
    for (auto& future : futures) {
        if (future.result()) {
            successCount++;
        }
    }
    
    qint64 totalTime = timer.elapsed();
    
    BenchmarkResult result;
    result.operationName = "Concurrent Transcription";
    result.averageTimeMs = static_cast<double>(totalTime);
    result.successfulOperations = successCount;
    result.totalOperations = concurrency;
    result.additionalMetrics["concurrency"] = concurrency;
    result.additionalMetrics["throughput_ops_per_sec"] = (successCount * 1000.0) / totalTime;
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkLargeFileProcessing() {
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for large file processing benchmark");
    }
    
    // Create larger version of sample video
    QString largeVideoPath = tempDir_->path() + "/large_test_video.mp4";
    
    QProcess ffmpeg;
    QStringList args;
    args << "-stream_loop" << "3" // Loop input 3 times
         << "-i" << realVideoFile_
         << "-c" << "copy"
         << "-y" << largeVideoPath;
    
    ffmpeg.start("ffmpeg", args);
    if (!ffmpeg.waitForFinished(60000) || ffmpeg.exitCode() != 0) {
        QSKIP("Failed to create large test file for benchmark");
    }
    
    auto result = runBenchmark("Large File Processing", 2, [this, largeVideoPath]() -> bool {
        QString outputPath = QString("%1/large_output_%2.mp4")
                           .arg(tempDir_->path())
                           .arg(QRandomGenerator::global()->generate());
        
        ConversionSettings settings;
        settings.outputFormat = "mp4";
        settings.videoCodec = "libx264";
        // Remove preset as it's not in ConversionSettings struct
        settings.maxWidth = 1280;
        settings.maxHeight = 720;
        
        auto future = mediaPipeline_->convertVideo(largeVideoPath, outputPath, settings);
        auto conversionResult = TestUtils::waitForFuture(future, 300000); // 5 minutes
        
        if (conversionResult.hasValue()) {
            QFileInfo outputInfo(outputPath);
            QFile::remove(outputPath);
            return outputInfo.size() > 0;
        }
        return false;
    });
    
    QFile::remove(largeVideoPath);
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkBatchProcessing() {
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for batch processing benchmark");
    }
    
    QElapsedTimer timer;
    timer.start();
    
    const int batchSize = 5;
    QList<QFuture<bool>> futures;
    
    for (int i = 0; i < batchSize; ++i) {
        auto future = QtConcurrent::run([this, i]() -> bool {
            QString outputPath = QString("%1/batch_%2.mp4")
                               .arg(tempDir_->path()).arg(i);
            
            ConversionSettings settings;
            settings.outputFormat = "mp4";
            settings.videoCodec = "libx264";
            // Remove preset as it's not in ConversionSettings struct
            settings.maxWidth = 480;
            settings.maxHeight = 320;
            
            auto conversionFuture = mediaPipeline_->convertVideo(realVideoFile_, outputPath, settings);
            auto result = TestUtils::waitForFuture(conversionFuture, 60000);
            
            if (result.hasValue()) {
                QFile::remove(outputPath);
                return true;
            }
            return false;
        });
        futures.append(future);
    }
    
    int successCount = 0;
    for (auto& future : futures) {
        if (future.result()) {
            successCount++;
        }
    }
    
    qint64 totalTime = timer.elapsed();
    
    BenchmarkResult result;
    result.operationName = "Batch Processing";
    result.averageTimeMs = static_cast<double>(totalTime);
    result.successfulOperations = successCount;
    result.totalOperations = batchSize;
    result.additionalMetrics["batch_size"] = batchSize;
    result.additionalMetrics["throughput_ops_per_sec"] = (successCount * 1000.0) / totalTime;
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkMemoryUsageUnderLoad() {
    QJsonObject beforeStats = TestUtils::getResourceUsageReport();
    
    // Simulate memory-intensive operations
    QList<QFuture<void>> futures;
    
    for (int i = 0; i < 3; ++i) {
        auto future = QtConcurrent::run([this, i]() {
            QString outputPath = QString("%1/memory_test_%2.mp4")
                               .arg(tempDir_->path()).arg(i);
            
            ConversionSettings settings;
            settings.preserveQuality = true; // More memory intensive
            
            auto conversionFuture = mediaPipeline_->convertVideo(realVideoFile_, outputPath, settings);
            TestUtils::waitForFuture(conversionFuture, 120000);
            
            QFile::remove(outputPath);
        });
        futures.append(future);
    }
    
    // Wait for completion
    for (auto& future : futures) {
        future.waitForFinished();
    }
    
    QJsonObject afterStats = TestUtils::getResourceUsageReport();
    
    BenchmarkResult result;
    result.operationName = "Memory Usage Under Load";
    result.successfulOperations = 1;
    result.totalOperations = 1;
    
    if (beforeStats.contains("memory_mb") && afterStats.contains("memory_mb")) {
        double memoryDelta = afterStats["memory_mb"].toDouble() - beforeStats["memory_mb"].toDouble();
        result.memoryUsedMB = static_cast<qint64>(memoryDelta);
        result.additionalMetrics["peak_memory_mb"] = afterStats["memory_mb"].toDouble();
        result.additionalMetrics["memory_delta_mb"] = memoryDelta;
    }
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkResourceCleanupEfficiency() {
    QJsonObject beforeStats = TestUtils::getResourceUsageReport();
    
    // Create and destroy many temporary resources
    for (int i = 0; i < 10; ++i) {
        QString tempFile = QString("%1/temp_%2.txt").arg(tempDir_->path()).arg(i);
        QFile file(tempFile);
        file.open(QIODevice::WriteOnly);
        file.write(QByteArray(1024 * 1024, 'x')); // 1MB
        file.close();
        
        // Immediately delete
        QFile::remove(tempFile);
    }
    
    // Force garbage collection
    QCoreApplication::processEvents();
    
    QJsonObject afterStats = TestUtils::getResourceUsageReport();
    
    BenchmarkResult result;
    result.operationName = "Resource Cleanup Efficiency";
    result.successfulOperations = 10;
    result.totalOperations = 10;
    
    if (beforeStats.contains("memory_mb") && afterStats.contains("memory_mb")) {
        double memoryDelta = afterStats["memory_mb"].toDouble() - beforeStats["memory_mb"].toDouble();
        result.additionalMetrics["memory_delta_after_cleanup_mb"] = memoryDelta;
        result.additionalMetrics["cleanup_efficient"] = (memoryDelta < 10.0); // Less than 10MB delta
    }
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkLongRunningOperations() {
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for long running operations benchmark");
    }
    
    // Create extended duration version of video
    QString extendedVideoPath = tempDir_->path() + "/extended_video.mp4";
    
    QProcess ffmpeg;
    QStringList args;
    args << "-stream_loop" << "5" // Loop 5 times for longer duration
         << "-i" << realVideoFile_
         << "-c" << "copy"
         << "-y" << extendedVideoPath;
    
    ffmpeg.start("ffmpeg", args);
    if (!ffmpeg.waitForFinished(120000) || ffmpeg.exitCode() != 0) {
        QSKIP("Failed to create extended video for long running operations benchmark");
    }
    
    QElapsedTimer timer;
    timer.start();
    
    QString outputPath = tempDir_->path() + "/long_running_output.mp4";
    ConversionSettings settings;
    settings.outputFormat = "mp4";
    settings.videoCodec = "libx264";
    // Remove preset as it's not in ConversionSettings struct
    settings.maxWidth = 1920;
    settings.maxHeight = 1080;
    
    auto future = mediaPipeline_->convertVideo(extendedVideoPath, outputPath, settings);
    auto result = TestUtils::waitForFuture(future, 600000); // 10 minutes max
    
    qint64 totalTime = timer.elapsed();
    
    BenchmarkResult benchResult;
    benchResult.operationName = "Long Running Operations";
    benchResult.averageTimeMs = static_cast<double>(totalTime);
    benchResult.successfulOperations = result.hasValue() ? 1 : 0;
    benchResult.totalOperations = 1;
    benchResult.additionalMetrics["duration_minutes"] = totalTime / 60000.0;
    
    if (result.hasValue()) {
        QFileInfo outputInfo(outputPath);
        benchResult.additionalMetrics["output_size_mb"] = outputInfo.size() / (1024.0 * 1024.0);
        QFile::remove(outputPath);
    }
    
    QFile::remove(extendedVideoPath);
    
    benchmarkResults_.append(benchResult);
    logBenchmarkResult(benchResult);
}

void TestPerformanceBenchmarks::benchmarkResourceLeakDetection() {
    QJsonObject initialStats = TestUtils::getResourceUsageReport();
    
    // Perform many operations that could potentially leak
    for (int cycle = 0; cycle < 5; ++cycle) {
        // Video analysis operations
        for (int i = 0; i < 5; ++i) {
            auto future = mediaPipeline_->analyzeVideo(realVideoFile_);
            TestUtils::waitForFuture(future, 10000);
        }
        
        // Storage operations
        for (int i = 0; i < 20; ++i) {
            TorrentRecord torrent;
            torrent.infoHash = QString("leak%1%2").arg(cycle).arg(i, 32, 16, QChar('0'));
            torrent.name = QString("Leak Test %1-%2").arg(cycle).arg(i);
            torrent.magnetUri = QString("magnet:?xt=urn:btih:%1&dn=LeakTest").arg(torrent.infoHash);
            torrent.size = 1024 * 1024;
            torrent.dateAdded = QDateTime::currentDateTime();
            
            storageManager_->addTorrent(torrent);
            storageManager_->removeTorrent(torrent.infoHash);
        }
        
        // Force cleanup
        QCoreApplication::processEvents();
    }
    
    QJsonObject finalStats = TestUtils::getResourceUsageReport();
    
    BenchmarkResult result;
    result.operationName = "Resource Leak Detection";
    result.successfulOperations = 1;
    result.totalOperations = 1;
    
    if (initialStats.contains("memory_mb") && finalStats.contains("memory_mb")) {
        double memoryGrowth = finalStats["memory_mb"].toDouble() - initialStats["memory_mb"].toDouble();
        result.additionalMetrics["memory_growth_mb"] = memoryGrowth;
        result.additionalMetrics["potential_leak"] = (memoryGrowth > 50.0); // > 50MB growth suggests leak
        result.additionalMetrics["operations_performed"] = 125; // 5 cycles * 25 operations
    }
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkCompleteWorkflowPerformance() {
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for complete workflow benchmark");
    }
    
    QElapsedTimer timer;
    timer.start();
    
    bool workflowSuccess = true;
    QString errorMessage;
    
    try {
        // Step 1: Video Analysis
        auto analysisFuture = mediaPipeline_->analyzeVideo(realVideoFile_);
        auto analysisResult = TestUtils::waitForFuture(analysisFuture, 15000);
        if (analysisResult.hasError()) {
            workflowSuccess = false;
            errorMessage = "Video analysis failed";
        }
        
        // Step 2: Video Conversion
        QString convertedPath = tempDir_->path() + "/workflow_converted.mp4";
        ConversionSettings settings;
        settings.outputFormat = "mp4";
        settings.videoCodec = "libx264";
        settings.maxWidth = 1280;
        settings.maxHeight = 720;
        
        auto conversionFuture = mediaPipeline_->convertVideo(realVideoFile_, convertedPath, settings);
        auto conversionResult = TestUtils::waitForFuture(conversionFuture, 120000);
        if (conversionResult.hasError()) {
            workflowSuccess = false;
            errorMessage = "Video conversion failed";
        }
        
        // Step 3: Audio Extraction
        QString audioPath = tempDir_->path() + "/workflow_audio.wav";
        auto audioFuture = mediaPipeline_->extractAudio(convertedPath, audioPath, "wav");
        auto audioResult = TestUtils::waitForFuture(audioFuture, 30000);
        if (audioResult.hasError()) {
            // Audio extraction might fail if video has no audio - use original audio file
            audioPath = realAudioFile_;
        }
        
        // Step 4: Storage Operations
        TorrentRecord torrent;
        torrent.infoHash = QString("workflow%1").arg(QRandomGenerator::global()->generate(), 32, 16, QChar('0'));
        torrent.name = "Workflow Test";
        torrent.magnetUri = QString("magnet:?xt=urn:btih:%1&dn=WorkflowTest").arg(torrent.infoHash);
        torrent.size = QFileInfo(realVideoFile_).size();
        torrent.dateAdded = QDateTime::currentDateTime();
        
        auto addTorrentResult = storageManager_->addTorrent(torrent);
        if (addTorrentResult.hasError()) {
            workflowSuccess = false;
            errorMessage = "Torrent storage failed";
        }
        
        // Cleanup
        QFile::remove(convertedPath);
        if (audioPath != realAudioFile_) {
            QFile::remove(audioPath);
        }
        
    } catch (const std::exception& e) {
        workflowSuccess = false;
        errorMessage = QString("Exception: %1").arg(e.what());
    }
    
    qint64 totalTime = timer.elapsed();
    
    BenchmarkResult result;
    result.operationName = "Complete Workflow Performance";
    result.averageTimeMs = static_cast<double>(totalTime);
    result.successfulOperations = workflowSuccess ? 1 : 0;
    result.totalOperations = 1;
    result.additionalMetrics["workflow_steps"] = 4;
    result.additionalMetrics["total_time_minutes"] = totalTime / 60000.0;
    
    if (!workflowSuccess) {
        result.additionalMetrics["error_message"] = errorMessage;
    }
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkSystemUnderStress() {
    stressTestSystem(4, 30); // 4 concurrent operations for 30 seconds
    
    BenchmarkResult result;
    result.operationName = "System Under Stress";
    result.successfulOperations = 1;
    result.totalOperations = 1;
    result.additionalMetrics["stress_duration_seconds"] = 30;
    result.additionalMetrics["stress_concurrency"] = 4;
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

void TestPerformanceBenchmarks::benchmarkDegradationUnderLoad() {
    if (!TestUtils::isFFmpegAvailable()) {
        QSKIP("FFmpeg not available for degradation under load benchmark");
    }
    
    QList<qint64> processingTimes;
    
    // Measure baseline performance
    QElapsedTimer timer;
    timer.start();
    
    QString baselinePath = tempDir_->path() + "/baseline.mp4";
    ConversionSettings settings;
    settings.outputFormat = "mp4";
    // Remove preset as it's not in ConversionSettings struct
    
    auto baselineFuture = mediaPipeline_->convertVideo(realVideoFile_, baselinePath, settings);
    auto baselineResult = TestUtils::waitForFuture(baselineFuture, 60000);
    
    qint64 baselineTime = timer.elapsed();
    if (baselineResult.hasValue()) {
        processingTimes.append(baselineTime);
        QFile::remove(baselinePath);
    }
    
    // Measure performance under increasing load
    for (int load = 1; load <= 3; ++load) {
        QList<QFuture<qint64>> futures;
        
        for (int i = 0; i < load; ++i) {
            auto future = QtConcurrent::run([this, load, i]() -> qint64 {
                QElapsedTimer loadTimer;
                loadTimer.start();
                
                QString outputPath = QString("%1/load_%2_%3.mp4")
                                   .arg(tempDir_->path()).arg(load).arg(i);
                
                ConversionSettings loadSettings;
                loadSettings.outputFormat = "mp4";
                // Remove preset as it's not in ConversionSettings struct
                
                auto conversionFuture = mediaPipeline_->convertVideo(realVideoFile_, outputPath, loadSettings);
                auto result = TestUtils::waitForFuture(conversionFuture, 120000);
                
                qint64 time = loadTimer.elapsed();
                if (result.hasValue()) {
                    QFile::remove(outputPath);
                }
                
                return time;
            });
            futures.append(future);
        }
        
        // Wait for all operations to complete
        for (auto& future : futures) {
            qint64 time = future.result();
            if (time > 0) {
                processingTimes.append(time);
            }
        }
    }
    
    // Calculate degradation
    double averageDegradation = 0.0;
    if (processingTimes.size() > 1) {
        for (int i = 1; i < processingTimes.size(); ++i) {
            double degradation = static_cast<double>(processingTimes[i]) / processingTimes[0];
            averageDegradation += degradation;
        }
        averageDegradation /= (processingTimes.size() - 1);
    }
    
    BenchmarkResult result;
    result.operationName = "Degradation Under Load";
    result.successfulOperations = processingTimes.size();
    result.totalOperations = processingTimes.size();
    result.additionalMetrics["baseline_time_ms"] = processingTimes.isEmpty() ? 0 : processingTimes[0];
    result.additionalMetrics["average_degradation_factor"] = averageDegradation;
    result.additionalMetrics["max_concurrent_load"] = 3;
    
    benchmarkResults_.append(result);
    logBenchmarkResult(result);
}

// Helper method implementations
TestPerformanceBenchmarks::BenchmarkResult TestPerformanceBenchmarks::runBenchmark(
    const QString& name, int iterations, std::function<bool()> operation) {
    
    BenchmarkResult result;
    result.operationName = name;
    result.totalOperations = iterations;
    result.successfulOperations = 0;
    
    QList<qint64> times;
    QJsonObject startStats = TestUtils::getResourceUsageReport();
    
    for (int i = 0; i < iterations; ++i) {
        QElapsedTimer timer;
        timer.start();
        
        bool success = operation();
        
        qint64 elapsed = timer.elapsed();
        times.append(elapsed);
        
        if (success) {
            result.successfulOperations++;
        }
        
        // Allow system to breathe between iterations
        QCoreApplication::processEvents();
    }
    
    QJsonObject endStats = TestUtils::getResourceUsageReport();
    
    // Calculate statistics
    if (!times.isEmpty()) {
        qint64 sum = 0;
        qint64 minTime = times[0];
        qint64 maxTime = times[0];
        
        for (qint64 time : times) {
            sum += time;
            if (time < minTime) minTime = time;
            if (time > maxTime) maxTime = time;
        }
        
        result.averageTimeMs = static_cast<double>(sum) / times.size();
        result.minTimeMs = static_cast<double>(minTime);
        result.maxTimeMs = static_cast<double>(maxTime);
        
        // Calculate standard deviation
        double variance = 0.0;
        for (qint64 time : times) {
            double diff = time - result.averageTimeMs;
            variance += diff * diff;
        }
        variance /= times.size();
        result.standardDeviation = std::sqrt(variance);
    }
    
    // Calculate memory usage
    if (startStats.contains("memory_mb") && endStats.contains("memory_mb")) {
        result.memoryUsedMB = static_cast<qint64>(
            endStats["memory_mb"].toDouble() - startStats["memory_mb"].toDouble());
    }
    
    return result;
}

void TestPerformanceBenchmarks::logBenchmarkResult(const BenchmarkResult& result) {
    QString logMessage = QString("BENCHMARK %1: avg=%.2fms, min=%.2fms, max=%.2fms, "
                                "stddev=%.2fms, success=%2/%3, memory=%4MB")
                        .arg(result.operationName)
                        .arg(result.averageTimeMs)
                        .arg(result.minTimeMs)
                        .arg(result.maxTimeMs)
                        .arg(result.standardDeviation)
                        .arg(result.successfulOperations)
                        .arg(result.totalOperations)
                        .arg(result.memoryUsedMB);
    
    TestUtils::logMessage(logMessage);
    
    // Log additional metrics
    for (auto it = result.additionalMetrics.begin(); it != result.additionalMetrics.end(); ++it) {
        TestUtils::logMessage(QString("  %1: %2").arg(it.key()).arg(it.value().toString()));
    }
}

void TestPerformanceBenchmarks::generateBenchmarkReport() {
    TestUtils::logMessage("=== PERFORMANCE BENCHMARK REPORT ===");
    
    QJsonObject report;
    QJsonArray benchmarks;
    
    for (const auto& result : benchmarkResults_) {
        QJsonObject benchmarkObj;
        benchmarkObj["operation"] = result.operationName;
        benchmarkObj["average_time_ms"] = result.averageTimeMs;
        benchmarkObj["min_time_ms"] = result.minTimeMs;
        benchmarkObj["max_time_ms"] = result.maxTimeMs;
        benchmarkObj["standard_deviation"] = result.standardDeviation;
        benchmarkObj["successful_operations"] = result.successfulOperations;
        benchmarkObj["total_operations"] = result.totalOperations;
        benchmarkObj["memory_used_mb"] = result.memoryUsedMB;
        benchmarkObj["success_rate"] = static_cast<double>(result.successfulOperations) / result.totalOperations;
        
        // Add additional metrics
        for (auto it = result.additionalMetrics.begin(); it != result.additionalMetrics.end(); ++it) {
            benchmarkObj[it.key()] = it.value();
        }
        
        benchmarks.append(benchmarkObj);
    }
    
    report["benchmarks"] = benchmarks;
    report["system_info"] = TestUtils::getResourceUsageReport();
    report["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    // Write detailed report to file
    QString reportPath = tempDir_->path() + "/benchmark_report.json";
    QFile reportFile(reportPath);
    if (reportFile.open(QIODevice::WriteOnly)) {
        QJsonDocument doc(report);
        reportFile.write(doc.toJson());
        reportFile.close();
        TestUtils::logMessage(QString("Detailed benchmark report written to: %1").arg(reportPath));
    }
    
    // Print summary
    TestUtils::logMessage(QString("Total benchmarks run: %1").arg(benchmarkResults_.size()));
    
    double totalTime = 0;
    int totalOperations = 0;
    for (const auto& result : benchmarkResults_) {
        totalTime += result.averageTimeMs * result.totalOperations;
        totalOperations += result.totalOperations;
    }
    
    TestUtils::logMessage(QString("Total operation time: %.2f seconds").arg(totalTime / 1000.0));
    TestUtils::logMessage(QString("Total operations: %1").arg(totalOperations));
    TestUtils::logMessage("=== END BENCHMARK REPORT ===");
}

QJsonObject TestPerformanceBenchmarks::measureResourceUsage(std::function<void()> operation) {
    QJsonObject before = TestUtils::getResourceUsageReport();
    operation();
    QJsonObject after = TestUtils::getResourceUsageReport();
    
    QJsonObject usage;
    if (before.contains("memory_mb") && after.contains("memory_mb")) {
        usage["memory_delta_mb"] = after["memory_mb"].toDouble() - before["memory_mb"].toDouble();
    }
    
    return usage;
}

void TestPerformanceBenchmarks::stressTestSystem(int concurrency, int durationSeconds) {
    if (!TestUtils::isFFmpegAvailable()) {
        return;
    }
    
    TestUtils::logMessage(QString("Starting stress test: %1 concurrent operations for %2 seconds")
                         .arg(concurrency).arg(durationSeconds));
    
    QElapsedTimer stressTimer;
    stressTimer.start();
    
    QList<QFuture<void>> stressFutures;
    
    for (int i = 0; i < concurrency; ++i) {
        auto future = QtConcurrent::run([this, durationSeconds, i]() {
            QElapsedTimer operationTimer;
            operationTimer.start();
            
            int operationCount = 0;
            while (operationTimer.elapsed() < durationSeconds * 1000) {
                QString outputPath = QString("%1/stress_%2_%3.mp4")
                                   .arg(tempDir_->path()).arg(i).arg(operationCount);
                
                ConversionSettings settings;
                settings.outputFormat = "mp4";
                // Remove preset as it's not in ConversionSettings struct
                settings.maxWidth = 320;
                settings.maxHeight = 240;
                
                auto conversionFuture = mediaPipeline_->convertVideo(realVideoFile_, outputPath, settings);
                auto result = TestUtils::waitForFuture(conversionFuture, 30000);
                
                if (result.hasValue()) {
                    QFile::remove(outputPath);
                }
                
                operationCount++;
            }
            
            TestUtils::logMessage(QString("Stress worker %1 completed %2 operations")
                                 .arg(i).arg(operationCount));
        });
        stressFutures.append(future);
    }
    
    // Wait for all stress operations to complete
    for (auto& future : stressFutures) {
        future.waitForFinished();
    }
    
    qint64 actualDuration = stressTimer.elapsed();
    TestUtils::logMessage(QString("Stress test completed in %1ms").arg(actualDuration));
}

int runTestPerformanceBenchmarks(int argc, char** argv) {
    TestPerformanceBenchmarks test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_performance_benchmarks.moc"