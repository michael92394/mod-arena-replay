# Arena Replay modernization 5.4.2 — clone appearance snapshot layer

## Summary
- Split clone-scene defaults so the camera anchor uses invisible world entry `18793` and actor clones use dedicated replay actor entry `98501`.
- Added `character_arena_replay_actor_snapshot` persistence for replay actor appearance data.
- Captured per-actor replay appearance snapshots at save time and reloaded them for playback-time clone construction.
- Applied recorded display/native display ids and weapon item display ids onto clone-scene creatures at spawn time.

## Why
The prior clone-scene architecture could move actor surrogates but could not make them look like the recorded players. This pass adds the first durable visual identity bridge so the replay system can evolve away from the loose spectator-goblin method.

## Current snapshot payload
- actor GUID
- winner/loser side
- class / race / gender / name
- display id / native display id
- mainhand / offhand / ranged item display ids

## Limits
- This is not yet full armor-cosmetic parity.
- The current pass intentionally avoids trying to serialize every player-bytes or shapeshift variant until the new storage path is proven stable.
- Creature clones can now carry better identity and weapon visuals, but future passes are still needed for helm/cloak/body customization fidelity.

## Operational notes
- World DB now needs replay actor clone template `98501`.
- Characters DB now needs table `character_arena_replay_actor_snapshot`.
- Camera anchor should remain on invisible entry `18793`, not the replay actor clone template.
