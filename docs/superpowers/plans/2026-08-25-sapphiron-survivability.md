# Sapphiron Survivability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the existing Naxxramas Sapphiron strategy survive the initial ground phase, Chill/Blizzard movement, Icebolt/Frost Breath air phase, and landing transition.

**Architecture:** Keep the existing `SapphironBossHelper` → phase triggers → movement actions → multiplier flow. Add only the small phase/target state needed to distinguish initial ground positioning, incomplete Icebolt assignment, valid Icebolt cover, and post-landing recovery. Reuse generic `rear flank`, `avoid aoe`, and class curse-dispel behavior rather than adding parallel systems.

**Tech Stack:** C++17-compatible AzerothCore module code, existing PlayerbotAI movement/actions, AzerothCore raid difficulty and aura APIs, parent AzerothCore CMake build.

## Global Constraints

- Preserve unrelated working-tree files; do not modify `AGENTS.md` or `data/sql/playerbots/custom/playerbot_conversation_variations.sql`.
- Continue using `SapphironBossHelper`, the two existing phase triggers, the two existing actions, and `SapphironGenericMultiplier`.
- Do not add Frost Resistance gear selection, Frost Aura healing policy, explicit enrage timers, a new target-selection system, or a second encounter state machine.
- Reuse generic movement and dispel behavior before adding encounter-specific equivalents.
- Every member/unit lookup must validate null, world state, and alive state before dereference.
- If boss, target, map, or movement data is unavailable, return `false` without issuing fabricated coordinates.
- Compile the parent AzerothCore with the module after source changes; there is no module-local test framework.

---

### Task 1: Detect Initial Ground Phase

**Files:**
- Modify: `src/Ai/Raid/Naxx/NaxxBossHelper.h:158-177`
- Test: Parent AzerothCore build

**Interfaces:**
- Produces: `SapphironBossHelper::JustLanded()` returns true for the first grounded observation of a valid Sapphiron target and for the existing five-second flight-to-ground transition.
- Preserves: `UpdateBossAI()`, `IsPhaseGround()`, and `IsPhaseFlight()` callers in the existing triggers/actions/multiplier.

- [ ] **Step 1: Record the current helper behavior**

Confirm that `UpdateBossAI()` only sets `_last_land_ms` when `_was_flying` changes from true to false. The regression is that `_was_flying` starts false and `_last_land_ms` starts zero, so the first grounded pull never enters the placement branch.

- [ ] **Step 2: Add first-ground observation handling**

Inside the existing `_unit` acquisition block, after a valid target is found, seed the same landing window when the target is already grounded:

```cpp
if (!_unit)
{
    _unit = AI_VALUE2(Unit*, "find target", "sapphiron");
    if (!_unit)
        return false;

    if (!_unit->IsFlying())
        _last_land_ms = getMSTime();
}
```

Leave the existing transition logic unchanged so later landings still update `_last_land_ms`:

```cpp
bool now_flying = _unit->IsFlying();
if (_was_flying && !now_flying)
    _last_land_ms = getMSTime();

_was_flying = now_flying;
return true;
```

- [ ] **Step 3: Build the parent core**

Run from the parent AzerothCore checkout:

```bash
cmake --build build --config Release
```

Expected: the parent build completes successfully with `mod-playerbots` enabled.

- [ ] **Step 4: Commit the isolated helper change**

```bash
git add src/Ai/Raid/Naxx/NaxxBossHelper.h
git commit -m "fix(CORE/Raid): position Sapphiron bots on initial ground phase"
```

---

### Task 2: Prioritize Safe Ground Positioning

**Files:**
- Modify: `src/Ai/Raid/Naxx/Action/NaxxActions_Sapphiron.cpp:13-54`
- Inspect only: `src/Ai/Base/Actions/MovementActions.cpp:1851-1933`, `src/Ai/Base/Actions/MovementActions.cpp:2493-2533`
- Test: Parent AzerothCore build and 10-player initial-pull scenario

**Interfaces:**
- Consumes: `SapphironBossHelper::JustLanded()` from Task 1 and existing `FindPosToAvoidChill()`.
- Produces: ground positioning that runs on initial pull and landing, keeps the current tank/role geometry, and gives non-tank Chill escape precedence over routine placement.

- [ ] **Step 1: Confirm generic behavior before duplicating it**

Read the existing generic `rear flank` and `avoid aoe` implementations. Keep those strategies enabled for dragon cone and area-debuff behavior. The Sapphiron action should only handle its existing encounter-specific placement/Chill destination.

- [ ] **Step 2: Make Chill escape win over non-tank placement**

In `SapphironGroundPositionAction::Execute`, after `UpdateBossAI()` and before the `JustLanded()` placement branch for non-tanks, retain the existing destination calculation but execute it before ordinary role placement:

```cpp
if (!botAI->IsMainTank(bot))
{
    std::vector<float> dest;
    if (helper.FindPosToAvoidChill(dest))
        return MoveTo(NAXX_MAP_ID, dest[0], dest[1], dest[2], false, false, false, false,
                      MovementPriority::MOVEMENT_COMBAT);
}
```

Then retain the current main-tank aggro placement and current post-landing role geometry. Do not change the existing coordinates in this task; validate them in-game first.

- [ ] **Step 3: Keep the action safe on missing movement data**

Before each Sapphiron-specific `MoveTo`, require a complete destination vector. If `FindPosToAvoidChill` returns false or a role calculation cannot produce a valid destination, return false and let generic AI movement continue.

- [ ] **Step 4: Build and run the initial ground probe**

Run:

```bash
cmake --build build --config Release
```

Then pull Sapphiron with a 10-player bot group and verify that non-tanks begin their side positions during the first grounded phase, rather than waiting for the first landing. During a Chill/Blizzard pass, verify that non-tanks leave the effect without moving behind the dragon.

Expected: no bot starts directly in front of or behind Sapphiron, and the initial ground placement occurs before the first air phase.

- [ ] **Step 5: Commit the ground-position change**

```bash
git add src/Ai/Raid/Naxx/Action/NaxxActions_Sapphiron.cpp
git commit -m "fix(CORE/Raid): prioritize Sapphiron ground positioning"
```

---

### Task 3: Gate Air Cover on Complete Icebolt Assignment

**Files:**
- Modify: `src/Ai/Raid/Naxx/NaxxBossHelper.h:188-207`
- Modify: `src/Ai/Raid/Naxx/Action/NaxxActions_Sapphiron.cpp:56-107`
- Test: Parent AzerothCore build and first air phase

**Interfaces:**
- Produces: a helper predicate that reports Icebolt cover readiness only after the expected WotLK target count is present: 2 targets for `RAID_DIFFICULTY_10MAN_NORMAL`, 3 targets for `RAID_DIFFICULTY_25MAN_NORMAL`.
- Produces: `MoveToNearestIcebolt()` that ignores invalid members, uses the block’s actual position, and places the bot on the boss-opposite side of the block.

- [ ] **Step 1: Replace the ambiguous helper name with an explicit readiness contract**

Rename `WaitForExplosion()` to `IceboltsReady()` and keep its existing group scan as the basis. Count only members satisfying all of these conditions:

```cpp
member && member->IsInWorld() && member->IsAlive() &&
(NaxxSpellIds::HasAnyAura(member, {NaxxSpellIds::Icebolt10, NaxxSpellIds::Icebolt25}) ||
botAI->HasAura("icebolt", member, false, false, -1, true))
```

Use the existing repository difficulty pattern:

```cpp
uint32 requiredTargets = bot->GetRaidDifficulty() == RAID_DIFFICULTY_25MAN_NORMAL ? 3 : 2;
return targetCount >= requiredTargets;
```

Do not add a cast timer or event map. Until the expected count exists, the flight action must not collapse bots onto the first block.

- [ ] **Step 2: Update the flight action to use the readiness predicate**

Replace the current flight branch with a readiness-gated cover attempt followed by the existing Chill fallback:

```cpp
if (helper.IceboltsReady() && MoveToNearestIcebolt())
    return true;

std::vector<float> dest;
if (helper.FindPosToAvoidChill(dest))
    return MoveTo(NAXX_MAP_ID, dest[0], dest[1], dest[2], false, false, false, false,
                  MovementPriority::MOVEMENT_COMBAT);

return false;
```

When the expected Icebolt count is not present, the action makes no cover move, so bots remain spread. When cover movement fails or is not yet available, the existing Chill fallback remains available.

- [ ] **Step 3: Harden block selection and destination geometry**

In `MoveToNearestIcebolt()`:

1. Skip null, dead, out-of-world, and self members.
2. Initialize `minDistance` to `std::numeric_limits<float>::max()`.
3. Require a valid Sapphiron target before calculating the vector.
4. Use `playerWithIcebolt->GetPositionZ()` for the block destination instead of `helper.GENERIC_HEIGHT`.
5. Keep the boss-to-block angle and place the bot three yards beyond the block, away from Sapphiron.
6. If the direct `MoveTo` fails, retain `MoveNear(playerWithIcebolt, 3.0f, MovementPriority::MOVEMENT_COMBAT)` as the fallback.

The resulting destination calculation remains:

```cpp
float angle = boss->GetAngle(playerWithIcebolt);
float posX = playerWithIcebolt->GetPositionX() + cos(angle) * 3.0f;
float posY = playerWithIcebolt->GetPositionY() + sin(angle) * 3.0f;
float posZ = playerWithIcebolt->GetPositionZ();
```

- [ ] **Step 4: Build and run the air-phase probe**

Run:

```bash
cmake --build build --config Release
```

In a 10-player encounter, verify that bots remain spread while one Icebolt target exists, move behind a block after the second target is present, and remain there until Sapphiron lands. Repeat in 25-player mode and verify that three targets are required.

Expected: no premature stack on the first Icebolt target, and no bot blindly moves to a missing/dead member.

- [ ] **Step 5: Commit the air-phase change**

```bash
git add src/Ai/Raid/Naxx/NaxxBossHelper.h src/Ai/Raid/Naxx/Action/NaxxActions_Sapphiron.cpp
git commit -m "fix(CORE/Raid): gate Sapphiron air cover on Icebolts"
```

---

### Task 4: Suppress Airborne Boss Chasing

**Files:**
- Modify: `src/Ai/Raid/Naxx/NaxxMultipliers.cpp:181-190`
- Test: Parent AzerothCore build and air-phase movement probe

**Interfaces:**
- Consumes: `SapphironBossHelper::IsPhaseFlight()` and existing action class types from `ChooseTargetActions.h` and `ReachTargetActions.h`.
- Produces: ordinary DPS/tank assist and reach-target actions are suppressed while Sapphiron is airborne, while Sapphiron flight positioning and emergency movement remain eligible.

- [ ] **Step 1: Add phase-gated suppression after the existing formation suppression**

Extend `SapphironGenericMultiplier::GetValue` with this exact phase gate:

```cpp
if (helper.IsPhaseFlight() &&
    (dynamic_cast<DpsAssistAction*>(action) || dynamic_cast<TankAssistAction*>(action) ||
     dynamic_cast<ReachTargetAction*>(action) || dynamic_cast<CastReachTargetSpellAction*>(action)))
{
    return 0.0f;
}
```

Do not suppress `SapphironFlightPositionAction`, `AvoidAoeAction`, `CurePartyMemberAction`, or other emergency/cover movement. Keep the existing Death Grip and formation suppression unchanged.

- [ ] **Step 2: Build and inspect action behavior**

Run:

```bash
cmake --build build --config Release
```

During flight, inspect bot movement and combat logs. Expected: bots do not chase the airborne Sapphiron, but still execute cover movement and emergency area avoidance.

- [ ] **Step 3: Commit the multiplier change**

```bash
git add src/Ai/Raid/Naxx/NaxxMultipliers.cpp
git commit -m "fix(CORE/Raid): stop Sapphiron airborne boss chase"
```

---

### Task 5: Verify Generic Life Drain Dispels and Full Encounter Cycle

**Files:**
- Inspect: `src/Ai/Class/Mage/Strategy/GenericMageStrategy.cpp`
- Inspect: `src/Ai/Class/Druid/Strategy/GenericDruidStrategy.cpp`
- Inspect: `src/Ai/Class/Shaman/Strategy/GenericShamanStrategy.cpp`
- Inspect: `src/Ai/Base/Value/PartyMemberToDispel.cpp`
- Modify only if verification proves shared aura classification failure: the shared spell/aura classification source identified by the failed probe
- Test: 10-player and 25-player in-game scenarios

**Interfaces:**
- Consumes: existing `DISPEL_CURSE` detection and class strategy triggers.
- Produces: evidence that eligible Mage, Druid, and Shaman bots remove Life Drain without Sapphiron-only dispel code.

- [ ] **Step 1: Verify the existing generic curse path**

Confirm these existing strategies route party curse detection to their class actions:

- Mage: `party member remove curse` → `remove curse on party`.
- Druid: `party member remove curse` → `remove curse on party`.
- Shaman: `party member cleanse spirit curse` → `cleanse spirit curse on party`.

Confirm `PartyMemberToDispel` delegates to `botAI->HasAuraToDispel(unit, DISPEL_CURSE)`.

- [ ] **Step 2: Run the Life Drain probe**

Use a controlled Sapphiron pull with one eligible decurser class present. Record whether Life Drain is removed promptly from a group member and whether the bot logs show the curse dispel action.

Expected: the generic path removes Life Drain. No Naxx strategy change is made when this succeeds.

- [ ] **Step 3: Fix only shared classification if the probe fails**

If `HasAuraToDispel(..., DISPEL_CURSE)` does not recognize Life Drain, add the spell/aura ID at the shared classification source used by `HasAuraToDispel`, preserving the existing `DISPEL_CURSE` contract. Do not add a Sapphiron-specific trigger or action.

Re-run the same probe and confirm the action fires before the next Life Drain tick.

- [ ] **Step 4: Run the full acceptance cycle**

Run the parent build once more:

```bash
cmake --build build --config Release
```

Then execute:

1. 10-player initial pull and first grounded phase.
2. Chill/Blizzard avoidance without front/tail collapse.
3. First flight: spread, complete Icebolt assignment, reach cover, survive Frost Breath, recover after landing.
4. Life Drain with Mage, Druid, and Shaman decursers.
5. 25-player repeat with three Icebolt targets.

Success means the group survives the first ground→air→ground cycle without a bot dying to front/rear positioning, Chill, or Frost Breath line-of-sight failure, and generic curse dispels remove Life Drain when an eligible decurser is present.

- [ ] **Step 5: Commit any shared classification fix, then record verification**

If a shared classification fix was required:

```bash
git add <shared-classification-file>
git commit -m "fix(CORE/Raid): classify Sapphiron Life Drain as curse"
```

If no classification fix was required, leave source unchanged and record the successful generic-path probe in the final report.

---

## Plan Self-Review

- **Spec coverage:** Initial ground placement is Task 1; safe ground/Chill movement is Task 2; Icebolt assignment and cover are Task 3; airborne chase suppression is Task 4; generic Life Drain verification and 10/25 acceptance are Task 5. Out-of-scope Frost Resistance, Frost Aura healing, enrage, target selection, and event-map modeling are not introduced.
- **Placeholder scan:** No `TODO`, `TBD`, or unspecified test command is used. The only conditional source change is the explicitly test-gated shared Life Drain classification fix.
- **Type consistency:** `IceboltsReady()` is introduced and consumed only by `SapphironFlightPositionAction`; `IsPhaseFlight()` already exists and is consumed by `SapphironGenericMultiplier`; existing action class names are declared in the included headers.
- **Working-tree safety:** The plan stages only intended source files and never stages the two unrelated untracked files.
