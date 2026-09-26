/*
 * mod-forever-druid
 *
 * WoW Forever-style changes for druid bear tanks:
 *
 * - Swipe (Bear) costs no rage, at every rank.
 * - Bear Form and Dire Bear Form give 5 rage every time the druid dodges. This stacks with the
 *   Natural Reaction talent, which adds its own 1-3 rage per dodge.
 * - Frenzied Regeneration turns each point of rage into 1% of max health instead of 0.3%, as in
 *   WoW Forever, so a full 100 rage heals the druid to full over its 10 seconds.
 *
 * The rage cost and the Frenzied Regeneration rate are changed in the server's copy of the spell
 * data. The client only uses Frenzied Regeneration's rate for its tooltip, so that works without
 * a client patch. The client reads the cost from
 * its own Spell.dbc and won't let you press Swipe with less rage than that, so a free Swipe needs
 * the optional client patch (tools/patch-forever-druid-dbc.sh). The dodge rage needs no client
 * patch.
 *
 * The dodge rage works like Natural Reaction: a spell_proc row lets the Bear Form auras proc
 * when their owner dodges, and the aura script below turns that proc into rage.
 *
 * Released under the MIT License.
 */

#include "Config.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace
{
    // Every rank of Swipe (Bear), from rank 1 to rank 8. Must match tools/patch-forever-druid-dbc.sh.
    constexpr std::array<uint32, 8> SPELL_SWIPE_BEAR_RANKS = { 779, 780, 769, 9754, 9908, 26997, 48561, 48562 };

    // Must match tools/patch-forever-druid-dbc.sh.
    constexpr uint32 SPELL_FRENZIED_REGENERATION = 22842;

    struct Config
    {
        bool swipeEnabled = true;
        uint32 swipeRageCost = 0;
        bool dodgeRageEnabled = true;
        uint32 dodgeRage = 5;
        bool frenziedRegenerationEnabled = true;
        float frenziedRegenerationHealthPercent = 1.0f;
    };

    Config config;

    // The game data's rage cost for each Swipe rank (20 rage, stored as 200), read before the
    // module changes it, so Enable = 0 can put it back on a config reload.
    std::array<std::optional<uint32>, SPELL_SWIPE_BEAR_RANKS.size()> stockSwipeCost;

    // Set the server's rage cost for every Swipe (Bear) rank. The core then checks and takes the
    // new cost by itself; talents like Ferocity still take their share off, down to 0. Runs once
    // the spells are loaded, and again when the config is reloaded.
    void ApplySwipeRageCost()
    {
        for (size_t i = 0; i < SPELL_SWIPE_BEAR_RANKS.size(); ++i)
        {
            SpellInfo* spellInfo = const_cast<SpellInfo*>(sSpellMgr->GetSpellInfo(SPELL_SWIPE_BEAR_RANKS[i]));
            if (!spellInfo)
                continue;

            if (!stockSwipeCost[i])
                stockSwipeCost[i] = spellInfo->ManaCost;

            // Rage is stored ten times over: 20 rage is 200.
            spellInfo->ManaCost = config.swipeEnabled ? config.swipeRageCost * 10 : *stockSwipeCost[i];
        }
    }

    // The game data's base points for Frenzied Regeneration's rate (2, so the effect is worth 3),
    // read before the module changes it.
    std::optional<int32> stockFrenziedRegenerationPoints;

    // The core's Frenzied Regeneration script (spell_dru_frenzied_regeneration) heals the effect
    // 1 value, in tenths of a percent of max health, for each point of rage: 3 is 0.3%. Set it so
    // the core heals the configured share instead. The value is the base points plus 1 (the
    // effect's die has one side). Runs at the same times as ApplySwipeRageCost.
    void ApplyFrenziedRegenerationRate()
    {
        SpellInfo* spellInfo = const_cast<SpellInfo*>(sSpellMgr->GetSpellInfo(SPELL_FRENZIED_REGENERATION));
        if (!spellInfo)
            return;

        SpellEffectInfo& rate = spellInfo->Effects[EFFECT_1];
        if (!stockFrenziedRegenerationPoints)
            stockFrenziedRegenerationPoints = rate.BasePoints;

        if (config.frenziedRegenerationEnabled)
            rate.BasePoints = std::max(0, int32(std::lround(config.frenziedRegenerationHealthPercent * 10.0f))) - 1;
        else
            rate.BasePoints = *stockFrenziedRegenerationPoints;
    }

    void ApplySpellChanges()
    {
        ApplySwipeRageCost();
        ApplyFrenziedRegenerationRate();
    }
}

// 5487 - Bear Form
// 9634 - Dire Bear Form
class spell_dru_forever_bear_dodge_rage : public AuraScript
{
    PrepareAuraScript(spell_dru_forever_bear_dodge_rage);

    // spell_proc only lets the aura proc on a dodge. Only players get rage from it: NPC druids
    // in bear form use mana.
    bool CheckProc(ProcEventInfo& /*eventInfo*/)
    {
        return config.dodgeRageEnabled && config.dodgeRage && GetTarget()->IsPlayer();
    }

    // Like Natural Reaction's rage, it shows in the combat log ("You gain 5 Rage from Bear
    // Form.") and, as with any energize, adds a little threat.
    void HandleProc(AuraEffect const* /*aurEff*/, ProcEventInfo& /*eventInfo*/)
    {
        PreventDefaultAction();

        Unit* target = GetTarget();
        target->EnergizeBySpell(target, GetId(), config.dodgeRage * 10, POWER_RAGE);
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_dru_forever_bear_dodge_rage::CheckProc);
        OnEffectProc += AuraEffectProcFn(spell_dru_forever_bear_dodge_rage::HandleProc, EFFECT_0, SPELL_AURA_MOD_SHAPESHIFT);
    }
};

class ForeverDruidWorldScript : public WorldScript
{
public:
    ForeverDruidWorldScript() : WorldScript("ForeverDruidWorldScript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        config.swipeEnabled     = sConfigMgr->GetOption<bool>("ForeverDruid.Swipe.Enable", true);
        config.swipeRageCost    = sConfigMgr->GetOption<uint32>("ForeverDruid.Swipe.RageCost", 0);
        config.dodgeRageEnabled = sConfigMgr->GetOption<bool>("ForeverDruid.BearDodgeRage.Enable", true);
        config.dodgeRage        = sConfigMgr->GetOption<uint32>("ForeverDruid.BearDodgeRage.Amount", 5);

        config.frenziedRegenerationEnabled       = sConfigMgr->GetOption<bool>("ForeverDruid.FrenziedRegeneration.Enable", true);
        config.frenziedRegenerationHealthPercent = sConfigMgr->GetOption<float>("ForeverDruid.FrenziedRegeneration.HealthPercentPerRage", 1.0f);

        // At startup the spells aren't loaded yet; OnBeforeWorldInitialized does it then.
        ApplySpellChanges();
    }

    void OnBeforeWorldInitialized() override
    {
        ApplySpellChanges();
    }
};

void AddForeverDruidScripts()
{
    new ForeverDruidWorldScript();
    RegisterSpellScript(spell_dru_forever_bear_dodge_rage);
}
