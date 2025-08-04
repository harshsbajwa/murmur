# Murmur Desktop - Build Guide

## Prerequisites

### Common Requirements
- **CMake** 3.25 or later
- **Python** 3.8+ (for build scripts and Conan)
- **Conan** 2.0+ package manager
- **Qt** 6.8.3 or later
- **Git** (for version control and CI/CD)

### Platform-Specific Requirements

#### Windows
- **Visual Studio 2022** with C++ workload
- **NSIS** 3.08+ (for installer creation)
- **Windows SDK** 10.0.22000 or later
- **Code signing certificate** (for production releases)

#### macOS
- **Xcode** 14+ with Command Line Tools
- **macOS** 12.0+ (for universal binary support)
- **Apple Developer** account (for code signing and notarization)
- **create-dmg** tool (`brew install create-dmg`)

#### Linux
- **GCC** 11+ or **Clang** 14+
- **Qt development packages**
- **System libraries**: ALSA/PulseAudio, X11, OpenGL
- **Package tools**: dpkg, rpm, linuxdeploy (for AppImage)

## Quick Start

### 1. Clone and Setup
```bash
git clone https://github.com/harshsbajwa/murmur/desktop.git
cd desktop
```

### 2. Install Dependencies
```bash
pip install conan>=2.0
conan profile detect --force
```

### 3. Build for Your Platform
```bash
# Development build
python scripts/build.py --clean --test

# Production build
python scripts/build.py --build-type Release --clean --package
```
