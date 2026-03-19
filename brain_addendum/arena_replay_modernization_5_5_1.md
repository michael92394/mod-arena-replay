# ArenaReplay modernization 5.5.1 — fresh replay arena shell bootstrap

## Goal
Repair sandbox playback startup for arena map IDs that refused naked `TeleportTo(mapId, ...)` entry and timed out on the source world map.

## Findings carried into this pass
- The 5.5.0 sandbox shift removed dead-instance reuse correctly, but arena maps like `559` and `572` still would not accept a plain world teleport because they require a valid battleground/arena instance shell.
- Attach-timeout logs showed `playerMap` staying on the source world map while `replayMap` pointed at the arena map.
- This proved the next blocker was instance ownership, not clone scene or spectator-shell teardown.
- `mod-npc-spectator` remained valuable as spectator-state reference, but not as replay lifecycle owner.

## What changed
- Replaced naked replay-map `TeleportTo(...)` startup with creation of a **fresh replay-owned arena instance shell** through `BattlegroundMgr::CreateNewBattleground(...)`.
- Replay startup now sends the viewer into that fresh shell with `SendToBattleground(...)` instead of trying to teleport directly into battleground/arena maps with no instance ownership.
- The replay session now stores the fresh shell instance id before playback attach validation starts.
- Sandbox attach validation now requires both replay map match and replay battleground-instance match.
- Replay-owned battleground update logic is explicitly skipped for sessions already running under sandbox ownership so the old battleground replay ticker does not double-drive playback.
- Replay teardown preference now correctly allows leaving the fresh replay battleground shell even when the viewer started from the open world (`priorBg = 0`).

## Architecture note
ArenaReplay sandbox mode is now refined to:
1. lock hidden spectator shell,
2. create fresh replay-owned arena instance shell,
3. send viewer into that shell,
4. wait for confirmed map + battleground-instance attach,
5. bootstrap clone scene / camera anchor / HUD from world-update playback clock,
6. leave replay shell and return to anchor on teardown.

## Remaining caution
This pass is aimed at the arena-map entry blocker. If a future test still fails to attach, the next investigation surface should be shell creation / `SendToBattleground(...)` acceptance / spectator-state interaction with the fresh shell — not a return to dead replay instance reuse.
