# Arena Replay modernization 5.2.3

## Focus
- Harden replay HUD/runtime behavior so replay UI does not mutate outside active replay sessions.
- Reduce recurring stuck-gravity/levitation state after replay exit.

## Changes
- Added replay-session gating for replay HUD messages so START/POV/WATCHERS are only emitted while the player is actively inside a replay battleground with a live replay session.
- Reworked replay open/step command paths to proactively clear stale replay HUD state when the player is not actually watching a replay.
- Added stronger replay movement cleanup reuse so fly/hover/gravity flags are cleared on release, exit, and post-return restoration.
- Hid the viewer using both normal visibility and GM visibility toggles during replay and restored both on exit.

## Intent
This pass does not claim to be the final clone-renderer. It hardens the current hidden-spectator-anchor architecture so addon/UI state and post-replay movement state are less likely to leak outside replay playback.
