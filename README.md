## Project Overview

Murmur is a multimedia application with two main components:

1. **Web Application**: A Laravel + React application for video sharing and processing through WebTorrent
2. **Desktop Application**: A Qt-based cross-platform desktop client written in modern C++20

### Web Application Features

- **Backend**: Laravel 12 API with Inertia.js for full-stack reactivity
- **Frontend**: React 19 with TypeScript, TailwindCSS v4, and Radix UI components
- **Core Features**: Video upload, WebTorrent streaming, FFmpeg conversion, Whisper transcription
- **Storage**: IndexedDB for client-side file management, SQLite for torrent metadata

### Desktop Application Features

- **Framework**: Qt 6.8+ with QML for UI and C++20 for core functionality
- **Core Components**:
  - Torrent engine (libtorrent-rasterbar)
  - Media pipeline (FFmpeg)
  - Transcription engine (Whisper.cpp)
  - Storage manager (SQLite)
- **Architecture**: Modular design with clear separation between core engine and UI
- **Error Handling**: Expected<T, E> pattern
- **Platform Support**: macOS, Windows, and Linux (with platform-specific optimizations)

## Development Commands

### Web Application

#### Laravel Backend

```bash
# Start development environment
composer dev

# Development with SSR
composer dev:ssr

# Individual services
php artisan serve           # Laravel server (port 8000)
php artisan queue:listen    # Background jobs
php artisan pail            # Real-time logs

# Testing
composer test             # Run PHPUnit tests
php artisan test          # Alternative test command

# Code quality
./vendor/bin/pint         # PHP code formatting (Laravel Pint)
```

#### Frontend

```bash
# Development
pnpm run dev               # Vite dev server (port 5173)

# Building
pnpm run build            # Production build
pnpm run prebuild         # Copy FFmpeg assets to public

# Code quality
pnpm run lint             # ESLint with auto-fix
pnpm run types            # TypeScript type checking
pnpm run format           # Prettier formatting
pnpm run format:check     # Check formatting without changes
```

### Desktop Application

```bash
# Setup development environment (from desktop directory)
python3 scripts/setup.py

# Run the application
./MurmurDesktopApp

# Run tests
./MurmurTests
```

## Architecture

### Web Application Architecture

#### Backend Structure

- **Models**: `User` (with torrents relationship), `Torrent` (with user relationship and guest session support)
- **Controllers**: `TorrentController` handles CRUD operations for torrent metadata
- **API Routes**: RESTful endpoints at `/api/torrents` for torrent management
- **Authentication**: Laravel Breeze with email verification, supports guest sessions via session tokens

#### Frontend Architecture

- **Entry Point**: `resources/js/app.tsx` with Inertia.js integration
- **Main Component**: `resources/js/pages/dashboard/index.tsx` - comprehensive video processing interface
- **Context**: `WebTorrentContext` manages WebTorrent client lifecycle and torrent state
- **Custom Hooks**:
  - `use-ffmpeg.ts` - FFmpeg WASM integration for video conversion
  - `use-file-storage.ts` - IndexedDB management for client-side file storage
- **Components**: Organized in `resources/js/components/` with UI components in `/ui` subdirectory

### Desktop Application Architecture

#### Core Engine (C++20)

- **Torrent Engine**: Manages P2P file sharing using libtorrent-rasterbar

  - `TorrentEngine`: Main torrent management with Qt signal integration
  - `TorrentStateModel`: Qt model for UI binding
  - `TorrentSecurityWrapper`: Sandboxed operations for security
- **Media Pipeline**: Handles video processing using FFmpeg

  - `MediaPipeline`: Composable pipeline stages for media processing
  - `FFmpegWrapper`: Secure wrapper around FFmpeg libraries
  - `HardwareAccelerator`: Platform-specific hardware acceleration
- **Transcription Engine**: Speech-to-text using whisper.cpp

  - `WhisperEngine`: Main transcription engine with model management
  - `WhisperWrapper`: Low-level integration with whisper.cpp
  - `ModelDownloader`: Handles downloading ML models on demand
- **Storage Manager**: Data persistence using SQLite

  - `StorageManager`: Database operations with validation
  - `FileManager`: File system operations and caching

#### UI Layer (QML + C++)

- **Controllers**: Bridge between QML UI and C++ core

  - `AppController`: Main application controller
  - `MediaController`: Video playback and conversion
  - `TorrentController`: Torrent operations
  - `TranscriptionController`: ML operations
  - `FileManagerController`: File system operations
- **QML Components**: Modern UI with Qt Quick Controls

  - `main.qml`: Main application window
  - `VideoPlayer.qml`: Custom video player component
  - `TranscriptionViewer.qml`: Displays and navigates transcriptions
  - `TorrentItem.qml`: Torrent list item with controls

## Technology Stack

### Web Application

- **Backend**: Laravel 12, PHP 8.3+
- **Frontend**: React 19, TypeScript 5.4+, TailwindCSS 4
- **Libraries**: WebTorrent, FFmpeg WASM, Hugging Face Transformers
- **Storage**: IndexedDB, SQLite

### Desktop Application

- **Framework**: Qt 6.8+
- **Language**: C++20
- **Build System**: CMake 3.25+, Conan 2.0+
- **Dependencies**:
  - libtorrent-rasterbar 2.0.10
  - FFmpeg 7.1.1
  - whisper.cpp 1.7.5
  - SQLite 3.49.1
  - spdlog 1.15.3
  - libx265 3.4
- **Platform APIs**:
  - macOS: VideoToolbox, Metal
  - Windows: Media Foundation
  - Linux: VA-API
