#!/usr/bin/env bash
#
# Write the module's spell changes into a 3.3.5a (12340) client's Spell.dbc and
# SkillLineAbility.dbc, for an optional client patch:
#
# - Swipe (Bear): the new rage cost. Without it the client still thinks Swipe costs 20 rage: it
#   shows that in the tooltip and won't let you press Swipe with less rage.
# - Frenzied Regeneration: the new rate, for the tooltip.
# - Pulverize: turns "Test Maul" (24042) into Pulverize (name, icon, cost, global cooldown,
#   tooltip) in the Feral Combat tab, and makes its crit buff (742) show on the buff bar.
#
# Usage: tools/patch-forever-druid-dbc.sh <Spell.dbc> <SkillLineAbility.dbc> [output dir]
#   <Spell.dbc>             3.3.5a Spell.dbc: the client's own, or one another module's script
#                           already patched (the files may be the outputs: they're read first)
#   <SkillLineAbility.dbc>  3.3.5a SkillLineAbility.dbc, e.g. the AzerothCore data dir's
#                           dbc/SkillLineAbility.dbc, or one another module's script patched
#   [output dir]            default: ./DBFilesClient (ready to pack into an MPQ)
#
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    sed -n '12,17p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
fi

python3 - "$1" "$2" "${3:-DBFilesClient}" <<'PY'
import os, struct, sys

spell_src, skill_src, out_dir = sys.argv[1:4]

# Must match src/ForeverDruid.cpp and the settings in conf/mod_forever_druid.conf.dist. If you
# change those settings, change these and run the script again.
SWIPE_BEAR_RANKS = [779, 780, 769, 9754, 9908, 26997, 48561, 48562]
SWIPE_RAGE_COST = 0                      # ForeverDruid.Swipe.RageCost

SPELL_FRENZIED_REGENERATION = 22842
HEALTH_PERCENT_PER_RAGE = 1.0            # ForeverDruid.FrenziedRegeneration.HealthPercentPerRage

SPELL_PULVERIZE = 24042                  # "Test Maul" in the stock client
SPELL_PULVERIZE_BUFF = 742               # "Pulverize", an unused NPC aura
PULVERIZE_RAGE_COST = 15                 # ForeverDruid.Pulverize.RageCost
PULVERIZE_WEAPON_DAMAGE_PERCENT = 60     # ForeverDruid.Pulverize.WeaponDamagePercent
PULVERIZE_ATTACK_POWER_PER_STACK = 0.04  # ForeverDruid.Pulverize.AttackPowerPerStack
PULVERIZE_CRIT_PER_STACK = 2             # ForeverDruid.Pulverize.CritPerStack

POWER_RAGE = 1
ICON_ABILITY_SMASH = 102                 # SpellIcon.dbc: Pulverize's icon
GLOBAL_COOLDOWN_CATEGORY = 133
GLOBAL_COOLDOWN_MS = 1500
SPELL_EFFECT_WEAPON_PERCENT_DAMAGE = 31
SPELL_AURA_MOD_WEAPON_CRIT_PERCENT = 52
SPELL_ATTR0_PASSIVE = 0x40
DURATION_10_SECONDS = 1                  # SpellDuration.dbc
SKILL_FERAL_COMBAT = 134                 # SkillLine.dbc: the Feral Combat spellbook tab
CLASS_MASK_DRUID = 1024

ap_percent = f"{PULVERIZE_ATTACK_POWER_PER_STACK * 100:g}"
PULVERIZE_DESCRIPTION = (
    "Deals $s1% weapon damage plus ${$AP*" + ap_percent + "/100} additional damage for each of your "
    "Lacerate applications on the target, and increases your melee critical strike chance by "
    f"{PULVERIZE_CRIT_PER_STACK}% for each Lacerate application consumed for 10 sec."
)
PULVERIZE_BUFF_TOOLTIP = (
    f"Melee critical strike chance increased by {PULVERIZE_CRIT_PER_STACK}% for each Lacerate "
    "application consumed."
)


def read_dbc(path, field_count):
    with open(path, "rb") as f:
        data = f.read()
    magic, records, fields, record_size, string_size = struct.unpack_from("<4s4I", data, 0)
    if magic != b"WDBC" or fields != field_count or record_size != field_count * 4:
        sys.exit(f"{path}: not a 3.3.5a file (magic={magic!r} fields={fields} recordSize={record_size})")
    rows = [list(struct.unpack_from(f"<{fields}I", data, 20 + i * record_size)) for i in range(records)]
    strings = bytearray(data[20 + records * record_size:])
    assert len(strings) == string_size
    return rows, strings


def write_dbc(path, rows, strings, field_count):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(struct.pack("<4s4I", b"WDBC", len(rows), field_count, field_count * 4, len(strings)))
        for row in rows:
            f.write(struct.pack(f"<{field_count}I", *row))
        f.write(strings)


def add_string(strings, text):
    """Offset of text in the string block, appending it if it isn't there yet."""
    encoded = text.encode("utf-8") + b"\0"
    at = strings.find(encoded)
    while at > 0 and strings[at - 1] != 0:  # must be a whole string, not the tail of another
        at = strings.find(encoded, at + 1)
    if at >= 0:
        return at
    at = len(strings)
    strings.extend(encoded)
    return at


def int32(value):
    return value & 0xFFFFFFFF


# --- Spell.dbc -------------------------------------------------------------------------------
SPELL_FIELDS = 234
ATTRIBUTES = 4                # m_attributes
RECOVERY_TIME = 29            # m_recoveryTime
PROC_FLAGS = 34               # m_procTypeMask
PROC_CHANCE = 35              # m_procChance
DURATION = 40                 # m_durationIndex
POWER_TYPE = 41               # m_powerType
MANA_COST = 42                # m_manaCost (rage is stored ten times over: 20 rage is 200)
EFFECT_0 = 71                 # m_effect[0]
EFFECT_BASE_POINTS_0 = 80     # m_effectBasePoints[0]: the tooltip shows it plus 1
EFFECT_BASE_POINTS_1 = 81     # m_effectBasePoints[1]
EFFECT_AURA_0 = 95            # m_effectAura[0]
EFFECT_TRIGGER_SPELL_0 = 116  # m_effectTriggerSpell[0]
ICON = 133                    # m_spellIconID
NAME_ENUS = 136               # m_name_lang[0]
RANK_ENUS = 153               # m_nameSubtext_lang[0]
DESCRIPTION_ENUS = 170        # m_description_lang[0]
TOOLTIP_ENUS = 187            # m_auraDescription_lang[0]
START_RECOVERY_CATEGORY = 205 # m_startRecoveryCategory
START_RECOVERY_TIME = 206     # m_startRecoveryTime

spell_rows, spell_strings = read_dbc(spell_src, SPELL_FIELDS)
by_id = {row[0]: row for row in spell_rows}


def spell(spell_id):
    row = by_id.get(spell_id)
    if row is None:
        sys.exit(f"{spell_src}: spell {spell_id} not found")
    return row


for spell_id in SWIPE_BEAR_RANKS:
    row = spell(spell_id)
    if row[POWER_TYPE] != POWER_RAGE:
        sys.exit(f"{spell_src}: spell {spell_id} isn't a rage spell; is this a 3.3.5a Spell.dbc?")
    row[MANA_COST] = SWIPE_RAGE_COST * 10
print(f"  Swipe (Bear), all {len(SWIPE_BEAR_RANKS)} ranks: {SWIPE_RAGE_COST} rage")

row = spell(SPELL_FRENZIED_REGENERATION)
tenths = max(0, round(HEALTH_PERCENT_PER_RAGE * 10))
row[EFFECT_BASE_POINTS_1] = int32(tenths - 1)
print(f"  Frenzied Regeneration: {tenths / 10:.1f}% of max health per rage")

row = spell(SPELL_PULVERIZE)
if row[POWER_TYPE] != POWER_RAGE:
    sys.exit(f"{spell_src}: spell {SPELL_PULVERIZE} isn't a rage spell; is this a 3.3.5a Spell.dbc?")
row[NAME_ENUS] = add_string(spell_strings, "Pulverize")
row[RANK_ENUS] = add_string(spell_strings, "")
row[ICON] = ICON_ABILITY_SMASH
row[MANA_COST] = PULVERIZE_RAGE_COST * 10
row[START_RECOVERY_CATEGORY] = GLOBAL_COOLDOWN_CATEGORY
row[START_RECOVERY_TIME] = GLOBAL_COOLDOWN_MS
row[EFFECT_0] = SPELL_EFFECT_WEAPON_PERCENT_DAMAGE
row[EFFECT_BASE_POINTS_0] = int32(PULVERIZE_WEAPON_DAMAGE_PERCENT - 1)
row[DESCRIPTION_ENUS] = add_string(spell_strings, PULVERIZE_DESCRIPTION)
print(f"  Spell {SPELL_PULVERIZE}: Test Maul is now Pulverize, {PULVERIZE_RAGE_COST} rage")

row = spell(SPELL_PULVERIZE_BUFF)
row[ATTRIBUTES] &= ~SPELL_ATTR0_PASSIVE
row[RECOVERY_TIME] = 0
row[PROC_FLAGS] = 0
row[PROC_CHANCE] = 0
row[DURATION] = DURATION_10_SECONDS
row[EFFECT_AURA_0] = SPELL_AURA_MOD_WEAPON_CRIT_PERCENT
row[EFFECT_TRIGGER_SPELL_0] = 0
row[DESCRIPTION_ENUS] = add_string(spell_strings, PULVERIZE_BUFF_TOOLTIP)
row[TOOLTIP_ENUS] = add_string(spell_strings, PULVERIZE_BUFF_TOOLTIP)
print(f"  Spell {SPELL_PULVERIZE_BUFF}: visible 10 sec crit buff")

# --- SkillLineAbility.dbc: put Pulverize in the Feral Combat tab ------------------------------
SKILL_FIELDS = 14
# ID, SkillLine, Spell, RaceMask, ClassMask, ExcludeRace, ExcludeClass, MinSkillLineRank,
# SupercededBySpell, AcquireMethod, TrivialSkillLineRankHigh, TrivialSkillLineRankLow,
# CharacterPoints[2]

skill_rows, skill_strings = read_dbc(skill_src, SKILL_FIELDS)
existing = [row for row in skill_rows if row[2] == SPELL_PULVERIZE]
if existing:
    for row in existing:
        row[1] = SKILL_FERAL_COMBAT
        row[4] = CLASS_MASK_DRUID
    print(f"  SkillLineAbility {existing[0][0]}: Pulverize already listed, set to the Feral Combat tab")
else:
    new_id = max(row[0] for row in skill_rows) + 1
    skill_rows.append([new_id, SKILL_FERAL_COMBAT, SPELL_PULVERIZE, 0, CLASS_MASK_DRUID, 0, 0, 1, 0, 0, 0, 0, 0, 0])
    print(f"  SkillLineAbility {new_id}: Pulverize added to the Feral Combat tab")

write_dbc(os.path.join(out_dir, "Spell.dbc"), spell_rows, spell_strings, SPELL_FIELDS)
write_dbc(os.path.join(out_dir, "SkillLineAbility.dbc"), skill_rows, skill_strings, SKILL_FIELDS)
print(f"Wrote {out_dir}/Spell.dbc and {out_dir}/SkillLineAbility.dbc")
PY

cat <<'EOF'

Next:
  1. Pack both files into a client patch MPQ as DBFilesClient\Spell.dbc and
     DBFilesClient\SkillLineAbility.dbc. Only the newest MPQ's copy of each is used, so start
     from the files your current patch already ships (profession, hearthstone, Holy Strike
     changes) and put the results back in that same patch.
  2. Players who get the new patch should delete their Cache/ folder.
EOF
