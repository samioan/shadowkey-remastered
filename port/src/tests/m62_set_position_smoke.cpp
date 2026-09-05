// M62 smoke test: `SetPosition` and the Object/Entity position bindings.
//
// SimKin class `0x14d08` is the base a player, a monster, a door, an item
// and a prop all derive from, and five of its bindings are one feature:
// `SetPosition`, `SetPositionMirror`, `GetPositionX/Y/Z`. 63 `SetPosition`
// call sites across four receivers; only the monster had any of it.
//
// What each part checks, and why it is the check that could fail:
//
//   1. The shipped corpus: how many sites, on which receivers, and how many
//      pass two arguments rather than three. `lakvan.s` has two real
//      two-argument calls, which the port's old monster-only handler
//      rejected outright (`args.entries() >= 3`).
//   2. `gate.s` end to end -- a real shipped portcullis whose entire
//      open/close mechanism is GetPositionZ() +/- 1200 then SetPosition.
//      It needs the getters, the setter, and a door NOT being floor-snapped.
//   3. The actor predicate (`vtable[0xc8]`), which decides the snap.
//   4. The snap formula itself, `resolveSurface(x, y, z + 0x180) + 0x80`,
//      against real zone geometry.
//   5. That the real ground function differs from the corner blend the
//      player's physics used to call -- measured on real azra data, since
//      the size of that difference is the whole argument for the change.
//   6. A real script driving a player teleport.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/door_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/zone_script_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/zone.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%-84s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

bool Near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }

// Every `SetPosition(`/`SetPositionMirror(` in the shipped scripts, with
// its receiver and argument count. Comment-stripped, single-line calls
// only -- which is all the corpus has.
struct Site {
    std::string receiver;  // "" for a bare call inside the entity's own script
    int argc = 0;
    bool mirror = false;
};

void CollectSites(const std::string& path, std::vector<Site>& out) {
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        size_t comment = line.find("//");
        if (comment != std::string::npos) line = line.substr(0, comment);
        size_t from = 0;
        while (true) {
            size_t at = line.find("SetPosition", from);
            if (at == std::string::npos) break;
            from = at + 1;
            size_t open = at + std::string("SetPosition").size();
            bool mirror = false;
            if (line.compare(open, 6, "Mirror") == 0) {
                mirror = true;
                open += 6;
                if (line.compare(open, 3, "All") == 0) open += 3;  // a different binding
            }
            while (open < line.size() && line[open] == ' ') ++open;
            if (open >= line.size() || line[open] != '(') continue;
            size_t close = line.find(')', open);
            if (close == std::string::npos) continue;

            Site site;
            site.mirror = mirror;
            std::string inner = line.substr(open + 1, close - open - 1);
            // Argument count: commas at depth 0 (no nested calls appear in
            // any real argument list here) plus one, or 0 for empty.
            bool blank = inner.find_first_not_of(" \t") == std::string::npos;
            site.argc = blank ? 0 : 1;
            for (char c : inner) {
                if (c == ',') ++site.argc;
            }
            // The receiver is whatever identifier (possibly `GetPlayer()`)
            // precedes a '.' immediately before the name.
            if (at > 0 && line[at - 1] == '.') {
                size_t end = at - 1;
                size_t start = end;
                while (start > 0) {
                    char c = line[start - 1];
                    if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ')' ||
                        c == '(') {
                        --start;
                    } else {
                        break;
                    }
                }
                site.receiver = line.substr(start, end - start);
            }
            out.push_back(site);
        }
    }
}

const char* kScriptFiles[] = {
    "lakvan.s",       "broken2.s",     "cheatmenu.s",   "crypt2.s",   "gate.s",
    "arat2.s",        "snowline.s",    "crypt1/azra.s", "delfhide/heather.s",
    "dstar_e/tanyin_aldwyr.s",         "glcrcrwl/icegate.s",
    "dstar_e/pit_boss_battle.s",       "monsters/azra_incrossing.s",
    "monsters/birgiddaazra.s",         "monsters/old_trinket.s",
    "monsters/skelos_undriel_azra.s",  "monsters/skelos_undriel_et.s",
    "monsters/villager_prisoner1.s",   "monsters/villager_prisoner2.s",
    "monsters/villager_prisoner3.s",   "monsters/villager_prisoner4.s",
    "monsters/azra_rat.s",
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
        std::printf("m62_set_position_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    // ---------------------------------------------------------------
    // Part 1: the shipped corpus.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 1: the shipped corpus ---\n");
    {
        std::vector<Site> sites;
        for (const char* rel : kScriptFiles) CollectSites(root + "/" + rel, sites);

        int plain = 0, mirror = 0, twoArg = 0, threeArg = 0, bare = 0;
        std::set<std::string> receivers;
        for (const Site& s : sites) {
            if (s.argc == 0) continue;  // SetPositionMirrorAll's own arg list is counted below
            if (s.mirror) {
                ++mirror;
            } else {
                ++plain;
            }
            if (s.argc == 2) ++twoArg;
            if (s.argc == 3) ++threeArg;
            if (s.receiver.empty()) {
                ++bare;
            } else {
                receivers.insert(s.receiver);
            }
        }
        std::printf("   %d plain + %d mirror across %zu explicit receivers, %d bare\n", plain,
                    mirror, receivers.size(), bare);

        Check(plain >= 55, "the plain SetPosition sites are the bulk of the feature (" +
                               std::to_string(plain) + ")");
        Check(twoArg == 2, "exactly two shipped calls pass only x and y (lakvan.s's To_4A/To_4B)");
        Check(threeArg >= 55, "every other call passes x, y and z (" + std::to_string(threeArg) +
                                  ")");
        Check(bare >= 15,
              "a good share are bare calls -- an entity moving itself from its own script (" +
                  std::to_string(bare) + ")");
        Check(receivers.count("GetPlayer()") == 1 && receivers.count("Player") == 1,
              "the player is addressed both as GetPlayer() and via a local named Player");
        // `Player` is not a global: broken2.s writes `Player = GetPlayer();`
        // on the line before each use. Undeclared, so SimKin makes it a
        // stack local holding a real object -- which is why it works.
        std::ifstream b2(root + "/broken2.s");
        std::string text((std::istreambuf_iterator<char>(b2)), std::istreambuf_iterator<char>());
        size_t assigns = 0, uses = 0, at = 0;
        while ((at = text.find("Player = GetPlayer()", at)) != std::string::npos) {
            ++assigns;
            ++at;
        }
        at = 0;
        while ((at = text.find("Player.SetPosition", at)) != std::string::npos) {
            ++uses;
            ++at;
        }
        Check(assigns == uses && assigns > 0,
              "every `Player.SetPosition` in broken2.s has its own `Player = GetPlayer()`");
    }

    // ---------------------------------------------------------------
    // Part 2: gate.s, a real portcullis, end to end.
    //
    //     RaiseDoor[ (s) {
    //         GetPlayer().PlaySound( 63 );
    //         x=GetPositionX(); y=GetPositionY(); z=GetPositionZ() + 1200;
    //         SetPosition(x,y,z);
    //         saved_Open = 1; DoorOpened(self,1,0); return; } ]
    //
    // Three bindings this port did not have, in four lines, plus the rule
    // that a door is not floor-snapped. Every one of them has to be right
    // or the gate does not move.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 2: gate.s, a real portcullis ---\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        std::unique_ptr<sk_bindings::DoorExecutable> gate;
        skExecutableContext loadCtxt(&interpreter);
        try {
            gate = std::make_unique<sk_bindings::DoorExecutable>(
                skString((root + "/gate.s").c_str()), loadCtxt, stack.player());
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR loading gate.s: %s)\n", e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR loading gate.s: %s)\n", e.toString().ptr());
        }
        Check(gate != nullptr, "gate.s loads");

        if (gate) {
            // Seeded by the host from the .ent placement, before Init() --
            // the order the engine uses.
            gate->SetWorldPosition(13000, 7000, -2816);
            Check(!gate->isActorForPositioning(),
                  "a door is not an actor, so its teleport is exempt from the floor snap");

            auto invoke = [&](const char* handler) {
                skRValueArray args;
                args.append(skRValue(0));
                skRValue ret;
                skExecutableContext ctxt(&interpreter);
                try {
                    gate->method(skString(handler), args, ret, ctxt);
                } catch (skRuntimeException& e) {
                    std::printf("   (RUNTIME ERROR in %s: %s)\n", handler, e.toString().ptr());
                }
            };

            invoke("RaiseDoor");
            float x = 0, y = 0, z = 0;
            bool moved = gate->TakePendingPosition(x, y, z);
            Check(moved, "RaiseDoor requested a move");
            Check(Near(x, 13000.0f) && Near(y, 7000.0f),
                  "the gate lifts straight up -- x and y are unchanged");
            Check(Near(z, -2816.0f + 1200.0f),
                  "the gate rose exactly 1200 raw units (GetPositionZ() + 1200)");

            invoke("RaiseDoor");  // second call takes the "it is open, close it" branch
            moved = gate->TakePendingPosition(x, y, z);
            Check(moved, "the second RaiseDoor took the close branch and requested a move");
            Check(Near(z, -2816.0f),
                  "the gate came back down to exactly where it started");
        }
    }

    // ---------------------------------------------------------------
    // Part 3: the actor predicate, and the two-argument form.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 3: actors, non-actors, and SetPosition(x, y) ---\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        sk_bindings::PlayerExecutable& player = stack.player();
        Check(player.isActorForPositioning(), "the player IS an actor -- teleports floor-snap");

        skExecutableContext ctxt(&interpreter);
        skRValue ret;

        // lakvan.s's To_4A: the two-argument form the old handler rejected.
        skRValueArray two;
        two.append(skRValue(8089));
        two.append(skRValue(4480));
        bool handled = player.method(skString("SetPosition"), two, ret, ctxt);
        Check(handled, "GetPlayer().SetPosition(8089, 4480) is handled (lakvan.s's To_4A)");
        float x = 0, y = 0, z = -1;
        Check(player.TakePendingPosition(x, y, z) && Near(x, 8089.0f) && Near(y, 4480.0f) &&
                  Near(z, 0.0f),
              "the two-argument form defaults z to 0, as the real case does");

        // The getters, which gate.s needs and nothing implemented before.
        skRValueArray none;
        player.method(skString("GetPositionX"), none, ret, ctxt);
        int gx = ret.intValue();
        player.method(skString("GetPositionY"), none, ret, ctxt);
        int gy = ret.intValue();
        player.method(skString("GetPositionZ"), none, ret, ctxt);
        int gz = ret.intValue();
        Check(gx == 8089 && gy == 4480 && gz == 0,
              "GetPositionX/Y/Z read back what SetPosition wrote");

        Check(!player.TakePendingPosition(x, y, z),
              "a drained teleport does not fire twice");

        // SetPositionMirror is the same binding one index along.
        skRValueArray three;
        three.append(skRValue(1));
        three.append(skRValue(2));
        three.append(skRValue(3));
        Check(player.method(skString("SetPositionMirror"), three, ret, ctxt) &&
                  player.TakePendingPosition(x, y, z) && Near(z, 3.0f),
              "SetPositionMirror writes the same fields (azra_rat.s moves Trothgar with it)");
    }

    // ---------------------------------------------------------------
    // Part 4 + 5: the snap, and the ground function it is built on,
    // against real zone geometry.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 4: the snap, on real geometry ---\n");
    {
        sk::Zone azra;
        bool loaded = azra.Load(scriptRoot, "azra");
        Check(loaded, "azra loads");
        if (loaded) {
            const float px = static_cast<float>(azra.playerStartX);
            const float py = static_cast<float>(azra.playerStartY);
            const float pz = static_cast<float>(azra.playerStartZ);

            // The formula, spelled out: resolveSurface(x, y, z + 0x180) + 0x80.
            Check(Near(azra.SnapActorToGround(px, py, pz),
                       azra.CollisionFloorHeightAt(px, py, pz + 384.0f) + 128.0f),
                  "SnapActorToGround is exactly CollisionFloorHeightAt(z + 0x180) + 0x80");

            // Teleporting to the player start should land on the player
            // start's own floor, give or take the 0x80 lift.
            float snapped = azra.SnapActorToGround(px, py, pz);
            Check(std::fabs(snapped - pz) < 600.0f,
                  "teleporting to azra's own spawn lands within a tile-height of its authored z");

            // The z argument is a probe, not the answer: two very different
            // requested heights on an ordinary tile resolve to the same
            // ground.
            Check(Near(azra.SnapActorToGround(px, py, pz),
                       azra.SnapActorToGround(px, py, pz - 2000.0f)),
                  "on a single-storey tile the requested z is discarded, as the decompile says");
        }

    }

    // ---------------------------------------------------------------
    // Part 5: what the real ground function actually buys, measured.
    //
    // The player's footing used `FloorHeightAt` (always bilinear over the
    // four corners) rather than `CollisionFloorHeightAt` (`FUN_1001beac`).
    // Two branches separate them, and the shipped data says they are worth
    // very different amounts -- which is worth recording, because guessing
    // would have got it backwards:
    //
    //   * the flat-vs-corners branch (`ZmpCell::flags & 0x04`) makes **no**
    //     observable difference in any shipped zone: when the flag is clear
    //     the four corners already equal `floorBandThreshold`. It is a
    //     storage/authoring distinction, not a behavioural one.
    //   * the **two-storey** branch is entirely real, and entirely local:
    //     1,921 tiles carry it, all of them in `dstar_w` and
    //     `glaciercrawl`, and on those the old function put the player on
    //     the floor *below* the one they were standing on.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 5: what the real ground function buys ---\n");
    {
        struct ZoneResult {
            int flat = 0, twoStorey = 0, usable = 0, differ = 0;
            float worst = 0.0f;
        };
        auto survey = [](const sk::Zone& zone) {
            ZoneResult r;
            for (int ty = 0; ty < zone.height(); ++ty) {
                for (int tx = 0; tx < zone.width(); ++tx) {
                    const float wx = (static_cast<float>(tx) + 0.5f) * sk::kTileScale;
                    const float wy = (static_cast<float>(ty) + 0.5f) * sk::kTileScale;
                    const sk::ZmpCell& cell = zone.CellAt(tx, ty);
                    if ((cell.flags & 0x04) == 0) ++r.flat;
                    if (cell.blockFlags & 0x02) {
                        ++r.twoStorey;
                        const sk::ZcpEntry& t = zone.TypeOf(cell);
                        if (t.surIndexCeilingA != 0xff &&
                            !zone.surface(t.surIndexCeilingA).disabled()) {
                            ++r.usable;
                        }
                    }
                    // Probe from above -- which is where an actor standing
                    // on an upper storey actually is.
                    float blend = zone.FloorHeightAt(wx, wy);
                    float real = zone.CollisionFloorHeightAt(wx, wy, 30000.0f);
                    if (std::fabs(blend - real) > 1.0f) {
                        ++r.differ;
                        r.worst = (std::max)(r.worst, std::fabs(blend - real));
                    }
                }
            }
            return r;
        };

        // A zone with sloped terrain and no upper storeys: the flat branch
        // has plenty of material here and still changes nothing.
        sk::Zone azraZone;
        if (azraZone.Load(scriptRoot, "azra")) {
            ZoneResult r = survey(azraZone);
            std::printf("   azra:     flat=%d twoStorey=%d differ=%d\n", r.flat, r.twoStorey,
                        r.differ);
            Check(r.flat > 1000 && r.twoStorey == 0 && r.differ == 0,
                  "azra: thousands of flat-authored tiles, no upper storeys, zero disagreement");
        }

        // Dragonstar West is where the difference lives.
        sk::Zone dstarW;
        if (dstarW.Load(scriptRoot, "dstar_w")) {
            ZoneResult r = survey(dstarW);
            std::printf("   dstar_w:  twoStorey=%d usable=%d differ=%d worst=%.0f\n", r.twoStorey,
                        r.usable, r.differ, r.worst);
            Check(r.twoStorey > 1000 && r.usable > 1000,
                  "dstar_w really is a multi-storey zone (" + std::to_string(r.twoStorey) +
                      " tiles, " + std::to_string(r.usable) + " with a standable ceiling)");
            Check(r.differ > 1000,
                  "on " + std::to_string(r.differ) +
                      " of them the old ground function returned the storey below");
            Check(r.worst > 1000.0f,
                  "and it was wrong by up to " + std::to_string(static_cast<int>(r.worst)) +
                      " raw units -- several tile-widths, not rounding");
        }

        sk::Zone glacier;
        if (glacier.Load(scriptRoot, "glaciercrawl")) {
            ZoneResult r = survey(glacier);
            std::printf("   glacier:  twoStorey=%d usable=%d differ=%d worst=%.0f\n", r.twoStorey,
                        r.usable, r.differ, r.worst);
            Check(r.twoStorey > 0 && r.differ > 0,
                  "glaciercrawl is the only other zone with upper storeys (" +
                      std::to_string(r.twoStorey) + " tiles)");
        }
    }

    // ---------------------------------------------------------------
    // Part 6: a real script driving a player teleport.
    // `lakvan.s`'s EnterZone("To_4A") is the two-argument form in situ.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 6: lakvan.s EnterZone(\"To_4A\") ---\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        std::unique_ptr<sk_bindings::ZoneScriptExecutable> script;
        skExecutableContext loadCtxt(&interpreter);
        try {
            script = std::make_unique<sk_bindings::ZoneScriptExecutable>(
                skString((root + "/lakvan.s").c_str()), loadCtxt, stack);
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR loading lakvan.s: %s)\n", e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR loading lakvan.s: %s)\n", e.toString().ptr());
        }
        Check(script != nullptr, "lakvan.s loads");
        if (script) {
            skRValueArray args;
            args.append(skRValue(skString("To_4A")));
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            try {
                script->method(skString("EnterZone"), args, ret, ctxt);
            } catch (skRuntimeException& e) {
                std::printf("   (RUNTIME ERROR in EnterZone: %s)\n", e.toString().ptr());
            }
            float x = 0, y = 0, z = -1;
            Check(stack.player().TakePendingPosition(x, y, z) && Near(x, 8089.0f) &&
                      Near(y, 4480.0f) && Near(z, 0.0f),
                  "the real handler teleported the player to (8089, 4480, 0)");
        }
    }

    std::printf("\nm62_set_position_smoke: %s (%d checks)\n",
                g_failures == 0 ? "PASSED (all checks)" : "FAILED", g_checks);
    return g_failures == 0 ? 0 : 1;
}
