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

// M52 named all but two of these; see "What the scalars are" in
// docs/SAVE_FORMAT.md for how, and the per-field notes below for what.
struct SavedEntityBase {  // FUN_10066614 / FUN_10066cc4
    // +0xc4. Allocated in Entity::Init (FUN_100610e4) and registered in the
    // engine's id table, so it is a runtime handle, not a type.
    uint16_t entityId = 0;
    // +0x86. Render flags: the two renderers test bit 1 (skip transform)
    // and bit 2, and bit 4 is set/cleared around a kill and an equip.
    int16_t renderFlags = 0;
    // +0x8c. **M55**: "this entity is solid". A byte in memory, widened
    // to a word on the wire -- the only place in the whole image a
    // one-byte field goes through the stream's i32 overload, because the
    // member is an enum rather than a `TBool` (the shipped data's only
    // non-zero value is 2, never 1).
    //
    // M52 could not name it because it is never touched by a direct field
    // access: `Entity::Init` writes it through vtable slot `+0x4c` and
    // the five collision functions read it through `+0x40`, and all 31
    // Entity-derived vtables carry the identical pair. Its value comes
    // from column 2 of `<zone>_models.txt` -- see
    // world/model_collision.h, which has the full writeup.
    int32_t collisionSolid = 0;
    // +0x8e / +0x90. The collision box's half-extents in **X and Y**, in
    // 8.8 world units (256 per tile) -- columns 3 and 4 of the same
    // manifest row, and what the `SetRadius`/`SetRadius2` script bindings
    // write (vtable slots `+0x50`/`+0x54`). FUN_100017c8 walks x+-X and
    // y+-Y against Map_GetTileAt and pushes back to the tile boundary,
    // then does the same box test against every entity in the tile;
    // deactivating an entity (FUN_10005d60) zeroes both and sets
    // `passable`, but only when `tileStamped` is clear.
    int16_t collisionRadius = 0;
    int16_t collisionHeight = 0;
    // +0xe2. The `.ent` placement name, second copy; the loader sprintf()s
    // an art/script path from it (ZONE_FORMAT.md's step 3).
    std::string artName;
    // +0xe0. String-table index for the *short* name; default 14, set
    // beside `nameStringId` by Entity::Init.
    uint16_t shortNameStringId = 0;
    uint8_t f93 = 0;              // +0x93  the `PutInReverse` flag
    int32_t x = 0;                // +0x94  8.8 world units
    int32_t y = 0;                // +0x9c
    int16_t z = 0;                // +0xa4
    // The three orientation channels, 0x10000 to the turn. `.ent` supplies
    // all three per placement (ZONE_FORMAT.md).
    int16_t roll = 0;             // +0xb2  AddRotationRoll / SetRotationRoll
    int16_t pitch = 0;            // +0xa8
    int16_t yaw = 0;              // +0xb6
    // +0xcb. The object's own id string -- what `GetID` returns, what
    // `HasItem` matches against, and what the object registry is keyed by.
    // Also where the `.ent` placement name's *first* copy lands.
    std::string objectId;
    uint8_t skin = 0;             // +0xca  SetSkin(n), 0..12 in the corpus
    // +0x92. **M55**: "this entity is wider than the tile it stands on".
    // Set by GameEngine_InitLevel (and again by the save loader,
    // FUN_100188dc) on exactly `halfExtentX > 128 || halfExtentY > 128`;
    // the engine then calls FUN_10066204, which walks the box rotated by
    // the entity's heading and ORs bit 2 into the flags byte of every
    // tile cell it covers, so a big static object blocks like a wall
    // rather than being re-tested per entity. Reached only through vtable
    // slots +0xac/+0xb0, which is why M52's offset search saw nothing.
    uint8_t tileStamped = 0;
    uint8_t passable = 0;         // +0xd5  SetPassable(bool)
    uint8_t usable = 0;           // +0xd8  SetUsable(bool)
    // +0xd9 / +0xdc. "This entity has a name of its own" and that name's
    // string-table index (default 13). FUN_1006842c picks between the
    // index and a fixed fallback slot on exactly this flag.
    uint8_t hasCustomName = 0;
    uint32_t nameStringId = 0;
    int16_t spriteId = 0;         // +0x4a  30000 = take it from the template
    // --- the animation player, +0x6c..+0x80 ---------------------------
    // Set as a group by the clip starter at Drawable vtable +0x13c
    // (`SetAnimationRange(mode, firstFrame, frameCount, rate, loops)`) and
    // advanced by FUN_10065438 once per frame:
    //     position += (dt * rate) >> 8
    // with `dt` the engine's frame delta in 8.8 *seconds*. The three frame
    // fields are 8.8 fixed-point frame numbers -- the setter shifts its
    // plain frame arguments left by 8 -- so 0x100 is one frame.
    uint8_t animMode = 0;         // +0x6c  the setter's first argument; default 1
    // +0x6d. Loops left. 0x7f means "play forever" (its own branch at the
    // top of the tick), 0 means finished -- at which point the tick calls
    // vtable+0x148 with `SavedDrawable::nextAnimClip`. Default 0xff.
    uint8_t animLoopsLeft = 0;
    uint32_t animPosition = 0;    // +0x70  8.8 frames, the play cursor
    uint32_t animFirstFrame = 0;  // +0x74  8.8 frames
    uint32_t animFrameSpan = 0;   // +0x78  8.8 frames; default 0x100
    uint32_t animLastFrame = 0;   // +0x7c  8.8 frames = first + span
    // +0x80. The internal rate: frames per second times 256. Default 0xf00
    // = 15 fps. A model clip's own `rate` field is multiplied by 384 to get
    // here, which is what pins that field's units (model_archive.h).
    uint16_t animRate = 0;
    // --- the two wall-clock timers ------------------------------------
    // +0x10a / +0x10c / +0x110: the script `Delay(seconds, n)` timer.
    uint8_t delayArmed = 0;
    int32_t delayDeadline = 0;    // +0x10c, stored as (value - now)
    int16_t delayArg = 0;         // +0x110, Delay's second argument
    // +0x114 / +0x115 / +0x118: the native timer FUN_10068480 arms as
    // `(armed = 1, kind = k, deadline = time(0) + seconds)`.
    uint8_t timerArmed = 0;
    int32_t timerDeadline = 0;    // +0x118, likewise stored relative
    uint8_t timerKind = 0;        // +0x115
    // +0x60. Entity flag word, default 2. Bit 0 is cleared when the entity
    // is deactivated; the Character saver sets bit 4 on whatever it is
    // linked to.
    int16_t flags = 0;
    // +0x11c. "Something is mounted on / using me". Written only by
    // `MountGun` and `MountFlak88` and read only by `IsInUse` -- three
    // bindings with **zero uses in the shipped scripts**, so this is
    // saved and restored and never true. (The names are not a joke: they
    // are what the engine's own binding table calls them.)
    uint8_t inUse = 0;
    std::vector<SavedScriptVar> scriptVars;

    void Write(SaveStream& s) const;
    void Read(SaveStream& s);

    // Per variable: a 1 tag, then name and value. The list ends with
    // 0xff. The loader complains (but continues) on any other tag.
    static constexpr uint8_t kVarPresent = 0x01;
    static constexpr uint8_t kVarListEnd = 0xff;
};

struct SavedDrawable {  // FUN_10067db0 / FUN_10067ca0
    // +0x12c / +0x130, both set by the clip starter FUN_100655a8: the clip
    // index currently playing, and the clip to switch to when it ends
    // (`AttachEffect` arms 4, `DoFireExplosionEffect` arms 0x20). -1 in
    // `animClip` means "no model animation".
    int32_t animClip = 0;
    int32_t nextAnimClip = 0;
    // +0x5e. The model's **scale**, 8.8 fixed: `SetScale(256)` is 1x, and
    // the shipped scripts also use 192, 352 and 512. `.ent` carries one per
    // placement in `unkB`'s low halfword -- which corrects ZONE_FORMAT.md,
    // where that destination was called `modelFlags`.
    int16_t scale = 0;
    // The animation group again -- Drawable re-writes seven of Entity's
    // fields, in a different order, and the loader reads both copies. See
    // the header note; names are SavedEntityBase's.
    uint8_t animMode = 0;         // +0x6c
    uint32_t animFirstFrame = 0;  // +0x74
    uint32_t animFrameSpan = 0;   // +0x78
    uint16_t animRate = 0;        // +0x80
    uint32_t animLastFrame = 0;   // +0x7c
    uint32_t animPosition = 0;    // +0x70
    uint8_t animLoopsLeft = 0;    // +0x6d

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
    // +0x180. `SetDestroy(true)` / `GetDestroy()`: the object asks to be
    // removed. Set on a loot bag spawned by a monster's death.
    uint8_t destroy = 0;
    // +0x160. The `Init` guard: the binding tests it, and sets it to 1, so
    // an entity's script `Init` runs at most once. This is why restoring
    // script variables and re-running `Init` on load is idempotent.
    uint8_t initDone = 0;
    // +0x181. "This is a lootable container." Set to 1 beside `destroy`
    // when a monster drops its bag, and tested by `PickupItem`.
    uint8_t isContainer = 0;
    // +0x16c is the list's own count -- written as a plain i32 field and
    // read back as the loop bound, so it must agree with `spells`.
    std::vector<SavedSpellEntry> spells;

    void Write(SaveStream& s) const;
    void Read(SaveStream& s);
};

struct SavedItem {  // FUN_1006d35c / FUN_1006d2ac
    uint8_t f180 = 0;    // +0x180  SetAnimationFrames / SetReloadFrames share it
    // +0x19c. `SetWeaponSprite(n)` -- the sprite drawn in the player's hand
    // for this item; the shipped weapons use 72/104/120/136.
    int32_t weaponSprite = 0;
    int32_t range = 0;           // +0x1a0  SetRange(384) / SetRange(16384)
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
    // +0x1d4. `SetArmorConstraint(AR_Light|AR_Medium|AR_Heavy)` -- the
    // armour weight class, 0/1/2 (game_constants.cpp), 58 uses across the
    // shipped armour scripts. A u32 in memory, a byte on the wire.
    uint8_t armorConstraint = 0;
    void Write(SaveStream& s) const;
    void Read(SaveStream& s);
};

// The Item branch is a *tree*: Weapon and Armor are siblings, so +0x1cc and
// +0x1d0 hold different things depending on which one this record is. The
// save function is shared, and nothing on the wire distinguishes them --
// the typeId's `entities.txt` category does (4 = weapon, 6 = armor,
// 15 = shield). Field names below are the Weapon reading, with the Armor
// reading in the comment.
struct SavedWeapon {  // FUN_1002eaa0 / FUN_1002ea0c
    // +0x1cc. `SetDamageMin` on a weapon; `SetArmorValue` on armour, where
    // it is the piece's armour rating.
    int16_t damageMin = 0;
    int16_t damageMax = 0;  // +0x1ce  SetDamageMax; unused by armour
    // +0x1d0. `SetWeaponType(WR_Dagger|WR_LongBlade|WR_Axe|...)` on a
    // weapon; `SetArmorType(n)` on armour, where it also selects the
    // piece's display text.
    int32_t weaponType = 0;
    int32_t quantity = 0;   // +0x1c4  same field Stackable writes
    void Write(SaveStream& s) const;
    void Read(SaveStream& s);
};

struct SavedInventoryHolder {  // FUN_10005164 / FUN_10005320
    // +0x1e8. Set to 1 by this class's own constructor (FUN_10000004) and
    // cleared by the one subclass constructor at FUN_1009037c -- so it is a
    // fixed per-class discriminator that the save carries anyway.
    uint8_t f1e8 = 0;
    // +0x1b9 / +0x1bc / +0x1c0: the move order `SetState` issues. It sets
    // the flag and stores the offset to the target as
    // `(target.x - self.x) * 20`, `(target.y - self.y) * 20`.
    uint8_t moveOrderActive = 0;
    int32_t moveDeltaX = 0;
    int32_t moveDeltaY = 0;
    int32_t f1c4 = 0;   // +0x1c4
    uint8_t walkingState = 0;  // +0x1e1  GetWalkingState; the ctor default is 1
    uint8_t invulnerable = 0;  // +0x1e2  SetInvulnerable(bool)
    // +0x1e4. `SetActive(bool)` (FUN_10005d60): turning an entity off also
    // clears its native timer, clears flag bit 0, zeroes the whole
    // motion/orientation group and the collision cylinder, and makes it
    // passable. Saved because a script can leave an entity switched off.
    uint8_t active = 0;
    // The children are counted first (a full pass over the list asking
    // each one's vtable +0x104 whether it is saveable at all), then
    // written as `i32 typeId` + the child's own record. Note the typeId
    // is a *32-bit* field here and a 16-bit one in the level file.
    void Write(SaveStream& s) const;
    void Read(SaveStream& s);
};

struct SavedActor {  // FUN_10086514 / FUN_10086454
    // +0x2a8. The current AI package -- what `GetCurrentAIPackage` returns
    // and what `AiAttack`/`AiDetect`/`AiFlee`/`AiPursue`/`AiSleep`/
    // `AiSpellAssistTarget` each set. A 32-bit field truncated to a byte
    // on save, which is fine for six packages.
    uint8_t aiPackage = 0;
    uint8_t aggressive = 0;  // +0x2ac  SetAggressive(bool) / Aggressive()
    uint8_t invulnerable = 0;  // +0x1e2  -- second copy
    uint8_t usable = 0;        // +0xd8   -- second copy
    // +0x2ec. `SetLifespan(n)`, a binding with **zero uses** in the shipped
    // scripts -- saved and restored, never set.
    int16_t lifespan = 0;
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
    // +0x398. `SetFrozen` / `SetParalyzed` -- two bindings, one byte,
    // and **neither is used by any shipped script**.
    uint8_t frozen = 0;
    // +0x22c. The AI/behaviour state. The think step (FUN_1001d778) copies
    // it to +0x230 before running, and starts a 0x400/0x800 transition
    // timer when it comes back changed. -1 in the Character constructor,
    // 7 in the one subclass constructor that overrides it.
    int32_t aiState = 0;
    // +0x358 / +0x359 / +0x35a. Three "busy" flags that gate the think
    // step: it refuses to run while any is set. **Nothing in the image
    // ever sets one.** They are written to zero by the constructor and by
    // FUN_1001d3e0, cleared again by the release path, and tested in three
    // places -- an entire mechanism that is saved, restored, and dead.
    uint8_t busy358 = 0;
    uint8_t usingObject = 0;   // +0x359, the one with a visible purpose below
    uint8_t busy35a = 0;
    // +0x364/+0x368 are saved indirectly: the index of the linked entity
    // within the engine's own entity list, and that entity's typeId; or
    // -1/-1 when there is none. The loader parks them at +0x35c/+0x360
    // untouched and resolves them later (vtable +0x254 = FUN_1001ecb4,
    // which walks the engine's entity list by index for the first and the
    // character's own inventory by typeId for the second).
    //
    // What they link to: +0x364 is the entity this character is *using* --
    // taking it sets that entity's `inUse` -- and +0x368 is an item held in
    // the character's own inventory. Both are reachable only through this
    // fixup: no gameplay path assigns either.
    int32_t linkIndex = -1;
    int32_t linkTypeId = -1;
    // +0x36c/+0x370/+0x374. The position to put the character back at when
    // it stops using the linked entity -- FUN_1001ecb4 copies these three
    // straight into x/y/z. Guarded by `usingObject`, so also unreachable.
    int32_t returnX = 0, returnY = 0, returnZ = 0;
    int32_t f378 = 0;    // +0x378, a halfword widened on the wire
    // +0x37c/+0x380/+0x384. **Provably dead**: across the whole image
    // these three are touched by nothing except this save function and its
    // loader. The loader writes them, the saver reads them, and no other
    // code path in the game does either -- not even the constructor. Three
    // words of pure round-trip ballast.
    int32_t dead37c = 0, dead380 = 0, dead384 = 0;
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
