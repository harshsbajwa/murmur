from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps, cmake_layout

class MurmurDesktopConan(ConanFile):
    name = "murmur-desktop"
    version = "1.0.0"
    
    settings = "os", "compiler", "build_type", "arch"

    def requirements(self):
        self.requires("libx265/3.4", options={"assembly": False})
        # self.requires("qt/6.8.3")
        self.requires("libtorrent/2.0.10")
        self.requires("ffmpeg/7.1.1")
        self.requires("whisper-cpp/1.7.5")
        self.requires("sqlite3/3.49.1")
        self.requires("spdlog/1.15.3")
        
    def layout(self):
        cmake_layout(self)
        
    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()