import { useState, useRef } from 'react';
import { Button } from '@/components/ui/button';
import AppLayout from '@/layouts/app-layout';
import { type BreadcrumbItem } from '@/types';
import { Head } from '@inertiajs/react';
import { Upload, Film, Loader2 } from 'lucide-react';

const breadcrumbs: BreadcrumbItem[] = [
    {
        title: 'Dashboard',
        href: '/dashboard',
    },
];

export default function Dashboard() {
    const [loaded, setLoaded] = useState(false);
    const ffmpegRef = useRef<any>(null);
    const videoRef = useRef<HTMLVideoElement | null>(null);
    const messageRef = useRef<HTMLParagraphElement | null>(null);
    const [isLoading, setIsLoading] = useState(false);

    const load = async () => {
        setIsLoading(true);
        if (messageRef.current) messageRef.current.innerHTML = 'Loading FFmpeg libraries...';

        const { FFmpeg } = await import(/* @vite-ignore */ `${window.location.origin}/vendor/ffmpeg_ffmpeg/index.js`);
        
        const ffmpeg = new FFmpeg();
        ffmpeg.on("log", ({ message }) => {
            if (messageRef.current) messageRef.current.innerHTML = message;
        });
        
        ffmpegRef.current = ffmpeg;
        
        const coreURL = '/vendor/ffmpeg_core/ffmpeg-core.js';
        const wasmURL = '/vendor/ffmpeg_core/ffmpeg-core.wasm';

        if (messageRef.current) messageRef.current.innerHTML = 'Loading FFmpeg core...';

        await ffmpeg.load({
            coreURL,
            wasmURL,
        });

        setLoaded(true);
        setIsLoading(false);
        if (messageRef.current) messageRef.current.innerHTML = 'FFmpeg is ready.';
    };
  
    const transcode = async () => {
        if (!ffmpegRef.current) {
            if (messageRef.current) messageRef.current.innerHTML = 'FFmpeg not loaded. Please load first.';
            return;
        }
        const { fetchFile } = await import(/* @vite-ignore */ `${window.location.origin}/vendor/ffmpeg_util/index.js`);
        const videoURL = "https://raw.githubusercontent.com/ffmpegwasm/testdata/master/video-15s.avi";
        await ffmpegRef.current.writeFile("input.avi", await fetchFile(videoURL));
        
        await ffmpegRef.current.exec(["-i", "input.avi", "output.mp4"]);
        
        const fileData = await ffmpegRef.current.readFile("output.mp4");
        const data = new Uint8Array(fileData as ArrayBuffer);
        if (videoRef.current) {
            videoRef.current.src = URL.createObjectURL(
                new Blob([data.buffer], { type: "video/mp4" })
            );
        }
    };
    
    return (
        <AppLayout breadcrumbs={breadcrumbs}>
            <Head title="Dashboard" />
            <div className="p-4">
                <div className="space-y-6">
                    <div className="flex items-center gap-3">
                        <Film className="h-8 w-8 text-primary" />
                        <div>
                            <h1 className="text-2xl font-bold">Video Transcoder</h1>
                            <p className="text-sm text-muted-foreground">Convert videos in your browser with FFmpeg.wasm</p>
                        </div>
                    </div>
                    {!loaded && (
                        <Button onClick={load} size="lg" disabled={isLoading}>
                            {isLoading ? (
                                <><Loader2 className="mr-2 h-4 w-4 animate-spin" />Loading...</>
                            ) : (
                                <><Upload className="mr-2 h-4 w-4" />Load FFmpeg</>
                            )}
                        </Button>
                    )}
                    {loaded && (
                        <>
                            <video ref={videoRef} controls className="w-full max-w-2xl rounded-md bg-black"></video>
                            <br />
                            <Button onClick={transcode}>Transcode avi to mp4</Button>
                        </>
                    )}
                    <div className="w-full p-4 bg-muted rounded-md font-mono text-xs overflow-x-auto h-48">
                        <p ref={messageRef}></p>
                    </div>
                </div>
            </div>
        </AppLayout>
    );
}