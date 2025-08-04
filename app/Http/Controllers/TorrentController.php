<?php

namespace App\Http\Controllers;

use App\Models\Torrent;
use App\Models\User;
use Illuminate\Http\Request;
use Illuminate\Support\Facades\Auth;
use Illuminate\Support\Facades\Gate;
use Illuminate\Support\Str;

class TorrentController extends Controller
{
    /**
     * Display a listing of the resource.
     */
    public function index()
    {
        return Torrent::with('user:id,name')->latest()->get();
    }

    /**
     * Store or update a resource in storage.
     */
    public function store(Request $request)
    {
        $validated = $request->validate([
            'info_hash' => 'required|string|size:40',
            'name' => 'required|string|max:255',
            'size' => 'required|numeric|min:0',
            'trackers' => 'present|nullable|array',
            'trackers.*' => 'string|url',
        ]);

        $sessionToken = null;
        if (Auth::guest()) {
            $user = User::firstOrCreate(
                ['email' => 'guest@murmur.app'],
                ['name' => 'Guest', 'password' => bcrypt(Str::random(20))]
            );

            $sessionToken = $request->session()->get('guest_token');
            if (! $sessionToken) {
                $sessionToken = Str::random(40);
                $request->session()->put('guest_token', $sessionToken);
            }
        } else {
            $user = Auth::user();
        }

        // Prevent 422 errors on re-seed.
        // This is an idempotent operation.
        $torrent = $user->torrents()->updateOrCreate(
            ['info_hash' => $validated['info_hash']],
            [
                'name' => $validated['name'],
                'size' => $validated['size'],
                'trackers' => $validated['trackers'],
                'session_token' => $sessionToken,
            ]
        );

        return response()->json($torrent->load('user:id,name'), $torrent->wasRecentlyCreated ? 201 : 200);
    }

    /**
     * Remove the specified resource from storage.
     */
    public function destroy(Request $request, string $info_hash)
    {
        $torrent = Torrent::where('info_hash', $info_hash)->firstOrFail();

        if (Auth::check()) {
            Gate::authorize('delete', $torrent);
        } else {
            $guestToken = $request->session()->get('guest_token');
            if (
                ! $guestToken ||
                $torrent->user->email !== 'guest@murmur.app' ||
                $torrent->session_token !== $guestToken
            ) {
                abort(403, 'You do not have permission to delete this torrent.');
            }
        }

        $torrent->delete();

        return response()->noContent();
    }
}
