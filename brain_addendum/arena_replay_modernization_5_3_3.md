# Arena Replay modernization addendum 5.3.3

## Theme
Config truth reconciliation, participant self-watch recovery, and replay-exit movement-state hardening.

## Delivered in this pass
- Audited the module config surface directly against `src/ArenaReplay.cpp` and confirmed the active ArenaReplay config contract is the current 34-key set already represented in `conf/arena_replay.conf.dist`.
- Reconciled the distributed config narrative so it stays grounded on actually used keys only, with refreshed explanations for participant self-watch and replay-exit cleanup behavior.
- Added a **viewer-avatar self-actor bridge**: when a replay participant is spectating their own actor track, the live viewer avatar remains visible instead of being hidden like an off-screen spectator anchor.
- Added self-actor visibility state to the active replay session so replay apply/reset/teardown can reason explicitly about whether the current POV is using the viewer as the self actor.
- Hardened replay exit by resetting actor-view state before teardown and by re-applying viewer-state restoration after the selected return path, reducing stuck hover/levitation/control fallout.
- Strengthened the no-session release fallback so visibility, control, and movement cleanup are still restored even if the active replay session record is already gone.
- Updated the README so it no longer describes self-watch as categorically broken and instead documents the new bridge architecture honestly.

## What was proven true from the code audit
The following config families are actively consumed by the current code path:
- core replay lifecycle
- special arena type routing (`1v1`, `3v3soloQ`)
- library browsing / recently watched
- actor spectate
- spectator-only controls
- playback packet budget
- replay debug toggles

No extra "paper config" families are required for the current build beyond those already present in `conf/arena_replay.conf.dist`.

## Why this matters
The missing-config spam from runtime logs made it look like the module config surface might still be drifting. The code audit showed the opposite: the distributed config was already close to the real truth surface, while the runtime config being used by the realm was simply missing a subset of currently active keys. That means the right move is to keep the config contract tight, not inflate it.

The self-watch issue also turned out to be more specific than "own replay is impossible." The module already selected the participant's actor track when actor data existed, but the viewer was still being forcibly hidden during actor application. That made self-watch feel like the viewer could only ever observe the opposing team. This pass removes that specific blind spot without pretending the full fake-actor renderer has been built.

## Remaining limitation
This is still **not** the final duplicate-actor / GUID-remap replay scene. It is a self-watch bridge built on the current actor-follow architecture. A future full fake-actor pass would still be needed for truly separate duplicate-unit playback.

## Intended proving experiments
1. Watch a replay as a participant whose actor track exists.
2. Confirm the first selected POV can remain on the participant and that their own side is no longer reduced to a permanently hidden spectator-anchor experience.
3. With debug enabled, verify `APPLY` lines include `selfActorView=1` when the participant's own actor track is selected.
4. Let a replay finish naturally and confirm the player exits without being stuck hovering, unable to jump, or otherwise trapped in replay movement state.
5. Re-run the realm with the cleaned config block present and confirm the repeated missing-config spam for the actor-spectate/debug keys disappears.
