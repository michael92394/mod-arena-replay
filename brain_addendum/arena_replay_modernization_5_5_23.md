# Arena Replay Modernization 5.5.23 - Visible Replay Polish + Buff Timeline Clarity

## Situation
The 5.5.22 pass finally restored visible replay actors by enabling backend-2 server clone fallback while leaving synthetic UNIT packet emission disabled by default. The follow-up evidence showed six actors created, six visible bindings, valid POV cycling, and successful cleanup/return. The remaining rough edges were polish issues around replay HUD transport noise and arena object timing clarity.

The log showed the new buff-object delay behavior was functionally delaying crystals, but the diagnostic line reported the raw playback timestamp, e.g. `activateMs=151000`, while the visible replay HUD starts at match time zero. That made the crystal timeline look wrong even when the intended activation was the normal 90 seconds after visible match start.

The client screenshot also showed raw `[RTG_REPLAY]` system payloads visible in chat. Those messages are intentionally sent as system text for compatibility, but the addon should consume and hide them so the replay HUD feels clean.

## Hypothesis
The buff activation check should be expressed against visible match elapsed time instead of the raw internal replay playback clock. The raw clock is still useful for internal scheduling, but operator-facing logs should show both the visible activation time and the backing playback timestamp.

The replay addon can suppress `[RTG_REPLAY]` system messages with a `CHAT_MSG_SYSTEM` chat-frame filter while still receiving the system event and feeding the HUD parser.

## Experiments
- Added explicit buff delay helper functions:
  - visible activation time: match-elapsed delay, normally 90,000 ms;
  - playback activation time: `session.matchOpenMs + visibleDelay`.
- Changed buff activation to trigger from `GetReplayMatchElapsedMs(session)` rather than comparing only the raw replay playback clock.
- Updated object timeline logs to include both visible and playback activation timings.
- Added a `BUFF_OBJECT_ACTIVATE` log when delayed buff crystals are spawned on activation.
- Added a replay HUD chat-frame filter in `RTG_ReplayHUD.lua` so raw `[RTG_REPLAY]` system payloads are hidden from chat while remaining parseable by the addon.

## Findings
Backend-2 clone fallback is now the stable visual layer and should remain the default while packet-only synthetic UNIT actors continue to be treated as diagnostics. The object layer now distinguishes visible replay time from raw playback time, which should make crystal activation easier to verify in future logs.

## Regression
Do not re-enable packet-only synthetic actor rendering by default. It previously produced invisible hitboxes and floating weapons. Keep `EmitUnitVisualPackets = 0` unless specifically testing packet shape.

Do not spawn buff crystals at scene initialization. They must remain delayed until the configured activation time in visible match time.

## Next Investigation
- Verify crystal spawn/activation in Nagrand, Blade's Edge, Ruins, Dalaran, and Ring of Valor at visible match time ~90 seconds.
- Continue improving clone silhouettes with better class/team displays without returning to raw player display IDs on creature clones.
- Investigate true player-object packet rendering separately from the working clone fallback path.

## Status
5.5.23 keeps the actor visibility breakthrough intact, cleans HUD chat noise, and makes buff timing diagnostics align with visible replay time.
