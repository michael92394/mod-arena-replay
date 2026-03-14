# Arena Replay modernization addendum 5.2.4

## Theme
Repair replay exit/homebind churn and stop replay HUD state from leaking outside active replay sessions.

## Delivered in this pass
- Bound replay battleground entry/exit bookkeeping to the viewer's real faction team instead of `TEAM_NEUTRAL`.
- Documented why neutral team transport is unsafe for replay viewers on some arena maps.
- Gated replay HUD sysmessages so START/POV/WATCHERS only emit for active replay sessions.
- Sent replay HUD END proactively when opening the replay service and on player logout.
- Hardened replay movement cleanup to always clear fly / gravity / hover regardless of partial session flags.
- Re-applied movement cleanup after returning the player to their pre-replay anchor.

## Why this matters
The recurring `Map 0 could not be created` / homebind churn strongly suggests replay viewers were leaving the battleground through an invalid neutral-team path. At the same time, stale HUD messages can confuse the replay addon outside playback and increase client instability.

## Testing targets
- leaving replay should no longer produce repeated Map 0 / homebind churn
- replay viewer should return cleanly to the original map and position
- replay HUD/menu should not continue updating outside active replay playback
- repeated open/close replay service usage should not leave the addon in replay mode
