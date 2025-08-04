# Changelog

## [1.0.0] - 2025-01-31

### Added
- **Core Features**
  - P2P video file sharing using BitTorrent protocol
  - AI-powered video transcription using Whisper.cpp
  - Hardware-accelerated video processing with FFmpeg
  - Cross-platform support (Windows, macOS, Linux)
  - Modern Qt 6.8+ interface with QML
  
- **Media Processing**
  - Support for multiple video formats (MP4, AVI, MKV, MOV, WebM, etc.)
  - Hardware acceleration (VideoToolbox on macOS, VAAPI/VDPAU on Linux)
  - Streaming video conversion
  - Audio extraction for transcription
  
- **Transcription Engine**
  - Local AI transcription using Whisper models
  - Multiple language support
  - Configurable model sizes (base, small, medium, large)
  - Metal acceleration on Apple Silicon
  - Real-time transcription display
  
- **User Interface**
  - Intuitive file management with drag-and-drop
  - Integrated video player with playback controls
  - Transcription viewer with search functionality
  - Progress tracking for all operations
  - Dark/light theme support
  
- **Storage & Database**
  - SQLite database for metadata storage
  - File caching and memory management
  - Automatic cleanup of temporary files
  - Configurable storage locations

### Technical Details
- **Dependencies**: Qt 6.8+, FFmpeg 7.1.1, Whisper.cpp 1.7.5, LibTorrent 2.0.10
- **Build System**: CMake with Conan package management
- **Supported Platforms**: 
  - Windows 10+ (64-bit)
  - macOS 11.0+ (Intel and Apple Silicon)
  - Linux (Ubuntu 20.04+, Fedora 35+)
- **Languages**: C++20, QML/JavaScript
- **Architecture**: Modular design with separate core and UI layers

[1.0.0]: https://github.com/harshsbajwa/murmur/desktop/releases/tag/v1.0.0