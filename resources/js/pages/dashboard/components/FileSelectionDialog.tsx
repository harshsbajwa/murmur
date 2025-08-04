
import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle } from '@/components/ui/dialog';
import { File as FileIcon } from 'lucide-react';
import { VideoFile } from '@/types';
import WebTorrent from 'webtorrent';

const formatFileSize = (bytes: number): string => {
    if (bytes === 0) return '0 Bytes';
    const k = 1024;
    const sizes = ['Bytes', 'KiB', 'MiB', 'GiB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return `${parseFloat((bytes / Math.pow(k, i)).toFixed(2))} ${sizes[i]}`;
};

interface FileSelectionDialogProps {
    torrent: WebTorrent.Torrent | null;
    onFileSelect: (videoFile: VideoFile) => void;
    onClose: () => void;
}

export function FileSelectionDialog({ torrent, onFileSelect, onClose }: FileSelectionDialogProps) {
    if (!torrent) return null;

    const handleFileClick = (file: WebTorrent.TorrentFile) => {
        file.getBlob((err, blob) => {
            if (err || !blob) {
                console.error(`Error getting blob for streaming: ${err}`);
                return;
            }
            const url = URL.createObjectURL(blob);
            const videoFile: VideoFile = {
                id: `${torrent.infoHash}-${file.path}`,
                name: file.name,
                size: file.length,
                type: blob.type || 'video/mp4',
                url: url,
                lastModified: Date.now(),
            };
            onFileSelect(videoFile);
            onClose();
        });
    };

    return (
        <Dialog open={!!torrent} onOpenChange={(isOpen) => !isOpen && onClose()}>
            <DialogContent>
                <DialogHeader>
                    <DialogTitle>Select a file to stream</DialogTitle>
                    <DialogDescription>
                        This torrent contains multiple files. Please choose one to begin streaming.
                    </DialogDescription>
                </DialogHeader>
                <div className="max-h-80 space-y-2 overflow-y-auto">
                    {torrent.files.map((file, index) => (
                        <div
                            key={index}
                            onClick={() => handleFileClick(file)}
                            className="flex cursor-pointer items-center justify-between rounded-md p-3 hover:bg-muted"
                        >
                            <div className="flex items-center gap-3">
                                <FileIcon className="h-4 w-4" />
                                <span className="truncate">{file.name}</span>
                            </div>
                            <span className="text-sm text-muted-foreground">{formatFileSize(file.length)}</span>
                        </div>
                    ))}
                </div>
            </DialogContent>
        </Dialog>
    );
}