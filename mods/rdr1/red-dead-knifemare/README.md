# Red Dead Knifemare

Red Dead Knifemare is an RDR1 PC ScriptHook mod that turns a normal Throwing Knife throw into a close-range contextual knife execution against the exact NPC John was aiming at.

The current implementation uses RDR1's own execution system. The mod does **not** choose a named finisher animation or run a roulette. It preserves John's approach side and heading, moves him close enough for a contextual execution, and lets the game select the linked execution that fits the current geometry/context.

## Current gameplay flow

1. Equip the Throwing Knife.
2. Aim at one supported living human NPC.
3. Keep TARGET / aim held and start a normal throw.
4. At throw start, the mod captures the exact NPC reported by RDR's targeting state.
5. The captured NPC is temporarily protected/stabilized while the handoff is prepared.
6. John's original throw action is cancelled for the execution handoff.
7. The mod preserves the existing target-to-John ground-plane direction and John's heading. If John is farther than **0.85 m**, only the distance is shortened to 0.85 m; John is not rotated around the target into a forced front or rear slot.
8. Player control is restored, the target is prepared for contextual execution, and the mod waits for FIRE / attack input to return to the released state.
9. With TARGET still held, the mod sends one fresh contextual FIRE pulse.
10. RDR1 decides which native linked execution starts from the resulting position, facing, weapon and target context.
11. Success is accepted only when the exact captured NPC and John enter the reciprocal linked execution.
12. Temporary protection/execution state is restored and normal gameplay resumes. If the linked execution finishes or times out while the exact target is still alive, the mod applies the existing exact-target lethal cleanup.

There is no nearest-NPC replacement, camera-direction guess, projectile-hit victim substitution, or second automatic FIRE retry.

## Assassin teleport transition

Issue #137 currently keeps only the reliable **arrival screen shock** around the automatic distance-closing teleport. The smoke/particle experiment was removed before merge after repeated in-game tests produced valid effect handles but no visible smoke.

- Only a meaningful teleport of at least **0.15 m** receives the shock.
- The existing teleport still happens in the same frame with no added wait.
- At arrival, the mod uses `SET_SHOCK_AMPLITUDE(1.0)`, `SET_SHOCK_SPEED(9.0)` and `FIRE_SHOCK(1.0)`.
- There is no smoke/particle emitter state in the release build.
- The visual layer does not take camera ownership, change time scale, add another teleport, or change contextual FIRE timing.
- Smoke remains deferred for later investigation under issue #137.

## Contextual execution selection

Issue #94 runtime testing confirmed the same automatic execution path can enter linked executions from **front, rear and side approaches**.

That means the mod currently treats finisher selection as a native RDR1 behavior rather than a mod-controlled roulette:

> preserve approach geometry -> enter valid close-range execution context -> let RDR1 select the execution

Relative position/facing are important inputs, but they are not assumed to be the only inputs. Target posture/state and weapon context can also affect what execution is available.

Future variants such as kneeling, Hunting Knife, hogtied or sleeping executions should therefore be investigated as additional **contexts**, not as hard-coded roulette entries unless later game-data research proves that explicit animation selection is needed.

## Exact target ownership

The automatic flow uses RDR1's targeting state and never chooses the nearest NPC.

At throw start it resolves the exact aimed target using:

- `GET_ACTOR_UNDER_RETICLE`
- `GET_TARGET_ACTOR`
- a single unambiguous `IS_PLAYER_TARGETTING_ACTOR` match when needed

If targeting information conflicts or becomes ambiguous, the automatic request is rejected rather than substituting another NPC.

The physical knife projectile does not select or replace the execution target.

## Supported targets and safe rejection

The normal automatic path is intended for ordinary living human NPCs who are on foot and available for a close-range execution handoff.

The flow rejects or aborts when the context becomes unsafe or unsupported, including cases such as:

- John/player context changing or dying;
- pause/cutscene interruption;
- the captured NPC becoming invalid or dying before the handoff;
- switching away from the Throwing Knife;
- riders or vehicle occupants;
- animals/players/non-supported actors;
- an existing linked action;
- invalid transforms or failed placement;
- TARGET being released before the contextual FIRE;
- live targeting resolving a conflicting/ambiguous NPC;
- RDR refusing to start a reciprocal linked execution.

Cleanup restores temporary proof, execution eligibility, one-shot-death state and temporary relationship changes when the target survives an aborted attempt.

## Controls

### Normal release controls

- Normal Throwing Knife aim/throw: triggers Red Dead Knifemare.
- The public release exposes no F6/F7/F8/F9 developer or inspection hotkeys.

### Developer execution controls

The public release does not register the ScriptHook keyboard-message handler, so the old function-key test controls cannot fire.

The source still contains the F6/F7/F8 execution regression harness and F9 outfit-inspection path for dedicated development builds. Re-enabling those controls requires intentionally enabling the release hotkey gate in `src/main.cpp`; F6/F7/F8 additionally remain protected by their execution-debug flag.

The Assassin outfit waits up to 750 ms for the asynchronous Deadly Assassin variation switch to actually become current before equipping `ACCESSORY_BANDANA` (slot 1) and directly enabling player mesh 26. This prevents the startup race where the bandana previously appeared only after pressing F9.

## Runtime logging

Knifemare tracing is **disabled by default** for normal gameplay, so the release build does not continuously create or flush `RedDeadKnifemare.trace.log`.

When diagnostics are needed, set:

```cpp
constexpr bool TRACE_ENABLED = true;
```

in `src/trace.cpp`, rebuild and reproduce the problem. The debug build will write `RedDeadKnifemare.trace.log` beside the ASI, or in `%TEMP%` if that location cannot be opened.

## Assassin-inspired outfit

The optional visual add-on currently uses Deadly Assassin variation **18**.

Default release preset:

- bandana mesh 26 enabled;
- glove meshes 27/28 enabled;
- no-glove meshes 29/30 disabled;
- long-arm holster mesh 25 left unchanged.

The public release keeps this preset fixed; the former F9 inspection toggle is disabled.

The ASI does not edit `fragments.rpf` or the physical throwing-knife model. The deploy script manages a separate `update\game\mapres.rpf` copy for the Assassin Knife UI artwork; Rockstar's original `game\mapres.rpf` remains untouched.

## Assassin Knife UI

The existing RDR1 throwing-knife weapon enum remains unchanged, but Knifemare presents that slot as **Assassin Knife**.

- The display and weapon-wheel labels use the same paired localization-buffer override pattern proven by Red Dead Possession. `Assassin Knife` is the same length as `Throwing Knife`, so the existing English buffers can be reused without resizing them.
- The repository stores two prepared red, stock-identity replacements directly under `assets\streaming\assassin_knife_ui_wtds\`: `radial_thrn_knife.wtd` for the weapon wheel and `weapons_thrn_knife.wtd` for **Weapons -> Organize and Compare**.
- Those replacements preserve the stock WTD filenames, internal resource identities, dimensions, mipmaps and texture formats. Only the Throwing Knife artwork color is changed to red.
- Release packaging reads those two checked-in WTDs directly and passes only them to MagicRDR. It does not use a nested asset ZIP and does not build or recolor WTDs on the player's machine.
- MagicRDR replaces only those two Throwing Knife entries in the managed `update\game\mapres.rpf` copy. Rockstar's original `game\mapres.rpf` remains a read-only source.
- Knifemare uses the same Ultimate ASI Loader update-folder path and ownership/backup safety model as Red Dead Possession.
- Deploy and undeploy restore any pre-existing update archive / `wininet.dll` / `wininet.ini` only when the saved ownership hashes still match.
- This UI change does not alter weapon enum 25, knife ammo, projectile behavior, targeting, teleport/execution logic, the physical knife model, or unrelated weapon artwork.

### UI test

1. Close RDR completely and run `.\mods\rdr1\red-dead-knifemare\deploy.ps1`.
2. Equip the Throwing Knife and open the weapon wheel. Confirm the slot reads **Assassin Knife** and the original Throwing Knife silhouette is red.
3. Keep the knife equipped, open **Weapons -> Organize and Compare**, and navigate to the knife. Confirm the panel reads **Assassin Knife** and the original knife artwork is red.
4. Check several other weapon slots to make sure their labels and artwork remain stock.
5. Throw Assassin Knife at a supported on-foot NPC and confirm the existing teleport/execution flow still works normally.

## Build and developer deployment

From PowerShell at the repository root, use the same mod-local layout as Bear of Redemption and Red Dead Possession:

```powershell
.\mods\rdr1\red-dead-knifemare\deploy.ps1
```

Use `-NoLaunch` to deploy without starting the game.

The deploy script performs a clean x64 Release build, verifies the deployed ASI hash and writes build/deployment output under:

```text
mods\rdr1\red-dead-knifemare\build\
  cmake\
  logs\
```

That local `build/` directory is gitignored.

To remove the developer build:

```powershell
.\mods\rdr1\red-dead-knifemare\undeploy.ps1
```

The older root-level `deploy-red-dead-knifemare.ps1` and `undeploy-red-dead-knifemare.ps1` commands remain as compatibility wrappers and forward to the mod-local scripts.

The undeploy script removes only Red Dead Knifemare managed files and leaves ScriptHookRDR, the ASI loader and other mods untouched.

## Current play-test focus

For the release-hardening pass, test normal gameplay rather than the disabled function-key harness:

1. Equip the Throwing Knife.
2. Aim at an ordinary standing NPC.
3. Keep TARGET held, throw once, then release FIRE/RT.
4. Confirm John moves only closer along the existing approach direction.
5. Confirm a native contextual execution starts against the exact aimed NPC.
6. Repeat from front, rear and side approaches.
7. Repeat during ordinary combat and with different civilian/lawman targets.
8. Try invalid contexts such as mounted NPCs, tight geometry, slopes, interruptions and rapid repeated throws; unsupported cases should fail without leaving John stuck.

## Known limitations / remaining #96 work

The core loop is playable, but release hardening still needs broader runtime coverage across combat, interiors, slopes/uneven terrain, obstacles/tight spaces, scripted encounters, mission contexts, interruptions and repeated use.

Mounted/vehicle targets are currently unsupported and should be rejected safely.

Steep or awkward terrain can still make contextual execution alignment less reliable. Release readiness depends on safe recovery in those situations rather than forcing every possible context to work.

A standalone end-user ZIP/package is also still part of #96; the current PowerShell deployment workflow is for repository/development use.
