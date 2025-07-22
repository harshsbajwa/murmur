cmake_minimum_required(VERSION 3.25)
project(MurmurDesktop VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/Qt/6.8.3/macos/lib/cmake")

# Add conan generators to CMAKE_PREFIX_PATH
list(PREPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR}/Debug/generators)
list(PREPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR}/Debug/generators)

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

# Security and optimization flags
if(MSVC)
    add_compile_options(/W4 /GS /DYNAMICBASE /NXCOMPAT)
else()
    add_compile_options(-Wall -Wextra -fstack-protector-strong -D_FORTIFY_SOURCE=2)
endif()

# Enable Qt's MOC, UIC, and RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

qt_standard_project_setup()

# Include directories
include_directories(src)

add_subdirectory(src)
add_subdirectory(tests)
