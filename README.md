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

## Replay diagnostics and proving workflow
For active hardening, enable the replay trace spine in `conf/arena_replay.conf.dist`:

- `ArenaReplay.Debug.Enable = 1`
- `ArenaReplay.Debug.LogHud = 1`
- `ArenaReplay.Debug.LogActors = 1`
- `ArenaReplay.Debug.LogTeardown = 1`
- `ArenaReplay.Debug.LogReturn = 1`
- optionally `ArenaReplay.Debug.LogPlayback = 1` for once-per-second replay heartbeat lines

The trace output uses a per-session `trace=` id so one replay run can be followed from:

- `LOCK`
- `ENTER_BG`
- `HUD` / `POV` / `WATCHERS`
- `STEP` / `APPLY`
- `PLAYBACK`
- `EXIT_BEGIN` / `EXIT_ACTION`
- `RETURN_STATE`

This is the intended basis for proving:

- whether replay HUD gating is failing
- whether actor-switch commands are applying a new POV immediately
- which teardown owner path is returning the player badly
- whether the viewer exits with stuck battleground or flight state



## RTG 5.3.0 arena replay hardening notes

This pass focuses on making arena-only replay playback safer and more deterministic rather than pretending the module is already a perfect clone-renderer.

### Included fixes
- replay HUD gating now verifies the active replay session, owning battleground instance, and loaded replay payload before replay UI traffic is allowed
- watcher HUD output only counts viewers attached to the same replay session instead of every player in the battleground container
- actor-track sanitization now removes malformed frames, normalizes empty names, and de-duplicates duplicate actor GUID tracks by keeping the strongest usable track
- replay POV application now forces an immediate camera reposition when the selected actor changes instead of waiting for the normal follow throttle window
- replay playback now drops unsafe client-origin opcodes from old recordings and refuses obviously oversized packet payloads during replay deserialization
- replay exit now prefers one battleground leave path when still attached to the replay battleground, with manual anchor return only as a fallback
- arena-only playback pacing defaults were reduced to safer values for packet burst budget and actor follow teleport cadence

### Current architecture note
This is still a hidden spectator-anchor replay system. It is substantially tighter and safer for arena replay viewing, but it is not yet a full cloned-actor scene renderer.


## RTG 5.3.2 deterministic teardown and camera notes

This pass moves ArenaReplay closer to production-readiness by making replay shutdown deterministic and reducing spectator-camera instability.

### Included fixes
- Added a replay teardown request/execute pipeline so replay completion, battleground end, and logout converge on one authoritative exit owner.
- Delayed packet-stream-complete teardown into a scheduled exit window instead of collapsing playback and teardown in the same update tick.
- Exit path selection now prefers anchor return for normal world-opened replay sessions and only prefers battleground leave when the viewer actually came from a battleground context.
- Viewer state restoration now normalizes control, visibility, gravity, hover, and fall state before session release.
- Actor-follow camera now supports configurable follow distance, follow height, snap distance, and orientation interpolation to reduce jitter and actor clipping.
- Added a dedicated debugging atlas at `brain_addendum/arena_replay_debugging_atlas.md` for future proving and repair passes.
- Updated the Replay HUD addon button path to drive replay prev/next through the chat command pipeline more safely.

### Current architecture note
This is still a hidden spectator-anchor replay system. It is now more deterministic, easier to debug, and less prone to replay-end fallout, but it is not yet a full clone-rendered replay scene.
