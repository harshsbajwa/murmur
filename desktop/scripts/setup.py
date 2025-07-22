#!/usr/bin/env python3

import os
import sys
import subprocess
import argparse
import multiprocessing
from pathlib import Path
from shutil import which

def run_command(cmd, cwd=None, check=True):
    """Run a command and optionally check for errors"""
    print(f"\n>>> Running: {' '.join(cmd)}")
    try:
        cwd_path = Path(cwd) if cwd else None
        result = subprocess.run(cmd, cwd=cwd_path, check=check, capture_output=True, text=True)
        if result.stdout:
            print(result.stdout.strip())
        if result.stderr:
            print(result.stderr.strip())
        return result.returncode == 0
    except subprocess.CalledProcessError as e:
        print(f"Command failed: {e}")
        if e.stderr:
            print(f"stderr: {e.stderr.strip()}")
        return False

def find_qt_cmake():
    """Try to locate qt-cmake in standard Qt install paths or PATH"""
    qt_versions = ["6.8.3"]
    qt_base = Path.home() / "Qt"
    
    for version in qt_versions:
        qt_cmake_path = qt_base / version / "macos" / "bin" / "qt-cmake"
        if qt_cmake_path.exists():
            print(f"Found qt-cmake: {qt_cmake_path}")
            return str(qt_cmake_path)

    qt_cmake_path = which("qt-cmake")
    if qt_cmake_path:
        print(f"Found qt-cmake in PATH: {qt_cmake_path}")
        return qt_cmake_path

    print("qt-cmake not found — falling back to system cmake")
    return "cmake"

def check_dependencies():
    """Check if required tools are available"""
    required_tools = ['cmake', 'conan', 'git']
    missing_tools = []
    
    for tool in required_tools:
        if not which(tool):
            missing_tools.append(tool)
            print(f"{tool} is missing")
        else:
            print(f"{tool} is available")
    
    if missing_tools:
        print(f"\nMissing required tools: {', '.join(missing_tools)}")
        return False
    
    return True

def setup_conan():
    """Set up Conan package manager"""
    print("\n=== Setting up Conan ===")
    run_command(['conan', 'profile', 'detect', '--force'], check=False)
    run_command(['conan', 'remote', 'add', 'conancenter', 'https://center.conan.io'], check=False)
    return True

def setup_build_directory():
    """Set up build directory"""
    print("\n=== Setting up build directory ===")
    build_dir = Path('build')
    build_dir.mkdir(exist_ok=True)
    return True

def install_dependencies():
    """Install Conan dependencies"""
    print("\n=== Installing dependencies ===")
    return run_command(['conan', 'install', '..', '--build=missing', '-s', 'build_type=Debug'], cwd='build')

def configure_cmake(qt_cmake_cmd):
    """Configure CMake build"""
    print("\n=== Configuring CMake ===")
    
    build_dir = Path('build') / 'Debug'
    build_dir.mkdir(parents=True, exist_ok=True)

    cmake_cmd = [
        qt_cmake_cmd,
        '../..',
        '-DCMAKE_BUILD_TYPE=Debug',
        '-DCMAKE_OSX_DEPLOYMENT_TARGET=15.4',
        '-DQMAKE_MACOSX_DEPLOYMENT_TARGET=15.4',
        '-DCMAKE_TOOLCHAIN_FILE=generators/conan_toolchain.cmake'
    ]
    
    return run_command(cmake_cmd, cwd=build_dir)

def build_project(parallel_jobs=None, target=None):
    """Build the project"""
    print("\n=== Building project ===")
    build_dir = Path('build') / 'Debug'
    
    cmd = ['cmake', '--build', '.']
    
    if parallel_jobs:
        cmd.extend(['--parallel', str(parallel_jobs)])
    
    if target:
        cmd.extend(['--target', target])
        
    return run_command(cmd, cwd=build_dir)

def main():
    parser = argparse.ArgumentParser(description="Murmur Desktop - Development Environment Setup")
    parser.add_argument('--target', '-t', help='Specific target to build (e.g., MurmurTests, MurmurDesktopApp)')
    parser.add_argument('--jobs', '-j', type=int, default=multiprocessing.cpu_count(), 
                       help='Number of parallel build jobs (default: CPU count)')
    parser.add_argument('--skip-deps', action='store_true', 
                       help='Skip dependency installation and setup')
    parser.add_argument('--clean', action='store_true', 
                       help='Clean build directory before building')
    
    args = parser.parse_args()
    
    print("Murmur Desktop - Development Environment Setup")
    print("=" * 50)
    print(f"Parallel jobs: {args.jobs}")
    if args.target:
        print(f"Target: {args.target}")

    os.chdir(Path(__file__).parent.parent)
    print(f"Working directory: {Path.cwd()}")

    if args.clean:
        print("\n=== Cleaning build directory ===")
        build_dir = Path('build')
        if build_dir.exists():
            import shutil
            shutil.rmtree(build_dir)
            print("Build directory cleaned")

    qt_cmake_cmd = find_qt_cmake()

    if args.skip_deps:
        print("\n=== Skipping dependency setup ===")
        steps = [
            ("Configuring CMake", lambda: configure_cmake(qt_cmake_cmd)),
            ("Building project", lambda: build_project(args.jobs, args.target)),
        ]
    else:
        steps = [
            ("Checking dependencies", check_dependencies),
            ("Setting up Conan", setup_conan),
            ("Setting up build directory", setup_build_directory),
            ("Installing dependencies", install_dependencies),
            ("Configuring CMake", lambda: configure_cmake(qt_cmake_cmd)),
            ("Building project", lambda: build_project(args.jobs, args.target)),
        ]
    
    for step_name, step_func in steps:
        print(f"\n{'='*20} {step_name} {'='*20}")
        if not step_func():
            print(f"\nFailed at step: {step_name}")
            sys.exit(1)
        print(f"Completed: {step_name}")

    print("\n" + "="*50)
    print("Setup completed successfully!\n")
    
    if args.target == 'MurmurTests':
        print("To run tests:")
        print("  cd build/Debug/tests && ./MurmurTests")
    elif args.target == 'MurmurDesktopApp':
        print("To run the application:")
        print("  cd build/Debug/src && ./MurmurDesktopApp")
    else:
        print("To run the application:")
        print("  cd build/Debug/src && ./MurmurDesktopApp")
        print("To run tests:")
        print("  cd build/Debug/tests && ./MurmurTests")
        print("\nBuild-only commands:")
        print("  python3 scripts/setup.py --target MurmurTests --skip-deps")
        print("  python3 scripts/setup.py --target MurmurDesktopApp --skip-deps -j 8")

if __name__ == '__main__':
    main()