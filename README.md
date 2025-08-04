# README.md

This file provides guidance when working with code in this repository.

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
  - Transcription engine (whisper.cpp)
  - Storage manager (SQLite)
- **Architecture**: Modular design with clear separation between core engine and UI
- **Error Handling**: Comprehensive error management using Expected<T, E> pattern
- **Platform Support**: macOS, Windows, and Linux (with platform-specific optimizations)

## Development Commands

### Web Application

#### Laravel Backend

```bash
# Start development environment (all services)
composer dev

# Development with SSR
composer dev:ssr

# Individual services
php artisan serve           # Laravel server (port 8000)
php artisan queue:listen    # Background jobs
php artisan pail           # Real-time logs

# Testing
composer test              # Run PHPUnit tests
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

#### Error Handling

- Uses `Expected<T, E>` pattern (similar to C++23's std::expected)
- Comprehensive error types for each subsystem
- Qt signal integration for error propagation to UI
- Graceful degradation with user-friendly error messages

## Technology Stack

### Web Application

- **Backend**: Laravel 12, PHP 8.3+
- **Frontend**: React 19, TypeScript 5.4+, TailwindCSS 4
- **Libraries**: WebTorrent, FFmpeg WASM, Hugging Face Transformers
- **Storage**: IndexedDB, SQLite

### Desktop Application

- **Framework**: Qt 6.8+ (found locally at ~/Qt)
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

## Development Status

### Web Application

- Fully functional with all core features implemented
- Active development focused on UI improvements and performance optimization

### Desktop Application

- **Implemented Features**:

  - Basic application structure and Qt integration
  - Torrent engine with libtorrent integration
  - Media pipeline with FFmpeg integration
  - Transcription engine with whisper.cpp integration
  - Storage manager with SQLite integration
  - QML UI with basic video playback and torrent management
- **In Progress**:

  - Vast amount of scattered TODOs scattered althroughout the codebase
  - Extensive testing, including unit tests and integration testing
  - Hardware acceleration testing
  - Comprehensive error handling and recovery
  - Platform-specific optimizations
  - File caching system
  - Model management for transcription
  - UI models for data binding

## Configuration Files

### Web Application

- **Vite**: `vite.config.ts` with Laravel plugin, React, TailwindCSS, and node polyfills
- **TypeScript**: `tsconfig.json` with strict mode, path aliases, and React JSX transform
- **ESLint**: `eslint.config.js` with TypeScript, React, and Prettier integration

### Desktop Application

- **CMake**: `CMakeLists.txt` with Qt integration and security flags
- **Conan**: `conanfile.py` for dependency management
- **Qt**: QML module configuration in src/CMakeLists.txt

## Development Workflow

### Web Application

1. **Starting Development**: Use `composer dev` for full-stack development with hot reload
2. **Adding Features**: Follow existing patterns in dashboard components for video processing features
3. **Testing**: Run both `composer test` for backend and ensure TypeScript compilation with `npm run types`
4. **Code Quality**: Use `npm run lint` and `./vendor/bin/pint` before commits

### Desktop Application

1. **Setup Environment**: Run `python3 scripts/setup.py` to configure build environment
2. **Development Cycle**: Modify C++ code and QML files, then rebuild

# Important Notes

## Core Workflow: Research → Plan → Implement → Validate

**Start every feature with:** "Let me research the codebase and create a plan before implementing."

1. **Research** - Understand existing patterns and architecture
2. **Plan** - Propose approach and verify with you
3. **Implement** - Build with tests and error handling
4. **Validate** - ALWAYS run tests after implementation

## Architecture Principles

- Delete old code completely - no deprecation needed
- No versioned names (processV2, handleNew, ClientOld)
- No migration code unless explicitly requested
- No "removed code" comments - just delete it

**Prefer explicit over implicit:**

- Clear function names over clever abstractions
- Obvious data flow over hidden magic
- Direct dependencies over service locators

## Maximize Efficiency

**Parallel operations:** Run multiple searches, reads, and greps in single messages
**Batch similar work:** Group related file edits together

## Problem Solving

**When stuck:** Stop. The simple solution is usually correct.

**When uncertain:** "Let me ultrathink about this architecture."

**When choosing:** "I see approach A (simple) vs B (flexible). Which do you prefer?"

Your redirects prevent over-engineering. When uncertain about implementation, stop and ask for guidance.

## Testing Strategy

**Match testing approach to code complexity:**

- Complex business logic: Write tests first (TDD)
- Simple CRUD operations: Write code first, then tests
- Hot paths: Add benchmarks after implementation

**Always keep security in mind:** Validate all inputs, use prepared SQL statements.

**Performance rule:** Measure before optimizing. No guessing.

### Web Application

- FFmpeg WASM files are copied to `public/vendor/` during build process via prebuild script
- WebTorrent trackers are configured in `WebTorrentContext.tsx` with fallback options
- Guest users can upload/share torrents using session-based tokens
- Transcriptions are embedded in torrent metadata as comments for P2P distribution
- All video processing happens client-side (FFmpeg, Whisper) to reduce server load

### Desktop Application

- Uses Qt's resource system for QML files and assets
- Hardware acceleration is platform-specific and requires different implementations
- Whisper models are downloaded on demand to reduce initial download size
- Error handling uses Expected<T, E> pattern throughout the codebase
- Security is a primary concern with input validation and sandboxing
