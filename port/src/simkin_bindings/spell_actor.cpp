#include "simkin_bindings/spell_actor.h"

#include "skiExecutable.h"

namespace sk_bindings {

SpellActor* ResolveSpellActor(skiExecutable* obj) {
    // The real FUN_1002fd30 asks two vtable questions and adds a fixed
    // offset to whichever answers yes. Here the two implementers are
    // MonsterExecutable and PlayerExecutable, which sit on unrelated Simkin
    // base classes (skScriptedExecutable and NativeStubExecutable) and so
    // reach SpellActor through two different multiple-inheritance lattices
    // -- a cross-cast, which is what dynamic_cast is for. Same answer, same
    // null-for-anything-else result.
    if (!obj) return nullptr;
    return dynamic_cast<SpellActor*>(obj);
}

}  // namespace sk_bindings
