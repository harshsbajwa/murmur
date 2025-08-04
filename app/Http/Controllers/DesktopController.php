<?php

namespace App\Http\Controllers;

use Illuminate\Http\JsonResponse;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\Cache;
use Illuminate\Support\Facades\Http;
use Illuminate\Support\Facades\Log;

class DesktopController extends Controller
{
    /**
     * Get the latest desktop application download information
     */
    public function getLatestRelease(): JsonResponse
    {
        try {
            // Cache the GitHub API response for 1 hour
            $release = Cache::remember('desktop_latest_release', 3600, function () {
                $response = Http::timeout(10)->get('https://api.github.com/repos/harshsbajwa/murmur/releases/latest');

                if (! $response->successful()) {
                    throw new \Exception('Failed to fetch release information');
                }

                return $response->json();
            });

            // Parse release assets by platform
            $downloads = $this->parseReleaseAssets($release['assets'] ?? []);

            return response()->json([
                'version' => $release['tag_name'] ?? 'v1.0.0',
                'name' => $release['name'] ?? 'Murmur Desktop',
                'published_at' => $release['published_at'] ?? now()->toISOString(),
                'description' => $release['body'] ?? '',
                'downloads' => $downloads,
                'system_requirements' => $this->getSystemRequirements(),
            ]);

        } catch (\Exception $e) {
            Log::error('Failed to fetch desktop release information', [
                'error' => $e->getMessage(),
            ]);

            // Return fallback information if GitHub API is unavailable
            return response()->json([
                'version' => 'v1.0.0',
                'name' => 'Murmur Desktop',
                'published_at' => now()->toISOString(),
                'description' => 'P2P Video Transcription Application',
                'downloads' => $this->getFallbackDownloads(),
                'system_requirements' => $this->getSystemRequirements(),
            ]);
        }
    }

    /**
     * Detect user's platform and redirect to appropriate download
     */
    public function downloadRedirect(Request $request)
    {
        $userAgent = $request->header('User-Agent', '');
        $platform = $this->detectPlatform($userAgent);

        try {
            $release = Cache::get('desktop_latest_release');
            if (! $release) {
                // Fetch fresh data if not cached
                $this->getLatestRelease();
                $release = Cache::get('desktop_latest_release');
            }

            $downloads = $this->parseReleaseAssets($release['assets'] ?? []);

            // Get the appropriate download URL for the detected platform
            $downloadUrl = $this->getDownloadUrlForPlatform($platform, $downloads);

            if ($downloadUrl) {
                return redirect($downloadUrl);
            }

            // Fallback to downloads page if no direct download available
            return redirect()->route('downloads');

        } catch (\Exception $e) {
            Log::error('Failed to redirect to download', [
                'platform' => $platform,
                'error' => $e->getMessage(),
            ]);

            return redirect()->route('downloads');
        }
    }

    /**
     * Show the downloads page
     */
    public function downloads()
    {
        $releaseData = $this->getLatestRelease()->getData(true);

        return inertia('Downloads', [
            'release' => $releaseData,
            'detectedPlatform' => $this->detectPlatform(request()->header('User-Agent', '')),
        ]);
    }

    /**
     * Parse GitHub release assets into organized download links
     */
    private function parseReleaseAssets(array $assets): array
    {
        $downloads = [
            'windows' => [],
            'macos' => [],
            'linux' => [],
        ];

        foreach ($assets as $asset) {
            $name = $asset['name'] ?? '';
            $downloadUrl = $asset['browser_download_url'] ?? '';
            $size = $asset['size'] ?? 0;

            // Windows downloads
            if (str_contains($name, 'Setup.exe')) {
                $downloads['windows'][] = [
                    'name' => 'Windows Installer (Recommended)',
                    'url' => $downloadUrl,
                    'size' => $size,
                    'type' => 'installer',
                    'architecture' => 'x64',
                    'requirements' => 'Windows 10 or later (64-bit)',
                ];
            } elseif (str_contains($name, '.msi')) {
                $downloads['windows'][] = [
                    'name' => 'Windows MSI Package',
                    'url' => $downloadUrl,
                    'size' => $size,
                    'type' => 'msi',
                    'architecture' => 'x64',
                    'requirements' => 'Windows 10 or later (64-bit)',
                ];
            }

            // macOS downloads
            elseif (str_contains($name, 'macOS-Intel.dmg')) {
                $downloads['macos'][] = [
                    'name' => 'macOS Intel',
                    'url' => $downloadUrl,
                    'size' => $size,
                    'type' => 'dmg',
                    'architecture' => 'x86_64',
                    'requirements' => 'macOS 11.0 or later (Intel Macs)',
                ];
            } elseif (str_contains($name, 'macOS-Apple-Silicon.dmg')) {
                $downloads['macos'][] = [
                    'name' => 'macOS Apple Silicon',
                    'url' => $downloadUrl,
                    'size' => $size,
                    'type' => 'dmg',
                    'architecture' => 'arm64',
                    'requirements' => 'macOS 11.0 or later (Apple Silicon Macs)',
                ];
            }

            // Linux downloads
            elseif (str_contains($name, '.deb')) {
                $downloads['linux'][] = [
                    'name' => 'Ubuntu/Debian Package',
                    'url' => $downloadUrl,
                    'size' => $size,
                    'type' => 'deb',
                    'architecture' => 'x86_64',
                    'requirements' => 'Ubuntu 20.04+ or Debian 11+ (64-bit)',
                ];
            } elseif (str_contains($name, '.rpm')) {
                $downloads['linux'][] = [
                    'name' => 'Fedora/RHEL/CentOS Package',
                    'url' => $downloadUrl,
                    'size' => $size,
                    'type' => 'rpm',
                    'architecture' => 'x86_64',
                    'requirements' => 'Fedora 35+ or RHEL 8+ (64-bit)',
                ];
            } elseif (str_contains($name, '.AppImage')) {
                $downloads['linux'][] = [
                    'name' => 'AppImage (Universal)',
                    'url' => $downloadUrl,
                    'size' => $size,
                    'type' => 'appimage',
                    'architecture' => 'x86_64',
                    'requirements' => 'Any Linux distribution (64-bit)',
                ];
            }
        }

        return $downloads;
    }

    /**
     * Detect user's platform from User-Agent
     */
    private function detectPlatform(string $userAgent): string
    {
        $userAgent = strtolower($userAgent);

        if (str_contains($userAgent, 'windows') || str_contains($userAgent, 'win32') || str_contains($userAgent, 'win64')) {
            return 'windows';
        } elseif (str_contains($userAgent, 'macintosh') || str_contains($userAgent, 'mac os x')) {
            return 'macos';
        } elseif (str_contains($userAgent, 'linux') || str_contains($userAgent, 'ubuntu') || str_contains($userAgent, 'fedora')) {
            return 'linux';
        }

        return 'unknown';
    }

    /**
     * Get download URL for specific platform
     */
    private function getDownloadUrlForPlatform(string $platform, array $downloads): ?string
    {
        if (! isset($downloads[$platform]) || empty($downloads[$platform])) {
            return null;
        }

        return $downloads[$platform][0]['url'] ?? null;
    }

    /**
     * Get system requirements information
     */
    private function getSystemRequirements(): array
    {
        return [
            'windows' => [
                'os' => 'Windows 10 or later (64-bit)',
                'processor' => 'Intel Core i3 or AMD equivalent',
                'memory' => '4 GB RAM minimum, 8 GB recommended',
                'storage' => '2 GB available space',
                'graphics' => 'DirectX 11 compatible',
                'network' => 'Broadband Internet connection',
            ],
            'macos' => [
                'os' => 'macOS 11.0 (Big Sur) or later',
                'processor' => 'Intel Core i3 or Apple Silicon (M1/M2)',
                'memory' => '4 GB RAM minimum, 8 GB recommended',
                'storage' => '2 GB available space',
                'graphics' => 'Metal-compatible graphics card',
                'network' => 'Broadband Internet connection',
            ],
            'linux' => [
                'os' => 'Ubuntu 20.04+ or equivalent distribution',
                'processor' => 'Intel Core i3 or AMD equivalent (64-bit)',
                'memory' => '4 GB RAM minimum, 8 GB recommended',
                'storage' => '2 GB available space',
                'graphics' => 'OpenGL 3.3 support',
                'network' => 'Broadband Internet connection',
                'additional' => 'ALSA or PulseAudio for audio support',
            ],
        ];
    }

    /**
     * Get fallback download information when GitHub API is unavailable
     */
    private function getFallbackDownloads(): array
    {
        return [
            'windows' => [
                [
                    'name' => 'Windows Installer',
                    'url' => '/downloads/murmur-desktop-windows-setup.exe',
                    'size' => 0,
                    'type' => 'installer',
                    'architecture' => 'x64',
                    'requirements' => 'Windows 10 or later (64-bit)',
                ],
            ],
            'macos' => [
                [
                    'name' => 'macOS Universal',
                    'url' => '/downloads/murmur-desktop-macos.dmg',
                    'size' => 0,
                    'type' => 'dmg',
                    'architecture' => 'universal',
                    'requirements' => 'macOS 11.0 or later',
                ],
            ],
            'linux' => [
                [
                    'name' => 'AppImage (Universal)',
                    'url' => '/downloads/murmur-desktop-linux.AppImage',
                    'size' => 0,
                    'type' => 'appimage',
                    'architecture' => 'x86_64',
                    'requirements' => 'Any Linux distribution (64-bit)',
                ],
            ],
        ];
    }

    /**
     * Track download statistics
     */
    public function trackDownload(Request $request)
    {
        $validated = $request->validate([
            'platform' => 'required|string|in:windows,macos,linux',
            'version' => 'required|string',
            'download_type' => 'required|string',
        ]);

        // Log download for analytics
        Log::info('Desktop app download', [
            'platform' => $validated['platform'],
            'version' => $validated['version'],
            'type' => $validated['download_type'],
            'ip' => $request->ip(),
            'user_agent' => $request->header('User-Agent'),
            'timestamp' => now()->toISOString(),
        ]);

        return response()->json(['success' => true]);
    }
}
