# Arena Replay Modernization 5.5.24 - Packet-Qualified Replay Gate

## Chapter 5.5.24

**Date:** 2026-05-16  
**Subsystem:** mod-arena-replay / replay quality gate / packet playback pivot

## Situation

Visible clone fallback finally proved the replay scene, camera anchor, POV cycling, arena copy maps, return restore, and delayed buff-object handling can function. However, the visible result is not production quality: actor bodies are creature silhouettes/NPC illusions, not the recorded player packet stream. The latest test also continued to show chat transport noise and proved the clone fallback is only a diagnostic shell.

The most important runtime evidence is repeated single-team packet capture:

```text
[RTG][REPLAY][PACKET_CAPTURE_SUMMARY] ... team0Packets=2463 team1Packets=0 ... result=single_team_or_empty
```

This means player-vs-bot or one-real-team matches can produce path/snapshot data and clone visualization, but not the real player packet coverage needed for faithful replays.

## Hypothesis

Arena replays should stop presenting clone/path reconstructions as normal replays. A replay is production-viewable only when it has usable packet coverage for both teams. Single-team packet rows, path-only rows, bot-only rows, and legacy rows without quality metadata should be hidden unless debug/admin overrides are enabled.

The current packet capture hook records from outbound packets sent to real WorldSessions. That explains why bot-only teams often generate no opposite-side receiver packet stream: the bots can move and be sampled into actor tracks, but they do not necessarily provide a replayable client packet perspective.

## Experiments / Changes

1. Added replay quality metadata fields to `character_arena_replays`:
   - `replayQuality`
   - `qualityReason`
   - `packetQualified`
   - `visibleInBrowser`
   - `team0PacketCount`
   - `team1PacketCount`
   - `neutralPacketCount`
   - `skippedPacketCount`
   - `team0RealPlayerCount`
   - `team1RealPlayerCount`
   - `winnerPacketCount`
   - `loserPacketCount`
   - `winnerRealPlayerCount`
   - `loserRealPlayerCount`

2. Added quality classification at save time:
   - `PACKET_FULL`
   - `PACKET_PARTIAL`
   - `PATH_ONLY`
   - `BOT_ONLY_OR_INVALID`

3. Added browser/direct-load filtering so normal replay lists only show rows where:
   - `packetQualified = 1`
   - `visibleInBrowser = 1`

4. Added direct Match ID blocking for unqualified replays unless explicitly overridden by config.

5. Changed production defaults:
   - `ArenaReplay.ActorVisual.Backend = 3`
   - `ArenaReplay.ActorVisual.RecordedPacketStream.Enable = 1`
   - `ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.ServerCloneFallback.Enable = 0`
   - `ArenaReplay.CloneScene.Enable = 0`
   - `ArenaReplay.HUD.VisibleChatDebug = 0`

6. Added full AzerothCore-style config documentation for quality gates and debug overrides.

## Findings

Clone fallback should remain useful only as a debug visualization tool. It is not faithful enough for the RTG replay product goal because creature clones cannot accurately reproduce full player model packets, animation sequence, aura state, or combat visuals.

The replay browser should now stop surfacing the most embarrassing rows: the single-team/bot/path-only records that caused stress during testing.

## Regression Risk

- Existing legacy rows will default to `legacy_unqualified` and hidden after the SQL update. This is intentional for production curation but may surprise testers who expect old clone/path replays to appear.
- If the SQL update is not applied, browser SQL filtering cannot use quality columns. Direct load will still block legacy/unqualified rows unless `ArenaReplay.Quality.AllowLegacyUnqualifiedDirectLoad = 1` is enabled.
- `ArenaReplay.HUD.VisibleChatDebug = 0` suppresses visible system-message HUD payloads. This cuts chat noise, but a future hidden addon transport is still needed if the custom HUD must update without visible chat lines.

## Next Investigation

The next real replay milestone is not clone polish. It is broadcast-level packet capture and safe GUID rewriting:

1. Audit `SERVERHOOK_CAN_PACKET_SEND` capture ownership.
2. Prove why one team repeatedly has zero packet coverage.
3. Capture broadcast-visible packets in a way that includes both teams when both teams contain real client sessions.
4. Store original actor GUID ownership and object-event state transitions.
5. Rewrite original GUIDs into replay-safe GUIDs during playback.
6. Use recorded object events for Dalaran water, gates, elevators, and crystals instead of timer guessing.

## Status

**5.5.24 pivots the module toward packet-qualified production replays.** Clone fallback is now debug-only by doctrine/defaults. The normal replay browser and direct replay loading are gated so player-vs-bot/single-team packet captures are hidden unless explicitly allowed for debugging.
