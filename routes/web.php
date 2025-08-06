<?php

use App\Http\Controllers\TorrentController;
use Illuminate\Support\Facades\Route;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Redis;
use Inertia\Inertia;

Route::get('/', function () {
    return Inertia::render('dashboard/index');
})->name('home');

Route::get('/dashboard', function () {
    return Inertia::render('dashboard/index');
})->name('dashboard');

// Health check endpoint
Route::get('/health', function () {
    $checks = [];
    $allHealthy = true;

    // Check database connection
    try {
        DB::connection()->getPdo();
        $checks['database'] = 'ok';
    } catch (Exception $e) {
        $checks['database'] = 'error: ' . $e->getMessage();
        $allHealthy = false;
    }

    // Check Redis connection
    try {
        Redis::ping();
        $checks['redis'] = 'ok';
    } catch (Exception $e) {
        $checks['redis'] = 'error: ' . $e->getMessage();
        $allHealthy = false;
    }

    // Check storage directory
    if (is_writable(storage_path())) {
        $checks['storage'] = 'ok';
    } else {
        $checks['storage'] = 'error: storage directory not writable';
        $allHealthy = false;
    }

    $response = [
        'status' => $allHealthy ? 'healthy' : 'unhealthy',
        'checks' => $checks,
        'timestamp' => now()->toISOString(),
        'version' => config('app.version', '1.0.0'),
    ];

    return response()->json($response, $allHealthy ? 200 : 503);
})->name('health');

// Desktop application routes
Route::get('/downloads', [App\Http\Controllers\DesktopController::class, 'downloads'])->name('downloads');
Route::get('/download', [App\Http\Controllers\DesktopController::class, 'downloadRedirect'])->name('download.redirect');

require __DIR__.'/auth.php';
require __DIR__.'/settings.php';

Route::prefix('api')->as('api.')->group(function () {
    Route::middleware(['auth'])->group(function () {
        Route::get('torrents', [TorrentController::class, 'index'])->name('torrents.index');
        Route::post('torrents', [TorrentController::class, 'store'])->name('torrents.store');
        Route::delete('torrents/{info_hash}', [TorrentController::class, 'destroy'])->name('torrents.destroy');
    });

    // Desktop application API routes
    Route::prefix('desktop')->group(function () {
        Route::get('latest', [App\Http\Controllers\DesktopController::class, 'getLatestRelease'])->name('desktop.latest');
        Route::post('track-download', [App\Http\Controllers\DesktopController::class, 'trackDownload'])->name('desktop.track-download');
    });
});
