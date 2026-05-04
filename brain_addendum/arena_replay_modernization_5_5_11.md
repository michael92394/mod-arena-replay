# Arena Replay Modernization 5.5.11 — Recorded Packet Visual Backend

## Chapter
5.5.11

## Date
2026-05-03

## Subsystem
mod-arena-replay copied-instance replay actor visuals

## Context
Creature silhouette actors successfully proved copied-map replay timing, object spawning, camera ownership, appearance capture, and cleanup. However, repeated tests showed the creature actor backend could not provide production-quality player visuals or movement animation.

## Situation
The replay system now captures full appearance and equipment metadata, and weapon item entries apply correctly. Despite that, creature-backed actors still appear as generic fallback bodies, pale player-display silhouettes, or repeated shell models. They also slide over recorded coordinates because they are not real player movement objects.

## Finding
The practical production path for immediate real-looking replay playback is to let the original recorded packet stream own player visuals and animation again while retaining copied-map sandboxing, replay objects, hidden spectator body, camera anchor, and cleanup. Recorded object/movement/spell/combat packets contain the client-native player visuals and animation data that creature clones cannot reconstruct.

## Change
Added recorded_packet_stream actor visual backend:

- `ArenaReplay.ActorVisual.Backend = 3`
- `ArenaReplay.ActorVisual.RecordedPacketStream.Enable = 1`
- recorded world/object/movement packets are allowed through when this backend is active
- creature actor clone prewarm is bypassed in recorded-packet mode
- clone scene can be disabled without causing replay startup failure in recorded-packet mode
- copied replay maps, session-spawned arena objects, hidden spectator body, and camera anchor remain active

## Intended Behavior
Recorded-packet visual mode should show real replay player bodies and client-side movement/combat animation from the original replay stream instead of creature clone silhouettes.

## Preserved Fallback
Creature silhouette remains available as a debug/fallback backend for map/object/camera testing, but it is no longer treated as the preferred visual backend.

## Testing Focus
After applying this pass:

1. Record a fresh arena replay.
2. Watch it with `ArenaReplay.ActorVisual.Backend = 3`.
3. Verify logs contain `ACTOR_VISUAL_BACKEND backend=recorded_packet_stream` and `PACKET_VISUAL_SCENE`.
4. Verify no creature clone silhouettes/Blood Elf fallback bodies appear.
5. Verify real-looking player objects, swings, spell visuals, and movement packets are visible.
6. Verify viewer body remains hidden and restore still clears flight/gravity state.
