import {
    Card, CardContent, CardHeader, CardTitle
} from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import {
    Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle, DialogTrigger
} from '@/components/ui/dialog';
import {
    Share2, User, DownloadCloud, UploadCloud, Trash2, File as FileIcon, Loader2
} from 'lucide-react';
import { APITorrent, SeedingTorrent } from '@/types';
import { useState, useEffect, useCallback } from 'react';
import axios from 'axios';
import { useWebTorrent } from '../context/WebTorrentContext';
import WebTorrent from 'webtorrent';

const formatFileSize = (bytes: number): string => {
    if (bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KiB', 'MiB', 'GiB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return `${parseFloat((bytes / Math.pow(k, i)).toFixed(2))} ${sizes[i]}`;
};

const StreamButton = ({ torrent, onStreamRequest }: { torrent: APITorrent, onStreamRequest: (torrent: WebTorrent.Torrent, file: WebTorrent.TorrentFile) => void; }) => {
    const { client: webTorrentClient } = useWebTorrent();
    const [isFetchingMetadata, setIsFetchingMetadata] = useState(false);
    const [isDialogOpen, setIsDialogOpen] = useState(false);
    const [torrentFiles, setTorrentFiles] = useState<WebTorrent.TorrentFile[]>([]);
    const [readyTorrent, setReadyTorrent] = useState<WebTorrent.Torrent | null>(null);

    const handleStream = useCallback(() => {
        if (!webTorrentClient) return;

        setIsFetchingMetadata(true);
        
        const trackerParams = torrent.trackers?.map(tr => `&tr=${encodeURIComponent(tr)}`).join('') ?? '';
        const identifier = `magnet:?xt=urn:btih:${torrent.info_hash}&dn=${encodeURIComponent(torrent.name)}${trackerParams}`;

        const wtTorrent = webTorrentClient.add(identifier);

        wtTorrent.on('ready', () => {
            setIsFetchingMetadata(false);
            setReadyTorrent(wtTorrent);

            const videoFiles = wtTorrent.files
                .filter(f => f.name.match(/\.(mp4|webm|mov|mkv|avi)$/i));
            
            if (videoFiles.length === 1) {
                onStreamRequest(wtTorrent, videoFiles[0]);
            } else if (videoFiles.length > 1) {
                setTorrentFiles(videoFiles);
                setIsDialogOpen(true);
            } else {
                 console.error('No streamable files found in torrent:', wtTorrent.name);
                 wtTorrent.destroy();
            }
        });
        
        wtTorrent.on('error', (err) => {
            console.error(`Error fetching metadata for ${torrent.info_hash}:`, err);
            setIsFetchingMetadata(false);
        });

    }, [webTorrentClient, torrent, onStreamRequest]);

    const handleFileSelect = (file: WebTorrent.TorrentFile) => {
        if (readyTorrent) {
            onStreamRequest(readyTorrent, file);
        }
        setIsDialogOpen(false);
    };

    return (
        <Dialog open={isDialogOpen} onOpenChange={setIsDialogOpen}>
            <DialogTrigger asChild>
                <Button size="sm" variant="outline" onClick={handleStream} disabled={isFetchingMetadata}>
                    {isFetchingMetadata ? <Loader2 className="h-4 w-4 animate-spin" /> : 'Stream'}
                </Button>
            </DialogTrigger>
            <DialogContent>
                <DialogHeader>
                    <DialogTitle>Select a file to stream</DialogTitle>
                    <DialogDescription>This torrent contains multiple files. Please choose one to begin streaming.</DialogDescription>
                </DialogHeader>
                <div className="max-h-80 space-y-2 overflow-y-auto">
                    {torrentFiles.map((file, index) => (
                        <div
                            key={index}
                            onClick={() => handleFileSelect(file)}
                            className="flex cursor-pointer items-center justify-between rounded-md p-3 hover:bg-muted"
                        >
                            <div className="flex items-center gap-3">
                                <FileIcon className="h-4 w-4" />
                                <span className="truncate" title={file.name}>{file.name}</span>
                            </div>
                            <span className="text-sm text-muted-foreground">{formatFileSize(file.length)}</span>
                        </div>
                    ))}
                </div>
            </DialogContent>
        </Dialog>
    );
};

export function TorrentManager({ seedingTorrents, onStreamRequest, onStopSeeding }: {
    seedingTorrents: SeedingTorrent[];
    onStreamRequest: (torrent: WebTorrent.Torrent, file: WebTorrent.TorrentFile) => void;
    onStopSeeding: (infoHash: string) => void;
}) {
    const [availableTorrents, setAvailableTorrents] = useState<APITorrent[]>([]);

    const fetchTorrents = useCallback(async () => {
        try {
            const response = await axios.get('/api/torrents', { withCredentials: true });
            const mappedData = response.data.map((t: APITorrent) => ({
                ...t,
                infoHash: t.info_hash, 
                size: t.size,
            }));
            setAvailableTorrents(mappedData);
        } catch (error) { console.error('Failed to fetch torrents:', error); }
    }, []);

    useEffect(() => {
        fetchTorrents();
        const interval = setInterval(fetchTorrents, 15000);
        return () => clearInterval(interval);
    }, [fetchTorrents]);

    const handleStopSeeding = (e: React.MouseEvent, infoHash: string) => {
        e.stopPropagation();
        onStopSeeding(infoHash);
    };

    return (
        <Card>
            <CardHeader>
                <CardTitle className="flex items-center gap-2">
                    <Share2 className="h-5 w-5" /> Torrent Sharing
                </CardTitle>
            </CardHeader>
            <CardContent className="space-y-4">
                <div>
                    <h4 className="mb-2 text-sm font-medium">Currently Seeding</h4>
                    {seedingTorrents.length > 0 ? (
                        <div className="space-y-2">
                            {seedingTorrents.map((torrent) => (
                                <div key={torrent.infoHash} className="flex items-center justify-between rounded-md border p-3">
                                    <div className="flex min-w-0 items-center gap-3">
                                        <UploadCloud className="h-5 w-5 flex-shrink-0 text-green-500" />
                                        <div className="min-w-0">
                                            <p className="truncate font-medium" title={torrent.name}>{torrent.name}</p>
                                            <p className="text-sm text-muted-foreground">{formatFileSize(torrent.length)}</p>
                                        </div>
                                    </div>
                                    <Button size="sm" variant="destructive" onClick={(e) => handleStopSeeding(e, torrent.infoHash)}>
                                        <Trash2 className="h-3 w-3" />
                                    </Button>
                                </div>
                            ))}
                        </div>
                    ) : ( <p className="text-sm text-muted-foreground">You are not seeding any files.</p> )}
                </div>

                <div>
                    <h4 className="mb-2 text-sm font-medium">Available on Network</h4>
                    {availableTorrents.length > 0 ? (
                        <div className="max-h-60 space-y-2 overflow-y-auto">
                            {availableTorrents.map((torrent) => (
                                <div key={torrent.id} className="flex flex-col items-start gap-2 rounded-md border p-3 sm:flex-row sm:items-center sm:justify-between">
                                    <div className="flex min-w-0 items-center gap-3">
                                        <DownloadCloud className="h-5 w-5 flex-shrink-0 text-blue-500" />
                                        <div className="min-w-0">
                                            <p className="truncate font-medium" title={torrent.name}>{torrent.name}</p>
                                            <div className="flex flex-wrap items-center gap-x-2 gap-y-1 text-sm text-muted-foreground">
                                                <span>{formatFileSize(torrent.size)}</span>
                                                <span className="flex items-center gap-1">
                                                    <User className="h-3 w-3" /> {torrent.user.name}
                                                </span>
                                            </div>
                                        </div>
                                    </div>
                                    <div className="flex w-full items-center justify-end gap-2 sm:w-auto sm:justify-start">
                                        <StreamButton torrent={torrent} onStreamRequest={onStreamRequest} />
                                        {torrent.can_delete && (
                                            <Button size="sm" variant="destructive" onClick={(e) => handleStopSeeding(e, torrent.info_hash)}>
                                                <Trash2 className="h-3 w-3" />
                                            </Button>
                                        )}
                                    </div>
                                </div>
                            ))}
                        </div>
                    ) : ( <p className="text-sm text-muted-foreground">No other torrents found on the network.</p> )}
                </div>
            </CardContent>
        </Card>
    );
}