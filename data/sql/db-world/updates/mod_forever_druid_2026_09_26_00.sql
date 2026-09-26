-- mod-forever-druid: rage when a druid in Bear Form (5487) or Dire Bear Form (9634) dodges.
--
-- The spell_proc rows let the two form auras proc when their owner dodges, the same way the
-- Natural Reaction talent does (ProcFlags 0x2AA8 = taken melee, ranged and spell attacks,
-- SpellTypeMask 1 = damage, HitMask 16 = dodge). The script turns the proc into rage. Neither
-- spell had a spell_proc row before; their existing script (spell_dru_feral_swiftness) is kept.
--
-- Spell ids must match src/ForeverDruid.cpp. Idempotent: safe to run again.

DELETE FROM `spell_proc` WHERE `SpellId` IN (5487, 9634);
INSERT INTO `spell_proc` (`SpellId`, `SchoolMask`, `SpellFamilyName`, `SpellFamilyMask0`, `SpellFamilyMask1`, `SpellFamilyMask2`, `ProcFlags`, `SpellTypeMask`, `SpellPhaseMask`, `HitMask`, `AttributesMask`, `DisableEffectsMask`, `ProcsPerMinute`, `Chance`, `Cooldown`, `Charges`) VALUES
(5487, 0, 0, 0, 0, 0, 0x2AA8, 1, 0, 16, 0, 0, 0, 100, 0, 0),
(9634, 0, 0, 0, 0, 0, 0x2AA8, 1, 0, 16, 0, 0, 0, 100, 0, 0);

DELETE FROM `spell_script_names` WHERE `ScriptName` = 'spell_dru_forever_bear_dodge_rage';
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(5487, 'spell_dru_forever_bear_dodge_rage'),
(9634, 'spell_dru_forever_bear_dodge_rage');
