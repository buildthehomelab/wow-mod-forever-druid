/*
 * mod-forever-druid
 *
 * WoW Forever-style changes for druid bear tanks:
 *
 * - Swipe (Bear) costs no rage, at every rank, and rank 1 is learned together with Bear Form.
 * - Bear Form and Dire Bear Form give 5 rage every time the druid dodges. This stacks with the
 *   Natural Reaction talent, which adds its own 1-3 rage per dodge.
 * - Frenzied Regeneration turns each point of rage into 1% of max health instead of 0.3%, as in
 *   WoW Forever, so a full 100 rage heals the druid to full over its 10 seconds.
 * - Pulverize, Cataclysm's bear finisher, learned at level 42: 15 rage, 60% weapon damage plus a
 *   bonus for each Lacerate stack on the target. It uses up the stacks and gives 2% melee crit
 *   per stack for 10 seconds. Lacerate moves from level 66 to 42 on the trainers (SQL), as in
 *   WoW Forever, so the two arrive together.
 *
 * The rage costs and the Frenzied Regeneration rate are changed in the server's copy of the spell
 * data. The client reads rage costs from its own Spell.dbc and won't let you press an ability with
 * less rage than that, so a free Swipe needs the optional client patch
 * (tools/patch-forever-druid-dbc.sh). Frenzied Regeneration and the dodge rage work without it.
 *
 * The dodge rage works like Natural Reaction: a spell_proc row lets the Bear Form auras proc
 * when their owner dodges, and an aura script turns that proc into rage.
 *
 * Pulverize reuses two spells the 3.3.5 client already has and nothing in the game uses:
 * "Test Maul" (24042), a leftover Blizzard test copy of Maul that is already a bear-only rage
 * attack, becomes the Pulverize button, and "Pulverize" (742), an unused NPC aura with
 * Pulverize's icon, becomes the crit buff. Without the client patch the button is called
 * "Test Maul" (with Maul's icon) and the crit buff doesn't show on the buff bar, but both work.
 *
 * Released under the MIT License.
 */

#include "Config.h"
#include "DBCStores.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
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

    constexpr uint32 SPELL_BEAR_FORM      = 5487;
    constexpr uint32 SPELL_DIRE_BEAR_FORM = 9634;
    constexpr uint32 SPELL_LACERATE_R1    = 33745;

    // Must match tools/patch-forever-druid-dbc.sh.
    constexpr uint32 SPELL_FRENZIED_REGENERATION = 22842;
    constexpr uint32 SPELL_PULVERIZE             = 24042; // "Test Maul" in the stock client
    constexpr uint32 SPELL_PULVERIZE_BUFF        = 742;   // "Pulverize", an unused NPC aura

    // Lacerate's global cooldown category and time, so Pulverize shares the global cooldown.
    constexpr uint32 GLOBAL_COOLDOWN_CATEGORY = 133;
    constexpr uint32 GLOBAL_COOLDOWN_MS       = 1500;

    // SpellDuration.dbc row for 10 seconds.
    constexpr uint32 DURATION_10_SECONDS = 1;

    struct Config
    {
        bool swipeEnabled = true;
        uint32 swipeRageCost = 0;
        bool swipeLearnWithBearForm = true;
        bool dodgeRageEnabled = true;
        uint32 dodgeRage = 5;
        bool frenziedRegenerationEnabled = true;
        float frenziedRegenerationHealthPercent = 1.0f;
        bool pulverizeEnabled = true;
        uint8 pulverizeLevel = 42;
        uint32 pulverizeRageCost = 15;
        uint32 pulverizeWeaponDamagePercent = 60;
        float pulverizeAttackPowerPerStack = 0.04f;
        uint32 pulverizeCritPerStack = 2;
    };

    Config config;

    // The game data's rage cost for each Swipe rank (20 rage, stored as 200), read before the
    // module changes it, so Enable = 0 can put it back on a config reload.
    std::array<std::optional<uint32>, SPELL_SWIPE_BEAR_RANKS.size()> stockSwipeCost;

    // Set the server's rage cost for every Swipe (Bear) rank. The core then checks and takes the
    // new cost by itself; talents like Ferocity still take their share off, down to 0.
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
    // effect's die has one side).
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

    // Turn "Test Maul" into Pulverize. It's already an instant, bear-only melee attack that can
    // be dodged and parried; it gets Pulverize's rage cost, the global cooldown (Test Maul was
    // never given one) and a percentage of weapon damage instead of weapon damage plus 210. The
    // Lacerate bonus and the crit buff are added by spell_dru_forever_pulverize.
    //
    // The buff is an NPC aura that made its owner's melee hits cast an ogre's Pulverize. It
    // becomes a visible 10 second buff that raises melee and ranged crit; the script sets how
    // much. Its old proc is switched off by spell_dru_forever_pulverize_buff.
    void ApplyPulverizeSpellData()
    {
        if (SpellInfo* spellInfo = const_cast<SpellInfo*>(sSpellMgr->GetSpellInfo(SPELL_PULVERIZE)))
        {
            spellInfo->ManaCost = config.pulverizeRageCost * 10;
            spellInfo->StartRecoveryCategory = GLOBAL_COOLDOWN_CATEGORY;
            spellInfo->StartRecoveryTime = GLOBAL_COOLDOWN_MS;

            SpellEffectInfo& weaponDamage = spellInfo->Effects[EFFECT_0];
            weaponDamage.Effect = SPELL_EFFECT_WEAPON_PERCENT_DAMAGE;
            weaponDamage.BasePoints = int32(config.pulverizeWeaponDamagePercent) - 1;
        }

        if (SpellInfo* spellInfo = const_cast<SpellInfo*>(sSpellMgr->GetSpellInfo(SPELL_PULVERIZE_BUFF)))
        {
            spellInfo->Attributes &= ~SPELL_ATTR0_PASSIVE;
            spellInfo->RecoveryTime = 0;
            spellInfo->ProcFlags = 0;
            spellInfo->ProcChance = 0;
            spellInfo->DurationEntry = sSpellDurationStore.LookupEntry(DURATION_10_SECONDS);

            SpellEffectInfo& crit = spellInfo->Effects[EFFECT_0];
            crit.ApplyAuraName = SPELL_AURA_MOD_WEAPON_CRIT_PERCENT;
            crit.TriggerSpell = 0;
            crit.BasePoints = 0;
        }
    }

    // Runs once the spells are loaded, and again when the config is reloaded.
    void ApplySpellChanges()
    {
        ApplySwipeRageCost();
        ApplyFrenziedRegenerationRate();
        ApplyPulverizeSpellData();
    }

    // Teach Swipe (Bear) rank 1 to a druid who knows Bear Form (or Dire Bear Form) but no rank of
    // Swipe yet. Stock druids get Bear Form at level 10 from their class quest but have to wait
    // for level 16 to train Swipe. Later ranks are still trained as usual. Nothing is taken away
    // when the setting is off: it's the normal rank 1 they would train anyway.
    void UpdateSwipe(Player* player)
    {
        if (!config.swipeLearnWithBearForm || player->getClass() != CLASS_DRUID)
            return;

        if (!player->HasSpell(SPELL_BEAR_FORM) && !player->HasSpell(SPELL_DIRE_BEAR_FORM))
            return;

        for (uint32 rank : SPELL_SWIPE_BEAR_RANKS)
            if (player->HasSpell(rank))
                return;

        player->learnSpell(SPELL_SWIPE_BEAR_RANKS[0]);
    }

    // Teach or remove Pulverize so it matches the druid's level and the Enable setting. Turning
    // the module off takes it away again at the next login.
    void UpdatePulverize(Player* player)
    {
        if (player->getClass() != CLASS_DRUID)
            return;

        bool const shouldKnow = config.pulverizeEnabled && player->GetLevel() >= config.pulverizeLevel;
        bool const knows = player->HasSpell(SPELL_PULVERIZE);

        if (shouldKnow && !knows)
            player->learnSpell(SPELL_PULVERIZE);
        else if (!shouldKnow && knows)
            player->removeSpell(SPELL_PULVERIZE, SPEC_MASK_ALL, false);
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

// 24042 - Pulverize ("Test Maul")
class spell_dru_forever_pulverize : public SpellScript
{
    PrepareSpellScript(spell_dru_forever_pulverize);

    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_PULVERIZE_BUFF, SPELL_LACERATE_R1 });
    }

    // The core calls OnHit for misses, dodges and parries too, so remember whether it landed.
    void RememberMiss(SpellMissInfo missInfo)
    {
        _landed = missInfo == SPELL_MISS_NONE;
    }

    // Runs before crits and armor are worked out, so the Lacerate bonus can crit and is reduced
    // by armor like the rest of the hit. A miss, dodge or parry leaves the stacks alone.
    void HandleHit()
    {
        if (!_landed)
            return;

        Unit* caster = GetCaster();
        Unit* target = GetHitUnit();
        if (!caster || !target)
            return;

        Aura* lacerate = target->GetAuraOfRankedSpell(SPELL_LACERATE_R1, caster->GetGUID());
        if (!lacerate)
            return;

        uint8 const stacks = lacerate->GetStackAmount();
        float const attackPower = caster->GetTotalAttackPowerValue(BASE_ATTACK);
        int32 const bonus = int32(attackPower * config.pulverizeAttackPowerPerStack * stacks);
        SetHitDamage(GetHitDamage() + bonus);

        lacerate->Remove();

        // A new Pulverize replaces the old buff, even with fewer stacks, as in Cataclysm.
        caster->RemoveAurasDueToSpell(SPELL_PULVERIZE_BUFF);
        if (!config.pulverizeCritPerStack)
            return;

        if (Aura* buff = caster->AddAura(SPELL_PULVERIZE_BUFF, caster))
            if (AuraEffect* crit = buff->GetEffect(EFFECT_0))
                crit->ChangeAmount(int32(config.pulverizeCritPerStack * stacks));
    }

    void Register() override
    {
        BeforeHit += BeforeSpellHitFn(spell_dru_forever_pulverize::RememberMiss);
        OnHit += SpellHitFn(spell_dru_forever_pulverize::HandleHit);
    }

    bool _landed = false;
};

// 742 - Pulverize (the crit buff)
class spell_dru_forever_pulverize_buff : public AuraScript
{
    PrepareAuraScript(spell_dru_forever_pulverize_buff);

    // The core made a proc entry for the NPC aura this used to be (35% on melee hits). The buff
    // only raises crit, so never let it proc.
    bool CheckProc(ProcEventInfo& /*eventInfo*/)
    {
        return false;
    }

    void Register() override
    {
        DoCheckProc += AuraCheckProcFn(spell_dru_forever_pulverize_buff::CheckProc);
    }
};

class ForeverDruidWorldScript : public WorldScript
{
public:
    ForeverDruidWorldScript() : WorldScript("ForeverDruidWorldScript") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        config.swipeEnabled           = sConfigMgr->GetOption<bool>("ForeverDruid.Swipe.Enable", true);
        config.swipeRageCost          = sConfigMgr->GetOption<uint32>("ForeverDruid.Swipe.RageCost", 0);
        config.swipeLearnWithBearForm = sConfigMgr->GetOption<bool>("ForeverDruid.Swipe.LearnWithBearForm", true);
        config.dodgeRageEnabled       = sConfigMgr->GetOption<bool>("ForeverDruid.BearDodgeRage.Enable", true);
        config.dodgeRage              = sConfigMgr->GetOption<uint32>("ForeverDruid.BearDodgeRage.Amount", 5);

        config.frenziedRegenerationEnabled       = sConfigMgr->GetOption<bool>("ForeverDruid.FrenziedRegeneration.Enable", true);
        config.frenziedRegenerationHealthPercent = sConfigMgr->GetOption<float>("ForeverDruid.FrenziedRegeneration.HealthPercentPerRage", 1.0f);

        config.pulverizeEnabled             = sConfigMgr->GetOption<bool>("ForeverDruid.Pulverize.Enable", true);
        config.pulverizeLevel               = uint8(sConfigMgr->GetOption<uint32>("ForeverDruid.Pulverize.Level", 42));
        config.pulverizeRageCost            = sConfigMgr->GetOption<uint32>("ForeverDruid.Pulverize.RageCost", 15);
        config.pulverizeWeaponDamagePercent = sConfigMgr->GetOption<uint32>("ForeverDruid.Pulverize.WeaponDamagePercent", 60);
        config.pulverizeAttackPowerPerStack = sConfigMgr->GetOption<float>("ForeverDruid.Pulverize.AttackPowerPerStack", 0.04f);
        config.pulverizeCritPerStack        = sConfigMgr->GetOption<uint32>("ForeverDruid.Pulverize.CritPerStack", 2);

        // At startup the spells aren't loaded yet; OnBeforeWorldInitialized does it then.
        ApplySpellChanges();
    }

    void OnBeforeWorldInitialized() override
    {
        ApplySpellChanges();
    }
};

class ForeverDruidPlayerScript : public PlayerScript
{
public:
    ForeverDruidPlayerScript() : PlayerScript("ForeverDruidPlayerScript") { }

    void OnPlayerLogin(Player* player) override
    {
        UpdateSwipe(player);
        UpdatePulverize(player);
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        UpdatePulverize(player);
    }

    void OnPlayerLearnSpell(Player* player, uint32 spellId) override
    {
        if (spellId == SPELL_BEAR_FORM || spellId == SPELL_DIRE_BEAR_FORM)
            UpdateSwipe(player);
    }
};

void AddForeverDruidScripts()
{
    new ForeverDruidWorldScript();
    new ForeverDruidPlayerScript();
    RegisterSpellScript(spell_dru_forever_bear_dodge_rage);
    RegisterSpellScript(spell_dru_forever_pulverize);
    RegisterSpellScript(spell_dru_forever_pulverize_buff);
}
