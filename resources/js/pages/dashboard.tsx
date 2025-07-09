import { useState, useRef, useEffect, useCallback } from 'react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Badge } from '@/components/ui/badge';
import { Alert, AlertDescription } from '@/components/ui/alert';
import { Slider } from '@/components/ui/slider';
import { 
  Upload, 
  Film, 
  Loader2, 
  Download, 
  Save, 
  Trash2, 
  Play, 
  Pause,
  Volume2,
  VolumeX,
  Maximize,
  Settings,
  FileVideo,
  Clock,
  HardDrive,
  CheckCircle,
  XCircle,
  AlertCircle,
  StopCircle,
  Minimize
} from 'lucide-react';

interface VideoFile {
  id: string;
  name: string;
  size: number;
  type: string;
  url: string;
  duration?: number;
  saved?: boolean;
  lastModified: number;
  originalFile?: File;
  playbackTime?: number;
}

interface ConversionJob {
  id: string;
  inputFile: VideoFile;
  outputFormat: string;
  status: 'pending' | 'processing' | 'completed' | 'error' | 'cancelled';
  progress: number;
  outputFile?: VideoFile;
  error?: string;
  cancelled?: boolean;
}

type LogEventCallback = (event: { type: string, message: string }) => void;

const formatFileSize = (bytes: number): string => {
  if (bytes === 0) return '0 Bytes';
  const k = 1024;
  const sizes = ['Bytes', 'KiB', 'MiB', 'GiB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
};

const formatDuration = (seconds: number): string => {
  const minutes = Math.floor(seconds / 60);
  const remainingSeconds = Math.floor(seconds % 60);
  return `${minutes}:${remainingSeconds.toString().padStart(2, '0')}`;
};

const truncateFileName = (fileName: string, maxLength: number = 30): string => {
  if (fileName.length <= maxLength) return fileName;
  const extension = fileName.split('.').pop();
  const nameWithoutExt = fileName.substring(0, fileName.lastIndexOf('.'));
  const truncatedName = nameWithoutExt.substring(0, maxLength - extension!.length - 4) + '...';
  return `${truncatedName}.${extension}`;
};

const openDB = (): Promise<IDBDatabase> => {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open('VideoTranscoder', 1);
    
    request.onerror = () => reject(request.error);
    request.onsuccess = () => resolve(request.result);
    
    request.onupgradeneeded = (event) => {
      const db = (event.target as IDBOpenDBRequest).result;
      if (!db.objectStoreNames.contains('videos')) {
        db.createObjectStore('videos', { keyPath: 'id' });
      }
    };
  });
};

const saveToIndexedDB = async (file: VideoFile, blob: Blob): Promise<void> => {
  const db = await openDB();
  const transaction = db.transaction(['videos'], 'readwrite');
  const store = transaction.objectStore('videos');
  
  const fileData = {
    ...file,
    blob: blob,
    savedAt: Date.now()
  };
  
  await store.put(fileData);
};

const getFromIndexedDB = async (id: string): Promise<{ file: VideoFile; blob: Blob } | null> => {
  const db = await openDB();
  const transaction = db.transaction(['videos'], 'readonly');
  const store = transaction.objectStore('videos');
  
  return new Promise((resolve) => {
    const request = store.get(id);
    request.onsuccess = () => {
      const result = request.result;
      if (result) {
        resolve({ file: result, blob: result.blob });
      } else {
        resolve(null);
      }
    };
    request.onerror = () => resolve(null);
  });
};

const getAllFromIndexedDB = async (): Promise<VideoFile[]> => {
  const db = await openDB();
  const transaction = db.transaction(['videos'], 'readonly');
  const store = transaction.objectStore('videos');
  
  return new Promise((resolve) => {
    const request = store.getAll();
    request.onsuccess = () => {
      const results = request.result || [];
      const files = results.map(result => ({
        ...result,
        url: URL.createObjectURL(result.blob),
        saved: true
      }));
      resolve(files);
    };
    request.onerror = () => resolve([]);
  });
};

const deleteFromIndexedDB = async (id: string): Promise<void> => {
  const db = await openDB();
  const transaction = db.transaction(['videos'], 'readwrite');
  const store = transaction.objectStore('videos');
  await store.delete(id);
};

const savePlaybackTime = async (id: string, time: number): Promise<void> => {
  const db = await openDB();
  const transaction = db.transaction(['videos'], 'readwrite');
  const store = transaction.objectStore('videos');
  const request = store.get(id);

  return new Promise((resolve, reject) => {
    request.onerror = () => reject(request.error);
    request.onsuccess = () => {
      const data = request.result;
      if (data) {
        data.playbackTime = time;
        store.put(data).onsuccess = () => resolve();
      } else {
        resolve();
      }
    };
  });
};

const getPlaybackTime = async (id: string): Promise<number | undefined> => {
  const db = await openDB();
  const transaction = db.transaction(['videos'], 'readonly');
  const store = transaction.objectStore('videos');
  const request = store.get(id);

  return new Promise((resolve) => {
      request.onerror = () => resolve(undefined);
      request.onsuccess = () => {
          resolve(request.result?.playbackTime);
      };
  });
};

export default function Dashboard() {
  // FFmpeg state
  const [loaded, setLoaded] = useState(false);
  const [isLoadingFFmpeg, setIsLoadingFFmpeg] = useState(false);
  const ffmpegRef = useRef<any>(null);
  const messageRef = useRef<HTMLParagraphElement | null>(null);
  const abortControllerRef = useRef<AbortController | null>(null);
  
  // File management state
  const [uploadedFiles, setUploadedFiles] = useState<VideoFile[]>([]);
  const [currentFile, setCurrentFile] = useState<VideoFile | null>(null);
  const [conversionJobs, setConversionJobs] = useState<ConversionJob[]>([]);
  
  // UI state
  const [isDragging, setIsDragging] = useState(false);
  const [outputFormat, setOutputFormat] = useState('mp4');
  const [storageUsage, setStorageUsage] = useState(0);
  const [isFullscreen, setIsFullscreen] = useState(false);
  
  // Video player state
  const [videoSrc, setVideoSrc] = useState('');
  const [isVideoVisible, setIsVideoVisible] = useState(false);
  const videoRef = useRef<HTMLVideoElement | null>(null);
  const videoContainerRef = useRef<HTMLDivElement | null>(null);
  const [isPlaying, setIsPlaying] = useState(false);
  const [isMuted, setIsMuted] = useState(false);
  const [volume, setVolume] = useState(1);
  const [currentTime, setCurrentTime] = useState(0);
  const [duration, setDuration] = useState(0);
  const seekTimeRef = useRef<number | null>(null);
  const timeThrottleRef = useRef<NodeJS.Timeout | null>(null);
  const playbackTimeCacheRef = useRef(new Map<string, number>());

  // Job state
  const activeJobIdRef = useRef<string | null>(null);
  const activeLogCallbackRef = useRef<LogEventCallback | null>(null);
  
  // Formats
  const supportedFormats = [
    { value: 'mp4', label: 'MP4', description: 'Most compatible' },
    { value: 'webm', label: 'WebM', description: 'Web optimized' },
    { value: 'avi', label: 'AVI', description: 'High quality' },
    { value: 'mov', label: 'MOV', description: 'Apple format' },
    { value: 'mkv', label: 'MKV', description: 'High quality container' }
  ];

  // Load FFmpeg and saved files on mount
  useEffect(() => {
    loadFFmpeg();
    loadSavedFiles();
  }, []);

  const updateMessage = useCallback((message: string) => {
    if (messageRef.current) {
      messageRef.current.innerHTML = message;
      const parent = messageRef.current.parentElement;
      if (parent) parent.scrollTop = parent.scrollHeight;
    }
  }, []);

  useEffect(() => {
    updateStorageUsage();
  }, [uploadedFiles]);

  useEffect(() => {
    const handleFullscreenChange = () => {
      setIsFullscreen(!!document.fullscreenElement);
    };

    document.addEventListener('fullscreenchange', handleFullscreenChange);
    return () => document.removeEventListener('fullscreenchange', handleFullscreenChange);
  }, []);

  useEffect(() => {
    const setupVideo = async () => {
      if (!currentFile) {
        setVideoSrc('');
        setIsVideoVisible(false);
        return;
      }
      setIsVideoVisible(false);

      setIsPlaying(false);
      setCurrentTime(0);
      setDuration(0);

      setVideoSrc(currentFile.url);
    };

    setupVideo();
  }, [currentFile]);
  
  const loadSavedFiles = async () => {
    try {
      const savedFiles = await getAllFromIndexedDB();
      setUploadedFiles(prev => [...prev, ...savedFiles]);
      updateMessage(`Loaded ${savedFiles.length} saved files from storage`);
    } catch (error) {
      updateMessage(`Error loading saved files: ${error}`);
    }
  };

  const loadFFmpeg = async () => {
    if (loaded || isLoadingFFmpeg) return;

    setIsLoadingFFmpeg(true);
    updateMessage('Loading FFmpeg libraries...');
    try {
      const { FFmpeg } = await import(/* @vite-ignore */ `${window.location.origin}/vendor/ffmpeg_ffmpeg/index.js`);

      const ffmpeg = new FFmpeg();
      ffmpegRef.current = ffmpeg;

      const coreURL = '/vendor/ffmpeg_core/ffmpeg-core.js';
      const wasmURL = '/vendor/ffmpeg_core/ffmpeg-core.wasm';
      await ffmpeg.load({ coreURL, wasmURL });

      setLoaded(true);
      updateMessage('FFmpeg is ready.');
    } catch (error) {
      updateMessage(`Error loading FFmpeg: ${error}`);
    } finally {
      setIsLoadingFFmpeg(false);
    }
  };
  
  const updateStorageUsage = async () => {
    if ('storage' in navigator && 'estimate' in navigator.storage) {
      const estimate = await navigator.storage.estimate();
      setStorageUsage((estimate.usage || 0) / (1024 * 1024)); // MiB
    }
  };

  const handleFileUpload = useCallback((files: FileList | null) => {
    if (!files || files.length === 0) return;
    
    const newFiles: VideoFile[] = Array.from(files)
    .filter(file => file.type.startsWith('video/'))
    .map(file => {
      const fileBlob = new Blob([file], { type: file.type });
      return {
        id: `${Date.now()}-${Math.random().toString(36).substr(2, 9)}`,
        name: file.name,
        size: file.size,
        type: file.type,
        url: URL.createObjectURL(fileBlob),
        lastModified: file.lastModified,
        originalFile: file
      } satisfies VideoFile;
    });

    if (newFiles.length > 0) {
      setUploadedFiles(prev => [...prev, ...newFiles]);
      setCurrentFile(newFiles[0]);
    }
  }, []);


  const handleDragOver = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(true);
  };

  const handleDragLeave = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(false);
  };

  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDragging(false);
    handleFileUpload(e.dataTransfer.files);
  };

  const cancelConversion = (jobId: string) => {
    if (ffmpegRef.current && activeLogCallbackRef.current) {
      ffmpegRef.current.off('log', activeLogCallbackRef.current);
      activeLogCallbackRef.current = null;
    }
    if (abortControllerRef.current) {
      abortControllerRef.current.abort();
    }
    setConversionJobs(prev => prev.map(j => j.id === jobId ? { ...j, status: 'cancelled' as const } : j));
    updateMessage('Conversion cancelled by user.');
  };

  const startConversion = async (file: VideoFile, format: string) => {
    if (!ffmpegRef.current || !loaded) {
      updateMessage('FFmpeg not loaded yet. Please wait...');
      return;
    }
    const jobId = `job-${Date.now()}`;
    const job: ConversionJob = {
      id: jobId, inputFile: file, outputFormat: format, status: 'pending', progress: 0
    };
    setConversionJobs(prev => [...prev, job]);
    abortControllerRef.current = new AbortController();
    activeJobIdRef.current = jobId;

    const logCallback: LogEventCallback = ({ message }) => {
      updateMessage(message);
    };

    try {
      setConversionJobs(prev => prev.map(j => j.id === jobId ? { ...j, status: 'processing' } : j));
      ffmpegRef.current.on('log', logCallback);
      activeLogCallbackRef.current = logCallback;

      const { fetchFile } = await import(/* @vite-ignore */ `${window.location.origin}/vendor/ffmpeg_util/index.js`);

      const inputExtension = file.name.split('.').pop()?.toLowerCase() || 'mp4';

      const inputFileName = `input_${jobId}.${inputExtension}`;
      const outputFileName = `output_${jobId}.${format}`;

      let fileData = file.originalFile ? await fetchFile(file.originalFile) : await fetchFile(file.url);
      
      if (abortControllerRef.current?.signal.aborted) throw new DOMException('Cancelled', 'AbortError');
      await ffmpegRef.current.writeFile(inputFileName, fileData);

      const ffmpegArgs = ['-i', inputFileName];
      switch (format) {
        case 'mp4': ffmpegArgs.push('-c:v', 'libx264', '-c:a', 'aac', '-crf', '23', '-preset', 'medium'); break;
        case 'webm': ffmpegArgs.push('-c:v', 'libvpx', '-c:a', 'libvorbis', '-crf', '30', '-b:v', '1M'); break;
        default: ffmpegArgs.push('-c:v', 'libx264', '-c:a', 'aac'); break;
      }
      ffmpegArgs.push(outputFileName);
      updateMessage(`Executing: ffmpeg ${ffmpegArgs.join(' ')}`);

      await ffmpegRef.current.exec(ffmpegArgs, undefined, { signal: abortControllerRef.current.signal });
      
      const outputData = await ffmpegRef.current.readFile(outputFileName);
      const outputBlob = new Blob([outputData], { type: `video/${format}` });

      const outputFile: VideoFile = {
        id: `output-${jobId}`, name: `${file.name.split('.')[0]}.${format}`, size: outputBlob.size,
        type: `video/${format}`, url: URL.createObjectURL(outputBlob), lastModified: Date.now()
      };

      setConversionJobs(prev => prev.map(j => j.id === jobId ? { ...j, status: 'completed', progress: 100, outputFile } : j));
      setUploadedFiles(prev => [...prev, outputFile]);
      updateMessage(`Conversion completed: ${outputFile.name}`);

    } catch (error) {
      const errorMessage = String(error);
      if (!(error instanceof DOMException && error.name === 'AbortError')) {
        console.error('Conversion error:', error);
        setConversionJobs(prev => prev.map(j => j.id === jobId ? { ...j, status: 'error', error: errorMessage } : j));
        updateMessage(`Conversion failed: ${error}`);
      }
    } finally {
      if (ffmpegRef.current) ffmpegRef.current.off('log', logCallback);
      activeLogCallbackRef.current = null;
      activeJobIdRef.current = null;
    }
  };

  const saveFileToStorage = async (file: VideoFile) => {
    try {
      const response = await fetch(file.url);
      const blob = await response.blob();
      
      if (blob.size > 50 * 1024 * 1024) {
        updateMessage('File too large to save locally (50MB limit)');
        return;
      }
      
      await saveToIndexedDB(file, blob);
      
      setUploadedFiles(prev => prev.map(f => 
        f.id === file.id ? { ...f, saved: true } : f
      ));
      
      updateMessage(`File saved locally: ${file.name}`);
      updateStorageUsage();
    } catch (error) {
      updateMessage(`Failed to save file: ${error}`);
    }
  };

  const deleteFile = async (file: VideoFile) => {
    try {
      if (file.saved) {
        await deleteFromIndexedDB(file.id);
      }
      
      URL.revokeObjectURL(file.url);
      setUploadedFiles(prev => prev.filter(f => f.id !== file.id));
      
      if (currentFile?.id === file.id) {
        setCurrentFile(null);
      }
      
      updateMessage(`File deleted: ${file.name}`);
      updateStorageUsage();
    } catch (error) {
      updateMessage(`Failed to delete file: ${error}`);
    }
  };

  const downloadFile = (file: VideoFile) => {
    const a = document.createElement('a');
    a.href = file.url;
    a.download = file.name;
    a.click();
  };

  const togglePlayPause = () => {
    if (!videoRef.current) return;

    if (isPlaying) {
      videoRef.current.pause();
    } else {
      const playPromise = videoRef.current.play();

      if (playPromise !== undefined) {
        playPromise
          .then(() => {
          })
          .catch((error) => {
            console.error("Playback failed, trying muted:", error);
            if (videoRef.current) {
              videoRef.current.muted = true;
              setIsMuted(true);
              videoRef.current.play().catch(e => console.error("Muted playback also failed:", e));
            }
          });
      }
    }
  };

  const toggleMute = () => {
    if (videoRef.current) {
      const newMutedState = !isMuted;
      videoRef.current.muted = newMutedState;
      setIsMuted(newMutedState);
    }
  };

  const handleVolumeChange = (value: number[]) => {
    const newVolume = value[0];
    setVolume(newVolume);
    if (videoRef.current) {
      videoRef.current.volume = newVolume;
      if (newVolume === 0) {
        setIsMuted(true);
        videoRef.current.muted = true;
      } else if (isMuted) {
        setIsMuted(false);
        videoRef.current.muted = false;
      }
    }
  };

  const toggleFullscreen = async () => {
    if (!videoContainerRef.current) return;

    try {
      if (!document.fullscreenElement) {
        await videoContainerRef.current.requestFullscreen();
      } else {
        await document.exitFullscreen();
      }
    } catch (error) {
      console.error('Fullscreen error:', error);
    }
  };

  const handleTimeUpdate = () => {
    if (!videoRef.current || !currentFile) return;
    
    const newTime = videoRef.current.currentTime;
    setCurrentTime(newTime);
    playbackTimeCacheRef.current.set(currentFile.id, newTime);
    
    if (currentFile.saved && timeThrottleRef.current === null) {
      timeThrottleRef.current = setTimeout(() => {
        savePlaybackTime(currentFile.id, newTime);
        timeThrottleRef.current = null;
      }, 2000);
    }
  };

  const handleLoadedData = () => {
    if (videoRef.current && seekTimeRef.current) {
      videoRef.current.currentTime = seekTimeRef.current;
      seekTimeRef.current = null;
    }
  };

  const handleLoadedMetadata = async () => {
    if (!videoRef.current || !currentFile) return;
    setDuration(videoRef.current.duration);
    
    let startTime = playbackTimeCacheRef.current.get(currentFile.id);
    if (startTime === undefined) {
        startTime = await getPlaybackTime(currentFile.id);
    }

    if (startTime && startTime > 0) {
      videoRef.current.currentTime = startTime;
    } else {
      videoRef.current.currentTime = 0;
      setIsVideoVisible(true);
    }
  };

  const handlePlay = () => {
    setIsPlaying(true);
  };

  const handlePause = () => {
    setIsPlaying(false);
  };

  const handleSeek = (value: number[]) => {
    if (videoRef.current) {
      videoRef.current.currentTime = value[0];
      setCurrentTime(value[0]);
    }
  };

  const handleSeeked = () => {
    setIsVideoVisible(true);
  };
  
  const handleCanPlay = () => {
    setIsVideoVisible(true);
  };

  return (
    <div className="p-6 space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          <Film className="h-8 w-8 text-primary" />
          <div>
            <h1 className="text-2xl font-bold">Video Transcoder</h1>
            <p className="text-muted-foreground">Convert and manage videos in your browser</p>
          </div>
        </div>
        <div className="flex items-center gap-2">
          <Badge variant={loaded ? "default" : "secondary"}>
            {loaded ? <CheckCircle className="h-3 w-3 mr-1" /> : <Loader2 className="h-3 w-3 mr-1 animate-spin" />}
            {loaded ? 'Ready' : 'Loading...'}
          </Badge>
          <Badge variant="outline">
            <HardDrive className="h-3 w-3 mr-1" />
            {storageUsage.toFixed(1)} MB
          </Badge>
        </div>
      </div>

      {/* Upload Area */}
      <Card>
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <Upload className="h-5 w-5" />
            Upload Videos
          </CardTitle>
        </CardHeader>
        <CardContent>
          <div
            className={`border-2 border-dashed rounded-lg p-8 text-center transition-colors ${
              isDragging ? 'border-primary bg-primary/5' : 'border-muted-foreground/25'
            }`}
            onDragOver={handleDragOver}
            onDragLeave={handleDragLeave}
            onDrop={handleDrop}
          >
            <FileVideo className="h-12 w-12 mx-auto mb-4 text-muted-foreground" />
            <p className="text-lg font-medium mb-2">Drop video files here or click to browse</p>
            <p className="text-sm text-muted-foreground mb-4">
              Supports MP4, WebM, AVI, MOV, MKV and more
            </p>
            <Button
              onClick={() => document.getElementById('file-input')?.click()}
              variant="outline"
            >
              <Upload className="h-4 w-4 mr-2" />
              Choose Files
            </Button>
            <input
              id="file-input"
              type="file"
              multiple
              accept="video/*"
              className="hidden"
              onChange={(e) => handleFileUpload(e.target.files)}
            />
          </div>
        </CardContent>
      </Card>

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        {/* Video Player */}
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <Play className="h-5 w-5" />
              Video Player
            </CardTitle>
          </CardHeader>
          <CardContent>
            {currentFile ? (
              <div className="space-y-4">
                <div
                  ref={videoContainerRef}
                  className="relative bg-black rounded-lg overflow-hidden"
                >
                  <video
                    key={currentFile.id}
                    ref={videoRef}
                    src={videoSrc}
                    className={`w-full aspect-video transition-opacity duration-300 ${
                      isVideoVisible ? 'opacity-100' : 'opacity-0'
                    }`}
                    onTimeUpdate={handleTimeUpdate}
                    onLoadedMetadata={handleLoadedMetadata}
                    onSeeked={handleSeeked}
                    onCanPlay={handleCanPlay}
                    onPlay={handlePlay}
                    onPause={handlePause}
                  />
                  {/* Player controls */}
                  <div className={`absolute bottom-0 left-0 right-0 bg-gradient-to-t 
                                 from-black/80 to-transparent p-4 transition-opacity 
                                   ${isVideoVisible ? 'opacity-100' : 'opacity-0'}`}>
                    <div className="mb-3">
                      <Slider
                        value={[currentTime]}
                        max={duration || 100}
                        step={0.1}
                        onValueChange={handleSeek}
                        className="w-full"
                      />
                    </div>
                    <div className="flex items-center justify-between">
                      <div className="flex items-center gap-2">
                        <Button
                          size="sm"
                          variant="ghost"
                          onClick={togglePlayPause}
                          className="text-white hover:bg-white/20"
                        >
                          {isPlaying ? <Pause className="h-4 w-4" /> : <Play className="h-4 w-4" />}
                        </Button>
                        
                        <div className="flex items-center gap-2">
                          <Button
                            size="sm"
                            variant="ghost"
                            onClick={toggleMute}
                            className="text-white hover:bg-white/20"
                          >
                            {isMuted || volume === 0 ? <VolumeX className="h-4 w-4" /> : <Volume2 className="h-4 w-4" />}
                          </Button>
                          
                          <div className="w-20">
                            <Slider
                              value={[volume]}
                              max={1}
                              step={0.1}
                              onValueChange={handleVolumeChange}
                              className="w-full"
                            />
                          </div>
                        </div>
                        
                        <div className="text-white text-sm">
                          {formatDuration(currentTime)} / {formatDuration(duration)}
                        </div>
                      </div>
                      
                      <Button
                        size="sm"
                        variant="ghost"
                        onClick={toggleFullscreen}
                        className="text-white hover:bg-white/20"
                      >
                        {isFullscreen ? <Minimize className="h-4 w-4" /> : <Maximize className="h-4 w-4" />}
                      </Button>
                    </div>
                  </div>
                </div>
                
                <div className="flex flex-wrap gap-2">
                  <Badge variant="secondary" title={currentFile.name}>
                    {truncateFileName(currentFile.name)}
                  </Badge>
                  <Badge variant="outline">{formatFileSize(currentFile.size)}</Badge>
                  <Badge variant="outline">{currentFile.type}</Badge>
                </div>
              </div>
            ) : (
              <div className="aspect-video bg-muted rounded-lg flex items-center justify-center">
                <div className="text-center">
                  <FileVideo className="h-12 w-12 mx-auto mb-2 text-muted-foreground" />
                  <p className="text-muted-foreground">Select a video to preview</p>
                </div>
              </div>
            )}
          </CardContent>
        </Card>

        {/* Conversion Panel */}
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <Settings className="h-5 w-5" />
              Convert Video
            </CardTitle>
          </CardHeader>
          <CardContent className="space-y-4">
            <div>
              <label className="text-sm font-medium mb-2 block">Output Format</label>
              <Select value={outputFormat} onValueChange={setOutputFormat}>
                <SelectTrigger>
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  {supportedFormats.map(format => (
                    <SelectItem key={format.value} value={format.value} className="py-2">
                      <div className="flex flex-col">
                        <div className="font-medium">{format.label}</div>
                        <div className="text-xs text-muted-foreground">{format.description}</div>
                      </div>
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>

            <Button
              onClick={() => currentFile && startConversion(currentFile, outputFormat)}
              disabled={!currentFile || !loaded}
              className="w-full"
            >
              <Film className="h-4 w-4 mr-2" />
              Convert to {outputFormat.toUpperCase()}
            </Button>
          </CardContent>
        </Card>
      </div>

      {/* Conversion Jobs */}
      {conversionJobs.length > 0 && (
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <Clock className="h-5 w-5" />
              Conversion Jobs
            </CardTitle>
          </CardHeader>
          <CardContent>
            <div className="space-y-2">
              {conversionJobs.map(job => (
                <Card key={job.id} className="p-3">
                  <div className="flex items-center justify-between mb-2">
                    <span className="text-sm font-medium" title={job.inputFile.name}>
                      {truncateFileName(job.inputFile.name, 25)}
                    </span>
                    <div className="flex items-center gap-2">
                      <Badge variant={
                        job.status === 'completed' ? 'default' :
                        job.status === 'error' ? 'destructive' :
                        job.status === 'cancelled' ? 'secondary' : 'secondary'
                      }>
                        {job.status === 'processing' && <Loader2 className="h-3 w-3 mr-1 animate-spin" />}
                        {job.status === 'completed' && <CheckCircle className="h-3 w-3 mr-1" />}
                        {job.status === 'error' && <XCircle className="h-3 w-3 mr-1" />}
                        {job.status === 'cancelled' && <StopCircle className="h-3 w-3 mr-1" />}
                        {job.status}
                      </Badge>
                      {job.status === 'processing' && (
                        <Button
                          size="sm"
                          variant="outline"
                          onClick={() => cancelConversion(job.id)}
                        >
                          <StopCircle className="h-3 w-3 mr-1" />
                          Cancel
                        </Button>
                      )}
                      {job.status === 'completed' && job.outputFile && (
                        <div className="flex gap-2">
                          <Button
                            size="sm"
                            variant="outline"
                            onClick={() => downloadFile(job.outputFile!)}
                          >
                            <Download className="h-3 w-3 mr-1" />
                            Download
                          </Button>
                          <Button
                            size="sm"
                            variant="outline"
                            onClick={() => setCurrentFile(job.outputFile!)}
                          >
                            <Play className="h-3 w-3 mr-1" />
                            Preview
                          </Button>
                        </div>
                      )}
                    </div>
                  </div>
                  {job.error && (
                    <Alert variant="destructive" className="mt-2 p-2">
                      <AlertCircle className="h-4 w-4" />
                      <AlertDescription className="text-xs">{job.error}</AlertDescription>
                    </Alert>
                  )}
                </Card>
              ))}
            </div>
          </CardContent>
        </Card>
      )}

      <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
        {/* File Manager */}
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <FileVideo className="h-5 w-5" />
              File Manager
            </CardTitle>
          </CardHeader>
          <CardContent>
            {uploadedFiles.length > 0 ? (
              <div className="space-y-2">
                {uploadedFiles.map(file => (
                  <div
                    key={file.id}
                    className={`flex flex-col sm:flex-row sm:items-center sm:justify-between gap-3 p-3 rounded-lg border cursor-pointer transition-colors ${
                      currentFile?.id === file.id ? 'bg-primary/5 border-primary' : 'hover:bg-muted/50'
                    }`}
                    onClick={() => setCurrentFile(file)}
                  >
                    <div className="flex items-center gap-3 min-w-0">
                      <FileVideo className="h-5 w-5 text-muted-foreground flex-shrink-0" />
                      <div className="min-w-0">
                        <p className="font-medium truncate" title={file.name}>{file.name}</p>
                        <p className="text-sm text-muted-foreground">
                          {formatFileSize(file.size)} • {file.type}
                        </p>
                      </div>
                    </div>
                    <div className="flex items-center gap-2 flex-shrink-0 self-end sm:self-auto">
                      {file.saved && (
                        <Badge variant="secondary">
                          <Save className="h-3 w-3 mr-1" />
                          Saved
                        </Badge>
                      )}
                      {!file.saved && (
                        <Button
                          size="sm"
                          variant="outline"
                          onClick={(e) => { e.stopPropagation(); saveFileToStorage(file); }}
                        >
                          <Save className="h-3 w-3" />
                        </Button>
                      )}
                      <Button
                        size="sm"
                        variant="outline"
                        onClick={(e) => { e.stopPropagation(); downloadFile(file); }}
                      >
                        <Download className="h-3 w-3" />
                      </Button>
                      <Button
                        size="sm"
                        variant="outline"
                        onClick={(e) => { e.stopPropagation(); deleteFile(file); }}
                      >
                        <Trash2 className="h-3 w-3" />
                      </Button>
                    </div>
                  </div>
                ))}
              </div>
            ) : (
              <div className="text-center py-8 text-muted-foreground">
                <FileVideo className="h-12 w-12 mx-auto mb-2" />
                <p>No files uploaded yet</p>
              </div>
            )}
          </CardContent>
        </Card>

        {/* Console */}
        <Card>
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <Clock className="h-5 w-5" />
              Console
            </CardTitle>
          </CardHeader>
          <CardContent>
            <div className="bg-muted rounded-lg p-4 font-mono text-sm h-32 overflow-y-auto">
              <p ref={messageRef} className="text-muted-foreground whitespace-pre-wrap break-all">
                {loaded ? 'FFmpeg ready. Upload a video to get started.' : 'Loading FFmpeg...'}
              </p>
            </div>
          </CardContent>
        </Card>
      </div>
    </div>
  );
}