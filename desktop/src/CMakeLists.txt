# Core library
add_library(MurmurCore STATIC
    # Common utilities
    core/common/Expected.hpp
    core/common/Logger.hpp
    core/common/Logger.cpp
    core/common/Config.hpp
    core/common/Config.cpp
    core/common/RetryManager.hpp
    core/common/RetryManager.cpp
    # Error recovery system
    core/common/ErrorRecovery.hpp
    core/common/ErrorRecovery.cpp
    
    # Security layer
    core/security/InputValidator.hpp
    core/security/InputValidator.cpp
    core/security/SandboxManager.hpp
    core/security/SandboxManager.cpp
    core/security/SecureIPC.hpp
    core/security/SecureIPC.cpp
    
    # Storage
    core/storage/StorageManager.hpp
    core/storage/StorageManager.cpp
    core/storage/FileCache.hpp
    core/storage/FileCache.cpp
    core/storage/MemoryManager.hpp
    core/storage/MemoryManager.cpp
    core/storage/FileManager.hpp
    core/storage/FileManager.cpp
    
    # Torrent engine
    core/torrent/TorrentEngine.hpp
    core/torrent/TorrentEngine.cpp
    core/torrent/LibTorrentWrapper.hpp
    core/torrent/LibTorrentWrapper.cpp
    core/torrent/TorrentStateModel.hpp
    core/torrent/TorrentStateModel.cpp
    core/torrent/TorrentSecurityWrapper.hpp
    core/torrent/TorrentSecurityWrapper.cpp
    
    # Media processing
    core/media/MediaPipeline.hpp
    core/media/MediaPipeline.cpp
    core/media/FFmpegWrapper.hpp
    core/media/FFmpegWrapper.cpp
    core/media/HardwareAccelerator.hpp
    core/media/HardwareAccelerator.cpp
    core/media/PlatformAccelerator.hpp
    core/media/PlatformAccelerator.cpp
    core/media/VideoPlayer.hpp
    core/media/VideoPlayer.cpp
    
    # Transcription engine
    core/transcription/WhisperEngine.hpp
    core/transcription/WhisperEngine.cpp
    core/transcription/WhisperWrapper.hpp
    core/transcription/WhisperWrapper.cpp
    core/transcription/ModelDownloader.hpp
    core/transcription/ModelDownloader.cpp
    core/transcription/TranscriptionFormatter.hpp
    core/transcription/TranscriptionFormatter.cpp
    core/transcription/TranscriptionTypes.hpp
    core/transcription/ModelManager.hpp
    core/transcription/ModelManager.cpp
)

target_include_directories(MurmurCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(MurmurCore PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::Concurrent
    Qt6::Sql
    Qt6::Multimedia
    LibtorrentRasterbar::torrent-rasterbar
    ffmpeg::ffmpeg
    libx265::libx265
    whisper-cpp::whisper-cpp
    SQLite::SQLite3
    spdlog::spdlog
)

# Ensure Conan targets are properly linked with include directories
get_target_property(SPDLOG_INCLUDE_DIRS spdlog::spdlog INTERFACE_INCLUDE_DIRECTORIES)
if(SPDLOG_INCLUDE_DIRS)
    target_include_directories(MurmurCore PUBLIC ${SPDLOG_INCLUDE_DIRS})
endif()

get_target_property(LIBTORRENT_INCLUDE_DIRS LibtorrentRasterbar::torrent-rasterbar INTERFACE_INCLUDE_DIRECTORIES)
if(LIBTORRENT_INCLUDE_DIRS)
    target_include_directories(MurmurCore PUBLIC ${LIBTORRENT_INCLUDE_DIRS})
endif()

get_target_property(FFMPEG_INCLUDE_DIRS ffmpeg::ffmpeg INTERFACE_INCLUDE_DIRECTORIES)
if(FFMPEG_INCLUDE_DIRS)
    target_include_directories(MurmurCore PUBLIC ${FFMPEG_INCLUDE_DIRS})
endif()

get_target_property(WHISPER_INCLUDE_DIRS whisper-cpp::whisper-cpp INTERFACE_INCLUDE_DIRECTORIES)
if(WHISPER_INCLUDE_DIRS)
    target_include_directories(MurmurCore PUBLIC ${WHISPER_INCLUDE_DIRS})
endif()

# UI controllers and models
add_library(MurmurUI STATIC
    ui/controllers/AppController.hpp
    ui/controllers/AppController.cpp
    ui/controllers/MediaController.hpp
    ui/controllers/MediaController.cpp
    ui/controllers/TorrentController.hpp
    ui/controllers/TorrentController.cpp
    ui/controllers/TranscriptionController.hpp
    ui/controllers/TranscriptionController.cpp
    ui/controllers/FileManagerController.hpp
    ui/controllers/FileManagerController.cpp
    
    ui/models/TorrentListModel.hpp
    ui/models/TorrentListModel.cpp
    ui/models/FileListModel.hpp
    ui/models/FileListModel.cpp
    ui/models/TranscriptionModel.hpp
    ui/models/TranscriptionModel.cpp
)

target_link_libraries(MurmurUI PUBLIC
    MurmurCore
    Qt6::Core
    Qt6::Quick
    Qt6::Multimedia
)

# Platform-specific implementations
if(WIN32)
    target_sources(MurmurCore PRIVATE
        platform/windows/WindowsMediaAccelerator.hpp
        platform/windows/WindowsMediaAccelerator.cpp
        platform/windows/WindowsSandbox.hpp
        platform/windows/WindowsSandbox.cpp
        platform/windows/WindowsIPC.hpp
        platform/windows/WindowsIPC.cpp
    )
elseif(APPLE)
    target_sources(MurmurCore PRIVATE
        platform/macos/MacOSMediaAccelerator.hpp
        platform/macos/MacOSMediaAccelerator.mm
        platform/macos/MacOSBridge.mm
        platform/macos/MacOSSandbox.hpp
        platform/macos/MacOSSandbox.cpp
        platform/macos/MacOSIPC.hpp
        platform/macos/MacOSIPC.cpp
    )
    
    # Link macOS frameworks
    target_link_libraries(MurmurCore PUBLIC
        "-framework Metal"
        "-framework VideoToolbox"
        "-framework CoreVideo"
        "-framework CoreFoundation"
        "-framework IOKit"
        "-framework CoreGraphics"
        "-framework ApplicationServices"
    )
else()
    target_sources(MurmurCore PRIVATE
        platform/linux/LinuxMediaAccelerator.hpp
        platform/linux/LinuxMediaAccelerator.cpp
        platform/linux/LinuxSandbox.hpp
        platform/linux/LinuxSandbox.cpp
        platform/linux/LinuxIPC.hpp
        platform/linux/LinuxIPC.cpp
    )
endif()

# Main application
qt_add_executable(MurmurDesktopApp
    main.cpp
)

qt_add_qml_module(MurmurDesktopApp
    URI Murmur
    VERSION 1.0
    QML_FILES
        qml/main.qml
        qml/components/VideoPlayer.qml
        qml/components/TranscriptionViewer.qml
        qml/components/TorrentItem.qml
        qml/components/ErrorDialog.qml
        qml/components/ProgressOverlay.qml
        qml/components/SettingsDialog.qml
        qml/components/MainMenuBar.qml
        qml/components/AboutDialog.qml
    RESOURCE_PREFIX /
)

target_link_libraries(MurmurDesktopApp PRIVATE
    MurmurUI
    Qt6::QuickControls2
)