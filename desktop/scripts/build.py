#!/usr/bin/env python3

import os
import sys
import subprocess
import argparse
import shutil
import platform
from pathlib import Path

class BuildConfiguration:
    def __init__(self, target_os=None, arch=None, build_type="Release"):
        self.target_os = target_os or platform.system().lower()
        # Handle special case for universal build
        if arch == "universal":
            self.arch = "universal"
        else:
            self.arch = arch or ("armv8" if platform.machine() == "arm64" else "x86_64")
        self.build_type = build_type
        self.root_dir = Path(__file__).parent.parent
        self.build_dir = self.root_dir / "build" / f"{self.target_os}-{self.arch}-{build_type}"
        self.conan_profile = f"{self.target_os}-{self.arch}"
        
    def setup_environment(self):
        """Set up build environment variables"""
        env = os.environ.copy()
        
        if self.target_os == "windows":
            env["CMAKE_GENERATOR"] = "Visual Studio 17 2022"
            env["CMAKE_GENERATOR_PLATFORM"] = "x64" if self.arch == "x86_64" else "ARM64"
        elif self.target_os == "macos":
            pass
        elif self.target_os == "linux":
            env["CC"] = "gcc"
            env["CXX"] = "g++"
            
        return env

class BuildManager:
    def __init__(self, config: BuildConfiguration):
        self.config = config
        self.env = config.setup_environment()
        
    def run_command(self, cmd, cwd=None, check=True, timeout=3600):
        print(f"Running: {' '.join(cmd)}")
        try:
            result = subprocess.run(
                cmd, 
                cwd=cwd or self.config.root_dir,
                env=self.env,
                check=check,
                capture_output=True,
                text=True,
                timeout=timeout
            )
            if result.stdout:
                print(result.stdout)
            return result
        except subprocess.TimeoutExpired as e:
            print(f"Command timed out after {timeout} seconds: {' '.join(cmd)}")
            raise
        except subprocess.CalledProcessError as e:
            print(f"Command failed: {e}")
            if e.stderr:
                print(f"Error: {e.stderr}")
            raise
    
    def clean_build(self):
        """Clean previous build artifacts"""
        print("Cleaning build directory...")
        if self.config.build_dir.exists():
            shutil.rmtree(self.config.build_dir)
        self.config.build_dir.mkdir(parents=True, exist_ok=True)
    
    def install_dependencies(self):
        """Install dependencies using Conan"""
        print("Installing dependencies with Conan...")
        print(f"Target OS: {self.config.target_os}")
        print(f"Architecture: {self.config.arch}")
        print(f"Build Type: {self.config.build_type}")
        
        # Create conan profile if needed
        profile_path = Path.home() / ".conan2" / "profiles" / self.config.conan_profile
        if not profile_path.exists():
            self.create_conan_profile()
        else:
            print(f"Using existing profile: {profile_path}")
        
        # Install dependencies
        conan_cmd = [
            "conan", "install", ".",
            f"--profile={self.config.conan_profile}",
            f"--build=missing",
            f"--settings=build_type={self.config.build_type}",
            f"--output-folder={self.config.build_dir}"
        ]
        
        if self.config.build_type == "Release":
            conan_cmd.extend([
                "--options=*:shared=False",
                "--options=*:static_runtime=True"
            ])
        
        self.run_command(conan_cmd)
    
    def create_conan_profile(self):
        print(f"Creating Conan profile: {self.config.conan_profile}")

        # Default settings
        os_name = "Macos" if self.config.target_os == "macos" else self.config.target_os.title()
        settings = {
            "os": os_name,
            "arch": "x86_64" if self.config.arch == "universal" else self.config.arch,  # Use x86_64 for universal
            "build_type": self.config.build_type,
            "compiler": "gcc",
            "compiler.version": "11",
            "compiler.libcxx": "libstdc++11",
            "compiler.cppstd": "20"
        }

        if self.config.target_os == "macos":
            settings["compiler"] = "apple-clang"
            settings["compiler.version"] = "14"
            settings["compiler.libcxx"] = "libc++"
        elif self.config.target_os == "windows":
            settings["compiler"] = "msvc"
            settings["compiler.version"] = "193"
            settings["compiler.runtime"] = "static" if self.config.build_type == "Release" else "dynamic"
            del settings["compiler.libcxx"]
            del settings["compiler.cppstd"]

        profile_content = "[settings]\n"
        for key, value in settings.items():
            profile_content += f"{key}={value}\n"
        
        profile_content += "\n[buildenv]\n"

        profile_path = Path.home() / ".conan2" / "profiles" / self.config.conan_profile
        profile_path.parent.mkdir(parents=True, exist_ok=True)
        profile_path.write_text(profile_content)

    def configure_cmake(self):
        """Configure CMake build"""
        print("Configuring CMake...")
        
        cmake_cmd = [
            "cmake",
            "-S", str(self.config.root_dir),
            "-B", str(self.config.build_dir),
            f"-DCMAKE_BUILD_TYPE={self.config.build_type}",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            f"-DCMAKE_TOOLCHAIN_FILE={self.config.build_dir}/conan_toolchain.cmake"
        ]
        
        if self.config.build_type == "Release":
            cmake_cmd.extend([
                # "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON", weird stuff happens...
                "-DENABLE_TESTING=OFF"
            ])
        
        if self.config.target_os == "windows":
            cmake_cmd.extend([
                "-G", "Visual Studio 17 2022",
                "-A", "x64" if self.config.arch == "x86_64" else "ARM64"
            ])
        elif self.config.target_os == "macos":
            cmake_cmd.extend([
                "-G", "Xcode",
                f"-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0"
            ])
        
        self.run_command(cmake_cmd)
    
    def build_application(self):
        """Build the application"""
        print("Building application...")
        
        build_cmd = [
            "cmake", "--build", str(self.config.build_dir),
            "--config", self.config.build_type,
            "--parallel"
        ]
        
        self.run_command(build_cmd)
    
    def sign_application(self, signing_config):
        """Sign the application for distribution"""
        if not signing_config:
            print("No signing configuration provided, skipping signing")
            return
        
        print("Signing application...")
        
        if self.config.target_os == "darwin":
            self.sign_macos_app(signing_config)
        elif self.config.target_os == "windows":
            self.sign_windows_app(signing_config)
    
    def sign_macos_app(self, signing_config):
        """Sign macOS application and prepare for notarization"""
        app_path = self.config.build_dir / "src" / "MurmurDesktopApp.app"
        
        codesign_cmd = [
            "codesign", "--sign", signing_config["identity"],
            "--entitlements", str(self.config.root_dir / "packaging" / "macos" / "entitlements.plist"),
            "--options", "runtime",
            "--timestamp",
            "--deep", "--force",
            str(app_path)
        ]
        
        self.run_command(codesign_cmd)
        
        dmg_path = self.config.build_dir / f"MurmurDesktop-{self.config.arch}.dmg"
        create_dmg_cmd = [
            "hdiutil", "create", "-volname", "Murmur Desktop",
            "-srcfolder", str(app_path),
            "-ov", "-format", "UDZO",
            str(dmg_path)
        ]
        
        self.run_command(create_dmg_cmd)
        
        codesign_dmg_cmd = [
            "codesign", "--sign", signing_config["identity"],
            "--timestamp",
            str(dmg_path)
        ]
        
        self.run_command(codesign_dmg_cmd)
        
        if "apple_id" in signing_config and "app_password" in signing_config:
            self.notarize_macos_app(dmg_path, signing_config)
    
    def notarize_macos_app(self, dmg_path, signing_config):
        """Notarize macOS application with Apple"""
        print("Notarizing with Apple...")
        
        notarize_cmd = [
            "xcrun", "notarytool", "submit", str(dmg_path),
            "--apple-id", signing_config["apple_id"],
            "--password", signing_config["app_password"],
            "--team-id", signing_config["team_id"],
            "--wait"
        ]
        
        self.run_command(notarize_cmd)
        
        staple_cmd = ["xcrun", "stapler", "staple", str(dmg_path)]
        self.run_command(staple_cmd)
    
    def sign_windows_app(self, signing_config):
        """Sign Windows application"""
        exe_path = self.config.build_dir / "src" / "MurmurDesktop.exe"
        
        signtool_cmd = [
            "signtool", "sign",
            "/f", signing_config["certificate_path"],
            "/p", signing_config["certificate_password"],
            "/t", "http://timestamp.digicert.com",
            "/v",
            str(exe_path)
        ]
        
        self.run_command(signtool_cmd)
    
    def create_package(self):
        """Create distribution package"""
        print("Creating distribution package...")
        
        if self.config.target_os == "windows":
            self.create_windows_installer()
        elif self.config.target_os == "linux":
            self.create_linux_packages()
        # macOS DMG is created during signing
    
    def create_windows_installer(self):
        """Create Windows NSIS installer"""
        nsis_script = self.config.root_dir / "packaging" / "windows" / "murmur.nsi"
        
        makensis_cmd = [
            "makensis",
            f"/DVERSION={self.get_version()}",
            str(nsis_script)
        ]
        
        self.run_command(makensis_cmd, cwd=self.config.build_dir / "src")
    
    def create_linux_packages(self):
        """Create Linux packages (DEB, RPM, AppImage)"""
        # Use CPack for DEB and RPM
        cpack_cmd = [
            "cpack",
            "-G", "DEB;RPM",
            "-B", str(self.config.build_dir / "packages")
        ]
        
        self.run_command(cpack_cmd, cwd=self.config.build_dir)
        
        # Create AppImage if linuxdeploy is available
        if shutil.which("linuxdeploy"):
            self.create_appimage()
    
    def create_appimage(self):
        """Create AppImage for Linux"""
        print("Creating AppImage...")
        
        appdir = self.config.build_dir / "MurmurDesktop.AppDir"
        exe_path = self.config.build_dir / "src" / "MurmurDesktop"
        
        appdir.mkdir(exist_ok=True)
        (appdir / "usr" / "bin").mkdir(parents=True, exist_ok=True)
        (appdir / "usr" / "share" / "applications").mkdir(parents=True, exist_ok=True)
        (appdir / "usr" / "share" / "icons" / "hicolor" / "256x256" / "apps").mkdir(parents=True, exist_ok=True)
        
        shutil.copy2(exe_path, appdir / "usr" / "bin")
        shutil.copy2(
            self.config.root_dir / "packaging" / "linux" / "murmur-desktop.desktop",
            appdir / "usr" / "share" / "applications"
        )
        shutil.copy2(
            self.config.root_dir / "resources" / "icons" / "app.png",
            appdir / "usr" / "share" / "icons" / "hicolor" / "256x256" / "apps" / "murmur-desktop.png"
        )
        
        linuxdeploy_cmd = [
            "linuxdeploy",
            "--appdir", str(appdir),
            "--output", "appimage"
        ]
        
        self.run_command(linuxdeploy_cmd, cwd=self.config.build_dir)
    
    def get_version(self):
        """Get version from CMakeLists.txt"""
        cmake_file = self.config.root_dir / "CMakeLists.txt"
        content = cmake_file.read_text()
        
        for line in content.split('\n'):
            if 'project(' in line and 'VERSION' in line:
                version_part = line.split('VERSION')[1].split()[0]
                return version_part.strip()
        
        return "1.0.0"
    
    def run_tests(self):
        """Run test suite"""
        if self.config.build_type == "Release":
            print("Skipping tests in Release build")
            return
        
        print("Running tests...")
        
        test_cmd = [
            "ctest",
            "--test-dir", str(self.config.build_dir),
            "--output-on-failure",
            "--parallel"
        ]
        
        try:
            self.run_command(test_cmd)
            print("All tests passed!")
        except subprocess.CalledProcessError:
            print("Some tests failed!")
            raise

def main():
    parser = argparse.ArgumentParser(description="Build Murmur Desktop for production")
    parser.add_argument("--target-os", choices=["windows", "macos", "linux"], 
                       help="Target operating system")
    parser.add_argument("--arch", choices=["x86_64", "armv8", "universal"], 
                       help="Target architecture")
    parser.add_argument("--build-type", choices=["Debug", "Release"], default="Release",
                       help="Build type")
    parser.add_argument("--clean", action="store_true", 
                       help="Clean build directory before building")
    parser.add_argument("--test", action="store_true", 
                       help="Run tests after building")
    parser.add_argument("--sign", 
                       help="Path to signing configuration JSON file")
    parser.add_argument("--package", action="store_true", 
                       help="Create distribution package")
    
    args = parser.parse_args()
    
    config = BuildConfiguration(args.target_os, args.arch, args.build_type)
    builder = BuildManager(config)
    
    signing_config = None
    if args.sign:
        import json
        with open(args.sign) as f:
            signing_config = json.load(f)
    
    try:
        if args.clean:
            builder.clean_build()
        
        builder.install_dependencies()
        builder.configure_cmake()
        builder.build_application()
        
        if args.test:
            builder.run_tests()
        
        if signing_config:
            builder.sign_application(signing_config)
        
        if args.package:
            builder.create_package()
        
        print("Build completed successfully!")
        
    except Exception as e:
        print(f"Build failed: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()