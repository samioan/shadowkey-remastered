// M26 (real zone-transition loading screen) smoke test: proves the
// real per-zone display-name table (assets/zone_display_names.h) against
// real stringtable.eng data, and that a real script's Level.LoadLevel(...)
// call (LevelExecutable::method(), e.g. cheatmenu.s's real
// `Level.LoadLevel("azra")`) genuinely requests a zone change without
// touching the player's already-in-progress inventory.
//
// Real data, read directly off the install image:
//   stringtable.eng id 3950: "Travel to: "
//   stringtable.eng ids 3821-3840: one real zone display name each, in
//     real-zone order (azra.zmp etc.) -- "Azra's Crossing", ...,
//     "Raider's Nest". ffarena has no entry in this real run.
#include <cstdio>
#include <string>

#include "assets/string_table.h"
#include "assets/zone_display_names.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRValueArray.h"

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m26_loading_screen_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    bool ok = true;

    // --- Part 1: the real zone display-name table, structurally and
    // against the real localized text. ---
    auto checkZone = [&](const char* zoneName, int expectedId, const char* expectedText) {
        int id = sk::ZoneDisplayNameStringId(zoneName);
        bool idOk = id == expectedId;
        std::printf("ZoneDisplayNameStringId(\"%s\"): %d (expected %d) %s\n", zoneName, id,
                    expectedId, idOk ? "OK" : "FAILED");
        if (!idOk) ok = false;
        if (id >= 0) {
            std::string text = strings.Get(id);
            bool textOk = text == expectedText;
            std::printf("  -> \"%s\" (expected \"%s\") %s\n", text.c_str(), expectedText,
                        textOk ? "OK" : "FAILED");
            if (!textOk) ok = false;
        }
    };
    checkZone("azra", 3821, "Azra's Crossing");
    checkZone("dstar_e", 3839, "Dragonstar East");
    checkZone("raiders", 3840, "Raider's Nest");
    // Real callers pass mixed-case internal names too (cheatmenu.s's own
    // Level.LoadLevel("GhstPass")/("LothCav")/("GlacierCrawl")) -- the
    // lookup must be case-insensitive.
    checkZone("GhstPass", 3824, "Ghast's Pass");
    checkZone("LothCav", 3837, "Loth' Na Caverns");

    int ffarenaId = sk::ZoneDisplayNameStringId("ffarena");
    bool ffarenaOk = ffarenaId < 0;
    std::printf(
        "ZoneDisplayNameStringId(\"ffarena\") (real zone, no entry in the real 3821-3840 run): %d "
        "(expected < 0) %s\n",
        ffarenaId, ffarenaOk ? "OK" : "FAILED");
    if (!ffarenaOk) ok = false;

    std::string prefix = strings.Get(3950);
    bool prefixOk = prefix == "Travel to: ";
    std::printf("stringtable.eng[3950] (real \"Travel to: \" prefix): \"%s\" %s\n", prefix.c_str(),
                prefixOk ? "OK" : "FAILED");
    if (!prefixOk) ok = false;

    // --- Part 2: a real script's Level.LoadLevel(...) genuinely requests
    // a zone change, without re-granting starting inventory. ---
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.RequestGameStart("azra");  // simulates a real game already under way
    stack.ClearGameStartRequest();
    size_t inventoryBefore = stack.player().inventory().size();

    skRValueArray args;
    args.append(skRValue(skString("delfhide")));
    skRValue ret;
    skExecutableContext ctxt(&interpreter);
    bool handled = stack.level().method(skString("LoadLevel"), args, ret, ctxt);
    std::printf("Level.LoadLevel(\"delfhide\") handled: %s (expected true) %s\n",
                handled ? "true" : "false", handled ? "OK" : "FAILED");
    if (!handled) ok = false;

    bool requestedOk = stack.gameStartRequested() && stack.requestedZone() == "delfhide";
    std::printf(
        "after LoadLevel: gameStartRequested()=%s requestedZone()=\"%s\" (expected true, "
        "\"delfhide\") %s\n",
        stack.gameStartRequested() ? "true" : "false", stack.requestedZone().c_str(),
        requestedOk ? "OK" : "FAILED");
    if (!requestedOk) ok = false;

    size_t inventoryAfter = stack.player().inventory().size();
    bool inventoryUnchanged = inventoryAfter == inventoryBefore;
    std::printf(
        "inventory size before/after LoadLevel: %zu -> %zu (a mid-game transition must not "
        "re-grant starting items) %s\n",
        inventoryBefore, inventoryAfter, inventoryUnchanged ? "OK" : "FAILED");
    if (!inventoryUnchanged) ok = false;

    std::printf("\nm26_loading_screen_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
