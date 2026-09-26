# Forever Druid

An [AzerothCore](https://www.azerothcore.org/) (WotLK 3.3.5a) module that brings WoW Forever-style
bear tanking to a 3.3.5 server:

- **Swipe (Bear) costs no rage**, at every rank.
- **Bear Form and Dire Bear Form give 5 rage every time you dodge.**
- **Frenzied Regeneration turns each point of rage into 1% of max health**, up from 0.3%.

The dodge rage and Frenzied Regeneration work without a client patch. A free Swipe needs the
optional client patch (see below), because the client checks Swipe's rage cost itself.

## Swipe (Bear)

All 8 ranks (779, 780, 769, 9754, 9908, 26997, 48561, 48562) cost 0 rage instead of 20. The rage
cost is a setting, so you can make it cheaper instead of free. The Ferocity talent still takes
its 1-5 rage off, down to 0.

Swipe (Cat) isn't changed.

## Rage from dodging

Every time a druid in Bear Form or Dire Bear Form dodges an attack, they gain 5 rage, whether it
was a melee swing or an ability. It triggers on the same attacks as Natural Reaction. The combat
log shows it as "You gain 5 Rage from Bear Form."

It stacks with the Natural Reaction talent, which gives its own 1-3 rage per dodge, so a druid
with 3/3 Natural Reaction gets 8 rage per dodge. Like any rage gain from a spell, it adds a
little threat.

NPC druids in bear form don't get it.

## Frenzied Regeneration

Frenzied Regeneration (3 minute cooldown) still turns up to 10 rage per second into health for
10 seconds, but each point of rage now heals 1% of max health instead of 0.3%, as in WoW Forever.
With 100 rage it heals you to full over the 10 seconds; with 30 rage, 30%. It's the thing to spend
the extra rage from dodging and free Swipe on.

The healing still counts as healing received, so talents, glyphs and healing debuffs change it as
before.

## Install

Clone it into your AzerothCore `modules` folder **as `mod-forever-druid`**, without the repo's
`wow-` prefix. AzerothCore finds the module's entry point from the folder name.

```bash
cd <azerothcore>/modules
git clone https://github.com/buildthehomelab/wow-mod-forever-druid.git mod-forever-druid
```

Rebuild the worldserver, then copy `conf/mod_forever_druid.conf.dist` to your config folder as
`mod_forever_druid.conf`. The database changes (two `spell_proc` rows and a spell script binding
on Bear Form and Dire Bear Form) are added to the world database on the next start.

## Settings

| Setting | Default | What it does |
|---------|---------|--------------|
| `ForeverDruid.Swipe.Enable` | `1` | Change Swipe (Bear)'s rage cost. `0` puts it back to 20. |
| `ForeverDruid.Swipe.RageCost` | `0` | Swipe (Bear)'s rage cost when enabled. |
| `ForeverDruid.BearDodgeRage.Enable` | `1` | Give rage for dodging in Bear Form and Dire Bear Form. |
| `ForeverDruid.BearDodgeRage.Amount` | `5` | Rage per dodge. |
| `ForeverDruid.FrenziedRegeneration.Enable` | `1` | Change Frenzied Regeneration's rate. `0` puts it back to 0.3%. |
| `ForeverDruid.FrenziedRegeneration.HealthPercentPerRage` | `1.0` | Share of max health per point of rage, in steps of 0.1. |

All of them take effect on `.reload config`. If you change `RageCost` or `HealthPercentPerRage`
and use the client patch, change `RAGE_COST` or `HEALTH_PERCENT_PER_RAGE` at the top of
`tools/patch-forever-druid-dbc.sh` and rebuild the patch.

## Optional client patch

The client reads Swipe's rage cost from its own Spell.dbc. Without the patch it still thinks
Swipe costs 20 rage: the tooltip says 20 Rage, and the client won't let you press Swipe with less
than 20 rage ("Not enough rage"). With 20 or more, Swipe works and costs nothing.

Frenzied Regeneration heals the new amount without the patch, but its tooltip still says 0.3%.

`tools/patch-forever-druid-dbc.sh` sets the rage cost of all 8 Swipe (Bear) ranks and Frenzied
Regeneration's rate in Spell.dbc. Nothing else changes.

```bash
tools/patch-forever-druid-dbc.sh <Spell.dbc> DBFilesClient
```

Only the newest client patch's Spell.dbc is used, so if another patch already ships one (for
example with mod-profession-craft-cd, mod-hearthstone-cd and mod-forever-paladin changes), give
the script that Spell.dbc and put the result back in that same patch. The script can be run again
on its own output.

Pack it into the MPQ as `DBFilesClient\Spell.dbc`. Players who get the new patch should delete
their `Cache/` folder.

## Turning it off

- **Keep the module, switch things off:** set any of `ForeverDruid.Swipe.Enable`,
  `ForeverDruid.BearDodgeRage.Enable` and `ForeverDruid.FrenziedRegeneration.Enable` to `0`, then
  `.reload config` or restart. If you shipped the client patch, players' tooltips keep the new
  values until you take the changes out of the patch.
- **Remove the module for good:** stop the worldserver, delete the module, rebuild, and run
  `data/sql/uninstall/mod_forever_druid_uninstall_world.sql` on the world database. It removes the
  `spell_proc` rows and the script binding. Nothing is saved on characters, so there's no
  characters database cleanup. Take the Swipe and Frenzied Regeneration changes out of the client
  patch too.

AzerothCore never runs the `uninstall` folder by itself; it only runs the module's `db-world`
folder.
