#include "simkin_bindings/character_progression.h"

#include <cstdlib>

namespace sk_bindings {
namespace {

// FUN_1001fc24's race switch, transcribed arm by arm. Each row is
// {strength, intelligence, agility, willpower, speed, endurance,
// personality, luck} -- the stats block's own field order, which is also
// the order the eight buttons appear in on `levelup.s`.
//
// The engine writes these one field at a time, in a different (and
// per-race different) order, with a derived-stat recompute wedged between
// most of them. The recompute reads only strength, endurance, willpower
// and intelligence and is idempotent, so the write order cannot be
// observed -- only the final values can, which is what this table holds.
struct RaceRow {
    BaseAttributes male;
    BaseAttributes female;
};

// clang-format off
const RaceRow kRaces[kRaceCount] = {
    // Argonian. The one race whose two sexes do not total the same:
    // the male's 330 against the female's 310.
    {{40, 40, 50, 50, 50, 30, 30, 40}, {40, 50, 40, 40, 40, 30, 30, 40}},
    // Breton -- intelligence and willpower 50 both ways.
    {{40, 50, 30, 50, 30, 30, 40, 40}, {30, 50, 30, 50, 40, 30, 40, 40}},
    // Dark Elf -- speed 50.
    {{40, 40, 40, 30, 50, 40, 30, 40}, {40, 40, 40, 30, 50, 30, 40, 40}},
    // High Elf -- intelligence 50, personality 50, and the only race with
    // a magicka bonus per level. Totals 320 male / 330 female, both above
    // everyone else's 310.
    {{30, 50, 40, 40, 30, 40, 50, 40}, {30, 50, 40, 40, 40, 40, 50, 40}},
    // Khajiit -- agility 50.
    {{40, 40, 50, 30, 40, 30, 40, 40}, {30, 40, 50, 30, 40, 40, 40, 40}},
    // Nord -- strength 50.
    {{50, 30, 30, 40, 40, 50, 30, 40}, {50, 30, 30, 50, 40, 40, 30, 40}},
    // Redguard -- endurance 50.
    {{50, 30, 40, 30, 40, 50, 30, 40}, {40, 30, 40, 30, 40, 50, 40, 40}},
    // Wood Elf -- agility and speed 50, and the only race whose arm has
    // no sex branch at all: the same eight numbers either way.
    {{30, 40, 50, 30, 50, 30, 40, 40}, {30, 40, 50, 30, 50, 30, 40, 40}},
};

// FUN_1001f9ac, field for field. The three unnamed u16s are the values it
// stores at record +0/+2/+4 before the class id and the magic flag; four
// of the nine carry a large one there (0xae8, 0x358, 0x20c, 0x36c) where
// the rest carry small ones, which reads like a starting-equipment id, but
// nothing this port has decompiled reads them, so they are not claimed.
const ClassRow kClasses[kClassCount] = {
    {kClassAssassin,   0x0002, 0x0002, 0x0002, false},
    {kClassBarbarian,  0x0006, 0x0002, 0x0004, false},
    {kClassBattlemage, 0x0002, 0x0002, 0x0018, true},
    {kClassKnight,     0x000e, 0x0002, 0x0004, false},
    {kClassNightblade, 0x0002, 0x0ae8, 0x0008, true},
    {kClassRogue,      0x000e, 0x0358, 0x0018, false},
    {kClassSpellsword, 0x0006, 0x0002, 0x0018, true},
    {kClassSorcerer,   0x0006, 0x020c, 0x0002, true},
    {kClassThief,      0x0002, 0x036c, 0x0008, false},
};
// clang-format on

bool InRaceRange(int race) { return race >= 0 && race < kRaceCount; }

}  // namespace

BaseAttributes RaceBaseAttributes(int race, bool male) {
    if (!InRaceRange(race)) return BaseAttributes{};
    const RaceRow& row = kRaces[race];
    return male ? row.male : row.female;
}

int RaceNameStringId(int race) { return InRaceRange(race) ? 1982 + race : -1; }

int CharacterClassNameStringId(int classId) {
    return (classId >= 0 && classId < kClassCount) ? 32 + classId : -1;
}

const ClassRow* FindClassRow(int classId) {
    // The linear scan the engine does, rather than an index -- the table's
    // rows happen to be in class order, but the lookup does not assume it.
    for (const ClassRow& row : kClasses) {
        if (row.classId == classId) return &row;
    }
    return nullptr;
}

bool ClassHasMagic(int classId) {
    const ClassRow* row = FindClassRow(classId);
    return row && row->hasMagic;
}

int ClassMagickaGrowth(int classId) {
    switch (classId) {
        case kClassBattlemage: return 0x26;
        case kClassNightblade: return 0x1a;
        case kClassSorcerer: return 0x26;
        default: return 0;
    }
}

int ClassExperienceBase(int classId) {
    switch (classId) {
        case kClassAssassin:
        case kClassSpellsword: return 1100;
        case kClassBarbarian:
        case kClassThief: return 900;
        case kClassBattlemage:
        case kClassSorcerer: return 1200;
        case kClassKnight:
        case kClassNightblade:
        case kClassRogue: return 1000;
        // The real default arm stalls 10ms and falls through with the
        // pre-switch 1000 still in the register.
        default: return 1000;
    }
}

int ExperienceToLeaveLevel(int classId, int level) {
    // `index = level - 1`, and the real guard is `if (index < 100)` on a
    // *signed* int -- a level of zero or less would index behind the
    // table. No path in the engine produces one (the stats block starts at
    // level 1 and only ever increments), so this clamps instead of
    // reproducing the read.
    const int index = level - 1;
    if (index >= 100) return 999999999;
    const int n = index < 0 ? 1 : index + 1;
    return ClassExperienceBase(classId) * (n * (n + 1) / 2);
}

bool AttributeCheck(int attributeValue, int difficulty) {
    const int span = attributeValue + difficulty;
    // `EUSER____modsi3(rand, span)`. Symbian's Math::Rand is documented to
    // return a non-negative TInt, so the roll cannot come back negative for
    // a positive span, and a *negative* span (only reachable if effects
    // drained the attribute far below the difficulty) still yields a
    // non-negative roll -- signed remainder takes the dividend's sign --
    // which the comparison below then fails, as the engine does.
    const int roll = span != 0 ? std::rand() % span : 0;
    return !(attributeValue < roll);
}

}  // namespace sk_bindings
