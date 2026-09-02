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
// The AR_*/WR_* per-category sub-constants (armor weight class, weapon
// damage type) were never cross-referenced against real data the way the
// IPT_* set was -- their exact values are a deliberate, documented
// placeholder (arbitrary but internally consistent small ints), same as
// this project's other "known unconfirmed, not worth blocking on"
// decisions (e.g. render3d/camera.h's kEyeHeightOffset). Nothing in this
// port's own logic reads them back, so any distinct values would behave
// identically -- they only need to *exist* so scripts that reference them
// don't throw.

class skInterpreter;

namespace sk_bindings {

constexpr int kItemTypeMisc = 0;
constexpr int kItemTypeWeapon = 1;
constexpr int kItemTypeSpell = 2;
constexpr int kItemTypeArmor = 3;
constexpr int kItemTypeConsumable = 4;

// Registers every bare-identifier constant this port's curated starting-
// inventory scripts (see PlayerExecutable::LoadStartingInventory) are known
// to reference. Idempotent -- safe to call once per skInterpreter instance,
// which is what every entry point (main.cpp, each relevant smoke test) does
// right after constructing its interpreter.
void RegisterGameConstants(skInterpreter& interpreter);

}  // namespace sk_bindings
