Drop soundtrack `.opus` files in the context folders under this folder.

The `SoundtrackSystem` waits for a random silence timer, picks one eligible
track from the current heuristic pool, plays it once, then resets the silence
timer. The default silence range is 20-60 minutes.

Folder names are the rules. The active pool is built from:

- `all_day_night`
- `all_day` or `all_night`
- `{biome}_day_night`
- `{biome}_day` or `{biome}_night`
- `underground` when player Y is 74 or lower
- `underwater` when the player is in water

Current biome folders are `conifer`, `meadow`, `desert`, `jungle`, `winter`,
and `grass`. ChucK scripts are no longer selected by the soundtrack system.
