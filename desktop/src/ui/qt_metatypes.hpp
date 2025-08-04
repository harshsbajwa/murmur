#pragma once

#include <QtCore/QMetaType>

// Forward declarations
namespace Murmur {
    class TorrentEngine;
    class MediaPipeline;
    class VideoPlayer;
    class StorageManager;
    class WhisperEngine;
    class FileManager;
}

// Register Murmur engine pointer types with Qt's meta-type system
Q_DECLARE_METATYPE(Murmur::TorrentEngine*)
Q_DECLARE_METATYPE(Murmur::MediaPipeline*)
Q_DECLARE_METATYPE(Murmur::VideoPlayer*)
Q_DECLARE_METATYPE(Murmur::StorageManager*)
Q_DECLARE_METATYPE(Murmur::WhisperEngine*)
Q_DECLARE_METATYPE(Murmur::FileManager*)
