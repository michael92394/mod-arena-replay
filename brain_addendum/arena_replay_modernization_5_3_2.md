# Arena Replay modernization addendum 5.3.2

## Theme
Deterministic replay teardown ownership, safer replay return sequencing, and spectator camera hardening.

## Delivered in this pass
- Added a single replay teardown request path via `RequestReplayTeardown(...)` so replay completion, battleground end, and logout all converge on the same exit-owner logic.
- Added `PerformReplayTeardown(...)` as the only function that restores viewer state, clears replay ownership, and chooses the final return path.
- Added explicit teardown state to the active replay session: request time, execute time, preferred exit mode, HUD-end sent state, replay battleground ended state, and camera stall counters.
- Changed replay completion from immediate teardown to a delayed teardown request so the final packet drain and the exit pipeline cannot collide in the same update window.
- Reworked exit path selection so battleground leave is only preferred when the viewer actually entered from another battleground context; otherwise replay exit returns the viewer to the stored anchor instead of risking homebind-style fallout.
- Strengthened replay viewer state restoration so gravity, hover, visibility, control lock, and fall state are normalized before the session is released.
- Hardened actor follow camera behavior with configurable follow distance, follow height, snap distance, and orientation interpolation.
- Added camera stall detection so a spectator that gets stuck or drifts can be force-corrected cleanly.
- Updated the addon HUD buttons to submit replay commands through the chat edit pipeline before falling back to SAY, improving compatibility with server command handling.
- Added a dedicated ArenaReplay debugging atlas so future GPT/debug passes can reason from stable log families instead of re-learning the replay lifecycle each time.

## Why this matters
The biggest replay problems left after the trace-spine pass were not purely visibility issues. They were ownership and sequencing issues:
- multiple replay-exit callers trying to clean up the viewer
- packet-drain and teardown competing in the same frame window
- a spectator sometimes being treated like a battleground traveler instead of a world-anchor viewer
- actor follow staying too tightly glued to the actor position, amplifying jitter and end-of-replay instability

This pass turns replay exit into a controlled pipeline and gives the spectator camera a more production-worthy chase/follow behavior.

## Intended proving experiments
1. Open a replay from the world and let it finish naturally.
2. Confirm `EXIT_REQUEST`, `EXIT_PENDING` (if verbose), `EXIT_BEGIN`, `EXIT_PATH`, and `RETURN_STATE` appear once for the same `trace=`.
3. Verify the exit action is `return_to_anchor` for normal world-opened replay sessions.
4. Open a replay and use HUD next/prev repeatedly while moving actors are active.
5. Confirm `STEP`, `APPLY`, and `POV` continue while the camera remains behind the actor rather than collapsing into the actor origin.
6. Force battleground-end teardown and verify it does not double-fire after packet-stream completion.

## Operational note
This remains a hidden spectator-anchor replay architecture, not a clone-spawn renderer. The module is now much more diagnosable and deterministic, but the debugging atlas should be treated as mandatory operating material for future modernization passes.
