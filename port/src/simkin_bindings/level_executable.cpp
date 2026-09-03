#include "simkin_bindings/level_executable.h"

#include <cstdio>

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
    if (methodName == skString("GetPlayer") && args.entries() == 0) {
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Stack.player()), false);
        return true;
    }
    if (methodName == skString("PlayAmbient")) {
        // No audio system in this port (consistent with every prior
        // milestone) -- real signature is PlayAmbient(soundId, volume);
        // logged and no-op'd rather than a dedicated SoftFailNativeCall so
        // it doesn't read as an unimplemented-and-unexpected call.
        std::printf("Level: PlayAmbient(...) -- no audio system, ignored\n");
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
