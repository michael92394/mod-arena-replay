# Arena Replay Modernization 5.5.8

## Real appearance backend foundation

- Creature clone playback is now explicitly treated as `creature_silhouette` mode. It is the stable fallback for copied-instance replay bodies, but it cannot show full player armor/customization because a creature unit is not a player visual container.
- Added `ArenaReplay.ActorVisual.Backend`:
  - `0 = creature_silhouette`
  - `1 = playerbot_body_experimental`
  - `2 = synthetic_player_object_experimental`
- Backend 1 is planning-only in this pass. It logs `PLAYER_BODY_PLAN` for every playable actor and falls back to creature silhouettes. This keeps replay watchable while validating the full data needed by future playerbot replay bodies.
- Actor appearance snapshots now capture packed player customization bytes, player flags, shapeshift display/form diagnostics, and visible equipment item entries for head, shoulders, chest, waist, legs, feet, wrists, hands, back, tabard, mainhand, offhand, and ranged.
- Inline snapshot encoding was extended additively. Older inline snapshots still load.
- Actor snapshot SQL now has optional full-appearance columns. Runtime checks gate SELECT/INSERT shape so older schemas can still load until the SQL update has run.
- Camera smoothing defaults were tightened for vertical stability: softer Z lerp, larger Z deadband, larger Z snap distance, and slower/sparser anchor moves.

## Future backend work

The next real visual fidelity step is to acquire temporary replay-body player objects, most likely through mod-playerbots or reserved replay accounts. Those bodies should be dressed from the captured item entries, customized from captured player bytes, moved strictly by actor frames, kept out of BattlegroundMgr/group/queue state, and released on cleanup.
