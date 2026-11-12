// M134 smoke test: using one of a stack of consumables uses one.
//
//   FUN_100451ac  player vtable +0x174 (RemoveItem): a type-4 item whose
//                 quantity is above 1 only loses one (FUN_1002ecd8); the
//                 last one is unlinked from the inventory and the hands.
//
// Every consumable's OnUse ends in `DestroyObject(self)`, which for a
// carried item is that slot. The port removed the whole object, so drinking
// one of three healing potions -- from the left hand or the inventory's Use
// row -- lost all three.
//
// Part 1  three healing potions stack, and each drink takes one
// Part 2  the script's RemoveItem(templateId) takes one from a stack too
// Part 3  Rat's Eye -- same icon as a healing potion -- is the item behind
//         the HUD's "Mystic Might:" line (SetSpellEffect kind 1)
// Part 4  a named door-class placement with no script (delfhide's
//         `!portcullis` GTA) is a live object Level.GetEntity finds, and
//         Gate_Open_A's OpenGate takes it out of the world
// Part 5  FUN_10042490: the armour rating is a fraction of each worn
//         slot's value, one piece per slot -- not the sum of every piece
#include <cstdio>
#include <memory>
#include <string>

#include "assets/string_table.h"
#include "simkin_bindings/character_progression.h"
#include "simkin_bindings/door_executable.h"
#include "simkin_bindings/entity_base_ref.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/hand_queue.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

namespace sk_b = sk_bindings;
using Item = sk_b::ItemExecutable;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-92s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

constexpr int kHealingPotion = 702;  // items/healing_potion.s
constexpr int kRatsEye = 704;        // items/ratseye.s

int CountCarried(const sk_b::PlayerExecutable& player, int templateId) {
    int n = 0;
    for (const auto& item : player.inventory()) {
        if (item && !item->markedForRemoval() && item->templateId() == templateId) {
            n += item->quantity();
        }
    }
    return n;
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
        std::printf("m134_consumable_stack_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    entityTypes.Load(root);
    skInterpreter interpreter;
    sk_b::MenuStack stack(root, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();

    if (player.leftItem()) player.UpdateEquipStatus(player.leftItem(), false);
    player.queue(sk_b::kEquipSlotLeft).Clear();

    // ---------------------------------------------------------------
    std::printf("\n== Part 1: a stack of healing potions ==\n");
    {
        for (int i = 0; i < 3; ++i) {
            std::unique_ptr<Item> p = stack.level().CreateItem(kHealingPotion, false);
            if (p) player.AddItem(std::move(p));
        }
        Item* potion = player.leftItem();
        Check(potion && potion->templateId() == kHealingPotion && potion->quantity() == 3,
              "three pickups make one stack of 3, in the left hand");

        player.SetHealth(10);
        if (potion) potion->InvokeOnUsedBy(&player);  // the left-trigger arm
        player.PurgeRemovedItems();
        Check(CountCarried(player, kHealingPotion) == 2 && player.leftItem() == potion,
              "one drink: 2 left, the same stack still in hand");
        Check(player.actorHealth() == 40, "  and it healed (+30)");

        if (potion) potion->InvokeOnUsedBy(&player);
        player.PurgeRemovedItems();
        Check(CountCarried(player, kHealingPotion) == 1 && player.leftItem() == potion,
              "a second: 1 left");

        if (potion) potion->InvokeOnUsedBy(&player);
        player.PurgeRemovedItems();
        Check(CountCarried(player, kHealingPotion) == 0 && player.leftItem() == nullptr,
              "the last one goes, and the hand empties");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 2: RemoveItem(templateId) on a stack ==\n");
    {
        for (int i = 0; i < 2; ++i) {
            std::unique_ptr<Item> p = stack.level().CreateItem(kHealingPotion, false);
            if (p) player.AddItem(std::move(p));
        }
        skRValueArray args;
        args.append(skRValue(kHealingPotion));
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        player.method(skString("RemoveItem"), args, ret, ctxt);
        player.PurgeRemovedItems();
        Check(CountCarried(player, kHealingPotion) == 1, "RemoveItem(702) on a stack of 2 leaves 1");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 3: Rat's Eye is \"Mystic Might\" ==\n");
    {
        std::unique_ptr<Item> eye = stack.level().CreateItem(kRatsEye, false);
        std::unique_ptr<Item> heal = stack.level().CreateItem(kHealingPotion, false);
        Check(eye && heal && eye->icon() == heal->icon(),
              "items/ratseye.s and items/healing_potion.s draw the same icon (210)");
        if (eye) {
            Item* e = eye.get();
            player.AddItem(std::move(eye));
            e->InvokeOnUsedBy(&player);
            Check(player.actorStats().periodicKind() == 1,
                  "drinking it arms periodic kind 1 -- the HUD's \"Mystic Might:\" line");
        }
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 4: the Key A portcullis ==\n");
    {
        sk_b::DoorExecutable gate(&interpreter, player);
        gate.SetEntityId("GTA");
        Check(!gate.usable() && !gate.passable(), "a script-less door: unusable and shut");
        stack.level().RegisterEntity("GTA", &gate);
        std::unique_ptr<Item> key = stack.level().CreateItem(802, false);  // delfhide/KeyA_33.s
        if (key) player.AddItem(std::move(key));
        stack.OpenMenu("delfhide\\Gate_Open_A");
        sk_b::MenuExecutable* menu = stack.currentMenu();
        Check(menu != nullptr, "delfhide\\Gate_Open_A opens");
        if (menu) {
            skRValueArray noArgs;
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            try {
                menu->method(skString("OpenGate"), noArgs, ret, ctxt);
            } catch (skRuntimeException& e) {
                std::printf("   (RUNTIME ERROR in OpenGate: %s)\n", e.toString().ptr());
            }
        }
        skRValue flag;
        stack.level().getValue(skString("saved_GateA"), skString(), flag);
        Check(gate.entityRemoved(),
              "OpenGate: Level.GetEntity(\"GTA\").DestroyObjectMirror removes the gate");
        Check(flag.intValue() == 1, "  and runs on to Level.saved_GateA = 1");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 5: the armour rating a creature's hit subtracts ==\n");
    {
        {
            skRValueArray args;
            args.append(skRValue(0));  // an Argonian: no Nord roll in the rating
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            player.method(skString("ChooseRace"), args, ret, ctxt);
            skRValueArray classArgs;
            classArgs.append(skRValue(sk_b::kClassKnight));  // may wear all three weights
            player.method(skString("ChooseCharacter"), classArgs, ret, ctxt);
        }
        Item* worn[3] = {};
        const int kit[3] = {605, 646, 630};  // iron cuirass, iron shield, Dragonstar helm
        for (int i = 0; i < 3; ++i) {
            std::unique_ptr<Item> a = stack.level().CreateItem(kit[i], false);
            if (!a) continue;
            worn[i] = a.get();
            player.AddItem(std::move(a));
            player.UpdateEquipStatus(worn[i], true);
        }
        // 12*0x6600>>16 = 4 (type 1) + 19*25>>8 = 1 + 22*25>>8 = 2
        Check(worn[0] && worn[1] && worn[2] && player.armorRating() == 7,
              "cuirass 12 + shield 19 + helm 22 rate 4 + 1 + 2 = 7, not their sum (53)");
        std::unique_ptr<Item> coif = stack.level().CreateItem(614, false);  // chain coif, type 4
        Item* c = coif.get();
        if (coif) player.AddItem(std::move(coif));
        if (c) player.UpdateEquipStatus(c, true);
        Check(c && c->equipped() && worn[2] && !worn[2]->equipped() && player.armorRating() == 6,
              "a second helm takes the helm slot: the first comes off, 4 + 1 + 1 = 6");
    }

    std::printf("\nm134_consumable_stack_smoke: %d/%d checks passed\n", g_checks - g_failures,
                g_checks);
    return g_failures == 0 ? 0 : 1;
}
