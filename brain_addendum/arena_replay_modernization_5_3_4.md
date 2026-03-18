# ArenaReplay modernization 5.3.4 — spectator clone stabilization + teardown hardening

## Intake signal
Live replay logs showed two coupled defects:

1. **Participant self-watch was collapsing back into the viewer body** instead of a replay-surrogate scene.
2. **Replay exit was leaving the viewer in a levitating / no-jump state** even after packet playback completed.

Representative signals from the live realm:
- `[RTG][REPLAY][APPLY] ... actorGuid=<viewerGuid>` repeatedly pinning the POV to the participant actor.
- `[RTG][REPLAY][RETURN_STATE] ... canFly=1` after replay exit, proving teardown was not fully clearing spectator movement state.

## Root cause assessment
The old pipeline still behaved like a camera-follow system wrapped around the real viewer body rather than a true replay scene. That meant:
- self-watch had no real replacement body inside the replay,
- actor switching had weak visual grounding,
- the viewer body remained part of the movement contract,
- teardown could leave residual hover/fly state active after return.

## 5.3.4 action taken
This pass introduces the first real clone-scene bridge:

### 1. Surrogate actor clones in replay instances
For each playable actor track, the replay session now attempts to summon a surrogate actor clone inside the replay battleground instance and synchronize it against the interpolated actor frames.

This is **not yet a full appearance clone pipeline**. The current record format only stores:
- guid
- class
- race
- gender
- name
- movement frames

It does **not** yet persist a full equipment/display appearance snapshot. Because of that, this pass uses **surrogate replay clones** rather than claiming perfect visual fidelity.

### 2. Teardown hardening
Replay teardown now:
- resets actor replay state,
- despawns replay clone actors,
- restores spectator movement state before exit,
- performs the exit path,
- restores spectator movement state again after exit.

This is intended to directly attack the lingering levitate / no-jump failure mode.

## Strategic interpretation
This pass is the bridge from **camera-only replay** toward **scene-based replay**.

The next full milestone is a deeper clone system with:
- persisted appearance snapshots,
- clone ownership/lifecycle tables per replay session,
- explicit spectator-target binding to clone GUIDs rather than raw actor-track camera math,
- replay-safe target switching from the HUD itself.

## Honest limit
5.3.4 should improve self-watch and team completeness materially, but it does **not** yet claim:
- perfect equipment mirroring,
- spell-cast visual fidelity on clones,
- combat-state animation parity,
- complete decoupling from battleground-side spectator plumbing.
