import { useState, useRef, useEffect, useCallback } from 'react';
import { Button } from '@/components/ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card';
import { Progress } from '@/components/ui/progress';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select';
import { Badge } from '@/components/ui/badge';
import { Separator } from '@/components/ui/separator';
import { Alert, AlertDescription } from '@/components/ui/alert';
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
  AlertCircle
} from 'lucide-react';
import AppLayout from '@/layouts/app-layout';
import { Head } from '@inertiajs/react';
import { BreadcrumbItem } from '@/types';

const breadcrumbs: BreadcrumbItem[] = [
    {
        title: 'Dashboard',
        href: '/dashboard',
    },
];

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
}

interface ConversionJob {
  id: string;
  inputFile: VideoFile;
  outputFormat: string;
  status: 'pending' | 'processing' | 'completed' | 'error';
  progress: number;
  outputFile?: VideoFile;
  error?: string;
}

const formatFileSize = (bytes: number): string => {
  if (bytes === 0) return '0 Bytes';
  const k = 1024;
  const sizes = ['Bytes', 'KB', 'MB', 'GB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
};

const formatDuration = (seconds: number): string => {
  const minutes = Math.floor(seconds / 60);
  const remainingSeconds = Math.floor(seconds % 60);
  return `${minutes}:${remainingSeconds.toString().padStart(2, '0')}`;
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

const deleteFromIndexedDB = async (id: string): Promise<void> => {
  const db = await openDB();
  const transaction = db.transaction(['videos'], 'readwrite');
  const store = transaction.objectStore('videos');
  await store.delete(id);
};

export default function Dashboard() {
  // FFmpeg state
  const [loaded, setLoaded] = useState(false);
  const [isLoadingFFmpeg, setIsLoadingFFmpeg] = useState(false);
  const ffmpegRef = useRef<any>(null);
  const messageRef = useRef<HTMLParagraphElement | null>(null);
  
  // File management state
  const [uploadedFiles, setUploadedFiles] = useState<VideoFile[]>([]);
  const [currentFile, setCurrentFile] = useState<VideoFile | null>(null);
  const [conversionJobs, setConversionJobs] = useState<ConversionJob[]>([]);
  
  // UI state
  const [isDragging, setIsDragging] = useState(false);
  const [outputFormat, setOutputFormat] = useState('mp4');
  const [storageUsage, setStorageUsage] = useState(0);
  
  // Video player state
  const videoRef = useRef<HTMLVideoElement | null>(null);
  const [isPlaying, setIsPlaying] = useState(false);
  const [isMuted, setIsMuted] = useState(false);
  const [currentTime, setCurrentTime] = useState(0);
  const [duration, setDuration] = useState(0);
  
  // Formats
  const supportedFormats = [
    { value: 'mp4', label: 'MP4', description: 'Most compatible' },
    { value: 'webm', label: 'WebM', description: 'Web optimized' },
    { value: 'avi', label: 'AVI', description: 'High quality' },
    { value: 'mov', label: 'MOV', description: 'Apple format' },
    { value: 'mkv', label: 'MKV', description: 'High quality container' }
  ];

  // Load FFmpeg on mount
  useEffect(() => {
    loadFFmpeg();
  }, []);

  // Update storage usage
  useEffect(() => {
    updateStorageUsage();
  }, [uploadedFiles]);

  const loadFFmpeg = async () => {
    if (loaded || isLoadingFFmpeg) return;
    
    setIsLoadingFFmpeg(true);
    updateMessage('Loading FFmpeg libraries...');

    try {
      const { FFmpeg } = await import(/* @vite-ignore */ `${window.location.origin}/vendor/ffmpeg_ffmpeg/index.js`);
      
      const ffmpeg = new FFmpeg();
      ffmpeg.on("log", ({ message }) => {
        updateMessage(message);
      });
      
      ffmpegRef.current = ffmpeg;
      
      const coreURL = '/vendor/ffmpeg_core/ffmpeg-core.js';
      const wasmURL = '/vendor/ffmpeg_core/ffmpeg-core.wasm';

      updateMessage('Loading FFmpeg core...');

      await ffmpeg.load({ coreURL, wasmURL });

      setLoaded(true);
      updateMessage('FFmpeg is ready.');
    } catch (error) {
      updateMessage(`Error loading FFmpeg: ${error}`);
    } finally {
      setIsLoadingFFmpeg(false);
    }
  };

  const updateMessage = (message: string) => {
    if (messageRef.current) {
      messageRef.current.innerHTML = message;
    }
  };

  const updateStorageUsage = async () => {
    if ('storage' in navigator && 'estimate' in navigator.storage) {
      const estimate = await navigator.storage.estimate();
      setStorageUsage((estimate.usage || 0) / (1024 * 1024)); // MB
    }
  };

  const handleFileUpload = useCallback((files: FileList | null) => {
    if (!files) return;
    
    Array.from(files).forEach(async (file) => {
      if (file.type.startsWith('video/')) {
        const fileBlob = new Blob([file], { type: file.type });
        const videoFile: VideoFile = {
          id: `${Date.now()}-${Math.random().toString(36).substr(2, 9)}`,
          name: file.name,
          size: file.size,
          type: file.type,
          url: URL.createObjectURL(fileBlob),
          lastModified: file.lastModified,
          // Store original for conversion
          originalFile: file
        };
        
        setUploadedFiles(prev => [...prev, videoFile]);
        
        // Auto-select first file
        if (!currentFile) {
          setCurrentFile(videoFile);
        }
      }
    });
  }, [currentFile]);

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

  const startConversion = async (file: VideoFile, format: string) => {
    if (!ffmpegRef.current || !loaded) {
      updateMessage('FFmpeg not loaded yet. Please wait...');
      return;
    }

    const jobId = `job-${Date.now()}`;
    const job: ConversionJob = {
      id: jobId,
      inputFile: file,
      outputFormat: format,
      status: 'pending',
      progress: 0
    };

    setConversionJobs(prev => [...prev, job]);

    try {
      // Update job status
      setConversionJobs(prev => prev.map(j => 
        j.id === jobId ? { ...j, status: 'processing' } : j
      ));

      updateMessage(`Starting conversion: ${file.name} → ${format}`);

      const { fetchFile } = await import(/* @vite-ignore */ `${window.location.origin}/vendor/ffmpeg_util/index.js`);
      
      // Get file extension for input
      const inputExtension = file.name.split('.').pop()?.toLowerCase() || 'mp4';
      const inputFileName = `input_${jobId}.${inputExtension}`;
      const outputFileName = `output_${jobId}.${format}`;
      
      // Use original file if available, otherwise fetch from URL
      let fileData;
      if (file.originalFile) {
        fileData = await fetchFile(file.originalFile);
      } else {
        fileData = await fetchFile(file.url);
      }
      
      await ffmpegRef.current.writeFile(inputFileName, fileData);
      updateMessage(`File written: ${inputFileName}`);
      
      const ffmpegArgs = ['-i', inputFileName];
      
      // Format-specific options
      switch (format) {
        case 'mp4':
          ffmpegArgs.push('-c:v', 'libx264', '-c:a', 'aac', '-crf', '23', '-preset', 'medium');
          break;
        case 'webm':
          ffmpegArgs.push('-c:v', 'libvpx', '-c:a', 'libvorbis', '-crf', '30', '-b:v', '1M');
          break;
        case 'avi':
          ffmpegArgs.push('-c:v', 'libx264', '-c:a', 'aac', '-crf', '18');
          break;
        case 'mov':
          ffmpegArgs.push('-c:v', 'libx264', '-c:a', 'aac', '-crf', '23');
          break;
        case 'mkv':
          ffmpegArgs.push('-c:v', 'libx264', '-c:a', 'aac', '-crf', '23');
          break;
        default:
          ffmpegArgs.push('-c:v', 'libx264', '-c:a', 'aac');
          break;
      }
      
      // Add output filename
      ffmpegArgs.push(outputFileName);
      
      updateMessage(`Executing: ffmpeg ${ffmpegArgs.join(' ')}`);
      
      // Execute conversion
      await ffmpegRef.current.exec(ffmpegArgs);
      
      updateMessage(`Conversion completed, reading output file...`);
      
      // Read output file
      const outputData = await ffmpegRef.current.readFile(outputFileName);
      const outputBlob = new Blob([outputData], { type: `video/${format}` });
      
      const outputFile: VideoFile = {
        id: `output-${jobId}`,
        name: `${file.name.split('.')[0]}.${format}`,
        size: outputBlob.size,
        type: `video/${format}`,
        url: URL.createObjectURL(outputBlob),
        lastModified: Date.now()
      };

      // Clean up temporary files
      try {
        await ffmpegRef.current.deleteFile(inputFileName);
        await ffmpegRef.current.deleteFile(outputFileName);
      } catch (cleanupError) {
        console.warn('Cleanup warning:', cleanupError);
      }

      // Update job as completed
      setConversionJobs(prev => prev.map(j => 
        j.id === jobId ? { ...j, status: 'completed', progress: 100, outputFile } : j
      ));

      // Add output file to the file list
      setUploadedFiles(prev => [...prev, outputFile]);

      updateMessage(`Conversion completed: ${outputFile.name} (${formatFileSize(outputFile.size)})`);
      
    } catch (error) {
      console.error('Conversion error:', error);
      setConversionJobs(prev => prev.map(j => 
        j.id === jobId ? { ...j, status: 'error', error: String(error) } : j
      ));
      updateMessage(`Conversion failed: ${error}`);
    }
  };

  const saveFileToStorage = async (file: VideoFile) => {
    try {
      const response = await fetch(file.url);
      const blob = await response.blob();
      
      // Check if file is too large
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
    if (videoRef.current) {
      if (isPlaying) {
        videoRef.current.pause();
      } else {
        videoRef.current.play();
      }
      setIsPlaying(!isPlaying);
    }
  };

  const toggleMute = () => {
    if (videoRef.current) {
      videoRef.current.muted = !isMuted;
      setIsMuted(!isMuted);
    }
  };

  const handleTimeUpdate = () => {
    if (videoRef.current) {
      setCurrentTime(videoRef.current.currentTime);
    }
  };

  const handleLoadedMetadata = () => {
    if (videoRef.current) {
      setDuration(videoRef.current.duration);
    }
  };

  return (
    <AppLayout breadcrumbs={breadcrumbs}>
        <Head title="Video Transcoder" />
        <div className="p-6 space-y-6">
        {/* Header */}
        <div className="flex items-center justify-between">
            <div className="flex items-center gap-3">
            <Film className="h-8 w-8 text-primary" />
            <div>
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

        {/* Main Content */}
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
                    <div className="relative bg-black rounded-lg overflow-hidden">
                    <video
                        ref={videoRef}
                        src={currentFile.url}
                        className="w-full aspect-video"
                        onTimeUpdate={handleTimeUpdate}
                        onLoadedMetadata={handleLoadedMetadata}
                        onPlay={() => setIsPlaying(true)}
                        onPause={() => setIsPlaying(false)}
                    />
                    <div className="absolute bottom-4 left-4 right-4 bg-black/50 rounded-lg p-2">
                        <div className="flex items-center gap-2">
                        <Button
                            size="sm"
                            variant="ghost"
                            onClick={togglePlayPause}
                            className="text-white hover:bg-white/20"
                        >
                            {isPlaying ? <Pause className="h-4 w-4" /> : <Play className="h-4 w-4" />}
                        </Button>
                        <Button
                            size="sm"
                            variant="ghost"
                            onClick={toggleMute}
                            className="text-white hover:bg-white/20"
                        >
                            {isMuted ? <VolumeX className="h-4 w-4" /> : <Volume2 className="h-4 w-4" />}
                        </Button>
                        <div className="flex-1 text-white text-xs">
                            {formatDuration(currentTime)} / {formatDuration(duration)}
                        </div>
                        </div>
                    </div>
                    </div>
                    <div className="flex flex-wrap gap-2">
                    <Badge variant="secondary">{currentFile.name}</Badge>
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
                        <SelectItem key={format.value} value={format.value}>
                        <div>
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

                {/* Conversion Jobs */}
                {conversionJobs.length > 0 && (
                <div className="space-y-2">
                    <h4 className="font-medium">Conversion Jobs</h4>
                    {conversionJobs.map(job => (
                    <Card key={job.id} className="p-3">
                        <div className="flex items-center justify-between mb-2">
                        <span className="text-sm font-medium">{job.inputFile.name}</span>
                        <Badge variant={
                            job.status === 'completed' ? 'default' :
                            job.status === 'error' ? 'destructive' : 'secondary'
                        }>
                            {job.status === 'processing' && <Loader2 className="h-3 w-3 mr-1 animate-spin" />}
                            {job.status === 'completed' && <CheckCircle className="h-3 w-3 mr-1" />}
                            {job.status === 'error' && <XCircle className="h-3 w-3 mr-1" />}
                            {job.status}
                        </Badge>
                        </div>
                        {job.status === 'processing' && (
                        <Progress value={job.progress} className="mb-2" />
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
                        {job.error && (
                        <Alert className="mt-2">
                            <AlertCircle className="h-4 w-4" />
                            <AlertDescription>{job.error}</AlertDescription>
                        </Alert>
                        )}
                    </Card>
                    ))}
                </div>
                )}
            </CardContent>
            </Card>
        </div>

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
                    className={`flex items-center justify-between p-3 rounded-lg border cursor-pointer transition-colors ${
                        currentFile?.id === file.id ? 'bg-primary/5 border-primary' : 'hover:bg-muted/50'
                    }`}
                    onClick={() => setCurrentFile(file)}
                    >
                    <div className="flex items-center gap-3">
                        <FileVideo className="h-5 w-5 text-muted-foreground" />
                        <div>
                        <p className="font-medium">{file.name}</p>
                        <p className="text-sm text-muted-foreground">
                            {formatFileSize(file.size)} • {file.type}
                        </p>
                        </div>
                    </div>
                    <div className="flex items-center gap-2">
                        {file.saved && (
                        <Badge variant="secondary">
                            <Save className="h-3 w-3 mr-1" />
                            Saved
                        </Badge>
                        )}
                        <Button
                        size="sm"
                        variant="outline"
                        onClick={(e) => {
                            e.stopPropagation();
                            downloadFile(file);
                        }}
                        >
                        <Download className="h-3 w-3" />
                        </Button>
                        {!file.saved && (
                        <Button
                            size="sm"
                            variant="outline"
                            onClick={(e) => {
                            e.stopPropagation();
                            saveFileToStorage(file);
                            }}
                        >
                            <Save className="h-3 w-3" />
                        </Button>
                        )}
                        <Button
                        size="sm"
                        variant="outline"
                        onClick={(e) => {
                            e.stopPropagation();
                            deleteFile(file);
                        }}
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
                <p ref={messageRef} className="text-muted-foreground">
                {loaded ? 'FFmpeg ready. Upload a video to get started.' : 'Loading FFmpeg...'}
                </p>
            </div>
            </CardContent>
        </Card>
        </div>
    </AppLayout>
  );
}