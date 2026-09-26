-- mod-forever-druid: undo the module's world database changes. Run it by hand on the world
-- database after removing the module; AzerothCore doesn't run it automatically (it only runs the
-- module's db-world folder).
--
-- Turning the module off doesn't need this: the Enable settings are enough. This is for taking the module out of the server for
-- good, so the core doesn't log a missing script. The spell changes (Swipe's rage cost, Frenzied
-- Regeneration's rate, Pulverize) live only in the running server's memory, so there's nothing in
-- the database to undo for them. Run mod_forever_druid_uninstall_characters.sql too.
--
-- Removes the spell_proc rows and script binding on Bear Form (5487) and Dire Bear Form (9634),
-- and the Pulverize script bindings on 24042 and 742. Stock AzerothCore has no spell_proc rows or
-- scripts for any of them except Bear Form's own spell_dru_feral_swiftness, which stays.
-- Idempotent: safe to run again.

DELETE FROM `spell_proc` WHERE `SpellId` IN (5487, 9634);
DELETE FROM `spell_script_names` WHERE `ScriptName` IN ('spell_dru_forever_bear_dodge_rage', 'spell_dru_forever_pulverize', 'spell_dru_forever_pulverize_buff');
