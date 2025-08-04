cmake_minimum_required(VERSION 3.25)
project(MurmurDesktop 
    VERSION 1.0.0 
    DESCRIPTION "Murmur Desktop - P2P Video Transcription Application"
    LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Production optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON) weird linker crash
    add_compile_definitions(QT_NO_DEBUG_OUTPUT QT_NO_WARNING_OUTPUT)
endif()

# Qt path detection
if(NOT Qt6_DIR)
    if(APPLE)
        list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.8.3/macos/lib/cmake")
    elseif(WIN32)
        list(APPEND CMAKE_PREFIX_PATH "C:/Qt/6.8.3/msvc2022_64/lib/cmake")
        list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}/lib/cmake")
    else()
        list(APPEND CMAKE_PREFIX_PATH "/usr/lib/x86_64-linux-gnu/cmake")
        list(APPEND CMAKE_PREFIX_PATH "$ENV{QTDIR}/lib/cmake")
    endif()
endif()

# Add conan generators to CMAKE_PREFIX_PATH
# list(PREPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR}/Debug/generators)
# list(PREPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR}/Debug/generators)

# Conan dependencies first
find_package(LibtorrentRasterbar REQUIRED)
find_package(ffmpeg REQUIRED)
find_package(libx265 REQUIRED)
find_package(whisper-cpp REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(spdlog REQUIRED)

# Qt
find_package(Qt6 REQUIRED COMPONENTS 
    Core Quick Multimedia Network Concurrent Sql QuickControls2)

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# Platform-specific configurations
if(WIN32)
    # Windows-specific settings
    set(CMAKE_WIN32_EXECUTABLE ON)
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        add_compile_options(/W4 /GS /DYNAMICBASE /NXCOMPAT /guard:cf)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
elseif(APPLE)
    # macOS-specific settings
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build architectures")
    add_compile_options(-Wall -Wextra -fstack-protector-strong -D_FORTIFY_SOURCE=2)
elseif(UNIX)
    # Linux-specific settings
    add_compile_options(-Wall -Wextra -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# Static linking configuration for production
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(WIN32)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .lib .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a .so ${CMAKE_FIND_LIBRARY_SUFFIXES})
    endif()
endif()

# Enable Qt's MOC, UIC, and RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

qt_standard_project_setup()

# Include directories
include_directories(src)

add_subdirectory(src)

# Testing (disabled in production builds by default)
if(ENABLE_TESTING OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation and packaging
include(GNUInstallDirs)
include(InstallRequiredSystemLibraries)

# CPack configuration for packaging
set(CPACK_PACKAGE_NAME "MurmurDesktop")
set(CPACK_PACKAGE_VENDOR "Murmur")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "P2P Video Transcription Application")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

# Platform-specific packaging
if(WIN32)
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "Murmur Desktop")
    set(CPACK_NSIS_PACKAGE_NAME "MurmurDesktop")
    set(CPACK_NSIS_MODIFY_PATH ON)
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Murmur Desktop")
    set(CPACK_DMG_FORMAT "UDZO")
elseif(UNIX)
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_DEBIAN_PACKAGE_SECTION "multimedia")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
    set(CPACK_RPM_PACKAGE_LICENSE "Proprietary")
endif()

include(CPack)
