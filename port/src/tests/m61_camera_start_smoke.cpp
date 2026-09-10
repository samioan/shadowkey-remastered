// M61 smoke test: the scripted spawn override, `SetCameraStart`.
//
// `GetPlayer().SetCameraStart(x, y, z, pitch, yaw, roll)` -- Player
// binding 0x39, 49 call sites, and the largest unhandled native left. The
// interesting claims, and what checks them here:
//
//   * The six values are stored in `.ent` **record** order (x, y, z, roll,
//     pitch, yaw at `engine+0x14a24..+0x14a38`) while the script signature
//     is (x, y, z, pitch, yaw, roll). Part 2 drives a real shipped script
//     and reads the values back by name; if the two orders were conflated
//     the yaw would land in the roll slot and every check would swap.
//   * Argument 5 is the heading. Part 4 checks that against all 49 shipped
//     call sites at once: pitch and roll are zero everywhere, the heading
//     is not.
//   * The heading runs the opposite way round from this port's camera yaw.
//     Part 1 checks `CameraAngleRadians`' negation against the engine's own
//     forward-motion identity, which is where it comes from.
//   * The override is armed before the level it applies to exists, so a
//     declined trip has to disarm it. Part 3 drives `RestoreSaveLevel`,
//     the binding `levelconfirm.s`'s "Don't Go" calls.
//   * The `.ent` player-start record carries its own three orientation
//     channels, which this port used to drop. Part 5 reads them off real
//     zone data.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "engine/screen_mode.h"
#include "simkin_bindings/level_executable.h"
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
#include "world/zone.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%-84s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

constexpr float kTwoPi = 6.28318530718f;

// The exact copy of main.cpp's CameraAngleRadians() -- kept here rather
// than exported because main.cpp is the host, not a library. Part 1 is
// what keeps the two honest about the one thing that matters (the sign).
float CameraAngleRadians(int32_t rawAngle) {
    return -static_cast<float>(static_cast<int16_t>(rawAngle & 0xffff)) / 65536.0f * kTwoPi;
}

bool Near(float a, float b, float eps = 0.002f) { return std::fabs(a - b) < eps; }

// Every `SetCameraStart(a, b, c, d, e, f)` in a shipped script, with the
// six arguments evaluated far enough to cover what the corpus actually
// writes -- integers, negatives, and broken1.s's one `0 + 512`.
struct CallSite {
    std::string file;
    long args[6] = {0, 0, 0, 0, 0, 0};
};

bool ParseArgs(const std::string& text, long out[6]) {
    // Split on commas at depth 0 and fold `a + b` / `a - b`.
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == ',') {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    parts.push_back(current);
    if (parts.size() != 6) return false;
    for (size_t i = 0; i < 6; ++i) {
        std::istringstream in(parts[i]);
        long total = 0;
        long term = 0;
        if (!(in >> term)) return false;
        total = term;
        std::string op;
        while (in >> op) {
            if (!(in >> term)) return false;
            if (op == "+") {
                total += term;
            } else if (op == "-") {
                total -= term;
            } else {
                return false;
            }
        }
        out[i] = total;
    }
    return true;
}

void CollectCallSites(const std::string& path, const std::string& label,
                      std::vector<CallSite>& out) {
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        size_t at = line.find("SetCameraStart");
        if (at == std::string::npos) continue;
        size_t open = line.find('(', at);
        size_t close = line.find(')', open == std::string::npos ? at : open);
        if (open == std::string::npos || close == std::string::npos) continue;
        CallSite site;
        site.file = label;
        if (ParseArgs(line.substr(open + 1, close - open - 1), site.args)) {
            out.push_back(site);
        }
    }
}

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
        std::printf("m61_camera_start_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    // ---------------------------------------------------------------
    // Part 1: the angle convention, derived rather than fitted.
    //
    // The engine's own "walk forward" (`FUN_100063f0`) advances an entity
    // by `dx += speed*sin(heading + 0x4000)` (== +cos) and
    // `dy += speed*sin(heading + 0x8000)` (== -sin). This port's tick loop
    // moves by `dx = cos(yaw), dy = sin(yaw)`. Those two agree only when
    // yaw and heading run opposite ways round, which is exactly what
    // CameraAngleRadians' minus sign is.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 1: raw heading -> camera yaw ---\n");
    {
        bool allAgree = true;
        for (int raw = 0; raw < 65536; raw += 1024) {
            float engineAngle = static_cast<float>(raw) / 65536.0f * kTwoPi;
            float engineDx = std::cos(engineAngle);
            float engineDy = -std::sin(engineAngle);
            float yaw = CameraAngleRadians(raw);
            if (!Near(std::cos(yaw), engineDx) || !Near(std::sin(yaw), engineDy)) allAgree = false;
        }
        Check(allAgree,
              "forward vector matches the engine's own FUN_100063f0 at all 64 sampled headings");

        // The four cardinal headings, spelled out, so a sign flip is
        // visible as a direction rather than as an epsilon.
        Check(Near(CameraAngleRadians(0), 0.0f), "heading 0x0000 -> yaw 0 (+x)");
        Check(Near(CameraAngleRadians(0x4000), -kTwoPi / 4.0f),
              "heading 0x4000 (quarter turn) -> yaw -pi/2, i.e. toward -y");
        // 0x8000 sign-extends to -32768, the same angle either way round.
        Check(Near(std::cos(CameraAngleRadians(0x8000)), -1.0f) &&
                  Near(std::sin(CameraAngleRadians(0x8000)), 0.0f, 0.01f),
              "heading 0x8000 (half turn) -> facing -x");
        Check(Near(CameraAngleRadians(-16384), kTwoPi / 4.0f),
              "a negative shipped heading (-16384) -> yaw +pi/2, i.e. toward +y");
    }

    // ---------------------------------------------------------------
    // Part 2: a real shipped script arms it, and the six values land in
    // the right slots.
    //
    // snowline.s's EnterZone handler:
    //     if( s = "FearFrost" ) {
    //         Level.LoadLevel("fearfrst");
    //         GetPlayer().SetCameraStart( 15975, 1094, 0, 0, 174, 0 );
    //     }
    // ---------------------------------------------------------------
    std::printf("\n--- Part 2: snowline.s EnterZone(\"FearFrost\") ---\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        stack.RequestGameStart("snowline");
        stack.ClearGameStartRequest();
        stack.SetCurrentLevelName("snowline");

        std::unique_ptr<sk_bindings::ZoneScriptExecutable> script;
        skExecutableContext loadCtxt(&interpreter);
        try {
            script = std::make_unique<sk_bindings::ZoneScriptExecutable>(
                skString((root + "/snowline.s").c_str()), loadCtxt, stack);
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR loading snowline.s: %s)\n", e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR loading snowline.s: %s)\n", e.toString().ptr());
        }
        Check(script != nullptr, "snowline.s loads");

        Check(!stack.cameraStart().armed, "nothing is armed before the handler runs");

        if (script) {
            skRValueArray args;
            args.append(skRValue(skString("FearFrost")));
            skRValue ret;
            skExecutableContext callCtxt(&interpreter);
            try {
                script->method(skString("EnterZone"), args, ret, callCtxt);
            } catch (skRuntimeException& e) {
                std::printf("   (RUNTIME ERROR in EnterZone: %s)\n", e.toString().ptr());
            }
        }

        const sk_bindings::MenuStack::CameraStart& cs = stack.cameraStart();
        Check(cs.armed, "the handler's SetCameraStart armed the override");
        Check(cs.x == 15975 && cs.y == 1094 && cs.z == 0,
              "arguments 1-3 are the spawn position (15975, 1094, 0)");
        Check(cs.yaw == 174, "argument 5 landed in yaw (174), not in roll");
        Check(cs.pitch == 0 && cs.roll == 0, "arguments 4 and 6 landed in pitch and roll, both 0");

        // The LoadLevel on the line above it, which is what makes arming
        // the override before the level exists meaningful.
        //
        // M90: and *nothing has loaded*. The whole point of the ordering
        // is that the override is armed for a trip the player has not
        // agreed to yet -- `LoadLevel` raised `levelconfirm.s` and went
        // no further. If it still loaded on the spot, arming afterwards
        // would be arming after the fact and the disarm in Part 3 would
        // have nothing to undo.
        Check(!stack.gameStartRequested(),
              "the same handler's Level.LoadLevel(\"fearfrst\") did NOT load -- prompt only");
        Check(stack.currentMenu() != nullptr &&
                  stack.currentMenu()->scriptName() == sk::kLevelConfirmMenuName,
              "LoadLevel raised the LevelConfirm screen");
        Check(stack.currentLevelName() == "fearfrst",
              "LoadLevel already moved the pending level name to the destination");

        // GetNextLevel/X/Y -- what levelconfirm.s's Go reads back.
        skRValueArray none;
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        stack.level().method(skString("GetNextLevel"), none, ret, ctxt);
        Check(std::string(ret.str().ptr()) == "fearfrst",
              "Level.GetNextLevel() returns the pending destination");
        stack.level().method(skString("GetNextLevelX"), none, ret, ctxt);
        int nextX = ret.intValue();
        stack.level().method(skString("GetNextLevelY"), none, ret, ctxt);
        int nextY = ret.intValue();
        Check(nextX == -1 && nextY == -1,
              "GetNextLevelX/Y default to -1 for a one-argument LoadLevel");

        // A different tag in the same handler must not re-arm: snowline.s's
        // "Tubes" branch opens a menu and calls SetPosition, not
        // SetCameraStart.
        stack.ClearCameraStart();
        if (script) {
            skRValueArray args;
            args.append(skRValue(skString("Tubes")));
            skRValue r;
            skExecutableContext callCtxt(&interpreter);
            try {
                script->method(skString("EnterZone"), args, r, callCtxt);
            } catch (skRuntimeException&) {
            }
        }
        Check(!stack.cameraStart().armed,
              "snowline.s's \"Tubes\" branch (SetPosition, no transition) arms nothing");
    }

    // ---------------------------------------------------------------
    // Part 3: the disarm. levelconfirm.s's "Don't Go" is
    // `Level.RestoreSaveLevel()`, and clearing the spawn override is half
    // of what that binding does -- otherwise a declined trip would leave
    // an override armed to fire at whatever loads next.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 3: RestoreSaveLevel, the declined trip ---\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        stack.SetCurrentLevelName("snowline");
        skExecutableContext ctxt(&interpreter);
        skRValue ret;

        skRValueArray loadArgs;
        loadArgs.append(skRValue(skString("fearfrst")));
        stack.level().method(skString("LoadLevel"), loadArgs, ret, ctxt);
        stack.ArmCameraStart(15975, 1094, 0, 0, 174, 0);
        Check(stack.cameraStart().armed && stack.currentLevelName() == "fearfrst",
              "armed and pending, exactly as the script leaves it");

        skRValueArray none;
        bool handled = stack.level().method(skString("RestoreSaveLevel"), none, ret, ctxt);
        Check(handled, "Level.RestoreSaveLevel() is handled");
        Check(!stack.cameraStart().armed, "RestoreSaveLevel disarmed the spawn override");
        Check(stack.currentLevelName() == "snowline",
              "RestoreSaveLevel put the previous level name back");
    }

    // ---------------------------------------------------------------
    // Part 4: all 49 shipped call sites at once.
    //
    // If argument 5 were not the heading, this is the check that would
    // fail: a whole game's worth of level designers do not put a signed
    // 65536-per-turn angle in the same slot 49 times by accident, and they
    // do not leave the other two at zero 49 times either.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 4: the shipped corpus ---\n");
    {
        static const char* kFiles[] = {
            "azra.s",       "broken1.s",    "broken2.s",  "crypt1.s",       "crypt2.s",
            "crypt3.s",     "delfhide.s",   "drgnfld.s",  "dstar_e.s",      "dstar_w.s",
            "erthcave.s",   "fearfrst.s",   "ffarena.s",  "ghstpass.s",     "glaciercrawl.s",
            "lakvan.s",     "lothcav.s",    "raiders.s",  "snowline.s",     "stouttp.s",
            "twilite.s",    "broken1/to_bw2.s",           "broken2/to_bw1.s",
            "ghstpass/gp_menu5.s",          "snowline/dragonfieldsmenu.s",
            "snowline/fearfrostmenu.s",     "snowline/ghastsmenu.s",
            "snowline/lakvanmenu.s",
        };
        std::vector<CallSite> sites;
        for (const char* rel : kFiles) {
            CollectCallSites(root + "/" + rel, rel, sites);
        }
        Check(sites.size() == 49, "found and parsed all 49 shipped SetCameraStart call sites (" +
                                      std::to_string(sites.size()) + ")");

        int nonZeroPitch = 0;
        int nonZeroRoll = 0;
        int nonZeroYaw = 0;
        int yawInRange = 0;
        int positionInRange = 0;
        for (const CallSite& s : sites) {
            if (s.args[3] != 0) ++nonZeroPitch;
            if (s.args[5] != 0) ++nonZeroRoll;
            if (s.args[4] != 0) ++nonZeroYaw;
            if (s.args[4] >= -32768 && s.args[4] <= 32767) ++yawInRange;
            // x and y are 8.8 world units inside a 128x128-tile grid;
            // z is a signed height. This is the check that would catch the
            // position arguments being misread as anything else.
            if (s.args[0] >= 0 && s.args[0] < 32768 && s.args[1] >= 0 && s.args[1] < 32768) {
                ++positionInRange;
            }
        }
        Check(nonZeroPitch == 0, "argument 4 (pitch) is 0 at every one of the 49 sites");
        Check(nonZeroRoll == 0, "argument 6 (roll) is 0 at every one of the 49 sites");
        Check(nonZeroYaw >= 40, "argument 5 (yaw) is a real value at nearly every site (" +
                                    std::to_string(nonZeroYaw) + "/49)");
        Check(yawInRange == static_cast<int>(sites.size()),
              "every yaw fits a signed 16-bit angle");
        Check(positionInRange == static_cast<int>(sites.size()),
              "every x/y is inside a 128x128-tile zone in 8.8 units");

        // The two dead sub-menu scripts: `broken1/to_bw2.s` and
        // `broken2/to_bw1.s` duplicate their parent zone's own arrival
        // coordinates but call ForceLoadLevel *before* SetCameraStart, so
        // the override would land after the load. Both are unreachable --
        // the OpenMenu that would raise them is commented out in the
        // shipped parent script -- which is why that ordering never
        // mattered. Checked here so the claim is data, not a memory.
        std::ifstream broken1(root + "/broken1.s");
        std::string text((std::istreambuf_iterator<char>(broken1)),
                         std::istreambuf_iterator<char>());
        size_t at = text.find("To_BW2");
        bool commentedOut = false;
        if (at != std::string::npos) {
            size_t lineStart = text.rfind('\n', at);
            std::string line = text.substr(lineStart + 1, at - lineStart);
            commentedOut = line.find("//") != std::string::npos;
        }
        Check(commentedOut,
              "broken1.s's OpenMenu(\"broken1\\\\To_BW2\") is commented out -- to_bw2.s is dead");
    }

    // ---------------------------------------------------------------
    // Part 5: the default path. The `.ent` player-start record carries its
    // own three orientation channels in exactly the slots the override
    // mirrors, and this port used to read the position and drop them.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 5: the .ent player-start record's own heading ---\n");
    {
        sk::Zone azra;
        bool loaded = azra.Load(scriptRoot, "azra");
        Check(loaded, "azra loads");
        if (loaded) {
            std::printf("   azra player start: (%d, %d, %d) pitch=%u yaw=%u roll=%u\n",
                        azra.playerStartX, azra.playerStartY, azra.playerStartZ,
                        azra.playerStartPitchRaw, azra.playerStartYawRaw,
                        azra.playerStartRollRaw);
            // The record's angle fields are read at the same three offsets
            // the generic-entity branch reads (0x10 pitch, 0x14 yaw, 0x0c
            // roll) -- the same offsets docs/ZONE_FORMAT.md verified from
            // raw disassembly. A real placement's angles are multiples of
            // 128; anything outside that would mean the offsets are wrong.
            Check((azra.playerStartYawRaw % 128) == 0,
                  "the player start's yaw is a multiple of 128, like every real placement angle");
            Check(azra.playerStartRollRaw == 0 && azra.playerStartPitchRaw == 0,
                  "azra's player start has no pitch or roll, matching every scripted spawn");
            float yaw = CameraAngleRadians(azra.playerStartYawRaw);
            Check(yaw <= 0.0001f && yaw > -kTwoPi,
                  "it converts to a camera yaw in (-2pi, 0]");
        }
    }

    std::printf("\nm61_camera_start_smoke: %s (%d checks)\n",
                g_failures == 0 ? "PASSED (all checks)" : "FAILED", g_checks);
    return g_failures == 0 ? 0 : 1;
}
