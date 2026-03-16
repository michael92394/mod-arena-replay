# Arena Replay modernization addendum 5.3.1

## Theme
Replay traceability and session-ownership correction for production hardening.

## Delivered in this pass
- Added structured replay trace logging with per-session `trace` identifiers.
- Added config-driven replay diagnostic surfaces for HUD, actor switching, playback heartbeat, teardown, and return verification.
- Corrected replay session battleground ownership so the session stores the replay battleground instance instead of the pre-transfer battleground id.
- Added replay HUD allow/deny reason tracing to expose addon/HUD gating failures.
- Added actor-step and actor-apply trace lines to make `.rtgreplay next` / `.rtgreplay prev` testable in one pass.
- Added teardown reason logging for packet-stream completion, battleground-end shutdown, and logout.
- Added final return-state logging so homebind / bad-return / stuck-flight outcomes can be proven from logs instead of guessed.

## Why this matters
The replay stack was still too opaque to move confidently toward production quality. The most likely blocker for addon POV switching was stale session battleground ownership, and the most frustrating replay-end issues were impossible to prove from existing logs. This pass turns the replay viewer lifecycle into a traceable sequence.

## Intended proving experiments
1. Enable `ArenaReplay.Debug.Enable = 1` and reproduce one replay open.
2. Confirm `LOCK` and `ENTER_BG` show the replay battleground instance, not the source battleground id.
3. Click HUD next/prev and verify `STEP`, `APPLY`, and `POV` lines align.
4. Let a replay end naturally and inspect `EXIT_BEGIN`, `EXIT_ACTION`, and `RETURN_STATE`.
5. Compare successful and failing runs by `trace=` value.
