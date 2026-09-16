#pragma once

// M10: registers the bare-identifier enum constants real item/weapon/armor
// scripts reference (e.g. armor/chain_coif.s's SetArmorConstraint(AR_Medium),
// weapons/club.s's SetWeaponType(WR_Blunt)) as Simkin global variables via
// skInterpreter::addGlobalVariable -- without this, parsing/running those
// scripts throws on the first unresolved bare identifier.
//
// The item-*type* values (kItemTypeWeapon/Spell/Armor/Consumable below) are
// real, not guessed: confirmed against actual corpus data --
// buysell.s/dstar_e/blk_market.s's own AddProduct(..., IPT_Weapon) /
// IPT_Spell / IPT_Armor / IPT_Consumable calls use literal 1/2/3/4
// (buysell.s's own comments spell this out: "GetProductCount(1); //
// IPT_Weapon", "(3); //IPT_Armor", "(4); //IPT_Consumable", "(2); //
// IPT_Spell"), and inventory.s's SelectedInventoryItem independently
// compares GetItemType() against the literals 3 (armor) and 4
// (consumable). kItemTypeMisc=0 is inferred (inventory.s's only other
// branch, "type = 0", gated on CanTravel()) rather than confirmed the same
// way.
//
// The AR_*/SR_*/WR_* per-category sub-constants (armor weight class,
// weapon class) were placeholders until M58 read the real global bootstrap
// out of the binary: they are **bit flags**, and every one of them is now
// the shipped value (simkin_bindings/effects.cpp's table). M74 is the first
// thing that reads them back -- PlayerExecutable::IsItemEnabledFor()'s
// armour and weapon arms test them against the class table's own masks.

class skInterpreter;

namespace sk_bindings {

constexpr int kItemTypeMisc = 0;
constexpr int kItemTypeWeapon = 1;
constexpr int kItemTypeSpell = 2;
constexpr int kItemTypeArmor = 3;
constexpr int kItemTypeConsumable = 4;

// ---- M74: where an item's type actually comes from ----
//
// It is **not** inferred. `GetItemType()` reads one stored word,
// `entity+0x16c` (`FUN_1006d508`), and so does everything that sorts an
// inventory: the five `Display*Page` natives all funnel into
// `FUN_10032f78`, whose entire filter is `FUN_1006d508(item) == page`.
//
// That word is a per-C++-class constant written by the class's own
// constructor, and the class is chosen by the **`entities.txt` category
// column** through the factory `FUN_1002aa14` (the seventeen-arm table in
// docs/ZONE_FORMAT.md). So the category *is* the item type, one step
// removed:
//
//   | cat | constructor    | `+0x16c` | `+0x1c0` | what it is           |
//   |-----|----------------|----------|----------|----------------------|
//   |  3  | `FUN_1002eeb8` | 0 misc   | 2 none   | keys, quest trinkets |
//   |  4  | `FUN_1002ce9c` | 1 weapon | 1 right  | `weapons\*`          |
//   |  5  | `FUN_10047740` | 2 spell  | 1 right  | `spells\*`, blaze.s  |
//   |  6  | `FUN_1002e954` | 3 armor  | 2 none   | `armor\*`            |
//   |  9  | `FUN_1002e78c` | 4 consum | 0 left   | `items\*`            |
//   | 14  | `FUN_1002e5ac` | 2 spell  | 1 right  | scrolls (see below)  |
//   | 15  | `FUN_1002e844` | 3 armor  | 2 none   | shields              |
//   | 16  | `FUN_10047500` | 1 weapon | 1 right  | the conjured sword   |
//
// Two of those arms are derived classes and inherit the value they do not
// set: 14 runs `FUN_10047740` first (hence spell) and only adds the scroll
// flag `+0x1d4`, and 16 runs `FUN_1002ce9c` (hence weapon). The base item
// constructor `FUN_1006c960` writes `1`, which is why every arm that wants
// something else overwrites it a few instructions later.
//
// Verified against the shipped `entities.txt`: 80 of category 4's 83 rows
// are under `weapons\`, 28 of category 5's 31 under `spells\`, 77 of
// category 6's 89 under `armor\`, 55 of category 9's 58 under `items\`,
// and all 7 of category 14 and 9 of category 15's 10 under `spells\` and
// `armor\` respectively.
//
// This port used to *infer* the type from whichever category setter a
// script happened to call (`SetArmorValue` -> armor, `SetDamageMin` ->
// weapon, `SetUsable` -> consumable). Nothing a spell script calls implies
// "spell", so `kItemTypeSpell` was never produced by anything and every
// spell in the game reported Misc -- which is the whole of M74's bug.
int ItemTypeForCategory(int entityCategory);

// ---- M79: the rest of what those constructors write ----
//
// The table above stopped at `+0x16c` and `+0x1c0` because those are the
// two fields M74 needed. Three more of the same writes decide what a held
// item *looks like*, and the port had none of them -- which is the whole of
// "the sprite that shows my hands reading the spell is missing".
//
// | class | ctor | `+0x19c` sprite | `+0x180` frames | `+0x184` scale |
// |-------|------|-----------------|-----------------|----------------|
// | base item | `FUN_1006c960` | 0 | 0 | `0x100` |
// | weapon (4, 16) | `FUN_1002ce9c` | -- | -- | **`0x300`** |
// | spell (5, 14) | `FUN_10047740` | **`0x98`** | **5** | -- |
//
// Both non-default rows are single `mov`/`str` pairs read straight out of
// the disassembly (`1002cf7c: mov r3, #0x300; str r3, [r4, #0x184]` and
// `10047768: mov r2, #0x98; str r2, [r4, #0x19c]` / `10047774: mov r3, #5;
// strb r3, [r4, r1]`), not decompiler output.
//
// So **every spell in the game already has a viewmodel** -- global.spr slot
// 152 plus a five-frame animation -- without a single spell script calling
// SetWeaponSprite. The slot is real and it is exactly where a spell belongs:
// the five melee weapon strips are 16 slots each at 72/88/104/120/136,
// 136 + 16 == 152, and slots 152..159 in the shipped global.spr are all
// full-screen 176x208 viewmodel frames (152 the smallest, i.e. the idle
// pose, 153..157 the cast).
constexpr int kSpellViewmodelSprite = 0x98;   // 152
constexpr int kSpellAnimationFrames = 5;

// `item+0x184`, the 8.8 scale on the swing accumulator's drain rate
// (weapon_viewmodel.h). M78 read the **base item** constructor's `0x100`
// and concluded the scale was the identity for every weapon in the game.
// It is not: the Weapon class constructor runs after the base one and
// overwrites it with `0x300`, so a real weapon's swing drains **three times
// as fast** as the raw `frameDelta * 4`. A spell keeps the identity, which
// is why a cast animation is a good deal slower than a sword swing.
constexpr int kDefaultReloadSpeed = 0x100;
constexpr int kWeaponReloadSpeed = 0x300;

// M93: the bag an inventory drop lands in. `FUN_1002c3a8` (M81's
// DropObject, shared with the drop-gold case) builds it with a
// hardcoded typeId **300** -- `entities.txt` line 212, `300 30 8
// !bag_loot`, category 8 (container), model 30 (`bag_dropped.bin`) --
// and the script tag **"Loot_Dropped"**, the literal at `0x100addf0`.
// `loot_dropped.s` is a real shipped script: SetName(456),
// SetUseText(457), SetUsable(true), and an OnUse() that opens
// LootMenu. So a dropped item is recoverable, which is the whole
// reason the drop is not a delete.
constexpr int kDroppedLootTypeId = 300;
constexpr const char* kDroppedLootScript = "Loot_Dropped";

// M81/M93: the `+ 300` both loot spawns put on the requested z -- the
// height the floor snap probes *from*, not a final offset (see
// Zone::SnapActorToGround for what the snap then does with it). A creature
// death drop (`FUN_10084438`) and an inventory drop (`FUN_1002c3a8`) use
// the same literal; shared here so they cannot drift apart. M93 hoisted it
// out of main.cpp's death handler, where M81 first wrote it.
constexpr float kLootDropRise = 300.0f;

// `entity+0x1c0`, from the same constructors -- which hand an item wants
// when it is picked up (`FUN_1003d8e0`) or equipped
// (`FUN_10033660` case 1). Not a preference the engine can be talked out
// of: an item whose slot is `kEquipSlotNone` is never put in a hand at
// all, and one whose slot is taken is simply not equipped.
constexpr int kEquipSlotLeft = 0;
constexpr int kEquipSlotRight = 1;
constexpr int kEquipSlotNone = 2;
int EquipSlotForCategory(int entityCategory);

// M103: the rest of what those same constructors write before a script's
// `Init()` ever runs. Every one of the eight item arms opens with the
// identical block (`FUN_1002eeb8`, and the weapon/armour/consumable arms
// repeat it verbatim rather than calling it):
//
//     +0x1b0 icon         = 0x1d   (0x3d for a spell, FUN_10047740)
//     +0x1b4 cost         = 0x1e
//     +0x1b8 marketValue  = 0x14
//     +0x1bc weight       = 1
//     +0x1c4 quantity     = 1
//     +0x1c9 canDrop      = 1
//
// which matters because a script only has to name what it wants to
// *change*. 185 of the 317 item scripts in the corpus never call
// `SetIcon` -- every piece of armour in the game -- and 46 never call
// `SetMarketValue`, `dagger.s` and `healwound.s` among them. This port
// started those at -1 and 0, so an unarmoured default drew no inventory
// or hand icon at all and ten droppable items sold to a merchant for
// nothing. The numbers are the engine's, not a house default.
constexpr int kItemDefaultIcon = 0x1d;
constexpr int kSpellDefaultIcon = 0x3d;
constexpr int kItemDefaultCost = 0x1e;
constexpr int kItemDefaultMarketValue = 0x14;

// Does this `entities.txt` category name one of the eight item classes
// above? Categories outside the table are creatures, doors, triggers and
// the rest of the world, which never enter an inventory.
bool IsInventoryItemCategory(int entityCategory);

// M60: the two labels the store/sell table's cost column is built from.
// They are stringtable ids in the engine's own code (`stringTable[0x2f7c/4]`
// and `[0x2f80/4]`), not literals -- and 3039 ships as "GP " with a
// trailing space, so the shipped line really reads "GP : 111 Qty: 3".
constexpr int kStoreGoldLabelStringId = 3039;
constexpr int kStoreQuantityLabelStringId = 3040;

// M60: global.spr slots the store table's comparison columns draw --
// "better than what you have" and "worse". They are the two ids
// `buysell.s` assigns to image_item_active/image_item_dormant at the top
// of OnDisplay and then never uses itself, because the native renderer
// (FUN_100a0cec) is what picks between them.
constexpr int kStoreBetterArrowSprite = 24;
constexpr int kStoreWorseArrowSprite = 25;

// M64: the sound FUN_10044618 plays when a level is gained -- a literal
// 0x57 in the engine, and slot 87 of every zone's `<zone>_sounds.txt` is
// `pl_cast_powerup.wav`.
constexpr int kLevelUpSoundSlot = 87;

// M82: `entities.txt` row 46 -- `52 51 3 gold.s`. Gold is an ordinary
// entity template like any other: a loot bag's script builds one with
// `Level.CreateEntity(52)`, gives it a `SetQuantity(Random(lo,hi))` and
// `AddObject`s it, and the loot menu shows it as an ordinary row. What
// makes it gold is a single hardcoded id in the add-to-inventory path --
// `FUN_1003d8e0`'s `if (item->templateId != 0x34)` -- which folds the
// quantity into the purse instead of keeping the object. Nothing in the
// script data marks it: `gold.s` is two lines (`SetID("gold")`,
// `SetName(1580)`) and says nothing about currency.
//
// All 48 `SetQuantity` call sites in the shipped corpus follow a
// `CreateEntity(52)`, so in practice quantity *is* the gold amount and
// nothing else in the game carries a stack size out of a script.
constexpr int kTemplateGold = 0x34;  // 52

// Registers every bare-identifier constant this port's curated starting-
// inventory scripts (see PlayerExecutable::LoadStartingInventory) are known
// to reference. Idempotent -- safe to call once per skInterpreter instance,
// which is what every entry point (main.cpp, each relevant smoke test) does
// right after constructing its interpreter.
void RegisterGameConstants(skInterpreter& interpreter);

}  // namespace sk_bindings
