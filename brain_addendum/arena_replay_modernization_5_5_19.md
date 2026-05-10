# Arena Replay Modernization 5.5.19 — Synthetic actor visibility + hard viewer restore

## Context
Follow-up replay testing showed two separate failures that should not be treated as one config issue. The log proves actor appearance capture and synthetic actor planning were both working, but the client still showed no actor bodies. After the replay, the viewer could also remain in the copied arena map and appear as weapon-only swords, meaning replay teardown still needed stronger viewer restoration and return logging.

## Findings
- Missing `ArenaReplay.ActorVisual.CreatureSilhouette.Display.*` warnings were mostly noise from dynamic per-race/per-gender/per-class config probing.
- The active replay loaded six actor snapshots, planned six synthetic replay visual GUIDs, and sent synthetic actor create packets, so this was past the capture/persistence stage.
- The previous synthetic UNIT create/movement block used the stationary position update block. That can produce server logs that look successful while still failing to render fake UNIT visuals client-side.
- Weapon-only viewer state after exit means the spectator shell display/equipment restore path must defend against a prior invisible display value, especially after any earlier failed replay left the character already hidden.
- Return-to-anchor previously issued a teleport without logging success/failure and without an immediate safe fallback when the saved anchor was missing or still pointed at a copied replay map.

## Changes
- Added `ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.MovementBlockMode`.
- Defaulted synthetic actor packets to a full 3.3.5 UNIT living `MovementInfo` block (`MovementBlockMode = 1`).
- Kept the old stationary position block available as `MovementBlockMode = 0` for emergency diagnostics only.
- Added a quiet creature-silhouette display resolver path. Per-race/per-gender/per-class display probing is now behind `ArenaReplay.ActorVisual.CreatureSilhouette.Display.UseSpecificOverrides = 0` by default.
- Added explicit `Team0` and `Team1` silhouette display defaults to reduce missing config noise.
- Hardened viewer display restore: if the saved prior display is missing or is a known invisible display, restore from the player's native display instead.
- Added return teleport logging and safe fallback handling when the saved anchor is missing, invalid, or still points at a replay map copy.

## Expected next test
- The copied arena should still load and attach normally.
- Synthetic actors should be more likely to appear because fake UNIT visuals now use the correct living movement block shape.
- Missing config warnings should shrink to only truly absent top-level config keys.
- Exiting a replay should restore the viewer body instead of leaving only weapons visible.
- If return-to-anchor fails, logs should now show `[RTG][REPLAY][RETURN_TELEPORT]` and optionally `[RTG][REPLAY][RETURN_TELEPORT_FALLBACK]` with the target map and result.

## Proof checklist
- Confirm logs include `MovementBlockMode = 1` in config and synthetic actor create/sync logs continue.
- Confirm actor bodies are visible or, if still invisible, capture whether `SYNTHETIC_ACTOR_CREATE` appears without client render.
- Confirm `[RTG][REPLAY][DISPLAY_RESTORE] ... result=ok` after exit.
- Confirm `[RTG][REPLAY][RETURN_TELEPORT] ... result=ok` and the player is no longer on map 725-729 after exit.
