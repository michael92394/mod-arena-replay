# Arena Replay Modernization 5.5.3 - Copied Map Dynamic Object Playback

Date: 2026-05-03
Subsystem: mod-arena-replay / replay actor playback / copied map sandbox
Status: patch prepared

## Context

The private phase sandbox still attempted to play arena replays on the native arena map id from the replay record. Native arena maps reject normal world-style replay entry because they are battleground-owned maps, while RTG replay playback must remain outside `BattlegroundMgr`.

The copied replay maps solve entry ownership, but they do not contain the transient battleground-script objects that make arena playback look correct. Gates, buffs, Dalaran Sewers water, and Ring of Valor machinery must be spawned as replay-session-owned objects after map attach and before actor clones.

## Decision

Replay startup now resolves native arena maps to copied non-battleground maps:

- 559 -> 725
- 562 -> 726
- 572 -> 727
- 617 -> 728
- 618 -> 729

Replay viewers remain outside battleground state: `playerBg=0`, `sessionBg=0`, and `InBattleground=false`. Playback uses the copied map id with the original recorded coordinates because the copied maps are terrain copies.

Dynamic arena objects are now owned by the active replay session. They are spawned in the viewer's copied map and private phase after attach succeeds, initialized before clone scene creation, driven by `session.replayPlaybackMs`, and cleaned up only from the session-tracked GUID list.

## Runtime Shape

A healthy copied-map replay start should show:

```text
[RTG][REPLAY][MAP_RESOLVE] ... nativeMap=562 replayMap=726 result=ok
[RTG][REPLAY][ENTER_SANDBOX] ... replayMap=726 playerBg=0 sessionBg=0
[RTG][REPLAY][ATTACH_OK] ... playerMap=726 replayMap=726 playerBg=0 sessionBg=0
[RTG][REPLAY][OBJECT_SPAWN] ... entry=183971 role=GATE result=ok
[RTG][REPLAY][OBJECT_STATE] ... entry=183971 role=GATE state=open replayTimeMs=0
[RTG][REPLAY][CLONE_SCENE] ... clones=4 result=ok
[RTG][REPLAY][VIEWPOINT] ... bound=1
```

Ring of Valor additionally reports object counts and first actor Z so initial upper-floor replays can avoid visually trapping clones below the arena floor.

## Guardrail

Do not repair future copied-map replay issues by calling `CreateNewBattleground`, `SendToBattleground`, `SetupBattleground`, `SetBattlegroundId`, or `LeaveBattleground` for replay viewers. Replay playback is a copied-map cinematic sandbox, not a live battleground participant.
