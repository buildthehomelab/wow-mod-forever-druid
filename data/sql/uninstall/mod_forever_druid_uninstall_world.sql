-- mod-forever-druid: undo the module's world database changes. Run it by hand on the world
-- database after removing the module; AzerothCore doesn't run it automatically (it only runs the
-- module's db-world folder).
--
-- Turning the module off doesn't need this: ForeverDruid.BearDodgeRage.Enable = 0 and
-- ForeverDruid.Swipe.Enable = 0 are enough. This is for taking the module out of the server for
-- good, so the core doesn't log a missing script. The Swipe rage cost lives only in the running
-- server's memory, so there's nothing in the database to undo for it.
--
-- Removes the spell_proc rows and script binding on Bear Form (5487) and Dire Bear Form (9634).
-- Stock AzerothCore has no spell_proc rows for them. Idempotent: safe to run again.

DELETE FROM `spell_proc` WHERE `SpellId` IN (5487, 9634);
DELETE FROM `spell_script_names` WHERE `ScriptName` = 'spell_dru_forever_bear_dodge_rage';
