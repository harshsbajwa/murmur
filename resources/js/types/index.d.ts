import { LucideIcon } from 'lucide-react';
import type { Config } from 'ziggy-js';
import WebTorrent from 'webtorrent';

export interface Auth {
    user: User;
}

export interface BreadcrumbItem {
    title: string;
    href: string;
}

export interface NavGroup {
    title: string;
    items: NavItem[];
}

export interface NavItem {
    title: string;
    href: string;
    icon?: LucideIcon | null;
    isActive?: boolean;
}

export interface SharedData {
    name: string;
    quote: { message: string; author: string };
    auth: Auth;
    ziggy: Config & { location: string };
    sidebarOpen: boolean;
    [key: string]: unknown;
}

export interface User {
    id: number;
    name: string;
    email: string;
    avatar?: string;
    email_verified_at: string | null;
    created_at: string;
    updated_at: string;
    [key: string]: unknown;
}

export interface APITorrent {
    id: number;
    user_id: number;
    info_hash: string;
    name: string;
    size: number;
    created_at: string;
    updated_at: string;
    user: Pick<User, 'id' | 'name'>;
    trackers?: string[] | null;
    session_token?: string | null;
    can_delete: boolean;
}

export interface SeedingTorrent {
    infoHash: string;
    name: string;
    length: number;
    files: Array<{ name: string; length: number; path: string }>;
}

export interface VideoFile {
    id: string;
    name:string;
    size: number;
    type: string;
    lastModified: number;
    saved?: boolean;
    playbackTime?: number;
    
    // Blob URL for local files or generated on demand
    url: string;
    
    // Local file properties
    originalFile?: File;
    blob?: Blob;

    // Torrent-specific properties
    isTorrent?: boolean;
    magnetURI?: string;
    infoHash?: string;
    torrentFilePath?: string;
    seedingInfoHash?: string;

    // Live torrent objects
    torrent?: WebTorrent.Torrent;
    torrentFile?: WebTorrent.TorrentFile;
}

export interface TranscriptionResult {
    text: string;
    chunks?: Array<{
        text: string;
        timestamp: [number, number];
    }>;
}

export interface ConversionJob {
    id: string;
    inputFile: VideoFile;
    outputFormat: string;
    status: 'pending' | 'processing' | 'completed' | 'error' | 'cancelled';
    progress: number;
    outputFile?: VideoFile;
    error?: string;
    cancelled?: boolean;
}