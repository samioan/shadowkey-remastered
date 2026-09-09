// M82 smoke test: gold from a loot bag reaching the player's purse.
//
// Reported once M81 made bags drop at all: "if an enemy drops gold pieces
// these don't get added to my gold total". Every link in the chain was
// already present -- `loot_gold25-35.s` built its `Level.CreateEntity(52)`,
// rolled `Random(25, 35)` into the object's quantity and `AddObject`ed it;
// `lootmenu.s` listed it as "27 Gold" and its SelectItem() ran
// `GetPlayer().PickupItem(Object)` then `GetOpener().RemoveObject(Object)`;
// and RemoveObject() handed the live object to PlayerExecutable::AddItem().
// The one missing link was inside AddItem itself.
//
// Gold is not special anywhere in the script data. `gold.s` is two lines
// (`SetID("gold")`, `SetName(1580)`) and `entities.txt` row 46 is an
// ordinary misc-item row. What makes it currency is a single hardcoded
// template id inside the engine's add-to-inventory path.
//
// Decompiled for this milestone:
//   FUN_1003d8e0  the add-to-inventory path (the player vtable's +0x164 --
//                 GiveItem, a world pickup and a loot-menu selection all
//                 funnel through it). Everything after M56's four key-item
//                 flag tests is wrapped in `if (item->+0xc8 != 0x34)`, and
//                 the else is `player+0x3ac+0x38 += item->+0x1c4` followed
//                 by FUN_1001b484(engine, item) -- add the quantity to the
//                 purse and destroy the object. Inside that `if`, and also
//                 missing from this port, is the consumable stacking loop.
//   FUN_1006d508  `item->+0x16c`, the item type word the stacking loop tests
//   FUN_1002ecd8  `SetQuantity` -- writes `item->+0x1c4`
//   FUN_1001b484  the destroy: sets `obj->+0x48` and queues the object on
//                 the engine's pending-free list (`engine+0xa58/+0xa5c`)
//   FUN_1003e030  BuyProduct, ported at M59 -- an independent read of the
//                 same purse, at `player+0x3e4`, which is exactly
//                 `0x3ac + 0x38`
//
// Part 1  entities.txt row 46, and gold.s -- nothing here says "currency"
// Part 2  every gold drop in the shipped corpus
// Part 3  the regression: what AddItem() used to do with a gold object
// Part 4  the real chain, end to end, exactly as lootmenu.s drives it
// Part 5  many bags: the roll is per-bag, and every coin is conserved
// Part 6  the other half of the same `if`: consumables stack
// Part 7  what gold must NOT do
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/amulet_flags.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "world/entity_types.h"

namespace {

int g_Checks = 0;
int g_Failed = 0;

void Check(bool cond, const char* what) {
    ++g_Checks;
    if (!cond) ++g_Failed;
    std::printf("  [%s] %s\n", cond ? "OK" : "FAILED", what);
}

// entities.txt line 46: `52 51 3 gold.s`.
constexpr int kGoldTypeId = 52;
constexpr int kGoldCategory = 3;  // misc -- the keys-and-quest-trinkets class
constexpr int kGoldModelIndex = 51;

// The bag typeId a creature's SetLoot names (M81), and the tag of the
// gold-only wrapper used below.
constexpr int kBagLootTypeId = 300;
const char* const kGoldBagTag = "Loot_Gold25-35";

// A `SetQuantity(...)` call site, paired with the `CreateEntity(...)` it
// belongs to. Collecting both is the point of Part 2: the claim is that
// the two always travel together and the entity is always gold.
struct QuantitySite {
    std::string file;
    int createdTypeId = -1;  // the nearest preceding CreateEntity argument
};

// Strip whitespace so `CreateEntity( 52 )` and `CreateEntity(52)` -- both
// spellings ship, 36 of one and 11 of the other -- compare equal.
std::string Squeeze(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (!std::isspace(static_cast<unsigned char>(c))) out += c;
    }
    return out;
}

// A plain scan of the shipped corpus rather than a transcribed list, so
// the check keeps meaning if the data is ever re-extracted.
std::vector<QuantitySite> CollectQuantitySites(const std::string& scriptRoot) {
    std::vector<QuantitySite> sites;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(scriptRoot, ec);
    if (ec) return sites;
    for (const std::filesystem::directory_entry& entry : it) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".s") continue;
        std::ifstream in(entry.path());
        std::stringstream buffer;
        buffer << in.rdbuf();
        const std::string text = Squeeze(buffer.str());
        for (size_t at = text.find("SetQuantity("); at != std::string::npos;
             at = text.find("SetQuantity(", at + 1)) {
            QuantitySite site;
            site.file = std::filesystem::relative(entry.path(), scriptRoot, ec).generic_string();
            // The entity whose quantity this is: the last CreateEntity
            // before it. In every real site it is the line immediately up.
            const size_t create = text.rfind("CreateEntity(", at);
            if (create != std::string::npos) {
                const size_t arg = create + sizeof("CreateEntity(") - 1;
                site.createdTypeId = std::atoi(text.c_str() + arg);
            }
            sites.push_back(site);
        }
    }
    return sites;
}

// One gold object, exactly as `loot_gold25-35.s`'s Init() makes it.
std::unique_ptr<sk_bindings::ItemExecutable> MakeGold(sk_bindings::MenuStack& stack, int quantity) {
    std::unique_ptr<sk_bindings::ItemExecutable> gold = stack.level().CreateItem(kGoldTypeId);
    if (gold) gold->SetQuantity(quantity);
    return gold;
}

// lootmenu.s's SelectItem(), in the two native calls it actually makes.
// Driven through method() rather than the C++ helpers, so this exercises
// the same entry points the interpreter uses.
void SelectItemFromBag(skInterpreter& interpreter, sk_bindings::PlayerExecutable& player,
                       sk_bindings::ItemExecutable& bag, skiExecutable* object) {
    skExecutableContext ctxt(&interpreter);
    skRValueArray args;
    args.append(skRValue(object, false));
    skRValue ret;
    player.method(skString("PickupItem"), args, ret, ctxt);
    bag.method(skString("RemoveObject"), args, ret, ctxt);
}

int TotalQuantityOf(const sk_bindings::ItemExecutable& bag, int templateId) {
    int total = 0;
    for (const std::unique_ptr<sk_bindings::ItemExecutable>& item : bag.contents()) {
        if (item && item->templateId() == templateId) total += item->quantity();
    }
    return total;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m82_gold_pickup_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m82_gold_pickup_smoke: FAILED to load entities.txt\n");
        return 1;
    }
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);

    // ---- Part 1: entities.txt row 46 ----
    std::printf("\nPart 1 -- typeId 52, and why nothing in the data marks it as money\n");
    {
        const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(kGoldTypeId);
        Check(desc != nullptr, "entities.txt has a row for typeId 52");
        if (desc) {
            std::printf("    row: %d %d %d %s\n", kGoldTypeId, desc->modelArchiveIndex,
                        desc->category, desc->name.c_str());
            Check(desc->category == kGoldCategory,
                  "typeId 52's category is 3 -- the plain misc-item class, the same one keys and "
                  "quest trinkets use; there is no currency category");
            Check(desc->modelArchiveIndex == kGoldModelIndex, "typeId 52's model index is 51");
            Check(desc->name == "gold.s",
                  "typeId 52 names a real script, so it is built by the ordinary item factory "
                  "like anything else");
        }
        Check(sk_bindings::kTemplateGold == kGoldTypeId,
              "kTemplateGold is 52 -- the literal 0x34 FUN_1003d8e0 compares against");

        std::unique_ptr<sk_bindings::ItemExecutable> gold = MakeGold(stack, 1);
        Check(gold != nullptr,
              "Level.CreateEntity(52) builds an item, which is how a bag script makes one");
        if (gold) {
            Check(gold->templateId() == kGoldTypeId,
                  "...and it carries template id 52 -- the only thing that distinguishes it from "
                  "any other misc item");
            Check(gold->id() == "gold", "...gold.s's own SetID ran");
            Check(gold->itemType() == sk_bindings::kItemTypeMisc,
                  "...its item type is Misc, not some distinct currency type: the stacking loop "
                  "in Part 6 only fires on Consumable, so it would never have caught gold either");
        }
    }

    // ---- Part 2: every gold drop in the shipped corpus ----
    std::printf("\nPart 2 -- the shipped gold drops\n");
    {
        std::vector<QuantitySite> sites = CollectQuantitySites(scriptRoot);
        std::printf("    %zu SetQuantity call site(s) found in the .s corpus\n", sites.size());
        Check(sites.size() == 48,
              "48 SetQuantity call sites, the count the corpus actually has -- if this number "
              "moves, the claims below are no longer about the same data");

        int nonGold = 0;
        std::string firstNonGold;
        for (const QuantitySite& site : sites) {
            if (site.createdTypeId == kGoldTypeId) continue;
            ++nonGold;
            if (firstNonGold.empty()) firstNonGold = site.file;
        }
        std::printf("    %d of them belong to something other than CreateEntity(52)%s%s\n", nonGold,
                    firstNonGold.empty() ? "" : ", first: ", firstNonGold.c_str());
        Check(nonGold == 0,
              "every SetQuantity in the game applies to a freshly created typeId 52 -- quantity "
              "*is* the gold amount, which is why the engine can read it straight into the purse");

        std::set<std::string> files;
        for (const QuantitySite& site : sites) files.insert(site.file);
        std::printf("    across %zu distinct script(s)\n", files.size());
        Check(files.size() == 48,
              "one gold drop per script, spread over 48 separate loot wrappers -- this was never "
              "about one bag");
    }

    // ---- Part 3: the regression ----
    std::printf("\nPart 3 -- what AddItem() used to do with a gold object\n");
    {
        sk_bindings::PlayerExecutable& player = stack.player();
        const int goldBefore = player.gold();
        const int countBefore = player.inventoryCount();

        std::unique_ptr<sk_bindings::ItemExecutable> gold = MakeGold(stack, 27);
        player.AddItem(std::move(gold));

        std::printf("    gold %d -> %d, inventory %d -> %d\n", goldBefore, player.gold(),
                    countBefore, player.inventoryCount());
        Check(player.gold() == goldBefore + 27,
              "picking up a 27-gold object raises the purse by exactly 27 -- before this "
              "milestone the purse never moved at all");
        Check(player.inventoryCount() == countBefore,
              "...and adds nothing to the inventory: the old behaviour left a permanent, "
              "unusable, unsellable \"Gold\" row in the bag, one per drop");

        // Guard the one thing an off-by-one here would silently break.
        std::unique_ptr<sk_bindings::ItemExecutable> more = MakeGold(stack, 3);
        player.AddItem(std::move(more));
        Check(player.gold() == goldBefore + 30,
              "a second pickup accumulates rather than replacing -- the real instruction is a "
              "read-modify-write of the purse, not a store");
    }

    // ---- Part 4: the real chain, end to end ----
    std::printf("\nPart 4 -- a real loot bag, driven exactly as lootmenu.s drives it\n");
    {
        std::unique_ptr<sk_bindings::ItemExecutable> bag =
            stack.level().CreateEntityWithScript(kBagLootTypeId, kGoldBagTag);
        Check(bag != nullptr, "loot_gold25-35.s loads as a real bag");
        if (bag) {
            Check(bag->usable(), "...its Init() ran (SetUsable(true))");
            Check(bag->contents().size() == 1, "...and it holds exactly one object");
            if (bag->contents().size() == 1) {
                sk_bindings::ItemExecutable* inside = bag->contents().front().get();
                const int rolled = inside->quantity();
                std::printf("    the bag holds %d x \"%s\" (template %d)\n", rolled,
                            inside->name().c_str(), inside->templateId());
                Check(inside->templateId() == kGoldTypeId, "...which is gold");
                Check(rolled >= 25 && rolled <= 35,
                      "...with a quantity inside Random(25, 35)'s inclusive range");

                sk_bindings::PlayerExecutable& player = stack.player();
                const int goldBefore = player.gold();
                const int countBefore = player.inventoryCount();
                SelectItemFromBag(interpreter, player, *bag, static_cast<skiExecutable*>(inside));
                std::printf(
                    "    after SelectItem(): gold %d -> %d, inventory %d -> %d, bag holds %zu\n",
                    goldBefore, player.gold(), countBefore, player.inventoryCount(),
                    bag->contents().size());
                Check(player.gold() == goldBefore + rolled,
                      "PickupItem() + RemoveObject() -- the two natives lootmenu.s actually calls "
                      "-- move exactly the rolled amount into the purse");
                Check(player.inventoryCount() == countBefore, "...and leave the inventory alone");
                Check(bag->contents().empty(),
                      "...and empty the bag, so lootmenu.s's UpdateMenu() takes its "
                      "`Opener.GetFirst() = null` branch and closes the menu");
            }
        }
    }

    // ---- Part 5: the roll is per-bag, and conserved ----
    std::printf("\nPart 5 -- many bags: the roll varies, and no coin is lost\n");
    {
        sk_bindings::PlayerExecutable& player = stack.player();
        const int goldBefore = player.gold();
        int bags = 0, expected = 0, lo = 1 << 30, hi = 0;
        std::set<int> distinct;
        for (int i = 0; i < 400; ++i) {
            std::unique_ptr<sk_bindings::ItemExecutable> bag =
                stack.level().CreateEntityWithScript(kBagLootTypeId, kGoldBagTag);
            if (!bag || bag->contents().size() != 1) continue;
            ++bags;
            const int rolled = TotalQuantityOf(*bag, kGoldTypeId);
            expected += rolled;
            distinct.insert(rolled);
            lo = (std::min)(lo, rolled);
            hi = (std::max)(hi, rolled);
            SelectItemFromBag(interpreter, player, *bag,
                              static_cast<skiExecutable*>(bag->contents().front().get()));
        }
        std::printf("    %d bags, rolls %d..%d over %zu distinct value(s), total %d\n", bags, lo,
                    hi, distinct.size(), expected);
        Check(bags == 400, "all 400 bags loaded and were filled");
        Check(lo >= 25 && hi <= 35, "every roll stayed inside Random(25, 35)");
        Check(distinct.size() > 5,
              "the amount really is rolled per bag rather than fixed -- Random() is re-evaluated "
              "in each bag's own Init()");
        Check(player.gold() == goldBefore + expected,
              "the purse gained exactly the sum of the 400 rolls: nothing double-counted, nothing "
              "dropped");
    }

    // ---- Part 6: the other half of the same `if` ----
    std::printf("\nPart 6 -- consumables stack (FUN_1003d8e0's inventory walk)\n");
    {
        // A fresh player, so the counts below are unambiguous.
        skInterpreter interp2;
        sk_bindings::MenuStack stack2(scriptRoot, interp2, &strings);
        stack2.level().SetEntityTypes(&entityTypes);
        sk_bindings::PlayerExecutable& player = stack2.player();

        // Two real consumable templates and a real weapon template, found
        // in the shipped table rather than asserted by id.
        int consumableId = -1, otherConsumableId = -1, weaponId = -1;
        for (int typeId = 1; typeId < 1200; ++typeId) {
            const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(typeId);
            if (!desc || !sk_bindings::IsInventoryItemCategory(desc->category)) continue;
            const int type = sk_bindings::ItemTypeForCategory(desc->category);
            if (type == sk_bindings::kItemTypeConsumable) {
                if (consumableId < 0) {
                    consumableId = typeId;
                } else if (otherConsumableId < 0) {
                    otherConsumableId = typeId;
                }
            } else if (type == sk_bindings::kItemTypeWeapon && weaponId < 0) {
                weaponId = typeId;
            }
            if (consumableId >= 0 && otherConsumableId >= 0 && weaponId >= 0) break;
        }
        std::printf("    using consumables %d and %d, weapon %d\n", consumableId, otherConsumableId,
                    weaponId);
        Check(consumableId >= 0 && otherConsumableId >= 0 && weaponId >= 0,
              "entities.txt supplies two consumable templates and a weapon template to test with");

        if (consumableId >= 0 && otherConsumableId >= 0) {
            const int before = player.inventoryCount();
            for (int i = 0; i < 3; ++i) {
                std::unique_ptr<sk_bindings::ItemExecutable> item =
                    stack2.level().CreateItem(consumableId);
                if (item) player.AddItem(std::move(item));
            }
            int rows = 0, stacked = 0;
            for (const std::unique_ptr<sk_bindings::ItemExecutable>& held : player.inventory()) {
                if (held && held->templateId() == consumableId) {
                    ++rows;
                    stacked = held->quantity();
                }
            }
            std::printf("    3 x template %d -> %d row(s), quantity %d\n", consumableId, rows,
                        stacked);
            Check(player.inventoryCount() == before + 1,
                  "three of the same consumable make ONE inventory row, not three -- the engine "
                  "walks the inventory and merges; this port only did it on the buy path");
            Check(rows == 1 && stacked == 3,
                  "...and that row's quantity is 3, incremented by one per pickup (the real "
                  "instruction is `SetQuantity(held, held->quantity + 1)`)");

            const int beforeOther = player.inventoryCount();
            std::unique_ptr<sk_bindings::ItemExecutable> other =
                stack2.level().CreateItem(otherConsumableId);
            if (other) player.AddItem(std::move(other));
            Check(player.inventoryCount() == beforeOther + 1,
                  "a *different* consumable does not stack onto it: the merge is keyed on the "
                  "template id, not merely on the type");
        }

        if (weaponId >= 0) {
            const int before = player.inventoryCount();
            for (int i = 0; i < 2; ++i) {
                std::unique_ptr<sk_bindings::ItemExecutable> item =
                    stack2.level().CreateItem(weaponId);
                if (item) player.AddItem(std::move(item));
            }
            Check(player.inventoryCount() == before + 2,
                  "two identical weapons stay two rows -- the merge only fires for item type 4, "
                  "so duplicate weapons and armour remain separate objects with their own "
                  "ratings");
        }
    }

    // ---- Part 7: what gold must not do ----
    std::printf("\nPart 7 -- gold takes none of the other paths through AddItem()\n");
    {
        skInterpreter interp3;
        sk_bindings::MenuStack stack3(scriptRoot, interp3, &strings);
        stack3.level().SetEntityTypes(&entityTypes);
        sk_bindings::PlayerExecutable& player = stack3.player();

        std::unique_ptr<sk_bindings::ItemExecutable> gold = MakeGold(stack3, 500);
        player.AddItem(std::move(gold));
        Check(player.gold() == 500, "a fresh player's purse holds the 500 that went in");
        Check(player.inventoryCount() == 0, "...and the inventory is empty");
        Check(player.leftItem() == nullptr && player.rightItem() == nullptr,
              "...and neither hand was filled: the gold branch returns before M74's equip tail, "
              "so a pile of coins can never be wielded");
        Check(!player.keyItemFlags().test(sk_bindings::kFlagRedAmulet) &&
                  !player.keyItemFlags().test(sk_bindings::kFlagFrozenKey),
              "...and none of M56's key-item flags moved: 52 matches no key item, and the four "
              "tests still run ahead of the gold branch exactly as they do in the original");
    }

    std::printf("\nm82_gold_pickup_smoke: %d check(s), %d failed -- %s\n", g_Checks, g_Failed,
                g_Failed == 0 ? "OK" : "FAILED");
    return g_Failed == 0 ? 0 : 1;
}
