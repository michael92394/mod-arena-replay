# Arena Replay modernization addendum 5.2.1

## Theme
Separate the replay viewer from combatant state and harden spectator camera behavior.

## Delivered in this pass
- Replay viewers now enter playback on `TEAM_NEUTRAL` so replay entry does not seed Gold/Green team counts.
- Fixed replay HUD/chat output path to use AzerothCore-safe formatting instead of leaving literal `%s` placeholders in-game.
- Added replay-viewer movement stabilization during playback (`SetCanFly`, gravity disabled, hover enabled during replay session only).
- Switched actor-follow to interpolated actor frames for less jittery replay camera snaps.
- Fixed replay fallback anchoring so the viewer is held in replay-space, not compared against the pre-replay world map anchor.
- Fixed replay exit flow so the saved anchor survives long enough to return the viewer correctly.

## Why this matters
This pass targets the exact modernization pain points still breaking the Apex-style replay feel:
1. the viewer should never become a team member
2. only replay actors should define replay combat state
3. the camera should feel intentional instead of gravity-driven or rubber-banded
4. replay sessions should start and end without corrupt spectator state

## Next logical modernization targets
- explicit replay-side world state reconstruction for team alive counters when legacy arena UI still disagrees with actor-track truth
- optional cinematic camera modes (freecam / fixed follow / killer POV / winner POV)
- replay timeline scrubbing metadata and actor event markers
- clone-side scoreboard overlay sourced from actor-track truth instead of legacy battleground state
