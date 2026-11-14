// M136 smoke test: scripted category-1 placements are usable objects.
//
// Category 11 (doors) is category 1 plus a vtable swap -- FUN_1002e708
// calls FUN_10067998 and replaces the vtable -- so the door class is built
// on the plain usable world object. The port only made category 11 live,
// which left every scripted category-1 placement as scenery, with no use
// prompt and no OnUse. Two quest objects were among them:
//
//   * ghstpass `wthtree` (typeId 4000, ghstpass\witchtree.s): the Witch
//     Tree, whose OnUse opens the quest 14-16 conversation (the Star Tooth).
//   * broken1 `loot1` (a `!desk` carrying broken1\loot1.s): Sergeant
//     Bled's desk, whose menu hands over the Bribery Papers (quest 47).
//
// main.cpp now sends them down the door path (IsUsableObjectCategory).
// This test drives both scripts as that object, against the shipped data.
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/door_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
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

void Check(bool ok, const std::string& what) {
    std::printf("  %-80s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

bool EqualsNoCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m136_usable_object_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(root)) {
        std::printf("m136_usable_object_smoke: FAILED to load entities.txt\n");
        return 1;
    }
    skInterpreter interpreter;
    sk_b::MenuStack stack(root, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();

    auto call = [&](skiExecutable* obj, const char* name, skRValueArray args) {
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        try {
            obj->method(skString(name), args, ret, ctxt);
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR in %s: %s)\n", name, e.toString().ptr());
        }
        return ret;
    };
    auto loadObject = [&](const char* rel, const char* id) {
        std::unique_ptr<sk_b::DoorExecutable> obj;
        try {
            skExecutableContext ctxt(&interpreter);
            const std::string path = root + "/" + rel;
            obj = std::make_unique<sk_b::DoorExecutable>(skString(path.c_str()), ctxt, player);
            obj->SetEntityId(id);
            stack.level().RegisterEntity(id, obj.get());
            skRValueArray args;
            args.append(skRValue(0));
            call(obj.get(), "Init", args);
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR: %s)\n", e.toString().ptr());
        }
        return obj;
    };

    // ---- the category table itself ----
    std::printf("\n== entities.txt ==\n");
    {
        const sk::EntityTypeDescriptor* tree = entityTypes.Lookup(4000);
        Check(tree && tree->category == 1, "typeId 4000 (ghstpass\\witchtree.s) is category 1");
        const sk::EntityTypeDescriptor* desk = entityTypes.Lookup(18);
        Check(desk && desk->category == 1, "typeId 18 (!desk, Bled's desk) is category 1");
    }

    // ---- the Witch Tree ----
    std::printf("\n== Ghast's Pass: the Witch Tree ==\n");
    {
        auto tree = loadObject("ghstpass/witchtree.s", "wthtree");
        Check(tree != nullptr, "ghstpass/witchtree.s loads as the usable-object class");
        if (tree) {
            Check(tree->usable() && tree->useTextId() == 1493,
                  "its Init's SetUseText(1493) gives it a use prompt");
            const unsigned serial = stack.openSerial();
            tree->InvokeOnUse();
            sk_b::MenuExecutable* menu = stack.currentMenu();
            Check(stack.openSerial() != serial && menu &&
                      EqualsNoCase(menu->scriptName(), "WitchTree_convo"),
                  "OnUse opens ghstpass\\WitchTree_convo");
            Check(menu && menu->opener() == tree.get(), "...with the tree as its opener");
        }
    }

    // ---- Sergeant Bled's desk ----
    std::printf("\n== Broken Wing: Sergeant Bled's desk ==\n");
    {
        // broken1.s declares `BledDesk [0]`; loot1.s's Init reads it.
        stack.level().setValue(skString("BledDesk"), skString(), skRValue(0));
        auto desk = loadObject("broken1/loot1.s", "loot1");
        Check(desk != nullptr, "broken1/loot1.s loads as the usable-object class");
        if (desk) {
            Check(desk->usable() && desk->useTextId() == 442,
                  "with Level.BledDesk = 0 its Init makes it usable (SetUseText(442))");
            desk->InvokeOnUse();
            sk_b::MenuExecutable* menu = stack.currentMenu();
            Check(menu && EqualsNoCase(menu->scriptName(), "loot1_menu"),
                  "OnUse (Desk = 0) opens broken1\\loot1_menu -- the Bribery Papers");
            if (menu) {
                call(menu, "Take", skRValueArray());
                Check(player.questSolved(47), "Take solves quest 47 (evidence for Meya Violet)");
                skRValue deskVar;
                desk->getValue(skString("Desk"), skString(), deskVar);
                Check(deskVar.intValue() == 1, "...writes GetOpener().Desk = 1 on the desk");
                Check(!desk->usable(), "...and makes the desk unusable");
            }
        }
    }

    std::printf("\nm136_usable_object_smoke: %s\n", g_failures == 0 ? "PASSED" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
