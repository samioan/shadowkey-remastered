#pragma once

// M50: what is inside a save file's members.
//
// M40 decoded the container and left the payloads open. They turn out to
// be one recursive object graph, written through the byte stream in
// save_stream.h by a `Save`/`Load` pair sitting at **vtable slots +0x130
// and +0x134** of every serializable class. Thirty-two vtables carry that
// pair; between them they name thirteen distinct save functions, and
// those thirteen form a single inheritance chain, each one writing its
// own fields and then tail-calling its base's:
//
//     Entity            FUN_10066614 / FUN_10066cc4   (the root)
//      +- Drawable      FUN_10067db0 / FUN_10067ca0
//          +- Item      FUN_1006d35c / FUN_1006d2ac
//          |   +- Stackable  FUN_1002ed3c / FUN_1002ed04
//          |   |   +- Wearable  FUN_10047584 / FUN_1004754c
//          |   +- Weapon       FUN_1002eaa0 / FUN_1002ea0c
//          +- Spellbook FUN_10028a60 / FUN_10028be8
//              +- InventoryHolder  FUN_10005164 / FUN_10005320
//                  +- Actor        FUN_10086514 / FUN_10086454
//                  +- LinkedActor  FUN_10006f90 / FUN_10006ee4
//                  +- Character    FUN_1001e754 / FUN_1001ea54
//                      +- Player   FUN_10043308 / FUN_10043bb0
//
// plus one non-virtual leaf, the **stats block** (`FUN_1004a284` /
// `FUN_1004a6f4`), which Actor and Player each call on their own block
// (`actor+0x224` / `player+0x3ac`).
//
// The two members M40 named then fall out directly:
//
//   `character.dat`  = one Player record. Written by `FUN_10018e70`
//                      through the player's own slot +0x130; read back by
//                      `FUN_100195b0` through slot +0x134.
//   `<level>.dat`    = `FUN_100187d0` / `FUN_100188dc`: a flat, tagged
//                      list of every saveable entity in the level ---
//                      `u8 1; i16 typeId; <that entity's own record>`
//                      repeated, then `u8 0`, then one trailing engine
//                      byte (`engine+0xbe0e`).
//
// Three things worth knowing before reading the field lists:
//
// **Fields are written more than once.** `Drawable` re-writes seven
// fields (`+0x6c`, `+0x6d`, `+0x70`..`+0x80`) that `Entity` also writes,
// and `Actor` re-writes `+0x1e2` and `+0xd8` for the same reason. Both
// copies really are on the wire, in both orders, and the loader reads
// both. Reproduced, not folded.
//
// **Nothing is version-tagged.** There is no magic, no field count and no
// per-record length: a record is exactly as long as the class that wrote
// it, and a reader that picks the wrong class desynchronises the rest of
// the stream silently. Which class to use comes from the entity's
// **typeId**, written immediately before each record by whoever owns it
// (the level file, or an inventory holder) and resolved through
// entities.txt. That is why `SavedEntity` carries an explicit `kind`.
//
// **The two timestamps are stored relative.** `Entity` writes `+0x10c`
// and `+0x118` as `value - time(0)` and the loader adds its own
// `time(0)` back, so a wall-clock deadline survives being saved on one
// day and loaded on another. Everything else is stored as-is.

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "assets/save_stream.h"

namespace sk {

// FUN_10066614/FUN_10066cc4 each call time(0) once and use it for both of
// the entity root's deadlines. Exposed so a round-trip test can pin it
// rather than straddle a second boundary; -1 clears the override.
int32_t SaveClockNow();
void SetSaveClockOverride(int32_t seconds);

// ---------------------------------------------------------------------
// The stats block -- FUN_1004a284 / FUN_1004a6f4.
//
// 70 bytes, and *not* in field order: the writer emits +0x00..+0x0e,
// skips to +0x14..+0x2e, then doubles back for +0x10/+0x12 after the
// 32-bit experience/level/gold group. Field names below are the engine's
// own, from the character-stats dispatcher (FUN_10048244) and the
// stat-index switch (FUN_1004ad40) agreeing on the same halfword.
// ---------------------------------------------------------------------
struct SavedStats {
    int16_t attack = 0;            // +0x00  SetAttack / index 1
    int16_t defense = 0;           // +0x02  SetDefense / index 2
    int16_t spellcast = 0;         // +0x04  GetSpellcast / index 3
    int16_t magicResistance = 0;   // +0x06  GetMagicResistance / index 4
    int16_t damageMin = 0;         // +0x08  GetDamageMin / index 5
    int16_t damageMax = 0;         // +0x0a  GetDamageMax / index 6
    int16_t armorValue = 0;        // +0x0c  SetArmorValue / index 7
    int16_t expWorth = 0;          // +0x0e  GetExpWorth / index 8

    int16_t strength = 0;          // +0x14  GetStrength (no stat index)
    int16_t intelligence = 0;      // +0x16  index 0xa
    int16_t agility = 0;           // +0x18  index 0xb
    int16_t willpower = 0;         // +0x1a  index 0xc
    int16_t speed = 0;             // +0x1c  index 0xd
    int16_t endurance = 0;         // +0x1e  index 0xe
    int16_t personality = 0;       // +0x20  GetPersonality / index 0xf
    int16_t luck = 0;              // +0x22  GetLuck / index 0x10
    int16_t maxHealth = 0;         // +0x24  index 0x11
    int16_t maxFatigue = 0;        // +0x26  index 0x12
    int16_t maxMagicka = 0;        // +0x28  index 0x13
    int16_t health = 0;            // +0x2a  index 0x14
    int16_t fatigue = 0;           // +0x2c  index 0x15
    int16_t magicka = 0;           // +0x2e  index 0x16

    int32_t experience = 0;        // +0x30  GetExperience / index 0x17
    int16_t level = 0;             // +0x34  GetLevel / index 0x18
    int32_t gold = 0;              // +0x38  GetGold / index 0x19

    int16_t strengthBonus = 0;     // +0x10  GetStrengthBonus / index 9
    int16_t healthBonus = 0;       // +0x12  SetHealthBonus / ModHealthBonus

    uint32_t effectFlags = 0;      // +0x44  the timed effect-flag bitfield
    int16_t effect70 = 0;          // +0x70
    int16_t effect78 = 0;          // +0x78  the second periodic channel
    int16_t effectTimer = 0;       // +0x72  set by FUN_1004bae8
    int16_t dotKind = 0;           // +0x76  both the effect kind and its damage

    void Write(SaveStream& s) const;
    void Read(SaveStream& s);

    // The loader zeroes +0x70..+0x7a and +0x7c up front and then fills
    // only four of them, so three live fields are *not* restored:
    //   +0x74 the damage-over-time accumulator (harmless, it re-fills)
    //   +0x7a the second channel's accumulator (likewise)
    //   +0x7c the second channel's *kind*, set by SetSpellEffect
    // The third is not harmless: a regeneration or drain effect keeps its
    // timer (+0x78) across a save but loses the kind that made it do
    // anything, so it ticks down inert. Recorded, and reproduced -- there
    // is nowhere on the wire to put it.
    static constexpr int kSerializedBytes = 70;
};

// ---------------------------------------------------------------------
// The layers, one struct per save function. Each holds only the fields
// *that function itself* writes; the chain is assembled by SavedEntity.
// ---------------------------------------------------------------------

// One SimKin instance variable, as the entity root persists it.
// FUN_10066614 walks the script object's attribute list and writes each
// name/value pair as two wide strings -- but only when the *value* text
// contains no `{`, i.e. it skips anything that reads like a nested
// structure or a method body. So script state survives a save as text,
// which is how a door remembers it was unlocked: not by saving the tile
// bytes (the tile-change journal at level+0xfc is not serialized at all)
// but by re-running each entity's `Init` on load with its variables
// already restored. FUN_100188dc calls exactly that, by name.
struct SavedScriptVar {
    std::string name;
    std::string value;
};

struct SavedEntityBase {  // FUN_10066614 / FUN_10066cc4
    uint16_t entityId = 0;        // +0xc4
    int16_t f86 = 0;              // +0x86
    int32_t f8c = 0;              // +0x8c  a byte field, widened on the wire
    int16_t f8e = 0;              // +0x8e
    int16_t f90 = 0;              // +0x90
    std::string artName;          // +0xe2  the loader sprintf()s a path from it
    uint16_t fe0 = 0;             // +0xe0
    uint8_t f93 = 0;              // +0x93
    int32_t x = 0;                // +0x94  8.8 world units
    int32_t y = 0;                // +0x9c
    int16_t z = 0;                // +0xa4
    int16_t fb2 = 0;              // +0xb2
    int16_t pitch = 0;            // +0xa8
    int16_t yaw = 0;              // +0xb6
    std::string scriptName;       // +0xcb  re-registered with the object registry
    uint8_t fca = 0;              // +0xca
    uint8_t f92 = 0;              // +0x92
    uint8_t fd5 = 0;              // +0xd5
    uint8_t fd8 = 0;              // +0xd8
    uint8_t fd9 = 0;              // +0xd9
    uint32_t fdc = 0;             // +0xdc
    int16_t spriteId = 0;         // +0x4a  30000 = take it from the template
    uint8_t f6c = 0, f6d = 0;     // +0x6c, +0x6d
    uint32_t f70 = 0, f74 = 0, f78 = 0, f7c = 0;
    uint16_t f80 = 0;             // +0x80
    uint8_t f10a = 0;             // +0x10a
    int32_t deadline10c = 0;      // +0x10c, stored as (value - now)
    int16_t f110 = 0;             // +0x110
    uint8_t f114 = 0;             // +0x114
    int32_t deadline118 = 0;      // +0x118, likewise
    uint8_t f115 = 0;             // +0x115
    int16_t f60 = 0;              // +0x60
    uint8_t f11c = 0;             // +0x11c
    std::vector<SavedScriptVar> scriptVars;

    void Write(SaveStream& s) const;
    void Read(SaveStream& s);

    // Per variable: a 1 tag, then name and value. The list ends with
    // 0xff. The loader complains (but continues) on any other tag.
    static constexpr uint8_t kVarPresent = 0x01;
    static constexpr uint8_t kVarListEnd = 0xff;
};

struct SavedDrawable {  // FUN_10067db0 / FUN_10067ca0
    int32_t f12c = 0;   // +0x12c
    int32_t f130 = 0;   // +0x130
    int16_t f5e = 0;    // +0x5e
    uint8_t f6c = 0;    // +0x6c   -- second copy, see the header note
    uint32_t f74 = 0;   // +0x74
    uint32_t f78 = 0;   // +0x78
    uint16_t f80 = 0;   // +0x80
    uint32_t f7c = 0;   // +0x7c
    uint32_t f70 = 0;   // +0x70
    uint8_t f6d = 0;    // +0x6d

    void Write(SaveStream& s) const;
    void Read(SaveStream& s);
};

struct SavedSpellEntry {  // one element of the Spellbook list
    int16_t typeId = 0;      // the spell entity's own +0xc8
    int32_t charges = 0;     // its +0x1c4, or 0 when it is not an item
    bool hasEnchantment = false;
    uint16_t enchantment = 0;
};

struct SavedSpellbook {  // FUN_10028a60 / FUN_10028be8
    uint8_t f180 = 0;    // +0x180
    uint8_t f160 = 0;    // +0x160
    uint8_t f181 = 0;    // +0x181
    // +0x16c is the list's own count -- written as a plain i32 field and
    // read back as the loop bound, so it must agree with `spells`.
    std::vector<SavedSpellEntry> spells;

    void Write(SaveStream& s) const;
    void Read(SaveStream& s);
};

struct SavedItem {  // FUN_1006d35c / FUN_1006d2ac
    uint8_t f180 = 0;    // +0x180  SetAnimationFrames / SetReloadFrames share it
    int32_t f19c = 0;    // +0x19c
    int32_t f1a0 = 0;    // +0x1a0
    uint8_t usesRangedPath = 0;  // +0x1a7 -- SetRange(v > 0x400), see M49

    void Write(SaveStream& s) const;
    void Read(SaveStream& s);
};

struct SavedStackable {  // FUN_1002ed3c / FUN_1002ed04
    int32_t quantity = 0;  // +0x1c4
    void Write(SaveStream& s) const;
    void Read(SaveStream& s);
};

struct SavedWearable {  // FUN_10047584 / FUN_1004754c
    uint8_t f1d4 = 0;  // +0x1d4
    void Write(SaveStream& s) const;
    void Read(SaveStream& s);
};

struct SavedWeapon {  // FUN_1002eaa0 / FUN_1002ea0c
    int16_t damageMin = 0;  // +0x1cc  SetDamageMin
    int16_t damageMax = 0;  // +0x1ce  SetDamageMax
    int32_t f1d0 = 0;       // +0x1d0
    int32_t quantity = 0;   // +0x1c4  same field Stackable writes
    void Write(SaveStream& s) const;
    void Read(SaveStream& s);
};

struct SavedInventoryHolder {  // FUN_10005164 / FUN_10005320
    uint8_t f1e8 = 0;   // +0x1e8
    uint8_t f1b9 = 0;   // +0x1b9
    int32_t f1bc = 0;   // +0x1bc
    int32_t f1c0 = 0;   // +0x1c0
    int32_t f1c4 = 0;   // +0x1c4
    uint8_t f1e1 = 0;   // +0x1e1
    uint8_t f1e2 = 0;   // +0x1e2
    uint8_t f1e4 = 0;   // +0x1e4
    // The children are counted first (a full pass over the list asking
    // each one's vtable +0x104 whether it is saveable at all), then
    // written as `i32 typeId` + the child's own record. Note the typeId
    // is a *32-bit* field here and a 16-bit one in the level file.
    void Write(SaveStream& s) const;
    void Read(SaveStream& s);
};

struct SavedActor {  // FUN_10086514 / FUN_10086454
    uint8_t f2a8 = 0;   // +0x2a8  a 32-bit field truncated to a byte on save
    uint8_t f2ac = 0;   // +0x2ac
    uint8_t f1e2 = 0;   // +0x1e2  -- second copy
    uint8_t fd8 = 0;    // +0xd8   -- second copy
    int16_t f2ec = 0;   // +0x2ec
    void Write(SaveStream& s) const;
    void Read(SaveStream& s);
};

struct SavedLinkedActor {  // FUN_10006f90 / FUN_10006ee4
    // Two sub-objects' ids (their own +0x08), or -1 when absent. The
    // loader only tests them against -1 and rebuilds from a factory, so
    // the values themselves are write-only.
    int32_t link224 = -1;
    int32_t link228 = -1;
    void Write(SaveStream& s) const;
    void Read(SaveStream& s);
};

struct SavedCharacter {  // FUN_1001e754 / FUN_1001ea54
    // The first thing in character.dat, and the reason a load knows what
    // to load: the *level* name, not the character's. Single-player
    // writes the engine's current level ("Saving %s as our current
    // level"); a multiplayer session writes the player's original one
    // instead. FUN_1001ea54 strcpy()s it straight back into the engine.
    std::string levelName;
    uint8_t f398 = 0;    // +0x398
    int32_t f22c = 0;    // +0x22c
    uint8_t f358 = 0;    // +0x358
    uint8_t f359 = 0;    // +0x359
    uint8_t f35a = 0;    // +0x35a
    // +0x364/+0x368 are saved indirectly: the index of the linked entity
    // within the engine's own entity list, and that entity's typeId; or
    // -1/-1 when there is none. The loader parks them at +0x35c/+0x360
    // untouched and resolves them later (vtable +0x254).
    int32_t linkIndex = -1;
    int32_t linkTypeId = -1;
    int32_t f36c = 0, f370 = 0, f374 = 0;
    int32_t f378 = 0;    // +0x378, a halfword widened on the wire
    int32_t f37c = 0, f380 = 0, f384 = 0;
    void Write(SaveStream& s) const;
    void Read(SaveStream& s);
};

struct SavedPlayer {  // FUN_10043308 / FUN_10043bb0
    static constexpr int kQuestSlots = 256;
    static constexpr int kMonsterSlots = 256;
    static constexpr int kQuickLists = 2;
    static constexpr int kQuickSlots = 5;
    static constexpr int kEquipSlots = 8;
    static constexpr int kTrailingInts = 18;

    // --- head, written before the stats block and the base chain ---
    std::string characterName;  // +0xfcc, a descriptor, wide on the wire
    // Three parallel 256-byte arrays, named by the Player/GameState
    // dispatcher's own bindings (FUN_1003f130 cases 0x4c..0x51):
    //   +0x430 SetQuestAssigned / QuestAssigned
    //   +0x530 SetQuestCompleted / QuestCompleted
    //   +0x630 SetQuestSolved / QuestSolved
    // interleaved one index at a time, not three blocks.
    std::array<uint8_t, kQuestSlots> questAssigned{};
    std::array<uint8_t, kQuestSlots> questCompleted{};
    std::array<uint8_t, kQuestSlots> questSolved{};
    // +0xa30, AddMonsterKilled / MonstersKilled -- a saturating per-type
    // kill counter, one byte each, written as its own second loop.
    std::array<uint8_t, kMonsterSlots> monstersKilled{};
    int32_t levelUpPoints = 0;  // +0xf44, GetLevelUpPoints

    // --- tail, written after the base chain ---
    // The two 5-slot quick lists (+0xf4c and +0xf68, 0x1c bytes apart),
    // by item id, -1 for an empty slot.
    std::array<std::array<int16_t, kQuickSlots>, kQuickLists> quickSlots{};
    // Which slot of each list the stats block's +0x48 / +0x4c currently
    // point at -- saved as an *index into the list above*, searched for
    // at save time, and -1 when the pointer is null or not found.
    int16_t selectedQuick0 = -1;
    int16_t selectedQuick1 = -1;
    // Named by the Player/GameState dispatcher (FUN_1003f130) reading and
    // writing the same fields:
    uint8_t hasCreatedCharacter = 0;  // +0xf35  HasCreatedCharacter
    // Class and race are 32-bit in memory (ChooseCharacter / ChooseRace
    // store a full word) but go out as a **single byte each** -- the save
    // slot for both is the u8 writer. With eight classes that never
    // matters; it is still what the format can hold.
    uint8_t characterClass = 0;       // +0xf38  ChooseCharacter / GetCharacter
    uint8_t race = 0;                 // +0xf3c  ChooseRace / GetRace
    uint16_t portraitId = 0;          // +0xf40  GetPortraitID / SetPortraitID
    uint8_t sex = 0;                  // +0xfac  SetSex
    int32_t specialAbility = 0;       // +0xfb0  Get/SetSpecialAbility
    int32_t raceAbility = 0;          // +0xfb4  Get/SetRaceAbility
    uint16_t manaReduction = 0;       // +0xfc2  Get/SetManaReduction
    // Both derived by UpdateAttributes (FUN_1001fc24) from the chosen
    // class and race -- +0xfba is scaled from the stats block's
    // intelligence, +0xfb8 takes a flat +5 for one race. Neither has a
    // script binding of its own, so neither has an engine-given name.
    int16_t ffb8 = 0;                 // +0xfb8
    uint16_t ffba = 0;                // +0xfba

    // +0xfd8..+0x101c: **the game's eighteen global story flags.**
    //
    // Not an anonymous block -- FUN_1003e7a4 / FUN_1003ebd8 are the
    // player object's own getValue/setValue, and they match an incoming
    // field name against eighteen hardcoded wide strings, one per int, in
    // exactly this order. Every one is a real `GetPlayer().saved_*`
    // reference in the shipped scripts (`GetPlayer().saved_Guild = 3`,
    // `if(GetPlayer().saved_EndGame = 1)`), which is what makes these
    // *global* state rather than per-level: an ordinary `saved_X` on a
    // script object rides along in that entity's own record (see
    // SavedScriptVar) and dies with its level, while these eighteen live
    // on the player and therefore in character.dat.
    //
    // A multiplayer save writes the parallel copy at +0x1020..+0x1064
    // instead -- but the loader always reads into the single-player block
    // and then mirrors it, so both cases land here.
    std::array<int32_t, kTrailingInts> trailingInts{};
    // The names, in wire order. `saved_NA_Crystal` is the one slot with
    // no reference anywhere in the shipped corpus -- a cut flag the
    // format still carries. Note also `saved_Birgidda` here versus the
    // separate `Level.saved_Birgitta` the scripts set beside it: two
    // different stores, two different spellings, both live.
    static const char* const kGlobalFlagNames[kTrailingInts];
    // +0xf8c[8], the equipment slots, saved by each item's typeId (0 for
    // an empty slot). On load each id is looked up in the inventory that
    // the base chain has just restored, and only accepted when its
    // category is 3.
    std::array<uint16_t, kEquipSlots> equipTypeIds{};

    void WriteHead(SaveStream& s) const;
    void ReadHead(SaveStream& s);
    void WriteTail(SaveStream& s) const;
    void ReadTail(SaveStream& s);
};

// ---------------------------------------------------------------------
// One serializable entity. `kind` selects which layers are on the wire
// and in which order -- exactly the chain the matching vtable's +0x130
// would have walked.
// ---------------------------------------------------------------------
enum class SavedEntityKind {
    Entity,           // FUN_10066614   -- 5 vtables
    Drawable,         // FUN_10067db0   -- 7 vtables
    Item,             // FUN_1006d35c   -- 1
    Stackable,        // FUN_1002ed3c   -- 4
    Wearable,         // FUN_10047584   -- 2
    Weapon,           // FUN_1002eaa0   -- 2
    Spellbook,        // FUN_10028a60   -- 2
    InventoryHolder,  // FUN_10005164   -- 2
    Actor,            // FUN_10086514   -- 3
    LinkedActor,      // FUN_10006f90   -- 1
    Character,        // FUN_1001e754   -- 1
    Player,           // FUN_10043308   -- 1
};

// Which class a nested record belongs to comes from its typeId, resolved
// through entities.txt by the real loader. A caller that has that table
// hands one of these in; without it, nested records fall back to the
// commonest case (a plain Item inside an inventory, and the entity root
// for a level entry).
using SavedKindResolver = std::function<SavedEntityKind(int32_t typeId)>;

struct SavedEntity {
    SavedEntityKind kind = SavedEntityKind::Entity;
    // The typeId written immediately *before* this record by whoever owns
    // it -- an i16 in the level file, an i32 inside an inventory holder.
    // It lives here rather than in the owner because it is what selects
    // `kind`, and every nesting site writes one.
    int32_t typeId = 0;

    SavedEntityBase entity;
    SavedDrawable drawable;
    SavedItem item;
    SavedStackable stackable;
    SavedWearable wearable;
    SavedWeapon weapon;
    SavedSpellbook spellbook;
    SavedInventoryHolder holder;
    SavedStats stats;
    SavedActor actor;
    SavedLinkedActor linkedActor;
    SavedCharacter character;
    SavedPlayer player;

    // Children of an InventoryHolder, each preceded on the wire by its
    // own i32 typeId (see `typeId` above).
    std::vector<SavedEntity> inventory;

    void Write(SaveStream& s) const;
    void Read(SaveStream& s, const SavedKindResolver& resolve = nullptr);

    // True for the kinds whose chain passes through InventoryHolder, i.e.
    // the ones that can carry `inventory` at all.
    static bool HasInventory(SavedEntityKind kind);
    static bool HasSpellbook(SavedEntityKind kind);
};

// ---------------------------------------------------------------------
// `<level>.dat` -- FUN_100187d0 / FUN_100188dc.
// ---------------------------------------------------------------------
struct SavedLevelState {
    // Each entity's own `typeId` is the i16 written in front of it.
    std::vector<SavedEntity> entities;
    // The one field that is not an entity: engine+0xbe0e, written after
    // the terminator. GameEngine's constructor sets it to 1,
    // GameEngine_InitLevel clears and re-sets it around a level load, and
    // Render3DScene refuses to draw the 3D view while it is 0 -- so it is
    // the "this level is ready to render" gate, saved so a game reloaded
    // mid-cutscene comes back in the same state.
    uint8_t renderEnabled = 0;

    void Write(SaveStream& s) const;
    void Read(SaveStream& s, const SavedKindResolver& resolve = nullptr);

    static constexpr uint8_t kEntryPresent = 1;
    static constexpr uint8_t kEntryListEnd = 0;
};

// The two member names the writer uses, verbatim. `<level>.dat` is
// sprintf("%s.dat", levelName) -- and the container's lookup is
// case-insensitive, so the case here does not matter to a reader.
extern const char* const kCharacterMemberName;  // "character.dat"
std::string LevelMemberName(const std::string& levelName);

}  // namespace sk
