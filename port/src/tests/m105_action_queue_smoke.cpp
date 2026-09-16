// M105 smoke test: the action queue.
//
//   FUN_100455b0  GetQueue(player, hand) = player + hand*0x1c + 0xf4c
//   FUN_1006c4c8/6a8/6f8/4d4/6d4/628   the container: At, IndexOf, Add,
//                 Remove (compacting), Clear, Move (swap)
//   FUN_1006c560/5c8                   rotate back / forward
//   FUN_1003f130  Player cases 0x43 ResetQueue, 0x44 MoveToLeftQueue,
//                 0x45 MoveToRightQueue, 0x46 MoveToOtherQueue,
//                 0x48 RemoveItemFromQueue
//   FUN_1003d8e0  the pickup tail that appends to the hand's queue
//   FUN_10033660  the equip screen's case 1: unequip removes, equip
//                 toggles, and a full queue is the "2" return
//   FUN_10033dd0  the screen's wcscmp chain: UpdateTextItems, GetLastItem,
//                 MoveItem (and M104's IsRightQueue)
//   FUN_10043308  the save routine that writes both lists
//
// Part 1  the container, against all six decompiled operations
// Part 2  what fills a queue: pickup, and the equip screen's toggle
// Part 3  the player's own five queue natives
// Part 4  the screen, driven through actionqueue.s itself
// Part 5  the save round trip
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "assets/save_records.h"
#include "assets/string_table.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/hand_queue.h"
#include "simkin_bindings/item_executable.h"
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
using Item = sk_b::ItemExecutable;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-92s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

// Six real right-hand weapons, so a five-slot queue can be overfilled.
const int kWeapons[] = {510, 511, 55, 512, 513, 514};

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m105_action_queue_smoke: FAILED to load stringtable.eng\n");
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
    auto one = [](skiExecutable* obj) {
        skRValueArray a;
        a.append(skRValue(obj, false));
        return a;
    };

    // ---------------------------------------------------------------
    // Part 1: the container
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: the five-slot container ==\n");
    {
        // Pointer identity is all the container cares about, so the
        // cheapest honest fixture is five real items.
        std::vector<std::unique_ptr<Item>> made;
        for (int typeId : kWeapons) {
            std::unique_ptr<Item> it = stack.level().CreateItem(typeId, false);
            if (it) made.push_back(std::move(it));
        }
        Check(made.size() == 6, "six real weapon scripts load");
        if (made.size() == 6) {
            sk_b::HandQueue q;
            Check(q.size() == 0 && q.At(0) == nullptr, "a fresh queue is empty");
            for (int i = 0; i < 5; ++i) Check(q.Add(made[static_cast<size_t>(i)].get()),
                                              i == 0 ? "Add fills the first free slot" : "  ...");
            Check(!q.Add(made[5].get()), "a sixth Add fails -- five slots, no growth");
            Check(q.size() == 5 && q.IndexOf(made[3].get()) == 3,
                  "IndexOf finds an item at its slot");
            Check(q.IndexOf(made[5].get()) == -1, "  and answers -1 for one that is not here");

            // Move is a swap, and both ends refuse.
            Check(!q.Move(made[0].get(), /*up=*/true),
                  "the first item cannot move up -- the engine's unsigned compare underflows");
            Check(!q.Move(made[4].get(), /*up=*/false), "and the last cannot move down");
            Check(q.Move(made[1].get(), true) && q.At(0) == made[1].get() &&
                      q.At(1) == made[0].get(),
                  "Move swaps with the neighbour rather than inserting");

            // Remove compacts -- the property UpdateTextItems relies on.
            Check(q.Remove(made[1].get()), "Remove takes it out");
            Check(q.At(0) == made[0].get() && q.At(3) == made[4].get() && q.At(4) == nullptr,
                  "  and shifts the tail down, clearing slot 4: a queue never has holes");
            Check(!q.Remove(made[5].get()), "removing something absent answers false");

            sk_b::HandQueue r;
            for (int i = 0; i < 3; ++i) r.Add(made[static_cast<size_t>(i)].get());
            Check(r.RotateBackward() == made[2].get() && r.At(1) == made[0].get(),
                  "RotateBackward brings the last filled entry to the front");
            Check(r.RotateForward() == made[0].get() && r.At(2) == made[2].get(),
                  "  and RotateForward puts it back");
            r.Clear();
            Check(r.size() == 0, "Clear empties all five");
        }
    }

    // ---------------------------------------------------------------
    // Part 2: what fills a queue
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: pickup and the equip screen ==\n");
    {
        const int before = player.queue(sk_b::kEquipSlotRight).size();
        std::unique_ptr<Item> sword = stack.level().CreateItem(kWeapons[0], false);
        Check(sword != nullptr, "a weapon loads");
        Item* raw = sword.get();
        if (sword) {
            player.AddItem(std::move(sword));
            Check(player.queue(sk_b::kEquipSlotRight).size() == before + 1 &&
                      player.queue(sk_b::kEquipSlotRight).Contains(raw),
                  "picking a weapon up puts it in the right hand's queue -- FUN_1003d8e0's tail");
            Check(player.QueueHoldingItem(raw) == 1, "  QueueHoldingItem names that hand");
        }
        // The equip screen's toggle: it is in the queue, so equipping takes
        // it back out; equipping again puts it back.
        if (raw) {
            Check(player.UpdateEquipStatus(raw, true) == 1 &&
                      !player.queue(sk_b::kEquipSlotRight).Contains(raw),
                  "the Equip row toggles a queued item out of the queue");
            Check(player.UpdateEquipStatus(raw, true) == 1 &&
                      player.queue(sk_b::kEquipSlotRight).Contains(raw),
                  "  and pressing it again puts it back");
        }
        // And the "2" the header called unreachable for fifty milestones.
        std::vector<Item*> filler;
        for (int typeId : kWeapons) {
            std::unique_ptr<Item> it = stack.level().CreateItem(typeId, false);
            if (!it) continue;
            Item* p = it.get();
            player.AddItem(std::move(it));
            filler.push_back(p);
        }
        int full = 0;
        for (Item* p : filler) {
            if (player.queue(sk_b::kEquipSlotRight).Contains(p)) continue;
            if (player.UpdateEquipStatus(p, true) == 2) ++full;
        }
        Check(player.queue(sk_b::kEquipSlotRight).size() == sk_b::HandQueue::kSlots,
              "the right queue fills to five");
        Check(full > 0, "  and the next Equip answers 2 -- the queue-full code, now reachable");
    }

    // ---------------------------------------------------------------
    // Part 3: the player's queue natives
    // ---------------------------------------------------------------
    std::printf("\n== Part 3: the player natives ==\n");
    {
        player.queue(sk_b::kEquipSlotLeft).Clear();
        player.queue(sk_b::kEquipSlotRight).Clear();
        std::unique_ptr<Item> sword = stack.level().CreateItem(kWeapons[1], false);
        std::unique_ptr<Item> potion = stack.level().CreateItem(711, false);  // a consumable
        std::unique_ptr<Item> key = stack.level().CreateItem(4005, false);    // misc
        Check(sword && potion && key, "a weapon, a consumable and a misc item load");
        if (sword && potion && key) {
            // Case 0x44 files by the ITEM's hand, not by its own name.
            Check(call(&player, "MoveToLeftQueue", one(sword.get())).boolValue(),
                  "MoveToLeftQueue accepts a weapon");
            Check(player.queue(sk_b::kEquipSlotRight).Contains(sword.get()),
                  "  and files it in the RIGHT queue -- it reads item+0x1c0, not its own name");
            // The two type refusals, which return without writing a value.
            Check(!call(&player, "MoveToLeftQueue", one(potion.get())).boolValue(),
                  "a consumable (type 4) is refused");
            Check(!call(&player, "MoveToLeftQueue", one(key.get())).boolValue(),
                  "a misc item (type 0) is refused");
            // Case 0x45 forces the right hand.
            player.queue(sk_b::kEquipSlotRight).Clear();
            Check(call(&player, "MoveToRightQueue", one(sword.get())).boolValue() &&
                      player.queue(sk_b::kEquipSlotRight).Contains(sword.get()),
                  "MoveToRightQueue forces hand 1");
            // 0x46: out of the one that has it, into the other.
            call(&player, "MoveToOtherQueue", one(sword.get()));
            Check(!player.queue(sk_b::kEquipSlotRight).Contains(sword.get()) &&
                      player.queue(sk_b::kEquipSlotLeft).Contains(sword.get()),
                  "MoveToOtherQueue moves it across");
            // 0x48: out of whichever holds it, but still in the bag.
            call(&player, "RemoveItemFromQueue", one(sword.get()));
            Check(player.QueueHoldingItem(sword.get()) == -1,
                  "RemoveItemFromQueue takes it out of both");
            // ResetQueue re-files by each item's own preferred hand.
            player.queue(sk_b::kEquipSlotLeft).Add(sword.get());
            skRValueArray hand;
            hand.append(skRValue(sk_b::kEquipSlotLeft));
            call(&player, "ResetQueue", hand);
            Check(player.queue(sk_b::kEquipSlotRight).Contains(sword.get()),
                  "ResetQueue empties that queue and re-files by item+0x1c0");
        }
    }

    // ---------------------------------------------------------------
    // Part 4: the screen
    // ---------------------------------------------------------------
    std::printf("\n== Part 4: actionqueue.s ==\n");
    {
        player.queue(sk_b::kEquipSlotLeft).Clear();
        player.queue(sk_b::kEquipSlotRight).Clear();
        std::vector<Item*> queued;
        for (int i = 0; i < 3; ++i) {
            std::unique_ptr<Item> it = stack.level().CreateItem(kWeapons[i], false);
            if (!it) continue;
            Item* p = it.get();
            player.AddItem(std::move(it));
            player.queue(sk_b::kEquipSlotRight).Clear();
            queued.push_back(p);
        }
        for (Item* p : queued) player.queue(sk_b::kEquipSlotRight).Add(p);
        Check(queued.size() == 3 && player.queue(sk_b::kEquipSlotRight).size() == 3,
              "three real weapons in the right queue");

        stack.OpenMenu("actionqueue");
        sk_b::MenuExecutable* screen = stack.currentMenu();
        Check(screen != nullptr, "actionqueue.s opens");
        if (screen && queued.size() == 3) {
            screen->SetQueueHandIsRight(true);
            screen->RunOnDisplay();  // its own OnDisplay ends in UpdateTextItems()
            int shown = 0, placeholders = 0;
            std::vector<const sk_b::MenuExecutable::MenuRow*> filled;
            for (const auto& row : screen->rows()) {
                if (row.kind != sk_b::MenuExecutable::RowKind::MenuItem || row.isQuitButton) {
                    continue;
                }
                if (row.visible) {
                    ++shown;
                    filled.push_back(&row);
                }
                // The engine hides an unused widget; it does not blank
                // it, so the two spare rows keep the script's "item" text
                // and simply stop being drawn. Only a *visible* one
                // showing it would be a bug.
                if (row.visible && row.literalText == "item") ++placeholders;
            }
            Check(shown == 3, "UpdateTextItems shows exactly the three queued items");
            Check(placeholders == 0,
                  "  and no visible row still reads the script's \"item\" placeholder");
            Check(filled.size() == 3 && filled[0]->literalText == queued[0]->name() &&
                      filled[2]->literalText == queued[2]->name(),
                  "  each row carries its item's real name, in queue order");
            Check(filled.size() == 3 &&
                      filled[1]->associatedObject == static_cast<skiExecutable*>(queued[1]),
                  "  and GetAssociatedObject's widget+0x90 points at the item");

            // GetLastItem is the last row it filled -- what ShowPopup uses
            // to decide whether to offer "Down".
            skRValueArray none;
            skRValue last = call(screen, "GetLastItem", none);
            Check(last.obj() != nullptr, "GetLastItem answers a widget");

            // MoveItem, through the screen's own ItemUp/ItemDown.
            skRValueArray up;
            up.append(skRValue(static_cast<skiExecutable*>(queued[2]), false));
            up.append(skRValue(true));
            Check(call(screen, "MoveItem", up).boolValue() &&
                      player.queue(sk_b::kEquipSlotRight).At(1) == queued[2],
                  "MoveItem(item, true) swaps it one place toward the front");
            skRValueArray topUp;
            topUp.append(skRValue(static_cast<skiExecutable*>(queued[0]), false));
            topUp.append(skRValue(true));
            Check(!call(screen, "MoveItem", topUp).boolValue(),
                  "  and refuses at the front, which is why ShowPopup blanks its Up row there");
        }
    }

    // ---------------------------------------------------------------
    // Part 5: the save
    // ---------------------------------------------------------------
    std::printf("\n== Part 5: quickSlots[2][5] ==\n");
    {
        // The wire format has carried both lists since M50 and this port
        // wrote one item per hand into them.
        const sk::SavedEntity rec = player.BuildSaveRecord("azra");
        int written = 0;
        for (const auto& list : rec.player.quickSlots) {
            for (int16_t id : list) {
                if (id > 0) ++written;
            }
        }
        Check(written == player.queue(sk_b::kEquipSlotLeft).size() +
                             player.queue(sk_b::kEquipSlotRight).size(),
              "every queued item reaches the save record, not just the wielded one");
    }

    std::printf("\nm105_action_queue_smoke: %d/%d checks passed -- %s\n", g_checks - g_failures,
                g_checks, g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
