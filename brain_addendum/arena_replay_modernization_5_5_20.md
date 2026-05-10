# Arena Replay Modernization 5.5.20 — Synthetic Living Movement Packet + Return Anchor Repair

## Chapter 5.5.20

**Date:** 2026-05-10  
**Subsystem:** `mod-arena-replay` copied-map replay playback, Option C synthetic actor visuals, replay teardown/return safety

## Context

The 5.5.19 test removed the active config spam, but the visible result remained effectively unchanged: replay actors were still invisible, the viewer became a floating/weapon-only sword shell near the enemy spawn/gate after playback, and replay exit left the viewer inside the copied arena map. A second replay attempt from the copied map then captured the copied map as the return anchor and could return the viewer to an unrelated faction fallback location.

The decisive log evidence was:

- `SYNTHETIC_ACTOR_PLAN` planned all 6 actors.
- `SYNTHETIC_ACTOR_CREATE` sent all 6 synthetic create packets.
- `SYNTHETIC_CREATE_SUMMARY` reported `createdThisPass=6 totalCreated=6 plannedActors=6 result=ok`.
- Teardown restored state but `STATE_RESTORE` still showed `playerMap=725`, meaning no real return teleport happened.
- `DISPLAY_RESTORE` trusted `priorDisplay=23691`, which matched the observed weapon-only/sword shell behavior.

## Situation

This is no longer a missing-config problem. The current fault has two independent roots:

1. **Map 0 return anchors were treated as “no anchor.”** Stormwind/Eastern Kingdoms is map `0`, but previous teardown only returned when `anchorMapId != 0`. Alliance viewers starting from Stormwind therefore never triggered `RETURN_TELEPORT`, remained inside copied map `725`, and poisoned the next replay start.

2. **Synthetic UNIT living movement packets were missing the speed float table.** WotLK clients expect living update blocks to include MovementInfo followed by movement speeds. Omitting the speed table can cause the client to misread the following update-value mask, producing invisible actors or weapon-only ghosts even though server logs show create packets were sent.

## Hypothesis

If the synthetic UNIT create/move packet is given a full living MovementInfo block plus movement speed floats, the client should parse the update field mask correctly and render actor bodies. If the return anchor is tracked with an explicit `anchorCaptured` boolean rather than using map id `0` as a sentinel, Alliance viewers starting in Stormwind should be returned to Stormwind instead of being stranded in the copied arena.

## Experiments Applied

### 1. Explicit return-anchor capture

Added `ActiveReplaySession::anchorCaptured` so map id `0` is legal and no longer means “missing anchor.”

### 2. Copied-map anchor sanitization

If replay is launched while the viewer is already in map `725-729`, the module now refuses to capture that copied map as the return target and instead uses faction-safe fallback coordinates.

### 3. Faction-safe fallbacks

Added separate fallback coordinates:

- Alliance fallback: Stormwind, map `0`
- Horde fallback: Orgrimmar, map `1`

This prevents Alliance viewers from being sent to the old generic Horde/Orgrimmar fallback after a bad copied-map replay start.

### 4. Native display restoration

Added `ArenaReplay.SpectatorShell.RestoreNativeDisplayOnExit = 1` by default. Teardown now restores the viewer to their native playable display instead of trusting a captured prior display that may already be a bad replay/sword shell.

### 5. Full synthetic living movement block

Changed synthetic movement mode default to `2`, which appends the WotLK living movement speed table after MovementInfo.

### 6. Cleaner synthetic visibility smoke defaults

During visibility testing:

- `UsePlayerDisplayIds = 1`
- `UnitFlags = 0`
- `FallbackDisplayId = 49`

This removes fallback-display ambiguity and hidden/nonselectable flag ambiguity while actor packet visibility is still being proven.

## Findings

The previous test proved that actor capture, actor planning, copied-map attach, and packet send loops were all active. The lack of visible bodies now points to packet shape/client parsing rather than replay data absence.

The return failure was concrete: teardown did not emit `RETURN_TELEPORT` because Stormwind’s map id `0` was incorrectly treated as no anchor by the return condition.

## Regression Risk

Medium.

- The return-anchor fix is low risk and directly corrects a bad sentinel assumption.
- The display restore fix is intentionally conservative for replay viewers; it may clear temporary morph visuals on exit, but that is acceptable for recovering from replay spectator corruption.
- The synthetic packet speed-table fix is the next correct WotLK packet-shape attempt, but packet-only visual actors remain experimental.

## Next Investigation

Retest replay 114 or a fresh replay from Stormwind. Confirm these log lines:

```txt
[RTG][REPLAY][SYNTHETIC_ACTOR_CREATE] ... result=sent
[RTG][REPLAY][SYNTHETIC_CREATE_SUMMARY] ... totalCreated=6 plannedActors=6 result=ok
[RTG][REPLAY][DISPLAY_RESTORE] ... source=native_display_forced result=ok
[RTG][REPLAY][RETURN_TELEPORT] ... source=anchor targetMap=0 ... result=ok
[RTG][REPLAY][STATE_RESTORE] ... playerMap=0 ... result=teardown
```

If actors remain invisible after mode `2`, the next repair should stop hand-writing generic `SMSG_UPDATE_OBJECT` payloads and instead build synthetic visuals through server-owned temporary units/creatures or a core-native update builder, then optionally rewrite GUIDs per viewer.

## Status

5.5.20 is a targeted repair pass for:

- Alliance map `0` return-anchor bug
- copied-map replay re-entry poisoning
- sword/weapon-shell display restoration
- synthetic UNIT movement-packet parsing completeness
