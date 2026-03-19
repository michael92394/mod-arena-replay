# ArenaReplay modernization 5.4.4 — spectator shell isolation contract for playerbot queue accounting

## Problem observed
Arena replay intentionally places the viewer inside a battleground/arena spectator shell so the client can observe replay playback. That shell is correct for replay ownership, but it can be misread by other systems as live battleground or arena participation.

In queue testing this presented as a cross-module blast radius risk:
- replay viewers could look like live arena participants to queue-demand systems
- playerbot helper retirement could remain noisy after arena testing
- second-arena demand could be distorted by replay-owned battleground state

## Contract reinforcement
This pass does not rewrite replay shell behavior itself. Instead it formalizes the integration contract:
- replay viewers remain spectator-shell battleground residents
- downstream queue systems must treat `IsSpectator()` players as replay-owned and exclude them from live arena demand/accounting

## Downstream effect
`mod-playerbots` now filters spectator-state players from its real-player battleground/arena accounting pass. That keeps replay state visible to the replay module while preventing replay viewers from contaminating helper-demand math.

## Why this matters
This preserves the existing replay spectator model while reducing cross-module ownership leakage. Replay stays spectator-only; queue orchestration stops mistaking that spectator shell for live queue demand.
