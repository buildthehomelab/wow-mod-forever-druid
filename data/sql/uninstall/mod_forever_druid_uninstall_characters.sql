-- mod-forever-druid: take Pulverize ("Test Maul", 24042) away from every character. Run it by
-- hand on the characters database, with the worldserver stopped: a running server saves online
-- characters over these tables. AzerothCore doesn't run it automatically.
--
-- While the module is still installed, ForeverDruid.Pulverize.Enable = 0 does the same thing at
-- each druid's next login. Once the module is gone nothing removes the spell, and druids would
-- keep the unscripted "Test Maul". Run this after removing the module (with
-- mod_forever_druid_uninstall_world.sql).
--
-- 24042 is a test spell nothing else teaches players, so every character that has it got it from
-- this module (or a GM). Also clears it from action bars, and the crit buff (742) from saved
-- auras. Swipe (Bear) rank 1 learned early is a normal trainer spell and stays. Idempotent: safe
-- to run again.

DELETE FROM `character_spell` WHERE `spell` = 24042;
DELETE FROM `character_action` WHERE `action` = 24042 AND `type` = 0; -- 0 = spell button
DELETE FROM `character_aura` WHERE `spell` = 742;
