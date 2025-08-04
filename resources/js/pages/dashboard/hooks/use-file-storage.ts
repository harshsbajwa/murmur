import { useState, useCallback, useEffect } from 'react';
import { VideoFile, TranscriptionResult } from '@/types';

const DB_NAME = 'MurmurDB';
const DB_VERSION = 1;
const STORE_NAME = 'videos';
const SUBTITLE_STORE_NAME = 'subtitles';

const openDB = (): Promise<IDBDatabase> => {
    return new Promise((resolve, reject) => {
        try {
            const request = indexedDB.open(DB_NAME, DB_VERSION);
            request.onerror = () => reject(request.error);
            request.onsuccess = () => resolve(request.result);
            request.onupgradeneeded = (event) => {
                const db = (event.target as IDBOpenDBRequest).result;
                if (!db.objectStoreNames.contains(STORE_NAME)) {
                    db.createObjectStore(STORE_NAME, { keyPath: 'id' });
                }
                if (!db.objectStoreNames.contains(SUBTITLE_STORE_NAME)) {
                    db.createObjectStore(SUBTITLE_STORE_NAME, { keyPath: 'fileId' });
                }
            };
        } catch (error) {
            reject(error);
        }
    });
};

export function useFileStorage() {
    const [uploadedFiles, setUploadedFiles] = useState<VideoFile[]>([]);
    const [storageUsage, setStorageUsage] = useState(0);
    const [subtitleCache, setSubtitleCache] = useState<Map<string, TranscriptionResult>>(new Map());

    const updateStorageUsage = useCallback(async () => {
        try {
            if ('storage' in navigator && 'estimate' in navigator.storage) {
                const estimate = await navigator.storage.estimate();
                setStorageUsage((estimate.usage || 0) / (1024 * 1024)); // MiB
            }
        } catch (error) {
            console.warn("Could not estimate storage usage:", error);
        }
    }, []);

    const loadSavedFiles = useCallback(async () => {
        try {
            const db = await openDB();
            const transaction = db.transaction([STORE_NAME], 'readonly');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.getAll();

            return new Promise<VideoFile[]>((resolve) => {
                request.onsuccess = () => {
                    const results = (request.result || []).map((res: VideoFile) => {
                        const url = res.blob ? URL.createObjectURL(res.blob) : '';
                        return {
                            ...res,
                            url,
                            saved: true,
                            // Live objects are not loaded from DB
                            torrent: undefined,
                            torrentFile: undefined,
                        };
                    });
                    setUploadedFiles((prev) => [...prev, ...results]);
                    resolve(results);
                };
                request.onerror = (event) => {
                    console.error('Error loading files from IndexedDB:', (event.target as IDBRequest).error);
                    resolve([]);
                }
            });
        } catch (error) {
            console.error('Failed to open IndexedDB for loading files:', error);
            return [];
        }
    }, []);

    const loadSubtitleCache = useCallback(async () => {
        try {
            const db = await openDB();
            const transaction = db.transaction([SUBTITLE_STORE_NAME], 'readonly');
            const store = transaction.objectStore(SUBTITLE_STORE_NAME);
            const request = store.getAll();

            return new Promise<void>((resolve) => {
                request.onsuccess = () => {
                    const results = request.result || [];
                    const cache = new Map<string, TranscriptionResult>();
                    results.forEach((item) => {
                        cache.set(item.fileId, item.transcription);
                    });
                    setSubtitleCache(cache);
                    resolve();
                };
                request.onerror = (event) => {
                     console.error('Error loading subtitle cache from IndexedDB:', (event.target as IDBRequest).error);
                    resolve();
                }
            });
        } catch (error) {
            console.error('Failed to open IndexedDB for loading subtitles:', error);
        }
    }, []);

    const saveFile = useCallback(
        async (file: VideoFile) => {
            try {
                let blobToSave: Blob | undefined;
                if (file.isTorrent && file.torrentFile) {
                    blobToSave = await new Promise((resolve, reject) => {
                        file.torrentFile!.getBlob((err: Error | string | undefined, blob?: Blob) => err ? reject(err) : resolve(blob!));
                    });
                } else if (!file.isTorrent) {
                    const response = await fetch(file.url);
                    blobToSave = await response.blob();
                }

                if (!blobToSave) {
                    throw new Error("Could not get a blob to save.");
                }
                
                const db = await openDB();
                const transaction = db.transaction([STORE_NAME], 'readwrite');
                const store = transaction.objectStore(STORE_NAME);

                const storableFile: Partial<VideoFile> = { ...file };
                delete storableFile.torrent;
                delete storableFile.torrentFile;
                delete storableFile.originalFile;
                storableFile.blob = blobToSave;
                storableFile.saved = true;

                const request = store.put(storableFile);
                
                await new Promise((resolve, reject) => {
                    request.onsuccess = resolve;
                    request.onerror = () => reject(request.error);
                });

                setUploadedFiles((prev) => prev.map((f) => (f.id === file.id ? { ...f, saved: true, blob: blobToSave } : f)));
                await updateStorageUsage();
            } catch (error) {
                console.error('Failed to save file to IndexedDB:', error);
            }
        },
        [updateStorageUsage],
    );

    const deleteFile = useCallback(
        async (fileId: string) => {
            try {
                const fileToDelete = uploadedFiles.find((f) => f.id === fileId);
                if (fileToDelete?.saved) {
                    const db = await openDB();
                    const transaction = db.transaction([STORE_NAME, SUBTITLE_STORE_NAME], 'readwrite');
                    const videoStore = transaction.objectStore(STORE_NAME);
                    const subtitleStore = transaction.objectStore(SUBTITLE_STORE_NAME);
                    
                    await new Promise((resolve, reject) => {
                        const delV = videoStore.delete(fileId);
                        delV.onerror = () => reject(delV.error);
                        
                        const delS = subtitleStore.delete(fileId);
                        delS.onerror = () => reject(delS.error);

                        transaction.oncomplete = resolve;
                        transaction.onerror = () => reject(transaction.error);
                    });
                }
                if (fileToDelete?.url.startsWith('blob:')) {
                    URL.revokeObjectURL(fileToDelete.url);
                }
                setUploadedFiles((prev) => prev.filter((f) => f.id !== fileId));
                setSubtitleCache((prev) => {
                    const newCache = new Map(prev);
                    newCache.delete(fileId);
                    return newCache;
                });
                await updateStorageUsage();
            } catch (error) {
                console.error('Failed to delete file from IndexedDB:', error);
            }
        },
        [uploadedFiles, updateStorageUsage],
    );

    const savePlaybackTime = useCallback(async (id: string, time: number) => {
        try {
            const db = await openDB();
            const transaction = db.transaction([STORE_NAME], 'readwrite');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.get(id);
            request.onsuccess = () => {
                const data = request.result;
                if (data) {
                    data.playbackTime = time;
                    store.put(data);
                }
            };
        } catch (error) {
            console.error('Could not save playback time:', error);
        }
    }, []);

    const getPlaybackTime = useCallback(async (id: string) => {
        try {
            const db = await openDB();
            const transaction = db.transaction([STORE_NAME], 'readonly');
            const store = transaction.objectStore(STORE_NAME);
            const request = store.get(id);
            return new Promise<number | undefined>((resolve) => {
                request.onsuccess = () => resolve(request.result?.playbackTime);
                request.onerror = () => resolve(undefined);
            });
        } catch (error) {
            console.error('Could not get playback time:', error);
            return undefined;
        }
    }, []);

    const saveSubtitles = useCallback(async (fileId: string, transcription: TranscriptionResult, persist: boolean = false) => {
        setSubtitleCache((prev) => new Map(prev).set(fileId, transcription));

        if (persist) {
            try {
                const db = await openDB();
                const transaction = db.transaction([SUBTITLE_STORE_NAME], 'readwrite');
                const store = transaction.objectStore(SUBTITLE_STORE_NAME);
                const request = store.put({ fileId, transcription, timestamp: Date.now() });
                await new Promise((resolve, reject) => {
                    request.onsuccess = resolve;
                    request.onerror = () => reject(request.error);
                });
                await updateStorageUsage();
            } catch (error) {
                console.error('Failed to save subtitles to IndexedDB:', error);
            }
        }
    }, [updateStorageUsage]);

    const getSubtitles = useCallback(async (fileId: string): Promise<TranscriptionResult | undefined> => {
        if (subtitleCache.has(fileId)) {
            return subtitleCache.get(fileId);
        }
        try {
            const db = await openDB();
            const transaction = db.transaction([SUBTITLE_STORE_NAME], 'readonly');
            const store = transaction.objectStore(SUBTITLE_STORE_NAME);
            const request = store.get(fileId);
            
            return new Promise<TranscriptionResult | undefined>((resolve) => {
                request.onsuccess = () => {
                    const result = request.result?.transcription;
                    if (result) {
                        setSubtitleCache((prev) => new Map(prev).set(fileId, result));
                    }
                    resolve(result);
                };
                request.onerror = () => resolve(undefined);
            });
        } catch (error) {
            console.error('Error loading subtitles from IndexedDB:', error);
            return undefined;
        }
    }, [subtitleCache]);

    useEffect(() => {
        loadSavedFiles();
        loadSubtitleCache();
        updateStorageUsage();
    }, [loadSavedFiles, loadSubtitleCache, updateStorageUsage]);

    return {
        uploadedFiles,
        setUploadedFiles,
        storageUsage,
        saveFile,
        deleteFile,
        savePlaybackTime,
        getPlaybackTime,
        saveSubtitles,
        getSubtitles,
    };
}