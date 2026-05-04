# Arena Replay Modernization 5.5.13

## Scope

Surgical recorded-packet visual repair for viewer self-GUID collision and packet-mode self POV behavior.

## Situation

The recorded-packet visual backend successfully showed real packet actors from both sides more accurately than creature clones, but when the viewer watched a replay containing their own character, packets referencing the viewer's original live player GUID could mutate the hidden spectator shell. In-game this manifested as floating weapon/sword artifacts, a hostile/targetable remnant in the middle of the arena, and dirty replay exit state.

Packet mode also continued to run self-POV clone-binding logic even though clone actors are intentionally disabled under recorded packet playback, causing repeated `SELF_POV ... no_clone_binding` logs and fixed-camera fallback spam.

## Changes

- Added packet-time viewer GUID remapping for recorded-packet playback.
- Added packed-GUID and raw-GUID byte replacement against outgoing replay packets before they are sent to the viewing client.
- Added a session-local fake replay visual GUID for the viewer's original replay actor so recorded updates do not target the live hidden viewer shell.
- Added a safety skip for packets that still contain the live viewer GUID after remapping.
- Added summary/diagnostic logs for remapped and skipped viewer-GUID packets.
- Updated packet-mode self POV handling so recorded-packet backend does not require creature clone bindings.
- Added config documentation for the new recorded-packet safety controls.

## New Config

```ini
ArenaReplay.ActorVisual.RecordedPacketStream.RemapViewerGuid = 1
ArenaReplay.ActorVisual.RecordedPacketStream.SkipUnmappedViewerGuidPackets = 1
```

## Expected Result

- Recorded packet stream remains the authoritative actor visual backend.
- Creature actor clones remain disabled for packet mode.
- The viewer's own live character should no longer appear as floating weapons or a targetable shell inside replay playback.
- Packet-mode self POV no longer spams missing clone binding logs.
- Replay teardown should be cleaner because recorded packets no longer directly mutate the hidden viewer object.

## Notes

This is a pragmatic session-local GUID isolation pass, not a full object-update parser. If future logs show `PACKET_VIEWER_GUID_SKIP` climbing heavily, the next step is a deeper opcode-aware GUID rewrite for object update blocks instead of byte-level packed/raw GUID replacement.
