// M65 smoke test: the `TestX` attribute rolls, Character-stats cases
// 0x0b..0x12, and the `crypt1.s` gauntlet they exist for.
//
// The claims this milestone makes, and what checks each:
//
//   * **The roll is `attribute >= Math::Rand() % (attribute + difficulty)`.**
//     So the script's argument is not a threshold to beat -- it is the
//     number of extra sides bolted onto a die whose face count is otherwise
//     the attribute itself, and the pass chance is exactly
//     `(attribute + 1) / (attribute + difficulty)`. Part 2 measures that
//     against the closed form over the shipped difficulty ladder, and Part 3
//     pins the two exact consequences of the `+1` and the `>=`: a difficulty
//     of 1 or less can never be failed, and no attribute however large can
//     make a difficulty of 2 or more certain.
//
//   * **Which field each of the eight reads.** This is the one place a
//     transcription slip would be invisible in play and wrong forever, and
//     it is a real trap here: the binding-index order (Strength,
//     Intelligence, Agility, Will, ...) is *not* the setter order
//     (Strength, Intelligence, Will, Agility, ...), so indices 0x0d/0x0e sit
//     opposite 0x26/0x27. Part 4 drives all eight bindings against all eight
//     fields and requires an exact identity matrix -- deterministically, no
//     statistics, using the "difficulty 1 always passes" edge from Part 3.
//     It writes the fields through `EffectStatSlot`, so it also ties this
//     milestone's field map to the independently-derived one in effects.h.
//
//   * **It reads the live field, not a stored base.** Part 5 fortifies
//     strength through the effects system and watches the odds move.
//
//   * **The gauntlet works end to end.** Part 6 loads the real `crypt1.s`,
//     fires its real `EnterZone` handler, and checks both outcomes: a forced
//     failure opens `checkfail5.s` and costs health, a pass opens
//     `checkpass.s` and latches the zone's `saved_str5` flag so the check
//     never runs again.
//
//   * **The corpus really is what the roadmap said.** Part 1 counts every
//     call site in every shipped script.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/character_progression.h"
#include "simkin_bindings/effects.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/zone_script_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%-88s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

// The eight bindings, in binding-index order (0x0b..0x12), each paired with
// the effects.h index of the field it must read. Every part below walks
// this one table, so a disagreement anywhere shows up in all of them.
struct TestBinding {
    const char* name;
    int bindingIndex;
    int effectStat;
};
const TestBinding kTestBindings[] = {
    {"TestStrength", 0x0b, sk_b::kEffectStatStrengthProper},
    {"TestIntelligence", 0x0c, sk_b::kEffectStatIntelligence},
    {"TestAgility", 0x0d, sk_b::kEffectStatAgility},
    {"TestWill", 0x0e, sk_b::kEffectStatWill},
    {"TestSpeed", 0x0f, sk_b::kEffectStatSpeed},
    {"TestEndurance", 0x10, sk_b::kEffectStatEndurance},
    {"TestPersonality", 0x11, sk_b::kEffectStatPersonality},
    {"TestLuck", 0x12, sk_b::kEffectStatLuck},
};
constexpr int kTestBindingCount = 8;

// Invoke one of them on the player exactly as a script would.
bool InvokeTest(sk_b::PlayerExecutable& player, skInterpreter& interpreter, const char* name,
                int difficulty, bool* handled) {
    skRValueArray args;
    args.append(skRValue(difficulty));
    skRValue ret;
    skExecutableContext ctxt(&interpreter);
    const bool ok = player.method(skString(name), args, ret, ctxt);
    if (handled) *handled = ok;
    return ok && ret.boolValue();
}

// Fraction of `trials` calls to AttributeCheck that pass.
double PassRate(int attribute, int difficulty, int trials) {
    int passes = 0;
    for (int i = 0; i < trials; ++i) {
        if (sk_b::AttributeCheck(attribute, difficulty)) ++passes;
    }
    return static_cast<double>(passes) / trials;
}

// The closed form the roll is claimed to implement.
double ExpectedPassRate(int attribute, int difficulty) {
    return static_cast<double>(attribute + 1) / static_cast<double>(attribute + difficulty);
}

bool Near(double a, double b, double eps) { return std::fabs(a - b) < eps; }

// One `GetPlayer().TestX(n)` in a shipped script.
struct CallSite {
    std::string file;
    std::string binding;
    int difficulty = 0;
    bool onPlayer = false;
};

void CollectCallSites(const std::string& path, const std::string& label,
                      std::vector<CallSite>& out) {
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        for (const TestBinding& b : kTestBindings) {
            size_t at = line.find(b.name);
            if (at == std::string::npos) continue;
            size_t open = line.find('(', at);
            if (open == std::string::npos) continue;
            size_t close = line.find(')', open);
            if (close == std::string::npos) continue;
            CallSite site;
            site.file = label;
            site.binding = b.name;
            site.difficulty = std::atoi(line.substr(open + 1, close - open - 1).c_str());
            // The receiver, as written immediately before the method name.
            site.onPlayer = at >= 12 && line.compare(at - 12, 12, "GetPlayer().") == 0;
            out.push_back(site);
        }
    }
}

bool FileExists(const std::string& path) {
    std::ifstream in(path);
    return static_cast<bool>(in);
}

// Does the current menu carry this static-item text id? checkpass.s adds
// 601/602 and checkfail5.s adds 599/600, so this is how the two are told
// apart without a menu-name accessor.
bool MenuHasTextId(const sk_b::MenuExecutable* menu, int textId) {
    if (!menu) return false;
    for (const sk_b::MenuExecutable::MenuRow& row : menu->rows()) {
        if (row.textId == textId) return true;
    }
    return false;
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
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";
    const std::string root = scriptRoot;

    // Fixed seed: every statistical check below is a measured rate against a
    // closed form, so the run has to be reproducible or a tolerance is
    // meaningless.
    std::srand(20260906u);

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m65_attribute_check_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    // ---------------------------------------------------------------
    // Part 1: the corpus. Fourteen calls, one script, one receiver.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 1: every TestX call site in the shipped corpus ---\n");
    {
        std::vector<std::string> scripts;
        CollectScripts(root, scripts);
        Check(scripts.size() > 500, "the script corpus loaded (" + std::to_string(scripts.size()) +
                                        " .s files walked)");

        std::vector<CallSite> sites;
        for (const std::string& path : scripts) {
            std::string label = path;
            size_t slash = label.find_last_of("/\\");
            if (slash != std::string::npos) label = label.substr(slash + 1);
            CollectCallSites(path, label, sites);
        }

        Check(sites.size() == 14, "14 TestX call sites in the whole corpus (found " +
                                      std::to_string(sites.size()) + ")");

        bool allCrypt1 = true;
        bool allOnPlayer = true;
        for (const CallSite& s : sites) {
            if (s.file != "crypt1.s") allCrypt1 = false;
            if (!s.onPlayer) allOnPlayer = false;
        }
        Check(allCrypt1, "every one of them is in crypt1.s -- the whole feature is one dungeon");
        Check(allOnPlayer, "every receiver is GetPlayer(); no script ever rolls a creature's stats");

        std::map<std::string, int> perBinding;
        for (const CallSite& s : sites) ++perBinding[s.binding];
        Check(perBinding["TestStrength"] == 5, "TestStrength: 5 sites");
        Check(perBinding["TestAgility"] == 3, "TestAgility: 3 sites");
        Check(perBinding["TestEndurance"] == 3, "TestEndurance: 3 sites");
        Check(perBinding["TestSpeed"] == 3, "TestSpeed: 3 sites");
        // Four of the eight bindings exist and are never used. Worth
        // asserting rather than assuming: it is why the other four have no
        // shipped difficulty to check them against.
        Check(perBinding.count("TestIntelligence") == 0 && perBinding.count("TestWill") == 0 &&
                  perBinding.count("TestPersonality") == 0 && perBinding.count("TestLuck") == 0,
              "Intelligence, Will, Personality and Luck have bindings and no caller");

        std::map<int, int> perDifficulty;
        for (const CallSite& s : sites) ++perDifficulty[s.difficulty];
        Check(perDifficulty[25] == 4 && perDifficulty[30] == 4 && perDifficulty[35] == 4,
              "three tiers of four (25, 30, 35) -- one per attribute at each tier");
        Check(perDifficulty[40] == 1 && perDifficulty[45] == 1,
              "plus two strength-only checks at 40 and 45");
        Check(perDifficulty.size() == 5, "five distinct difficulties in the whole game");

        // The other half of the gauntlet: what passing and failing lead to.
        Check(FileExists(root + "/crypt1/checkpass.s"), "crypt1/checkpass.s exists (the reward)");
        Check(FileExists(root + "/crypt1/checkfail5.s") &&
                  FileExists(root + "/crypt1/checkfail6.s") &&
                  FileExists(root + "/crypt1/checkfail7to9.s"),
              "three fail menus exist, one per tier -- damage escalates 2-4, 4-8, 6-12");
    }

    // ---------------------------------------------------------------
    // Part 2: the roll matches its closed form.
    //
    // If the implementation were the obvious misreading -- "roll a d100 and
    // beat the difficulty", or "attribute must exceed the difficulty" --
    // none of these rates would land anywhere near the prediction, because
    // the prediction depends on the attribute being *inside* the die.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 2: P(pass) == (attribute + 1) / (attribute + difficulty) ---\n");
    {
        const int kTrials = 200000;
        const int kAttributes[] = {30, 40, 50, 70};
        const int kDifficulties[] = {25, 30, 35, 40, 45};
        bool allMatch = true;
        double worst = 0.0;
        for (int attribute : kAttributes) {
            for (int difficulty : kDifficulties) {
                const double measured = PassRate(attribute, difficulty, kTrials);
                const double expected = ExpectedPassRate(attribute, difficulty);
                worst = std::fmax(worst, std::fabs(measured - expected));
                if (!Near(measured, expected, 0.01)) allMatch = false;
            }
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "all 20 (attribute, difficulty) pairs match the closed form (worst error "
                      "%.4f)",
                      worst);
        Check(allMatch, buf);

        // The two ends of the shipped ladder, spelled out, so the numbers a
        // player actually meets are in the record rather than only a
        // tolerance.
        const double easiest = PassRate(50, 25, kTrials);
        const double hardest = PassRate(50, 45, kTrials);
        std::snprintf(buf, sizeof(buf),
                      "at strength 50 the easiest check (25) passes %.1f%% and the hardest (45) "
                      "%.1f%%",
                      easiest * 100.0, hardest * 100.0);
        Check(Near(easiest, 51.0 / 75.0, 0.01) && Near(hardest, 51.0 / 95.0, 0.01), buf);

        // Monotonic in both arguments -- a better character is never worse
        // off, and a harder check is never easier.
        bool attributeMonotonic = true;
        for (int attribute = 10; attribute < 90; attribute += 10) {
            if (PassRate(attribute, 35, 40000) >= PassRate(attribute + 10, 35, 40000)) {
                attributeMonotonic = false;
            }
        }
        Check(attributeMonotonic, "raising the attribute always raises the pass rate");

        bool difficultyMonotonic = true;
        double previous = 1.0;
        for (int difficulty : kDifficulties) {
            const double rate = PassRate(50, difficulty, 40000);
            if (rate >= previous) difficultyMonotonic = false;
            previous = rate;
        }
        Check(difficultyMonotonic,
              "the shipped ladder 25 < 30 < 35 < 40 < 45 really does get harder each step");
    }

    // ---------------------------------------------------------------
    // Part 3: the exact edges, which are what the `+1` and the `>=` mean.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 3: never certain, never impossible ---\n");
    {
        // P(fail) is (difficulty - 1) / (attribute + difficulty), so a
        // difficulty of 1 has no failing outcome at all and a difficulty of
        // 0 has a negative count of them. Both are guaranteed passes for
        // *any* attribute, and Part 4 leans on it.
        bool alwaysPasses = true;
        for (int attribute = 0; attribute <= 200; attribute += 7) {
            for (int trial = 0; trial < 200; ++trial) {
                if (!sk_b::AttributeCheck(attribute, 1)) alwaysPasses = false;
                if (!sk_b::AttributeCheck(attribute, 0)) alwaysPasses = false;
            }
        }
        Check(alwaysPasses, "difficulty 0 or 1 can never be failed, at any attribute");

        // ...and no attribute makes a real check certain. 24 of the 1045
        // outcomes still fail at strength 1000 against difficulty 45.
        const double godlike = PassRate(1000, 45, 400000);
        Check(godlike < 1.0 && godlike > 0.95,
              "even at attribute 1000 the hardest check still fails sometimes");

        // The other end: a zero attribute is not locked out. One outcome in
        // (0 + 45) passes.
        const double hopeless = PassRate(0, 45, 400000);
        Check(hopeless > 0.0 && Near(hopeless, 1.0 / 45.0, 0.005),
              "a zero attribute still passes ~1 time in 45, not never");

        // Two arithmetic edges reachable only through the effects system
        // draining an attribute below zero. The engine's signed remainder
        // keeps the roll non-negative either way, so a negative attribute
        // always fails -- which is also what Part 4 uses to force a
        // deterministic answer.
        bool negativeAlwaysFails = true;
        for (int trial = 0; trial < 2000; ++trial) {
            if (sk_b::AttributeCheck(-2, 1)) negativeAlwaysFails = false;   // span -1
            if (sk_b::AttributeCheck(-5, 25) ) negativeAlwaysFails = false;  // span 20
            if (sk_b::AttributeCheck(-100, 25)) negativeAlwaysFails = false; // span -75
        }
        Check(negativeAlwaysFails,
              "a negative attribute always fails, whichever sign the span ends up with");

        // span == 0 is unreachable from the corpus; this only pins which
        // way the port answers it.
        Check(sk_b::AttributeCheck(0, 0),
              "span 0 (attribute 0, difficulty 0) answers pass, matching a zero remainder");
    }

    // ---------------------------------------------------------------
    // Part 4: the field map, as an exact 8x8 identity.
    //
    // Deterministic by construction. Every field is set to 50 except the
    // one under test, which is set to -2; every binding is then called with
    // difficulty 1. Part 3 established that difficulty 1 always passes for
    // a non-negative attribute and always fails for a negative one, so
    // exactly one cell of each row must come back false -- and it must be
    // the diagonal.
    //
    // The fields are written through EffectStatSlot(), so this also asserts
    // that the offsets this milestone read out of cases 0x0b..0x12 are the
    // same offsets M58 read out of the effect applier.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 4: which field each of the eight bindings reads ---\n");
    {
        skInterpreter interpreter;
        sk_b::MenuStack stack(root, interpreter, &strings);
        sk_b::PlayerExecutable& player = stack.player();

        bool identity = true;
        bool allHandled = true;
        std::string offDiagonalFailures;
        for (int drained = 0; drained < kTestBindingCount; ++drained) {
            for (const TestBinding& field : kTestBindings) {
                int* slot = player.EffectStatSlot(field.effectStat);
                if (!slot) {
                    allHandled = false;
                    continue;
                }
                *slot = 50;
            }
            int* drainedSlot = player.EffectStatSlot(kTestBindings[drained].effectStat);
            if (drainedSlot) *drainedSlot = -2;

            for (int called = 0; called < kTestBindingCount; ++called) {
                bool handled = false;
                const bool passed =
                    InvokeTest(player, interpreter, kTestBindings[called].name, 1, &handled);
                if (!handled) allHandled = false;
                const bool expectPass = (called != drained);
                if (passed != expectPass) {
                    identity = false;
                    if (offDiagonalFailures.size() < 120) {
                        offDiagonalFailures += std::string(kTestBindings[called].name) + "/" +
                                               kTestBindings[drained].name + " ";
                    }
                }
            }
        }
        Check(allHandled, "all eight bindings are handled (none fell through to the soft-fail)");
        Check(identity, "the 8x8 binding-vs-field matrix is exactly the identity" +
                            (offDiagonalFailures.empty() ? std::string()
                                                         : " -- wrong: " + offDiagonalFailures));

        // The specific pair that the two orderings put opposite each other,
        // called out by name because it is the failure this whole part
        // exists to catch.
        for (int* slot : {player.EffectStatSlot(sk_b::kEffectStatAgility),
                          player.EffectStatSlot(sk_b::kEffectStatWill)}) {
            if (slot) *slot = 50;
        }
        *player.EffectStatSlot(sk_b::kEffectStatWill) = -2;
        Check(InvokeTest(player, interpreter, "TestAgility", 1, nullptr),
              "TestAgility (0x0d) is unaffected by willpower, despite 0x26 being SetWillpower");
        Check(!InvokeTest(player, interpreter, "TestWill", 1, nullptr),
              "TestWill (0x0e) reads willpower (+0x1a), not agility (+0x18)");
    }

    // ---------------------------------------------------------------
    // Part 5: the roll reads the live field, so effects move the odds.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 5: a fortified attribute rolls better ---\n");
    {
        skInterpreter interpreter;
        sk_b::MenuStack stack(root, interpreter, &strings);
        sk_b::PlayerExecutable& player = stack.player();

        *player.EffectStatSlot(sk_b::kEffectStatStrengthProper) = 30;
        int passesBefore = 0;
        for (int i = 0; i < 20000; ++i) {
            if (InvokeTest(player, interpreter, "TestStrength", 45, nullptr)) ++passesBefore;
        }
        const double before = passesBefore / 20000.0;

        // A Fortify Strength, applied the way an effect node would.
        sk_b::ApplyEffectStat(player, sk_b::kEffectStatStrengthProper, sk_b::kOpIncrement, 40);
        Check(player.EffectStatValue(sk_b::kEffectStatStrengthProper) == 70,
              "the effect moved strength 30 -> 70");

        int passesAfter = 0;
        for (int i = 0; i < 20000; ++i) {
            if (InvokeTest(player, interpreter, "TestStrength", 45, nullptr)) ++passesAfter;
        }
        const double after = passesAfter / 20000.0;

        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "TestStrength(45) went %.1f%% -> %.1f%%, matching 31/75 -> 71/115",
                      before * 100.0, after * 100.0);
        Check(Near(before, 31.0 / 75.0, 0.02) && Near(after, 71.0 / 115.0, 0.02), buf);
        Check(after > before, "fortifying the attribute really does improve the check");
    }

    // ---------------------------------------------------------------
    // Part 6: the real crypt1.s gauntlet, both outcomes.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 6: crypt1.s EnterZone(\"str5\"), driven for real ---\n");
    {
        skInterpreter interpreter;
        sk_b::MenuStack stack(root, interpreter, &strings);
        stack.SetCurrentLevelName("crypt1");
        sk_b::PlayerExecutable& player = stack.player();

        std::unique_ptr<sk_b::ZoneScriptExecutable> script;
        skExecutableContext loadCtxt(&interpreter);
        try {
            script = std::make_unique<sk_b::ZoneScriptExecutable>(
                skString((root + "/crypt1.s").c_str()), loadCtxt, stack);
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR loading crypt1.s: %s)\n", e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR loading crypt1.s: %s)\n", e.toString().ptr());
        }
        Check(script != nullptr, "crypt1.s loads");

        if (script) {
            // --- the forced failure ---
            //
            // Strength -2 against difficulty 25 gives a span of 23 and a
            // non-negative roll, so the check cannot pass. That makes the
            // fail branch deterministic rather than merely likely.
            *player.EffectStatSlot(sk_b::kEffectStatStrengthProper) = -2;
            *player.EffectStatSlot(sk_b::kEffectStatMaxHealth) = 100;
            player.SetHealth(100);

            script->InvokeEnterZone("str5");

            const sk_b::MenuExecutable* opened = stack.currentMenu();
            Check(MenuHasTextId(opened, 599),
                  "a failed check opened crypt1\\CheckFail5 (static item 599)");
            Check(!MenuHasTextId(opened, 601), "...and not CheckPass (601)");
            const int healthAfterFirstFail = player.health();
            Check(healthAfterFirstFail <= 98 && healthAfterFirstFail >= 96,
                  "checkfail5.s's own DoDamage(Random(2,4)) took 2-4 health (now " +
                      std::to_string(healthAfterFirstFail) + ")");

            // The zone flag must NOT have latched -- the check has to stay
            // live so the player can come back and try again. Compared as a
            // delta rather than against a constant: the damage is itself a
            // 2-4 roll, so two failures land anywhere in 92..96.
            script->InvokeEnterZone("str5");
            Check(player.health() < healthAfterFirstFail,
                  "the check is still live: entering again rolled and hurt again (" +
                      std::to_string(healthAfterFirstFail) + " -> " +
                      std::to_string(player.health()) + ")");

            // --- the pass ---
            //
            // At strength 1000 a failure is 24 outcomes in 1025, so this
            // loops rather than asserting a single call: the point is that
            // a pass happens and latches, not which iteration it lands on.
            *player.EffectStatSlot(sk_b::kEffectStatStrengthProper) = 1000;
            player.SetHealth(100);
            bool passed = false;
            int attempts = 0;
            for (; attempts < 30 && !passed; ++attempts) {
                script->InvokeEnterZone("str5");
                if (MenuHasTextId(stack.currentMenu(), 601)) passed = true;
            }
            Check(passed, "a high-strength character passes within 30 tries (took " +
                              std::to_string(attempts) + ")");
            Check(MenuHasTextId(stack.currentMenu(), 601),
                  "the pass opened crypt1\\CheckPass (static item 601)");

            // saved_str5 = 1 now, so the whole block is skipped: no menu
            // change and no further damage, forever.
            const int healthAtLatch = player.health();
            for (int i = 0; i < 5; ++i) script->InvokeEnterZone("str5");
            Check(player.health() == healthAtLatch,
                  "saved_str5 latched: five more entries roll nothing and cost nothing");

            // A different zone name in the same handler is unaffected --
            // the fourteen checks are independent, one flag each.
            player.SetHealth(100);
            *player.EffectStatSlot(sk_b::kEffectStatStrengthProper) = -2;
            script->InvokeEnterZone("str6");
            Check(player.health() < 100,
                  "str6 is a separate check with its own flag and still fires");
            Check(MenuHasTextId(stack.currentMenu(), 599),
                  "...and routes to the tier-6 fail menu, which also opens with 599");
        }
    }

    std::printf("\n=========================================================\n");
    if (g_failures == 0) {
        std::printf("m65_attribute_check_smoke: PASSED (all checks) (%d checks)\n", g_checks);
    } else {
        std::printf("m65_attribute_check_smoke: %d/%d CHECKS FAILED\n", g_failures, g_checks);
    }
    return g_failures == 0 ? 0 : 1;
}
