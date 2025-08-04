import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle } from '@/components/ui/dialog';
import { Button } from '@/components/ui/button';
import { Input } from '@/components/ui/input';
import { Label } from '@/components/ui/label';
import { Check, Copy } from 'lucide-react';
import { useState } from 'react';

interface ShareDialogProps {
    open: boolean;
    onOpenChange: (open: boolean) => void;
    shareUrl: string;
    magnetLink: string;
    torrentName: string;
}

export function ShareDialog({ open, onOpenChange, shareUrl, magnetLink, torrentName }: ShareDialogProps) {
    const [copied, setCopied] = useState<'url' | 'magnet' | null>(null);

    const handleCopy = (text: string, type: 'url' | 'magnet') => {
        navigator.clipboard.writeText(text).then(() => {
            setCopied(type);
            setTimeout(() => setCopied(null), 2000);
        });
    };

    return (
        <Dialog open={open} onOpenChange={onOpenChange}>
            <DialogContent className="sm:max-w-md">
                <DialogHeader>
                    <DialogTitle>Share: {torrentName}</DialogTitle>
                    <DialogDescription>
                        Use the web link to share with other users for easy streaming.
                    </DialogDescription>
                </DialogHeader>
                <div className="space-y-4 py-2">
                    <div className="space-y-2">
                        <Label htmlFor="share-url">Shareable Web Link</Label>
                        <div className="flex items-center gap-2">
                            <Input id="share-url" value={shareUrl} readOnly />
                            <Button size="icon" variant="outline" onClick={() => handleCopy(shareUrl, 'url')}>
                                {copied === 'url' ? <Check className="h-4 w-4 text-green-500" /> : <Copy className="h-4 w-4" />}
                            </Button>
                        </div>
                    </div>
                    <div className="space-y-2">
                        <Label htmlFor="magnet-link">Magnet Link (for other clients)</Label>
                        <div className="flex items-center gap-2">
                            <Input id="magnet-link" value={magnetLink} readOnly />
                            <Button size="icon" variant="outline" onClick={() => handleCopy(magnetLink, 'magnet')}>
                                {copied === 'magnet' ? <Check className="h-4 w-4 text-green-500" /> : <Copy className="h-4 w-4" />}
                            </Button>
                        </div>
                    </div>
                </div>
            </DialogContent>
        </Dialog>
    );
}