<?php

namespace App\Policies;

use App\Models\Torrent;
use App\Models\User;

class TorrentPolicy
{
    /**
     * Determine whether the user can delete the model.
     */
    public function delete(User $user, Torrent $torrent): bool
    {
        return $user->id === $torrent->user_id;
    }
}
