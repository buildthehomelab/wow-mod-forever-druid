-- mod-forever-druid: Pulverize.
--
-- 24042 is "Test Maul", a leftover Blizzard test spell that no trainer, item or creature uses;
-- the module turns it into Pulverize. 742 is "Pulverize", an unused NPC aura (no creature has
-- it); the module turns it into Pulverize's crit buff. The scripts leave everything else alone.
--
-- Spell ids must match src/ForeverDruid.cpp. Idempotent: safe to run again.

DELETE FROM `spell_script_names` WHERE `ScriptName` IN ('spell_dru_forever_pulverize', 'spell_dru_forever_pulverize_buff');
INSERT INTO `spell_script_names` (`spell_id`, `ScriptName`) VALUES
(24042, 'spell_dru_forever_pulverize'),
(742, 'spell_dru_forever_pulverize_buff');
