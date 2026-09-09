#include "simkin_bindings/level_executable.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "assets/sound_archive.h"
#include "audio/audio_engine.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/zone_script_executable.h"
#include "skExecutableContext.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

namespace sk_bindings {

LevelExecutable::LevelExecutable(MenuStack& stack) : NativeStubExecutable("Level"), m_Stack(stack) {}

LevelExecutable::~LevelExecutable() = default;

std::unique_ptr<ItemExecutable> LevelExecutable::TakePendingCreatedEntity() {
    return std::move(m_PendingCreatedEntity);
}

// M44: FUN_1002bc6c's switch, verbatim. Note case 5's caption id is a
// decimal 700 rather than the 0xf3x/0xf4x run the other five use, and that
// it is the only five-slide vignette.
bool LevelExecutable::VignetteById(int id, VignetteDefinition& out) {
    switch (id) {
        case 0: out = {0xe0, 0xe3, 0xf30 + 9}; return true;  // delfhide.s
        case 1: out = {0xdc, 0xdf, 0xf40 + 9}; return true;  // crypt1.s
        case 2: out = {0xe4, 0xe7, 0xf41}; return true;      // drgnfld.s
        case 3: out = {0xe8, 0xeb, 0xf3d}; return true;      // ghstpass.s
        case 4: out = {0xec, 0xef, 0xf45}; return true;      // glaciercrawl.s
        case 5: out = {0xf0, 0xf4, 700}; return true;        // azra.s
        default: break;
    }
    // The real default arm returns without arming anything.
    return false;
}

// M45: see the header. Factored out of the four-argument CreateEntity so
// the encounter spawner builds its creatures through the same path.
std::unique_ptr<MonsterExecutable> LevelExecutable::CreateCreature(int typeId) {
    const sk::EntityTypeDescriptor* desc = m_EntityTypes ? m_EntityTypes->Lookup(typeId) : nullptr;
    bool isRealScript = desc && desc->name.size() > 2 &&
                         desc->name.compare(desc->name.size() - 2, 2, ".s") == 0;
    // Category 2 is entities.txt's creature/NPC category -- the same one
    // main.cpp's zone-load block resolves for placed monsters.
    if (!isRealScript || desc->category != 2) return nullptr;
    std::string relPath = desc->name;
    for (char& c : relPath) {
        if (c == '\\') c = '/';
    }
    std::string fullPath = m_Stack.scriptRoot() + "/" + relPath;
    skExecutableContext loadCtxt(&m_Stack.interpreter());
    try {
        auto monster = std::make_unique<MonsterExecutable>(skString(fullPath.c_str()), loadCtxt,
                                                            m_Stack.strings(), m_Stack.player(),
                                                            m_Stack);
        skRValueArray initArgs;
        initArgs.append(skRValue(0));  // Init's "(s)" placeholder
        skRValue initRet;
        skExecutableContext callCtxt(&m_Stack.interpreter());
        monster->method(skString("Init"), initArgs, initRet, callCtxt);
        return monster;
    } catch (skParseException& e) {
        std::printf("Level: CreateCreature(%d) -- PARSE ERROR loading %s: %s\n", typeId,
                    fullPath.c_str(), e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("Level: CreateCreature(%d) -- RUNTIME ERROR loading %s: %s\n", typeId,
                    fullPath.c_str(), e.toString().ptr());
    }
    return nullptr;
}

// M48: see the header.
int LevelExecutable::EntityCategoryOf(int typeId) const {
    const sk::EntityTypeDescriptor* desc = m_EntityTypes ? m_EntityTypes->Lookup(typeId) : nullptr;
    return desc ? desc->category : -1;
}

int LevelExecutable::TypeIdForScript(const std::string& relPath) const {
    if (!m_EntityTypes) return -1;
    for (const auto& entry : m_EntityTypes->all()) {
        std::string name = entry.second.name;
        for (char& c : name) {
            if (c == '\\') c = '/';
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (name == relPath) return entry.first;
    }
    return -1;
}

std::unique_ptr<ItemExecutable> LevelExecutable::CreateItem(int typeId, bool requireItemCategory) {
    const sk::EntityTypeDescriptor* desc = m_EntityTypes ? m_EntityTypes->Lookup(typeId) : nullptr;
    bool isRealScript = desc && desc->name.size() > 2 &&
                         desc->name.compare(desc->name.size() - 2, 2, ".s") == 0;
    if (!isRealScript) return nullptr;
    // M74: the category list is the factory's own item arms, not a
    // hand-picked five. It used to omit 14 (the seven scroll spells), 15
    // (nine of the ten shields) and 16 (the conjured Daedric sword), so
    // `Level.CreateEntity` on any of those answered null and the caller
    // silently got nothing.
    if (requireItemCategory && !IsInventoryItemCategory(desc->category)) return nullptr;
    std::string relPath = desc->name;
    for (char& c : relPath) {
        if (c == '\\') c = '/';
    }
    std::string fullPath = m_Stack.scriptRoot() + "/" + relPath;
    skExecutableContext loadCtxt(&m_Stack.interpreter());
    try {
        auto item = std::make_unique<ItemExecutable>(skString(fullPath.c_str()), loadCtxt, m_Stack);
        // M74: the type comes from the category, and it has to be in place
        // *before* Init() runs -- `blaze.s`'s own Init() asks
        // `GetPlayer().IsItemEnabledFor(self)` to pick between its two use
        // texts ("Learn Blaze" / "Take Blaze Scroll"), and that answer
        // depends on this object already knowing it is a spell.
        item->SetEntityCategory(desc->category);
        // M74: and the typeId moves ahead of Init() with it. The real
        // factory entry point is `create(category, typeId)`
        // (`FUN_100715a8`'s `vtable+0x18` call), so `entity+0xc8` is set
        // before the object exists as far as any script is concerned --
        // this port set it afterwards, which left every `Init()` looking at
        // a template id of -1.
        item->SetTemplateId(typeId);  // M36, see ItemExecutable::templateId()
        skRValueArray initArgs;
        initArgs.append(skRValue(0));  // placeholder for Init's "(s)" parameter
        skRValue initRet;
        skExecutableContext callCtxt(&m_Stack.interpreter());
        item->method(skString("Init"), initArgs, initRet, callCtxt);
        return item;
    } catch (skParseException& e) {
        std::printf("Level: CreateItem(%d) -- PARSE ERROR loading %s: %s\n", typeId,
                    fullPath.c_str(), e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("Level: CreateItem(%d) -- RUNTIME ERROR loading %s: %s\n", typeId,
                    fullPath.c_str(), e.toString().ptr());
    }
    return nullptr;
}

bool LevelExecutable::TakePendingCreature(PendingCreature& out,
                                           std::unique_ptr<MonsterExecutable>& script) {
    if (!m_PendingCreaturePending) return false;
    m_PendingCreaturePending = false;
    out = m_PendingCreature;
    script = std::move(m_PendingCreatureScript);
    return true;
}

bool LevelExecutable::method(const skString& methodName, skRValueArray& args,
                              skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("GetEntity") && args.entries() == 1) {
        auto it = m_Entities.find(ToStdString(args[0].str()));
        if (it != m_Entities.end()) {
            returnValue = skRValue(it->second, false);
        } else {
            // Matches the "null" global game_constants.cpp registers --
            // see its own comment for why this specific default (not a
            // dedicated sentinel object) is the right design.
            returnValue = skRValue();
        }
        return true;
    }
    if ((methodName == skString("Log") || methodName == skString("TraceInt")) &&
        args.entries() >= 1) {
        // M39: `Level.Log("...")` / `Level.TraceInt(...)` -- real debug
        // traces (~100 Log call sites across the shipped corpus). See
        // ZoneScriptExecutable's own Log handler; a zone script reaches
        // this one when it qualifies the call as `Level.Log(...)`, which
        // most of them do.
        std::printf("  [level log] %s\n", ToStdString(args[0].str()).c_str());
        return true;
    }
    if (methodName == skString("GetPlayer") && args.entries() == 0) {
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Stack.player()), false);
        return true;
    }
    if ((methodName == skString("LoadLevel") || methodName == skString("ForceLoadLevel") ||
         methodName == skString("ActuallyLoadLevel")) &&
        args.entries() >= 1) {
        // M26: a real script's own zone-transition request (e.g.
        // cheatmenu.s's `Level.LoadLevel("azra")`) -- see MenuStack::
        // RequestZoneChange()'s own comment for why this reuses
        // RequestGameStart()'s same two fields instead of a parallel
        // mechanism, and main.cpp's M26 loading-screen block for what
        // actually consumes them (a real per-zone display-name banner +
        // the real loading-progress bar, docs/PORT_ROADMAP.md's M26
        // entry).
        //
        // M61: all three take `(name, x, y)` with the two coordinates
        // optional and defaulting to -1, and all three record the pending
        // level the same way (MenuStack::SetPendingLevel). Where they
        // differ is *when* the load happens, and that is a difference this
        // port does not currently have to make:
        //
        //   ForceLoadLevel (0x16) / ActuallyLoadLevel (0x18) load right
        //     now, straight through the app object's own loader.
        //   LoadLevel (0x17) does NOT load. It records the destination and
        //     opens `levelconfirm.s` -- the "Travel to: <name> / Go /
        //     Don't Go" prompt -- and it is `Go` that calls
        //     `ActuallyLoadLevel(GetNextLevel(), GetNextLevelX(),
        //     GetNextLevelY())`.
        //
        // Raising that prompt needs a menu screen wired through main.cpp's
        // mode machinery plus the menu-side `GetNextLevelName()`, so it is
        // deliberately left out of this milestone and noted in
        // docs/PORT_ROADMAP.md. Every one of the three still defers by one
        // tick here, which is what the shipped scripts' arm-then-load
        // ordering needs (they call SetCameraStart on the line *after*
        // LoadLevel).
        std::string name = ToStdString(args[0].str());
        int nextX = args.entries() >= 2 ? args[1].intValue() : -1;
        int nextY = args.entries() >= 3 ? args[2].intValue() : -1;
        m_Stack.SetPendingLevel(name, nextX, nextY);
        m_Stack.RequestZoneChange(std::move(name));
        return true;
    }
    if (methodName == skString("GetNextLevel") && args.entries() == 0) {
        // `app+0x28`'s current-name slot, which LoadLevel has already
        // overwritten with the destination -- see SetPendingLevel().
        returnValue = skRValue(skString(m_Stack.currentLevelName().c_str()));
        return true;
    }
    if (methodName == skString("GetNextLevelX") && args.entries() == 0) {
        returnValue = skRValue(m_Stack.pendingLevelX());
        return true;
    }
    if (methodName == skString("GetNextLevelY") && args.entries() == 0) {
        returnValue = skRValue(m_Stack.pendingLevelY());
        return true;
    }
    if (methodName == skString("RestoreSaveLevel") && args.entries() == 0) {
        // Level binding 0x15, and the reason SetCameraStart can safely be
        // armed before its level exists. `levelconfirm.s`'s Don't Go calls
        // it: it copies the previous level name back over the pending one
        // and clears `engine+0x14a20`, so a declined trip cannot leave a
        // spawn override armed to fire at whatever loads next.
        m_Stack.RestorePendingLevel();
        return true;
    }
    if (methodName == skString("PlayAmbient") && args.entries() >= 1) {
        // M27: real signature PlayAmbient(soundId, volume) -- soundId is
        // the current zone's own <zone>_sounds.txt slot index, same real
        // convention PlayerExecutable::PlaySound() documents in full
        // (assets/sound_archive.h's class comment); every real corpus
        // call targets a real .ogg slot (a zone's own ambient/battle
        // track, e.g. azra.s's `Level.PlayAmbient(73,100)` -> azra_
        // sounds.txt's own `73 explore3.ogg`) and is meant to loop for as
        // long as that zone is active, so this always goes through
        // PlayMusic() (replaces whatever was playing) rather than a
        // one-shot -- IsMusic() is just a defensive check in case a
        // future/unseen real call ever targets a real .wav slot instead.
        // M51: `volume` is 0-100 and goes through the engine's own
        // 101-entry volume curve (audio/sound_mixing.h), not the linear
        // mapping this used to assume -- the curve is close to linear but
        // is a hand-tweaked table, so it is applied verbatim.
        if (m_Stack.sounds() && m_Stack.audio()) {
            int soundId = args[0].intValue();
            int volume = args.entries() >= 2 ? args[1].intValue() : sk::kDefaultSoundVolume;
            const sk::Sound* sound = m_Stack.sounds()->GetSound(soundId);
            if (sound) {
                if (m_Stack.sounds()->IsMusic(soundId)) {
                    m_Stack.audio()->PlayMusic(*sound, volume);
                } else {
                    m_Stack.audio()->PlaySfx(*sound, volume);
                }
            }
        }
        returnValue = skRValue(0);
        return true;
    }
    // ---- M44: the named-region natives (see level_executable.h) ----
    if (methodName == skString("LockZone") && args.entries() == 1) {
        std::string name = ToStdString(args[0].str());
        int tiles = m_ZoneRegions ? m_ZoneRegions->LockRegion(name) : 0;
        std::printf("  [zone] LockZone(\"%s\") -- %d tile(s) blocked\n", name.c_str(), tiles);
        return true;
    }
    if (methodName == skString("UnlockZone") && args.entries() == 1) {
        std::string name = ToStdString(args[0].str());
        int tiles = m_ZoneRegions ? m_ZoneRegions->UnlockRegion(name) : 0;
        std::printf("  [zone] UnlockZone(\"%s\") -- %d tile(s) opened\n", name.c_str(), tiles);
        return true;
    }
    if (methodName == skString("LightRect") && args.entries() == 5) {
        if (m_ZoneRegions) {
            m_ZoneRegions->LightRect(args[0].intValue(), args[1].intValue(), args[2].intValue(),
                                      args[3].intValue(), args[4].intValue());
        }
        return true;
    }
    if (methodName == skString("Vignette") && args.entries() == 1) {
        int id = args[0].intValue();
        if (VignetteById(id, m_PendingVignette)) {
            m_PendingVignettePending = true;
        } else {
            // The real default arm does nothing but a debug wait.
            std::printf("Level: Vignette(%d) -- no such vignette\n", id);
        }
        return true;
    }
    // M44: the zone-effects dispatcher's other four bindings
    // (SpawnWithinRadius, TickZones, AddInterestPoint, ClearInterestPoints)
    // are deliberately left to soft-fail: a grep of the entire shipped
    // script corpus finds **zero** call sites for any of them, so there is
    // nothing to be faithful to and no way to check an implementation.
    // Recorded rather than guessed at, the same treatment SetClipSize/
    // SetFireRate/SetReloadFrames already get in ItemExecutable.
    if (methodName == skString("GetZone") && args.entries() == 1) {
        // The real handler returns the room object itself, which scripts
        // only ever compare against null. Nothing in the corpus calls a
        // method on the result, so a plain "does this region exist"
        // boolean carries the same information without inventing a script-
        // visible object with no members.
        std::string name = ToStdString(args[0].str());
        returnValue = skRValue(m_ZoneRegions && m_ZoneRegions->HasRegion(name));
        return true;
    }
    if (methodName == skString("CreateEntity") && args.entries() == 4) {
        // M44: the four-argument form -- `Level.CreateEntity(274, x, y, z)`
        // in crypt1.s's EnterZone("UmbraHere") branch. typeId 274 is a
        // monster, i.e. exactly the non-item-shaped category the
        // one-argument form below refuses. Building a live creature needs
        // both a MonsterExecutable and a world instance, so the request is
        // parked here and main.cpp does the work on its next tick -- the
        // same defer-to-the-host shape TakePendingCreatedEntity() already
        // uses for an item -- except that the *script object* is built
        // here and returned straight away, because crypt1.s null-checks
        // the result and immediately calls `SetCanTeleport(true)` on it.
        // Only the world instance is deferred.
        int typeId = args[0].intValue();
        std::unique_ptr<MonsterExecutable> monster = CreateCreature(typeId);
        if (monster) {
            returnValue = skRValue(static_cast<skiExecutable*>(monster.get()), false);
            m_PendingCreature = PendingCreature{typeId, args[1].intValue(), args[2].intValue(),
                                                 args[3].intValue(), monster.get()};
            m_PendingCreatureScript = std::move(monster);
            m_PendingCreaturePending = true;
            return true;
        }
        returnValue = skRValue();  // "not found" -- see GetEntity()'s miss case
        return true;
    }
    if (methodName == skString("CreateEntity") && args.entries() == 1) {
        // M21: see this class's header comment for the real corpus
        // evidence (loot_ratseye.s's own Init()). Only item-shaped
        // categories resolve -- every real loot-bag script only ever
        // creates one of these; a monster/door/merchant typeId here would
        // need a MenuStack&-shaped construction this port's other loader
        // (main.cpp's zone-load block) already does differently, not
        // attempted for this native entry point.
        // M48: the body moved to CreateItem() so the cast's conjure branch
        // shares it; the handler's own category filter is unchanged.
        int typeId = args[0].intValue();
        std::unique_ptr<ItemExecutable> item = CreateItem(typeId);
        if (item) {
            returnValue = skRValue(static_cast<skiExecutable*>(item.get()), false);
            m_PendingCreatedEntity = std::move(item);
            return true;
        }
        returnValue = skRValue();  // "not found" -- see GetEntity()'s miss case
        return true;
    }
    if (methodName == skString("CreateEffect")) {
        // M63: the real case's very first test is `if (argc < 10) return
        // 1` -- it accepts the call and does nothing rather than raising,
        // so a shorter form is silently ignored rather than being a
        // different overload. Reproduced exactly, including the acceptance:
        // soft-failing a nine-argument call would report a missing native
        // that is not missing.
        if (args.entries() < 10) {
            std::printf("Level: CreateEffect with %d arguments -- the real case needs 10, ignored\n",
                        static_cast<int>(args.entries()));
            return true;
        }
        m_Effects.push_back(MakeEffect(args[0].intValue(), args[1].intValue(), args[2].intValue(),
                                       args[3].intValue(), args[4].intValue(), args[5].intValue(),
                                       args[6].intValue(), args[7].intValue(), args[8].intValue(),
                                       args[9].intValue()));
        const EffectEntity& e = m_Effects.back();
        std::printf("  [effect] slots %d-%d at (%d, %d, %d), %s, size %dx%d\n", e.firstSprite,
                    e.lastSprite, e.x, e.y, e.z, e.immortal ? "permanent" : "timed", e.sizeX,
                    e.sizeY);
        return true;
    }
    // M75: `Level` is the zone-root script object -- see AttachZoneScript()
    // in the header for the two independent pieces of corpus evidence.
    // Anything this class has no native handler for is a call on that
    // script: `crypt2/pedestal_entity.s` says `Level.AddCrystal()` seven
    // times and `AddCrystal[()...]` is defined in `crypt2.s`.
    //
    // The zone script's own method() runs its native handlers first and
    // then falls through to skScriptedExecutable, which is what finds the
    // script-defined handler; a name neither of them knows comes back
    // false and lands on this class's soft-fail below, so the "unresolved
    // native" log still reports every genuinely missing binding.
    if (m_ZoneScript && m_ZoneScript->method(methodName, args, returnValue, context)) return true;
    return SoftFailNativeCall("Level", methodName, args, returnValue);
}

void LevelExecutable::AttachZoneScript(ZoneScriptExecutable* script) {
    m_ZoneScript = script;
    if (!script) return;
    // Undeclared fields read back falsy rather than raising "Cannot get
    // field". 163 of the 168 `Level.<field>` names in the corpus are
    // declared at the top level of the right zone's root script and so
    // never need this; the remaining five are shipped typos that differ
    // from a declared name only in case or a digit (`saved_Openswdoor`
    // for `saved_OpenSwdoor`, `saved_Bread4` where dstar_w declares
    // 1..3), and each one of them sits on the first line of a real
    // conversation's Init(). Erroring there would close the whole menu
    // over a misspelling, so they read back falsy instead -- the same
    // choice, for the same reason, that PlayerExecutable::getValue()
    // already makes for `GetPlayer().saved_X`.
    //
    // M80: this used to be `script->setAddIfNotPresent(true)`, which had
    // the side effect of making the zone script answer to *every* name --
    // including `Level` itself, the global every zone root script calls
    // through. ZoneScriptExecutable::getValue() now spells the tolerance
    // out directly; see the long comment there.
}

bool LevelExecutable::setValue(const skString& fieldName, const skString& attribute,
                                const skRValue& value) {
    if (m_ZoneScript) return m_ZoneScript->setValue(fieldName, attribute, value);
    m_Fields[ToStdString(fieldName)] = value;
    return true;
}

bool LevelExecutable::getValue(const skString& fieldName, const skString& attribute,
                                skRValue& value) {
    if (m_ZoneScript) return m_ZoneScript->getValue(fieldName, attribute, value);
    auto it = m_Fields.find(ToStdString(fieldName));
    value = it != m_Fields.end() ? it->second : skRValue(0);
    return true;
}

void LevelExecutable::TickEffects(int frameDelta, int gravityUnits) {
    for (EffectEntity& effect : m_Effects) TickEffect(effect, frameDelta, gravityUnits);
    PruneEffects(m_Effects);
}

}  // namespace sk_bindings
