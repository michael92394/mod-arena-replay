# Arena Replay Modernization 5.5.10

## Backend pivot labeling

- Backend `0` is now explicitly logged/documented as `creature_silhouette_debug`.
- Creature silhouettes remain useful for copied-map/object/camera/timing validation, rough actor positions, and held weapon checks.
- Creature silhouettes are not treated as the final appearance backend because Creature units cannot naturally render full player gear/customization or faithful player combat animation.

## Player-body planning diagnostics

- `PLAYER_BODY_PLAN` now logs the actual fields needed by a future real player-body backend:
  - race, class, gender
  - skin, face, hair style, hair color, facial hair
  - packed player bytes, player flags, shapeshift display/form
  - all captured visible equipment item entries
- Added future execution switches:
  - `ArenaReplay.ActorVisual.PlayerBody.UseCapturedEquipment = 1`
  - `ArenaReplay.ActorVisual.PlayerBody.UseCapturedCustomization = 1`
- Backend `1` still plans only and falls back to `creature_silhouette_debug` until a real playerbot/player-object acquisition path is integrated.
