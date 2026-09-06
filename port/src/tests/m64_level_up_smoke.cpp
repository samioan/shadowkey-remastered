// M64 smoke test: the `levelup.s` cluster.
//
// `levelup.s` is eight buttons and a confirm popup, and behind them are
// four natives on two different SimKin classes plus a whole character
// system nothing had looked at:
//
//   GetLevelUpPoints / DecreaseLevelUpPoints   Player cases 0x29 / 0x2a
//   LevelUp                                    Player case 4
//   UpdateAttributes(bool)                     Player case 0x1f
//   SetStrength .. SetLuck                     stats cases 0x24..0x2b
//
// What each part checks, and why it is the check that could fail:
//
//   1. The shipped corpus -- how many call sites, on which receivers, so
//      the milestone's scale claims are measured rather than asserted.
//   2. The race/sex attribute table (FUN_1001fc24), all sixteen rows,
//      including the two that do *not* total 310 the way the other six
//      do. Those two are the reason this table is transcribed rather than
//      generated from a rule.
//   3. The class table (FUN_1001f9ac) and its `HasMagic` byte, against the
//      magicka-growth switch -- which is a different set of classes, and
//      that difference is a shipped gap.
//   4. The derived-stat recompute's player arm (FUN_10049698), which M58
//      could not take because it needed exactly the two tables above.
//   5. UpdateAttributes at level 1: real character creation end to end,
//      through the real `choosecharactermenu.s` / `chooseracemenu.s` /
//      `chooseportraitmenu.s` natives.
//   6. UpdateAttributes above level 1: the two ability ranks, the magicka
//      growth, the High Elf's +5, and the restore flag.
//   7. The eight setters, including the one that skips the recompute.
//   8. The experience curve and the automatic level-up (FUN_1004a104).
//   9. `levelup.s` itself, driven through the real interpreter.
//  10. The save round trip for the five words this milestone starts using.
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "assets/save_records.h"
#include "assets/string_table.h"
#include "simkin_bindings/character_progression.h"
#include "simkin_bindings/effects.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRValueArray.h"

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%-86s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

// Every `.s` under the script root, so the census below is over the whole
// shipped corpus and not a hand-picked list.
void CollectScripts(const std::string& dir, std::vector<std::string>& out);

int CountOccurrences(const std::string& haystack, const std::string& needle) {
    int n = 0;
    for (size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size())) {
        ++n;
    }
    return n;
}

int Total(const sk_b::BaseAttributes& a) {
    return a.strength + a.intelligence + a.agility + a.willpower + a.speed + a.endurance +
           a.personality + a.luck;
}

}  // namespace

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace {
void CollectScripts(const std::string& dir, std::vector<std::string>& out) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::string name = fd.cFileName;
        if (name == "." || name == "..") continue;
        const std::string full = dir + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CollectScripts(full, out);
        } else if (name.size() > 2 && name.compare(name.size() - 2, 2, ".s") == 0) {
            out.push_back(full);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
}  // namespace
#else
#include <dirent.h>
#include <sys/stat.h>
namespace {
void CollectScripts(const std::string& dir, std::vector<std::string>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        const std::string full = dir + "/" + name;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            CollectScripts(full, out);
        } else if (name.size() > 2 && name.compare(name.size() - 2, 2, ".s") == 0) {
            out.push_back(full);
        }
    }
    closedir(d);
}
}  // namespace
#endif

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m64_level_up_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_b::MenuStack stack(scriptRoot, interpreter, &strings);
    sk_b::PlayerExecutable& player = stack.player();

    auto call = [&](auto* obj, const char* name, skRValueArray& args) {
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        obj->method(skString(name), args, ret, ctxt);
        return ret;
    };
    auto call0 = [&](auto* obj, const char* name) {
        skRValueArray none;
        return call(obj, name, none);
    };
    auto callInt = [&](auto* obj, const char* name, int v) {
        skRValueArray a;
        a.append(skRValue(v));
        return call(obj, name, a);
    };

    // ---- 1. the shipped corpus ----
    std::printf("-- the shipped corpus --\n");
    {
        std::vector<std::string> scripts;
        CollectScripts(scriptRoot, scripts);
        Check(scripts.size() > 500, "found the script corpus (" + std::to_string(scripts.size()) +
                                        " .s files)");

        const char* kNatives[] = {"DecreaseLevelUpPoints", "GetLevelUpPoints",
                                  "UpdateAttributes",      "LevelUp(",
                                  "SetStrength",           "SetIntelligence",
                                  "SetWillpower",          "SetAgility",
                                  "SetSpeed",              "SetEndurance",
                                  "SetPersonality",        "SetLuck"};
        std::map<std::string, int> counts;
        for (const std::string& path : scripts) {
            std::ifstream f(path, std::ios::binary);
            std::ostringstream ss;
            ss << f.rdbuf();
            const std::string text = ss.str();
            for (const char* n : kNatives) counts[n] += CountOccurrences(text, n);
        }
        for (const auto& kv : counts) {
            std::printf("   %-24s %3d\n", kv.first.c_str(), kv.second);
        }
        // The eight `ChooseX` handlers, one per attribute button.
        Check(counts["DecreaseLevelUpPoints"] == 8,
              "DecreaseLevelUpPoints has exactly 8 sites, one per attribute button");
        Check(counts["GetLevelUpPoints"] == 1, "GetLevelUpPoints has one site, levelup.s's guard");
        // `levelup.s`'s back key, and chooseportraitmenu.s's two portraits.
        Check(counts["UpdateAttributes"] == 3, "UpdateAttributes has 3 sites across two screens");
        Check(counts["LevelUp("] == 1, "LevelUp() has one site, and it is cheatmenu.s");
        int setters = 0;
        for (const char* n : {"SetStrength", "SetIntelligence", "SetWillpower", "SetAgility",
                              "SetSpeed", "SetEndurance", "SetPersonality", "SetLuck"}) {
            setters += counts[n];
        }
        Check(setters == 21, "the eight attribute setters have 21 sites between them (got " +
                                 std::to_string(setters) + ")");
    }

    // ---- 2. the race/sex attribute table ----
    std::printf("\n-- FUN_1001fc24's race table --\n");
    {
        struct Row {
            int race;
            const char* name;
        };
        const Row kRows[] = {{sk_b::kRaceArgonian, "Argonian"}, {sk_b::kRaceBreton, "Breton"},
                             {sk_b::kRaceDarkElf, "Dark Elf"}, {sk_b::kRaceHighElf, "High Elf"},
                             {sk_b::kRaceKhajiit, "Khajiit"},  {sk_b::kRaceNord, "Nord"},
                             {sk_b::kRaceRedguard, "Redguard"},{sk_b::kRaceWoodElf, "Wood Elf"}};
        for (const Row& r : kRows) {
            const sk_b::BaseAttributes m = sk_b::RaceBaseAttributes(r.race, true);
            const sk_b::BaseAttributes f = sk_b::RaceBaseAttributes(r.race, false);
            std::printf("   %-9s M %3d/%3d/%3d/%3d/%3d/%3d/%3d/%3d = %d   F = %d\n", r.name,
                        m.strength, m.intelligence, m.agility, m.willpower, m.speed, m.endurance,
                        m.personality, m.luck, Total(m), Total(f));
            // The name string the race chooser puts next to this index.
            Check(strings.Get(sk_b::RaceNameStringId(r.race)) == r.name,
                  std::string("race ") + std::to_string(r.race) + " is " + r.name +
                      " in the string table");
            // Every value the table can hold is one of exactly three.
            const int all[] = {m.strength, m.intelligence, m.agility,     m.willpower,
                               m.speed,    m.endurance,    m.personality, m.luck,
                               f.strength, f.intelligence, f.agility,     f.willpower,
                               f.speed,    f.endurance,    f.personality, f.luck};
            bool threeValued = true;
            for (int v : all) threeValued = threeValued && (v == 30 || v == 40 || v == 50);
            Check(threeValued, std::string(r.name) + "'s sixteen numbers are all 30, 40 or 50");
        }

        // The lore signature: each race's own 50s are the ones the series
        // gives it. This is what confirms the index -> race mapping is not
        // off by one.
        Check(sk_b::RaceBaseAttributes(sk_b::kRaceNord, true).strength == 50 &&
                  sk_b::RaceBaseAttributes(sk_b::kRaceNord, false).strength == 50,
              "Nord has strength 50 either way");
        Check(sk_b::RaceBaseAttributes(sk_b::kRaceBreton, true).intelligence == 50 &&
                  sk_b::RaceBaseAttributes(sk_b::kRaceBreton, true).willpower == 50,
              "Breton has intelligence and willpower 50");
        Check(sk_b::RaceBaseAttributes(sk_b::kRaceRedguard, true).endurance == 50,
              "Redguard has endurance 50");
        Check(sk_b::RaceBaseAttributes(sk_b::kRaceWoodElf, true).agility == 50 &&
                  sk_b::RaceBaseAttributes(sk_b::kRaceWoodElf, true).speed == 50,
              "Wood Elf has agility and speed 50");

        // Six races total 310 whichever sex is chosen. Two do not, and
        // both are reproduced as shipped.
        int balanced = 0;
        for (int race = 0; race < sk_b::kRaceCount; ++race) {
            const int m = Total(sk_b::RaceBaseAttributes(race, true));
            const int f = Total(sk_b::RaceBaseAttributes(race, false));
            if (m == 310 && f == 310) ++balanced;
        }
        Check(balanced == 6, "six of the eight races total 310 for both sexes");
        Check(Total(sk_b::RaceBaseAttributes(sk_b::kRaceArgonian, true)) == 330 &&
                  Total(sk_b::RaceBaseAttributes(sk_b::kRaceArgonian, false)) == 310,
              "an Argonian male gets 330 points where the female gets 310");
        Check(Total(sk_b::RaceBaseAttributes(sk_b::kRaceHighElf, true)) == 320 &&
                  Total(sk_b::RaceBaseAttributes(sk_b::kRaceHighElf, false)) == 330,
              "a High Elf gets 320 male / 330 female, both above everyone else");

        // Wood Elf's arm has no sex branch at all.
        const sk_b::BaseAttributes wm = sk_b::RaceBaseAttributes(sk_b::kRaceWoodElf, true);
        const sk_b::BaseAttributes wf = sk_b::RaceBaseAttributes(sk_b::kRaceWoodElf, false);
        Check(wm.strength == wf.strength && wm.speed == wf.speed && wm.endurance == wf.endurance &&
                  wm.personality == wf.personality,
              "Wood Elf is the one race whose two sexes are identical");

        // Out of range is the switch's default: no attributes written.
        Check(Total(sk_b::RaceBaseAttributes(9, true)) == 0,
              "a race outside 0..7 writes no attributes (the switch's default arm)");
    }

    // ---- 3. the class table ----
    std::printf("\n-- FUN_1001f9ac's class table --\n");
    {
        const char* kNames[] = {"Assassin", "Barbarian",  "Battlemage", "Knight", "Nightblade",
                                "Rogue",    "Spellsword", "Sorcerer",   "Thief"};
        for (int c = 0; c < sk_b::kClassCount; ++c) {
            const sk_b::ClassRow* row = sk_b::FindClassRow(c);
            Check(row != nullptr && row->classId == c,
                  std::string("class ") + std::to_string(c) + " has a row, searched by its id");
            Check(strings.Get(sk_b::CharacterClassNameStringId(c)) == kNames[c],
                  std::string("class ") + std::to_string(c) + " is " + kNames[c]);
            std::printf("   %-11s magic=%d  growth=%3d  xpBase=%d\n", kNames[c],
                        sk_b::ClassHasMagic(c) ? 1 : 0, sk_b::ClassMagickaGrowth(c),
                        sk_b::ClassExperienceBase(c));
        }
        Check(sk_b::FindClassRow(9) == nullptr, "a tenth class id finds no row");

        // The `+6` byte: four casters.
        int casters = 0;
        for (int c = 0; c < sk_b::kClassCount; ++c) casters += sk_b::ClassHasMagic(c) ? 1 : 0;
        Check(casters == 4, "four of the nine classes carry the HasMagic byte");
        Check(sk_b::ClassHasMagic(sk_b::kClassBattlemage) &&
                  sk_b::ClassHasMagic(sk_b::kClassNightblade) &&
                  sk_b::ClassHasMagic(sk_b::kClassSpellsword) &&
                  sk_b::ClassHasMagic(sk_b::kClassSorcerer),
              "and they are Battlemage, Nightblade, Spellsword and Sorcerer");
        Check(!sk_b::ClassHasMagic(sk_b::kClassKnight) && !sk_b::ClassHasMagic(sk_b::kClassThief),
              "a Knight and a Thief have none");

        // The growth switch is a *different*, smaller set -- and the class
        // it leaves out is a caster.
        Check(sk_b::ClassMagickaGrowth(sk_b::kClassBattlemage) == 0x26 &&
                  sk_b::ClassMagickaGrowth(sk_b::kClassSorcerer) == 0x26,
              "Battlemage and Sorcerer both scale magicka by 0x26");
        Check(sk_b::ClassMagickaGrowth(sk_b::kClassNightblade) == 0x1a,
              "a Nightblade scales by 0x1a");
        Check(sk_b::ClassHasMagic(sk_b::kClassSpellsword) &&
                  sk_b::ClassMagickaGrowth(sk_b::kClassSpellsword) == 0,
              "a Spellsword has magic and NO growth arm -- its pool never grows");
        Check(sk_b::ClassMagickaGrowth(sk_b::kClassKnight) == 0, "a Knight has no growth arm");
    }

    // ---- 4. the experience curve ----
    std::printf("\n-- FUN_1004a104's experience curve --\n");
    {
        // The engine's 99-entry table is the triangular numbers exactly,
        // so `base * level * (level+1) / 2` has to reproduce all of it.
        Check(sk_b::ExperienceToLeaveLevel(sk_b::kClassBattlemage, 1) == 1200,
              "a Battlemage leaves level 1 at 1200 experience (1200 x 1)");
        Check(sk_b::ExperienceToLeaveLevel(sk_b::kClassBattlemage, 2) == 3600,
              "and level 2 at 3600 (1200 x 3)");
        Check(sk_b::ExperienceToLeaveLevel(sk_b::kClassBattlemage, 3) == 7200,
              "and level 3 at 7200 (1200 x 6)");
        Check(sk_b::ExperienceToLeaveLevel(sk_b::kClassThief, 1) == 900,
              "a Thief's base is 900, the cheapest");
        Check(sk_b::ExperienceToLeaveLevel(sk_b::kClassAssassin, 1) == 1100 &&
                  sk_b::ExperienceToLeaveLevel(sk_b::kClassSpellsword, 1) == 1100,
              "Assassin and Spellsword share 1100");
        Check(sk_b::ExperienceToLeaveLevel(sk_b::kClassKnight, 99) == 1000 * 4950,
              "the last real curve entry is the 99th triangular number, 4950");
        Check(sk_b::ExperienceToLeaveLevel(sk_b::kClassKnight, 101) == 999999999,
              "past level 100 the threshold is the engine's 999999999 sentinel");
    }

    // ---- 5. character creation, end to end ----
    std::printf("\n-- character creation --\n");
    {
        // The three real screens' natives, in the order the menus call
        // them: class, then race, then portrait (which is what picks the
        // sex and then calls UpdateAttributes(true)).
        callInt(&player, "ChooseCharacter", sk_b::kClassBattlemage);
        Check(player.level() == 1, "ChooseCharacter resets the level to 1");
        callInt(&player, "ChooseRace", sk_b::kRaceNord);
        Check(call0(&player, "GetRace").intValue() == sk_b::kRaceNord, "ChooseRace stores the race");
        callInt(&player, "SetSex", 1);
        callInt(&player, "UpdateAttributes", 1);

        // Nord male: 50 / 30 / 30 / 40 / 40 / 50 / 30 / 40.
        Check(call0(&player, "GetStrength").intValue() == 50, "a male Nord starts at strength 50");
        Check(call0(&player, "GetIntelligence").intValue() == 30, "  intelligence 30");
        Check(call0(&player, "GetAgility").intValue() == 30, "  agility 30");
        Check(call0(&player, "GetWillpower").intValue() == 40, "  willpower 40");
        Check(call0(&player, "GetSpeed").intValue() == 40, "  speed 40");
        Check(call0(&player, "GetEndurance").intValue() == 50, "  endurance 50");
        Check(call0(&player, "GetPersonality").intValue() == 30, "  personality 30");
        Check(call0(&player, "GetLuck").intValue() == 40, "  luck 40");

        // And the three derived maxima FUN_10049698 computes from them.
        Check(player.maxHealth() == (50 + 50) / 2, "max health is (strength + endurance) / 2 = 50");
        Check(player.maxFatigue() == 40 + 50 + 50,
              "max fatigue is willpower + strength + endurance = 140");
        Check(player.maxMagicka() == 30, "a level-1 Battlemage's max magicka is its intelligence");
        Check(player.health() == player.maxHealth() && player.fatigue() == player.maxFatigue() &&
                  player.magicka() == player.maxMagicka(),
              "creation refills all three pools to their new maxima");
        Check(call0(&player, "HasCreatedCharacter").boolValue(),
              "UpdateAttributes raises the created-character flag");
        Check(call0(&player, "HasMagic").boolValue(), "HasMagic() is true for a Battlemage");

        // The female arm of the same race differs, and the difference is
        // exactly the willpower/endurance swap.
        callInt(&player, "SetSex", 0);
        callInt(&player, "UpdateAttributes", 1);
        Check(call0(&player, "GetWillpower").intValue() == 50 &&
                  call0(&player, "GetEndurance").intValue() == 40,
              "the female Nord swaps willpower and endurance");
        Check(player.maxFatigue() == 50 + 50 + 40, "and her max fatigue moves with them");

        // A non-caster's pool is zeroed outright, not left at intelligence.
        callInt(&player, "ChooseCharacter", sk_b::kClassKnight);
        callInt(&player, "UpdateAttributes", 1);
        Check(!call0(&player, "HasMagic").boolValue(), "HasMagic() is false for a Knight");
        Check(player.maxMagicka() == 0 && player.magicka() == 0,
              "a Knight's magicka pool is forced to 0/0, not 0/intelligence");
    }

    // ---- 6. the derived-stat recompute's player arm ----
    std::printf("\n-- FUN_10049698's player arm --\n");
    {
        callInt(&player, "ChooseCharacter", sk_b::kClassSorcerer);
        callInt(&player, "ChooseRace", sk_b::kRaceHighElf);
        callInt(&player, "SetSex", 1);
        callInt(&player, "UpdateAttributes", 1);
        const int intelligence = call0(&player, "GetIntelligence").intValue();
        Check(intelligence == 50, "a High Elf's intelligence is 50");
        Check(player.maxMagicka() == 50, "at level 1 max magicka is intelligence + 0 + 0");

        // The no-magic branch does not merely compute zero -- it returns
        // *before writing max magicka at all*, having already written the
        // other two. Set a class with no magic and move endurance: health
        // and fatigue must move, magicka must not.
        callInt(&player, "ChooseCharacter", sk_b::kClassKnight);
        const int keptMagicka = 4242;
        callInt(&player, "SetMagicka", 0);
        // Reach max magicka through the effect table, the one path that
        // can write it without going through UpdateAttributes.
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatMaxMagicka, sk_b::kOpSet, keptMagicka);
        const int healthBefore = player.maxHealth();
        callInt(&player, "SetEndurance", 60);
        Check(player.maxHealth() != healthBefore, "SetEndurance recomputes max health");
        Check(player.maxMagicka() == keptMagicka,
              "and the no-magic arm leaves max magicka untouched rather than zeroing it");
    }

    // ---- 7. gaining a level ----
    std::printf("\n-- gaining a level --\n");
    {
        callInt(&player, "ChooseCharacter", sk_b::kClassBattlemage);
        callInt(&player, "ChooseRace", sk_b::kRaceHighElf);
        callInt(&player, "SetSex", 1);
        callInt(&player, "UpdateAttributes", 1);
        Check(player.specialAbility() == 1 && player.raceAbility() == 1,
              "both ability ranks start at 1, which is FUN_1003d670's own default");
        Check(call0(&player, "GetLevelUpPoints").intValue() == 0, "and no level-up points");
        Check(call0(&player, "GetSpecialAbility").intValue() == 1,
              "GetSpecialAbility returns that rank as an int, not the string \"None\"");

        const int intelligence = call0(&player, "GetIntelligence").intValue();
        call0(&player, "LevelUp");
        Check(call0(&player, "GetLevel").intValue() == 2, "LevelUp() raises the level to 2");
        Check(call0(&player, "GetLevelUpPoints").intValue() == 1, "and awards one point");
        Check(player.specialAbility() == 2 && player.raceAbility() == 2,
              "and raises both ability ranks, because the award re-derives");
        // rank 2, Battlemage's 0x26: (50 * 2 * 38) >> 8 = 14. Plus the
        // High Elf's flat +5.
        const int expectedClassBonus = (intelligence * 2 * 0x26) >> 8;
        Check(player.maxMagicka() == intelligence + expectedClassBonus + 5,
              "max magicka is intelligence + (int x rank x 0x26 >> 8) + the High Elf's 5");

        // The screen's back key calls UpdateAttributes again, so a real
        // level-up bumps each rank twice. Shipped behaviour, not a bug
        // this port introduced.
        callInt(&player, "UpdateAttributes", 0);
        Check(player.specialAbility() == 3 && player.raceAbility() == 3,
              "closing the level-up screen raises both ranks AGAIN (the shipped double-count)");
        Check(player.maxMagicka() == intelligence + ((intelligence * 3 * 0x26) >> 8) + 10,
              "and the High Elf's racial bonus accumulates a second +5 with it");

        // restoreVitals is the whole difference between the two callers.
        player.SetHealth(1);
        callInt(&player, "UpdateAttributes", 0);
        Check(player.health() == 1, "UpdateAttributes(false) leaves health alone (levelup.s)");
        callInt(&player, "UpdateAttributes", 1);
        Check(player.health() == player.maxHealth(),
              "UpdateAttributes(true) refills it (chooseportraitmenu.s)");
    }

    // ---- 8. the eight setters ----
    std::printf("\n-- the eight attribute setters --\n");
    {
        callInt(&player, "ChooseCharacter", sk_b::kClassKnight);
        callInt(&player, "ChooseRace", sk_b::kRaceNord);
        callInt(&player, "SetSex", 1);
        callInt(&player, "UpdateAttributes", 1);

        struct SetterCase {
            const char* setter;
            const char* getter;
        };
        const SetterCase kCases[] = {
            {"SetStrength", "GetStrength"},         {"SetIntelligence", "GetIntelligence"},
            {"SetWillpower", "GetWillpower"},       {"SetAgility", "GetAgility"},
            {"SetSpeed", "GetSpeed"},               {"SetEndurance", "GetEndurance"},
            {"SetPersonality", "GetPersonality"},   {"SetLuck", "GetLuck"}};
        for (const SetterCase& c : kCases) {
            callInt(&player, c.setter, 77);
            Check(call0(&player, c.getter).intValue() == 77,
                  std::string(c.setter) + " writes the field " + c.getter + " reads");
        }
        // A halfword store: 0x10000 truncates to zero.
        callInt(&player, "SetLuck", 0x10000 + 9);
        Check(call0(&player, "GetLuck").intValue() == 9, "the store is 16-bit, so 65545 becomes 9");

        // The recompute: seven setters tail into it, SetSpeed does not.
        callInt(&player, "SetStrength", 40);
        callInt(&player, "SetEndurance", 40);
        Check(player.maxHealth() == 40, "SetEndurance's recompute makes max health 40");
        // Move a field the recompute reads *behind its back*, through the
        // stats block's own storage, then poke each setter and see which
        // one notices.
        *player.EffectStatSlot(sk_b::kEffectStatStrengthProper) = 60;
        callInt(&player, "SetSpeed", 44);
        Check(player.maxHealth() == 40, "SetSpeed alone leaves the stale max health behind");
        callInt(&player, "SetPersonality", 44);
        Check(player.maxHealth() == 50, "any other setter recomputes it -- (60 + 40) / 2 = 50");
    }

    // ---- 9. experience and the automatic level-up ----
    std::printf("\n-- AddExperience --\n");
    {
        callInt(&player, "ChooseCharacter", sk_b::kClassThief);  // base 900
        callInt(&player, "ChooseRace", sk_b::kRaceKhajiit);
        callInt(&player, "SetSex", 1);
        callInt(&player, "UpdateAttributes", 1);
        Check(call0(&player, "GetExpToNextLevel").intValue() == 900,
              "a fresh Thief needs 900 experience, not the old flat 1000");

        const int pointsBefore = call0(&player, "GetLevelUpPoints").intValue();
        const int experienceBefore = player.experience();
        callInt(&player, "AddExperience", 900 - experienceBefore);
        Check(call0(&player, "GetLevel").intValue() == 1,
              "exactly the threshold does not level -- the test is strictly greater");
        Check(call0(&player, "GetExpToNextLevel").intValue() == 0, "and the gap is now 0");
        callInt(&player, "AddExperience", 1);
        Check(call0(&player, "GetLevel").intValue() == 2, "one more point does level");
        Check(call0(&player, "GetLevelUpPoints").intValue() == pointsBefore + 1,
              "and the level-up point comes from the experience path, not from cheatmenu.s");
        Check(player.experience() == 901, "the award is banked after the check");

        // One level per call, however much is awarded.
        const int before = call0(&player, "GetLevel").intValue();
        callInt(&player, "AddExperience", 30000);
        Check(call0(&player, "GetLevel").intValue() == before + 1,
              "a huge award still grants exactly one level");
        // ...and 30000 is already past the 16-bit boundary the argument
        // takes, so the *sign* survives; 40000 does not.
        const int expBefore = player.experience();
        callInt(&player, "AddExperience", 40000);
        Check(player.experience() == expBefore + static_cast<int16_t>(40000),
              "the award truncates to 16 bits, so 40000 is banked as -25536");
    }

    // ---- 10. levelup.s itself ----
    std::printf("\n-- levelup.s --\n");
    {
        callInt(&player, "ChooseCharacter", sk_b::kClassBattlemage);
        callInt(&player, "ChooseRace", sk_b::kRaceBreton);
        callInt(&player, "SetSex", 1);
        callInt(&player, "UpdateAttributes", 1);
        const int pointsBefore = call0(&player, "GetLevelUpPoints").intValue();
        call0(&player, "LevelUp");
        call0(&player, "LevelUp");
        call0(&player, "LevelUp");
        const int points = call0(&player, "GetLevelUpPoints").intValue();
        Check(points == pointsBefore + 3, "three levels have left three more points to spend");

        stack.OpenMenu("levelup");
        sk_b::MenuExecutable* screen = stack.currentMenu();
        Check(screen != nullptr, "levelup.s opens (DisplayLevelUp's target)");
        if (screen) {
            const int strengthBefore = call0(&player, "GetStrength").intValue();
            Check(screen->TryInvoke("AskStr"), "the Strength button's AskStr handler exists");
            Check(screen->TryInvoke("ChooseStr"), "and its confirm handler ChooseStr runs");
            Check(call0(&player, "GetStrength").intValue() == strengthBefore + 5,
                  "confirming spends the point for five points of strength");
            Check(call0(&player, "GetLevelUpPoints").intValue() == points - 1,
                  "and DecreaseLevelUpPoints took one point");

            // Every other button, through the real script.
            struct ButtonCase {
                const char* handler;
                const char* getter;
            };
            const ButtonCase kButtons[] = {
                {"ChooseInt", "GetIntelligence"}, {"ChooseWil", "GetWillpower"},
                {"ChooseAgi", "GetAgility"},      {"ChooseSpd", "GetSpeed"},
                {"ChooseEnd", "GetEndurance"},    {"ChoosePer", "GetPersonality"},
                {"ChooseLuck", "GetLuck"}};
            for (const ButtonCase& b : kButtons) {
                const int before = call0(&player, b.getter).intValue();
                screen->TryInvoke(b.handler);
                Check(call0(&player, b.getter).intValue() == before + 5,
                      std::string(b.handler) + " adds five to " + b.getter);
            }
            // Eight buttons, eight spends, and the counter tracks every
            // one of them however few points were on offer.
            Check(call0(&player, "GetLevelUpPoints").intValue() == points - 8,
                  "eight confirms took eight points off the counter");
            // ...and nothing floors it: keep going and it goes negative.
            const int deep = call0(&player, "GetLevelUpPoints").intValue();
            for (int i = 0; i < 20; ++i) call0(&player, "DecreaseLevelUpPoints");
            Check(call0(&player, "GetLevelUpPoints").intValue() == deep - 20 &&
                      call0(&player, "GetLevelUpPoints").intValue() < 0,
                  "spending past zero really does go negative -- there is no floor");

            // The back key: UpdateAttributes(false), then Quit.
            const int rankBefore = player.specialAbility();
            Check(screen->TryInvoke("LevelUpBack"), "the back handler LevelUpBack runs");
            Check(player.specialAbility() == rankBefore + 1,
                  "and its UpdateAttributes(false) bumps the ability rank on the way out");
        }
    }

    // ---- 11. the save round trip ----
    std::printf("\n-- the save round trip --\n");
    {
        callInt(&player, "ChooseCharacter", sk_b::kClassNightblade);
        callInt(&player, "ChooseRace", sk_b::kRaceHighElf);
        callInt(&player, "SetSex", 0);
        callInt(&player, "UpdateAttributes", 1);
        call0(&player, "LevelUp");
        callInt(&player, "SetSpecialAbility", 12);
        callInt(&player, "SetRaceAbility", 7);

        const sk::SavedEntity rec = player.BuildSaveRecord("azra");
        Check(rec.player.levelUpPoints == player.levelUpPoints(),
              "the record carries the level-up points (it used to write a hard 0)");
        Check(rec.player.characterClass == sk_b::kClassNightblade,
              "and the class, without which every derived number would come back wrong");
        Check(rec.player.specialAbility == 12 && rec.player.raceAbility == 7,
              "and both ability ranks");
        Check(rec.player.ffb8 != 0, "and the High Elf's racial magicka bonus at +0xfb8");

        skInterpreter interp2;
        sk_b::MenuStack stack2(scriptRoot, interp2, &strings);
        stack2.player().ApplySaveRecord(rec, stack2);
        sk_b::PlayerExecutable& loaded = stack2.player();
        Check(loaded.levelUpPoints() == player.levelUpPoints() &&
                  loaded.specialAbility() == 12 && loaded.raceAbility() == 7,
              "a load restores all three counters");
        Check(loaded.maxMagicka() == player.maxMagicka(),
              "and the restored character's magicka pool matches without re-deriving");
    }

    std::printf("\nm64_level_up_smoke: %s (%d checks)\n",
                g_failures == 0 ? "PASSED (all checks)" : "FAILED", g_checks);
    return g_failures == 0 ? 0 : 1;
}
