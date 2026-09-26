-- mod-forever-druid: druid trainers teach Lacerate (rank 1, 33745) at level 42 instead of 66, as
-- in WoW Forever, so Pulverize (level 42) has stacks to use up from the start. It costs 1g 60s,
-- like the other level 42 druid spells, instead of 6g 60s. Ranks 2 (73) and 3 (80) don't change.
--
-- data/sql/uninstall/mod_forever_druid_uninstall_world.sql puts back 66 and 6g 60s. Idempotent:
-- safe to run again.

UPDATE `trainer_spell` SET `ReqLevel` = 42, `MoneyCost` = 16000 WHERE `SpellId` = 33745;
