import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import laravel from 'laravel-vite-plugin';
import tailwindcss from '@tailwindcss/vite';
import path from 'path';
import { nodePolyfills } from 'vite-plugin-node-polyfills';

export default defineConfig(({ command }) => ({
    plugins: [
        laravel({
            input: ['resources/css/app.css', 'resources/js/app.tsx'],
            ssr: 'resources/js/ssr.tsx',
            refresh: true,
        }),
        react(),
        tailwindcss(),
        nodePolyfills({
            protocolImports: true,
        }),
    ],
    resolve: {
        alias: {
            'bittorrent-dht': path.resolve(__dirname, 'resources/js/lib/stubs/bittorrent-dht.js'),
            'ziggy-js': path.resolve('vendor/tightenco/ziggy/dist/index.js'),
        },
    },
    server: {
        host: '127.0.0.1',
        port: 5173,
        headers: {
            'Access-Control-Allow-Origin': '*',
            'Cross-Origin-Resource-Policy': 'cross-origin',
        },
    },
    build: {
        rollupOptions: {
            external: ['/vendor/ffmpeg_ffmpeg/index.js', '/vendor/ffmpeg_util/index.js'],
        },
    },
}));