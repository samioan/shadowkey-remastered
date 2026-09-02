// M16 (generalized monster/NPC loading + Action::Use dialogue) smoke
// test: proves MonsterExecutable's new NPC-mode fields against a real
// placed NPC, not a synthetic fixture -- loads the real azra zone,
// resolves its real typeId-166 (.ent -> entities.txt, category 2)
// placement to monsters/Tanyin_Aldwyr.s, actually runs that script's
// real Init() through the vendored interpreter, and checks the
// resulting state (aggressive/usable/useTextId/invulnerable) matches
// the script's own literal values. Then runs the real OnUse() handler
// and confirms it actually opens the real snowline/TanyinConvo.s
// dialogue menu (a real branching conversation, not a stub). See
// docs/PORT_ROADMAP.md's M16 entry and monster_executable.h's class
// comment for the real script text this checks against.
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/zone.h"

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";
    const char* zoneName = argc > 2 ? argv[2] : "azra";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m16_npc_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m16_npc_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    sk::Zone zone;
    if (!zone.Load(scriptRoot, zoneName)) {
        std::printf("m16_npc_smoke: FAILED to load zone %s\n", zoneName);
        return 1;
    }

    // 1. Real Tanyin Aldwyr placement -- azra places exactly 1 (typeId
    // 166). Also confirm main.cpp's zone-load filter (category==2 + name
    // ends ".s") sees it, same generalization every other category-2
    // monster (typeId 202/azra_rat, etc.) already goes through.
    const sk::Zone::EntPlacement* tanyinPlacement = nullptr;
    int typeId166Count = 0;
    for (const auto& e : zone.entities()) {
        if (e.typeId == 166) {
            ++typeId166Count;
            tanyinPlacement = &e;
        }
    }
    bool ok = true;
    std::printf("real typeId=166 (Tanyin_Aldwyr.s) placements in %s: %d (expected 1)\n", zoneName,
                typeId166Count);
    if (typeId166Count != 1) ok = false;
    if (!tanyinPlacement) {
        std::printf("m16_npc_smoke: FAILED -- no typeId=166 placement in %s\n", zoneName);
        return 1;
    }

    const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(tanyinPlacement->typeId);
    if (!desc) {
        std::printf("m16_npc_smoke: FAILED -- typeId 166 has no entities.txt entry\n");
        return 1;
    }
    if (desc->category != 2) {
        std::printf("m16_npc_smoke: FAILED -- typeId 166 category=%d, expected 2 (monster)\n",
                    desc->category);
        return 1;
    }
    std::string relPath = desc->name;
    std::replace(relPath.begin(), relPath.end(), '\\', '/');
    std::string fullPath = std::string(scriptRoot) + "/" + relPath;
    std::printf("resolved typeId=166 -> \"%s\" (category=%d)\n", relPath.c_str(), desc->category);

    // 2. Actually run the real script's Init() -- same pattern
    // m12_combat_smoke.cpp/m15_interact_smoke.cpp already established.
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);

    skExecutableContext loadCtxt(&interpreter);
    std::unique_ptr<sk_bindings::MonsterExecutable> tanyin;
    try {
        tanyin = std::make_unique<sk_bindings::MonsterExecutable>(
            skString(fullPath.c_str()), loadCtxt, &strings, stack.player(), stack);
        skRValueArray args;
        args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
        skRValue ret;
        skExecutableContext callCtxt(&interpreter);
        tanyin->method(skString("Init"), args, ret, callCtxt);
    } catch (skParseException& e) {
        std::printf("m16_npc_smoke: FAILED -- PARSE ERROR loading %s: %s\n", fullPath.c_str(),
                    e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m16_npc_smoke: FAILED -- RUNTIME ERROR loading %s: %s\n", fullPath.c_str(),
                    e.toString().ptr());
        return 1;
    }

    // 3. Real literal values from Tanyin_Aldwyr.s's own Init() body:
    // SetAggressive(false), SetUsable(true), SetUseText(1062),
    // SetInvulnerable(true) -- the exact NPC-mode signature
    // monster_executable.h's class comment describes, distinguishing her
    // from an ordinary hostile monster like azra_rat.
    auto checkBool = [&](const char* label, bool actual, bool expected) {
        bool pass = actual == expected;
        std::printf("%s: %s (expected %s) %s\n", label, actual ? "true" : "false",
                    expected ? "true" : "false", pass ? "OK" : "FAILED");
        if (!pass) ok = false;
    };
    checkBool("aggressive", tanyin->aggressive(), false);
    checkBool("usable", tanyin->usable(), true);
    checkBool("invulnerable", tanyin->invulnerable(), true);
    bool useTextOk = tanyin->useTextId() == 1062;
    std::printf("useTextId: %d (expected 1062) %s\n", tanyin->useTextId(),
                useTextOk ? "OK" : "FAILED");
    if (!useTextOk) ok = false;

    // 4. Invulnerable -- ApplyDamage() must be a real no-op (an essential
    // quest NPC can't be killed), matching main.cpp's separate targeting
    // skip.
    tanyin->ApplyDamage(9999);
    bool survivedOk = tanyin->alive();
    std::printf("alive() after ApplyDamage(9999): %s (expected true, invulnerable) %s\n",
                survivedOk ? "true" : "false", survivedOk ? "OK" : "FAILED");
    if (!survivedOk) ok = false;

    // 5. OnUse() -- real script calls OpenMenu("snowline\\TanyinConvo"),
    // which should resolve and actually run snowline/tanyinconvo.s's own
    // real Init(), a genuine branching dialogue tree (not a stub) --
    // confirm by checking MenuStack::currentMenu() actually changed to a
    // new, non-null menu, and that it has at least one real row (its
    // Init() calls AddStaticItem/AddMenuItem regardless of which branch
    // unmodeled quest state soft-fails into).
    sk_bindings::MenuExecutable* before = stack.currentMenu();
    tanyin->InvokeOnUse();
    sk_bindings::MenuExecutable* after = stack.currentMenu();
    bool menuChangedOk = after != nullptr && after != before;
    std::printf("currentMenu() changed after OnUse(): %s %s\n", menuChangedOk ? "true" : "false",
                menuChangedOk ? "OK" : "FAILED");
    if (!menuChangedOk) ok = false;
    if (after) {
        bool hasRowsOk = !after->rows().empty();
        std::printf("dialogue menu row count: %zu %s\n", after->rows().size(),
                    hasRowsOk ? "OK" : "FAILED");
        if (!hasRowsOk) ok = false;
    }

    std::printf("\nm16_npc_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
