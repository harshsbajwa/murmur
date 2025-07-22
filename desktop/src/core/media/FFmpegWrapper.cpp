#include "FFmpegWrapper.hpp"
#include "../common/Logger.hpp"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QThread>
#include <QtCore/QUuid>
#include <QElapsedTimer>

// FFmpeg C API includes
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/pixfmt.h>
#include <libavutil/timestamp.h>
#include <libavutil/hwcontext.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace Murmur {

struct OperationContext {
    QString id;
    QString inputPath;
    QString outputPath;
    ConversionOptions options;
    QElapsedTimer timer;
    std::atomic<bool> cancelled{false};
    
    // FFmpeg contexts
    AVFormatContext* inputFormat = nullptr;
    AVFormatContext* outputFormat = nullptr;
    AVCodecContext* videoDecoder = nullptr;
    AVCodecContext* audioDecoder = nullptr;
    AVCodecContext* videoEncoder = nullptr;
    AVCodecContext* audioEncoder = nullptr;
    SwsContext* swsContext = nullptr;
    SwrContext* swrContext = nullptr;
    AVFilterGraph* filterGraph = nullptr;
    AVFilterContext* filterSrc = nullptr;
    AVFilterContext* filterSink = nullptr;
    
    // Stream information
    VideoStreamInfo videoInfo;
    AudioStreamInfo audioInfo;
    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    
    // Progress tracking
    qint64 totalFrames = 0;
    qint64 processedFrames = 0;
    
    // Audio frame buffering for fixed frame size encoders (like AAC)
    std::vector<uint8_t> audioBuffer;
    int audioBufferSampleCount = 0;
    int targetAudioFrameSize = 0;
    FFmpegProgressCallback progressCallback = nullptr;
};

struct FFmpegWrapper::FFmpegWrapperPrivate {
    mutable QMutex operationsMutex;
    QHash<QString, OperationContext*> activeOperations;
    
    // Hardware acceleration availability
    QList<HardwareAccel> availableHwAccel;
    bool hwaccelInitialized = false;
    
    // FFmpeg initialization state
    bool librariesInitialized = false;
    
    // Configuration
    QString tempDirectory;
    int maxConcurrentOperations = 4;
};

FFmpegWrapper::FFmpegWrapper(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<FFmpegWrapperPrivate>()) {
    
    d->tempDirectory = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/MurmurFFmpeg";
    QDir().mkpath(d->tempDirectory);
    
    Logger::instance().info("FFmpegWrapper initialized");
}

FFmpegWrapper::~FFmpegWrapper() {
    // Handles cancellation and waiting for operations to complete
    shutdownLibraries();
    Logger::instance().info("FFmpegWrapper destroyed");
}

Expected<bool, FFmpegError> FFmpegWrapper::initialize() {
    if (d->librariesInitialized) {
        return true;
    }
    
    auto initResult = initializeLibraries();
    if (initResult.hasError()) {
        return initResult;
    }
    
    auto hwResult = detectHardwareAcceleration();
    if (hwResult.hasError()) {
        Logger::instance().warn("Hardware acceleration detection failed, continuing with software only");
    }
    
    d->librariesInitialized = true;
    Logger::instance().info("FFmpeg libraries initialized successfully");
    return true;
}

QFuture<Expected<MediaFileInfo, FFmpegError>> FFmpegWrapper::analyzeFile(const QString& filePath) {
    return QtConcurrent::run([this, filePath]() -> Expected<MediaFileInfo, FFmpegError> {
        try {
            auto validateResult = validateFilePath(filePath, true);
            if (validateResult.hasError()) {
                return makeUnexpected(validateResult.error());
            }
            
            AVFormatContext* formatContext = nullptr;
            auto openResult = openInputFile(filePath);
            if (openResult.hasError()) {
                return makeUnexpected(openResult.error());
            }
            formatContext = openResult.value();
            
            MediaFileInfo info;
            info.filePath = filePath;
            info.format = QString::fromUtf8(formatContext->iformat->name);
            info.duration = static_cast<double>(formatContext->duration) / AV_TIME_BASE;
            info.bitrate = formatContext->bit_rate;
            info.fileSize = QFileInfo(filePath).size();
            
            // Find video and audio streams
            for (unsigned int i = 0; i < formatContext->nb_streams; ++i) {
                AVStream* stream = formatContext->streams[i];
                AVCodecParameters* codecParams = stream->codecpar;
                
                if (codecParams->codec_type == AVMEDIA_TYPE_VIDEO && info.video.streamIndex == -1) {
                    info.video.streamIndex = i;
                    info.video.codec = QString::fromUtf8(avcodec_get_name(codecParams->codec_id));
                    info.video.width = codecParams->width;
                    info.video.height = codecParams->height;
                    info.video.bitrate = codecParams->bit_rate;
                    info.video.pixelFormat = QString::fromUtf8(av_get_pix_fmt_name(static_cast<AVPixelFormat>(codecParams->format)));
                    
                    // Calculate frame rate
                    if (stream->r_frame_rate.den != 0) {
                        info.video.frameRate = static_cast<double>(stream->r_frame_rate.num) / stream->r_frame_rate.den;
                    }
                    
                    // Calculate frame count
                    if (stream->nb_frames > 0) {
                        info.video.frameCount = stream->nb_frames;
                    } else if (info.video.frameRate > 0 && info.duration > 0) {
                        info.video.frameCount = static_cast<qint64>(info.duration * info.video.frameRate);
                    }
                    
                    info.video.duration = info.duration;
                }
                else if (codecParams->codec_type == AVMEDIA_TYPE_AUDIO && info.audio.streamIndex == -1) {
                    info.audio.streamIndex = i;
                    info.audio.codec = QString::fromUtf8(avcodec_get_name(codecParams->codec_id));
                    info.audio.sampleRate = codecParams->sample_rate;
                    info.audio.channels = codecParams->ch_layout.nb_channels;
                    info.audio.bitrate = codecParams->bit_rate;
                    info.audio.duration = info.duration;
                    info.audio.sampleFormat = QString::fromUtf8(av_get_sample_fmt_name(static_cast<AVSampleFormat>(codecParams->format)));
                    
                    char layout[256];
                    av_channel_layout_describe(&codecParams->ch_layout, layout, sizeof(layout));
                    info.audio.channelLayout = QString::fromUtf8(layout);
                    
                    info.video.hasAudioStream = true;
                }
            }
            
            // Extract metadata
            AVDictionaryEntry* entry = nullptr;
            while ((entry = av_dict_get(formatContext->metadata, "", entry, AV_DICT_IGNORE_SUFFIX))) {
                info.metadata << QString("%1=%2").arg(QString::fromUtf8(entry->key)).arg(QString::fromUtf8(entry->value));
            }
            
            info.isValid = (info.video.streamIndex != -1 || info.audio.streamIndex != -1);
            
            closeFormatContext(formatContext);
            
            Logger::instance().info("Analyzed file: {} ({}x{}, {:.2f}s)", 
                                          filePath.toStdString(), 
                                          info.video.width, 
                                          info.video.height, 
                                          info.duration);
            
            return info;
        } catch (const std::exception& e) {
            Logger::instance().error("Exception during file analysis: {}", e.what());
            return makeUnexpected(FFmpegError::InvalidFile);
        } catch (...) {
            Logger::instance().error("Unknown exception during file analysis");
            return makeUnexpected(FFmpegError::InvalidFile);
        }
    });
}

QFuture<Expected<QString, FFmpegError>> FFmpegWrapper::convertVideo(
    const QString& inputPath,
    const QString& outputPath,
    const ConversionOptions& options,
    FFmpegProgressCallback progressCallback) {
    
    return QtConcurrent::run([this, inputPath, outputPath, options, progressCallback]() -> Expected<QString, FFmpegError> {
        try {
            // Validate inputs
            auto validateResult = validateFilePath(inputPath, true);
            if (validateResult.hasError()) {
                return makeUnexpected(validateResult.error());
            }
            
            // Validate output path directory exists
            QFileInfo outputInfo(outputPath);
            QDir outputDir = outputInfo.absoluteDir();
            if (!outputDir.exists()) {
                Logger::instance().error("Output directory does not exist: {}", outputDir.absolutePath().toStdString());
                return makeUnexpected(FFmpegError::IOError);
            }
            
            auto optionsValidation = validateConversionOptions(options);
            if (optionsValidation.hasError()) {
                return makeUnexpected(optionsValidation.error());
            }
            
            // Check concurrent operations limit
            {
                QMutexLocker locker(&d->operationsMutex);
                if (d->activeOperations.size() >= d->maxConcurrentOperations) {
                    return makeUnexpected(FFmpegError::AllocationFailed);
                }
            }
            
            // Create operation context
            auto context = std::make_unique<OperationContext>();
            context->id = generateOperationId();
            context->inputPath = inputPath;
            context->outputPath = outputPath;
            context->options = options;
            context->progressCallback = progressCallback;
            context->timer.start();
            
            QString operationId = context->id;
            
            // Store operation context
            {
                QMutexLocker locker(&d->operationsMutex);
                d->activeOperations.insert(operationId, context.release());
            }
            
            emit operationStarted(operationId, inputPath);
            
            // Perform conversion
            auto result = performVideoConversion(operationId);

            bool wasCancelled = false;
            {
                QMutexLocker locker(&d->operationsMutex);
                auto it = d->activeOperations.find(operationId);
                if (it != d->activeOperations.end()) {
                    wasCancelled = it.value()->cancelled;
                }
            }        
            
            // Cleanup and emit results
            {
                QMutexLocker locker(&d->operationsMutex);
                cleanupOperation(operationId);
            }
            
            // If the operation was marked for cancellation, honor it even if it completed
            if (wasCancelled) {
                if (result.hasValue()) { QFile::remove(result.value()); }
                emit operationFailed(operationId, FFmpegError::CancellationRequested, "Operation was cancelled");
                return makeUnexpected(FFmpegError::CancellationRequested);
            } else if (result.hasError()) {
                emit operationFailed(operationId, result.error(), translateFFmpegError(result.error()));
                return result;
            }
            
            emit operationCompleted(operationId, outputPath);
            
            return outputPath;
        } catch (const std::exception& e) {
            Logger::instance().error("Exception during video conversion: {}", e.what());
            return makeUnexpected(FFmpegError::EncodingFailed);
        } catch (...) {
            Logger::instance().error("Unknown exception during video conversion");
            return makeUnexpected(FFmpegError::EncodingFailed);
        }
    });
}

QFuture<Expected<QString, FFmpegError>> FFmpegWrapper::extractAudio(
    const QString& inputPath,
    const QString& outputPath,
    const ConversionOptions& options) {
    
    return QtConcurrent::run([this, inputPath, outputPath, options]() -> Expected<QString, FFmpegError> {
        // Create modified options for audio-only extraction
        ConversionOptions audioOptions = options;
        audioOptions.videoCodec = ""; // No video encoding
        
        return performAudioExtraction(inputPath, outputPath, audioOptions);
    });
}

QFuture<Expected<QString, FFmpegError>> FFmpegWrapper::generateThumbnail(
    const QString& inputPath,
    const QString& outputPath,
    double timeSeconds,
    int width,
    int height) {
    
    return QtConcurrent::run([this, inputPath, outputPath, timeSeconds, width, height]() -> Expected<QString, FFmpegError> {
        auto validateResult = validateFilePath(inputPath, true);
        if (validateResult.hasError()) {
            return makeUnexpected(validateResult.error());
        }
        
        // Open input file
        auto openResult = openInputFile(inputPath);
        if (openResult.hasError()) {
            return makeUnexpected(openResult.error());
        }
        AVFormatContext* inputFormat = openResult.value();
        
        // Find video stream
        auto videoStreamResult = findBestVideoStream(inputFormat);
        if (videoStreamResult.hasError()) {
            closeFormatContext(inputFormat);
            return makeUnexpected(videoStreamResult.error());
        }
        int videoStreamIndex = videoStreamResult.value();
        
        AVStream* videoStream = inputFormat->streams[videoStreamIndex];
        
        // Create decoder
        auto decoderResult = createVideoDecoder(videoStream);
        if (decoderResult.hasError()) {
            closeFormatContext(inputFormat);
            return makeUnexpected(decoderResult.error());
        }
        AVCodecContext* decoder = decoderResult.value();
        
        // Seek to desired time
        int64_t seekTarget = static_cast<int64_t>(timeSeconds * videoStream->time_base.den / videoStream->time_base.num);
        if (av_seek_frame(inputFormat, videoStreamIndex, seekTarget, AVSEEK_FLAG_BACKWARD) < 0) {
            Logger::instance().warn("Could not seek to specified time, using first keyframe");
        }
        
        // Read and decode frames until we find the right one
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        
        bool foundFrame = false;
        while (av_read_frame(inputFormat, packet) >= 0) {
            if (packet->stream_index == videoStreamIndex) {
                if (avcodec_send_packet(decoder, packet) >= 0) {
                    if (avcodec_receive_frame(decoder, frame) >= 0) {
                        foundFrame = true;
                        break;
                    }
                }
            }
            av_packet_unref(packet);
        }
        
        Expected<QString, FFmpegError> result = makeUnexpected(FFmpegError::DecodingFailed);
        
        if (foundFrame) {
            QString format = QFileInfo(outputPath).suffix().toLower();
            if (format.isEmpty()) format = "jpg";

            AVFrame* outputFrame = av_frame_alloc();
            int outputWidth = (width > 0) ? width : frame->width;
            int outputHeight = (height > 0) ? height : frame->height;
            bool conversionSuccess = false;

            if (outputFrame) {
                outputFrame->width = outputWidth;
                outputFrame->height = outputHeight;
                
                // Use YUV format for JPEG output, RGB for PNG
                AVPixelFormat outputPixFmt = (format.toLower() == "png") ? AV_PIX_FMT_RGB24 : AV_PIX_FMT_YUVJ420P;
                outputFrame->format = outputPixFmt;

                if (av_frame_get_buffer(outputFrame, 32) >= 0) {
                    SwsContext* swsContext = sws_getContext(
                        frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                        outputWidth, outputHeight, outputPixFmt,
                        SWS_BILINEAR, nullptr, nullptr, nullptr
                    );
                    
                    if (swsContext) {
                        sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height,
                                  outputFrame->data, outputFrame->linesize);
                        sws_freeContext(swsContext);
                        conversionSuccess = true;
                    }
                }

                if (conversionSuccess) {
                    if (saveFrameAsImage(outputFrame, outputPath, format)) {
                        result = outputPath;
                    } else {
                        result = makeUnexpected(FFmpegError::EncodingFailed);
                    }
                } else {
                    result = makeUnexpected(FFmpegError::AllocationFailed);
                }
                av_frame_free(&outputFrame);
            } else {
                result = makeUnexpected(FFmpegError::AllocationFailed);
            }
        }
        
        // Cleanup
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&decoder);
        closeFormatContext(inputFormat);
        
        return result;
    });
}

QFuture<Expected<QStringList, FFmpegError>> FFmpegWrapper::extractFrames(
    const QString& inputPath,
    const QString& outputDir,
    double intervalSeconds,
    const QString& format) {
    
    return QtConcurrent::run([this, inputPath, outputDir, intervalSeconds, format]() -> Expected<QStringList, FFmpegError> {
        auto validateResult = validateFilePath(inputPath, true);
        if (validateResult.hasError()) {
            return makeUnexpected(validateResult.error());
        }
        
        // Ensure output directory exists
        QDir outDir(outputDir);
        if (!outDir.exists() && !outDir.mkpath(".")) {
            return makeUnexpected(FFmpegError::IOError);
        }
        
        // Open input file
        auto openResult = openInputFile(inputPath);
        if (openResult.hasError()) {
            return makeUnexpected(openResult.error());
        }
        AVFormatContext* inputFormat = openResult.value();
        
        // Find video stream
        auto videoStreamResult = findBestVideoStream(inputFormat);
        if (videoStreamResult.hasError()) {
            closeFormatContext(inputFormat);
            return makeUnexpected(videoStreamResult.error());
        }
        int videoStreamIndex = videoStreamResult.value();
        
        AVStream* videoStream = inputFormat->streams[videoStreamIndex];
        
        // Create decoder
        auto decoderResult = createVideoDecoder(videoStream);
        if (decoderResult.hasError()) {
            closeFormatContext(inputFormat);
            return makeUnexpected(decoderResult.error());
        }
        AVCodecContext* decoder = decoderResult.value();
        
        // Calculate duration and frame extraction points
        double duration = static_cast<double>(inputFormat->duration) / AV_TIME_BASE;
        if (duration <= 0) {
            duration = static_cast<double>(videoStream->duration) * av_q2d(videoStream->time_base);
        }
        
        QStringList extractedFrames;
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        
        // Extract frames at regular intervals
        double currentTime = 0.0;
        int frameNumber = 0;
        
        while (currentTime < duration) {
            // Seek to the desired time
            int64_t seekTarget = static_cast<int64_t>(currentTime * AV_TIME_BASE);
            if (av_seek_frame(inputFormat, -1, seekTarget, AVSEEK_FLAG_BACKWARD) < 0) {
                Logger::instance().warn("Could not seek to time {}", currentTime);
                currentTime += intervalSeconds;
                continue;
            }
            
            // Read frames until we find one at or after the target time
            bool foundFrame = false;
            while (av_read_frame(inputFormat, packet) >= 0) {
                if (packet->stream_index == videoStreamIndex) {
                    if (avcodec_send_packet(decoder, packet) >= 0) {
                        if (avcodec_receive_frame(decoder, frame) >= 0) {
                            foundFrame = true;
                            break;
                        }
                    }
                }
                av_packet_unref(packet);
            }
            
            if (foundFrame) {
                // Create output filename
                QString outputPath = QString("%1/frame_%2.%3")
                    .arg(outputDir)
                    .arg(frameNumber, 6, 10, QChar('0'))
                    .arg(format);
                
                // Convert frame to RGB24 for saving
                AVFrame* rgbFrame = av_frame_alloc();
                rgbFrame->width = frame->width;
                rgbFrame->height = frame->height;
                rgbFrame->format = AV_PIX_FMT_RGB24;
                
                if (av_frame_get_buffer(rgbFrame, 32) >= 0) {
                    SwsContext* swsContext = sws_getContext(
                        frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                        frame->width, frame->height, AV_PIX_FMT_RGB24,
                        SWS_BILINEAR, nullptr, nullptr, nullptr
                    );
                    
                    if (swsContext) {
                        sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height,
                                 rgbFrame->data, rgbFrame->linesize);
                        sws_freeContext(swsContext);
                        
                        // Encode frame to image format
                        if (saveFrameAsImage(rgbFrame, outputPath, format)) {
                            extractedFrames.append(outputPath);
                            frameNumber++;
                        } else {
                            Logger::instance().warn("Failed to save frame as image: {}", outputPath.toStdString());
                        }
                    }
                }
                
                av_frame_free(&rgbFrame);
                av_packet_unref(packet);
            }
            
            currentTime += intervalSeconds;
        }
        
        // Cleanup
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&decoder);
        closeFormatContext(inputFormat);
        
        return extractedFrames;
    });
}

QFuture<Expected<QString, FFmpegError>> FFmpegWrapper::applyFilters(
    const QString& inputPath,
    const QString& outputPath,
    const QString& filterGraph,
    const ConversionOptions& options) {
    
    return QtConcurrent::run([this, inputPath, outputPath, filterGraph, options]() -> Expected<QString, FFmpegError> {
        auto validateResult = validateFilePath(inputPath, true);
        if (validateResult.hasError()) {
            return makeUnexpected(validateResult.error());
        }
        
        // Validate output path directory
        QFileInfo outputInfo(outputPath);
        QDir outputDir = outputInfo.dir();
        if (!outputDir.exists() && !outputDir.mkpath(".")) {
            return makeUnexpected(FFmpegError::IOError);
        }
        
        // Open input file
        auto openInputResult = openInputFile(inputPath);
        if (openInputResult.hasError()) {
            return makeUnexpected(openInputResult.error());
        }
        AVFormatContext* inputFormat = openInputResult.value();
        
        // Find video stream
        auto videoStreamResult = findBestVideoStream(inputFormat);
        if (videoStreamResult.hasError()) {
            closeFormatContext(inputFormat);
            return makeUnexpected(videoStreamResult.error());
        }
        int videoStreamIndex = videoStreamResult.value();
        
        AVStream* videoStream = inputFormat->streams[videoStreamIndex];
        
        // Create decoder
        auto decoderResult = createVideoDecoder(videoStream);
        if (decoderResult.hasError()) {
            closeFormatContext(inputFormat);
            return makeUnexpected(decoderResult.error());
        }
        AVCodecContext* decoder = decoderResult.value();
        
        // Create output format context
        auto openOutputResult = createOutputFile(outputPath, options.containerFormat);
        if (openOutputResult.hasError()) {
            avcodec_free_context(&decoder);
            closeFormatContext(inputFormat);
            return makeUnexpected(openOutputResult.error());
        }
        AVFormatContext* outputFormat = openOutputResult.value();
        
        // Extract video info for filter graph creation
        VideoStreamInfo videoInfo = extractVideoStreamInfo(videoStream, decoder);
        
        // Create filter graph
        auto filterGraphResult = createFilterGraph(filterGraph, videoInfo, options);
        if (filterGraphResult.hasError()) {
            closeFormatContext(outputFormat);
            avcodec_free_context(&decoder);
            closeFormatContext(inputFormat);
            return makeUnexpected(filterGraphResult.error());
        }
        AVFilterGraph* filterGraphPtr = filterGraphResult.value();
        
        // Create encoder based on filtered output
        auto encoderResult = createVideoEncoder(options, videoInfo);
        if (encoderResult.hasError()) {
            avfilter_graph_free(&filterGraphPtr);
            closeFormatContext(outputFormat);
            avcodec_free_context(&decoder);
            closeFormatContext(inputFormat);
            return makeUnexpected(encoderResult.error());
        }
        AVCodecContext* encoder = encoderResult.value();
        
        // Add video stream to output
        AVStream* outputStream = avformat_new_stream(outputFormat, encoder->codec);
        if (!outputStream) {
            avcodec_free_context(&encoder);
            avfilter_graph_free(&filterGraphPtr);
            closeFormatContext(outputFormat);
            avcodec_free_context(&decoder);
            closeFormatContext(inputFormat);
            return makeUnexpected(FFmpegError::AllocationFailed);
        }
        
        // Copy codec parameters
        if (avcodec_parameters_from_context(outputStream->codecpar, encoder) < 0) {
            avcodec_free_context(&encoder);
            avfilter_graph_free(&filterGraphPtr);
            closeFormatContext(outputFormat);
            avcodec_free_context(&decoder);
            closeFormatContext(inputFormat);
            return makeUnexpected(FFmpegError::InitializationFailed);
        }
        
        // Write header
        if (avformat_write_header(outputFormat, nullptr) < 0) {
            avcodec_free_context(&encoder);
            avfilter_graph_free(&filterGraphPtr);
            closeFormatContext(outputFormat);
            avcodec_free_context(&decoder);
            closeFormatContext(inputFormat);
            return makeUnexpected(FFmpegError::IOError);
        }
        
        // Process frames through filter graph
        AVPacket* packet = av_packet_alloc();
        AVFrame* inputFrame = av_frame_alloc();
        AVFrame* filteredFrame = av_frame_alloc();
        
        bool processingSuccess = true;
        
        while (av_read_frame(inputFormat, packet) >= 0) {
            if (packet->stream_index == videoStreamIndex) {
                if (avcodec_send_packet(decoder, packet) >= 0) {
                    while (avcodec_receive_frame(decoder, inputFrame) >= 0) {
                        // Process frame through filter graph
                        auto filterResult = processFrameThroughFilter(inputFrame, filteredFrame, filterGraphPtr);
                        if (filterResult.hasError()) {
                            Logger::instance().warn("Error processing frame through filter: {}", 
                                                   static_cast<int>(filterResult.error()));
                            continue;
                        }
                        
                        // Encode filtered frame
                        if (avcodec_send_frame(encoder, filteredFrame) >= 0) {
                            AVPacket* encodedPacket = av_packet_alloc();
                            while (avcodec_receive_packet(encoder, encodedPacket) >= 0) {
                                encodedPacket->stream_index = outputStream->index;
                                av_packet_rescale_ts(encodedPacket, encoder->time_base, outputStream->time_base);
                                
                                if (av_interleaved_write_frame(outputFormat, encodedPacket) < 0) {
                                    Logger::instance().error("Error writing frame to output");
                                    processingSuccess = false;
                                }
                            }
                            av_packet_free(&encodedPacket);
                        }
                    }
                }
            }
            av_packet_unref(packet);
        }
        
        // Flush encoder
        if (avcodec_send_frame(encoder, nullptr) >= 0) {
            AVPacket* encodedPacket = av_packet_alloc();
            while (avcodec_receive_packet(encoder, encodedPacket) >= 0) {
                encodedPacket->stream_index = outputStream->index;
                av_packet_rescale_ts(encodedPacket, encoder->time_base, outputStream->time_base);
                av_interleaved_write_frame(outputFormat, encodedPacket);
            }
            av_packet_free(&encodedPacket);
        }
        
        // Write trailer
        av_write_trailer(outputFormat);
        
        // Cleanup
        av_frame_free(&filteredFrame);
        av_frame_free(&inputFrame);
        av_packet_free(&packet);
        avcodec_free_context(&encoder);
        avfilter_graph_free(&filterGraphPtr);
        closeFormatContext(outputFormat);
        avcodec_free_context(&decoder);
        closeFormatContext(inputFormat);
        
        if (processingSuccess) {
            return outputPath;
        } else {
            return makeUnexpected(FFmpegError::FilteringFailed);
        }
    });
}

void FFmpegWrapper::cancelOperation(const QString& operationId) {
    QMutexLocker locker(&d->operationsMutex);
    
    auto it = d->activeOperations.find(operationId);
    if (it != d->activeOperations.end()) {
        it.value()->cancelled = true;
        emit operationCancelled(operationId);
        Logger::instance().info("Operation cancelled: {}", operationId.toStdString());
    }
}

void FFmpegWrapper::cancelAllOperations() {
    QMutexLocker locker(&d->operationsMutex);
    
    // Background threads are responsible for their own cleanup after they notice the cancellation flag
    for (auto it = d->activeOperations.begin(); it != d->activeOperations.end(); ++it) {
        it.value()->cancelled = true;
        emit operationCancelled(it.key());
    }
    Logger::instance().info("All operations cancelled");
}

QStringList FFmpegWrapper::getActiveOperations() const {
    QMutexLocker locker(&d->operationsMutex);
    return d->activeOperations.keys();
}

bool FFmpegWrapper::isHardwareAccelAvailable(HardwareAccel hwAccel) const {
    return d->availableHwAccel.contains(hwAccel);
}

QList<HardwareAccel> FFmpegWrapper::getAvailableHardwareAccel() const {
    return d->availableHwAccel;
}

// Static utility methods
QStringList FFmpegWrapper::getSupportedInputFormats() {
    QStringList formats;
    
    const AVInputFormat* format = nullptr;
    void* opaque = nullptr;
    
    while ((format = av_demuxer_iterate(&opaque))) {
        formats << QString::fromUtf8(format->name);
    }
    
    return formats;
}

QStringList FFmpegWrapper::getSupportedOutputFormats() {
    QStringList formats;
    
    const AVOutputFormat* format = nullptr;
    void* opaque = nullptr;
    
    while ((format = av_muxer_iterate(&opaque))) {
        formats << QString::fromUtf8(format->name);
    }
    
    return formats;
}

QStringList FFmpegWrapper::getSupportedVideoCodecs() {
    QStringList codecs;
    
    const AVCodec* codec = nullptr;
    void* opaque = nullptr;
    
    while ((codec = av_codec_iterate(&opaque))) {
        if (av_codec_is_encoder(codec) && codec->type == AVMEDIA_TYPE_VIDEO) {
            codecs << QString::fromUtf8(codec->name);
        }
    }
    
    return codecs;
}

QStringList FFmpegWrapper::getSupportedAudioCodecs() {
    QStringList codecs;
    
    const AVCodec* codec = nullptr;
    void* opaque = nullptr;
    
    while ((codec = av_codec_iterate(&opaque))) {
        if (av_codec_is_encoder(codec) && codec->type == AVMEDIA_TYPE_AUDIO) {
            codecs << QString::fromUtf8(codec->name);
        }
    }
    
    return codecs;
}

QString FFmpegWrapper::getFFmpegVersion() {
    return QString::fromUtf8(av_version_info());
}

Expected<bool, FFmpegError> FFmpegWrapper::validateFile(const QString& filePath) {
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return makeUnexpected(FFmpegError::InvalidFile);
    }
    
    if (fileInfo.size() == 0) {
        return makeUnexpected(FFmpegError::InvalidFile);
    }
    
    return true;
}

Expected<bool, FFmpegError> FFmpegWrapper::initializeLibraries() {
    // Initialize FFmpeg libraries
    av_log_set_level(AV_LOG_WARNING);
    
    Logger::instance().info("Initializing FFmpeg version: {}", getFFmpegVersion().toStdString());
    return true;
}

Expected<bool, FFmpegError> FFmpegWrapper::detectHardwareAcceleration() {
    d->availableHwAccel.clear();
    d->availableHwAccel.append(HardwareAccel::None);
    
    // Check for hardware acceleration availability
    AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
    while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE) {
        const char* name = av_hwdevice_get_type_name(type);
        QString hwName = QString::fromUtf8(name);
        
        if (hwName == "videotoolbox") {
            d->availableHwAccel.append(HardwareAccel::VideoToolbox);
        } else if (hwName == "cuda") {
            d->availableHwAccel.append(HardwareAccel::CUDA);
        } else if (hwName == "vaapi") {
            d->availableHwAccel.append(HardwareAccel::VAAPI);
        } else if (hwName == "qsv") {
            d->availableHwAccel.append(HardwareAccel::QSV);
        } else if (hwName == "dxva2") {
            d->availableHwAccel.append(HardwareAccel::DXVA2);
        } else if (hwName == "d3d11va") {
            d->availableHwAccel.append(HardwareAccel::D3D11VA);
        }
        
        Logger::instance().info("Found hardware acceleration: {}", hwName.toStdString());
    }
    
    d->hwaccelInitialized = true;
    return true;
}

void FFmpegWrapper::waitForAllOperations() {
    // Block until all background operations have completed
    QMutexLocker locker(&d->operationsMutex);
    if (d->activeOperations.isEmpty()) {
        return;
    }

    Logger::instance().info("Waiting for {} active operations to complete...", d->activeOperations.size());
    while (!d->activeOperations.isEmpty()) {
        locker.unlock();
        QThread::msleep(100); // Give threads time to finish and clean up
        locker.relock();
    }
    Logger::instance().info("All active operations have completed.");
}


void FFmpegWrapper::shutdownLibraries() {
    if (d->librariesInitialized) {
        cancelAllOperations();  // Signal all operations to stop
        waitForAllOperations(); // Wait for them to actually finish
        d->librariesInitialized = false;
        Logger::instance().info("FFmpeg libraries shutdown");
    }
}

Expected<AVFormatContext*, FFmpegError> FFmpegWrapper::openInputFile(const QString& filePath) {
    AVFormatContext* formatContext = nullptr;
    
    int ret = avformat_open_input(&formatContext, filePath.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        Logger::instance().error("Failed to open input file: {} ({})", 
                                        filePath.toStdString(), 
                                        getAVErrorString(ret).toStdString());
        return makeUnexpected(mapAVError(ret));
    }
    
    ret = avformat_find_stream_info(formatContext, nullptr);
    if (ret < 0) {
        avformat_close_input(&formatContext);
        Logger::instance().error("Failed to find stream info: {}", 
                                        getAVErrorString(ret).toStdString());
        return makeUnexpected(mapAVError(ret));
    }
    
    return formatContext;
}

Expected<AVFormatContext*, FFmpegError> FFmpegWrapper::createOutputFile(const QString& filePath, const QString& format) {
    AVFormatContext* formatContext = nullptr;
    
    int ret = avformat_alloc_output_context2(&formatContext, nullptr, 
                                           format.isEmpty() ? nullptr : format.toUtf8().constData(),
                                           filePath.toUtf8().constData());
    if (ret < 0) {
        Logger::instance().error("Failed to create output context: {}", 
                                        getAVErrorString(ret).toStdString());
        return makeUnexpected(mapAVError(ret));
    }
    
    return formatContext;
}

void FFmpegWrapper::closeFormatContext(AVFormatContext* context) {
    if (context) {
        if (context->oformat) {
            // For output contexts, we must close the file handle if it was opened
            if (!(context->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&context->pb);
            }
            avformat_free_context(context);
        } else {
            avformat_close_input(&context);
        }
    }
}

Expected<int, FFmpegError> FFmpegWrapper::findBestVideoStream(AVFormatContext* formatContext) {
    int ret = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (ret < 0) {
        return makeUnexpected(FFmpegError::InvalidFile);
    }
    return ret;
}

Expected<int, FFmpegError> FFmpegWrapper::findBestAudioStream(AVFormatContext* formatContext) {
    int ret = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (ret < 0) {
        return makeUnexpected(FFmpegError::InvalidFile);
    }
    return ret;
}

Expected<AVCodecContext*, FFmpegError> FFmpegWrapper::createVideoDecoder(AVStream* stream) {
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        return makeUnexpected(FFmpegError::UnsupportedFormat);
    }
    
    AVCodecContext* codecContext = avcodec_alloc_context3(decoder);
    if (!codecContext) {
        return makeUnexpected(FFmpegError::AllocationFailed);
    }
    
    int ret = avcodec_parameters_to_context(codecContext, stream->codecpar);
    if (ret < 0) {
        avcodec_free_context(&codecContext);
        return makeUnexpected(mapAVError(ret));
    }
    
    ret = avcodec_open2(codecContext, decoder, nullptr);
    if (ret < 0) {
        avcodec_free_context(&codecContext);
        return makeUnexpected(mapAVError(ret));
    }
    
    return codecContext;
}

std::vector<AVFrame*> FFmpegWrapper::bufferAudioFrame(OperationContext* context, AVFrame* inputFrame) {
    std::vector<AVFrame*> outputFrames;
    
    if (context->targetAudioFrameSize <= 0) {
        // No frame size constraints, just pass through
        outputFrames.push_back(inputFrame);
        return outputFrames;
    }
    
    // For AAC encoder, we need exactly the right frame size
    // If the input frame size matches, use it directly
    if (inputFrame->nb_samples == context->targetAudioFrameSize) {
        outputFrames.push_back(inputFrame);
        return outputFrames;
    }
    
    // If the input frame size is different, we need to handle it
    // For now, create a frame with the correct size by truncating or padding
    AVFrame* outputFrame = av_frame_alloc();
    outputFrame->nb_samples = context->targetAudioFrameSize;
    outputFrame->format = context->audioEncoder->sample_fmt;
    outputFrame->sample_rate = context->audioEncoder->sample_rate;
    outputFrame->ch_layout = context->audioEncoder->ch_layout;
    
    if (av_frame_get_buffer(outputFrame, 0) >= 0) {
        // Copy samples from input to output
        int samplesToCopy = std::min(inputFrame->nb_samples, context->targetAudioFrameSize);
        
        if (av_sample_fmt_is_planar(context->audioEncoder->sample_fmt)) {
            // Planar format: each channel in separate buffer
            for (int ch = 0; ch < context->audioEncoder->ch_layout.nb_channels; ch++) {
                int bytesPerSample = av_get_bytes_per_sample(context->audioEncoder->sample_fmt);
                int bytesToCopy = samplesToCopy * bytesPerSample;
                std::memcpy(outputFrame->data[ch], inputFrame->data[ch], bytesToCopy);
                
                // Pad with zeros if needed
                if (samplesToCopy < context->targetAudioFrameSize) {
                    int padBytes = (context->targetAudioFrameSize - samplesToCopy) * bytesPerSample;
                    std::memset(outputFrame->data[ch] + bytesToCopy, 0, padBytes);
                }
            }
        } else {
            // Packed format: all channels interleaved
            int bytesPerSample = av_get_bytes_per_sample(context->audioEncoder->sample_fmt);
            int channels = context->audioEncoder->ch_layout.nb_channels;
            int bytesToCopy = samplesToCopy * bytesPerSample * channels;
            std::memcpy(outputFrame->data[0], inputFrame->data[0], bytesToCopy);
            
            // Pad with zeros if needed
            if (samplesToCopy < context->targetAudioFrameSize) {
                int padBytes = (context->targetAudioFrameSize - samplesToCopy) * bytesPerSample * channels;
                std::memset(outputFrame->data[0] + bytesToCopy, 0, padBytes);
            }
        }
        
        outputFrames.push_back(outputFrame);
    } else {
        av_frame_free(&outputFrame);
        // Fall back to original frame if buffer allocation fails
        outputFrames.push_back(inputFrame);
    }
    
    return outputFrames;
}

FFmpegError FFmpegWrapper::mapAVError(int averror) const {
    try {
        QString errorString = QString::fromUtf8(av_err2str(averror));
        Logger::instance().debug("mapAVError called with error code: {} ({})", averror, errorString.toStdString());
        
        switch (averror) {
            case AVERROR(ENOENT):
                return FFmpegError::InvalidFile;
            case AVERROR(EINVAL):
                Logger::instance().error("FFmpeg returned EINVAL error: {}", errorString.toStdString());
                return FFmpegError::InvalidParameters;
            case AVERROR(ENOMEM):
                return FFmpegError::AllocationFailed;
            case AVERROR_EOF:
                return FFmpegError::IOError;
            case AVERROR_INVALIDDATA:
                return FFmpegError::UnsupportedFormat;
            default:
                Logger::instance().warn("Unknown FFmpeg error: {} ({})", averror, errorString.toStdString());
                return FFmpegError::UnknownError;
        }
    } catch (const std::exception& e) {
        Logger::instance().error("Exception in mapAVError: {}", e.what());
        return FFmpegError::UnknownError;
    } catch (...) {
        Logger::instance().error("Unknown exception in mapAVError");
        return FFmpegError::UnknownError;
    }
}

QString FFmpegWrapper::translateFFmpegError(FFmpegError error) const {
    switch (error) {
        case FFmpegError::InvalidFile:
            return "Invalid or corrupted file";
        case FFmpegError::UnsupportedFormat:
            return "Unsupported format or codec";
        case FFmpegError::InitializationFailed:
            return "Failed to initialize FFmpeg components";
        case FFmpegError::DecodingFailed:
            return "Video/audio decoding failed";
        case FFmpegError::EncodingFailed:
            return "Video/audio encoding failed";
        case FFmpegError::FilteringFailed:
            return "Video filtering failed";
        case FFmpegError::AllocationFailed:
            return "Memory allocation failed";
        case FFmpegError::IOError:
            return "Input/output error";
        case FFmpegError::InvalidParameters:
            return "Invalid parameters provided";
        case FFmpegError::HardwareError:
            return "Hardware acceleration error";
        case FFmpegError::CancellationRequested:
            return "Operation was cancelled";
        case FFmpegError::UnknownError:
        default:
            return "Unknown error occurred";
    }
}

QString FFmpegWrapper::getAVErrorString(int averror) const {
    char errorBuffer[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(averror, errorBuffer, AV_ERROR_MAX_STRING_SIZE);
    return QString::fromUtf8(errorBuffer);
}

QString FFmpegWrapper::generateOperationId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

Expected<bool, FFmpegError> FFmpegWrapper::validateFilePath(const QString& filePath, bool mustExist) {
    QFileInfo fileInfo(filePath);
    
    if (mustExist && !fileInfo.exists()) {
        return makeUnexpected(FFmpegError::InvalidFile);
    }
    
    if (mustExist && !fileInfo.isFile()) {
        return makeUnexpected(FFmpegError::InvalidFile);
    }
    
    if (mustExist && fileInfo.size() == 0) {
        return makeUnexpected(FFmpegError::InvalidFile);
    }
    
    return true;
}

Expected<bool, FFmpegError> FFmpegWrapper::validateConversionOptions(const ConversionOptions& options) {
    // Validate video codec
    if (!options.videoCodec.isEmpty()) {
        const AVCodec* codec = avcodec_find_encoder_by_name(options.videoCodec.toUtf8().constData());
        if (!codec) {
            Logger::instance().error("Video codec not found: {}", options.videoCodec.toStdString());
            return makeUnexpected(FFmpegError::UnsupportedFormat);
        }
        if (codec->type != AVMEDIA_TYPE_VIDEO) {
            Logger::instance().error("Codec is not a video encoder: {}", options.videoCodec.toStdString());
            return makeUnexpected(FFmpegError::UnsupportedFormat);
        }
    }
    
    // Validate audio codec
    if (!options.audioCodec.isEmpty()) {
        const AVCodec* codec = avcodec_find_encoder_by_name(options.audioCodec.toUtf8().constData());
        if (!codec) {
            Logger::instance().error("Audio codec not found: {}", options.audioCodec.toStdString());
            return makeUnexpected(FFmpegError::UnsupportedFormat);
        }
        if (codec->type != AVMEDIA_TYPE_AUDIO) {
            Logger::instance().error("Codec is not an audio encoder: {}", options.audioCodec.toStdString());
            return makeUnexpected(FFmpegError::UnsupportedFormat);
        }
    }
    
    // Validate bitrates
    if (options.videoBitrate < 0 || options.videoBitrate > 100000) {
        Logger::instance().error("Invalid video bitrate: {}", options.videoBitrate);
        return makeUnexpected(FFmpegError::InvalidParameters);
    }
    
    if (options.audioBitrate < 0 || options.audioBitrate > 1000) {
        Logger::instance().error("Invalid audio bitrate: {}", options.audioBitrate);
        return makeUnexpected(FFmpegError::InvalidParameters);
    }
    
    Logger::instance().debug("Conversion options validated successfully - Video: {}, Audio: {}, VideoBitrate: {}, AudioBitrate: {}", 
                            options.videoCodec.toStdString(), options.audioCodec.toStdString(), 
                            options.videoBitrate, options.audioBitrate);
    
    return true;
}

void FFmpegWrapper::cleanupOperation(const QString& operationId) {
    auto it = d->activeOperations.find(operationId);
    if (it != d->activeOperations.end()) {
        auto context = it.value();
        
        // Cleanup FFmpeg resources
        if (context->swsContext) {
            sws_freeContext(context->swsContext);
        }
        if (context->swrContext) {
            swr_free(&context->swrContext);
        }
        if (context->filterGraph) {
            avfilter_graph_free(&context->filterGraph);
        }
        if (context->videoDecoder) {
            avcodec_free_context(&context->videoDecoder);
        }
        if (context->audioDecoder) {
            avcodec_free_context(&context->audioDecoder);
        }
        if (context->videoEncoder) {
            avcodec_free_context(&context->videoEncoder);
        }
        if (context->audioEncoder) {
            avcodec_free_context(&context->audioEncoder);
        }
        if (context->inputFormat) {
            avformat_close_input(&context->inputFormat);
        }
        if (context->outputFormat) {
             if (!(context->outputFormat->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&context->outputFormat->pb);
            }
            avformat_free_context(context->outputFormat);
        }
        
        delete context;
        d->activeOperations.erase(it);
    }
}

Expected<AVCodecContext*, FFmpegError> FFmpegWrapper::createAudioDecoder(AVStream* stream) {
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        return makeUnexpected(FFmpegError::UnsupportedFormat);
    }

    AVCodecContext* codecContext = avcodec_alloc_context3(decoder);
    if (!codecContext) {
        return makeUnexpected(FFmpegError::AllocationFailed);
    }

    int ret = avcodec_parameters_to_context(codecContext, stream->codecpar);
    if (ret < 0) {
        avcodec_free_context(&codecContext);
        return makeUnexpected(mapAVError(ret));
    }

    ret = avcodec_open2(codecContext, decoder, nullptr);
    if (ret < 0) {
        avcodec_free_context(&codecContext);
        return makeUnexpected(mapAVError(ret));
    }

    return codecContext;
}

Expected<QString, FFmpegError> FFmpegWrapper::performVideoConversion(const QString& operationId) {
    Logger::instance().debug("performVideoConversion called with operationId: {}", operationId.toStdString());
    
    {
        QMutexLocker locker(&d->operationsMutex);
        Logger::instance().debug("Current active operations count: {}", d->activeOperations.size());
        for (auto it = d->activeOperations.begin(); it != d->activeOperations.end(); ++it) {
            Logger::instance().debug("Active operation: {}", it.key().toStdString());
        }
    }
    
    OperationContext* context = nullptr;
    {
        QMutexLocker locker(&d->operationsMutex);
        auto operationIt = d->activeOperations.find(operationId);
        if (operationIt == d->activeOperations.end()) {
            Logger::instance().error("Operation not found in active operations: {}", operationId.toStdString());
            return makeUnexpected(FFmpegError::InvalidParameters);
        }
        context = operationIt.value();
    }

    AVFormatContext* inFmtCtx = nullptr;
    AVFormatContext* outFmtCtx = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* filt_frame = nullptr;
    AVFrame* resampled_frame = nullptr;
    int video_out_stream_idx = -1;
    int audio_out_stream_idx = -1;
    int ret = 0;

    // 1. Open Input File
    Logger::instance().debug("Opening input file: {}", context->inputPath.toStdString());
    if ((ret = avformat_open_input(&inFmtCtx, context->inputPath.toUtf8().constData(), nullptr, nullptr)) < 0) {
        Logger::instance().error("Failed to open input file: {}", context->inputPath.toStdString());
        return makeUnexpected(mapAVError(ret));
    }
    context->inputFormat = inFmtCtx;

    Logger::instance().debug("Finding stream info for input file");
    if ((ret = avformat_find_stream_info(inFmtCtx, nullptr)) < 0) {
        Logger::instance().error("Failed to find stream info");
        goto end;
    }

    // 2. Setup Output
    Logger::instance().debug("Setting up output context for: {}", context->outputPath.toStdString());
    avformat_alloc_output_context2(&outFmtCtx, nullptr, nullptr, context->outputPath.toUtf8().constData());
    if (!outFmtCtx) {
        Logger::instance().error("Failed to allocate output context");
        ret = AVERROR(ENOMEM);
        goto end;
    }
    context->outputFormat = outFmtCtx;

    // 3. Setup Streams, Decoders, and Encoders
    for (unsigned int i = 0; i < inFmtCtx->nb_streams; i++) {
        AVStream* in_stream = inFmtCtx->streams[i];
        const AVCodec* decoder = avcodec_find_decoder(in_stream->codecpar->codec_id);
        if (!decoder) continue;

        if (decoder->type == AVMEDIA_TYPE_VIDEO && context->videoDecoder == nullptr) {
            // Video Stream Setup
            context->videoStreamIndex = i;
            
            auto decoderResult = createVideoDecoder(in_stream);
            if (decoderResult.hasError()) { ret = AVERROR(EINVAL); goto end; }
            context->videoDecoder = decoderResult.value();

            const AVCodec* encoder = avcodec_find_encoder_by_name(context->options.videoCodec.toUtf8().constData());
            if (!encoder) { ret = AVERROR(EINVAL); goto end; }
            context->videoEncoder = avcodec_alloc_context3(encoder);
            
            context->videoEncoder->height = in_stream->codecpar->height;
            context->videoEncoder->width = in_stream->codecpar->width;
            context->videoEncoder->sample_aspect_ratio = in_stream->codecpar->sample_aspect_ratio;
            // Use fallback approach for deprecated pix_fmts
            context->videoEncoder->pix_fmt = AV_PIX_FMT_YUV420P;
            if (encoder->pix_fmts) {
                context->videoEncoder->pix_fmt = encoder->pix_fmts[0];
            }
            context->videoEncoder->time_base = av_inv_q(in_stream->r_frame_rate);

            Logger::instance().debug("Opening video encoder: {}", context->options.videoCodec.toStdString());
            if ((ret = avcodec_open2(context->videoEncoder, encoder, nullptr)) < 0) {
                Logger::instance().error("Failed to open video encoder: {}", context->options.videoCodec.toStdString());
                goto end;
            }
            
            AVStream* out_stream = avformat_new_stream(outFmtCtx, nullptr);
            if (!out_stream) { ret = AVERROR(ENOMEM); goto end; }
            video_out_stream_idx = out_stream->index;
            avcodec_parameters_from_context(out_stream->codecpar, context->videoEncoder);
            out_stream->time_base = context->videoEncoder->time_base;

        } else if (decoder->type == AVMEDIA_TYPE_AUDIO && context->audioDecoder == nullptr) {
            // Audio Stream Setup
            context->audioStreamIndex = i;

            auto decoderResult = createAudioDecoder(in_stream);
            if (decoderResult.hasError()) { ret = AVERROR(EINVAL); goto end; }
            context->audioDecoder = decoderResult.value();

            const AVCodec* encoder = avcodec_find_encoder_by_name(context->options.audioCodec.toUtf8().constData());
            if (!encoder) { ret = AVERROR(EINVAL); goto end; }
            context->audioEncoder = avcodec_alloc_context3(encoder);
            
            av_channel_layout_default(&context->audioEncoder->ch_layout, 2);
            // Use fallback approach for deprecated audio format fields
            context->audioEncoder->sample_rate = 44100;
            context->audioEncoder->sample_fmt = AV_SAMPLE_FMT_FLTP;
            
            if (encoder->supported_samplerates) {
                context->audioEncoder->sample_rate = encoder->supported_samplerates[0];
            }
            if (encoder->sample_fmts) {
                context->audioEncoder->sample_fmt = encoder->sample_fmts[0];
            }
            context->audioEncoder->time_base = {1, context->audioEncoder->sample_rate};
            
            // Set frame size for AAC encoder (required for proper header writing)
            if (context->options.audioCodec == "aac") {
                context->audioEncoder->frame_size = 1024;  // Standard AAC frame size
                context->targetAudioFrameSize = 1024;
                // Also set bit rate for AAC encoder
                context->audioEncoder->bit_rate = context->options.audioBitrate * 1000;
            } else {
                context->audioEncoder->frame_size = 0;  // Let encoder decide
                context->targetAudioFrameSize = 0;
                context->audioEncoder->bit_rate = context->options.audioBitrate * 1000;
            }
            
            Logger::instance().debug("Opening audio encoder: {}", context->options.audioCodec.toStdString());
            ret = avcodec_open2(context->audioEncoder, encoder, nullptr);
            if (ret < 0) {
                Logger::instance().error("Failed to open audio encoder: {}", context->options.audioCodec.toStdString());
                Logger::instance().debug("avcodec_open2 failed with error: {}", ret);
                
                // For AAC encoder failures, try falling back to libmp3lame
                if (context->options.audioCodec == "aac") {
                    Logger::instance().info("Falling back to libmp3lame audio encoder");
                    avcodec_free_context(&context->audioEncoder);
                    
                    encoder = avcodec_find_encoder_by_name("libmp3lame");
                    if (!encoder) {
                        Logger::instance().error("libmp3lame encoder not found");
                        ret = AVERROR(EINVAL);
                        goto end;
                    }
                    
                    context->audioEncoder = avcodec_alloc_context3(encoder);
                    av_channel_layout_default(&context->audioEncoder->ch_layout, 2);
                    context->audioEncoder->sample_rate = 44100;
                    context->audioEncoder->sample_fmt = AV_SAMPLE_FMT_S16P;
                    context->audioEncoder->bit_rate = context->options.audioBitrate * 1000;
                    context->audioEncoder->time_base = {1, context->audioEncoder->sample_rate};
                    
                    ret = avcodec_open2(context->audioEncoder, encoder, nullptr);
                    if (ret < 0) {
                        Logger::instance().error("Failed to open libmp3lame encoder as fallback");
                        goto end;
                    }
                } else {
                    goto end;
                }
            }
            
            AVStream* out_stream = avformat_new_stream(outFmtCtx, nullptr);
            if (!out_stream) { ret = AVERROR(ENOMEM); goto end; }
            audio_out_stream_idx = out_stream->index;
            avcodec_parameters_from_context(out_stream->codecpar, context->audioEncoder);
            out_stream->time_base = context->audioEncoder->time_base;
        }
    }

    if (context->audioDecoder && context->audioEncoder) {
        Logger::instance().debug("Setting up audio resampler");
        ret = swr_alloc_set_opts2(&context->swrContext,
                                &context->audioEncoder->ch_layout,
                                context->audioEncoder->sample_fmt,
                                context->audioEncoder->sample_rate,
                                &context->audioDecoder->ch_layout,
                                context->audioDecoder->sample_fmt,
                                context->audioDecoder->sample_rate,
                                0, nullptr);
        if (ret < 0) {
            Logger::instance().error("Failed to allocate audio resampler");
            goto end;
        }

        Logger::instance().debug("Initializing audio resampler");
        if (!context->swrContext || (ret = swr_init(context->swrContext)) < 0) {
            Logger::instance().error("Failed to initialize audio resampler");
            swr_free(&context->swrContext);
            goto end;
        }
    }

    if (!(outFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        Logger::instance().debug("Opening output file for writing");
        if ((ret = avio_open(&outFmtCtx->pb, context->outputPath.toUtf8().constData(), AVIO_FLAG_WRITE)) < 0) {
            Logger::instance().error("Failed to open output file for writing");
            goto end;
        }
    }

    Logger::instance().debug("Writing output header");
    if ((ret = avformat_write_header(outFmtCtx, nullptr)) < 0) {
        Logger::instance().error("Failed to write output header");
        goto end;
    }

    // 4. Transcoding Loop
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    filt_frame = av_frame_alloc();
    resampled_frame = av_frame_alloc();

    if (!packet || !frame || !filt_frame || !resampled_frame) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    while (av_read_frame(inFmtCtx, packet) >= 0) {
        if (context->cancelled) { break; }

        AVStream* in_stream = inFmtCtx->streams[packet->stream_index];
        AVCodecContext* dec_ctx = (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) ? context->videoDecoder : context->audioDecoder;
        AVCodecContext* enc_ctx = (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) ? context->videoEncoder : context->audioEncoder;
        int out_stream_idx = (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) ? video_out_stream_idx : audio_out_stream_idx;
        AVStream* out_stream = (out_stream_idx >= 0) ? outFmtCtx->streams[out_stream_idx] : nullptr;

        if (!dec_ctx || !enc_ctx || !out_stream) {
            av_packet_unref(packet);
            continue;
        }

        if ((ret = avcodec_send_packet(dec_ctx, packet)) < 0) break;
        while (ret >= 0) {
            ret = avcodec_receive_frame(dec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) goto end;
            
            AVFrame* frame_to_encode = frame;
            
            if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && context->swrContext) {
                av_frame_make_writable(resampled_frame);
                resampled_frame->sample_rate = context->audioEncoder->sample_rate;
                av_channel_layout_copy(&resampled_frame->ch_layout, &context->audioEncoder->ch_layout);
                resampled_frame->format = context->audioEncoder->sample_fmt;

                ret = swr_convert_frame(context->swrContext, resampled_frame, frame);
                if (ret < 0) goto end;

                resampled_frame->pts = av_rescale_q(frame->pts, in_stream->time_base, enc_ctx->time_base);
                
                // Use frame buffering for proper frame size handling
                std::vector<AVFrame*> bufferedFrames = bufferAudioFrame(context, resampled_frame);
                
                // Encode all buffered frames
                for (AVFrame* bufferedFrame : bufferedFrames) {
                    if (avcodec_send_frame(enc_ctx, bufferedFrame) < 0) {
                        Logger::instance().warn("Failed to send frame to audio encoder");
                        continue;
                    }
                    
                    // Only free the frame if it's not the original input frame
                    if (bufferedFrame != resampled_frame && bufferedFrame != frame) {
                        av_frame_free(&bufferedFrame);
                    }
                }
                
                // Continue to packet processing
                ret = 0;
            } else {
                 frame_to_encode->pts = av_rescale_q(frame->pts, in_stream->time_base, enc_ctx->time_base);
                 if ((ret = avcodec_send_frame(enc_ctx, frame_to_encode)) < 0) goto end;
            }

            while (ret >= 0) {
                AVPacket* enc_pkt = av_packet_alloc();
                ret = avcodec_receive_packet(enc_ctx, enc_pkt);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    av_packet_free(&enc_pkt);
                    break;
                }
                if (ret < 0) {
                    av_packet_free(&enc_pkt);
                    goto end;
                }

                enc_pkt->stream_index = out_stream_idx;
                av_packet_rescale_ts(enc_pkt, enc_ctx->time_base, out_stream->time_base);
                if ((ret = av_interleaved_write_frame(outFmtCtx, enc_pkt)) < 0) {
                    av_packet_free(&enc_pkt);
                    goto end;
                }
                av_packet_free(&enc_pkt);
            }
        }
        av_packet_unref(packet);
    }
    ret = 0; // Reset ret after normal loop exit

end:
    // 5. Flush Encoders
    if (context->videoEncoder) {
        avcodec_send_frame(context->videoEncoder, nullptr); // Flush
    }
    if (context->audioEncoder) {
        avcodec_send_frame(context->audioEncoder, nullptr); // Flush
    }

    if (outFmtCtx) {
        av_write_trailer(outFmtCtx);
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    av_frame_free(&filt_frame);
    av_frame_free(&resampled_frame);

    if (ret < 0 && ret != AVERROR_EOF) {
        return makeUnexpected(mapAVError(ret));
    }
    
    if (context->cancelled) {
        // Remove the incomplete output file before returning
        QFile::remove(context->outputPath);
        return makeUnexpected(FFmpegError::CancellationRequested);
    }

    return context->outputPath;
}

Expected<QString, FFmpegError> FFmpegWrapper::performAudioExtraction(
    const QString& inputPath,
    const QString& outputPath,
    const ConversionOptions& options) {
    Q_UNUSED(options);
    AVFormatContext* inputFormatCtx = nullptr;
    AVFormatContext* outputFormatCtx = nullptr;
    
    // Open input file
    if (avformat_open_input(&inputFormatCtx, inputPath.toLocal8Bit().data(), nullptr, nullptr) != 0) {
        return makeUnexpected(FFmpegError::InvalidFile);
    }
    
    // Retrieve stream information
    if (avformat_find_stream_info(inputFormatCtx, nullptr) < 0) {
        avformat_close_input(&inputFormatCtx);
        return makeUnexpected(FFmpegError::UnsupportedFormat);
    }
    
    // Find audio stream
    int audioStreamIndex = -1;
    for (unsigned int i = 0; i < inputFormatCtx->nb_streams; i++) {
        if (inputFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = i;
            break;
        }
    }
    
    if (audioStreamIndex == -1) {
        avformat_close_input(&inputFormatCtx);
        return makeUnexpected(FFmpegError::InvalidFile);
    }
    
    // Create output format context
    if (avformat_alloc_output_context2(&outputFormatCtx, nullptr, nullptr, outputPath.toLocal8Bit().data()) < 0) {
        avformat_close_input(&inputFormatCtx);
        return makeUnexpected(FFmpegError::EncodingFailed);
    }
    
    AVStream* inputStream = inputFormatCtx->streams[audioStreamIndex];
    
    // Setup audio encoder
    const AVCodec* encoder = nullptr;
    
    // Determine output format based on file extension or options
    QString outputExt = QFileInfo(outputPath).suffix().toLower();
    if (outputExt == "wav") {
        encoder = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    } else if (outputExt == "mp3") {
        encoder = avcodec_find_encoder(AV_CODEC_ID_MP3);
    } else if (outputExt == "aac") {
        encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
    } else {
        // Default to AAC
        encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
    }
    
    if (!encoder) {
        avformat_close_input(&inputFormatCtx);
        avformat_free_context(outputFormatCtx);
        return makeUnexpected(FFmpegError::UnsupportedFormat);
    }
    
    AVCodecContext* encoderCtx = avcodec_alloc_context3(encoder);
    if (!encoderCtx) {
        avformat_close_input(&inputFormatCtx);
        avformat_free_context(outputFormatCtx);
        return makeUnexpected(FFmpegError::InitializationFailed);
    }
    
    // Set encoding parameters
    encoderCtx->sample_rate = options.audioSampleRate > 0 ? options.audioSampleRate : inputStream->codecpar->sample_rate;
    int channels = options.audioChannels > 0 ? options.audioChannels : inputStream->codecpar->ch_layout.nb_channels;
    av_channel_layout_default(&encoderCtx->ch_layout, channels);
    encoderCtx->bit_rate = options.audioBitrate > 0 ? options.audioBitrate * 1000 : 128000; // kbps to bps, with a default

    if (encoder->id == AV_CODEC_ID_PCM_S16LE) {
        encoderCtx->sample_fmt = AV_SAMPLE_FMT_S16;
    } else if (encoder->id == AV_CODEC_ID_MP3) {
        encoderCtx->sample_fmt = AV_SAMPLE_FMT_S16P;
    } else { // AAC and other modern codecs often prefer planar float.
        encoderCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    }
    
    // Open encoder
    if (avcodec_open2(encoderCtx, encoder, nullptr) < 0) {
        avcodec_free_context(&encoderCtx);
        avformat_close_input(&inputFormatCtx);
        avformat_free_context(outputFormatCtx);
        return makeUnexpected(FFmpegError::UnsupportedFormat);
    }
    
    // Create output stream
    AVStream* outputStream = avformat_new_stream(outputFormatCtx, encoder);
    if (!outputStream) {
        avcodec_free_context(&encoderCtx);
        avformat_close_input(&inputFormatCtx);
        avformat_free_context(outputFormatCtx);
        return makeUnexpected(FFmpegError::EncodingFailed);
    }
    
    // Copy encoder parameters to output stream
    if (avcodec_parameters_from_context(outputStream->codecpar, encoderCtx) < 0) {
        avcodec_free_context(&encoderCtx);
        avformat_close_input(&inputFormatCtx);
        avformat_free_context(outputFormatCtx);
        return makeUnexpected(FFmpegError::EncodingFailed);
    }
    
    // Open output file
    if (!(outputFormatCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&outputFormatCtx->pb, outputPath.toLocal8Bit().data(), AVIO_FLAG_WRITE) < 0) {
            avcodec_free_context(&encoderCtx);
            avformat_close_input(&inputFormatCtx);
            avformat_free_context(outputFormatCtx);
            return makeUnexpected(FFmpegError::IOError);
        }
    }
    
    // Write header
    if (avformat_write_header(outputFormatCtx, nullptr) < 0) {
        avcodec_free_context(&encoderCtx);
        avformat_close_input(&inputFormatCtx);
        if (!(outputFormatCtx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&outputFormatCtx->pb);
        }
        avformat_free_context(outputFormatCtx);
        return makeUnexpected(FFmpegError::IOError);
    }
    
    // Process audio packets
    AVPacket* packet = av_packet_alloc();
    if (!packet) {
        avcodec_free_context(&encoderCtx);
        avformat_close_input(&inputFormatCtx);
        av_write_trailer(outputFormatCtx);
        if (!(outputFormatCtx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&outputFormatCtx->pb);
        }
        avformat_free_context(outputFormatCtx);
        return makeUnexpected(FFmpegError::AllocationFailed);
    }
    
    while (av_read_frame(inputFormatCtx, packet) >= 0) {
        if (packet->stream_index == audioStreamIndex) {
            // Create decoder for audio stream
            const AVCodec* decoder = avcodec_find_decoder(inputStream->codecpar->codec_id);
            if (!decoder) {
                Logger::instance().warn("Failed to find audio decoder");
                av_packet_unref(packet);
                continue;
            }
            
            AVCodecContext* decoderCtx = avcodec_alloc_context3(decoder);
            if (avcodec_parameters_to_context(decoderCtx, inputStream->codecpar) < 0 ||
                avcodec_open2(decoderCtx, decoder, nullptr) < 0) {
                Logger::instance().warn("Failed to open audio decoder");
                avcodec_free_context(&decoderCtx);
                av_packet_unref(packet);
                continue;
            }
            
            // Decode audio frame
            if (avcodec_send_packet(decoderCtx, packet) < 0) {
                Logger::instance().warn("Failed to send packet to audio decoder");
                avcodec_free_context(&decoderCtx);
                av_packet_unref(packet);
                continue;
            }
            
            AVFrame* decodedFrame = av_frame_alloc();
            while (avcodec_receive_frame(decoderCtx, decodedFrame) >= 0) {
                // Resample audio if needed
                AVFrame* resampledFrame = decodedFrame;
                SwrContext* swrCtx = nullptr;
                
                if (decodedFrame->sample_rate != encoderCtx->sample_rate ||
                    decodedFrame->ch_layout.nb_channels != encoderCtx->ch_layout.nb_channels ||
                    decodedFrame->format != encoderCtx->sample_fmt) {
                    
                    swrCtx = swr_alloc();
                    av_opt_set_chlayout(swrCtx, "in_chlayout", &decodedFrame->ch_layout, 0);
                    av_opt_set_int(swrCtx, "in_sample_rate", decodedFrame->sample_rate, 0);
                    av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", (AVSampleFormat)decodedFrame->format, 0);
                    av_opt_set_chlayout(swrCtx, "out_chlayout", &encoderCtx->ch_layout, 0);
                    av_opt_set_int(swrCtx, "out_sample_rate", encoderCtx->sample_rate, 0);
                    av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", encoderCtx->sample_fmt, 0);
                    
                    if (swr_init(swrCtx) < 0) {
                        Logger::instance().warn("Failed to initialize audio resampler");
                        swr_free(&swrCtx);
                    } else {
                        resampledFrame = av_frame_alloc();
                        resampledFrame->sample_rate = encoderCtx->sample_rate;
                        resampledFrame->ch_layout = encoderCtx->ch_layout;
                        resampledFrame->format = encoderCtx->sample_fmt;
                        
                        // Calculate required output samples based on input
                        int out_samples = av_rescale_rnd(decodedFrame->nb_samples,
                                                        encoderCtx->sample_rate,
                                                        decodedFrame->sample_rate,
                                                        AV_ROUND_UP);
                        
                        // Use calculated output samples for resampling
                        resampledFrame->nb_samples = out_samples;
                        
                        if (av_frame_get_buffer(resampledFrame, 0) < 0) {
                            Logger::instance().warn("Failed to allocate audio frame buffer");
                            av_frame_free(&resampledFrame);
                            resampledFrame = decodedFrame;
                        } else {
                            // Convert audio samples, handling frame size constraints
                            int converted_samples = swr_convert(swrCtx, resampledFrame->data, resampledFrame->nb_samples,
                                                               (const uint8_t**)decodedFrame->data, decodedFrame->nb_samples);
                            if (converted_samples < 0) {
                                Logger::instance().warn("Failed to resample audio frame");
                                av_frame_free(&resampledFrame);
                                resampledFrame = decodedFrame;
                            } else {
                                // Update the actual number of samples converted
                                resampledFrame->nb_samples = converted_samples;
                            }
                        }
                        swr_free(&swrCtx);
                    }
                }
                
                // Use frame buffering for proper frame size handling
                OperationContext bufferContext;
                bufferContext.targetAudioFrameSize = encoderCtx->frame_size;
                bufferContext.audioEncoder = encoderCtx;
                
                std::vector<AVFrame*> bufferedFrames = bufferAudioFrame(&bufferContext, resampledFrame);
                
                // Encode all buffered frames
                for (AVFrame* bufferedFrame : bufferedFrames) {
                    if (avcodec_send_frame(encoderCtx, bufferedFrame) < 0) {
                        Logger::instance().warn("Failed to send frame to audio encoder");
                        continue;
                    }
                    
                    // Only free the frame if it's not the original input frame
                    if (bufferedFrame != resampledFrame && bufferedFrame != decodedFrame) {
                        av_frame_free(&bufferedFrame);
                    }
                }
                
                AVPacket* encodedPacket = av_packet_alloc();
                while (avcodec_receive_packet(encoderCtx, encodedPacket) >= 0) {
                    av_packet_rescale_ts(encodedPacket, encoderCtx->time_base, outputStream->time_base);
                    encodedPacket->stream_index = outputStream->index;
                    
                    if (av_interleaved_write_frame(outputFormatCtx, encodedPacket) < 0) {
                        Logger::instance().error("Failed to write encoded audio frame");
                        av_packet_free(&encodedPacket);
                        if (resampledFrame != decodedFrame) av_frame_free(&resampledFrame);
                        av_frame_free(&decodedFrame);
                        avcodec_free_context(&decoderCtx);
                        av_packet_free(&packet);
                        avcodec_free_context(&encoderCtx);
                        avformat_close_input(&inputFormatCtx);
                        av_write_trailer(outputFormatCtx);
                        if (!(outputFormatCtx->oformat->flags & AVFMT_NOFILE)) {
                            avio_closep(&outputFormatCtx->pb);
                        }
                        avformat_free_context(outputFormatCtx);
                        return makeUnexpected(FFmpegError::IOError);
                    }
                    av_packet_unref(encodedPacket);
                }
                av_packet_free(&encodedPacket);
                
                if (resampledFrame != decodedFrame) av_frame_free(&resampledFrame);
            }
            av_frame_free(&decodedFrame);
            avcodec_free_context(&decoderCtx);
        }
        
        av_packet_unref(packet);
    }
    
    // Write trailer and cleanup
    av_write_trailer(outputFormatCtx);
    av_packet_free(&packet);
    avcodec_free_context(&encoderCtx);
    avformat_close_input(&inputFormatCtx);
    if (!(outputFormatCtx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&outputFormatCtx->pb);
    }
    avformat_free_context(outputFormatCtx);
    
    return outputPath;
}

bool FFmpegWrapper::saveFrameAsImage(AVFrame* frame, const QString& outputPath, const QString& format) {
    if (!frame || outputPath.isEmpty()) {
        return false;
    }
    
    // Create output format context
    AVFormatContext* formatContext = nullptr;
    const char* formatName = format.toLower() == "png" ? "image2" : "mjpeg";
    
    if (avformat_alloc_output_context2(&formatContext, nullptr, formatName, outputPath.toUtf8().constData()) < 0) {
        Logger::instance().error("Failed to allocate output context for image: {}", outputPath.toStdString());
        return false;
    }
    
    // Find encoder
    const AVCodec* codec = nullptr;
    AVCodecID codecId = AV_CODEC_ID_NONE;
    
    if (format.toLower() == "png") {
        codecId = AV_CODEC_ID_PNG;
    } else if (format.toLower() == "jpg" || format.toLower() == "jpeg") {
        codecId = AV_CODEC_ID_MJPEG;
    } else {
        codecId = AV_CODEC_ID_MJPEG; // Default to JPEG
    }
    
    codec = avcodec_find_encoder(codecId);
    if (!codec) {
        Logger::instance().error("Encoder not found for format: {}", format.toStdString());
        avformat_free_context(formatContext);
        return false;
    }
    
    // Create stream
    AVStream* stream = avformat_new_stream(formatContext, codec);
    if (!stream) {
        Logger::instance().error("Failed to create stream for image output");
        avformat_free_context(formatContext);
        return false;
    }
    
    // Create encoder context
    AVCodecContext* codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        Logger::instance().error("Failed to allocate codec context");
        avformat_free_context(formatContext);
        return false;
    }
    
    // Set encoder parameters
    codecContext->codec_id = codecId;
    codecContext->codec_type = AVMEDIA_TYPE_VIDEO;
    
    // Set pixel format based on codec
    if (codecId == AV_CODEC_ID_MJPEG) {
        codecContext->pix_fmt = AV_PIX_FMT_YUVJ420P;  // MJPEG prefers YUVJ420P
    } else if (codecId == AV_CODEC_ID_PNG) {
        codecContext->pix_fmt = AV_PIX_FMT_RGB24;     // PNG supports RGB24
    } else {
        codecContext->pix_fmt = AV_PIX_FMT_YUV420P;   // Default fallback
    }
    
    codecContext->width = frame->width;
    codecContext->height = frame->height;
    codecContext->time_base = {1, 1};
    if (formatContext->oformat->flags & AVFMT_GLOBALHEADER)
        codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    
    // Open encoder
    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        Logger::instance().error("Failed to open encoder");
        avcodec_free_context(&codecContext);
        avformat_free_context(formatContext);
        return false;
    }
    
    // Copy codec parameters to stream
    if (avcodec_parameters_from_context(stream->codecpar, codecContext) < 0) {
        Logger::instance().error("Failed to copy codec parameters");
        avcodec_free_context(&codecContext);
        avformat_free_context(formatContext);
        return false;
    }
    
    // Open output file
    if (avio_open(&formatContext->pb, outputPath.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
        Logger::instance().error("Failed to open output file: {}", outputPath.toStdString());
        avcodec_free_context(&codecContext);
        avformat_free_context(formatContext);
        return false;
    }
    
    // Write header
    if (avformat_write_header(formatContext, nullptr) < 0) {
        Logger::instance().error("Failed to write header");
        avio_closep(&formatContext->pb);
        avcodec_free_context(&codecContext);
        avformat_free_context(formatContext);
        return false;
    }
    
    // Encode frame
    frame->pts = 0;
    
    if (avcodec_send_frame(codecContext, frame) < 0) {
        Logger::instance().error("Failed to send frame to encoder");
        av_write_trailer(formatContext);
        avio_closep(&formatContext->pb);
        avcodec_free_context(&codecContext);
        avformat_free_context(formatContext);
        return false;
    }
    
    AVPacket* packet = av_packet_alloc();
    bool success = false;
    
    if (avcodec_receive_packet(codecContext, packet) == 0) {
        packet->stream_index = stream->index;
        
        if (av_interleaved_write_frame(formatContext, packet) == 0) {
            success = true;
        } else {
            Logger::instance().error("Failed to write frame");
        }
        av_packet_unref(packet);
    }
    
    av_packet_free(&packet);
    
    // Write trailer
    av_write_trailer(formatContext);
    
    // Cleanup
    avio_closep(&formatContext->pb);
    avcodec_free_context(&codecContext);
    avformat_free_context(formatContext);
    
    return success;
}

Expected<AVCodecContext*, FFmpegError> FFmpegWrapper::createVideoEncoder(
    const ConversionOptions& options, 
    const VideoStreamInfo& videoInfo) {
    
    // Find encoder based on output format
    const AVCodec* encoder = nullptr;
    QString format = options.containerFormat.toLower();
    
    if (format == "h264" || format == "mp4") {
        encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    } else if (format == "h265" || format == "hevc") {
        encoder = avcodec_find_encoder(AV_CODEC_ID_HEVC);
    } else if (format == "vp9") {
        encoder = avcodec_find_encoder(AV_CODEC_ID_VP9);
    } else if (format == "av1") {
        encoder = avcodec_find_encoder(AV_CODEC_ID_AV1);
    } else {
        // Default to H.264
        encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    }
    
    if (!encoder) {
        Logger::instance().error("Failed to find video encoder for format: {}", format.toStdString());
        return makeUnexpected(FFmpegError::UnsupportedFormat);
    }
    
    // Create codec context
    AVCodecContext* codecCtx = avcodec_alloc_context3(encoder);
    if (!codecCtx) {
        Logger::instance().error("Failed to allocate codec context");
        return makeUnexpected(FFmpegError::AllocationFailed);
    }
    
    // Configure encoder parameters
    codecCtx->codec_id = encoder->id;
    codecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    codecCtx->width = options.width > 0 ? options.width : videoInfo.width;
    codecCtx->height = options.height > 0 ? options.height : videoInfo.height;
    codecCtx->time_base = {1, 30}; // 30 FPS default
    codecCtx->framerate = {30, 1};
    codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    
    // Set bitrate
    if (options.videoBitrate > 0) {
        codecCtx->bit_rate = options.videoBitrate;
    } else {
        // Calculate reasonable bitrate based on resolution
        codecCtx->bit_rate = codecCtx->width * codecCtx->height * 2; // 2 bits per pixel
    }
    
    // Quality settings
    codecCtx->gop_size = 30; // GOP size
    codecCtx->max_b_frames = 1;
    
    // Set codec-specific options
    if (codecCtx->codec_id == AV_CODEC_ID_H264) {
        av_opt_set(codecCtx->priv_data, "preset", "medium", 0);
        av_opt_set(codecCtx->priv_data, "crf", "23", 0);
    }
    
    // Open codec
    if (avcodec_open2(codecCtx, encoder, nullptr) < 0) {
        Logger::instance().error("Failed to open video encoder");
        avcodec_free_context(&codecCtx);
        return makeUnexpected(FFmpegError::EncodingFailed);
    }
    
    Logger::instance().debug("Created video encoder: {} ({}x{})", 
                           encoder->name, codecCtx->width, codecCtx->height);
    
    return codecCtx;
}

Expected<AVFilterGraph*, FFmpegError> FFmpegWrapper::createFilterGraph(
    const QString& filterSpec,
    const VideoStreamInfo& videoInfo,
    const ConversionOptions& options) {
    
    AVFilterGraph* filterGraph = avfilter_graph_alloc();
    if (!filterGraph) {
        Logger::instance().error("Failed to allocate filter graph");
        return makeUnexpected(FFmpegError::AllocationFailed);
    }
    
    // Create buffer source
    const AVFilter* bufferSrc = avfilter_get_by_name("buffer");
    const AVFilter* bufferSink = avfilter_get_by_name("buffersink");
    
    if (!bufferSrc || !bufferSink) {
        Logger::instance().error("Failed to find buffer filters");
        avfilter_graph_free(&filterGraph);
        return makeUnexpected(FFmpegError::FilteringFailed);
    }
    
    // Configure buffer source
    QString args = QString("video_size=%1x%2:pix_fmt=%3:time_base=1/30:pixel_aspect=1/1")
                       .arg(videoInfo.width)
                       .arg(videoInfo.height)
                       .arg(static_cast<int>(AV_PIX_FMT_YUV420P));
    
    AVFilterContext* bufferSrcCtx = nullptr;
    if (avfilter_graph_create_filter(&bufferSrcCtx, bufferSrc, "in", 
                                     args.toLocal8Bit().data(), nullptr, filterGraph) < 0) {
        Logger::instance().error("Failed to create buffer source");
        avfilter_graph_free(&filterGraph);
        return makeUnexpected(FFmpegError::FilteringFailed);
    }
    
    // Create buffer sink
    AVFilterContext* bufferSinkCtx = nullptr;
    if (avfilter_graph_create_filter(&bufferSinkCtx, bufferSink, "out", 
                                     nullptr, nullptr, filterGraph) < 0) {
        Logger::instance().error("Failed to create buffer sink");
        avfilter_graph_free(&filterGraph);
        return makeUnexpected(FFmpegError::FilteringFailed);
    }
    
    // Set pixel formats for sink
    enum AVPixelFormat pixFmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
    if (av_opt_set_int_list(bufferSinkCtx, "pix_fmts", pixFmts, 
                           AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN) < 0) {
        Logger::instance().error("Failed to set pixel formats");
        avfilter_graph_free(&filterGraph);
        return makeUnexpected(FFmpegError::FilteringFailed);
    }
    
    // Parse and apply filter string
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    
    outputs->name = av_strdup("in");
    outputs->filter_ctx = bufferSrcCtx;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    
    inputs->name = av_strdup("out");
    inputs->filter_ctx = bufferSinkCtx;
    inputs->pad_idx = 0;
    inputs->next = nullptr;
    
    QString filter = filterSpec.isEmpty() ? "null" : filterSpec;
    
    if (avfilter_graph_parse_ptr(filterGraph, filter.toLocal8Bit().data(),
                                &inputs, &outputs, nullptr) < 0) {
        Logger::instance().error("Failed to parse filter graph");
        avfilter_inout_free(&outputs);
        avfilter_inout_free(&inputs);
        avfilter_graph_free(&filterGraph);
        return makeUnexpected(FFmpegError::FilteringFailed);
    }
    
    // Configure the graph
    if (avfilter_graph_config(filterGraph, nullptr) < 0) {
        Logger::instance().error("Failed to configure filter graph");
        avfilter_inout_free(&outputs);
        avfilter_inout_free(&inputs);
        avfilter_graph_free(&filterGraph);
        return makeUnexpected(FFmpegError::FilteringFailed);
    }
    
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    
    Logger::instance().debug("Created filter graph with spec: {}", filter.toStdString());
    
    return filterGraph;
}

Expected<bool, FFmpegError> FFmpegWrapper::processFrameThroughFilter(
    AVFrame* inputFrame,
    AVFrame* outputFrame,
    AVFilterGraph* filterGraph) {
    
    if (!inputFrame || !outputFrame || !filterGraph) {
        Logger::instance().error("Invalid parameters for frame filtering");
        return makeUnexpected(FFmpegError::InvalidParameters);
    }
    
    // Get source and sink contexts
    AVFilterContext* sourceCxt = nullptr;
    AVFilterContext* sinkCtx = nullptr;
    
    for (unsigned int i = 0; i < filterGraph->nb_filters; i++) {
        AVFilterContext* ctx = filterGraph->filters[i];
        if (strcmp(ctx->filter->name, "buffer") == 0) {
            sourceCxt = ctx;
        } else if (strcmp(ctx->filter->name, "buffersink") == 0) {
            sinkCtx = ctx;
        }
    }
    
    if (!sourceCxt || !sinkCtx) {
        Logger::instance().error("Failed to find source/sink in filter graph");
        return makeUnexpected(FFmpegError::FilteringFailed);
    }
    
    // Add frame to source
    if (av_buffersrc_add_frame_flags(sourceCxt, inputFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
        Logger::instance().error("Failed to add frame to buffer source");
        return makeUnexpected(FFmpegError::FilteringFailed);
    }
    
    // Get filtered frame from sink
    int ret = av_buffersink_get_frame(sinkCtx, outputFrame);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // No more frames available, not an error
            return false;
        } else {
            Logger::instance().error("Failed to get frame from buffer sink");
            return makeUnexpected(FFmpegError::FilteringFailed);
        }
    }
    
    return true;
}

VideoStreamInfo FFmpegWrapper::extractVideoStreamInfo(AVStream* stream, AVCodecContext* codecContext) {
    VideoStreamInfo info;
    
    if (!stream || !codecContext) {
        Logger::instance().warn("Invalid stream or codec context");
        return info;
    }
    
    info.width = codecContext->width;
    info.height = codecContext->height;
    info.pixelFormat = QString::number(codecContext->pix_fmt);
    info.frameRate = av_q2d(stream->avg_frame_rate);
    info.bitrate = codecContext->bit_rate;
    info.duration = stream->duration != AV_NOPTS_VALUE ? 
                   static_cast<double>(stream->duration) * av_q2d(stream->time_base) : 0.0;
    
    // Get codec information
    if (codecContext->codec) {
        info.codec = QString::fromLatin1(codecContext->codec->name);
    }
    
    // Calculate total frames if possible
    if (info.frameRate > 0 && info.duration > 0) {
        info.frameCount = static_cast<qint64>(info.frameRate * info.duration);
    } else if (stream->nb_frames > 0) {
        info.frameCount = stream->nb_frames;
    }
    
    Logger::instance().debug("Extracted video info: {}x{} @ {:.2f}fps, {} frames, codec: {}", 
                           info.width, info.height, info.frameRate, 
                           info.frameCount, info.codec.toStdString());
    
    return info;
}

} // namespace Murmur