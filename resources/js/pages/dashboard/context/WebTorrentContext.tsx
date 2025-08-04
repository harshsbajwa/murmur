import { createContext, useContext, useEffect, useRef, useState, ReactNode } from 'react';
import WebTorrent, { Instance, Options } from 'webtorrent';

interface WebTorrentContextType {
    client: Instance | null;
    isReady: boolean;
    isInitializing: boolean;
}

const WebTorrentContext = createContext<WebTorrentContextType | undefined>(undefined);

const clientOptions: Options = {
    tracker: {
        // Public TURN servers for NAT traversal
        iceServers: [
            { urls: 'stun:stun.l.google.com:19302' },
            { urls: 'stun:global.stun.twilio.com:3478' },
            { urls: 'stun:stun.services.mozilla.com' },
            { urls: 'turn:openrelay.metered.ca:80', username: 'openrelayproject', credential: 'openrelayproject' },
            { urls: 'turn:openrelay.metered.ca:443', username: 'openrelayproject', credential: 'openrelayproject' },
        ],
        // Default trackers to announce to for all torrents
        announce: [
            'wss://tracker.webtorrent.dev',
            'wss://tracker.openwebtorrent.com',
            'wss://tracker.btorrent.xyz'
        ],
    },
    // DHT for better peer discovery
    dht: true,
    uploadLimit: -1,
    downloadLimit: -1,
    maxConns: 100,
    // uTP for better performance
    utp: true,
};

export const WebTorrentProvider = ({ children }: { children: ReactNode }) => {
    const clientRef = useRef<Instance | null>(null);
    const [isReady, setIsReady] = useState(false);
    const [isInitializing, setIsInitializing] = useState(false);
    const initializationRef = useRef(false);

    useEffect(() => {
        if (clientRef.current || initializationRef.current) return;
        
        initializationRef.current = true;
        setIsInitializing(true);
        console.log('Initializing WebTorrent client...');
        
        try {
            const client = new WebTorrent(clientOptions);
            clientRef.current = client;
            
            // Timeout to mark the client as ready
            // WebTorrent client is ready immediately after creation in browsers
            const readyTimeout = setTimeout(() => {
                if (clientRef.current) {
                    console.log('WebTorrent client ready for use.');
                    setIsReady(true);
                    setIsInitializing(false);
                }
            }, 100); // Small delay to ensure client is fully initialized
            
            // Global error handler for the client
            client.on('error', (err) => {
                console.error('WebTorrent client error:', err);
                // Don't set isReady to false on minor errors
                if (err.toString().includes('IndexedDB')) {
                    console.warn('IndexedDB storage may not be available - torrents will work in memory only');
                }
            });
            
            // Torrent-specific events
            client.on('torrent', (torrent) => {
                console.log(`Torrent added: ${torrent.name || torrent.infoHash}`);
                
                torrent.on('error', (err) => {
                    console.error(`Torrent error for ${torrent.name || torrent.infoHash}:`, err);
                });
                
                torrent.on('warning', (err) => {
                    console.warn(`Torrent warning for ${torrent.name || torrent.infoHash}:`, err);
                });

                torrent.on('ready', () => {
                    console.log(`Torrent ready: ${torrent.name}`);
                });

                torrent.on('done', () => {
                    console.log(`Torrent download complete: ${torrent.name}`);
                });
            });
            
            console.log('WebTorrent client initialized successfully.');
            
            return () => {
                clearTimeout(readyTimeout);
            };
            
        } catch (error) {
            console.error('Failed to initialize WebTorrent client:', error);
            setIsReady(false);
            setIsInitializing(false);
            initializationRef.current = false;
        }

        return () => {
            if (clientRef.current) {
                console.log('Destroying WebTorrent client...');
                try {
                    // Destroy all torrents first
                    clientRef.current.torrents.forEach(torrent => {
                        try {
                            torrent.destroy();
                        } catch (err) {
                            console.warn('Error destroying torrent:', err);
                        }
                    });
                    
                    clientRef.current.destroy();
                } catch (destroyError) {
                    console.error("Error while destroying WebTorrent client:", destroyError);
                }
                clientRef.current = null;
                setIsReady(false);
                setIsInitializing(false);
                initializationRef.current = false;
            }
        };
    }, []);

    const value = { 
        client: clientRef.current, 
        isReady,
        isInitializing 
    };
    
    return (
        <WebTorrentContext.Provider value={value}>
            {children}
        </WebTorrentContext.Provider>
    );
};

export const useWebTorrent = (): WebTorrentContextType => {
    const context = useContext(WebTorrentContext);
    if (context === undefined) {
        throw new Error('useWebTorrent must be used within a WebTorrentProvider');
    }
    return context;
};