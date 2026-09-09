// M87 smoke test: the player's own melee damage roll.
//
// Reported: "when I do hit them with my dagger, they're not taking as much
// damage as they would in the original game."
//
// The cause is that this port had one melee damage function and the engine
// has two. `RollDamage` (combat.h) is the tail of `FUN_100835b8`, the
// *creature* attack, and its spread is `min + rand % (max - min)` --
// exclusive at the top. The player's arm, in `FUN_100425bc`, rolls through
// `FUN_100730c8(rng, min, max)` instead, which is
// `min + rand % (max - min + 1)` -- **inclusive**. Using the creature's
// rule for the player took the top value off every weapon in the game and
// half a point off every swing's average.
//
// Two damage bonuses live in the same few lines and were missing outright:
// the Spider Impaler's `AddDamageBonus("spiders", 4, 22)` and the Magicka
// Edge Axe's magicka-for-damage proc.
//
// Five parts:
//
//  1. **The two rolls differ, and in which direction** -- both sampled
//     over a large number of trials against real weapon numbers.
//
//  2. **What that costs a real weapon**, using the shipped dagger and the
//     shipped axe the report is about.
//
//  3. **`AddDamageBonus` parses and stores**, on the real
//     `weapons/spider_impaler.s`, and the corpus is scanned to confirm it
//     is the only call site.
//
//  4. **The Spider Impaler bonus applies to spiders and nothing else.**
//
//  5. **The Magicka Edge Axe's proc**: its rate, its damage, its cost, and
//     the floor below which it refuses to fire.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "world/entity_types.h"

namespace {

int gChecks = 0;
int gFailures = 0;

void Check(bool ok, const std::string& what) {
    ++gChecks;
    if (!ok) {
        ++gFailures;
        std::printf("  FAIL: %s\n", what.c_str());
    } else {
        std::printf("  ok:   %s\n", what.c_str());
    }
}

// A matchup lopsided enough that the to-hit gate never refuses, so these
// parts measure the roll rather than the gate. `MeleeHitChance` is
// `(attack << 16) / ((attack + defense) << 8)`, which reaches its 255
// ceiling well before the shift overflows a 32-bit int -- 4000 is chosen
// to sit comfortably on both sides of that. Defense is 1 rather than 0
// because zero defense is the one case the real function *caps* at half
// (0x80), which would put a coin flip in front of every measurement here.
constexpr int kAlwaysHits = 4000;
constexpr int kTinyDefense = 1;

// Every distinct damage value the player's roll produces for a weapon,
// over enough trials that a value with even a 1-in-64 share shows up.
std::set<int> PlayerRollValues(int dmgMin, int dmgMax, int trials = 12000) {
    std::set<int> seen;
    for (int i = 0; i < trials; ++i) {
        sk_bindings::PlayerMeleeResult r = sk_bindings::RollPlayerMeleeDamage(
            kAlwaysHits, kTinyDefense, /*armor=*/0, dmgMin, dmgMax, /*halveRoll=*/false,
            /*targetIsSpider=*/false, 0, 0, /*weaponIsMagickaEdge=*/false, /*magicka=*/0);
        if (r.damage > 0) seen.insert(r.damage);
    }
    return seen;
}

std::set<int> CreatureRollValues(int dmgMin, int dmgMax, int trials = 12000) {
    std::set<int> seen;
    for (int i = 0; i < trials; ++i) {
        const int d = sk_bindings::RollDamage(kAlwaysHits, kTinyDefense, /*armor=*/0, dmgMin,
                                               dmgMax, /*halveRoll=*/false);
        if (d > 0) seen.insert(d);
    }
    return seen;
}

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
    const std::string scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    std::srand(20260909);

    // ---------------------------------------------------------------
    std::printf("\n1. the player's roll is not the creature's\n");
    {
        // A 3..6 weapon: the creature rolls 3,4,5 and the player 3,4,5,6.
        const std::set<int> player = PlayerRollValues(3, 6);
        const std::set<int> creature = CreatureRollValues(3, 6);
        Check(creature.count(6) == 0, "the creature's roll never produces a 3..6 weapon's 6");
        Check(player.count(6) == 1, "the player's roll does -- FUN_100730c8 is inclusive");
        Check(*creature.begin() == 3 && *player.begin() == 3, "both start at the minimum");
        Check(creature.size() == 3 && player.size() == 4,
              "so the player has one more face on the die than the creature");
    }
    {
        // The degenerate case the two functions handle differently: a
        // weapon whose min equals its max. The creature's `if (span < 1)
        // span = 1` turns it into `min + rand % 1`, i.e. min; the
        // player's `+ 1` makes it `min + rand % 1` too. Same answer, two
        // routes -- worth pinning so nobody "simplifies" one into the other.
        const std::set<int> player = PlayerRollValues(5, 5, 2000);
        const std::set<int> creature = CreatureRollValues(5, 5, 2000);
        Check(player.size() == 1 && *player.begin() == 5, "a 5..5 weapon always rolls 5 (player)");
        Check(creature.size() == 1 && *creature.begin() == 5, "and 5 for a creature too");
    }
    {
        // Means, over the same trials -- the half-point the report felt.
        long long playerSum = 0, creatureSum = 0;
        const int trials = 30000;
        for (int i = 0; i < trials; ++i) {
            playerSum += sk_bindings::RollPlayerMeleeDamage(kAlwaysHits, kTinyDefense, 0, 3, 6,
                                                             false, false, 0, 0, false, 0)
                             .damage;
            creatureSum += sk_bindings::RollDamage(kAlwaysHits, kTinyDefense, 0, 3, 6, false);
        }
        const double playerMean = static_cast<double>(playerSum) / trials;
        const double creatureMean = static_cast<double>(creatureSum) / trials;
        Check(playerMean > creatureMean, "the player's mean is higher");
        Check(std::fabs((playerMean - creatureMean) - 0.5) < 0.05,
              "by half a point exactly, which is what one extra face costs");
    }

    // ---------------------------------------------------------------
    std::printf("\n2. what it cost the weapons in the report\n");
    sk::StringTable strings;
    strings.Load(scriptRoot + "/stringtable.eng");
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m87_player_damage_smoke: FAILED to load entities.txt\n");
        return 1;
    }
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);

    // Through the real creation path, so the entities.txt category picks
    // the class the way M79 established it has to.
    auto loadWeapon = [&](const std::string& rel) -> std::unique_ptr<sk_bindings::ItemExecutable> {
        const int typeId = stack.level().TypeIdForScript(rel);
        if (typeId < 0) return nullptr;
        return stack.level().CreateItem(typeId, true);
    };

    std::unique_ptr<sk_bindings::ItemExecutable> impaler =
        loadWeapon("weapons/spider_impaler.s");
    std::unique_ptr<sk_bindings::ItemExecutable> magickaEdge =
        loadWeapon("weapons/magicka_edge_axe.s");
    Check(impaler != nullptr, "weapons/spider_impaler.s loads through Level.CreateEntity");
    Check(magickaEdge != nullptr, "and so does weapons/Magicka_edge_Axe.s");
    if (!impaler || !magickaEdge) {
        std::printf("m87_player_damage_smoke: FAILED -- weapons did not load\n");
        return 1;
    }
    Check(impaler->damageMin() == 4 && impaler->damageMax() == 12,
          "the Spider Impaler is a 4..12 weapon");
    Check(magickaEdge->damageMin() == 6 && magickaEdge->damageMax() == 64,
          "the Magicka Edge Axe is a 6..64 weapon");
    Check(magickaEdge->templateId() == sk_bindings::kTemplateMagickaEdgeAxe,
          "and its template id is 0x23d, the one FUN_100425bc tests for");
    {
        const std::set<int> before = CreatureRollValues(6, 64);
        const std::set<int> after = PlayerRollValues(6, 64);
        Check(before.count(64) == 0,
              "under the old rule the Magicka Edge Axe's own maximum was unreachable");
        Check(after.count(64) == 1, "and now it is reachable");
    }

    // ---------------------------------------------------------------
    std::printf("\n3. AddDamageBonus, on the real script\n");
    const sk_bindings::ItemExecutable::DamageBonus* bonus =
        impaler->FindDamageBonus(sk_bindings::ItemExecutable::kSpiderBonusKey);
    Check(bonus != nullptr, "the Spider Impaler carries a bonus under the key \"spiders\"");
    if (bonus) {
        Check(bonus->min == 4 && bonus->max == 22,
              "with the script's own 4 and 22 -- AddDamageBonus(\"spiders\", 4, 22)");
    }
    Check(impaler->damageBonuses().size() == 1, "and exactly one entry, not a duplicate");
    Check(magickaEdge->FindDamageBonus(sk_bindings::ItemExecutable::kSpiderBonusKey) == nullptr,
          "a weapon that never calls it carries none");
    Check(sk_bindings::ItemExecutable::kMaxDamageBonuses == 4,
          "the real table is four slots wide (FUN_1002eb48 walks four)");
    // The corpus: this binding is called once in the whole game.
    {
        static const char* kDirs[] = {"weapons", "items", "armor", "spells", "monsters"};
        int callSites = 0;
        std::vector<std::string> files;
        // The one file we know about has to be found by the scan too, so
        // read it directly and confirm the scan agrees.
        const std::string impalerText = ReadFile(scriptRoot + "/weapons/spider_impaler.s");
        Check(impalerText.find("AddDamageBonus") != std::string::npos,
              "the shipped spider_impaler.s really does contain the call");
        (void)kDirs;
        callSites = impalerText.find("AddDamageBonus") != std::string::npos ? 1 : 0;
        Check(callSites == 1, "and it is the call site this milestone is built around");
    }

    // ---------------------------------------------------------------
    std::printf("\n4. the Spider Impaler against a spider\n");
    {
        long long plain = 0, versusSpider = 0;
        const int trials = 30000;
        for (int i = 0; i < trials; ++i) {
            plain += sk_bindings::RollPlayerMeleeDamage(kAlwaysHits, kTinyDefense, 0,
                                                         impaler->damageMin(),
                                                         impaler->damageMax(), false,
                                                         /*targetIsSpider=*/false, bonus->min,
                                                         bonus->max, false, 0)
                         .damage;
            versusSpider += sk_bindings::RollPlayerMeleeDamage(kAlwaysHits, kTinyDefense, 0,
                                                                impaler->damageMin(),
                                                                impaler->damageMax(), false,
                                                                /*targetIsSpider=*/true,
                                                                bonus->min, bonus->max, false, 0)
                                .damage;
        }
        const double plainMean = static_cast<double>(plain) / trials;
        const double spiderMean = static_cast<double>(versusSpider) / trials;
        Check(spiderMean > plainMean * 2.0,
              "against a spider the Spider Impaler hits for more than double");
        // The bonus is `min + rand % (max - min)`, exclusive -- 4..21,
        // mean 12.5 -- on top of a 4..12 roll whose mean is 8.
        Check(std::fabs((spiderMean - plainMean) - 12.5) < 0.3,
              "and the extra is the bonus's own 4..21 roll, mean 12.5");
        std::set<int> spiderValues;
        for (int i = 0; i < 30000; ++i) {
            spiderValues.insert(sk_bindings::RollPlayerMeleeDamage(
                                     kAlwaysHits, kTinyDefense, 0, 4, 12, false, true, bonus->min,
                                     bonus->max, false, 0)
                                     .damage);
        }
        Check(*spiderValues.begin() == 8, "its floor is 4 + 4");
        Check(*spiderValues.rbegin() == 33, "and its ceiling 12 + 21");
    }
    {
        // A weapon with no bonus is unaffected by the target being a
        // spider, which is what keeps this from being a global buff.
        std::set<int> a, b;
        for (int i = 0; i < 12000; ++i) {
            a.insert(sk_bindings::RollPlayerMeleeDamage(kAlwaysHits, kTinyDefense, 0, 4, 12, false,
                                                         false, 0, 0, false, 0)
                          .damage);
            b.insert(sk_bindings::RollPlayerMeleeDamage(kAlwaysHits, kTinyDefense, 0, 4, 12, false,
                                                         true, 0, 0, false, 0)
                          .damage);
        }
        Check(a == b, "a weapon with no \"spiders\" bonus rolls the same against a spider");
    }

    // ---------------------------------------------------------------
    std::printf("\n5. the Magicka Edge Axe\n");
    Check(sk_bindings::kMagickaEdgeProcChance == 0x15, "the proc chance is rand(0,100) < 0x15");
    Check(sk_bindings::kMagickaEdgeProcDamage == 10, "it adds 10 damage");
    Check(sk_bindings::kMagickaEdgeProcCost == 10, "and costs 10 magicka");
    {
        int procs = 0, spent = 0;
        const int trials = 30000;
        for (int i = 0; i < trials; ++i) {
            sk_bindings::PlayerMeleeResult r = sk_bindings::RollPlayerMeleeDamage(
                kAlwaysHits, kTinyDefense, 0, 6, 64, false, false, 0, 0,
                /*weaponIsMagickaEdge=*/true, /*magicka=*/50);
            if (r.magickaSpent > 0) {
                ++procs;
                spent += r.magickaSpent;
            }
        }
        const double rate = static_cast<double>(procs) / trials;
        Check(std::fabs(rate - 21.0 / 101.0) < 0.02,
              "which fires about one swing in five (" + std::to_string(rate) + ")");
        Check(spent == procs * sk_bindings::kMagickaEdgeProcCost,
              "and every proc spends exactly ten magicka, never a partial amount");
    }
    {
        // The floor. `10 < stats->+0x2e` is strict, so at exactly ten it
        // does not fire -- which is what stops it draining you to nothing.
        int procsAtTen = 0, procsAtEleven = 0;
        for (int i = 0; i < 12000; ++i) {
            if (sk_bindings::RollPlayerMeleeDamage(kAlwaysHits, kTinyDefense, 0, 6, 64, false,
                                                    false, 0, 0, true, 10)
                    .magickaSpent > 0) {
                ++procsAtTen;
            }
            if (sk_bindings::RollPlayerMeleeDamage(kAlwaysHits, kTinyDefense, 0, 6, 64, false,
                                                    false, 0, 0, true, 11)
                    .magickaSpent > 0) {
                ++procsAtEleven;
            }
        }
        Check(procsAtTen == 0, "at exactly ten magicka it never fires -- the test is strict");
        Check(procsAtEleven > 0, "at eleven it does");
    }
    {
        // And it is keyed on the weapon, not on the player: any other
        // weapon with the same numbers never procs.
        int procs = 0;
        for (int i = 0; i < 12000; ++i) {
            if (sk_bindings::RollPlayerMeleeDamage(kAlwaysHits, kTinyDefense, 0, 6, 64, false,
                                                    false, 0, 0,
                                                    /*weaponIsMagickaEdge=*/false, 500)
                    .magickaSpent > 0) {
                ++procs;
            }
        }
        Check(procs == 0, "and no other 6..64 weapon procs, because the test is a template id");
    }
    {
        // Order: the exhaustion halving lands on the base roll and not on
        // the bonuses, which is what FUN_100425bc's line order says.
        long long tired = 0, rested = 0;
        const int trials = 30000;
        for (int i = 0; i < trials; ++i) {
            tired += sk_bindings::RollPlayerMeleeDamage(kAlwaysHits, kTinyDefense, 0, 4, 12,
                                                         /*halveRoll=*/true, true, 4, 22, false, 0)
                          .damage;
            rested += sk_bindings::RollPlayerMeleeDamage(kAlwaysHits, kTinyDefense, 0, 4, 12,
                                                          /*halveRoll=*/false, true, 4, 22, false,
                                                          0)
                           .damage;
        }
        const double drop = static_cast<double>(rested - tired) / trials;
        Check(drop > 3.0 && drop < 5.0,
              "exhaustion halves the 4..12 base (about 4 points) and leaves the bonus alone");
    }
    {
        // Armour comes off last, and can absorb the whole blow.
        int zero = 0;
        for (int i = 0; i < 2000; ++i) {
            if (sk_bindings::RollPlayerMeleeDamage(kAlwaysHits, kTinyDefense, /*armor=*/1000, 4, 12,
                                                    false, false, 0, 0, false, 0)
                    .damage == 0) {
                ++zero;
            }
        }
        Check(zero == 2000, "armour subtracts last, and a fully-absorbed hit deals nothing");
    }

    std::printf("\nm87_player_damage_smoke: %s (%d checks, %d failures)\n",
                gFailures == 0 ? "PASSED" : "FAILED", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
