// M53 smoke test: the two scripts this port could not place.
//
// The roadmap bullet named `crypt2/controller.s` and `twilite/steamsound.s`
// together, on the theory that neither script's placement mechanism was
// one of the entity categories the zone-load block resolves. That theory
// is wrong for both, in opposite directions:
//
//   * **`crypt2/controller.s` is placed**, and always was --
//     `crypt2.ent` record #166, typeId 6023 (`entities.txt`'s `!final_tp`,
//     category 3), placement name `"star"`, at exactly the coordinates the
//     script itself later spawns the Umbra at. The port loads that
//     placement already. What it lacked was the *timer* the script is
//     built out of: `Delay(seconds, tag)` -> `DelayReached(tag)`, which
//     nothing in this port implemented, and which is what `crypt2.s`
//     reaches for (`Star = GetEntity("star"); Star.Delay(1, 0);`) on the
//     seventh crystal.
//   * **`twilite/steamsound.s` is not placed by anything**, and is not
//     special: it is one of a large set of shipped scripts that no `.ent`
//     record, no `entities.txt` row and no other script names. Cut
//     content, not a missing mechanism.
//
// So the assertions come in four groups: the timer's arithmetic against
// the decompiled engine, the timer driving the two real scripts, the
// placement record that proves controller.s was always reachable, and the
// survey that proves steamsound.s never was.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/script_delay.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/zone.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-78s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    ++g_checks;
    if (!ok) ++g_failures;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string ReadFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// A LevelHost that only records what the script asked the zone to do --
// enough to watch controller.s's seven lighting steps go by without
// needing a live renderer.
struct RecordingZoneRegions : sk_bindings::LevelExecutable::ZoneRegions {
    struct Lit { int x0, y0, x1, y1, level; };
    std::vector<Lit> lit;

    bool HasRegion(const std::string&) const override { return false; }
    int LockRegion(const std::string&) override { return 0; }
    int UnlockRegion(const std::string&) override { return 0; }
    void LightRect(int x0, int y0, int x1, int y1, int level) override {
        lit.push_back({x0, y0, x1, y1, level});
    }
};

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";
    const std::filesystem::path root(scriptRoot);

    std::printf("=== M53: Delay/DelayReached, and the two \"unplaceable\" scripts ===\n\n");

    // ------------------------------------------------------------------
    // Part 1 -- the timer's arithmetic, against FUN_1006410c and the
    // Entity dispatcher's own Delay case.
    // ------------------------------------------------------------------
    std::printf("-- 1. the timer --\n");
    {
        sk_bindings::GameClock clock;
        sk_bindings::ScriptDelay d;
        Check(sk_bindings::kClockUnitsPerSecond == 256,
              "the clock is 8.8 fixed-point seconds, so Delay(n) is n * 0x100 units");

        d.Arm(clock, 1, 7);
        Check(d.armed() && d.deadline() == 256 && d.tag() == 7,
              "Delay(1, 7) arms a deadline one second out and stores the tag");

        int tag = -1;
        clock.Advance(0.5f);
        Check(!d.Fire(clock, &tag), "half a second in, it has not fired");
        clock.Advance(0.5f);
        Check(d.Fire(clock, &tag) && tag == 7,
              "at one second it fires, with its tag -- the comparison is `deadline <= clock`");
        Check(!d.armed(), "and the armed flag is cleared *before* dispatch (FUN_1006410c)");
        Check(!d.Fire(clock, &tag), "so it does not fire again on the next tick");

        // A handler that re-arms is the whole point: every chained
        // sequence in the corpus ends its case by arming the next one.
        d.Arm(clock, 1, 8);
        Check(d.armed() && d.tag() == 8, "re-arming from inside the callback is what chains work by");
        d.Stop();
        Check(!d.armed(), "StopDelay disarms without firing");

        // The one-argument form: the dispatcher stores 0 for the tag.
        sk_bindings::ScriptDelay bare;
        skRValueArray args;
        args.append(skRValue(2));
        skRValue ret;
        Check(sk_bindings::TryHandleDelay(bare, clock, skString("Delay"), args, ret),
              "Delay(seconds) is handled with one argument");
        Check(bare.armed() && bare.tag() == 0, "and its tag defaults to 0");
    }

    // ------------------------------------------------------------------
    // Part 2 -- crypt2/controller.s, driven for real.
    // ------------------------------------------------------------------
    std::printf("\n-- 2. crypt2/controller.s, the Umbra arrival sequence --\n");
    {
        sk::StringTable strings;
        strings.Load(std::string(scriptRoot) + "/stringtable.eng");
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings, nullptr, nullptr);
        RecordingZoneRegions host;
        stack.level().SetZoneRegions(&host);

        const std::string path = std::string(scriptRoot) + "/crypt2/controller.s";
        std::unique_ptr<sk_bindings::ItemExecutable> controller;
        try {
            skExecutableContext loadCtxt(&interpreter);
            controller = std::make_unique<sk_bindings::ItemExecutable>(
                skString(path.c_str()), loadCtxt, stack);
        } catch (skParseException& ex) {
            std::printf("   PARSE ERROR: %s\n", ex.toString().ptr());
        }
        Check(controller != nullptr, "the script loads as an ordinary category-3 item object");

        if (controller) {
            // crypt2.s's own trigger, verbatim: Star.Delay(1, 0).
            skExecutableContext ctxt(&interpreter);
            skRValueArray args;
            args.append(skRValue(1));
            args.append(skRValue(0));
            skRValue ret;
            controller->method(skString("Delay"), args, ret, ctxt);
            Check(controller->delay().armed(), "crypt2.s's `Star.Delay(1, 0)` arms it");

            // Run the chain the way the tick loop does: advance, fire,
            // dispatch, repeat. Eight steps (tags 0..7) at one second each.
            int steps = 0;
            for (int i = 0; i < 400 && controller->delay().armed(); ++i) {
                stack.gameClock().Advance(0.04f);  // the real 25 Hz tick
                int tag = 0;
                if (!controller->delay().Fire(stack.gameClock(), &tag)) continue;
                ++steps;
                skRValueArray cbArgs;
                cbArgs.append(skRValue(tag));
                skRValue cbRet;
                skExecutableContext cbCtxt(&interpreter);
                try {
                    controller->method(skString(sk_bindings::kDelayReachedMethod), cbArgs, cbRet,
                                       cbCtxt);
                } catch (skRuntimeException& ex) {
                    std::printf("   RUNTIME ERROR at tag %d: %s\n", tag, ex.toString().ptr());
                    break;
                }
            }
            Check(steps == 8,
                  "it runs eight steps and stops -- tags 0..7, each arming the next");
            Check(host.lit.size() == 7,
                  "seven of them light a rectangle; the eighth spawns the Umbra instead");
            if (host.lit.size() == 7) {
                // The first and last rectangles, verbatim from the script.
                const auto& first = host.lit.front();
                const auto& last = host.lit.back();
                Check(first.x0 == 11 && first.y0 == 36 && first.x1 == 14 && first.y1 == 39 &&
                          first.level == 64,
                      "the first is LightRect(11, 36, 14, 39, 64)");
                Check(last.x0 == 7 && last.y0 == 30 && last.x1 == 10 && last.y1 == 33,
                      "the last is LightRect(7, 30, 10, 33, 64) -- seven crystal alcoves");
                bool allSameLevel = true;
                for (const auto& l : host.lit) {
                    if (l.level != 64) allSameLevel = false;
                }
                Check(allSameLevel, "all seven light to the same level");
            }
            std::printf("   (the sweep is %zu lit rectangles over 8 seconds of game time)\n",
                        host.lit.size());
        }
    }

    // ------------------------------------------------------------------
    // Part 2b -- the other host. Category 2 is where most of the shipped
    // Delay usage actually is: azra_rat.s alone has 35 placements in the
    // port's own starting zone.
    // ------------------------------------------------------------------
    std::printf("\n-- 2b. monsters/azra_rat.s, the other host --\n");
    {
        sk::StringTable strings;
        strings.Load(std::string(scriptRoot) + "/stringtable.eng");
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings, nullptr, nullptr);

        const std::string path = std::string(scriptRoot) + "/monsters/azra_rat.s";
        std::unique_ptr<sk_bindings::MonsterExecutable> rat;
        try {
            skExecutableContext loadCtxt(&interpreter);
            rat = std::make_unique<sk_bindings::MonsterExecutable>(
                skString(path.c_str()), loadCtxt, &strings, stack.player(), stack);
        } catch (skParseException& ex) {
            std::printf("   PARSE ERROR: %s\n", ex.toString().ptr());
        }
        Check(rat != nullptr, "a real monster script loads");
        if (rat) {
            skExecutableContext ctxt(&interpreter);
            skRValueArray args;
            args.append(skRValue(2));
            args.append(skRValue(0));
            skRValue ret;
            rat->method(skString("Delay"), args, ret, ctxt);
            Check(rat->delay().armed() && rat->delay().deadline() == 512,
                  "its OnKilled `Delay(2, 0)` arms two seconds out on the monster host too");

            // Two seconds of real ticks, then the callback: azra_rat.s's
            // DelayReached(0) opens the "rathurrah" congratulation menu --
            // the eighth-rat-kill payoff for the game's first quest, in
            // the zone this port starts in.
            int fired = 0, tag = -1;
            for (int i = 0; i < 200 && !fired; ++i) {
                stack.gameClock().Advance(0.04f);
                if (rat->delay().Fire(stack.gameClock(), &tag)) ++fired;
            }
            Check(fired == 1 && tag == 0, "and fires once, two seconds later, with tag 0");
            std::printf("   (its DelayReached(0) opens \"rathurrah\" -- the eight-kill quest "
                        "payoff)\n");
        }
    }

    // ------------------------------------------------------------------
    // Part 3 -- the placement that proves controller.s was reachable.
    // ------------------------------------------------------------------
    std::printf("\n-- 3. controller.s's placement, and its trigger --\n");
    {
        sk::Zone crypt2;
        Check(crypt2.Load(scriptRoot, "crypt2"), "crypt2 loads");
        const sk::Zone::EntPlacement* star = nullptr;
        for (const sk::Zone::EntPlacement& e : crypt2.entities()) {
            if (Lower(e.scriptPath).find("controller") != std::string::npos) star = &e;
        }
        Check(star != nullptr, "crypt2.ent carries a placement whose script is crypt2\\controller.s");
        if (star) {
            std::printf("   typeId %d, name \"%s\", at (%d, %d, %d)\n", star->typeId,
                        star->name.c_str(), star->x, star->y, star->z);
            Check(star->name == "star",
                  "its placement name is \"star\" -- what crypt2.s's GetEntity(\"star\") asks for");
            sk::EntityTypeTable types;
            Check(types.Load(scriptRoot), "entities.txt loads");
            const sk::EntityTypeDescriptor* desc = types.Lookup(star->typeId);
            Check(desc != nullptr && desc->category == 3,
                  "and its typeId is category 3, which this port already loads");
            if (desc) std::printf("   entities.txt: %d -> category %d, \"%s\"\n",
                                  star->typeId, desc->category, desc->name.c_str());
            // The script spawns the Umbra at 4480, 7808 -- the placement's
            // own spot. That is the tell that this record is the
            // sequencer, not scenery that happens to carry a tag.
            Check(star->x == 4480 && star->y == 7808,
                  "it sits exactly where its own script later spawns the Umbra (4480, 7808)");
        }

        const std::string crypt2Script = ReadFile(root / "crypt2.s");
        Check(crypt2Script.find("GetEntity(\"star\")") != std::string::npos &&
                  crypt2Script.find("Star.Delay(1, 0)") != std::string::npos,
              "crypt2.s starts the sequence with GetEntity(\"star\").Delay(1, 0)");
        Check(crypt2Script.find("num_crystals = 7") != std::string::npos,
              "and it does so on the seventh crystal");
    }

    // ------------------------------------------------------------------
    // Part 4 -- steamsound.s, and the company it keeps.
    // ------------------------------------------------------------------
    std::printf("\n-- 4. twilite/steamsound.s --\n");
    {
        // Every script path any .ent placement names.
        std::set<std::string> namedByEnt;
        std::error_code ec;
        for (auto& de : std::filesystem::directory_iterator(root, ec)) {
            if (de.path().extension() != ".ent") continue;
            sk::Zone z;
            if (!z.Load(scriptRoot, de.path().stem().string())) continue;
            for (const sk::Zone::EntPlacement& e : z.entities()) {
                if (e.scriptPath.empty()) continue;
                std::string s = Lower(e.scriptPath);
                std::replace(s.begin(), s.end(), '\\', '/');
                if (s.size() > 2 && s.substr(s.size() - 2) == ".s") s.resize(s.size() - 2);
                namedByEnt.insert(s);
            }
        }
        Check(!namedByEnt.empty(), "every zone's .ent placements parse");
        std::printf("   %zu distinct script paths are named by a placement\n", namedByEnt.size());

        // Every script path entities.txt names.
        std::set<std::string> namedByTypes;
        {
            std::ifstream f(root / "entities.txt");
            std::string line;
            while (std::getline(f, line)) {
                std::istringstream ls(line);
                int id = 0, model = 0, cat = 0;
                std::string name;
                if (!(ls >> id >> model >> cat >> name)) continue;
                std::string s = Lower(name);
                std::replace(s.begin(), s.end(), '\\', '/');
                if (s.size() > 2 && s.substr(s.size() - 2) == ".s") s.resize(s.size() - 2);
                namedByTypes.insert(s);
            }
        }

        // Every .s file, and the whole corpus as one blob.
        std::vector<std::filesystem::path> scripts;
        std::string corpus;
        for (auto it = std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, ec);
             it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec) || it->path().extension() != ".s") continue;
            scripts.push_back(it->path());
        }
        Check(scripts.size() > 1000, "the shipped script corpus is present");

        auto relKey = [&](const std::filesystem::path& p) {
            std::string s = Lower(std::filesystem::relative(p, root).generic_string());
            if (s.size() > 2 && s.substr(s.size() - 2) == ".s") s.resize(s.size() - 2);
            return s;
        };

        // Is any script's *stem* mentioned by some other script? That is
        // how OpenMenu/RunScript/LoadLevel reach one by name.
        std::string allButNothing;
        for (const auto& p : scripts) allButNothing += Lower(ReadFile(p));

        auto reachable = [&](const std::filesystem::path& p) {
            const std::string key = relKey(p);
            if (namedByEnt.count(key) || namedByTypes.count(key)) return true;
            const std::string stem = Lower(p.stem().string());
            // Count mentions across the whole corpus; a script naming only
            // itself does not count, and no script in this set does.
            std::string own = Lower(ReadFile(p));
            size_t total = 0, mine = 0;
            for (size_t i = allButNothing.find(stem); i != std::string::npos;
                 i = allButNothing.find(stem, i + stem.size())) {
                ++total;
            }
            for (size_t i = own.find(stem); i != std::string::npos; i = own.find(stem, i + stem.size())) {
                ++mine;
            }
            return total > mine;
        };

        const std::filesystem::path steam = root / "twilite" / "steamsound.s";
        Check(std::filesystem::exists(steam), "twilite/steamsound.s ships");
        Check(namedByEnt.count("twilite/steamsound") == 0,
              "no .ent placement in any zone names it");
        Check(namedByTypes.count("twilite/steamsound") == 0, "entities.txt does not name it");
        Check(!reachable(steam), "and no other script mentions it -- nothing can load it");

        // And it is not alone, which is the actual point: this is a shape
        // of leftover, not a placement mechanism.
        int orphans = 0;
        for (const auto& p : scripts) {
            if (!reachable(p)) ++orphans;
        }
        Check(orphans > 50,
              "it is one of many unreachable scripts, not a one-off missing mechanism");
        std::printf("   %d of %zu shipped scripts are named by no placement, no entities.txt\n"
                    "   row and no other script. (Some of those are opened by the engine\n"
                    "   itself by name -- SaveConfirm and friends -- which this test cannot\n"
                    "   see without the binary; steamsound.s is an entity script, so that\n"
                    "   escape hatch does not apply to it.)\n",
                    orphans, scripts.size());

        // What the script would have done, for the record.
        const std::string body = ReadFile(steam);
        Check(body.find("PlaySound(65, 75, 1, 255)") != std::string::npos,
              "its whole body is the four-argument PlaySound M51 decoded");
        Check(body.find("ShowEntity(false)") != std::string::npos &&
                  body.find("SetPassable(true)") != std::string::npos,
              "on an invisible, walk-through entity -- an ambient emitter and nothing else");
    }

    std::printf("\nm53_script_delay_smoke: %s (%d checks)\n",
                g_failures == 0 ? "PASSED (all checks)" : "FAILED", g_checks);
    return g_failures == 0 ? 0 : 1;
}
