# Arena Replay Modernization 5.5.18 — Synthetic UNIT visibility + Dalaran water safety

## Context
The first real Option C smoke test showed that actor tracks and POV switching worked, synthetic create logs were emitted for all actors, and replay teardown exited cleanly. However, no synthetic actor bodies were visible in-game, the Dalaran Sewers waterfall timeline visibly disagreed with the recorded match, and some weapon-only artifacts still appeared.

## Findings
- Synthetic actors were planned and create packets were sent for all actors.
- The synthetic update block still used a two-byte update flag for stationary position, which misaligns the following movement/update values in a 3.3.5 `SMSG_UPDATE_OBJECT` create block.
- The first test display resolver reused the large creature-silhouette fallback chain, causing repeated missing config warnings and making synthetic UNIT visibility harder to isolate.
- Dalaran Sewers water was generated from deterministic live-like timing, not recorded BattlegroundDS events. In synthetic backend this can disagree with the actual fight pathing.

## Changes
- Fixed synthetic create/movement position block alignment by writing the 3.3.5 update flags as one byte.
- Kept synthetic update fields aligned to AzerothCore 3.3.5 update field indexes.
- Added bounding radius, combat reach, and attack timers to synthetic UNIT create values.
- Added a stable synthetic fallback display path via `ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.FallbackDisplayId`.
- Added `UseCreatureSilhouetteFallbackResolver = 0` by default so Option C smoke tests avoid config spam and focus on packet visibility.
- Added `ArenaReplay.ReplayObjects.DalaranWater.EnableInSyntheticBackend = 0` by default. Dalaran water remains available outside synthetic backend, but Option C no longer fabricates water timing until real DS event capture exists.

## Expected next test
- Synthetic actors should become visible as stable UNIT fallback actors.
- Actor POV/path switching should continue working.
- Dalaran Sewers should no longer show inaccurate waterfall timing during Option C playback.
- Replay teardown should remain clean.

## Known limitation
Synthetic UNIT visuals are still not exact player armor/customization. This pass is strictly for making Option C actor bodies visible and safe before moving toward richer actor appearance packets.
