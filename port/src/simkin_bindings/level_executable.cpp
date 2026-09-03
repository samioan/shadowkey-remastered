#include "simkin_bindings/level_executable.h"

#include <algorithm>
#include <cstdio>

#include "assets/sound_archive.h"
#include "audio/audio_engine.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
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
    if (methodName == skString("LoadLevel") && args.entries() == 1) {
        // M26: a real script's own zone-transition request (e.g.
        // cheatmenu.s's `Level.LoadLevel("azra")`) -- see MenuStack::
        // RequestZoneChange()'s own comment for why this reuses
        // RequestGameStart()'s same two fields instead of a parallel
        // mechanism, and main.cpp's M26 loading-screen block for what
        // actually consumes them (a real per-zone display-name banner +
        // the real loading-progress bar, docs/PORT_ROADMAP.md's M26
        // entry).
        m_Stack.RequestZoneChange(ToStdString(args[0].str()));
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
        // volume (0-100 in the one real corpus value seen, 100) maps
        // linearly to XAudio2's 0.0-1.0 gain.
        if (m_Stack.sounds() && m_Stack.audio()) {
            int soundId = args[0].intValue();
            int volume = args.entries() >= 2 ? args[1].intValue() : 100;
            float gain = (std::max)(0.0f, (std::min)(1.0f, static_cast<float>(volume) / 100.0f));
            const sk::Sound* sound = m_Stack.sounds()->GetSound(soundId);
            if (sound) {
                if (m_Stack.sounds()->IsMusic(soundId)) {
                    m_Stack.audio()->PlayMusic(*sound, gain);
                } else {
                    m_Stack.audio()->PlaySfx(*sound, gain);
                }
            }
        }
        returnValue = skRValue(0);
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
        int typeId = args[0].intValue();
        const sk::EntityTypeDescriptor* desc = m_EntityTypes ? m_EntityTypes->Lookup(typeId) : nullptr;
        bool isRealScript =
            desc && desc->name.size() > 2 && desc->name.compare(desc->name.size() - 2, 2, ".s") == 0;
        bool isItemCategory = desc && (desc->category == 3 || desc->category == 4 ||
                                        desc->category == 5 || desc->category == 6 ||
                                        desc->category == 9);
        if (isRealScript && isItemCategory) {
            std::string relPath = desc->name;
            for (char& c : relPath) {
                if (c == '\\') c = '/';
            }
            std::string fullPath = m_Stack.scriptRoot() + "/" + relPath;
            skExecutableContext loadCtxt(&m_Stack.interpreter());
            try {
                auto item = std::make_unique<ItemExecutable>(skString(fullPath.c_str()), loadCtxt,
                                                               m_Stack);
                skRValueArray initArgs;
                initArgs.append(skRValue(0));  // placeholder for Init's "(s)" parameter
                skRValue initRet;
                skExecutableContext callCtxt(&m_Stack.interpreter());
                item->method(skString("Init"), initArgs, initRet, callCtxt);
                item->SetTemplateId(typeId);  // M36, see ItemExecutable::templateId()
                returnValue = skRValue(static_cast<skiExecutable*>(item.get()), false);
                m_PendingCreatedEntity = std::move(item);
                return true;
            } catch (skParseException& e) {
                std::printf("Level: CreateEntity(%d) -- PARSE ERROR loading %s: %s\n", typeId,
                            fullPath.c_str(), e.toString().ptr());
            } catch (skRuntimeException& e) {
                std::printf("Level: CreateEntity(%d) -- RUNTIME ERROR loading %s: %s\n", typeId,
                            fullPath.c_str(), e.toString().ptr());
            }
        }
        returnValue = skRValue();  // "not found" -- see GetEntity()'s miss case
        return true;
    }
    return SoftFailNativeCall("Level", methodName, args, returnValue);
}

}  // namespace sk_bindings
