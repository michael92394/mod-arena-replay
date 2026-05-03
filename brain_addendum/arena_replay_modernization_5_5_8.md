# Arena Replay Modernization 5.5.8

## Chapter 5.5.8 — Clone-only replay load and spectator-shell restore safety

Date: 2026-05-03
Subsystem: mod-arena-replay copied-instance clone playback

### Context
Recent RTG replay tests showed appearance capture now fires during live arena join/sample, but newly recorded clone-mode replays could still be rejected with “Replay data is incomplete or unsafe for playback.” The same test pass also showed replay spectator shell flight/no-gravity flags could survive teardown on the RTG branch.

### Findings
- Clone-only copied-instance replays may legitimately have no raw packet payload while still containing valid actor tracks and appearance snapshots.
- Treating `record.packets.empty()` as a hard playback failure blocks clone-only replay rows even when actor-track data is valid.
- The hidden viewer body no longer needs fly/no-gravity/hover for normal copied-instance clone playback because the camera anchor drives the view and the body is invisible/parked.
- Repeated appearance sample logging was too noisy; appearance capture should still update every sample, but only log sample captures when the snapshot changes.

### Changes
- Allows packet-empty replay rows to load when playable actor tracks exist.
- Keeps the unsafe replay rejection only for rows with no raw packets and no usable actor tracks.
- Adds `[RTG][REPLAY][LOAD_COMPAT]` for clone-only/no-packet replay loading.
- Adds `[RTG][REPLAY][LOAD_FAIL]` diagnostics for truly unsafe rows.
- Adds `ArenaReplay.SpectatorShell.UseFlightForParking = 0` as the explicit compatibility switch for fly/no-gravity parking.
- Stops applying fly/no-gravity/hover to the hidden viewer body unless `UseFlightForParking` is enabled.
- Throttles sample appearance-capture logs by logging sample captures only when actor appearance data changes.

### Status
Ready for a fresh arena replay test. Expected behavior: fresh clone-only replays with valid actor tracks no longer show “Replay data is incomplete or unsafe for playback,” and new replay exits should no longer create replay-induced floating unless `UseFlightForParking` is explicitly enabled.
