#pragma once

// M64: the character-progression tables -- the data half of `levelup.s`.
//
// Three tables, all recovered this milestone, all hardcoded in the shipped
// binary rather than loaded from a data file:
//
//   1. **The race/sex base attributes** (`FUN_1001fc24`'s eight-arm switch).
//      Every character's starting Strength..Luck, chosen by the race the
//      player picked and whether they picked the male or the female
//      portrait. M10's header comment said "no character-creation stat-
//      rolling system exists ... so there was nothing to derive them from"
//      and used a flat 50 for all eight; this is the system it could not
//      find.
//
//   2. **The nine class rows** (`FUN_1001f9ac`), a 0x9c-byte object built
//      field by field in code. The one field anything outside character
//      creation reads is `+6`, which the `HasMagic` native returns
//      directly (Player dispatcher case 0xd reads
//      `classTable[class * 0x10 + 0xe]`, the same byte) and which
//      `UpdateAttributes` uses to force a non-caster's magicka pool to
//      zero.
//
//   3. **The experience curve** (`FUN_1004a104` / `FUN_1004a020`), a
//      per-class base multiplied by a shared 99-entry u16 table. That
//      table turns out to be the triangular numbers exactly -- entry `i`
//      is `(i+1)(i+2)/2`, all 99 of them, 1 through 4950 -- so it is
//      reproduced as the closed form rather than as 198 bytes of data.
//
// Nothing here needs a PlayerExecutable; the behaviour that consumes these
// tables is PlayerExecutable::UpdateAttributes() / AddExperience().

#include <cstdint>

namespace sk_bindings {

// The eight attributes, in the stats block's own field order (+0x14
// through +0x22). `levelup.s` offers exactly these eight buttons, in this
// order, one per five points.
struct BaseAttributes {
    int strength = 0;      // stats +0x14
    int intelligence = 0;  // stats +0x16
    int agility = 0;       // stats +0x18
    int willpower = 0;     // stats +0x1a
    int speed = 0;         // stats +0x1c
    int endurance = 0;     // stats +0x1e
    int personality = 0;   // stats +0x20
    int luck = 0;          // stats +0x22
};

// `chooseracemenu.s`'s combo box, in its own order -- its eight
// `AddOption()` calls carry the race name as a source comment, and string
// ids 1982..1989 are "Argonian".."Wood Elf", so the index the script hands
// `ChooseRace()` is this enum.
enum Race {
    kRaceArgonian = 0,
    kRaceBreton = 1,
    kRaceDarkElf = 2,
    kRaceHighElf = 3,
    kRaceKhajiit = 4,
    kRaceNord = 5,
    kRaceRedguard = 6,
    kRaceWoodElf = 7,
    kRaceCount = 8,
};

// `choosecharactermenu.s`'s nine options, string ids 32..40.
enum CharacterClass {
    kClassAssassin = 0,
    kClassBarbarian = 1,
    kClassBattlemage = 2,
    kClassKnight = 3,
    kClassNightblade = 4,
    kClassRogue = 5,
    kClassSpellsword = 6,
    kClassSorcerer = 7,
    kClassThief = 8,
    kClassCount = 9,
};

// FUN_1001fc24's race switch. `male` is the `SetSex(true)`/`SetSex(false)`
// the portrait screen writes -- true for the male portrait.
//
// Six of the eight races are a straight swap between the sexes and total
// 310 points either way. Two are not, and both are reproduced as found
// (verified in the disassembly, not just the decompiler's C): an
// **Argonian male totals 330** where the female totals 310, and a **High
// Elf totals 330 female / 320 male**. Nothing in the engine rebalances
// them afterwards.
//
// A race outside 0..7 is the switch's `default`, which writes no
// attributes at all -- the caller still recomputes and refills, so such a
// character keeps whatever the stats block's constructor left. Returns a
// zeroed struct for that case; UpdateAttributes checks the range itself.
BaseAttributes RaceBaseAttributes(int race, bool male);

// String-table id of the race name (`chooseracemenu.s`'s option ids), and
// of the class name (`choosecharactermenu.s`'s). -1 when out of range.
int RaceNameStringId(int race);
int CharacterClassNameStringId(int classId);

// One row of the nine-entry class table FUN_1001f9ac builds. The three
// u16s at +0, +2 and +4 are read by nothing this port has reached, so
// they are carried verbatim rather than named; `classId` is the +8 field
// the table is searched by, and `hasMagic` the +6 byte.
struct ClassRow {
    int classId = 0;
    uint16_t field0 = 0;
    uint16_t field2 = 0;
    uint16_t field4 = 0;
    bool hasMagic = false;
};

// The row whose `classId` matches, or null. The real lookup is a linear
// scan of all nine comparing that same field, which is why an unknown
// class id yields a null row (and, in UpdateAttributes, a null-row
// dereference the engine would take -- see its own note).
const ClassRow* FindClassRow(int classId);

// `HasMagic` (Player dispatcher case 0xd): the row's `+6` byte.
// Battlemage, Nightblade, Spellsword and Sorcerer; nobody else.
bool ClassHasMagic(int classId);

// FUN_1001fc24's *second* switch, the one that scales the per-level
// magicka bonus. Only three classes have an arm: Battlemage and Sorcerer
// both multiply by 0x26, Nightblade by 0x1a, everyone else by nothing.
//
// Note which class is missing: **Spellsword has magic and no growth arm**,
// so its magicka pool is its intelligence forever. Battlemage's and
// Sorcerer's arms are byte-for-byte different code paths that compute the
// identical value (the compiler folded `0x100 * 0x26` into one of them),
// so the two are not merely similar -- they are the same number.
// Returns 0 for a class with no arm.
int ClassMagickaGrowth(int classId);

// FUN_1004a104 / FUN_1004a020's per-class experience base:
//   Assassin, Spellsword   1100
//   Barbarian, Thief        900
//   Battlemage, Sorcerer   1200
//   Knight, Nightblade, Rogue  1000
// An out-of-range class also answers 1000 -- the real default arm stalls
// 10ms in `User::After` and leaves the pre-switch value alone.
int ClassExperienceBase(int classId);

// The total experience a character of this class and level must exceed to
// gain the next one: `base * level * (level + 1) / 2`, i.e. the class base
// times the level'th triangular number. Levels past 100 answer
// 999999999 -- the real code indexes a 99-entry table and substitutes that
// constant rather than reading off the end.
int ExperienceToLeaveLevel(int classId, int level);

// ---- M65: the `TestX` attribute rolls, Character-stats cases 0x0b..0x12 ----
//
// Eight bindings (TestStrength, TestIntelligence, TestAgility, TestWill,
// TestSpeed, TestEndurance, TestPersonality, TestLuck), one per attribute,
// reading the same eight fields the getters at 0x1b..0x23 read -- verified
// field by field against those getters, which matters because the *binding
// index* order here (Str, Int, Agi, Will, ...) is not the setter order
// (Str, Int, Will, Agi, ...); the fields agree even though the indices do
// not.
//
// All eight arms are the same four instructions:
//
//     span = attribute + difficulty          // the script's argument
//     roll = Math::Rand(engine->iSeed) % span
//     return !(attribute < roll)
//
// So the difficulty is not a threshold -- it is the number of *extra sides*
// added to a die whose face count is otherwise the attribute itself. The
// roll is uniform over [0, attribute + difficulty), and it passes when it
// lands at or below the attribute, which makes
//
//     P(pass) = (attribute + 1) / (attribute + difficulty)
//
// A check is therefore never certain and never impossible, however extreme
// the attribute: `TestStrength(25)` at strength 50 passes 68% of the time,
// and the hardest shipped check (`TestStrength(45)`) still passes 54% at
// strength 50. There is no critical success or failure, and no luck term --
// Luck has its own binding here but no shipped caller.
//
// `span == 0` is not reachable from the shipped corpus (every difficulty is
// 25..45 and attributes start at 30+), and is answered here with a zero
// roll, which is what the runtime's `__modsi3` divide-by-zero returns.
//
// Uses the same host `std::rand()` the rest of the port's rolls use rather
// than reproducing Symbian's `Math::Rand` LCG; the engine's own generator is
// seeded from the clock and its exact stream is not observable.
bool AttributeCheck(int attributeValue, int difficulty);

}  // namespace sk_bindings
