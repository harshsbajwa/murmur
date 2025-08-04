<?php

use App\Http\Controllers\TorrentController;
use Illuminate\Support\Facades\Route;
use Inertia\Inertia;

Route::get('/', function () {
    return Inertia::render('dashboard/index');
})->name('home');

Route::get('/dashboard', function () {
    return Inertia::render('dashboard/index');
})->name('dashboard');

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
