#include "WhisperWrapper.hpp"
#include "../common/Logger.hpp"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryFile>
#include <QElapsedTimer>
#include <QtEndian>
#include <QDebug>

#include <whisper.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <numeric>

namespace Murmur {

struct WhisperWrapper::WhisperWrapperPrivate {
    whisper_context* ctx = nullptr;
    bool isInitialized = false;
    QString loadedModelPath;
    
    // Progress callback data
    ProgressCallback progressCallback = nullptr;
    int lastProgress = -1;
    
    // Model info
    QString modelInfo;
    std::vector<QString> supportedLanguages;
    
    // Audio processing buffers
    std::vector<float> audioBuffer;
    
    // Performance tracking
    mutable size_t memoryUsage = 0;
};

WhisperWrapper::WhisperWrapper() 
    : d(std::make_unique<WhisperWrapperPrivate>()) {
    Logger::instance().info("Creating WhisperWrapper instance");
}

WhisperWrapper::~WhisperWrapper() {
    unloadModel();
    Logger::instance().info("WhisperWrapper destroyed");
}

Expected<bool, WhisperError> WhisperWrapper::initialize() {
    if (d->isInitialized) {
        return true;
    }

    // Initialize whisper.cpp backend
    whisper_log_set([](enum ggml_log_level level, const char* text, void* user_data) {
        Q_UNUSED(user_data)
        QString message = QString::fromUtf8(text).trimmed();
        
        switch (level) {
            case GGML_LOG_LEVEL_ERROR:
                Logger::instance().error("{}", message.toStdString());
                break;
            case GGML_LOG_LEVEL_WARN:
                Logger::instance().warn("{}", message.toStdString());
                break;
            case GGML_LOG_LEVEL_INFO:
                Logger::instance().info("{}", message.toStdString());
                break;
            default:
                Logger::instance().debug("{}", message.toStdString());
                break;
        }
    }, nullptr);

    d->isInitialized = true;
    Logger::instance().info("whisper.cpp initialized successfully");
    return true;
}

Expected<bool, WhisperError> WhisperWrapper::loadModel(const QString& modelPath) {
    if (!d->isInitialized) {
        auto initResult = initialize();
        if (initResult.hasError()) {
            return initResult;
        }
    }

    // Unload existing model
    unloadModel();

    QFileInfo modelFile(modelPath);
    if (!modelFile.exists() || !modelFile.isFile()) {
        Logger::instance().error("Model file not found: {}", modelPath.toStdString());
        return makeUnexpected(WhisperError::ModelLoadFailed);
    }

    if (modelFile.size() < 1024 * 1024) { // Models should be at least 1MB
        Logger::instance().error("Model file too small: {}", modelPath.toStdString());
        return makeUnexpected(WhisperError::InvalidModel);
    }

    // Load model using whisper.cpp
    Logger::instance().info("Loading model: {}", modelPath.toStdString());
    
    // Convert QString to std::string for whisper.cpp
    std::string modelPathStd = modelPath.toStdString();
    
    // Create whisper context parameters
    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = true; // Enable GPU if available
    
    d->ctx = whisper_init_from_file_with_params(modelPathStd.c_str(), cparams);
    
    if (!d->ctx) {
        Logger::instance().error("Failed to load model: {}", modelPath.toStdString());
        return makeUnexpected(WhisperError::ModelLoadFailed);
    }

    d->loadedModelPath = modelPath;
    
    // Extract model information
    d->modelInfo = QString("Model: %1, Vocab size: %2")
                   .arg(modelFile.baseName())
                   .arg(whisper_n_vocab(d->ctx));
    
    // Get supported languages
    d->supportedLanguages.clear();
    int n_lang = whisper_lang_max_id();
    for (int i = 0; i < n_lang; ++i) {
        const char* lang = whisper_lang_str(i);
        if (lang && strlen(lang) > 0) {
            d->supportedLanguages.emplace_back(QString::fromUtf8(lang));
        }
    }

    // Update memory usage
    d->memoryUsage = modelFile.size(); // Approximate
    
    Logger::instance().info("Model loaded successfully: {} (vocab: {}, languages: {})", 
                           modelFile.baseName().toStdString(),
                           whisper_n_vocab(d->ctx),
                           d->supportedLanguages.size());
    
    return true;
}

bool WhisperWrapper::isModelLoaded() const {
    return d->ctx != nullptr;
}

void WhisperWrapper::unloadModel() {
    if (d->ctx) {
        whisper_free(d->ctx);
        d->ctx = nullptr;
        d->loadedModelPath.clear();
        d->modelInfo.clear();
        d->supportedLanguages.clear();
        d->memoryUsage = 0;
        Logger::instance().info("Model unloaded");
    }
}

Expected<WhisperResult, WhisperError> WhisperWrapper::transcribe(
    const std::vector<float>& audioData,
    const WhisperConfig& config,
    ProgressCallback progressCallback) {
    
    if (!isModelLoaded()) {
        Logger::instance().error("No model loaded for transcription");
        return makeUnexpected(WhisperError::ModelLoadFailed);
    }

    if (audioData.empty()) {
        Logger::instance().error("Empty audio data provided");
        return makeUnexpected(WhisperError::InvalidInput);
    }

    auto validateResult = validateAudioData(audioData);
    if (validateResult.hasError()) {
        return makeUnexpected(validateResult.error());
    }

    QElapsedTimer timer;
    timer.start();

    // Set up progress callback
    d->progressCallback = progressCallback;
    d->lastProgress = -1;

    // Initialize whisper parameters
    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
    auto paramResult = initializeWhisperParams(params, config);
    if (paramResult.hasError()) {
        return makeUnexpected(paramResult.error());
    }

    // Set progress callback if provided
    if (progressCallback) {
        params.progress_callback = progressCallbackWrapper;
        params.progress_callback_user_data = this;
    }

    // Run transcription
    Logger::instance().info("Starting transcription of {} samples", audioData.size());
    
    int result = whisper_full(d->ctx, params, audioData.data(), static_cast<int>(audioData.size()));
    
    if (result != 0) {
        QString errorMsg = translateWhisperError(result);
        Logger::instance().error("Transcription failed: {}", errorMsg.toStdString());
        return makeUnexpected(WhisperError::InferenceFailed);
    }

    // Extract results
    WhisperResult whisperResult = extractResult(config);
    whisperResult.processingTime = timer.elapsed() / 1000.0; // Convert to seconds
    
    Logger::instance().info("Transcription completed in {:.2f}s, {} segments",
                           whisperResult.processingTime,
                           whisperResult.segments.size());

    return whisperResult;
}

Expected<WhisperResult, WhisperError> WhisperWrapper::transcribeFile(
    const QString& audioFilePath,
    const WhisperConfig& config,
    ProgressCallback progressCallback) {
    
    QFileInfo fileInfo(audioFilePath);
    if (!fileInfo.exists()) {
        Logger::instance().error("Audio file not found: {}", audioFilePath.toStdString());
        return makeUnexpected(WhisperError::InvalidInput);
    }

    // Load audio file
    auto audioResult = loadAudioFile(audioFilePath);
    if (audioResult.hasError()) {
        return makeUnexpected(audioResult.error());
    }

    return transcribe(audioResult.value(), config, progressCallback);
}

Expected<std::vector<float>, WhisperError> WhisperWrapper::loadAudioFile(const QString& audioFilePath) {
    QFileInfo fileInfo(audioFilePath);
    QString suffix = fileInfo.suffix().toLower();
    
    // If it's not a WAV file, convert it first
    if (suffix != "wav") {
        QTemporaryFile tempFile;
        tempFile.setFileTemplate("whisper_XXXXXX.wav");
        if (!tempFile.open()) {
            Logger::instance().error("Failed to create temporary file");
            return makeUnexpected(WhisperError::AudioProcessingFailed);
        }
        
        QString tempPath = tempFile.fileName();
        tempFile.close();
        
        auto convertResult = convertAudioFormat(audioFilePath, tempPath);
        if (convertResult.hasError()) {
            return makeUnexpected(convertResult.error());
        }
        
        auto loadResult = loadWavFile(tempPath);
        QFile::remove(tempPath); // Clean up temp file
        return loadResult;
    }
    
    return loadWavFile(audioFilePath);
}

Expected<std::vector<float>, WhisperError> WhisperWrapper::loadWavFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        Logger::instance().error("Cannot open WAV file: {}", filePath.toStdString());
        return makeUnexpected(WhisperError::AudioProcessingFailed);
    }

    // Read WAV header
    char header[44];
    if (file.read(header, 44) != 44) {
        Logger::instance().error("Invalid WAV file header: {}", filePath.toStdString());
        return makeUnexpected(WhisperError::AudioProcessingFailed);
    }

    // Verify WAV format
    if (strncmp(header, "RIFF", 4) != 0 || strncmp(header + 8, "WAVE", 4) != 0) {
        Logger::instance().error("Not a valid WAV file: {}", filePath.toStdString());
        return makeUnexpected(WhisperError::AudioProcessingFailed);
    }

    // Extract audio format information
    quint16 audioFormat = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(header + 20));
    quint16 numChannels = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(header + 22));
    quint32 sampleRate = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(header + 24));
    quint16 bitsPerSample = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(header + 34));
    quint32 dataSize = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(header + 40));

    // Validate format (we expect 16-bit PCM, 16kHz, mono)
    if (audioFormat != 1) { // PCM
        Logger::instance().error("Unsupported audio format (not PCM): {}", filePath.toStdString());
        return makeUnexpected(WhisperError::AudioProcessingFailed);
    }

    Logger::instance().info("Loading WAV: {}Hz, {} channels, {}-bit, {} bytes",
                           sampleRate, numChannels, bitsPerSample, dataSize);

    // Read audio data
    QByteArray audioBytes = file.readAll();
    if (audioBytes.size() != static_cast<int>(dataSize)) {
        Logger::instance().warn("Audio data size mismatch");
    }

    // Convert to float array
    std::vector<float> audioData;
    
    if (bitsPerSample == 16) {
        // 16-bit signed PCM
        const qint16* samples = reinterpret_cast<const qint16*>(audioBytes.constData());
        int numSamples = audioBytes.size() / sizeof(qint16);
        
        if (numChannels == 1) {
            // Mono - direct conversion
            audioData.reserve(numSamples);
            for (int i = 0; i < numSamples; ++i) {
                audioData.push_back(static_cast<float>(qFromLittleEndian(samples[i])) / 32768.0f);
            }
        } else if (numChannels == 2) {
            // Stereo - convert to mono by averaging channels
            audioData.reserve(numSamples / 2);
            for (int i = 0; i < numSamples; i += 2) {
                float left = static_cast<float>(qFromLittleEndian(samples[i])) / 32768.0f;
                float right = static_cast<float>(qFromLittleEndian(samples[i + 1])) / 32768.0f;
                audioData.push_back((left + right) / 2.0f);
            }
        } else {
            Logger::instance().error("Unsupported channel count: {}", numChannels);
            return makeUnexpected(WhisperError::AudioProcessingFailed);
        }
    } else {
        Logger::instance().error("Unsupported bit depth: {}", bitsPerSample);
        return makeUnexpected(WhisperError::AudioProcessingFailed);
    }

    // Resample if necessary (whisper expects 16kHz)
    if (sampleRate != 16000) {
        Logger::instance().info("Resampling from {}Hz to 16kHz", sampleRate);
        
        // Simple linear resampling 
        // Note: For production use, consider using a dedicated resampling library like libsamplerate
        // for better quality, but linear interpolation is sufficient for whisper.cpp
        double ratio = 16000.0 / sampleRate;
        std::vector<float> resampled;
        resampled.reserve(static_cast<size_t>(audioData.size() * ratio));
        
        for (size_t i = 0; i < static_cast<size_t>(audioData.size() * ratio); ++i) {
            double srcIndex = i / ratio;
            size_t index = static_cast<size_t>(srcIndex);
            
            if (index < audioData.size() - 1) {
                double frac = srcIndex - index;
                float sample = audioData[index] * (1.0f - frac) + audioData[index + 1] * frac;
                resampled.push_back(sample);
            } else if (index < audioData.size()) {
                resampled.push_back(audioData[index]);
            }
        }
        
        audioData = std::move(resampled);
    }

    Logger::instance().info("Loaded {} audio samples", audioData.size());
    return audioData;
}

Expected<bool, WhisperError> WhisperWrapper::convertAudioFormat(
    const QString& inputPath,
    const QString& outputPath) {
    
    // Use FFmpeg to convert to 16kHz, 16-bit, mono WAV
    QStringList arguments;
    arguments << "-i" << inputPath
              << "-ar" << "16000"    // 16kHz sample rate
              << "-ac" << "1"        // Mono
              << "-c:a" << "pcm_s16le" // 16-bit PCM
              << "-y" << outputPath; // Overwrite output

    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", arguments);
    
    if (!ffmpeg.waitForStarted()) {
        Logger::instance().error("Failed to start FFmpeg for audio conversion");
        return makeUnexpected(WhisperError::AudioProcessingFailed);
    }

    if (!ffmpeg.waitForFinished(60000)) { // 60 second timeout
        ffmpeg.kill();
        Logger::instance().error("FFmpeg conversion timeout");
        return makeUnexpected(WhisperError::AudioProcessingFailed);
    }

    if (ffmpeg.exitCode() != 0) {
        QString error = QString::fromUtf8(ffmpeg.readAllStandardError());
        Logger::instance().error("FFmpeg conversion failed: {}", error.toStdString());
        return makeUnexpected(WhisperError::AudioProcessingFailed);
    }

    Logger::instance().info("Audio format conversion completed: {}", outputPath.toStdString());
    return true;
}

Expected<bool, WhisperError> WhisperWrapper::initializeWhisperParams(
    whisper_full_params& params,
    const WhisperConfig& config) {
    
    // Language
    if (config.language != "auto") {
        std::string langStr = config.language.toStdString();
        params.language = langStr.c_str();
    } else {
        params.language = "auto";
    }

    // Threading
    params.n_threads = std::max(1, config.nThreads);

    // Temperature
    params.temperature = config.temperature;

    // Beam search
    params.beam_search.beam_size = std::max(1, config.beamSize);

    // Timestamps
    params.print_timestamps = config.enableTimestamps;
    params.print_progress = config.printProgress;
    params.print_realtime = config.printRealtime;
    params.print_special = config.printSpecial;

    // Audio context
    if (config.audioCtx > 0.0f) {
        params.audio_ctx = static_cast<int>(config.audioCtx);
    }

    // Text context
    params.n_max_text_ctx = config.nMaxTextCtx;

    // Translation
    params.translate = config.enableTranslation;

    // Single segment
    params.single_segment = config.singleSegment;

    // No context
    params.no_context = config.noContext;

    // Split on word
    params.split_on_word = config.splitOnWord;

    // Token timestamps
    params.token_timestamps = config.enableTokenTimestamps;
    params.max_len = 1; // 1 word per segment for word timestamps

    return true;
}

WhisperResult WhisperWrapper::extractResult(const WhisperConfig& config) const {
    WhisperResult result;
    
    if (!d->ctx) {
        return result;
    }

    const int n_segments = whisper_full_n_segments(d->ctx);
    result.segments.reserve(n_segments);

    QString fullText;
    float totalConfidence = 0.0f;
    int segmentCount = 0;

    for (int i = 0; i < n_segments; ++i) {
        WhisperSegment segment;
        
        // Get segment timing
        segment.startTime = whisper_full_get_segment_t0(d->ctx, i) / 100.0; // Convert centiseconds to seconds
        segment.endTime = whisper_full_get_segment_t1(d->ctx, i) / 100.0;   // Convert centiseconds to seconds
        
        // Get segment text
        const char* text = whisper_full_get_segment_text(d->ctx, i);
        if (text) {
            segment.text = QString::fromUtf8(text).trimmed();
            fullText += segment.text + " ";
        }

        const int n_tokens = whisper_full_n_tokens(d->ctx, i);
        double segment_prob_sum = 0.0;
        
        if (n_tokens > 0) {
            for (int j = 0; j < n_tokens; ++j) {
                whisper_token_data token_data = whisper_full_get_token_data(d->ctx, i, j);
                segment_prob_sum += token_data.p;
                if (config.enableTokenTimestamps || config.enableWordTimestamps) {
                    const char* word = whisper_token_to_str(d->ctx, token_data.id);
                    if (word) {
                        segment.words.emplace_back(QString::fromUtf8(word), token_data.p);
                    }
                }
            }
            segment.confidence = segment_prob_sum / n_tokens;
        } else {
            segment.confidence = 0.0f;
        }

        result.segments.push_back(segment);
        totalConfidence += segment.confidence;
        segmentCount++;
    }

    result.fullText = fullText.trimmed();
    result.avgConfidence = segmentCount > 0 ? totalConfidence / segmentCount : 0.0f;
    
    // Detect language
    int lang_id = whisper_full_lang_id(d->ctx);
    if(lang_id >= 0) {
        const char* lang = whisper_lang_str(lang_id);
        result.language = lang ? QString::fromUtf8(lang) : "unknown";
    } else {
        result.language = "unknown";
    }

    return result;
}

void WhisperWrapper::progressCallbackWrapper(
    struct whisper_context* ctx,
    struct whisper_state* state,
    int progress,
    void* user_data) {
    
    Q_UNUSED(ctx)
    Q_UNUSED(state)
    
    auto* wrapper = static_cast<WhisperWrapper*>(user_data);
    if (wrapper && wrapper->d->progressCallback && progress != wrapper->d->lastProgress) {
        wrapper->d->progressCallback(progress);
        wrapper->d->lastProgress = progress;
    }
}

Expected<bool, WhisperError> WhisperWrapper::validateAudioData(const std::vector<float>& audioData) {
    if (audioData.empty()) {
        return makeUnexpected(WhisperError::InvalidInput);
    }

    // Check for reasonable audio length (at least 0.1 seconds)
    if (audioData.size() < 1600) { // 16kHz * 0.1s
        Logger::instance().warn("Audio data is very short");
    }

    // Check for audio clipping or other issues
    bool hasValidSamples = false;
    for (float sample : audioData) {
        if (std::abs(sample) > 1.0f) {
            Logger::instance().warn("Audio samples out of range [-1.0, 1.0]");
            break;
        }
        if (std::abs(sample) > 0.001f) {
            hasValidSamples = true;
        }
    }

    if (!hasValidSamples) {
        Logger::instance().warn("Audio appears to be silent or very quiet");
    }

    return true;
}

QString WhisperWrapper::translateWhisperError(int errorCode) const {
    switch (errorCode) {
        case 0: return "Success";
        case -1: return "Failed to load model";
        case -2: return "Failed to encode audio";
        case -3: return "Failed to decode audio";
        default: return QString("Unknown error code: %1").arg(errorCode);
    }
}

// Remaining interface methods

Expected<QString, WhisperError> WhisperWrapper::detectLanguage(
    const std::vector<float>& audioData,
    int nThreads) {
    
    if (!isModelLoaded()) {
        return makeUnexpected(WhisperError::ModelLoadFailed);
    }

    if (audioData.empty()) {
        return makeUnexpected(WhisperError::InvalidInput);
    }

    // Use a subset of audio for language detection (first 30 seconds)
    const size_t maxSamples = 16000 * 30; // 30 seconds at 16kHz
    std::vector<float> detectAudio(audioData.begin(), 
        audioData.begin() + std::min(audioData.size(), maxSamples));

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.n_threads = nThreads;
    params.language = "auto";
    params.single_segment = true;
    params.print_progress = false;
    params.print_timestamps = false;
    params.token_timestamps = false;
    params.max_len = 0;

    int result = whisper_full(d->ctx, params, detectAudio.data(), static_cast<int>(detectAudio.size()));
    
    if (result != 0) {
        return makeUnexpected(WhisperError::InferenceFailed);
    }

    int lang_id = whisper_full_lang_id(d->ctx);
    const char* lang = whisper_lang_str(lang_id);
    
    return lang ? QString::fromUtf8(lang) : QString("unknown");
}

QString WhisperWrapper::getModelInfo() const {
    return d->modelInfo;
}

std::vector<QString> WhisperWrapper::getSupportedLanguages() const {
    return d->supportedLanguages;
}

size_t WhisperWrapper::getMemoryUsage() const {
    return d->memoryUsage;
}

bool WhisperWrapper::supportsTranslation() const {
    // Check if model supports English translation
    return isModelLoaded() && d->supportedLanguages.size() > 1;
}

QString WhisperWrapper::getLibraryVersion() {
    return QString("whisper.cpp version: %1").arg(whisper_print_system_info());
}

bool WhisperWrapper::hasGpuSupport() {
#if defined(WHISPER_USE_CUDA) || defined(WHISPER_USE_METAL) || defined(WHISPER_USE_OPENVINO)
    return true;
#else
    return false;
#endif
}

} // namespace Murmur