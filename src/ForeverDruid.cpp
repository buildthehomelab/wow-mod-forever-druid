/*
 * mod-forever-druid
 *
 * WoW Forever-style changes for druid bear tanks:
 *
 * - Swipe (Bear) costs no rage, at every rank.
 * - Bear Form and Dire Bear Form give 5 rage every time the druid dodges. This stacks with the
 *   Natural Reaction talent, which adds its own 1-3 rage per dodge.
 *
 * The rage cost is changed in the server's copy of the spell data. The client reads the cost from
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

#include <array>
#include <optional>

namespace
{
    // Every rank of Swipe (Bear), from rank 1 to rank 8. Must match tools/patch-forever-druid-dbc.sh.
    constexpr std::array<uint32, 8> SPELL_SWIPE_BEAR_RANKS = { 779, 780, 769, 9754, 9908, 26997, 48561, 48562 };

    struct Config
    {
        bool swipeEnabled = true;
        uint32 swipeRageCost = 0;
        bool dodgeRageEnabled = true;
        uint32 dodgeRage = 5;
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

        // At startup the spells aren't loaded yet; OnBeforeWorldInitialized does it then.
        ApplySwipeRageCost();
    }

    void OnBeforeWorldInitialized() override
    {
        ApplySwipeRageCost();
    }
};

void AddForeverDruidScripts()
{
    new ForeverDruidWorldScript();
    RegisterSpellScript(spell_dru_forever_bear_dodge_rage);
}
