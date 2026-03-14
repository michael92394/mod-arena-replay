# ArenaReplay modernization 5.2.7

- deduplicated replay watcher HUD traffic to reduce chat/addon spam
- capped replay packet playback per battleground update to reduce hitching and client overload on dense older replays
- tightened playable actor validation to require valid GUIDs and finite frame data
- re-enforced hidden viewer state during replay updates
- switched replay viewer battleground team bookkeeping to the real faction team
- made replay exit prefer battleground return flow instead of stacking an extra manual anchor teleport on top of battleground leave
