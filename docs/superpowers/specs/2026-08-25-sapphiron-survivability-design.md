# Sapphiron Survivability Design

## Goal

Make the existing Naxxramas Sapphiron strategy reliably survivable for playerbots in the WotLK encounter. The first slice prioritizes initial positioning, Blizzard/Chill movement, Icebolt/Frost Breath line-of-sight, and preservation of existing generic curse dispels.

This is a focused behavior fix, not a complete simulation of every Sapphiron spell or timer.

## Evidence

The current strategy registers only `sapphiron ground position` and `sapphiron flight position` in `src/Ai/Raid/Naxx/NaxxStrategy.cpp`. `SapphironBossHelper` tracks `IsFlying()`, a five-second post-landing window, Icebolt auras, and the bot's own Chill dynamic object.

The current ground action places non-tanks only inside `JustLanded()`, so the initial ground pull has no Sapphiron-specific raid placement. The current flight action moves toward the nearest Icebolt aura as soon as any such aura appears, but does not model assignment completion, validate the member/position, or guarantee a line-of-sight break during Frost Breath. No Sapphiron-specific action handles Life Drain, Tail Sweep, Frost Breath state, or enrage.

Generic `rear flank`, `avoid aoe`, and class curse-dispel strategies already exist. They must be reused or verified before adding duplicates. Generic area-debuff avoidance is reactive to an aura already applied to the bot, so it does not prove that the bot will leave a moving Blizzard early enough.

WotLK mechanics require side positioning for a dragon, avoidance of roaming Blizzard/Chill, spreading during Icebolt assignment, and hiding behind an Icebolt block before Frost Breath. Life Drain is a curse and should be removed promptly by Mage, Druid, or Shaman decursers.

References:

- https://www.icy-veins.com/wotlk-classic/sapphiron-encounter-guide-strategy-abilities-loot
- https://www.wowhead.com/wotlk/guide/raids/naxxramas/sapphiron-strategy

## Scope

### In scope

- Make initial ground positioning use the same safe role geometry as post-flight landing.
- Keep the tank and raid on a safe side/flank, avoiding front and rear dragon cones.
- Improve Chill movement so the action has priority over ordinary formation/combat movement and safely handles missing or stale aura data.
- Keep bots spread while Icebolt targets are being assigned.
- Move valid, non-dead bots behind a valid Icebolt block relative to Sapphiron's position for Frost Breath.
- Keep cover behavior active until the flight phase ends, then restore ground positioning.
- Validate generic curse dispels against Life Drain; make only a shared classification fix if the aura is not recognized as `DISPEL_CURSE`.
- Suppress ordinary movement that would make bots chase the airborne boss when the existing multiplier is insufficient.

### Out of scope

- Frost Resistance gear selection or potion automation.
- New raid-wide Frost Aura healing policy.
- Explicit enrage timers.
- A new Sapphiron target-selection system.
- Full spell-cast/event timing for every Icebolt and Frost Breath transition.
- Unrelated Naxx encounters or cleanup of unrelated working-tree files.

## Design

### Existing architecture

Continue using `SapphironBossHelper`, the two existing phase triggers, the two existing actions, and `SapphironGenericMultiplier`. Avoid introducing a second encounter state machine.

### Ground phase

`SapphironGroundPositionAction` will run the initial placement branch when the encounter is first detected as grounded, not only after `_was_flying` changes from true to false. The main tank will retain its known center-side destination when it has Sapphiron aggro. Other roles will use one side of the boss with role-appropriate distances and small group-slot offsets.

The helper will prefer valid map/ground coordinates and the bot's current Z or map-derived Z over an unconditional static Z where the movement API permits it. Missing boss or invalid movement data will return false without issuing a fabricated move.

Chill avoidance remains higher priority than routine positioning. The implementation will reuse generic avoidance where it is sufficient and retain only Sapphiron-specific geometry that generic avoidance cannot provide.

### Air phase

The helper will distinguish three practical states without adding full cast-timer modeling:

1. no valid Icebolt block is currently known: remain spread and do not stack;
2. a valid Icebolt block is known: move to the side opposite Sapphiron so the block breaks line of sight;
3. the boss has landed: stop cover movement and let ground positioning restore the formation.

The block search will ignore missing, dead, or out-of-world group members. The destination will be calculated from the boss-to-block vector, with a safe fallback if the destination cannot be reached. The action must not assume that being within a few yards of the block means the bot is covered.

### Generic behavior and multiplier

Before adding encounter-specific Life Drain code, confirm that existing Mage, Druid, and Shaman `DISPEL_CURSE` paths detect the Sapphiron aura. If not, fix the shared aura classification rather than creating a Sapphiron-only dispel action.

The Sapphiron multiplier will continue blocking movement that conflicts with the encounter. If runtime evidence shows ordinary chase/formation actions override the flight cover action, add the smallest phase-gated suppression for those actions while preserving emergency movement and cover movement.

## Safety and failure handling

- No raw `Player*` or `Unit*` is dereferenced without null/world/alive checks.
- No movement is sent to a stale group-member position or an invalid Z.
- If no Icebolt block is available, the bot does not blindly collapse onto another member.
- If the boss cannot be found or phase state is unavailable, the action returns false and existing AI behavior continues.
- The change will not alter generic dispel behavior unless the Life Drain classification check proves it is necessary.

## Verification

The parent AzerothCore build with this module enabled is the compile check.

The in-game scenario must cover:

1. 10-player initial pull: tank and non-tanks establish safe side positions before damage accumulates.
2. Ground phase: bots leave Chill/Blizzard without collapsing into the dragon or another group.
3. First air phase: bots remain spread during Icebolt assignment, then reach valid cover before Frost Breath and resume after landing.
4. Life Drain: Mage, Druid, and Shaman decursers remove the curse promptly; if not, record the aura classification failure.
5. 25-player repeat: confirm that the geometry does not depend on 10-player Icebolt count or radius.

Success means the raid survives the first complete ground→air→ground cycle without bots dying to front/rear positioning, Chill, or Frost Breath line-of-sight failure, and generic curse dispels remove Life Drain when an eligible decurser is present.
