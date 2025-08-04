import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import { Slider } from '@/components/ui/slider';
import { TranscriptionResult, VideoFile } from '@/types';
import {
    Play,
    Pause,
    Volume2,
    VolumeX,
    Maximize,
    Minimize,
    Captions,
    CaptionsOff,
    SkipBack,
    SkipForward,
    Settings,
    Loader2,
    Upload,
    Share2,
} from 'lucide-react';
import { useState, useRef, useEffect, useCallback } from 'react';
import WebTorrent from 'webtorrent';

const formatDuration = (seconds: number): string => {
    if (isNaN(seconds)) return '0:00';
    const minutes = Math.floor(seconds / 60);
    const remainingSeconds = Math.floor(seconds % 60);
    return `${minutes}:${remainingSeconds.toString().padStart(2, '0')}`;
};

interface VideoPlayerProps {
    currentFile: VideoFile | null;
    onTimeUpdate: (id: string, time: number) => void;
    getPlaybackTime: (id: string) => Promise<number | undefined>;
    transcriptionResult?: TranscriptionResult;
    subtitleUrl?: string;
    torrentInstance?: WebTorrent.Torrent;
    onShare?: (file: VideoFile) => void;
}

const isTorrentInstance = (obj: unknown): obj is WebTorrent.Torrent => {
    if (!obj || typeof obj !== 'object' || obj === null) return false;
    const candidate = obj as Record<string, unknown>;
    return 'infoHash' in candidate && typeof candidate.infoHash === 'string' && 
           'on' in candidate && typeof candidate.on === 'function';
};

export function VideoPlayer({
    currentFile,
    onTimeUpdate,
    getPlaybackTime,
    transcriptionResult,
    subtitleUrl,
    torrentInstance,
    onShare,
}: VideoPlayerProps) {
    const videoRef = useRef<HTMLVideoElement>(null);
    const videoContainerRef = useRef<HTMLDivElement>(null);
    const controlsRef = useRef<HTMLDivElement>(null);
    const hideControlsTimeoutRef = useRef<NodeJS.Timeout | null>(null);
    const currentBlobUrlRef = useRef<string | null>(null);

    const [isPlaying, setIsPlaying] = useState(false);
    const [isMuted, setIsMuted] = useState(false);
    const [volume, setVolume] = useState(1);
    const [currentTime, setCurrentTime] = useState(0);
    const [duration, setDuration] = useState(0);
    const [isFullscreen, setIsFullscreen] = useState(false);
    const [showSubtitles, setShowSubtitles] = useState(true);
    const [currentSubtitle, setCurrentSubtitle] = useState<string>('');
    const [showControls, setShowControls] = useState(true);
    const [isControlsHovered, setIsControlsHovered] = useState(false);
    const [playbackRate, setPlaybackRate] = useState(1);
    const [showSettings, setShowSettings] = useState(false);
    const [isBuffering, setIsBuffering] = useState(false);
    const [bufferedRanges, setBufferedRanges] = useState<TimeRanges | null>(null);
    const [torrentReady, setTorrentReady] = useState(false);

    const playbackRates = [0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2];

    useEffect(() => {
        const videoElement = videoRef.current;
        if (!videoElement) return;

        let cleanupFunctions: (() => void)[] = [];

        const cleanup = () => {
            console.log('Cleaning up video player...');
            cleanupFunctions.forEach(fn => {
                try {
                    fn();
                } catch (error) {
                    console.error('Error during video player cleanup:', error);
                }
            });
            cleanupFunctions = [];

            if (videoElement) {
                videoElement.pause();
                if (videoElement.src && videoElement.src.startsWith('blob:')) {
                    URL.revokeObjectURL(videoElement.src);
                }
                if (currentBlobUrlRef.current) {
                    URL.revokeObjectURL(currentBlobUrlRef.current);
                    currentBlobUrlRef.current = null;
                }
                videoElement.removeAttribute('src');
                videoElement.load();
                setIsPlaying(false);
                setCurrentTime(0);
                setDuration(0);
                setTorrentReady(false);
                setIsBuffering(false);
            }
        };

        if (currentFile?.isTorrent) {
            console.log('Setting up torrent video player for:', currentFile.name);
            cleanup();
            setIsBuffering(true);

            const torrent = torrentInstance;

            if (!isTorrentInstance(torrent)) {
                console.error(
                    'VideoPlayer Error: Invalid or missing torrentInstance prop for the current file.',
                    { currentFile, torrentInstance },
                );
                setIsBuffering(false);
                return;
            }

            const setupStreamWithFallbacks = async (streamFile: WebTorrent.TorrentFile, videoElement: HTMLVideoElement) => {
                console.log('Setting up stream for file:', streamFile.name);
                
                // Method 1: Try renderTo (most direct)
                if (typeof streamFile.renderTo === 'function') {
                    console.log('Using renderTo method...');
                    try {
                        streamFile.renderTo(videoElement, { controls: false, autoplay: false });
                        return true;
                    } catch (error) {
                        console.warn('renderTo failed, trying next method:', error);
                    }
                }
                
                // Method 2: Try getBlobURL
                if (typeof streamFile.getBlobURL === 'function') {
                    console.log('Using getBlobURL method...');
                    return new Promise<boolean>((resolve) => {
                        streamFile.getBlobURL((err, blobUrl) => {
                            if (err) {
                                console.warn('getBlobURL failed:', err);
                                resolve(false);
                                return;
                            }
                            
                            if (blobUrl && videoElement) {
                                currentBlobUrlRef.current = blobUrl;
                                videoElement.src = blobUrl;
                                videoElement.load();
                                resolve(true);
                            } else {
                                resolve(false);
                            }
                        });
                    });
                }
                
                // Method 3: Try getBlob
                if (typeof streamFile.getBlob === 'function') {
                    console.log('Using getBlob method...');
                    return new Promise<boolean>((resolve) => {
                        streamFile.getBlob((err, blob) => {
                            if (err) {
                                console.warn('getBlobURL failed:', err);
                                resolve(false);
                                return;
                            }

                            if (blob && videoElement) {
                                const blobUrl = URL.createObjectURL(blob);
                                currentBlobUrlRef.current = blobUrl;
                                videoElement.src = blobUrl;
                                videoElement.load();
                                resolve(true);
                            } else {
                                resolve(false);
                            }
                        });
                    });
                }
                
                // Method 4: Try createReadStream (last resort)
                if (typeof streamFile.createReadStream === 'function') {
                    console.log('Using createReadStream method...');
                    try {
                        const stream = streamFile.createReadStream();
                        const chunks: Uint8Array[] = [];
                        
                        stream.on('data', (chunk: Uint8Array) => {
                            chunks.push(chunk);
                        });
                        
                        stream.on('end', () => {
                            const blob = new Blob(chunks, { type: 'video/mp4' });
                            const blobUrl = URL.createObjectURL(blob);
                            currentBlobUrlRef.current = blobUrl;
                            videoElement.src = blobUrl;
                            videoElement.load();
                        });
                        
                        stream.on('error', (error: Error) => {
                            console.error('Stream error:', error);
                        });
                        
                        return true;
                    } catch (error) {
                        console.warn('createReadStream failed:', error);
                    }
                }
                
                return false;
            };
            
            const setupStream = async () => {
                if (!videoElement || !currentFile) return;
            
                console.log('Torrent is ready, finding file by path and setting up stream...');
                
                const streamFile = torrent.files.find(
                    f => f.path === currentFile.torrentFilePath || f.name === currentFile.name
                );
            
                if (!streamFile) {
                    console.error('VideoPlayer Error: Could not find file in torrent.', {
                        searchPath: currentFile.torrentFilePath,
                        searchName: currentFile.name,
                        availableFiles: torrent.files.map(f => ({ name: f.name, path: f.path }))
                    });
                    setIsBuffering(false);
                    return;
                }
                console.log('Found file, attempting to set up stream...', streamFile);
                
                try {
                    const success = await setupStreamWithFallbacks(streamFile, videoElement);
                    
                    if (!success) {
                        console.error('All streaming methods failed');
                        setIsBuffering(false);
                        return;
                    }
            
                    const handleCanPlay = async () => {
                        console.log('Video can play, setting up playback...');
                        setIsBuffering(false);
                        setTorrentReady(true);
            
                        try {
                            // Try to start playback
                            videoElement.muted = true;
                            await videoElement.play();
                            
                            // Unmute after a short delay if not muted by user
                            if (!isMuted) {
                                setTimeout(() => {
                                    if (videoElement && !videoElement.muted) {
                                        videoElement.muted = false;
                                    }
                                }, 500);
                            }
                        } catch (err) {
                            console.warn('Video autoplay was blocked by the browser.', err);
                            setIsBuffering(false);
                        }
                    };
            
                    // Event listeners
                    const handleLoadStart = () => {
                        console.log('Video load started');
                        setIsBuffering(true);
                    };
            
                    const handleLoadedData = () => {
                        console.log('Video data loaded');
                        setIsBuffering(false);
                    };
            
                    const handleProgress = () => {
                        if (videoElement.buffered.length > 0) {
                            setBufferedRanges(videoElement.buffered);
                        }
                    };
            
                    const handleError = (error: Event) => {
                        console.error('Video element error:', error);
                        setIsBuffering(false);
                    };
            
                    const handleWaiting = () => {
                        console.log('Video waiting for more data');
                        setIsBuffering(true);
                    };
            
                    const handlePlaying = () => {
                        console.log('Video playing');
                        setIsBuffering(false);
                    };
            
                    const handleStalled = () => {
                        console.log('Video stalled');
                        setIsBuffering(true);
                    };
            
                    const handleCanPlayThrough = () => {
                        console.log('Video can play through');
                        setIsBuffering(false);
                    };
            
                    // Add all event listeners
                    videoElement.addEventListener('canplay', handleCanPlay);
                    videoElement.addEventListener('loadstart', handleLoadStart);
                    videoElement.addEventListener('loadeddata', handleLoadedData);
                    videoElement.addEventListener('progress', handleProgress);
                    videoElement.addEventListener('error', handleError);
                    videoElement.addEventListener('waiting', handleWaiting);
                    videoElement.addEventListener('playing', handlePlaying);
                    videoElement.addEventListener('stalled', handleStalled);
                    videoElement.addEventListener('canplaythrough', handleCanPlayThrough);
            
                    cleanupFunctions.push(() => {
                        videoElement.removeEventListener('canplay', handleCanPlay);
                        videoElement.removeEventListener('loadstart', handleLoadStart);
                        videoElement.removeEventListener('loadeddata', handleLoadedData);
                        videoElement.removeEventListener('progress', handleProgress);
                        videoElement.removeEventListener('error', handleError);
                        videoElement.removeEventListener('waiting', handleWaiting);
                        videoElement.removeEventListener('playing', handlePlaying);
                        videoElement.removeEventListener('stalled', handleStalled);
                        videoElement.removeEventListener('canplaythrough', handleCanPlayThrough);
                    });
            
                    console.log('Torrent stream setup complete.');
                } catch (error) {
                    console.error('Error setting up torrent stream:', error);
                    setIsBuffering(false);
                }
            };
            if (torrent.ready) {
                setupStream();
            } else {
                console.log('Waiting for torrent to be ready...');
                const onReady = () => {
                    console.log('Torrent is now ready.');
                    setupStream();
                };
                torrent.once('ready', onReady);
                cleanupFunctions.push(() => torrent.removeListener('ready', onReady));
            }
        } else if (currentFile && !currentFile.isTorrent) {
            console.log('Setting up regular video player for:', currentFile.name);
            cleanup();
            if (videoElement.src !== currentFile.url) {
                videoElement.src = currentFile.url;
                videoElement.load();
            }
            setTorrentReady(true);
        } else {
            cleanup();
        }

        return cleanup;
    }, [currentFile, torrentInstance, isMuted]);

    useEffect(() => {
        const handleFullscreenChange = () => setIsFullscreen(!!document.fullscreenElement);
        document.addEventListener('fullscreenchange', handleFullscreenChange);
        return () => document.removeEventListener('fullscreenchange', handleFullscreenChange);
    }, []);

    useEffect(() => {
        if (!currentFile) {
            setCurrentSubtitle('');
        }
    }, [currentFile]);

    const resetControlsTimeout = useCallback(() => {
        if (hideControlsTimeoutRef.current) clearTimeout(hideControlsTimeoutRef.current);
        setShowControls(true);
        if (isPlaying && !isControlsHovered) {
            hideControlsTimeoutRef.current = setTimeout(() => setShowControls(false), 3000);
        }
    }, [isPlaying, isControlsHovered]);

    useEffect(() => {
        resetControlsTimeout();
        return () => {
            if (hideControlsTimeoutRef.current) clearTimeout(hideControlsTimeoutRef.current);
        };
    }, [resetControlsTimeout]);

    const handleMouseMove = useCallback(() => resetControlsTimeout(), [resetControlsTimeout]);
    const handleMouseLeave = useCallback(() => {
        if (isPlaying) setShowControls(false);
    }, [isPlaying]);

    useEffect(() => {
        if (!transcriptionResult?.chunks || !showSubtitles) {
            setCurrentSubtitle('');
            return;
        }
        const currentChunk = transcriptionResult.chunks.find(
            chunk => currentTime >= chunk.timestamp[0] && currentTime <= chunk.timestamp[1],
        );
        setCurrentSubtitle(currentChunk?.text || '');
    }, [currentTime, transcriptionResult, showSubtitles]);

    const handleLoadedMetadata = async () => {
        if (!videoRef.current || !currentFile) return;
        console.log('Video metadata loaded, duration:', videoRef.current.duration);
        setDuration(videoRef.current.duration);

        try {
            const startTime = await getPlaybackTime(currentFile.id);
            if (startTime && isFinite(startTime)) {
                videoRef.current.currentTime = startTime;
            }
        } catch (error) {
            console.error('Error setting playback time:', error);
        }
    };

    const handleTimeUpdateInternal = () => {
        if (!videoRef.current || !currentFile) return;
        const time = videoRef.current.currentTime;
        setCurrentTime(time);
        onTimeUpdate(currentFile.id, time);
        if (videoRef.current.buffered) setBufferedRanges(videoRef.current.buffered);
    };

    const togglePlayPause = useCallback(() => {
        if (!videoRef.current) return;
        if (videoRef.current.paused) {
            videoRef.current.play().catch(err => console.error('Play failed:', err));
        } else {
            videoRef.current.pause();
        }
    }, []);

    const toggleMute = useCallback(() => {
        if (!videoRef.current) return;
        videoRef.current.muted = !videoRef.current.muted;
        setIsMuted(videoRef.current.muted);
    }, []);

    const handleVolumeChange = useCallback((value: number[]) => {
        if (!videoRef.current) return;
        const newVolume = value[0];
        setVolume(newVolume);
        videoRef.current.volume = newVolume;
        if (newVolume > 0 && videoRef.current.muted) {
            videoRef.current.muted = false;
            setIsMuted(false);
        }
    }, []);

    const handleSeek = (value: number[]) => {
        if (videoRef.current) videoRef.current.currentTime = value[0];
    };

    const skip = useCallback(
        (seconds: number) => {
            if (!videoRef.current) return;
            videoRef.current.currentTime = Math.max(0, Math.min(duration, videoRef.current.currentTime + seconds));
        },
        [duration],
    );

    const toggleFullscreen = useCallback(() => {
        if (!document.fullscreenElement) videoContainerRef.current?.requestFullscreen();
        else document.exitFullscreen();
    }, []);

    const toggleSubtitles = useCallback(() => setShowSubtitles(s => !s), []);

    const handlePlaybackRateChange = (rate: number) => {
        if (!videoRef.current) return;
        videoRef.current.playbackRate = rate;
        setPlaybackRate(rate);
        setShowSettings(false);
    };

    useEffect(() => {
        const handleKeyDown = (e: KeyboardEvent) => {
            if (!videoRef.current || document.activeElement instanceof HTMLInputElement) return;
            switch (e.code) {
                case 'Space':
                    e.preventDefault();
                    togglePlayPause();
                    break;
                case 'ArrowLeft':
                    e.preventDefault();
                    skip(-10);
                    break;
                case 'ArrowRight':
                    e.preventDefault();
                    skip(10);
                    break;
                case 'ArrowUp':
                    e.preventDefault();
                    handleVolumeChange([Math.min(1, volume + 0.1)]);
                    break;
                case 'ArrowDown':
                    e.preventDefault();
                    handleVolumeChange([Math.max(0, volume - 0.1)]);
                    break;
                case 'KeyF':
                    e.preventDefault();
                    toggleFullscreen();
                    break;
                case 'KeyM':
                    e.preventDefault();
                    toggleMute();
                    break;
                case 'KeyC':
                    e.preventDefault();
                    toggleSubtitles();
                    break;
            }
        };
        if (currentFile) window.addEventListener('keydown', handleKeyDown);
        return () => window.removeEventListener('keydown', handleKeyDown);
    }, [currentFile, volume, skip, togglePlayPause, handleVolumeChange, toggleFullscreen, toggleMute, toggleSubtitles]);

    const getBufferedPercent = () => {
        if (!bufferedRanges || !duration || bufferedRanges.length === 0) return 0;
        const currentTimeBuffered = bufferedRanges.end(bufferedRanges.length - 1);
        return (currentTimeBuffered / duration) * 100;
    };

    return (
        <Card className="flex h-full flex-col">
            <CardHeader>
                <CardTitle className="flex items-center justify-between gap-2">
                    <div className="flex items-center gap-2 min-w-0">
                        <Play className="h-5 w-5 flex-shrink-0" />
                        <span className="truncate">Video Player</span>
                        {currentFile?.isTorrent && (
                            <span className="text-sm font-normal text-muted-foreground whitespace-nowrap">
                                {torrentReady ? '(Torrent Ready)' : '(Loading Torrent...)'}
                            </span>
                        )}
                    </div>
                    {currentFile && onShare && (
                        <Button
                            size="sm"
                            variant="outline"
                            onClick={() => onShare(currentFile)}
                            className="flex-shrink-0"
                        >
                            <Share2 className="h-3 w-3" />
                            <span className="ml-2 hidden sm:inline">Share</span>
                        </Button>
                    )}
                </CardTitle>
            </CardHeader>
            <CardContent className="flex flex-1 flex-col">
                {currentFile ? (
                    <div
                        ref={videoContainerRef}
                        className="group relative overflow-hidden rounded-lg bg-black"
                        onMouseMove={handleMouseMove}
                        onMouseLeave={handleMouseLeave}
                        tabIndex={0}
                    >
                        <video
                            ref={videoRef}
                            className="aspect-video w-full"
                            onPlay={() => setIsPlaying(true)}
                            onPause={() => setIsPlaying(false)}
                            onLoadedMetadata={handleLoadedMetadata}
                            onTimeUpdate={handleTimeUpdateInternal}
                            onVolumeChange={() => {
                                if (videoRef.current) {
                                    setVolume(videoRef.current.volume);
                                    setIsMuted(videoRef.current.muted);
                                }
                            }}
                            onClick={togglePlayPause}
                            onDoubleClick={toggleFullscreen}
                            crossOrigin="anonymous"
                            preload="metadata"
                        />

                        {isBuffering && (
                            <div className="pointer-events-none absolute inset-0 flex items-center justify-center">
                                <div className="rounded-lg bg-black/80 p-4">
                                    <Loader2 className="mx-auto mb-2 h-8 w-8 animate-spin text-white" />
                                    <p className="text-sm text-white">
                                        {currentFile.isTorrent ? 'Loading torrent stream...' : 'Buffering...'}
                                    </p>
                                </div>
                            </div>
                        )}

                        {showSubtitles && currentSubtitle && (
                            <div
                                className={`pointer-events-none absolute left-0 right-0 flex justify-center px-4 transition-all duration-300 ${
                                    showControls || !isPlaying ? 'bottom-24 sm:bottom-20' : 'bottom-4'
                                }`}
                            >
                                <div className="max-w-4xl rounded-lg bg-black/80 px-4 py-2 text-center text-sm sm:text-base font-medium text-white shadow-lg">
                                    {currentSubtitle}
                                </div>
                            </div>
                        )}

                        <div
                            ref={controlsRef}
                            className={`absolute inset-x-0 bottom-0 bg-gradient-to-t from-black/90 via-black/60 to-transparent p-2 sm:p-4 transition-opacity duration-300 ${
                                showControls || !isPlaying ? 'opacity-100' : 'opacity-0'
                            }`}
                            onMouseEnter={() => setIsControlsHovered(true)}
                            onMouseLeave={() => setIsControlsHovered(false)}
                            onClick={e => e.stopPropagation()}
                        >
                            <div className="group/slider mb-2 sm:mb-3 w-full">
                                <Slider
                                    value={[currentTime]}
                                    max={duration || 1}
                                    step={0.1}
                                    onValueChange={handleSeek}
                                    className="h-2 w-full"
                                    buffered={getBufferedPercent()}
                                />
                            </div>

                            <div className="flex flex-col gap-2">
                                {/* Main controls row */}
                                <div className="flex items-center justify-between gap-2">
                                    <div className="flex items-center gap-1 sm:gap-2 text-white">
                                        <Button
                                            size="icon"
                                            variant="ghost"
                                            onClick={togglePlayPause}
                                            className="text-white hover:bg-white/20 h-8 w-8 sm:h-10 sm:w-10"
                                        >
                                            {isPlaying ? <Pause className="h-4 w-4 sm:h-5 sm:w-5" /> : <Play className="h-4 w-4 sm:h-5 sm:w-5" />}
                                        </Button>
                                        <Button
                                            size="icon"
                                            variant="ghost"
                                            onClick={() => skip(-10)}
                                            className="text-white hover:bg-white/20 h-8 w-8 sm:h-10 sm:w-10"
                                            title="Skip back 10s"
                                        >
                                            <SkipBack className="h-4 w-4 sm:h-5 sm:w-5" />
                                        </Button>
                                        <Button
                                            size="icon"
                                            variant="ghost"
                                            onClick={() => skip(10)}
                                            className="text-white hover:bg-white/20 h-8 w-8 sm:h-10 sm:w-10"
                                            title="Skip forward 10s"
                                        >
                                            <SkipForward className="h-4 w-4 sm:h-5 sm:w-5" />
                                        </Button>
                                        <div className="hidden sm:flex w-32 items-center gap-2">
                                            <Button
                                                size="icon"
                                                variant="ghost"
                                                onClick={toggleMute}
                                                className="text-white hover:bg-white/20"
                                            >
                                                {isMuted || volume === 0 ? (
                                                    <VolumeX className="h-5 w-5" />
                                                ) : (
                                                    <Volume2 className="h-5 w-5" />
                                                )}
                                            </Button>
                                            <Slider value={[volume]} max={1} step={0.05} onValueChange={handleVolumeChange} />
                                        </div>
                                        <div className="font-mono text-xs sm:text-sm tracking-tighter ml-1 sm:ml-2">
                                            {formatDuration(currentTime)} / {formatDuration(duration)}
                                        </div>
                                    </div>
                                    <div className="flex items-center gap-1 sm:gap-2">
                                        <Button
                                            size="icon"
                                            variant="ghost"
                                            onClick={toggleMute}
                                            className="text-white hover:bg-white/20 h-8 w-8 sm:h-10 sm:w-10 sm:hidden"
                                        >
                                            {isMuted || volume === 0 ? (
                                                <VolumeX className="h-4 w-4" />
                                            ) : (
                                                <Volume2 className="h-4 w-4" />
                                            )}
                                        </Button>
                                        {(transcriptionResult || subtitleUrl) && (
                                            <Button
                                                size="icon"
                                                variant="ghost"
                                                onClick={toggleSubtitles}
                                                className="text-white hover:bg-white/20 h-8 w-8 sm:h-10 sm:w-10"
                                                title={showSubtitles ? 'Hide subtitles' : 'Show subtitles'}
                                            >
                                                {showSubtitles ? (
                                                    <Captions className="h-4 w-4 sm:h-5 sm:w-5" />
                                                ) : (
                                                    <CaptionsOff className="h-4 w-4 sm:h-5 sm:w-5" />
                                                )}
                                            </Button>
                                        )}
                                        <div className="relative">
                                            <Button
                                                size="icon"
                                                variant="ghost"
                                                onClick={() => setShowSettings(!showSettings)}
                                                className="text-white hover:bg-white/20 h-8 w-8 sm:h-10 sm:w-10"
                                                title="Settings"
                                            >
                                                <Settings className="h-4 w-4 sm:h-5 sm:w-5" />
                                            </Button>
                                            {showSettings && (
                                                <div className="absolute bottom-full right-0 mb-2 min-w-32 rounded-lg bg-black/90 p-2">
                                                    <div className="mb-2 text-sm font-medium text-white">Speed</div>
                                                    {playbackRates.map(rate => (
                                                        <button
                                                            key={rate}
                                                            onClick={() => handlePlaybackRateChange(rate)}
                                                            className={`block w-full rounded px-2 py-1 text-left text-sm hover:bg-white/20 ${
                                                                playbackRate === rate ? 'bg-white/20 text-white' : 'text-gray-300'
                                                            }`}
                                                        >
                                                            {rate === 1 ? 'Normal' : `${rate}x`}
                                                        </button>
                                                    ))}
                                                </div>
                                            )}
                                        </div>
                                        <Button
                                            size="icon"
                                            variant="ghost"
                                            onClick={toggleFullscreen}
                                            className="text-white hover:bg-white/20 h-8 w-8 sm:h-10 sm:w-10"
                                            title={isFullscreen ? 'Exit fullscreen' : 'Enter fullscreen'}
                                        >
                                            {isFullscreen ? <Minimize className="h-4 w-4 sm:h-5 sm:w-5" /> : <Maximize className="h-4 w-4 sm:h-5 sm:w-5" />}
                                        </Button>
                                    </div>
                                </div>
                            </div>
                        </div>
                    </div>
                ) : (
                    <div
                        className="flex h-full w-full cursor-pointer items-center justify-center rounded-lg border-2 border-dashed"
                        onClick={() => document.getElementById('file-input')?.click()}
                    >
                        <div className="text-center text-muted-foreground my-8">
                            <Upload className="mx-auto h-12 w-12 pb-4" />
                            <p>Upload or select a file to play</p>
                        </div>
                    </div>
                )}
            </CardContent>
        </Card>
    );
}