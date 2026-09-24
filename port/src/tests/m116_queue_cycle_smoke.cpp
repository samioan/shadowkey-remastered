// M116 smoke test: the cycle keys and the consumable in the hand.
//
//   FUN_1001c9c0  the input poll: action 10 -> player vtable +0x270,
//                 action 11 -> +0x26c (both edges)
//   FUN_100a0934  +0x26c: RotateForward(player+0xf4c), SetLeftItem(head)
//   FUN_100a0968  +0x270: RotateForward(player+0xf68), SetRightItem(head)
//   FUN_10042394  case 4: item vtable +0x90 (OnUsedBy), returns 0
//   FUN_10044c04 / FUN_10044c50  on that 0, InputState_SetBoundButton(0)
//
// Part 1  the right queue cycles through its weapons and wields each
// Part 2  an empty queue leaves the hand alone
// Part 3  the left queue cycles potions, and a potion in hand is drunk
// Part 4  the released key stays up through auto-repeat
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "engine/input_state.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/hand_queue.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "skInterpreter.h"
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

const int kWeapons[] = {510, 511, 55};
constexpr int kHealingPotion = 702;  // items/healing_potion.s: +30 health, DestroyObject
constexpr int kMagickaPotion = 703;  // items/magicka_potion.s
constexpr int kBread = 700;          // items/bread.s
constexpr int kBlaze = 50;           // the starting spell

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m116_queue_cycle_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    entityTypes.Load(root);
    skInterpreter interpreter;
    sk_b::MenuStack stack(root, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();

    // Start from empty hands and queues, whatever the starting kit was.
    auto clearHands = [&]() {
        if (player.rightItem()) player.UpdateEquipStatus(player.rightItem(), false);
        if (player.leftItem()) player.UpdateEquipStatus(player.leftItem(), false);
        player.queue(sk_b::kEquipSlotLeft).Clear();
        player.queue(sk_b::kEquipSlotRight).Clear();
    };
    clearHands();

    // ---------------------------------------------------------------
    std::printf("\n== Part 1: Cycle Right Queue ==\n");
    {
        std::vector<Item*> w;
        for (int typeId : kWeapons) {
            std::unique_ptr<Item> it = stack.level().CreateItem(typeId, false);
            if (!it) continue;
            w.push_back(it.get());
            player.AddItem(std::move(it));
        }
        Check(w.size() == 3, "three real weapons load");
        if (w.size() == 3) {
            Check(player.rightItem() == w[0], "the first pickup is wielded");
            // The other two are carried unqueued (the hand was full); the
            // Equip row queues them without touching the hand.
            player.UpdateEquipStatus(w[1], true);
            player.UpdateEquipStatus(w[2], true);
            const sk_b::HandQueue& q = player.queue(sk_b::kEquipSlotRight);
            Check(q.size() == 3 && player.rightItem() == w[0],
                  "Equip on the other two queues them and leaves the hand alone");

            Check(player.CycleQueue(sk_b::kEquipSlotRight) == w[1] && player.rightItem() == w[1],
                  "one cycle wields the second weapon");
            Check(w[1]->equipped() && !w[0]->equipped(),
                  "  and moves the equipped flag with it");
            Check(q.At(0) == w[1] && q.At(2) == w[0], "  the old head goes to the back");
            player.CycleQueue(sk_b::kEquipSlotRight);
            Check(player.rightItem() == w[2], "a second cycle wields the third");
            player.CycleQueue(sk_b::kEquipSlotRight);
            Check(player.rightItem() == w[0], "a third comes back round to the first");
            Check(player.leftItem() == nullptr, "the left hand was never touched");
        }
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 2: an empty queue ==\n");
    {
        Item* before = player.leftItem();
        Check(player.CycleQueue(sk_b::kEquipSlotLeft) == nullptr && player.leftItem() == before,
              "cycling an empty queue changes nothing -- FUN_100a0934's `if (head)`");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 3: potions in the left hand ==\n");
    {
        std::unique_ptr<Item> heal = stack.level().CreateItem(kHealingPotion, false);
        std::unique_ptr<Item> mana = stack.level().CreateItem(kMagickaPotion, false);
        Check(heal && mana, "both potions load");
        if (heal && mana) {
            Item* h = heal.get();
            Item* m = mana.get();
            // What the hand-use switch reads. A template-spawned consumable
            // *does* answer a nonzero spellTypeId() (the template-id
            // fallback), which is why main.cpp's spell arm must test the
            // type alone -- it used not to, and every potion was "cast".
            Check(h->itemType() == sk_b::kItemTypeConsumable && h->spellTypeId() != 0,
                  "a real potion is type 4, and spellTypeId() is nonzero -- not a spell test");
            std::unique_ptr<Item> bread = stack.level().CreateItem(kBread, false);
            Check(bread && bread->itemType() == sk_b::kItemTypeConsumable,
                  "  items/bread.s is type 4 too");
            std::unique_ptr<Item> blaze = stack.level().CreateItem(kBlaze, false);
            Check(blaze && blaze->itemType() == sk_b::kItemTypeSpell,
                  "  while a real spell (Blaze) is type 2 on its own");
            player.AddItem(std::move(heal));
            player.AddItem(std::move(mana));
            Check(player.leftItem() == h, "the first potion picked up goes in the left hand");
            player.UpdateEquipStatus(m, true);
            Check(player.queue(sk_b::kEquipSlotLeft).size() == 2, "  the second joins the queue");
            player.CycleQueue(sk_b::kEquipSlotLeft);
            Check(player.leftItem() == m, "Cycle Left Queue swaps to the magicka potion");
            player.CycleQueue(sk_b::kEquipSlotLeft);
            Check(player.leftItem() == h, "  and back to the healing one");

            player.SetHealth(10);
            const int before = player.health();
            h->InvokeOnUsedBy(&player);
            Check(player.health() == before + 30, "using the potion in hand runs its OnUse: +30");
            Check(h->markedForRemoval(), "  and its DestroyObject consumes it");
            player.PurgeRemovedItems();
            Check(player.leftItem() == nullptr &&
                      player.queue(sk_b::kEquipSlotLeft).size() == 1,
                  "  leaving the hand empty and one potion in the queue");
            Check(player.CycleQueue(sk_b::kEquipSlotLeft) == m && player.leftItem() == m,
                  "a cycle then wields the remaining potion");
        }
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 4: ReleaseBound ==\n");
    {
        sk::InputState input;
        input.SetButton(sk::ButtonSlot::Key7, true);
        Check(input.GetBoundButton(sk::Action::UseLeftAction), "Key7 held is the left-hand use");
        input.ReleaseBound(sk::Action::UseLeftAction);
        Check(!input.GetBoundButton(sk::Action::UseLeftAction), "ReleaseBound forces it up");
        input.SetButton(sk::ButtonSlot::Key7, true);  // Windows auto-repeat
        Check(!input.GetBoundButton(sk::Action::UseLeftAction) &&
                  !input.ConsumeBoundJustPressed(sk::Action::UseLeftAction),
              "  and an auto-repeat key-down does not press it again");
        input.SetButton(sk::ButtonSlot::Key7, false);
        input.SetButton(sk::ButtonSlot::Key7, true);
        Check(input.GetBoundButton(sk::Action::UseLeftAction) &&
                  input.ConsumeBoundJustPressed(sk::Action::UseLeftAction),
              "a real release and press work again");
        Check(input.GetBoundButton(sk::Action::MoveForward) == false,
              "  other actions are untouched");
    }

    std::printf("\nm116_queue_cycle_smoke: %d/%d checks passed\n", g_checks - g_failures,
                g_checks);
    return g_failures == 0 ? 0 : 1;
}
