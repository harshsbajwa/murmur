#include "MockComponents.hpp"

namespace Murmur {
namespace Test {

// MockFFmpegWrapper implementation
MockFFmpegWrapper::MockFFmpegWrapper(QObject* parent) : QObject(parent) {
    // Constructor implementation
}

void MockFFmpegWrapper::simulateProgress() {
    // Simulate progress signal emission
}

void MockFFmpegWrapper::simulateCompletion() {
    // Simulate completion signal emission
}

// MockNetworkManager implementation
MockNetworkManager::MockNetworkManager(QObject* parent) : QObject(parent) {
    // Constructor implementation
}

// MockWhisperWrapper implementation
MockWhisperWrapper::MockWhisperWrapper(QObject* parent) : QObject(parent) {
    // Constructor implementation
}

// MockLibTorrentWrapper implementation
MockLibTorrentWrapper::MockLibTorrentWrapper(QObject* parent) : QObject(parent) {
    // Constructor implementation
}

void MockLibTorrentWrapper::updateTorrentProgress() {
    // Update torrent progress simulation
}

// MockHardwareAccelerator implementation
MockHardwareAccelerator::MockHardwareAccelerator(QObject* parent) : QObject(parent) {
    // Constructor implementation
}

} // namespace Test
} // namespace Murmur