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
    core/security/InfoHashValidator.hpp
    core/security/InfoHashValidator.cpp
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
if(WIN32)
    qt_add_executable(MurmurDesktopApp WIN32
        main.cpp
    )
else()
    qt_add_executable(MurmurDesktopApp
        main.cpp
    )
endif()

# Set application properties
set_target_properties(MurmurDesktopApp PROPERTIES
    OUTPUT_NAME "MurmurDesktop"
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# macOS bundle configuration
if(APPLE)
    set_target_properties(MurmurDesktopApp PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Murmur Desktop"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_GUI_IDENTIFIER "app.murmur.desktop"
        MACOSX_BUNDLE_ICON_FILE "app.icns"
        MACOSX_BUNDLE_INFO_STRING "Murmur Desktop ${PROJECT_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
        MACOSX_BUNDLE_COPYRIGHT "© 2025 Murmur. All rights reserved."
    )
endif()

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
    RESOURCES
        ../resources/images/backward10.svg
        ../resources/images/ff.svg
        ../resources/images/forward10.svg
        ../resources/images/link.svg
        ../resources/images/loop.svg
        ../resources/images/more.svg
        ../resources/images/mute.svg
        ../resources/images/open_new.svg
        ../resources/images/pause_symbol.svg
        ../resources/images/play_symbol.svg
        ../resources/images/rewind.svg
        ../resources/images/settings.svg
        ../resources/images/speaker.svg
        ../resources/images/stop_symbol.svg
        ../resources/images/url.svg
        ../resources/images/volume.svg
        ../resources/images/volume_mute.svg
        ../resources/images/zoom_maximize.svg
        ../resources/images/zoom_minimize.svg
        ../resources/icons/app.png
        ../resources/icons/app.icns
)

target_link_libraries(MurmurDesktopApp PRIVATE
    MurmurUI
    Qt6::QuickControls2
)

# Installation rules
install(TARGETS MurmurDesktopApp
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Platform-specific installation
if(WIN32)
    # Install Qt dependencies
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(WINDEPLOYQT_EXECUTABLE)
        install(CODE "execute_process(COMMAND ${WINDEPLOYQT_EXECUTABLE} --qmldir ${CMAKE_SOURCE_DIR}/src/qml $<TARGET_FILE:MurmurDesktopApp>)")
    endif()
elseif(APPLE)
    # Install macOS bundle and dependencies
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(MACDEPLOYQT_EXECUTABLE)
        install(CODE "execute_process(COMMAND ${MACDEPLOYQT_EXECUTABLE} $<TARGET_BUNDLE_DIR:MurmurDesktopApp> -qmldir=${CMAKE_SOURCE_DIR}/src/qml)")
    endif()
    # Install app icon
    install(FILES ../resources/icons/app.icns DESTINATION MurmurDesktopApp.app/Contents/Resources/)
elseif(UNIX)
    # Install desktop file and icon for Linux
    configure_file(../packaging/linux/murmur-desktop.desktop.in murmur-desktop.desktop @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/murmur-desktop.desktop 
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    install(FILES ../resources/icons/app.png 
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps/
            RENAME murmur-desktop.png)
endif()