import { useState, useRef, useCallback, useEffect } from 'react';
import type { FFmpeg, ProgressEvent } from '@ffmpeg/ffmpeg';

type LogCallback = (message: string) => void;
type ProgressCallback = (progress: number) => void;

export function useFFmpeg(logCallback: LogCallback, progressCallback: ProgressCallback) {
    const [isLoaded, setIsLoaded] = useState(false);
    const [isLoading, setIsLoading] = useState(false);
    const ffmpegRef = useRef<FFmpeg | null>(null);

    const loadFFmpeg = useCallback(async () => {
        if (ffmpegRef.current || isLoading) return;

        setIsLoading(true);
        logCallback('Loading FFmpeg libraries...');
        try {
            const { FFmpeg } = await import(/* @vite-ignore */ `${window.location.origin}/vendor/ffmpeg_ffmpeg/index.js`);
            const ffmpeg = new FFmpeg();
            ffmpegRef.current = ffmpeg;

            ffmpeg.on('log', ({ message }) => {
                logCallback(message);
            });
            
            ffmpeg.on('progress', ({ progress }: ProgressEvent) => {
                progressCallback(progress * 100);
            });

            const coreURL = '/vendor/ffmpeg_core/ffmpeg-core.js';
            const wasmURL = '/vendor/ffmpeg_core/ffmpeg-core.wasm';
            await ffmpeg.load({ coreURL, wasmURL });


            setIsLoaded(true);
            logCallback('FFmpeg is ready.');
        } catch (error) {
            logCallback(`Error loading FFmpeg: ${error}`);
            console.error(error);
        } finally {
            setIsLoading(false);
        }
    }, [isLoading, logCallback, progressCallback]);

    useEffect(() => {
        loadFFmpeg();
    }, [loadFFmpeg]);

    return { ffmpeg: ffmpegRef.current, isLoaded, isLoading };
}