/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "NaxxActions.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "SharedDefines.h"

bool MaexxnaChooseTargetAction::Execute(Event /*event*/)
{
    if (!helper.UpdateBossAI())
        return false;

    Unit* boss = helper.GetBoss();
    if (!boss)
        return false;

    GuidVector attackers = context->GetValue<GuidVector>("possible targets")->Get();
    Unit* target_wrap = nullptr;
    Unit* target_spiderling = nullptr;
    for (GuidVector::iterator i = attackers.begin(); i != attackers.end(); ++i)
    {
        Unit* unit = botAI->GetUnit(*i);
        if (!unit)
            continue;

        if (!unit->IsAlive())
            continue;

        if (unit->GetEntry() == helper.NPC_WEB_WRAP)
        {
            if (!target_wrap || bot->GetDistance2d(unit) < bot->GetDistance2d(target_wrap))
                target_wrap = unit;
        }
        else if (unit->GetEntry() == helper.NPC_MAEXXNA_SPIDERLING)
        {
            if (!target_spiderling || bot->GetDistance2d(unit) < bot->GetDistance2d(target_spiderling))
                target_spiderling = unit;
        }
    }

    Unit* target = nullptr;
    if (botAI->IsMainTank(bot))
        target = boss;
    else if (target_wrap)
        target = target_wrap;
    else if (target_spiderling)
        target = target_spiderling;
    else
        target = boss;

    if (!target || context->GetValue<Unit*>("current target")->Get() == target)
        return false;

    if (target == boss)
        return Attack(target, true);

    return Attack(target, false);
}
