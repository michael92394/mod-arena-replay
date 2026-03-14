# RTG ArenaReplay Modernization 5.2.8

## Scope
Arena-only stabilization pass focused on replay playback correctness, client safety, and spectator usability.

## Changes
- Added `IsReplayHudAllowed` replay-session gate helper.
- Added actor-track sanitization to drop malformed / non-finite frames from older replay loads.
- Kept POV selection bound to playable actor tracks only.
- Forced immediate camera application on `.rtgreplay next` / `.rtgreplay prev`.
- Added configurable `ArenaReplay.Playback.PacketBudgetPerUpdate` with a conservative default of `60`.
- Added replay-end grace period before teardown to reduce abrupt replay-finish crashes.
- Simplified replay exit toward one anchor-return path instead of combining battleground leave and extra teleport in the same path.
- Tuned distributed config for arena-only playback by default (`ArenaReplay.SaveBattlegrounds = 0`).

## Known limitation
The module still uses a hidden spectator-anchor architecture rather than a true clone-renderer. Older replay safety is improved, but malformed historical replays may still fail if their source packet stream itself is invalid.
