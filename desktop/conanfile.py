from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps, cmake_layout
from conan.tools.system import package_manager

class MurmurDesktopConan(ConanFile):
    name = "murmur-desktop"
    version = "1.0.0"
    
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "with_gui": [True, False],
        "enable_tests": [True, False],
        "static_runtime": [True, False]
    }
    default_options = {
        "shared": False,
        "with_gui": True,
        "enable_tests": False,
        "static_runtime": True,
        # FFmpeg options for production
        "ffmpeg/*:shared": False,
        "ffmpeg/*:with_programs": False,
        "ffmpeg/*:with_ssl": "openssl",
        "ffmpeg/*:with_zlib": True,
        "ffmpeg/*:with_bzip2": True,
        "ffmpeg/*:with_lzma": True,
        # LibTorrent options
        "libtorrent/*:shared": False,
        "libtorrent/*:with_deprecated_functions": False,
        # SQLite options
        "sqlite3/*:shared": False,
        # OpenSSL options
        "openssl/*:shared": False,
        # Whisper.cpp options
        "whisper-cpp/*:shared": False,
        "whisper-cpp/*:with_metal": True,  # macOS Metal acceleration
        "whisper-cpp/*:with_cuda": False,  # Disable CUDA for compatibility
        "whisper-cpp/*:with_openvino": False,
    }

    def requirements(self):
        # Core multimedia dependencies
        self.requires("ffmpeg/7.1.1")
        self.requires("libx265/3.4")
        
        # Networking and P2P  
        self.requires("libtorrent/2.0.10")
        self.requires("openssl/[>=1.1 <4]")
        
        # Transcription
        self.requires("whisper-cpp/1.7.5")
        
        # Database and logging
        self.requires("sqlite3/3.49.1")
        self.requires("spdlog/1.15.3")
        
        # Compression libraries
        self.requires("zlib/1.3.1")
        self.requires("bzip2/1.0.8")
        self.requires("xz_utils/5.4.5")

    def system_requirements(self):
        """Install system dependencies for each platform"""
        if self.settings.os == "Linux":
            package_manager.Apt(self).install([
                "libasound2-dev", "libpulse-dev", "libjack-dev",  # Audio
                "libxkbcommon-dev", "libxcb-xinerama0-dev",       # GUI
                "libglu1-mesa-dev", "libdrm-dev",                 # OpenGL
                "libva-dev", "libvdpau-dev"                       # Hardware acceleration
            ])
        elif self.settings.os == "Windows":
            # Windows dependencies are typically bundled
            pass

    def configure(self):
        """Configure options based on build settings"""
        if self.settings.build_type == "Release":
            self.options.static_runtime = True
            self.options.shared = False
        
        # Platform-specific configurations
        if self.settings.os == "Windows":
            self.options["ffmpeg/*"].with_cuda = False  # Avoid driver dependencies
        elif self.settings.os == "Macos":
            self.options["whisper-cpp/*"].with_metal = True
            self.options["ffmpeg/*"].with_videotoolbox = True
        elif self.settings.os == "Linux":
            self.options["ffmpeg/*"].with_vaapi = True
            self.options["ffmpeg/*"].with_vdpau = True
        
    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        
        tc = CMakeToolchain(self)
        # Production build flags
        if self.settings.build_type == "Release":
            # tc.variables["CMAKE_INTERPROCEDURAL_OPTIMIZATION"] = True  # weird stuff happens...
            tc.variables["CMAKE_BUILD_TYPE"] = "Release"
            tc.variables["CMAKE_CXX_FLAGS_RELEASE"] = "-O3 -DNDEBUG" # -flto
            
        # Static linking for production
        if self.options.static_runtime:
            tc.variables["CMAKE_MSVC_RUNTIME_LIBRARY"] = "MultiThreaded"
            
        # Security flags
        tc.variables["ENABLE_SECURITY_FLAGS"] = True
        tc.variables["ENABLE_HARDENING"] = True
        
        tc.generate()