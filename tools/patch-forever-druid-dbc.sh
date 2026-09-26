#!/usr/bin/env bash
#
# Write Swipe (Bear)'s new rage cost and Frenzied Regeneration's new rate into a 3.3.5a (12340)
# client's Spell.dbc, for an optional client patch. Without it the client still thinks Swipe costs
# 20 rage: it shows that in the tooltip and won't let you press Swipe with less rage. Frenzied
# Regeneration works either way; the patch only fixes its tooltip.
#
# Usage: tools/patch-forever-druid-dbc.sh <Spell.dbc> [output dir]
#   <Spell.dbc>     3.3.5a Spell.dbc: the client's own, or one another module's script already
#                   patched (it may be the output file: it's read first)
#   [output dir]    default: ./DBFilesClient (ready to pack into an MPQ)
#
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    sed -n '8,11p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
fi

python3 - "$1" "${2:-DBFilesClient}" <<'PY'
import os, struct, sys

spell_src, out_dir = sys.argv[1:3]

# Must match src/ForeverDruid.cpp and ForeverDruid.Swipe.RageCost and
# ForeverDruid.FrenziedRegeneration.HealthPercentPerRage in conf/mod_forever_druid.conf.dist. If
# you change those settings, change these and run the script again.
SWIPE_BEAR_RANKS = [779, 780, 769, 9754, 9908, 26997, 48561, 48562]
RAGE_COST = 0

SPELL_FRENZIED_REGENERATION = 22842
HEALTH_PERCENT_PER_RAGE = 1.0

POWER_RAGE = 1


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


SPELL_FIELDS = 234
POWER_TYPE = 41           # m_powerType
MANA_COST = 42            # m_manaCost (rage is stored ten times over: 20 rage is 200)
EFFECT_BASE_POINTS_1 = 81 # m_effectBasePoints[1]: the tooltip shows it plus 1, in tenths of a percent

spell_rows, spell_strings = read_dbc(spell_src, SPELL_FIELDS)
by_id = {row[0]: row for row in spell_rows}

for spell_id in SWIPE_BEAR_RANKS:
    row = by_id.get(spell_id)
    if row is None:
        sys.exit(f"{spell_src}: spell {spell_id} not found")
    if row[POWER_TYPE] != POWER_RAGE:
        sys.exit(f"{spell_src}: spell {spell_id} isn't a rage spell; is this a 3.3.5a Spell.dbc?")
    row[MANA_COST] = RAGE_COST * 10

print(f"  Swipe (Bear), all {len(SWIPE_BEAR_RANKS)} ranks: {RAGE_COST} rage")

row = by_id.get(SPELL_FRENZIED_REGENERATION)
if row is None:
    sys.exit(f"{spell_src}: spell {SPELL_FRENZIED_REGENERATION} not found")
tenths = max(0, round(HEALTH_PERCENT_PER_RAGE * 10))
row[EFFECT_BASE_POINTS_1] = (tenths - 1) & 0xFFFFFFFF
print(f"  Frenzied Regeneration: {tenths / 10:.1f}% of max health per rage")

write_dbc(os.path.join(out_dir, "Spell.dbc"), spell_rows, spell_strings, SPELL_FIELDS)
print(f"Wrote {out_dir}/Spell.dbc")
PY

cat <<'EOF'

Next:
  1. Pack it into a client patch MPQ as DBFilesClient\Spell.dbc. Only the newest MPQ's
     Spell.dbc is used, so start from the Spell.dbc your current patch already ships (profession,
     hearthstone, Holy Strike changes) and put the result back in that same patch.
  2. Players who get the new patch should delete their Cache/ folder.
EOF
