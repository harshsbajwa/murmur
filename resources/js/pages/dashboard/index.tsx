import { useState, useCallback, useRef, useEffect } from 'react';
import axios from 'axios';
import { AlertCircle, CheckCircle, Clock, HardDrive, Loader2, Upload } from 'lucide-react';
import { useFileStorage } from './hooks/use-file-storage';
import { useFFmpeg } from './hooks/use-ffmpeg';
import { VideoPlayer } from './components/VideoPlayer';
import { FileManager } from './components/FileManager';
import { VideoTools } from './components/VideoTools';
import { Console, StatusIndicator } from './components/Console';
import { TorrentManager } from './components/TorrentManager';
import { ShareDialog } from './components/ShareDialog';
import { VideoFile, SeedingTorrent, TranscriptionResult } from '@/types';
import WebTorrent from 'webtorrent';
import { env, pipeline, Pipeline } from '@huggingface/transformers';
import { WebTorrentProvider, useWebTorrent } from './context/WebTorrentContext';
import { Badge } from '@/components/ui/badge';
import { Toaster } from 'sonner';

env.allowLocalModels = false;

const formatFileSize = (bytes: number): string => {
    if (bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KiB', 'MiB', 'GiB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return `${parseFloat((bytes / Math.pow(k, i)).toFixed(2))} ${sizes[i]}`;
};

interface ShareData {
    url: string;
    magnet: string;
    torrentName: string;
}

const getTorrentFileData = async (torrentFile: WebTorrent.TorrentFile): Promise<Uint8Array> => {
    return new Promise((resolve, reject) => {
        const chunks: Uint8Array[] = [];
        let totalLength = 0;
        
        const stream = torrentFile.createReadStream();
        
        stream.on('data', (chunk: Buffer) => {
            const uint8Chunk = new Uint8Array(chunk);
            chunks.push(uint8Chunk);
            totalLength += uint8Chunk.length;
        });
        
        stream.on('end', () => {
            const result = new Uint8Array(totalLength);
            let offset = 0;
            for (const chunk of chunks) {
                result.set(chunk, offset);
                offset += chunk.length;
            }
            resolve(result);
        });
        
        stream.on('error', (error: Error) => {
            reject(error);
        });
    });
};

const parseTranscriptionFromTorrent = (torrent: WebTorrent.Torrent): TranscriptionResult | null => {
    try {
        if (torrent.comment) {
            const comment = torrent.comment;
            if (comment.startsWith('TRANSCRIPTION:')) {
                const transcriptionData = comment.substring('TRANSCRIPTION:'.length);
                return JSON.parse(transcriptionData);
            }
        }
        return null;
    } catch (error) {
        console.error('Error parsing transcription from torrent:', error);
        return null;
    }
};

const DashboardContent = () => {
    const { client: webTorrentClient, isReady: isWebTorrentReady, isInitializing } = useWebTorrent();
    const activeTorrents = useRef<Map<string, WebTorrent.Torrent>>(new Map());

    const [logs, setLogs] = useState<string[]>(['System initializing...']);
    const [conversionProgress, setConversionProgress] = useState(0);
    const [isTranscribing, setIsTranscribing] = useState(false);
    const [transcriptionResult, setTranscriptionResult] = useState<TranscriptionResult | undefined>(undefined);
    const [isShareDialogOpen, setIsShareDialogOpen] = useState(false);
    const [shareData, setShareData] = useState<ShareData | null>(null);

    const addLog = useCallback((message: string) => {
        setLogs(prev => [...prev.slice(-100), `${new Date().toLocaleTimeString()}: ${message}`]);
    }, []);

    const { 
        uploadedFiles, 
        setUploadedFiles, 
        storageUsage, 
        saveFile, 
        deleteFile: deleteFileFromStorage,
        savePlaybackTime, 
        getPlaybackTime,
        saveSubtitles,
        getSubtitles
    } = useFileStorage();
    
    const { ffmpeg, isLoaded: isFFmpegLoaded, isLoading: isFFmpegLoading } = useFFmpeg(addLog, setConversionProgress);

    const [currentFile, setCurrentFile] = useState<VideoFile | null>(null);
    const [isDragging, setIsDragging] = useState(false);
    const transcriber = useRef<Pipeline | null>(null);
    const [isModelLoading, setIsModelLoading] = useState(false);
    const [isModelLoaded, setIsModelLoaded] = useState(false);
    const [seedingTorrents, setSeedingTorrents] = useState<SeedingTorrent[]>([]);
    const processedMagnetsRef = useRef<Set<string>>(new Set());
    const [isConsoleDialogOpen, setIsConsoleDialogOpen] = useState(false);
    const [windowWidth, setWindowWidth] = useState(typeof window !== 'undefined' ? window.innerWidth : 0);

    useEffect(() => {
        const handleResize = () => setWindowWidth(window.innerWidth);
        window.addEventListener('resize', handleResize);
        return () => window.removeEventListener('resize', handleResize);
    }, []);

    const isLargeScreen = windowWidth >= 600;

    const isTorrentInstance = (obj: any): obj is WebTorrent.Torrent => {
        return obj && typeof obj === 'object' && typeof obj.infoHash === 'string' && typeof obj.on === 'function';
    };

    const getTorrentSafely = (client: WebTorrent.Instance, identifier: string): WebTorrent.Torrent | null => {
        try {
            const result = client.get(identifier);
            return isTorrentInstance(result) ? result : null;
        } catch (error) {
            console.error('Error getting torrent:', error);
            return null;
        }
    };
    
    useEffect(() => {
        if (isWebTorrentReady) {
            addLog('WebTorrent client ready.');
        } else if (isInitializing) {
            addLog('WebTorrent client initializing...');
        }
    }, [isWebTorrentReady, isInitializing, addLog]);

    useEffect(() => {
        const initAI = async () => {
            if (transcriber.current || isModelLoading) return;
            setIsModelLoading(true);
            addLog('Initializing AI model for transcription...');
            try {
                transcriber.current = await pipeline('automatic-speech-recognition', 'Xenova/whisper-base');
                setIsModelLoaded(true); 
                addLog('AI model loaded successfully.');
            } catch (e) { 
                addLog(`Failed to load AI model: ${e}`); 
            } finally { 
                setIsModelLoading(false); 
            }
        };
        initAI();
    }, [addLog, isModelLoading]);

    const getConsoleStatus = useCallback(() => {
        if (isFFmpegLoading || isModelLoading || isTranscribing || conversionProgress > 0) {
          return 'loading';
        }
        return 'ready';
    }, [isFFmpegLoading, isModelLoading, isTranscribing, conversionProgress, isFFmpegLoaded, isModelLoaded]);
    
    const consoleStatus = getConsoleStatus();

    const handleStreamRequest = useCallback(async (torrent: WebTorrent.Torrent, file: WebTorrent.TorrentFile) => {
        addLog(`Setting up stream for ${file.name}...`);
        
        activeTorrents.current.set(torrent.infoHash, torrent);

        const newFile: VideoFile = {
            id: `torrent-${torrent.infoHash}-${file.path}`,
            name: file.name,
            size: file.length,
            type: file.name.endsWith('.mp4') ? 'video/mp4' : 
                  file.name.endsWith('.webm') ? 'video/webm' : 
                  file.name.endsWith('.mkv') ? 'video/x-matroska' : 'video/mp4',
            url: '', 
            lastModified: Date.now(),
            isTorrent: true,
            magnetURI: torrent.magnetURI,
            infoHash: torrent.infoHash,
            torrentFilePath: file.path,
            saved: false,
        };

        setUploadedFiles(prev => {
            const existing = prev.find(f => f.id === newFile.id);
            if (existing) {
                return prev.map(f => f.id === newFile.id ? newFile : f);
            }
            return [...prev, newFile];
        });
        
        setCurrentFile(newFile);
        addLog(`Stream ready for ${file.name}`);

        // Check for embedded transcription
        const embeddedTranscription = parseTranscriptionFromTorrent(torrent);
        if (embeddedTranscription) {
            addLog(`Found embedded transcription for ${file.name}`);
            setTranscriptionResult(embeddedTranscription);
            // Save to cache for future use
            await saveSubtitles(newFile.id, embeddedTranscription, false);
        }

        const progressInterval = setInterval(() => {
            if (!torrent || torrent.progress === 1) {
                clearInterval(progressInterval);
                if (torrent && torrent.progress === 1) {
                    addLog(`${file.name} download completed.`);
                }
                return;
            }
            const progress = (torrent.progress * 100).toFixed(1);
            const downloadSpeed = formatFileSize(torrent.downloadSpeed);
            const uploadSpeed = formatFileSize(torrent.uploadSpeed);
            addLog(`${file.name}: ${progress}% | ↓${downloadSpeed}/s ↑${uploadSpeed}/s | Peers: ${torrent.numPeers}`);
        }, 5000);

        torrent.once('error', (err: Error | string) => {
            clearInterval(progressInterval);
            addLog(`Error with ${file.name}: ${err.toString()}`);
        });

        torrent.once('done', () => {
            clearInterval(progressInterval);
            addLog(`Torrent for ${file.name} download finished.`);
        });

    }, [addLog, setUploadedFiles, saveSubtitles]);

    const handleFileSelect = useCallback(async (file: VideoFile) => {
        if (file.isTorrent && webTorrentClient && file.magnetURI && !activeTorrents.current.has(file.infoHash!)) {
            addLog(`Re-initializing torrent for ${file.name}...`);
            
            const onTorrentReady = (torrent: WebTorrent.Torrent) => {
                const torrentFile = torrent.files.find(f => f.path === file.torrentFilePath);
                if (torrentFile) {
                    activeTorrents.current.set(torrent.infoHash, torrent);
                    setCurrentFile(file);
                    addLog(`Torrent reinitialized for ${file.name}`);
                } else {
                    addLog(`Could not find file path ${file.torrentFilePath} in rehydrated torrent.`);
                }
            };
            
            const existingTorrent = getTorrentSafely(webTorrentClient, file.magnetURI);
            if (existingTorrent) {
                if (existingTorrent.ready) {
                    onTorrentReady(existingTorrent);
                } else {
                    existingTorrent.once('ready', () => onTorrentReady(existingTorrent));
                }
            } else {
                const newTorrent = webTorrentClient.add(file.magnetURI, {
                    announce: [
                        'wss://tracker.webtorrent.dev',
                        'wss://tracker.openwebtorrent.com',
                        'wss://tracker.btorrent.xyz'
                    ]
                });
                
                newTorrent.once('ready', () => onTorrentReady(newTorrent));
            }
        } else {
            setCurrentFile(file);
        }
        
        // Load existing transcription if available
        const existingTranscription = await getSubtitles(file.id);
        if (existingTranscription) {
            setTranscriptionResult(existingTranscription);
            addLog(`Loaded existing transcription for ${file.name}`);
        } else {
            setTranscriptionResult(undefined);
        }
    }, [webTorrentClient, addLog, getSubtitles]);

    const handleDeleteFile = useCallback((fileId: string) => {
        const fileToDelete = uploadedFiles.find(f => f.id === fileId);
        if (currentFile?.id === fileId) setCurrentFile(null);
        
        if (fileToDelete?.isTorrent && fileToDelete.infoHash) {
            activeTorrents.current.delete(fileToDelete.infoHash);
        }

        deleteFileFromStorage(fileId);
        addLog(`Deleted file from manager.`);
    }, [currentFile, deleteFileFromStorage, addLog, uploadedFiles]);

    useEffect(() => {
        if (!isWebTorrentReady || !webTorrentClient) return;
        
        const urlParams = new URLSearchParams(window.location.search);
        const magnetURI = urlParams.get('magnet');
        if (!magnetURI) return;

        const decodedMagnetURI = decodeURIComponent(magnetURI);
        
        if (processedMagnetsRef.current.has(decodedMagnetURI)) {
            return;
        }
        
        processedMagnetsRef.current.add(decodedMagnetURI);
        
        addLog(`Processing magnet link from URL...`);
        
        const newUrl = new URL(window.location.href);
        newUrl.searchParams.delete('magnet');
        window.history.replaceState({}, document.title, newUrl.toString());
        
        if (!decodedMagnetURI.startsWith('magnet:?xt=urn:btih:')) {
            addLog('Invalid magnet URI format');
            return;
        }

        const setupStream = (torrent: WebTorrent.Torrent) => {
            addLog(`Torrent "${torrent.name}" ready with ${torrent.files.length} files.`);
            
            const videoExtensions = /\.(mp4|webm|mov|mkv|avi|m4v|flv|wmv|ogv|3gp|ts|m2ts)$/i;
            const videoFile = torrent.files
                .filter(f => videoExtensions.test(f.name))
                .sort((a, b) => b.length - a.length)[0];
            
            if (videoFile) {
                addLog(`Selected video file: ${videoFile.name} (${formatFileSize(videoFile.length)})`);
                
                videoFile.select();
                handleStreamRequest(torrent, videoFile);
            } else {
                addLog('No suitable video file found in torrent.');
                const availableFiles = torrent.files.map(f => `${f.name} (${formatFileSize(f.length)})`).join(', ');
                addLog(`Available files: ${availableFiles}`);
            }
        };
        
        const handleTorrentSetup = (torrent: WebTorrent.Torrent) => {
            addLog(`Torrent "${torrent.name || 'loading...'}" added to client.`);
            
            let setupCompleted = false;
            
            const onReady = () => {
                if (setupCompleted) return;
                setupCompleted = true;
                setupStream(torrent);
            };
            
            if (torrent.ready) {
                onReady();
            } else {
                torrent.once('ready', onReady);
                
                torrent.once('metadata', () => {
                    if (!setupCompleted) {
                        addLog('Torrent metadata received, setting up stream...');
                        onReady();
                    }
                });
                
                const readyTimeout = setTimeout(() => {
                    if (!setupCompleted) {
                        addLog('Torrent taking longer than expected. Checking for peers...');
                        addLog(`Peers: ${torrent.numPeers}, Progress: ${(torrent.progress * 100).toFixed(1)}%`);
                        
                        if (torrent.files.length > 0) {
                            addLog('Attempting to set up stream with available metadata...');
                            onReady();
                        }
                    }
                }, 15000);
                
                torrent.once('ready', () => clearTimeout(readyTimeout));
            }
        };
        
        const existingTorrent = getTorrentSafely(webTorrentClient, decodedMagnetURI);
        
        if (existingTorrent) {
            addLog(`Using existing torrent instance.`);
            handleTorrentSetup(existingTorrent);
        } else {
            addLog(`Adding new torrent to client...`);
            try {
                const torrent = webTorrentClient.add(decodedMagnetURI, {
                    announce: [
                        'wss://tracker.webtorrent.dev',
                        'wss://tracker.openwebtorrent.com',
                        'wss://tracker.btorrent.xyz'
                    ]
                });
                
                handleTorrentSetup(torrent);
                
            } catch (error) {
                addLog(`Failed to add torrent: ${error}`);
                console.error('Error adding torrent:', error);
            }
        }
    }, [isWebTorrentReady, webTorrentClient, addLog, handleStreamRequest]);
    
    const handleFileUpload = useCallback((files: FileList | null) => {
        if (!files || files.length === 0) return;
        
        const newFiles: VideoFile[] = Array.from(files)
            .filter(f => f.type.startsWith('video/'))
            .map(f => ({
                id: crypto.randomUUID(), 
                name: f.name, 
                size: f.size, 
                type: f.type,
                url: URL.createObjectURL(new Blob([f], { type: f.type })),
                lastModified: f.lastModified, 
                originalFile: f, 
                isTorrent: false,
            }));
        
        if (newFiles.length > 0) {
            setUploadedFiles(prev => [...prev, ...newFiles]);
            if (!currentFile) handleFileSelect(newFiles[0]);
            addLog(`Uploaded ${newFiles.length} new file(s).`);
        }
    }, [currentFile, handleFileSelect, addLog, setUploadedFiles]);

    const handleShare = useCallback(async (file: VideoFile) => {
        if (!webTorrentClient) { 
            addLog('WebTorrent client not ready.'); 
            return; 
        }
        
        let existingTorrent: WebTorrent.Torrent | null = null;
        
        // Check for an existing torrent using infoHash from multiple sources
        const infoHashToCheck = file.isTorrent ? file.infoHash : file.seedingInfoHash;
        if (infoHashToCheck) {
            existingTorrent = activeTorrents.current.get(infoHashToCheck) ?? getTorrentSafely(webTorrentClient, infoHashToCheck);
        }

        if (existingTorrent) {
            addLog(`Already seeding ${file.name}. Opening share dialog.`);
            setShareData({ 
                url: `${window.location.origin}/dashboard?magnet=${encodeURIComponent(existingTorrent.magnetURI)}`, 
                magnet: existingTorrent.magnetURI, 
                torrentName: existingTorrent.name 
            });
            setIsShareDialogOpen(true);
            return;
        }

        addLog(`Starting to seed ${file.name}...`);
        
        try {
            const blobToSeed = file.originalFile ? file.originalFile : await fetch(file.url).then(res => res.blob());
            const fileToSeed = new File([blobToSeed], file.name, { type: file.type });

            const existingTranscription = await getSubtitles(file.id);
            let seedOptions: any = {
                announce: [
                    'wss://tracker.webtorrent.dev',
                    'wss://tracker.openwebtorrent.com',
                    'wss://tracker.btorrent.xyz'
                ]
            };

            if (existingTranscription) {
                addLog(`Including transcription in torrent metadata for ${file.name}`);
                seedOptions.comment = `TRANSCRIPTION:${JSON.stringify(existingTranscription)}`;
            }

            webTorrentClient.seed(fileToSeed, seedOptions, async (torrent) => {
                addLog(`Seeding ${torrent.name} successfully. Info Hash: ${torrent.infoHash}`);
                
                activeTorrents.current.set(torrent.infoHash, torrent);

                // If the original file was not a torrent, link it via seedingInfoHash
                if (!file.isTorrent) {
                    setUploadedFiles(prev => prev.map(f => 
                        f.id === file.id ? { ...f, seedingInfoHash: torrent.infoHash } : f
                    ));
                }

                const newSeedingTorrent: SeedingTorrent = { 
                    infoHash: torrent.infoHash, 
                    name: torrent.name, 
                    length: torrent.length, 
                    files: torrent.files.map(f => ({ name: f.name, length: f.length, path: f.path })) 
                };
                
                try {
                    const trackers = torrent.announce || [];
                    await axios.post('/api/torrents', { 
                        info_hash: torrent.infoHash, 
                        name: torrent.name, 
                        size: torrent.length, 
                        trackers: trackers 
                    }, { withCredentials: true });
                    
                    addLog(`Registered ${torrent.name} with the server.`);
                    setSeedingTorrents(prev => [...prev, newSeedingTorrent]);
                    setShareData({ 
                        url: `${window.location.origin}/dashboard?magnet=${encodeURIComponent(torrent.magnetURI)}`, 
                        magnet: torrent.magnetURI, 
                        torrentName: torrent.name 
                    });
                    setIsShareDialogOpen(true);
                } catch (error) {
                    addLog(`Failed to register torrent: ${error}`);
                    console.error('Registration error:', error);
                }
            });
        } catch (error) {
            addLog(`Failed to create torrent: ${error}`);
            console.error('Seeding error:', error);
        }
    }, [webTorrentClient, addLog, getSubtitles, setUploadedFiles]);

    const handleConvert = async (format: string) => {
        if (!ffmpeg || !currentFile) {
            addLog("Conversion requires FFmpeg to be loaded and a file to be selected.");
            return;
        }

        addLog(`Starting conversion of ${currentFile.name} to ${format}...`);

        try {
            const { fetchFile } = await import(/* @vite-ignore */ `${window.location.origin}/vendor/ffmpeg_util/index.js`);
            let fileData: Uint8Array;

            if (currentFile.isTorrent && currentFile.infoHash) {
                const torrent = activeTorrents.current.get(currentFile.infoHash);
                const torrentFile = torrent?.files.find(f => f.path === currentFile.torrentFilePath);

                if (!torrentFile) {
                    throw new Error('Could not find active torrent file to convert.');
                }
                
                addLog('Extracting data from torrent stream for conversion...');
                fileData = await getTorrentFileData(torrentFile);
            } else if (!currentFile.isTorrent) {
                fileData = await fetchFile(currentFile.originalFile || currentFile.url);
            } else {
                throw new Error('No file data available to convert.');
            }

            const inputFileName = `input.${currentFile.name.split('.').pop() || 'media'}`;
            const outputFileName = `output.${format}`;

            await ffmpeg.writeFile(inputFileName, fileData);
            await ffmpeg.exec(['-i', inputFileName, outputFileName]);
            const data = await ffmpeg.readFile(outputFileName);

            const outputFile: VideoFile = {
                id: crypto.randomUUID(),
                name: `${currentFile.name.split('.').slice(0, -1).join('.')}.${format}`,
                size: (data as Uint8Array).byteLength,
                type: `video/${format}`,
                url: URL.createObjectURL(new Blob([(data as Uint8Array).buffer], { type: `video/${format}` })),
                lastModified: Date.now(),
                isTorrent: false,
            };

            setUploadedFiles((prev) => [...prev, outputFile]);
            addLog(`Conversion successful: ${outputFile.name}`);
        } catch (error) {
            addLog(`Conversion failed: ${error}`);
            console.error('Conversion error:', error);
        }
    };

    const handleTranscribe = useCallback(
        async (language: string) => {
            if (!transcriber.current || !ffmpeg || !currentFile) return;
    
            setIsTranscribing(true);
            setTranscriptionResult(undefined);
            setConversionProgress(0);
            addLog(`Starting transcription for ${currentFile.name}...`);
    
            try {
                const { fetchFile } = await import(/* @vite-ignore */ `${window.location.origin}/vendor/ffmpeg_util/index.js`);
                
                let fileData: Uint8Array;

                if (currentFile.isTorrent && currentFile.infoHash) {
                    const torrent = activeTorrents.current.get(currentFile.infoHash);
                    const torrentFile = torrent?.files.find(f => f.path === currentFile.torrentFilePath);

                    if (!torrentFile) throw new Error('Could not find active torrent file to transcribe.');
                    
                    addLog('Extracting audio from torrent stream...');
                    fileData = await getTorrentFileData(torrentFile);
                } else if (!currentFile.isTorrent) {
                    fileData = await fetchFile(currentFile.url);
                } else { 
                    throw new Error('No file data available to transcribe'); 
                }
                
                await ffmpeg.writeFile('input.media', fileData);

                await ffmpeg.exec([
                    '-i', 
                    'input.media', 
                    '-ar', 
                    '16000', 
                    '-ac', 
                    '1', 
                    '-c:a', 
                    'pcm_s16le', 
                    'output.wav'
                ]);
                const audioData = await ffmpeg.readFile('output.wav');
                const audioBuffer = new Int16Array((audioData as Uint8Array).buffer);
                const float32Array = new Float32Array(audioBuffer.length);
                
                for (let i = 0; i < audioBuffer.length; i++) {
                    float32Array[i] = audioBuffer[i] / 32768.0;
                }
    
                addLog('Audio extracted. Transcribing with Whisper model...');
                const output = (await transcriber.current(float32Array, { 
                    chunk_length_s: 30, 
                    stride_length_s: 5, 
                    return_timestamps: true, 
                    language: language, 
                    task: 'transcribe' 
                })) as TranscriptionResult;
    
                addLog('Transcription completed.');
                setTranscriptionResult(output);
            
                await saveSubtitles(currentFile.id, output, true);
                addLog('Transcription saved to cache.');
                
            } catch (error) { 
                addLog(`Transcription failed: ${error}`); 
                console.error('Transcription error:', error); 
            } finally { 
                setIsTranscribing(false); 
                setConversionProgress(0); 
            }
        },
        [ffmpeg, currentFile, addLog, saveSubtitles],
    );

    const handleStopSeeding = useCallback(async (infoHash: string) => {
        if (!webTorrentClient) return;
        
        try {
            await axios.delete(`/api/torrents/${infoHash}`, { withCredentials: true });
            
            const torrent = getTorrentSafely(webTorrentClient, infoHash);
            if (torrent) {
                torrent.destroy();
                activeTorrents.current.delete(infoHash);
                addLog(`Stopped seeding and destroyed torrent ${infoHash}.`);
            }
            
            setSeedingTorrents((prev) => prev.filter((t) => t.infoHash !== infoHash));

            // Unlink the seedingInfoHash from the original file
            setUploadedFiles(prev => prev.map(f => 
                f.seedingInfoHash === infoHash ? { ...f, seedingInfoHash: undefined } : f
            ));
            addLog(`Unlinked local file from seeding torrent ${infoHash}.`);

        } catch (error) {
            console.error('Failed to stop seeding:', error);
            addLog(`Failed to stop seeding torrent ${infoHash}: ${error}`);
        }
    }, [webTorrentClient, addLog, setUploadedFiles]);

    const dropzoneProps = {
        onDragOver: (e: React.DragEvent) => { e.preventDefault(); setIsDragging(true); },
        onDragLeave: (e: React.DragEvent) => { e.preventDefault(); setIsDragging(false); },
        onDrop: (e: React.DragEvent) => { e.preventDefault(); setIsDragging(false); handleFileUpload(e.dataTransfer.files); },
    };

    const currentTorrentInstance = currentFile?.isTorrent && currentFile.infoHash
        ? activeTorrents.current.get(currentFile.infoHash)
        : undefined;
        
        return (
            <div className="space-y-6 p-6" {...dropzoneProps}>
              {shareData && (
                <ShareDialog
                  open={isShareDialogOpen}
                  onOpenChange={setIsShareDialogOpen}
                  shareUrl={shareData.url}
                  magnetLink={shareData.magnet}
                  torrentName={shareData.torrentName}
                />
              )}
              
              <div className="flex flex-col sm:flex-row sm:items-center sm:justify-between">
                <div className="w-full text-center mb-2 sm:mb-0 sm:text-left">
                    <h1 className="text-2xl font-bold">murmur</h1>
                </div>

                <div className="flex items-center gap-2 justify-center sm:justify-end w-full sm:w-auto">
                  <Badge variant={isFFmpegLoaded ? 'default' : 'secondary'} title="FFmpeg Status">
                    {isFFmpegLoading ? (
                      <Loader2 className="mr-1 h-3 w-3 animate-spin" />
                    ) : isFFmpegLoaded ? (
                      <CheckCircle className="mr-1 h-3 w-3" />
                    ) : (
                      <AlertCircle className="mr-1 h-3 w-3" />
                    )}
                    FFmpeg
                  </Badge>
                  
                  <Badge variant={isModelLoaded ? 'default' : 'secondary'} title="Whisper Status">
                    {isModelLoading ? (
                      <Loader2 className="mr-1 h-3 w-3 animate-spin" />
                    ) : isModelLoaded ? (
                      <CheckCircle className="mr-1 h-3 w-3" />
                    ) : (
                      <AlertCircle className="mr-1 h-3 w-3" />
                    )}
                    Whisper
                  </Badge>
                  
                  <Badge variant="outline">
                    <HardDrive className="mr-1 h-3 w-3" />
                    {storageUsage.toFixed(1)} MiB
                  </Badge>
                  
                  {isLargeScreen && (
                    <Badge 
                      variant="outline" 
                      className="cursor-pointer hover:bg-muted/50 transition-colors"
                      onClick={() => setIsConsoleDialogOpen(true)}
                    >
                      <StatusIndicator status={consoleStatus} />
                      <Clock className="ml-2 mr-1 h-3 w-3" />
                      Console
                    </Badge>
                  )}
                </div>
              </div>
        
              <div className={`pointer-events-none fixed inset-0 z-50 flex items-center justify-center border-4 border-dashed border-primary bg-primary/20 transition-opacity ${isDragging ? 'opacity-100' : 'opacity-0'}`}>
                <div className="text-center">
                  <Upload className="mx-auto mb-4 h-16 w-16 text-white" />
                  <p className="text-2xl font-bold text-white">Drop Files Anywhere to Upload</p>
                </div>
              </div>
        
              <input 
                id="file-input" 
                type="file" 
                multiple 
                accept="video/*" 
                className="hidden" 
                onChange={(e) => handleFileUpload(e.target.files)} 
              />
        
              <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
                <VideoPlayer
                  currentFile={currentFile}
                  onTimeUpdate={savePlaybackTime}
                  getPlaybackTime={getPlaybackTime}
                  transcriptionResult={transcriptionResult}
                  torrentInstance={currentTorrentInstance}
                />
                <VideoTools
                  isReady={!!currentFile && isFFmpegLoaded && isModelLoaded}
                  onConvert={handleConvert}
                  onTranscribe={handleTranscribe}
                  currentFileName={currentFile?.name}
                  transcriptionResult={transcriptionResult}
                  isTranscribing={isTranscribing}
                  conversionProgress={conversionProgress}
                />
              </div>
        
              <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
                <FileManager
                  files={uploadedFiles}
                  currentFileId={currentFile?.id || null}
                  onFileSelect={handleFileSelect}
                  onFileSave={saveFile}
                  onFileDelete={handleDeleteFile}
                  onFileShare={handleShare}
                  onFileUpload={handleFileUpload}
                />
                <TorrentManager
                  seedingTorrents={seedingTorrents}
                  onStreamRequest={handleStreamRequest}
                  onStopSeeding={handleStopSeeding}
                />
              </div>
        
              {/* Console - Accordion for smaller screens */}
              {!isLargeScreen && (
                <Console 
                    logs={logs} 
                    variant='accordion'
                    status={consoleStatus} 
                />
              )}
        
              {/* Console - Dialog for larger screens */}
              {isLargeScreen && (
                <Console
                    logs={logs}
                    variant='dialog'
                    open={isConsoleDialogOpen}
                    onOpenChange={setIsConsoleDialogOpen}
                    status={consoleStatus}
                />
              )}
            </div>
        );
};

export default function Dashboard() {
    return (
        <WebTorrentProvider>
            <DashboardContent />
            <Toaster position="top-right" />
        </WebTorrentProvider>
    );
}