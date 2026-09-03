// M18 (Level/Zone global object) smoke test: proves the new bare `Level`
// global -- and the real .ent per-instance `name` field it resolves
// GetEntity() lookups against -- closes the exact gap M17's own
// quest_smoke test documented as an open loose end (monsters/
// azra_rat.s's `Level.GetEntity("trthgar")` throwing before its 8th-kill
// branch's `SetQuestSolved(0,true)` line ever ran).
//
// Part 1: structural check against real azra.ent data -- confirms
// EntPlacement::name actually parses "trthgar" at the one real typeId=141
// placement, and that entities.txt genuinely categorizes it as 7
// (merchant) -- monsters/Gravel_Trothgar.s is Gravel the blacksmith, not
// a category-2 monster/NPC, which is why main.cpp's zone-load loop needed
// the new category-7 generalization (see main.cpp's own comment) for
// this entity to ever become a live, nameable object at all.
//
// Part 2: runs Gravel_Trothgar.s's real Init() (a real merchant script --
// SetInvulnerable/SetAggressive(false)/ClearProducts()/AddProduct(...),
// all already-soft-fail-compatible with MonsterExecutable, no new native
// class needed), registers it into the Level global under its real
// instance name, then confirms `Level.GetEntity("trthgar")` resolves to
// that exact object -- and that an unregistered name resolves to the
// same blank default the new `null` global constant holds, matching real
// scripts' `if (X != null)` pattern (azra.s).
//
// Part 3: the actual payoff -- runs real Azra_Rat.s's OnKilled() 8 times
// (same real data M17's quest_smoke test used) and confirms
// GetPlayer().SetQuestSolved(0,true) now genuinely executes: quest id 0
// flips to solved, closing the exact gap M17 left open.
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/zone.h"

namespace {

std::unique_ptr<sk_bindings::MonsterExecutable> LoadAndInit(const std::string& fullPath,
                                                              skInterpreter& interpreter,
                                                              const sk::StringTable* strings,
                                                              sk_bindings::PlayerExecutable& player,
                                                              sk_bindings::MenuStack& stack) {
    skExecutableContext loadCtxt(&interpreter);
    auto obj = std::make_unique<sk_bindings::MonsterExecutable>(skString(fullPath.c_str()), loadCtxt,
                                                                  strings, player, stack);
    skRValueArray args;
    args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
    skRValue ret;
    skExecutableContext callCtxt(&interpreter);
    obj->method(skString("Init"), args, ret, callCtxt);
    return obj;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";
    const char* zoneName = argc > 2 ? argv[2] : "azra";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m18_level_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m18_level_smoke: FAILED to load entities.txt\n");
        return 1;
    }
    sk::Zone zone;
    if (!zone.Load(scriptRoot, zoneName)) {
        std::printf("m18_level_smoke: FAILED to load zone %s\n", zoneName);
        return 1;
    }

    bool ok = true;

    // --- Part 1: real .ent name field + category, structural ---
    const sk::Zone::EntPlacement* trothgarPlacement = nullptr;
    int typeId141Count = 0;
    for (const auto& e : zone.entities()) {
        if (e.typeId == 141) {
            ++typeId141Count;
            trothgarPlacement = &e;
        }
    }
    std::printf("real typeId=141 (Gravel_Trothgar.s) placements in %s: %d (expected 1)\n", zoneName,
                typeId141Count);
    if (typeId141Count != 1) ok = false;
    if (!trothgarPlacement) {
        std::printf("m18_level_smoke: FAILED -- no typeId=141 placement in %s\n", zoneName);
        return 1;
    }
    bool nameOk = trothgarPlacement->name == "trthgar";
    std::printf("placement's real .ent name field: \"%s\" (expected \"trthgar\") %s\n",
                trothgarPlacement->name.c_str(), nameOk ? "OK" : "FAILED");
    if (!nameOk) ok = false;

    const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(141);
    if (!desc) {
        std::printf("m18_level_smoke: FAILED -- typeId 141 has no entities.txt entry\n");
        return 1;
    }
    bool categoryOk = desc->category == 7;
    std::printf(
        "typeId=141 entities.txt category: %d (expected 7/merchant -- not 2/monster, this is why "
        "main.cpp needed the new category-7 generalization) %s\n",
        desc->category, categoryOk ? "OK" : "FAILED");
    if (!categoryOk) ok = false;

    // --- Part 2: real Level global, GetEntity() hit + miss ---
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);

    std::string trothgarPath = std::string(scriptRoot) + "/monsters/Gravel_Trothgar.s";
    std::unique_ptr<sk_bindings::MonsterExecutable> trothgar;
    try {
        trothgar = LoadAndInit(trothgarPath, interpreter, &strings, stack.player(), stack);
    } catch (skParseException& e) {
        std::printf("m18_level_smoke: FAILED -- PARSE ERROR loading %s: %s\n", trothgarPath.c_str(),
                    e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m18_level_smoke: FAILED -- RUNTIME ERROR loading %s: %s\n", trothgarPath.c_str(),
                    e.toString().ptr());
        return 1;
    }
    stack.level().RegisterEntity("trthgar", trothgar.get());

    {
        skRValueArray args;
        args.append(skRValue(skString("trthgar")));
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        stack.level().method(skString("GetEntity"), args, ret, ctxt);
        bool hitOk = ret.type() == skRValue::T_Object && ret.obj() == trothgar.get();
        std::printf("Level.GetEntity(\"trthgar\") resolves to the real registered object: %s %s\n",
                    hitOk ? "true" : "false", hitOk ? "OK" : "FAILED");
        if (!hitOk) ok = false;
    }
    {
        skRValueArray args;
        args.append(skRValue(skString("m1")));
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        stack.level().method(skString("GetEntity"), args, ret, ctxt);
        // Real scripts (azra.s) compare this against the `null` global
        // game_constants.cpp registers -- both are the same blank default
        // skRValue(), so this isn't a T_Object at all (see level_
        // executable.h's comment for why that's what makes `!= null`
        // evaluate correctly in the real interpreter).
        bool missOk = ret.type() != skRValue::T_Object;
        std::printf(
            "Level.GetEntity(\"m1\") (never registered) resolves to the blank/\"null\" default: %s "
            "%s\n",
            missOk ? "true" : "false", missOk ? "OK" : "FAILED");
        if (!missOk) ok = false;
    }

    // --- Part 3: the payoff -- real quest completion, previously blocked ---
    std::string ratPath = std::string(scriptRoot) + "/monsters/Azra_Rat.s";
    for (int i = 0; i < 8; ++i) {
        std::unique_ptr<sk_bindings::MonsterExecutable> rat;
        try {
            rat = LoadAndInit(ratPath, interpreter, &strings, stack.player(), stack);
        } catch (skParseException& e) {
            std::printf("m18_level_smoke: FAILED -- PARSE ERROR loading %s: %s\n", ratPath.c_str(),
                        e.toString().ptr());
            return 1;
        } catch (skRuntimeException& e) {
            std::printf("m18_level_smoke: FAILED -- RUNTIME ERROR loading %s: %s\n", ratPath.c_str(),
                        e.toString().ptr());
            return 1;
        }
        rat->ApplyDamage(rat->maxHealth());
        rat->InvokeOnKilled();
    }
    bool killCountOk = stack.player().monstersKilled(203) == 8;
    std::printf("player.monstersKilled(203) after 8 kills: %d (expected 8) %s\n",
                stack.player().monstersKilled(203), killCountOk ? "OK" : "FAILED");
    if (!killCountOk) ok = false;
    bool questSolvedOk = stack.player().questSolved(0);
    std::printf(
        "player.questSolved(0) after 8th kill: %s (expected true -- M17's documented gap, now "
        "closed) %s\n",
        questSolvedOk ? "true" : "false", questSolvedOk ? "OK" : "FAILED");
    if (!questSolvedOk) ok = false;

    std::printf("\nm18_level_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
