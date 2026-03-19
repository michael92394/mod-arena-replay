# ArenaReplay modernization 5.5.0 — replay sandbox mode

## Goal
Replace replay playback's dependency on battleground joining with a sandbox simulation flow so replay startup no longer depends on a real, joinable battleground instance.

## Findings carried into this pass
- The prior replay flow still attempted to create and join a battleground instance for playback.
- The client could reject that path with `Map cannot be entered at this time.` because replay playback was trying to attach to a non-normal battleground lifecycle.
- Replay startup and camera apply could then continue against the wrong world context, leaving the player locked on the source map or at unsafe coordinates.
- `mod-npc-spectator` remains useful as a spectator-control reference, but its battleground-attachment assumptions are not the correct ownership model for deterministic replay playback.

## What changed
- Removed replay startup's dependency on `SendToBattleground()` and battleground status packet orchestration.
- Replay launch now chooses a bootstrap actor frame and teleports the hidden spectator shell directly to the replay map.
- Added a sandbox replay world-update driver that advances playback outside battleground update ownership.
- Added sandbox attach validation that waits for the player to actually land on the replay map before clone-scene build, viewpoint bind, HUD startup, or actor view application.
- Replay packet playback, clone sync, actor camera updates, and teardown scheduling now use a replay-local playback clock instead of battleground start time.
- Viewer-body teleports are now skipped while the viewpoint camera is bound so the hidden spectator shell stops fighting the camera anchor.
- Replay watcher HUD sync now works without requiring a battleground player map.

## Architecture note
Replay playback is now explicitly split into:
1. spectator shell lock / hide,
2. direct sandbox teleport to replay map,
3. clone-scene and camera-anchor bootstrap after confirmed map attach,
4. world-update-driven deterministic replay timeline,
5. safe return-to-anchor teardown.

## Remaining caution
This pass removes real battleground join ownership from replay startup, but it does not yet solve every possible arena-map edge case. Any future regressions should now be debugged through sandbox attach, replay map bootstrap, clone-scene lifecycle, or teardown recovery — not through battleground queue semantics.
