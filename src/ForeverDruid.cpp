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
 *   WoW Forever, so the two arrive together, and its rank 1 damage scales with level from 42 to
 *   66 so it isn't too strong early.
 *
 * - With mod-mount-scaling installed, Travel Form, Flight Form and Swift Flight Form follow its
 *   level-scaled mount speeds, like a mount would. Travel Form only out of combat: in combat it's
 *   the stock 40%, since a mount can't be used in combat at all.
 *
 * - Consumables work in Cat Form, Bear Form and Dire Bear Form. Food, potions, flasks, elixirs
 *   and bandages already do in stock 3.3.5; this adds the ones the game data blocks while
 *   shapeshifted: stat scrolls (Scroll of Agility and the like), water breathing elixirs, Gift of
 *   Arthas, drums, battle standards and the rest. The other forms keep the stock rules.
 *
 * - Cat Form combo points work like mod-forever-rogue's rogue combo points: unused points follow
 *   the druid to the next target, with the full count, and points left on a target that died wait
 *   a little while for the next one.
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
#include "DataMap.h"
#include "DBCStores.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Spell.h"
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
#include <unordered_map>

namespace
{
    // Every rank of Swipe (Bear), from rank 1 to rank 8. Must match tools/patch-forever-druid-dbc.sh.
    constexpr std::array<uint32, 8> SPELL_SWIPE_BEAR_RANKS = { 779, 780, 769, 9754, 9908, 26997, 48561, 48562 };

    constexpr uint32 SPELL_BEAR_FORM      = 5487;
    constexpr uint32 SPELL_DIRE_BEAR_FORM = 9634;
    constexpr uint32 SPELL_LACERATE_R1    = 33745; // must match tools/patch-forever-druid-dbc.sh

    // Lacerate rank 1's scaling: its hit and each bleed tick do LACERATE_BASE_DAMAGE at
    // LACERATE_BASE_LEVEL, growing LACERATE_DAMAGE_PER_LEVEL a level up to the game data's 31 at
    // LACERATE_MAX_LEVEL (66, where it's normally trained). Must match
    // tools/patch-forever-druid-dbc.sh.
    constexpr uint32 LACERATE_BASE_LEVEL       = 42;
    constexpr uint32 LACERATE_MAX_LEVEL        = 66;
    constexpr int32  LACERATE_BASE_DAMAGE      = 20;
    constexpr float  LACERATE_DAMAGE_PER_LEVEL = 0.46f;

    // Must match tools/patch-forever-druid-dbc.sh.
    constexpr uint32 SPELL_FRENZIED_REGENERATION = 22842;
    constexpr uint32 SPELL_PULVERIZE             = 24042; // "Test Maul" in the stock client
    constexpr uint32 SPELL_PULVERIZE_BUFF        = 742;   // "Pulverize", an unused NPC aura

    // Lacerate's global cooldown category and time, so Pulverize shares the global cooldown.
    constexpr uint32 GLOBAL_COOLDOWN_CATEGORY = 133;
    constexpr uint32 GLOBAL_COOLDOWN_MS       = 1500;

    // SpellDuration.dbc row for 10 seconds.
    constexpr uint32 DURATION_10_SECONDS = 1;

    // The passive auras the travel forms cast on the druid, which hold the forms' speed. Travel
    // Form: effect 0 ground speed (40%). Flight Form and Swift Flight Form: effect 0 ground speed
    // (60% / 100%), effect 1 flight speed (150% / 280%).
    constexpr uint32 SPELL_TRAVEL_FORM_PASSIVE       = 5419;
    constexpr uint32 SPELL_FLIGHT_FORM_PASSIVE       = 33948;
    constexpr uint32 SPELL_SWIFT_FLIGHT_FORM_PASSIVE = 40121;

    // Riding skills, as mod-mount-scaling checks them.
    constexpr uint32 SPELL_APPRENTICE_RIDING = 33388;
    constexpr uint32 SPELL_JOURNEYMAN_RIDING = 33391;
    constexpr uint32 SPELL_EXPERT_RIDING     = 34090;
    constexpr uint32 SPELL_ARTISAN_RIDING    = 34091;

    struct Config
    {
        bool swipeEnabled = true;
        uint32 swipeRageCost = 0;
        bool swipeLearnWithBearForm = true;
        bool dodgeRageEnabled = true;
        uint32 dodgeRage = 5;
        bool frenziedRegenerationEnabled = true;
        float frenziedRegenerationHealthPercent = 1.0f;
        bool lacerateScaling = true;
        bool pulverizeEnabled = true;
        uint8 pulverizeLevel = 42;
        uint32 pulverizeRageCost = 15;
        uint32 pulverizeWeaponDamagePercent = 60;
        float pulverizeAttackPowerPerStack = 0.04f;
        uint32 pulverizeCritPerStack = 2;
        bool formSpeedEnabled = true;
        bool formSpeedOutOfCombatOnly = true;
        bool formConsumablesEnabled = true;
        bool catComboPointsEnabled = true;
        uint32 catComboPointsKeepAfterKill = 20000;
    };

    // mod-mount-scaling's settings, read from its own config file (MountScaling.*). enabled is
    // false when that module isn't installed, and the forms keep their stock speed.
    struct MountScalingConfig
    {
        bool enabled = false;
        float groundPerLevel = 2.5f;
        float groundMin = 20.0f;
        float groundMax = 100.0f;
        float journeymanPerLevel = 2.5f;
        float journeymanMin = 100.0f;
        float journeymanMax = 150.0f;
        float expertPerLevel = 5.0f;
        float expertMin = 150.0f;
        float expertMax = 200.0f;
        float artisanPerLevel = 8.0f;
        float artisanMin = 200.0f;
        float artisanMax = 280.0f;
    };

    MountScalingConfig mountScaling;

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

    // Lacerate rank 1 as the game data has it, read before the module changes it.
    struct LacerateData
    {
        uint32 maxLevel;
        uint32 baseLevel;
        uint32 spellLevel;
        std::array<int32, 2> basePoints;
        std::array<float, 2> pointsPerLevel;
    };

    std::optional<LacerateData> stockLacerate;

    // Druid trainers teach Lacerate rank 1 at 42 instead of 66 (see the SQL), but its flat damage
    // (31 on the hit and 31 per bleed tick, per stack) was set for 66. Make both effects scale
    // with the druid's level instead, the way the game data does for spells like pet abilities:
    // the core adds the points per level for each level above the spell level, up to the max
    // level. From 66 on it's the stock 31. The value is the base points plus 1 (the die has one
    // side).
    void ApplyLacerateScaling()
    {
        SpellInfo* spellInfo = const_cast<SpellInfo*>(sSpellMgr->GetSpellInfo(SPELL_LACERATE_R1));
        if (!spellInfo)
            return;

        if (!stockLacerate)
        {
            stockLacerate = LacerateData{ spellInfo->MaxLevel, spellInfo->BaseLevel, spellInfo->SpellLevel,
                { spellInfo->Effects[EFFECT_0].BasePoints, spellInfo->Effects[EFFECT_1].BasePoints },
                { spellInfo->Effects[EFFECT_0].RealPointsPerLevel, spellInfo->Effects[EFFECT_1].RealPointsPerLevel } };
        }

        bool const scale = config.lacerateScaling;
        spellInfo->MaxLevel   = scale ? LACERATE_MAX_LEVEL : stockLacerate->maxLevel;
        spellInfo->BaseLevel  = scale ? LACERATE_BASE_LEVEL : stockLacerate->baseLevel;
        spellInfo->SpellLevel = scale ? LACERATE_BASE_LEVEL : stockLacerate->spellLevel;

        for (uint8 i = EFFECT_0; i <= EFFECT_1; ++i)
        {
            SpellEffectInfo& effect = spellInfo->Effects[i];
            effect.BasePoints         = scale ? LACERATE_BASE_DAMAGE - 1 : stockLacerate->basePoints[i];
            effect.RealPointsPerLevel = scale ? LACERATE_DAMAGE_PER_LEVEL : stockLacerate->pointsPerLevel[i];
        }
    }

    // Cat Form, Bear Form and Dire Bear Form as a spell's form mask (bit form - 1).
    constexpr uint32 FORM_MASK_CAT_AND_BEAR = (1 << (FORM_CAT - 1)) | (1 << (FORM_BEAR - 1)) | (1 << (FORM_DIREBEAR - 1));

    // A consumable's spell as the game data has it, read before the module changes it.
    struct ConsumableSpellData
    {
        uint32 stances;
        uint32 attributesEx2;
    };

    std::unordered_map<uint32, ConsumableSpellData> stockConsumableSpells;

    // Whether a consumable's on-use spell should become usable in Cat Form and Bear Form. Must
    // match tools/patch-forever-druid-dbc.sh, which has the list these rules give for the stock
    // items.
    bool IsFormConsumableSpell(SpellInfo const* spellInfo)
    {
        // Only the spells shapeshifting blocks, and none that already ask for a form.
        if (!spellInfo->HasAttribute(SPELL_ATTR0_NOT_SHAPESHIFTED) || spellInfo->Stances)
            return false;

        // Enchanting scrolls cast the enchanter's own trade skill spell, and a few quest items
        // cast a class spell (Flamestrike, Chain Heal): leave the spells players learn alone.
        SkillLineAbilityMapBounds const skills = sSpellMgr->GetSkillLineAbilityMapBounds(spellInfo->Id);
        if (spellInfo->HasAttribute(SPELL_ATTR0_IS_TRADESKILL) || skills.first != skills.second)
            return false;

        // Rogue poisons go on a weapon a feral druid isn't using, and mounts and disguises don't
        // mix with a form.
        return !spellInfo->HasEffect(SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY) && !spellInfo->HasAura(SPELL_AURA_MOUNTED)
            && !spellInfo->HasAura(SPELL_AURA_TRANSFORM);
    }

    // Let the consumables the game data blocks while shapeshifted be used in Cat Form, Bear Form
    // and Dire Bear Form too. Their spells say "not while shapeshifted"; adding the feral forms
    // to their form list, with "also outside a form", makes them work in caster form and the
    // feral forms only, the way the game data lets Thorns be cast in Moonkin Form. Other forms
    // (Travel, Moonkin, Tree of Life, Ghost Wolf) still block them, and the buffs they give
    // don't drop when the druid shifts.
    void ApplyFormConsumables()
    {
        // The item templates are loaded after the first config load, so this finds nothing until
        // OnBeforeWorldInitialized.
        if (stockConsumableSpells.empty())
        {
            for (auto const& [entry, proto] : *sObjectMgr->GetItemTemplateStore())
            {
                if (proto.Class != ITEM_CLASS_CONSUMABLE)
                    continue;

                for (auto const& itemSpell : proto.Spells)
                {
                    if (itemSpell.SpellId <= 0 || itemSpell.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
                        continue;

                    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(itemSpell.SpellId);
                    if (spellInfo && IsFormConsumableSpell(spellInfo))
                        stockConsumableSpells.emplace(spellInfo->Id, ConsumableSpellData{ spellInfo->Stances, spellInfo->AttributesEx2 });
                }
            }
        }

        for (auto const& [spellId, stock] : stockConsumableSpells)
        {
            SpellInfo* spellInfo = const_cast<SpellInfo*>(sSpellMgr->GetSpellInfo(spellId));
            if (!spellInfo)
                continue;

            if (config.formConsumablesEnabled)
            {
                spellInfo->Stances = stock.stances | FORM_MASK_CAT_AND_BEAR;
                spellInfo->AttributesEx2 = stock.attributesEx2 | SPELL_ATTR2_ALLOW_WHILE_NOT_SHAPESHIFTED;
            }
            else
            {
                spellInfo->Stances = stock.stances;
                spellInfo->AttributesEx2 = stock.attributesEx2;
            }
        }
    }

    // Runs once the spells are loaded, and again when the config is reloaded.
    void ApplySpellChanges()
    {
        ApplySwipeRageCost();
        ApplyFrenziedRegenerationRate();
        ApplyLacerateScaling();
        ApplyPulverizeSpellData();
        ApplyFormConsumables();
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

// mod-mount-scaling's formulas (src/MountScaling.cpp there), so a druid in a travel form goes as
// fast as they would on a mount: ground speed from Apprentice or Journeyman Riding, flight speed
// from Expert or Artisan Riding, growing with level. 0 means "no riding skill for it": keep the
// form's stock speed.
namespace FormSpeed
{
    int32 Ground(Player* player)
    {
        if (!player->HasSpell(SPELL_APPRENTICE_RIDING))
            return 0;

        float const level = player->GetLevel();
        MountScalingConfig const& c = mountScaling;

        if (player->HasSpell(SPELL_JOURNEYMAN_RIDING))
            return int32(std::clamp(c.journeymanMin + (level - 40) * c.journeymanPerLevel, c.journeymanMin, c.journeymanMax));

        return int32(std::min(std::max(c.groundMin, level * c.groundPerLevel), c.groundMax));
    }

    int32 Flight(Player* player)
    {
        float const level = player->GetLevel();
        MountScalingConfig const& c = mountScaling;

        if (player->HasSpell(SPELL_ARTISAN_RIDING))
            return int32(std::clamp(c.artisanMin + (level - 70) * c.artisanPerLevel, c.artisanMin, c.artisanMax));

        if (player->HasSpell(SPELL_EXPERT_RIDING))
            return int32(std::clamp(c.expertMin + (level - 60) * c.expertPerLevel, c.expertMin, c.expertMax));

        return 0;
    }

    bool IsFormPassive(uint32 spellId)
    {
        return spellId == SPELL_TRAVEL_FORM_PASSIVE || spellId == SPELL_FLIGHT_FORM_PASSIVE
            || spellId == SPELL_SWIFT_FLIGHT_FORM_PASSIVE;
    }

    // Swift Flight Form's speed for a druid who owns a 310% flying mount, as the core's
    // spell_dru_swift_flight_passive gives it.
    constexpr int32 SWIFT_FLIGHT_FORM_310_SPEED = 310;

    // Set the speed effects of one of the form passives. Runs after the aura is applied, so it
    // comes after the core's spell_dru_swift_flight_passive. ChangeAmount updates the speed at
    // once.
    void Apply(Player* player, Aura* aura)
    {
        if (!config.formSpeedEnabled || !mountScaling.enabled)
            return;

        for (uint8 i = EFFECT_0; i <= EFFECT_1; ++i)
        {
            AuraEffect* effect = aura->GetEffect(i);
            if (!effect)
                continue;

            int32 speed = 0;
            if (effect->GetAuraType() == SPELL_AURA_MOD_INCREASE_SPEED)
            {
                // Travel Form goes back to its own speed (40%) while the druid is in combat.
                if (aura->GetId() == SPELL_TRAVEL_FORM_PASSIVE && config.formSpeedOutOfCombatOnly && player->IsInCombat())
                    speed = effect->GetSpellInfo()->Effects[i].CalcValue();
                else
                    speed = Ground(player);
            }
            else if (effect->GetAuraType() == SPELL_AURA_MOD_INCREASE_FLIGHT_SPEED)
            {
                speed = Flight(player);

                // A druid with a 310% mount keeps a 310% Swift Flight Form at every level: the
                // scaling never takes that away.
                if (aura->GetId() == SPELL_SWIFT_FLIGHT_FORM_PASSIVE && player->Has310Flyer(false))
                    speed = std::max(speed, SWIFT_FLIGHT_FORM_310_SPEED);
            }

            if (speed > 0 && speed != effect->GetAmount())
                effect->ChangeAmount(speed);
        }
    }

    // After a level up, or entering or leaving combat, for a druid who is in a travel form right
    // now. Other changes (a new riding skill, a config reload) take effect the next time they
    // shift.
    void Update(Player* player)
    {
        for (uint32 spellId : { SPELL_TRAVEL_FORM_PASSIVE, SPELL_FLIGHT_FORM_PASSIVE, SPELL_SWIFT_FLIGHT_FORM_PASSIVE })
            if (Aura* aura = player->GetAura(spellId))
                Apply(player, aura);
    }
}

// Cat Form combo points, as mod-forever-rogue does them for rogues: unused points move to the
// next hostile target the druid selects or attacks, with the full count, and points left on a
// target that died wait CatComboPoints.KeepAfterKill for the next one. The server tells the client
// which target holds the points, so it needs no client patch. Only druids get combo points from
// Cat Form abilities, so this follows any druid's points, whatever form they're in.
namespace CatComboPoints
{
    // After a finisher, points that vanish belong to the finisher, not to a dying target.
    constexpr uint32 FINISHER_WINDOW = 3000;

    struct ComboState : public DataMap::Base
    {
        // Points and target as of the last update, to tell what was lost when the target died.
        uint8 lastPoints = 0;
        ObjectGuid lastTarget;

        // Points from a target that died, waiting for the next target.
        uint8 savedPoints = 0;
        uint32 savedTimer = 0;

        // Time left in which a finisher may still take the points.
        uint32 finisherTimer = 0;

        // Points held when a builder was used, in case the builder kills its target: the core
        // clears the points at the death and then adds the builder's points to the corpse from
        // zero.
        bool builderPending = false;
        uint8 builderBase = 0;
        ObjectGuid builderTarget;
    };

    ComboState* GetState(Player* player)
    {
        return player->CustomData.GetDefault<ComboState>("mod-forever-druid-combo-points");
    }

    bool IsEnabledFor(Player* player)
    {
        return config.catComboPointsEnabled && player->getClass() == CLASS_DRUID;
    }

    bool CanTakePoints(Player* player, Unit* target)
    {
        return target && target != player && target->IsAlive() && player->IsValidAttackTarget(target);
    }

    // Moves the druid's combo points, or points saved from a dead target, onto the new target.
    void MovePointsTo(Player* player, ComboState* state, Unit* target)
    {
        if (!CanTakePoints(player, target) || player->GetComboTarget() == target)
            return;

        uint8 points = player->GetComboPoints();
        if (!points)
        {
            points = state->savedPoints;
            if (!points)
                return;
        }

        state->savedPoints = 0;
        state->savedTimer = 0;

        // For a new target, AddComboPoints sets the count instead of adding to it. It also moves
        // the points' holder and tells the client, which then shows them on the new target.
        player->AddComboPoints(target, points);
    }

    // Runs before the spell's effects and before its last cast check, so a builder or finisher
    // used on a target that isn't selected (a mouseover or focus macro) finds the points there.
    void OnSpellCast(Player* player, Spell* spell)
    {
        if (!IsEnabledFor(player))
            return;

        SpellInfo const* spellInfo = spell->GetSpellInfo();
        ComboState* state = GetState(player);

        if (spellInfo->NeedsComboPoints())
        {
            state->lastPoints = 0;
            state->savedPoints = 0;
            state->savedTimer = 0;
            state->builderPending = false;
            state->finisherTimer = FINISHER_WINDOW;
            return;
        }

        if (!spellInfo->HasEffect(SPELL_EFFECT_ADD_COMBO_POINTS))
            return;

        Unit* target = spell->m_targets.GetUnitTarget();
        if (!target)
            return;

        MovePointsTo(player, state, target);

        // A proc like Primal Fury adds a point to the same target before the builder is done;
        // keep the count from before the builder.
        if (state->builderPending && state->builderTarget == target->GetGUID())
            return;

        state->builderPending = true;
        state->builderBase = player->GetComboTarget() == target ? player->GetComboPoints() : 0;
        state->builderTarget = target->GetGUID();
    }

    void OnUpdate(Player* player, uint32 diff)
    {
        if (!IsEnabledFor(player))
            return;

        ComboState* state = GetState(player);

        if (!player->IsAlive())
        {
            *state = ComboState();
            return;
        }

        state->finisherTimer = state->finisherTimer > diff ? state->finisherTimer - diff : 0;

        if (state->savedTimer)
        {
            state->savedTimer = state->savedTimer > diff ? state->savedTimer - diff : 0;
            if (!state->savedTimer)
                state->savedPoints = 0;
        }

        // A builder killed its target: put back the points the death took.
        if (state->builderPending)
        {
            state->builderPending = false;

            Unit* target = ObjectAccessor::GetUnit(*player, state->builderTarget);
            if (config.catComboPointsKeepAfterKill && state->builderBase && player->GetComboPoints()
                && player->GetComboTargetGUID() == state->builderTarget && (!target || !target->IsAlive()))
                player->AddComboPoints(state->builderBase);
        }

        uint8 const points = player->GetComboPoints();

        if (points)
        {
            state->lastPoints = points;
            state->lastTarget = player->GetComboTargetGUID();
        }
        else if (state->lastPoints)
        {
            // The points are gone. Keep them only if their target died or despawned; a finisher
            // or the end of a duel leave the target alive.
            Unit* target = ObjectAccessor::GetUnit(*player, state->lastTarget);
            if ((!target || !target->IsAlive()) && !state->finisherTimer && config.catComboPointsKeepAfterKill)
            {
                state->savedPoints = state->lastPoints;
                state->savedTimer = config.catComboPointsKeepAfterKill;
            }

            state->lastPoints = 0;
            state->lastTarget.Clear();
        }

        MovePointsTo(player, state, player->GetSelectedUnit());
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

        config.lacerateScaling = sConfigMgr->GetOption<bool>("ForeverDruid.Lacerate.ScaleWithLevel", true);

        config.pulverizeEnabled             = sConfigMgr->GetOption<bool>("ForeverDruid.Pulverize.Enable", true);
        config.pulverizeLevel               = uint8(sConfigMgr->GetOption<uint32>("ForeverDruid.Pulverize.Level", 42));
        config.pulverizeRageCost            = sConfigMgr->GetOption<uint32>("ForeverDruid.Pulverize.RageCost", 15);
        config.pulverizeWeaponDamagePercent = sConfigMgr->GetOption<uint32>("ForeverDruid.Pulverize.WeaponDamagePercent", 60);
        config.pulverizeAttackPowerPerStack = sConfigMgr->GetOption<float>("ForeverDruid.Pulverize.AttackPowerPerStack", 0.04f);
        config.pulverizeCritPerStack        = sConfigMgr->GetOption<uint32>("ForeverDruid.Pulverize.CritPerStack", 2);

        config.formSpeedEnabled         = sConfigMgr->GetOption<bool>("ForeverDruid.FormSpeed.Enable", true);
        config.formSpeedOutOfCombatOnly = sConfigMgr->GetOption<bool>("ForeverDruid.FormSpeed.OutOfCombatOnly", true);

        config.formConsumablesEnabled = sConfigMgr->GetOption<bool>("ForeverDruid.FormConsumables.Enable", true);

        config.catComboPointsEnabled       = sConfigMgr->GetOption<bool>("ForeverDruid.CatComboPoints.Enable", true);
        config.catComboPointsKeepAfterKill = sConfigMgr->GetOption<uint32>("ForeverDruid.CatComboPoints.KeepAfterKill", 20000);

        // mod-mount-scaling's own settings, with its defaults. Without that module these aren't
        // in any config file, so don't log them as missing.
        auto mountOption = [](char const* name, float def) { return sConfigMgr->GetOption<float>(name, def, false); };
        mountScaling.enabled            = sConfigMgr->GetOption<bool>("MountScaling.Enable", false, false);
        mountScaling.groundPerLevel     = mountOption("MountScaling.Ground.SpeedPerLevel", 2.5f);
        mountScaling.groundMin          = mountOption("MountScaling.Ground.MinSpeed", 20.0f);
        mountScaling.groundMax          = mountOption("MountScaling.Ground.MaxSpeed", 100.0f);
        mountScaling.journeymanPerLevel = mountOption("MountScaling.Ground.Journeyman.SpeedPerLevel", 2.5f);
        mountScaling.journeymanMin      = mountOption("MountScaling.Ground.Journeyman.MinSpeed", 100.0f);
        mountScaling.journeymanMax      = mountOption("MountScaling.Ground.Journeyman.MaxSpeed", 150.0f);
        mountScaling.expertPerLevel     = mountOption("MountScaling.Flying.Expert.SpeedPerLevel", 5.0f);
        mountScaling.expertMin          = mountOption("MountScaling.Flying.Expert.MinSpeed", 150.0f);
        mountScaling.expertMax          = mountOption("MountScaling.Flying.Expert.MaxSpeed", 200.0f);
        mountScaling.artisanPerLevel    = mountOption("MountScaling.Flying.Artisan.SpeedPerLevel", 8.0f);
        mountScaling.artisanMin         = mountOption("MountScaling.Flying.Artisan.MinSpeed", 200.0f);
        mountScaling.artisanMax         = mountOption("MountScaling.Flying.Artisan.MaxSpeed", 280.0f);

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
        FormSpeed::Update(player);
    }

    // The core sets the combat flag before calling these, so FormSpeed sees the new state.
    void OnPlayerEnterCombat(Player* player, Unit* /*enemy*/) override
    {
        if (player->getClass() == CLASS_DRUID)
            FormSpeed::Update(player);
    }

    void OnPlayerLeaveCombat(Player* player) override
    {
        if (player->getClass() == CLASS_DRUID)
            FormSpeed::Update(player);
    }

    void OnPlayerLearnSpell(Player* player, uint32 spellId) override
    {
        if (spellId == SPELL_BEAR_FORM || spellId == SPELL_DIRE_BEAR_FORM)
            UpdateSwipe(player);
    }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        CatComboPoints::OnSpellCast(player, spell);
    }

    void OnPlayerUpdate(Player* player, uint32 diff) override
    {
        CatComboPoints::OnUpdate(player, diff);
    }
};

// Travel form speeds. A UnitScript, like mod-mount-scaling's, rather than an AuraScript: it runs
// after every effect is applied, so it comes after the core's own script on Swift Flight Form.
class ForeverDruidUnitScript : public UnitScript
{
public:
    ForeverDruidUnitScript() : UnitScript("ForeverDruidUnitScript", true, { UNITHOOK_ON_AURA_APPLY }) { }

    void OnAuraApply(Unit* unit, Aura* aura) override
    {
        if (!FormSpeed::IsFormPassive(aura->GetId()))
            return;

        if (Player* player = unit->ToPlayer())
            FormSpeed::Apply(player, aura);
    }
};

void AddForeverDruidScripts()
{
    new ForeverDruidWorldScript();
    new ForeverDruidPlayerScript();
    new ForeverDruidUnitScript();
    RegisterSpellScript(spell_dru_forever_bear_dodge_rage);
    RegisterSpellScript(spell_dru_forever_pulverize);
    RegisterSpellScript(spell_dru_forever_pulverize_buff);
}
