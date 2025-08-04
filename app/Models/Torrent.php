<?php

namespace App\Models;

use Illuminate\Database\Eloquent\Factories\HasFactory;
use Illuminate\Database\Eloquent\Model;
use Illuminate\Database\Eloquent\Relations\BelongsTo;
use Illuminate\Support\Facades\Auth;

class Torrent extends Model
{
    use HasFactory;

    /**
     * The attributes that are mass assignable.
     *
     * @var array<int, string>
     */
    protected $fillable = [
        'user_id',
        'info_hash',
        'name',
        'size',
        'trackers',
        'session_token',
    ];

    /**
     * The attributes that should be cast.
     *
     * @var array<string, string>
     */
    protected $casts = [
        'trackers' => 'array',
    ];

    /**
     * The accessors to append to the model's array form.
     *
     * @var array
     */
    protected $appends = ['can_delete'];

    /**
     * Get the user that owns the torrent.
     */
    public function user(): BelongsTo
    {
        return $this->belongsTo(User::class);
    }

    /**
     * Determine if the currently authenticated user can delete the torrent.
     */
    public function getCanDeleteAttribute(): bool
    {
        $user = Auth::user();
        $request = request();

        if ($user) {
            // Authenticated user check
            return $user->id === $this->user_id;
        }

        // Guest user check
        $guestToken = $request->session()->get('guest_token');
        if (! $guestToken) {
            return false;
        }

        return $this->user->email === 'guest@murmur.app' && $this->session_token === $guestToken;
    }
}
