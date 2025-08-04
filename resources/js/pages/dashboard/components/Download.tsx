import { Head } from '@inertiajs/react';
import { useState, useEffect } from 'react';
import { Download, Monitor, Smartphone, HardDrive, CheckCircle, AlertCircle, ExternalLink } from 'lucide-react';

interface DownloadItem {
    name: string;
    url: string;
    size: number;
    type: string;
    architecture: string;
    requirements: string;
}

interface ReleaseData {
    version: string;
    name: string;
    published_at: string;
    description: string;
    downloads: {
        windows: DownloadItem[];
        macos: DownloadItem[];
        linux: DownloadItem[];
    };
    system_requirements: {
        windows: Record<string, string>;
        macos: Record<string, string>;
        linux: Record<string, string>;
    };
}

interface DownloadsProps {
    release: ReleaseData;
    detectedPlatform: string;
}

export default function Downloads({ release, detectedPlatform }: DownloadsProps) {
    const [selectedPlatform, setSelectedPlatform] = useState(detectedPlatform !== 'unknown' ? detectedPlatform : 'windows');
    const [downloadStarted, setDownloadStarted] = useState<string | null>(null);

    const formatFileSize = (bytes: number): string => {
        if (bytes === 0) return 'Unknown size';
        const sizes = ['Bytes', 'KB', 'MB', 'GB'];
        const i = Math.floor(Math.log(bytes) / Math.log(1024));
        return Math.round(bytes / Math.pow(1024, i) * 100) / 100 + ' ' + sizes[i];
    };

    const handleDownload = async (item: DownloadItem) => {
        setDownloadStarted(item.url);
        
        // Track download
        try {
            await fetch('/api/desktop/track-download', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                    'X-CSRF-TOKEN': document.querySelector('meta[name="csrf-token"]')?.getAttribute('content') || '',
                },
                body: JSON.stringify({
                    platform: selectedPlatform,
                    version: release.version,
                    download_type: item.type,
                }),
            });
        } catch (error) {
            console.warn('Failed to track download:', error);
        }

        // Start download
        window.open(item.url, '_blank');
        
        // Reset download state after a delay
        setTimeout(() => setDownloadStarted(null), 3000);
    };

    const getPlatformIcon = (platform: string) => {
        switch (platform) {
            case 'windows':
                return <Monitor className="w-5 h-5" />;
            case 'macos':
                return <Smartphone className="w-5 h-5" />;
            case 'linux':
                return <HardDrive className="w-5 h-5" />;
            default:
                return <Download className="w-5 h-5" />;
        }
    };

    const getPlatformName = (platform: string) => {
        switch (platform) {
            case 'windows':
                return 'Windows';
            case 'macos':
                return 'macOS';
            case 'linux':
                return 'Linux';
            default:
                return platform;
        }
    };

    const getDownloadTypeLabel = (type: string) => {
        switch (type) {
            case 'installer':
                return 'Installer (Recommended)';
            case 'msi':
                return 'MSI Package';
            case 'dmg':
                return 'DMG Image';
            case 'deb':
                return 'DEB Package';
            case 'rpm':
                return 'RPM Package';
            case 'appimage':
                return 'AppImage';
            default:
                return type.toUpperCase();
        }
    };

    return (
        <>
            <Head title="Download Murmur Desktop" />
            
            <div className="min-h-screen bg-gradient-to-br from-blue-50 to-indigo-100 py-12 px-4 sm:px-6 lg:px-8">
                <div className="max-w-4xl mx-auto">
                    {/* Header */}
                    <div className="text-center mb-12">
                        <h1 className="text-4xl font-bold text-gray-900 mb-4">
                            Download Murmur Desktop
                        </h1>
                        <p className="text-xl text-gray-600 mb-6">
                            P2P Video Transcription Application
                        </p>
                        <div className="inline-flex items-center space-x-2 px-4 py-2 bg-green-100 text-green-800 rounded-full">
                            <CheckCircle className="w-5 h-5" />
                            <span className="font-medium">{release.version}</span>
                            <span className="text-green-600">Latest Version</span>
                        </div>
                    </div>

                    {/* Platform Selection */}
                    <div className="mb-8">
                        <div className="flex justify-center space-x-4 mb-6">
                            {(['windows', 'macos', 'linux'] as const).map((platform) => (
                                <button
                                    key={platform}
                                    onClick={() => setSelectedPlatform(platform)}
                                    className={`flex items-center space-x-2 px-6 py-3 rounded-lg transition-all ${
                                        selectedPlatform === platform
                                            ? 'bg-blue-600 text-white shadow-lg'
                                            : 'bg-white text-gray-700 hover:bg-gray-50 border border-gray-200'
                                    }`}
                                >
                                    {getPlatformIcon(platform)}
                                    <span className="font-medium">{getPlatformName(platform)}</span>
                                    {detectedPlatform === platform && (
                                        <span className="text-xs bg-green-500 text-white px-2 py-1 rounded-full">
                                            Detected
                                        </span>
                                    )}
                                </button>
                            ))}
                        </div>
                    </div>

                    {/* Downloads */}
                    <div className="grid md:grid-cols-2 gap-8">
                        {/* Download Options */}
                        <div className="bg-white rounded-lg shadow-lg p-6">
                            <h2 className="text-2xl font-bold text-gray-900 mb-6">
                                Download for {getPlatformName(selectedPlatform)}
                            </h2>
                            
                            <div className="space-y-4">
                                {release.downloads[selectedPlatform as keyof typeof release.downloads]?.map((item, index) => (
                                    <div key={index} className="border border-gray-200 rounded-lg p-4 hover:shadow-md transition-shadow">
                                        <div className="flex items-center justify-between mb-2">
                                            <h3 className="font-semibold text-gray-900">{item.name}</h3>
                                            <span className="text-sm text-gray-500">{formatFileSize(item.size)}</span>
                                        </div>
                                        
                                        <p className="text-sm text-gray-600 mb-4">{item.requirements}</p>
                                        
                                        <button
                                            onClick={() => handleDownload(item)}
                                            disabled={downloadStarted === item.url}
                                            className={`w-full flex items-center justify-center space-x-2 px-4 py-2 rounded-lg transition-all ${
                                                downloadStarted === item.url
                                                    ? 'bg-green-600 text-white'
                                                    : 'bg-blue-600 hover:bg-blue-700 text-white'
                                            }`}
                                        >
                                            {downloadStarted === item.url ? (
                                                <>
                                                    <CheckCircle className="w-5 h-5" />
                                                    <span>Download Started</span>
                                                </>
                                            ) : (
                                                <>
                                                    <Download className="w-5 h-5" />
                                                    <span>Download {getDownloadTypeLabel(item.type)}</span>
                                                </>
                                            )}
                                        </button>
                                    </div>
                                )) || (
                                    <div className="text-center py-8">
                                        <AlertCircle className="w-12 h-12 text-gray-400 mx-auto mb-4" />
                                        <p className="text-gray-600">No downloads available for this platform yet.</p>
                                    </div>
                                )}
                            </div>
                        </div>

                        {/* System Requirements */}
                        <div className="bg-white rounded-lg shadow-lg p-6">
                            <h2 className="text-2xl font-bold text-gray-900 mb-6">System Requirements</h2>
                            
                            <div className="space-y-4">
                                {Object.entries(release.system_requirements[selectedPlatform as keyof typeof release.system_requirements] || {}).map(([key, value]) => (
                                    <div key={key} className="flex justify-between py-2 border-b border-gray-100 last:border-b-0">
                                        <span className="font-medium text-gray-700 capitalize">
                                            {key.replace('_', ' ')}:
                                        </span>
                                        <span className="text-gray-600 text-right max-w-xs">{value}</span>
                                    </div>
                                ))}
                            </div>

                            {/* Installation Instructions */}
                            <div className="mt-6 p-4 bg-blue-50 rounded-lg">
                                <h3 className="font-semibold text-blue-900 mb-2">Installation Instructions</h3>
                                <div className="text-sm text-blue-800">
                                    {selectedPlatform === 'windows' && (
                                        <ol className="list-decimal list-inside space-y-1">
                                            <li>Download the installer (.exe file)</li>
                                            <li>Run the installer as Administrator</li>
                                            <li>Follow the installation wizard</li>
                                            <li>Launch Murmur Desktop from Start Menu</li>
                                        </ol>
                                    )}
                                    {selectedPlatform === 'macos' && (
                                        <ol className="list-decimal list-inside space-y-1">
                                            <li>Download the DMG file</li>
                                            <li>Open the DMG file</li>
                                            <li>Drag Murmur Desktop to Applications folder</li>
                                            <li>Launch from Applications or Launchpad</li>
                                        </ol>
                                    )}
                                    {selectedPlatform === 'linux' && (
                                        <ol className="list-decimal list-inside space-y-1">
                                            <li>Download the appropriate package for your distribution</li>
                                            <li>Install using your package manager (dpkg, rpm, or make executable for AppImage)</li>
                                            <li>Launch from application menu or terminal</li>
                                        </ol>
                                    )}
                                </div>
                            </div>
                        </div>
                    </div>

                    {/* Release Notes */}
                    {release.description && (
                        <div className="mt-8 bg-white rounded-lg shadow-lg p-6">
                            <h2 className="text-2xl font-bold text-gray-900 mb-4">Release Notes</h2>
                            <div className="prose max-w-none text-gray-700">
                                {release.description.split('\n').map((line, index) => (
                                    <p key={index} className="mb-2">{line}</p>
                                ))}
                            </div>
                        </div>
                    )}

                    {/* Support Links */}
                    <div className="mt-8 text-center">
                        <div className="flex justify-center space-x-6 text-sm text-gray-600">
                            <a href="/help" className="flex items-center space-x-1 hover:text-blue-600">
                                <ExternalLink className="w-4 h-4" />
                                <span>Help & Support</span>
                            </a>
                            <a href="https://github.com/harshsbajwa/murmur/desktop" className="flex items-center space-x-1 hover:text-blue-600">
                                <ExternalLink className="w-4 h-4" />
                                <span>Source Code</span>
                            </a>
                            <a href="/changelog" className="flex items-center space-x-1 hover:text-blue-600">
                                <ExternalLink className="w-4 h-4" />
                                <span>Changelog</span>
                            </a>
                        </div>
                    </div>
                </div>
            </div>
        </>
    );
}