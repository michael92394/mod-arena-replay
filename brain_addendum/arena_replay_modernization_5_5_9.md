# Arena Replay Modernization 5.5.9

## Creature silhouette display hardening

- Creature silhouette backend now keeps captured player display ids in snapshots for future player-body backends, but it no longer applies raw player display ids to Creature bodies by default.
- Added:
  - `ArenaReplay.ActorVisual.CreatureSilhouette.UsePlayerDisplayIds = 0`
  - `ArenaReplay.ActorVisual.CreatureSilhouette.UseNpcRaceFallbackDisplays = 1`
  - `ArenaReplay.ActorVisual.CreatureSilhouette.Display.Default = 27800`
- Display resolution supports future tuning by race, gender, class, and team. Unconfigured actors fall back to the module's textured replay actor creature display.
- New `CREATURE_SILHOUETTE_DISPLAY` logs show the captured player display id and the fallback creature display selected for replay.
- Weapon item-entry application is unchanged and remains the correct short-term way to show held weapons in creature mode.

## Camera smoothing

- Camera smoothing defaults were tightened again to reduce vertical bobbing:
  - XY lerp 0.30
  - Z lerp 0.05
  - Z deadband 1.00
  - Z snap distance 8.0
  - minimum anchor XY move 0.35
  - minimum anchor Z move 0.75
- The camera still prefers the visible clone position when available and only falls back to raw actor frame position when the clone is missing.

## Backend direction

The white/blue body issue is now treated as a visual backend limitation, not a snapshot bug. Creature silhouettes should look stable and non-broken, while the experimental player-body backend remains the path toward real player armor/customization fidelity.
