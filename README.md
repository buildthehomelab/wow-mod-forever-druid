# Forever Druid

An [AzerothCore](https://www.azerothcore.org/) (WotLK 3.3.5a) module that brings WoW Forever-style
bear tanking to a 3.3.5 server:

- **Swipe (Bear) costs no rage**, at every rank, and you learn it together with Bear Form.
- **Bear Form and Dire Bear Form give 5 rage every time you dodge.**
- **Frenzied Regeneration turns each point of rage into 1% of max health**, up from 0.3%.
- **Pulverize**, Cataclysm's bear finisher, at level 42, and **Lacerate moves to level 42** to
  go with it.

Everything works on the server alone, but the optional client patch (see below) is strongly
recommended: without it the client won't let you press Swipe with less than 20 rage, and
Pulverize shows up as "Test Maul".

## Swipe (Bear)

All 8 ranks (779, 780, 769, 9754, 9908, 26997, 48561, 48562) cost 0 rage instead of 20. The rage
cost is a setting, so you can make it cheaper instead of free. The Ferocity talent still takes
its 1-5 rage off, down to 0.

Swipe (Cat) isn't changed.

Druids learn Swipe (Bear) rank 1 together with Bear Form (the level 10 class quest) instead of
training it at level 16, so a new bear has something to spend rage on. Druids who already know
Bear Form get it at their next login. Later ranks are trained as usual.

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

## Pulverize

Cataclysm's Pulverize, learned at level 42:

- **15 rage**, instant, melee range, on the global cooldown. Bear Form or Dire Bear Form only.
- Deals **60% weapon damage plus 4% of your attack power for each Lacerate stack** you have on
  the target, and uses up the stacks.
- Gives you **2% melee crit for each stack used up, for 10 seconds** (up to 10% with 5 stacks).
  A new Pulverize replaces the buff.
- A miss, dodge or parry keeps the stacks.

The usual rotation is to build Lacerate to 5 stacks, Pulverize, and start again.

Druid trainers teach Lacerate (rank 1) at level 42 instead of 66, as in WoW Forever, for 1g 60s
like the other level 42 druid spells. Ranks 2 and 3 stay at 73 and 80. This is a database change,
not a setting: if you set a different `Pulverize.Level`, Lacerate still comes at 42. The trainer
window gets the level from the server, so it needs no client patch.

The numbers differ from Cataclysm on purpose: WotLK's Lacerate stacks to 5 (Cataclysm's stacked
to 3), so crit per stack is 2% instead of 3%, and the per-stack damage scales with attack power
so it works at every level.

It reuses two spells every 3.3.5 client already has and nothing in the game uses: "Test Maul"
(24042), a leftover Blizzard test copy of Maul, becomes the button, and "Pulverize" (742), an
unused NPC aura with Pulverize's icon, becomes the crit buff. No NPC or player spell changes.

## Install

Clone it into your AzerothCore `modules` folder **as `mod-forever-druid`**, without the repo's
`wow-` prefix. AzerothCore finds the module's entry point from the folder name.

```bash
cd <azerothcore>/modules
git clone https://github.com/buildthehomelab/wow-mod-forever-druid.git mod-forever-druid
```

Rebuild the worldserver, then copy `conf/mod_forever_druid.conf.dist` to your config folder as
`mod_forever_druid.conf`. The database changes (two `spell_proc` rows and spell script bindings)
are added to the world database on the next start.

## Settings

| Setting | Default | What it does |
|---------|---------|--------------|
| `ForeverDruid.Swipe.Enable` | `1` | Change Swipe (Bear)'s rage cost. `0` puts it back to 20. |
| `ForeverDruid.Swipe.RageCost` | `0` | Swipe (Bear)'s rage cost when enabled. |
| `ForeverDruid.Swipe.LearnWithBearForm` | `1` | Learn Swipe (Bear) rank 1 with Bear Form. |
| `ForeverDruid.BearDodgeRage.Enable` | `1` | Give rage for dodging in Bear Form and Dire Bear Form. |
| `ForeverDruid.BearDodgeRage.Amount` | `5` | Rage per dodge. |
| `ForeverDruid.FrenziedRegeneration.Enable` | `1` | Change Frenzied Regeneration's rate. `0` puts it back to 0.3%. |
| `ForeverDruid.FrenziedRegeneration.HealthPercentPerRage` | `1.0` | Share of max health per point of rage, in steps of 0.1. |
| `ForeverDruid.Pulverize.Enable` | `1` | Teach Pulverize. `0` takes it away at the next login. |
| `ForeverDruid.Pulverize.Level` | `42` | Level at which druids learn it. |
| `ForeverDruid.Pulverize.RageCost` | `15` | Rage cost. |
| `ForeverDruid.Pulverize.WeaponDamagePercent` | `60` | Hit damage as a percentage of weapon damage. |
| `ForeverDruid.Pulverize.AttackPowerPerStack` | `0.04` | Extra damage per Lacerate stack, as a share of attack power. |
| `ForeverDruid.Pulverize.CritPerStack` | `2` | Crit % per Lacerate stack for 10 seconds. `0` for no buff. |

All of them take effect on `.reload config` (a new Pulverize level applies at each druid's next
login or level-up). Rage costs, the Frenzied Regeneration rate and the Pulverize numbers are also
in the client patch's tooltips: if you change them and use the patch, change the matching values
at the top of `tools/patch-forever-druid-dbc.sh` and rebuild the patch.

## Optional client patch

The client reads Swipe's rage cost from its own Spell.dbc. Without the patch it still thinks
Swipe costs 20 rage: the tooltip says 20 Rage, and the client won't let you press Swipe with less
than 20 rage ("Not enough rage"). With 20 or more, Swipe works and costs nothing.

Frenzied Regeneration heals the new amount without the patch, but its tooltip still says 0.3%.
Pulverize works without the patch, but the client calls it "Test Maul" (Rank 4, Maul's icon and
an old tooltip), thinks it costs 30 rage (so it won't let you press it with less), puts it in the
General tab, and doesn't show the crit buff on the buff bar.

`tools/patch-forever-druid-dbc.sh` changes two client files:

- **Spell.dbc:** the rage cost of all 8 Swipe (Bear) ranks, Frenzied Regeneration's rate, and
  Test Maul (24042) becomes Pulverize (name, `ability_smash` icon, cost, global cooldown,
  tooltip). The unused aura 742 becomes a visible 10 second buff with its own tooltip.
- **SkillLineAbility.dbc:** adds Pulverize to the Feral Combat tab of the spellbook.

```bash
tools/patch-forever-druid-dbc.sh <Spell.dbc> <SkillLineAbility.dbc> DBFilesClient
```

Only the newest client patch's copy of each file is used, so if another patch already ships them
(for example with mod-profession-craft-cd, mod-hearthstone-cd and mod-forever-paladin changes),
give the script those files and put the results back in that same patch. Take
SkillLineAbility.dbc from the AzerothCore data folder (`dbc/SkillLineAbility.dbc`) if no patch
ships one yet. The script can be run again on its own output.

Pack both files into the MPQ as `DBFilesClient\Spell.dbc` and `DBFilesClient\SkillLineAbility.dbc`.
Players who get the new patch should delete their `Cache/` folder.

## Turning it off

- **Keep the module, switch things off:** set any of the `Enable` settings to `0`, then
  `.reload config` or restart. Druids lose Pulverize at their next login. If you shipped the client patch, players' tooltips keep the new
  values until you take the changes out of the patch.
- **Remove the module for good:** set `ForeverDruid.Pulverize.Enable = 0` first if you can, so
  druids lose it as they log in. Then stop the worldserver, delete the module, rebuild, and run
  both uninstall files:

  - `data/sql/uninstall/mod_forever_druid_uninstall_world.sql` on the world database removes the
    `spell_proc` rows and the script bindings, and puts Lacerate back at level 66. Druids who
    already trained it keep it.
  - `data/sql/uninstall/mod_forever_druid_uninstall_characters.sql` on the characters database
    takes Pulverize off every character and their action bars. Without it, druids who didn't log
    in keep the unscripted "Test Maul".

  Take the module's changes out of the client patch too.

AzerothCore never runs the `uninstall` folder by itself; it only runs the module's `db-world`
folder.

## Limits

- Playerbots druids learn Pulverize but don't press it. Their strategies would need a Pulverize
  action.
- Pulverize's damage numbers are this module's guesses for 3.3.5, not WoW Forever's (WoW Forever
  doesn't have Pulverize).
