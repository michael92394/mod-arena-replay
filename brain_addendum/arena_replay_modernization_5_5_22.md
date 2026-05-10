# Arena Replay Modernization 5.5.22

## Subsystem
mod-arena-replay — backend 2 actor visibility bridge, spectator shell cleanup, and buff crystal timing.

## Context
After the 5.5.20/5.5.21 line, replay exit/return behavior improved, but backend 2 still produced no visible replay actors. Logs continued to prove that actor snapshots were present and synthetic actor create packets were sent, yet the client rendered no bodies. The only visible unit-like artifact was the viewer's hidden replay shell or synthetic self actor as a floating ranged/thrown weapon. Arena buff crystals also spawned at replay start even though they should not visually appear until activation.

## Situation
The uploaded log for replay 116 showed:

- 6 inline appearance snapshots loaded successfully.
- 6 synthetic actors planned and 6 synthetic create packets sent.
- `CLONE_SCENE` still reported zero clone bindings.
- `SELF_POV` still had `cloneGuid=0`.
- Synthetic packet actor emission still used captured player display IDs and equipment-capable actor plans.
- Buff crystals were spawned during object initialization, long before their 90-second activation timer.

This means the actor failure is no longer a missing-config issue. Packet-only synthetic UNIT visuals are not yet a reliable visible actor backend for the 3.3.5 client. Backend 2 needs a visible server-owned clone bridge until true packet-correct player/object replay is finished.

## Changes

### Backend 2 server clone fallback is now real
Backend 2 now supports `ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.ServerCloneFallback.Enable = 1` as the default visible actor path. When enabled, synthetic planning still runs, but `BuildReplayActorCloneScene` continues into the normal server-owned clone prewarm path instead of returning after packet planning.

Expected proof logs:

- `[RTG][REPLAY][SYNTHETIC_CLONE_FALLBACK] ... enabled=1 ... result=server_clone_visibility_enabled`
- `[RTG][REPLAY][CLONE_PREWARM] ... result=ok`
- `[RTG][REPLAY][CLONE_SCENE] ... clonedActors=6 ... result=ok`
- `[RTG][REPLAY][SELF_POV] ... cloneGuid=<non-zero> ... result=ok`

### Packet-only UNIT visuals disabled by default while fallback is active
`EmitUnitVisualPackets` now defaults to disabled when clone fallback is enabled. This prevents the packet-only smoke-test path from creating invisible hitboxes or weapon-only ghost actors during normal replay tests.

New/updated defaults:

```ini
ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.EmitUnitVisualPackets = 0
ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.ServerCloneFallback.Enable = 1
ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.EmitEquipment = 0
ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.SkipViewerActorPackets = 1
ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.UsePlayerDisplayIds = 0
ArenaReplay.ActorVisual.SyntheticReplayPacketEmitter.UseShapeshiftDisplays = 0
ArenaReplay.CloneScene.Enable = 1
```

### Viewer/self packet ghost guard
Synthetic packet emission now skips the viewer's own actor by default. If packet diagnostics are re-enabled later, the viewer's camera shell should no longer produce the duplicate invisible self hitbox/ranged-weapon artifact.

### Buff crystals spawn only when activated
Buff crystal objects now support lazy spawn through:

```ini
ArenaReplay.ReplayObjects.Buff.SpawnOnlyOnActivation = 1
```

When enabled, buff crystals are not summoned at replay initialization. They are summoned and activated only when the replay timeline reaches `ArenaReplay.ReplayObjects.BuffDelaySeconds`.

## Regression Guard
Do not re-enable packet-only synthetic actor emission as a normal test default until logs prove that client-visible bodies render without floating weapons. Backend 2 is still the routing shell for Option C, but visible/cyclable actor playback should use server clones until opcode-correct player/object update construction is implemented.

## Next Investigation
If clones still fail to appear after this patch, stop investigating synthetic packet actor display and inspect the server clone template path directly:

1. Confirm clone template entry `98501` exists in world DB.
2. Confirm camera anchor entry `98502` is distinct from clone entry.
3. Check for `[RTG][REPLAY][CLONE_PREWARM_FAIL]` lines.
4. Confirm phase and map match copied replay map/phase.
5. Confirm `ArenaReplay.CloneScene.Enable = 1` is loaded in the actual runtime config, not only `.conf.dist`.

## Status
Ready for the next real replay test.
