# This is a module for ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore

## mod-arena-replay

# ![mod-arena-replay](https://github.com/azerothcore/mod-arena-replay/blob/main/icon.png?raw=true)

The arena replay module allows you to watch a replay of rated arena games (I think it can be modified to Replay BGs and Raid instances as well).

You can see a little bit of how the module works here:
https://www.youtube.com/watch?v=7z0RA6Dsm9s

### Known issue

it's not 100% done yet because if a player tries to watch a Replay of a game that he was in, the replay acts weirdly. Words of the guy who updated the module:
"try spectating as a player who wasn't involved in the arena
I need to change it so that it uses new duplicate players instead of"

### Usage

`.npc add 98500`

![image](https://github.com/user-attachments/assets/221b2304-218e-4a7b-a7c3-0cf07388319d)


### Credits

- Romain-P ([original author](https://gist.github.com/Romain-P/069749c3acced35f4b0ae6841cb94e79))
- thomasjteachey
- Laasker
- Helias


## RTG modernization notes

This branch adds a new **Replay Actor Spectate** layer. It does not yet spawn fully remapped clone units, but it records per-combatant actor tracks and uses them to drive a spectator follow camera that auto-cycles targets. That makes self-watch dramatically safer than the legacy packet-only camera path and moves the module closer to a future full clone-target implementation.

### New replay data
- `winnerActorTrack`
- `loserActorTrack`

### New config
- `ArenaReplay.ActorSpectate.Enable`
- `ArenaReplay.ActorSpectate.AutoCycleMs`
- `ArenaReplay.ActorSpectate.FollowDistance`
- `ArenaReplay.ActorSpectate.FollowHeight`
- `ArenaReplay.ActorSpectate.StartOnWinnerTeam`
- `ArenaReplay.ActorSpectate.StartOnSelfWhenParticipant`

### Important limitation
This is **clone-target spectating scaffolding**, not the final cloned-actor / GUID-remap engine. The camera now follows recorded participant tracks instead of relying only on winner/loser POV anchor tracks, but it still does not spawn fully remapped duplicate units yet.


## RTG 5.2.7 replay stabilization notes

- Replay HUD watcher updates are deduplicated instead of re-sent every tick.
- Replay playback now uses a capped packet budget per battleground update to reduce hitching on older replays.
- Replay exit prefers the normal battleground return path when available instead of forcing an extra anchor teleport on top of battleground leave.
- Replay actor selection only treats tracks with valid GUIDs and finite frame data as playable POV targets.


## RTG 5.2.8 arena-only stabilization notes

This packaging is now tuned primarily for **arena-only replay** on RTG. Battleground replay recording is disabled by default in the distributed config.

### Included fixes
- Added replay HUD gating helper so replay-only HUD messages do not compile-break or leak as easily outside active replay sessions.
- Added actor-track sanitization during replay load so malformed or non-finite actor frames from older replays are filtered out before POV selection.
- Tightened playable POV selection to valid actor tracks only.
- Added immediate camera re-apply for `.rtgreplay next` / `.rtgreplay prev` so manual POV switching forces a new spectator teleport right away.
- Added a replay-end grace window before teardown to reduce abrupt end-of-replay crashes.
- Replaced aggressive replay packet bursts with a configurable per-update packet budget via `ArenaReplay.Playback.PacketBudgetPerUpdate`.
- Simplified replay exit toward a single anchor-return path instead of stacking battleground-leave and manual teleport in the same teardown.

### Current architecture note
This remains a **hidden spectator-anchor replay system**, not a full cloned-actor renderer. The module is materially more stable for arena-only use, but legacy replay compatibility still depends on the quality of the recorded packet/actor data.
