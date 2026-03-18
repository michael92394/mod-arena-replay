# RTG ArenaReplay Debugging Atlas

## Why this exists
ArenaReplay has enough moving parts that guessing wastes time. This atlas is the fast path for understanding which replay stage is failing and which log family should appear when the system is healthy.

## Core replay lifecycle
1. Replay record is loaded into `loadedReplays`.
2. Viewer is locked into replay control and receives an active replay session.
3. Replay battleground is created/entered.
4. Packet playback begins in update ticks.
5. Actor follow camera applies a selected POV track.
6. HUD broadcasts START / POV / WATCHERS.
7. Replay completion requests teardown.
8. Teardown restores player state and returns the viewer safely.

## Load-bearing log families
### Session bring-up
`[RTG][REPLAY][LOCK]`
- Confirms the replay session was created.
- Shows `priorBg`, `replayBg`, and stored world anchor.
- First place to verify whether the viewer was opened from world or from another battleground context.

`[RTG][REPLAY][ENTER_BG]`
- Confirms the viewer actually entered the replay battleground.
- Should match the same replay battleground instance captured in `LOCK`.

### HUD and addon path
`[RTG][REPLAY][HUD]`
- Shows whether replay HUD messages were allowed or blocked.
- Critical fields: `allowed`, `reason`, `playerBg`, `sessionBg`.
- Most useful for diagnosing “addon looks dead” or “POV button does nothing” issues.

`[RTG][REPLAY][POV]`
- Confirms which actor the server believes is the active POV.
- Should update after replay start and after manual prev/next actions.

`[RTG][REPLAY][WATCHERS]`
- Confirms watcher strip state sent to the HUD.
- Useful when the addon appears active but the viewer list is stale or empty.

### Actor selection / camera
`[RTG][REPLAY][STEP]`
- Emitted on manual actor previous/next changes.
- If button presses happen and `STEP` does not appear, command routing is broken.

`[RTG][REPLAY][APPLY]`
- Emitted when the actor camera application actually updates.
- Critical fields: `actorGuid`, `actorName`, `actorIndex`, `x`, `y`, `z`, `o`, `force`.
- If `STEP` appears without `APPLY`, actor selection moved but camera application failed.

### Playback
`[RTG][REPLAY][PLAYBACK]`
- Once-per-second heartbeat when playback logging is enabled.
- Shows packet drain, replay time, replayComplete state, and last applied actor.
- Useful for proving whether replay time is advancing or frozen.

### Exit / teardown
`[RTG][REPLAY][EXIT_REQUEST]`
- Single ownership handoff into replay teardown.
- Shows reason, delay, execute time, and whether battleground leave is preferred.

`[RTG][REPLAY][EXIT_PENDING]`
- Optional verbose heartbeat while waiting for the scheduled teardown window.
- Useful when packet drain finishes and you need to prove teardown is intentionally delayed rather than stuck.

`[RTG][REPLAY][EXIT_BEGIN]`
- The teardown owner is actively executing.
- If this appears more than once for the same trace, you have a double-teardown regression.

`[RTG][REPLAY][EXIT_PATH]`
- Shows which return path won: `leave_battleground`, `return_to_anchor`, or `restore_only`.
- This is the most important line for homebind diagnosis.

`[RTG][REPLAY][RETURN_STATE]`
- Final truth line after teardown.
- Confirms map, coordinates, battleground state, visibility state, and flight state.

`[RTG][REPLAY][LOGOUT]`
- Viewer logged out while a replay session existed.
- Use this to separate replay regressions from logout/interruption cases.

## Fast diagnosis patterns
### Pattern 1: Addon shows but next/prev does nothing
Check, in order:
1. Does the addon button actually trigger a command?
2. Do `STEP` logs appear?
3. If `STEP` appears, do `APPLY` logs appear?
4. If `APPLY` appears, do `POV` logs update?

Likely causes:
- addon command path broken
- actor track selection empty or invalid
- session teardown already active
- replay HUD gating denying outbound state updates

### Pattern 2: Replay HUD never appears
Check:
- `LOCK`
- `ENTER_BG`
- `HUD`

Most likely causes:
- session bg mismatch
- replay not loaded for the owner
- teardown already started
- viewer not actually inside replay battleground

### Pattern 3: Replay ends and viewer goes homebind / wrong place
Check:
- `EXIT_REQUEST`
- `EXIT_BEGIN`
- `EXIT_PATH`
- `RETURN_STATE`

Most likely causes:
- exit path chose battleground leave when anchor return should have been used
- anchor state invalid or zero
- double teardown / battleground-end race

### Pattern 4: Viewer crashes or disconnects at replay end
Check:
- did `EXIT_REQUEST` happen exactly once?
- did packet stream completion immediately collide with battleground end?
- did `EXIT_BEGIN` happen only once?
- was the replay still sending actor updates after teardown request?

Most likely causes:
- replay end race
- packet playback continuing into teardown window
- stale spectator state or invalid final teleports

### Pattern 5: Camera jitters, falls, or teleports erratically
Check:
- `APPLY` cadence
- packet heartbeat
- final `RETURN_STATE`

Likely causes:
- follow distance too small
- teleport snap threshold too low/high
- gravity/hover restoration not happening after replay
- actor track frames too sparse or malformed

## Proving checklist for one healthy replay
You want to see a clean chain for one `trace=`:
1. `LOCK`
2. `ENTER_BG`
3. `POV`
4. `WATCHERS`
5. `PLAYBACK` heartbeats
6. optional `STEP` / `APPLY` when manually switching
7. `EXIT_REQUEST`
8. `EXIT_BEGIN`
9. `EXIT_PATH`
10. `RETURN_STATE`

## Recommended debug settings
### Standard proving pass
```ini
ArenaReplay.Debug.Enable = 1
ArenaReplay.Debug.Verbose = 0
ArenaReplay.Debug.LogHud = 1
ArenaReplay.Debug.LogActors = 1
ArenaReplay.Debug.LogPlayback = 0
ArenaReplay.Debug.LogTeardown = 1
ArenaReplay.Debug.LogReturn = 1
```

### Deep replay surgery pass
```ini
ArenaReplay.Debug.Enable = 1
ArenaReplay.Debug.Verbose = 1
ArenaReplay.Debug.LogHud = 1
ArenaReplay.Debug.LogActors = 1
ArenaReplay.Debug.LogPlayback = 1
ArenaReplay.Debug.LogTeardown = 1
ArenaReplay.Debug.LogReturn = 1
```

## Operational rule
Do not skip the atlas and jump straight to speculative fixes. ArenaReplay regressions are almost always easier to solve once the exact lifecycle phase is proven.


### Pattern 6: Participant self-watch only shows the enemy team
Check:
- whether the replay has actor tracks at all
- whether `StartOnSelfWhenParticipant` is enabled
- whether `APPLY` lines show `selfActorView=1` when the participant track is selected

Likely causes:
- viewer actor track missing from the saved replay
- participant selection never landed on the viewer track
- viewer was still being hidden during self-actor application

### Pattern 7: Replay exit leaves the player hovering or unable to jump
Check:
- `EXIT_BEGIN`
- `EXIT_PATH`
- `RETURN_STATE`
- whether cleanup happened both before and after the final return path

Likely causes:
- gravity/hover/client-control cleanup only happened before battleground leave or anchor return
- actor-view state was not reset before teardown
- release fallback path skipped full viewer-state restoration
