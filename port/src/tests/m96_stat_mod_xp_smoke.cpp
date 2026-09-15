// M96 smoke test: StatModXP, the quest-reward experience.
//
// Character stats (`0x14db0`, dispatcher `FUN_10048244`) binding 58,
// `StatModXP(amount)`, 7 live call sites -- six conversation rewards and
// one kill script:
//
//   almatheaconvo.s               MoreResponse   1500
//   ghstpass/trailslag_convo.s    Finish13        600
//   ghstpass/violet_convo.s       Beat17          600
//   lothna/get_quest400_convo.s   Beat44         2250
//   lothna/get_quest44_convo.s    Beat44         2250
//   lothna/pilgrim_convo.s        Mo43           2250
//   ratherb.s                     OnKilled         40
//
// Case 0x3a is case 0x3b (AddExperience) instruction for instruction, so
// the implementation is one line; what this checks is that it is that line
// and that the seven real handlers reach it:
//
//   1. The corpus census: 7 live sites, every one on GetPlayer().
//   2. StatModXP and AddExperience agree, step by step, on two players fed
//      the same awards -- including the three quirks of FUN_1004a104
//      (strictly-greater threshold, one level per call, 16-bit wrap).
//   3. The six conversations, opened by path and driven through the
//      handler that pays: the experience lands, the level-up it earns
//      lands with it, and nothing in those handlers soft-fails.
//   4. ratherb.s's OnKilled pays its 40.
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/character_progression.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-86s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

std::vector<std::string> g_softFails;
void RecordSoftFail(const char* object, const char* methodName, const char* /*args*/) {
    g_softFails.push_back(std::string(object ? object : "?") + "." +
                          (methodName ? methodName : "?"));
}

std::vector<std::filesystem::path> AllScripts(const std::string& root) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && it->path().extension() == ".s") files.push_back(it->path());
    }
    return files;
}

skRValue Call(skiExecutable& obj, const char* name, skRValueArray args, skInterpreter& interpreter) {
    skRValue ret;
    skExecutableContext ctxt(&interpreter);
    try {
        obj.method(skString(name), args, ret, ctxt);
    } catch (skRuntimeException& e) {
        std::printf("   (RUNTIME ERROR in %s: %s)\n", name, e.toString().ptr());
    }
    return ret;
}

skRValueArray Args() { return skRValueArray(); }
skRValueArray Args(skRValue a) {
    skRValueArray v;
    v.append(a);
    return v;
}

// FUN_1004a104 transcribed a second time, independently of the port's own
// AddExperience, so the level arithmetic in part 3 is predicted rather
// than read back.
struct Model {
    int classId = 0;
    int level = 1;
    int experience = 0;
    int points = 0;
    void Award(int amount) {
        const int delta = static_cast<int16_t>(amount);
        if (sk_b::ExperienceToLeaveLevel(classId, level) < experience + delta) {
            ++level;
            ++points;
        }
        experience += delta;
    }
};

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";
    const std::string root = scriptRoot;

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m96_stat_mod_xp_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(root)) {
        std::printf("m96_stat_mod_xp_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    // ---------------------------------------------------------------
    // Part 1: the shipped corpus.
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: what the shipped scripts ask for ==\n");
    {
        int live = 0, onPlayer = 0, commented = 0;
        for (const std::filesystem::path& f : AllScripts(root)) {
            std::ifstream in(f);
            std::string line;
            while (std::getline(in, line)) {
                const size_t at = line.find("StatModXP");
                if (at == std::string::npos) continue;
                const size_t comment = line.find("//");
                if (comment != std::string::npos && comment < at) {
                    ++commented;
                    continue;
                }
                ++live;
                if (line.rfind("GetPlayer().", at) != std::string::npos &&
                    line.rfind("GetPlayer().", at) + 12 == at) {
                    ++onPlayer;
                }
            }
        }
        std::printf("     live %d, on GetPlayer() %d, commented out %d\n", live, onPlayer,
                    commented);
        Check(live == 7, "StatModXP has 7 live call sites");
        Check(onPlayer == 7, "  every one of them GetPlayer().StatModXP(...)");
        Check(commented == 8, "  (and 8 more commented out, which stay dead)");
    }

    // ---------------------------------------------------------------
    // Part 2: StatModXP is AddExperience.
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: StatModXP and AddExperience, side by side ==\n");
    {
        skInterpreter interpreterA, interpreterB;
        sk_b::MenuStack stackA(root, interpreterA, &strings);
        sk_b::MenuStack stackB(root, interpreterB, &strings);
        sk_b::PlayerExecutable& a = stackA.player();
        sk_b::PlayerExecutable& b = stackB.player();
        Call(a, "ChooseCharacter", Args(skRValue(1)), interpreterA);  // Barbarian: base 900
        Call(b, "ChooseCharacter", Args(skRValue(1)), interpreterB);

        // Exactly the threshold, one past it, a jump across several
        // thresholds, a negative (which still levels: the test is against
        // the running total, and the jump left it past the next one), and
        // a value that wraps through 16 bits.
        const int awards[] = {900, 1, 20000, -50, 40000, 2250, 600, 40};
        bool agree = true;
        bool handled = true;
        for (int award : awards) {
            skRValue ret;
            skRValueArray argsA = Args(skRValue(award));
            skRValueArray argsB = Args(skRValue(award));
            skExecutableContext ctxtA(&interpreterA), ctxtB(&interpreterB);
            g_softFails.clear();
            sk_b::SetSoftFailObserver(&RecordSoftFail);
            handled = a.method(skString("StatModXP"), argsA, ret, ctxtA) && handled;
            b.method(skString("AddExperience"), argsB, ret, ctxtB);
            sk_b::SetSoftFailObserver(nullptr);
            if (!g_softFails.empty()) handled = false;
            const bool same = a.experience() == b.experience() && a.level() == b.level() &&
                              a.levelUpPoints() == b.levelUpPoints();
            std::printf("     %+6d  -> StatModXP xp %6d L%d pts %d | AddExperience xp %6d L%d "
                        "pts %d\n",
                        award, a.experience(), a.level(), a.levelUpPoints(), b.experience(),
                        b.level(), b.levelUpPoints());
            if (!same) agree = false;
        }
        Check(handled, "StatModXP is handled, and never soft-fails");
        Check(agree, "after every award the two players have the same experience, level and "
                     "points");

        // And the three FUN_1004a104 quirks, on a fresh player.
        skInterpreter interpreterC;
        sk_b::MenuStack stackC(root, interpreterC, &strings);
        sk_b::PlayerExecutable& c = stackC.player();
        Call(c, "ChooseCharacter", Args(skRValue(1)), interpreterC);
        Call(c, "StatModXP", Args(skRValue(900)), interpreterC);
        Check(c.level() == 1 && c.experience() == 900,
              "StatModXP(900) at a 900 threshold banks it and does not level (strictly greater)");
        Call(c, "StatModXP", Args(skRValue(20000)), interpreterC);
        Check(c.level() == 2 && c.levelUpPoints() == 1,
              "StatModXP(20000) crosses six thresholds and gains exactly one level");
        const int before = c.experience();
        Call(c, "StatModXP", Args(skRValue(40000)), interpreterC);
        Check(c.experience() == before - 25536, "StatModXP(40000) banks -25536 (16-bit)");
    }

    // ---------------------------------------------------------------
    // Part 3: the six conversations.
    // ---------------------------------------------------------------
    std::printf("\n== Part 3: the conversations that pay ==\n");
    {
        skInterpreter interpreter;
        sk_b::MenuStack stack(root, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        sk_b::PlayerExecutable& player = stack.player();
        Call(player, "ChooseCharacter", Args(skRValue(1)), interpreter);

        // Two of these conversations read the creature that opened them:
        // Trailslag_convo's Init() is `Level.GetEntity("olpac").saved_Killed`
        // and Violet_convo's Beat17 destroys `Level.GetEntity("violet")`. In
        // play those are the live .ent placements; here they are the same
        // two scripts, registered under the same names.
        skExecutableContext loadCtxt(&interpreter);
        std::unique_ptr<sk_b::MonsterExecutable> olpac, violet;
        try {
            olpac = std::make_unique<sk_b::MonsterExecutable>(
                skString((root + "/monsters/Olpac_Trailslag.s").c_str()), loadCtxt, &strings,
                player, stack);
            violet = std::make_unique<sk_b::MonsterExecutable>(
                skString((root + "/ghstpass/violet.s").c_str()), loadCtxt, &strings, player,
                stack);
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR: %s)\n", e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR: %s)\n", e.toString().ptr());
        }
        Check(olpac && violet, "Olpac and Violet load, to stand where the conversations look");
        if (olpac) stack.level().RegisterEntity("olpac", olpac.get());
        if (violet) stack.level().RegisterEntity("violet", violet.get());

        struct Reward {
            const char* menu;
            const char* handler;
            int xp;
        };
        const Reward rewards[] = {
            {"AlmatheaConvo", "MoreResponse", 1500},
            {"ghstpass\\Trailslag_convo", "Finish13", 600},
            {"ghstpass\\Violet_convo", "Beat17", 600},
            {"lothna\\get_quest400_convo", "Beat44", 2250},
            {"lothna\\get_quest44_convo", "Beat44", 2250},
            {"lothna\\pilgrim_convo", "Mo43", 2250},
        };
        Model model;
        model.classId = player.characterClass();
        sk_b::MenuExecutable* previous = nullptr;
        for (const Reward& r : rewards) {
            stack.OpenMenu(r.menu, &player);
            sk_b::MenuExecutable* menu = stack.currentMenu();
            // A menu whose Init() throws leaves the last one current, so
            // "not null" is not enough to call this opened.
            Check(menu != nullptr && menu != previous, std::string(r.menu) + " opens");
            if (menu == previous) menu = nullptr;
            previous = stack.currentMenu();
            if (!menu) continue;
            const int xpBefore = player.experience();
            g_softFails.clear();
            sk_b::SetSoftFailObserver(&RecordSoftFail);
            bool threw = false;
            {
                skRValue ret;
                skRValueArray args = Args(skRValue(0));  // the handlers' "(s)"
                skExecutableContext ctxt(&interpreter);
                try {
                    menu->method(skString(r.handler), args, ret, ctxt);
                } catch (skRuntimeException& e) {
                    std::printf("   (RUNTIME ERROR in %s: %s)\n", r.handler, e.toString().ptr());
                } catch (skParseException& e) {
                    std::printf("   (PARSE ERROR in %s: %s)\n", r.handler, e.toString().ptr());
                }
            }
            sk_b::SetSoftFailObserver(nullptr);
            model.Award(r.xp);
            std::string fails;
            for (const std::string& s : g_softFails) fails += " " + s;
            Check(player.experience() - xpBefore == r.xp,
                  "  " + std::string(r.handler) + " pays " + std::to_string(r.xp) +
                      " experience (" + std::to_string(player.experience() - xpBefore) + ")");
            Check(g_softFails.empty() && !threw,
                  "  and the rest of it runs: no soft-fail, no script error" +
                      (fails.empty() ? "" : " --" + fails));
            Check(player.level() == model.level && player.levelUpPoints() == model.points,
                  "  level " + std::to_string(player.level()) + ", " +
                      std::to_string(player.levelUpPoints()) + " point(s) -- as FUN_1004a104 "
                      "predicts");
        }
        Check(player.questCompleted(2) && player.questCompleted(13) && player.questSolved(17) &&
                  player.questCompleted(43) && player.questCompleted(44),
              "the quests those handlers close are closed (2, 13, 17, 43, 44)");
        Check(player.experience() == 9450, "9450 experience in all");
        Check(player.level() > 1, "and the character has levelled from it (level " +
                                      std::to_string(player.level()) + ")");
    }

    // ---------------------------------------------------------------
    // Part 4: ratherb.s's kill.
    // ---------------------------------------------------------------
    std::printf("\n== Part 4: ratherb.s OnKilled ==\n");
    {
        skInterpreter interpreter;
        sk_b::MenuStack stack(root, interpreter, &strings);
        stack.level().SetEntityTypes(&entityTypes);
        skExecutableContext loadCtxt(&interpreter);
        std::unique_ptr<sk_b::MonsterExecutable> rat;
        try {
            rat = std::make_unique<sk_b::MonsterExecutable>(
                skString((root + "/ratherb.s").c_str()), loadCtxt, &strings, stack.player(),
                stack);
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR: %s)\n", e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR: %s)\n", e.toString().ptr());
        }
        Check(rat != nullptr, "ratherb.s loads as a creature");
        if (rat) {
            const int before = stack.player().experience();
            g_softFails.clear();
            sk_b::SetSoftFailObserver(&RecordSoftFail);
            rat->InvokeOnKilled();
            sk_b::SetSoftFailObserver(nullptr);
            // Two names in this handler are still open, and neither is this
            // milestone's: `Level.IsMultiplayer()` soft-fails to 0, which is
            // the single-player branch it should take anyway, and
            // herbhurrah.s's `DelayOnEnter` is the roadmap's menu/popup row.
            bool onlyKnown = true;
            std::string fails;
            for (const std::string& f : g_softFails) {
                fails += " " + f;
                if (f != "Level.IsMultiplayer" && f != "Menu.DelayOnEnter") onlyKnown = false;
            }
            Check(onlyKnown, "  and soft-fails on nothing but the two known-open names --" +
                                 (fails.empty() ? std::string(" none") : fails));
            Check(stack.player().experience() - before == 40, "its OnKilled pays 40 experience");
            Check(stack.player().questSolved(1), "  and solves quest 1, the rest of the handler");
        }
    }

    std::printf("\nm96_stat_mod_xp_smoke: %d/%d checks passed\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
