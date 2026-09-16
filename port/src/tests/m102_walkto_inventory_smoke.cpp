// M102 smoke test: the two creature natives of audit item 4.
//
//   FUN_10003810  Actor case 0x1a AddInventoryItem, case 0x1e WalkTo
//   FUN_10005be4  vtable[0x1fc]: the move order WalkTo writes
//   FUN_10005ad4  vtable[0x204]: the arrival stop -- dead code, no caller
//   FUN_10084924  Monster case 8 AddSpell, whose three ownership lines
//                 AddInventoryItem repeats exactly
//
// What this checks:
//
//   1. `lakvan/highwaymage_cskye.s`, the one shipped caller: its two spells
//      end up in the creature's inventory and its spell table, owned by it
//      and out of the world, with no soft-fail left.
//   2. WalkTo sets the move order, the defaults are accepted and dropped,
//      and the order survives until something clears it.
//   3. `bbrawler_talkrun.s`'s OnDetect issues the walk, and its
//      ReachedDestination never runs -- the engine has no arrival test and
//      no such literal. The script is also unplaced: nothing in the shipped
//      data spawns it.
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/item_executable.h"
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

namespace sk_b = sk_bindings;
using Monster = sk_b::MonsterExecutable;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-92s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

bool FileContains(const std::string& path, const std::string& needle) {
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return text.find(needle) != std::string::npos;
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
        std::printf("m102_walkto_inventory_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    entityTypes.Load(root);
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
    auto one = [](skRValue v) {
        skRValueArray a;
        a.append(v);
        return a;
    };
    auto loadMonster = [&](const std::string& rel) -> std::unique_ptr<Monster> {
        try {
            skExecutableContext ctxt(&interpreter);
            const std::string path = root + "/" + rel;
            auto m = std::make_unique<Monster>(skString(path.c_str()), ctxt, &strings, player,
                                               stack);
            call(m.get(), "Init", one(skRValue(0)));
            return m;
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR loading %s: %s)\n", rel.c_str(), e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR loading %s: %s)\n", rel.c_str(), e.toString().ptr());
        }
        return nullptr;
    };

    // ---------------------------------------------------------------
    // Part 1: AddInventoryItem, through its one shipped caller
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: lakvan/highwaymage_cskye.s ==\n");
    {
        std::unique_ptr<Monster> mage = loadMonster("lakvan/highwaymage_cskye.s");
        Check(mage != nullptr, "the highway mage loads");
        if (mage) {
            // Its Init(): CreateEntity(4021) -> AddSpell(50); CreateEntity(4024)
            // -> AddInventoryItem then AddSpell(50).
            Check(mage->inventory().size() == 2,
                  "it owns both spells (one handed over twice, stored once)");
            Check(mage->spellSlot(0).spell != nullptr && mage->spellSlot(1).spell != nullptr,
                  "  and both are in its spell table");
            const sk_b::ItemExecutable* ignite = nullptr;
            for (const auto& held : mage->inventory()) {
                if (held && held->templateId() == 4024) ignite = held.get();
            }
            Check(ignite != nullptr, "  the AddInventoryItem one (typeId 4024) is in the list");
            if (ignite) {
                Check(ignite->entityRemoved(),
                      "  FUN_1001bf6c took it out of the world when it was handed over");
                Check(ignite->spellLevel() == 8, "  and the script's own SetLevel(8) still reached it");
            }
            bool slotted = false;
            for (int i = 0; i < Monster::kSpellSlots; ++i) {
                const sk_b::ItemExecutable* spell = mage->spellSlot(i).spell;
                if (spell && spell->templateId() == 4024) slotted = true;
            }
            Check(slotted, "  AddSpell still accepted it afterwards (same list, not a stranger)");
        }
    }

    // ---------------------------------------------------------------
    // Part 2: WalkTo
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: WalkTo ==\n");
    {
        std::unique_ptr<Monster> rat = loadMonster("monsters/azra_rat.s");
        Check(rat != nullptr, "a creature loads");
        if (rat) {
            Check(!rat->walkOrderActive(), "it starts with no move order");
            skRValueArray args;
            args.append(skRValue(5760));
            args.append(skRValue(13696));
            call(rat.get(), "WalkTo", args);
            Check(rat->walkOrderActive() && rat->walkOrderX() == 5760 &&
                      rat->walkOrderY() == 13696,
                  "WalkTo(5760, 13696) sets the order at +0x1bc/+0x1c0 and raises +0x1b9");
            // The four-argument form: the extras go to fields nothing reads.
            skRValueArray four;
            four.append(skRValue(100));
            four.append(skRValue(200));
            four.append(skRValue(1));
            four.append(skRValue(3));
            call(rat.get(), "WalkTo", four);
            Check(rat->walkOrderActive() && rat->walkOrderX() == 100 && rat->walkOrderY() == 200,
                  "  the a3/a4 form is accepted and moves the goal");
            rat->ClearWalkOrder();
            Check(!rat->walkOrderActive(), "the AI tick's own `+0x1b9 = 0` drops it");
        }
    }

    // ---------------------------------------------------------------
    // Part 3: the script it was written for
    // ---------------------------------------------------------------
    std::printf("\n== Part 3: bbrawler_talkrun.s ==\n");
    {
        std::unique_ptr<Monster> brawler = loadMonster("bbrawler_talkrun.s");
        Check(brawler != nullptr, "bbrawler_talkrun.s loads");
        if (brawler) {
            Check(!brawler->aggressive(), "its Init leaves it non-aggressive");
            brawler->InvokeOnDetect(static_cast<skiExecutable*>(&player));
            Check(brawler->walkOrderActive() && brawler->walkOrderX() == 5760 &&
                      brawler->walkOrderY() == 13696,
                  "OnDetect's WalkTo(5760, 13696) reaches the creature");
            // ReachedDestination would SetAggressive(true). Nothing in the
            // engine ever calls it: no arrival test, and no such literal.
            Check(!brawler->aggressive(),
                  "  and its ReachedDestination never runs -- the engine has no arrival test");
        }
        Check(!FileContains(root + "/entities.txt", "bbrawler_talkrun"),
              "nothing in entities.txt names the script: it is unplaced in the shipped game");
    }

    std::printf("\nm102_walkto_inventory_smoke: %d/%d checks passed -- %s\n", g_checks - g_failures,
                g_checks, g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
