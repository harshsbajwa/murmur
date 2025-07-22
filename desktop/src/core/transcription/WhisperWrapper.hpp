#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <QtCore/QString>
#include <QtCore/QByteArray>
#include "../common/Expected.hpp"

// Forward declare whisper.cpp types to avoid including the header here
struct whisper_context;
struct whisper_full_params;
struct whisper_state;

namespace Murmur {

enum class WhisperError {
    InitializationFailed,
    ModelLoadFailed,
    AudioProcessingFailed,
    InferenceFailed,
    InvalidInput,
    OutOfMemory,
    InvalidModel,
    UnsupportedFeature
};

struct WhisperSegment {
    double startTime = 0.0;  // in seconds
    double endTime = 0.0;    // in seconds
    QString text;
    float confidence = 0.0f;
    std::vector<std::pair<QString, float>> words; // word, confidence pairs
};

struct WhisperResult {
    QString language;
    QString fullText;
    std::vector<WhisperSegment> segments;
    float avgConfidence = 0.0f;
    double processingTime = 0.0; // in seconds
};

struct WhisperConfig {
    QString language = "auto";
    bool enableTimestamps = true;
    bool enableWordTimestamps = false;
    int nThreads = 4;
    float temperature = 0.0f;
    int nMaxTextCtx = 16384;
    bool enableTranslation = false;
    bool enableTokenTimestamps = false;
    int beamSize = 1;
    float audioCtx = 0.0f;
    bool enableProgressCallback = true;
    bool enableDtwCallback = false;
    bool splitOnWord = false;
    bool noContext = false;
    bool singleSegment = false;
    bool printSpecial = false;
    bool printProgress = true;
    bool printRealtime = false;
    bool printTimestamps = true;
};

// Progress callback function type
using ProgressCallback = std::function<void(int progress_percentage)>;

/**
 * @brief Direct wrapper around whisper.cpp C library
 * 
 * This class provides a C++ interface to the whisper.cpp library,
 * handling model loading, audio processing, and transcription.
 */
class WhisperWrapper {
public:
    WhisperWrapper();
    ~WhisperWrapper();

    // Non-copyable, non-movable
    WhisperWrapper(const WhisperWrapper&) = delete;
    WhisperWrapper& operator=(const WhisperWrapper&) = delete;
    WhisperWrapper(WhisperWrapper&&) = delete;
    WhisperWrapper& operator=(WhisperWrapper&&) = delete;

    /**
     * @brief Initialize whisper.cpp library
     * @return true if successful, false otherwise
     */
    Expected<bool, WhisperError> initialize();

    /**
     * @brief Load a whisper model from file
     * @param modelPath Path to the .bin model file
     * @return true if successful, false otherwise
     */
    Expected<bool, WhisperError> loadModel(const QString& modelPath);

    /**
     * @brief Check if a model is currently loaded
     * @return true if model is loaded and ready
     */
    bool isModelLoaded() const;

    /**
     * @brief Unload the current model and free memory
     */
    void unloadModel();

    /**
     * @brief Transcribe audio data
     * @param audioData Raw PCM audio data (16kHz, 16-bit, mono)
     * @param config Transcription configuration
     * @param progressCallback Optional progress callback
     * @return Transcription result or error
     */
    Expected<WhisperResult, WhisperError> transcribe(
        const std::vector<float>& audioData,
        const WhisperConfig& config = WhisperConfig{},
        ProgressCallback progressCallback = nullptr
    );

    /**
     * @brief Transcribe audio from file
     * @param audioFilePath Path to audio file (will be converted to required format)
     * @param config Transcription configuration
     * @param progressCallback Optional progress callback
     * @return Transcription result or error
     */
    Expected<WhisperResult, WhisperError> transcribeFile(
        const QString& audioFilePath,
        const WhisperConfig& config = WhisperConfig{},
        ProgressCallback progressCallback = nullptr
    );

    /**
     * @brief Convert audio file to required format (16kHz, 16-bit, mono PCM)
     * @param inputPath Input audio file path
     * @param outputPath Output WAV file path
     * @return true if successful, false otherwise
     */
    Expected<bool, WhisperError> convertAudioFormat(
        const QString& inputPath,
        const QString& outputPath
    );

    /**
     * @brief Load audio file and convert to float array
     * @param audioFilePath Path to audio file
     * @return Audio data as float array or error
     */
    Expected<std::vector<float>, WhisperError> loadAudioFile(const QString& audioFilePath);

    /**
     * @brief Get model information
     * @return Model info string or empty if no model loaded
     */
    QString getModelInfo() const;

    /**
     * @brief Get supported languages for current model
     * @return List of language codes
     */
    std::vector<QString> getSupportedLanguages() const;

    /**
     * @brief Detect language from audio data
     * @param audioData Raw PCM audio data
     * @param nThreads Number of threads to use
     * @return Detected language code or error
     */
    Expected<QString, WhisperError> detectLanguage(
        const std::vector<float>& audioData,
        int nThreads = 4
    );

    /**
     * @brief Get memory usage statistics
     * @return Memory usage in bytes
     */
    size_t getMemoryUsage() const;

    /**
     * @brief Check if model supports translation to English
     * @return true if translation is supported
     */
    bool supportsTranslation() const;

    /**
     * @brief Get whisper.cpp library version
     * @return Version string
     */
    static QString getLibraryVersion();

    /**
     * @brief Check if whisper.cpp was compiled with GPU support
     * @return true if GPU support is available
     */
    static bool hasGpuSupport();

private:
    struct WhisperWrapperPrivate;
    std::unique_ptr<WhisperWrapperPrivate> d;

    // Internal helper methods
    Expected<bool, WhisperError> initializeWhisperParams(
        whisper_full_params& params,
        const WhisperConfig& config
    );
    
    WhisperResult extractResult(const WhisperConfig& config) const;
    
    static void progressCallbackWrapper(
        struct whisper_context* ctx,
        struct whisper_state* state,
        int progress,
        void* user_data
    );

    Expected<std::vector<float>, WhisperError> loadWavFile(const QString& filePath);
    Expected<bool, WhisperError> validateAudioData(const std::vector<float>& audioData);
    
    QString translateWhisperError(int errorCode) const;
};

} // namespace Murmur