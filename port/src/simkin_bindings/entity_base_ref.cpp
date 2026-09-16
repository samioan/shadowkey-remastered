#include "simkin_bindings/entity_base_ref.h"

#include <cstdio>

#include "assets/sound_archive.h"
#include "audio/audio_engine.h"
#include "audio/sound_mixing.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/native_binding_common.h"
#include "skParseException.h"
#include "skRuntimeException.h"
#include "skTreeNode.h"
#include "skTreeNodeObject.h"

namespace sk_bindings {
namespace {

// A script that names itself would swap forever. The shipped corpus never
// chains at all (all seven sites land on a base spell that does not call
// RunScript), so anything past the first swap is already unusual and
// anything past this is a bug in the data.
constexpr int kMaxScriptSwaps = 8;

}  // namespace

bool EntityBaseRef::HandleEntityBaseNative(const skString& methodName, skRValueArray& args,
                                           skRValue& returnValue) {
    // ---- position: 0x30 / 0x31 / 0x34 / 0x35 / 0x36 (M62) --------------
    //
    // The real case branches on `argc == 3`: with three arguments it reads
    // z, with two it passes a literal 0 to the setter. Anything less leaves
    // with the interpreter's own argument-count error, so two is genuinely
    // the minimum.
    if ((methodName == skString("SetPosition") || methodName == skString("SetPositionMirror")) &&
        args.entries() >= 2) {
        m_X = args[0].intValue();
        m_Y = args[1].intValue();
        m_Z = args.entries() >= 3 ? args[2].intValue() : 0;
        m_PositionDirty = true;
        return true;
    }
    if (methodName == skString("GetPositionX") && args.entries() == 0) {
        returnValue = skRValue(m_X);
        return true;
    }
    if (methodName == skString("GetPositionY") && args.entries() == 0) {
        returnValue = skRValue(m_Y);
        return true;
    }
    if (methodName == skString("GetPositionZ") && args.entries() == 0) {
        returnValue = skRValue(m_Z);
        return true;
    }

    // ---- visibility: 0x0e (M92) ---------------------------------------
    //
    // `vtable[0x58](entity, arg ^ 1)` -- the slot is SetHidden and this is
    // its inverse. 40 call sites, the most of any unimplemented binding in
    // the census, and the only one with consequences a player can see: the
    // crypt2 crystals, the dstar_w and fearfrst reveals and the stouttp
    // trinket all left props standing where the script had hidden them.
    if (methodName == skString("ShowEntity") && args.entries() == 1) {
        m_Hidden = !args[0].boolValue();
        return true;
    }

    // ---- identity: 0x11 / 0x13 / 0x14 (M92) ---------------------------
    if (methodName == skString("GetID") && args.entries() == 0) {
        returnValue = skRValue(skString(m_Id.c_str()));
        return true;
    }
    if (methodName == skString("SetID") && args.entries() == 1) {
        m_Id = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("SetName") && args.entries() == 1) {
        // The real case rejects a string with a dialog rather than setting
        // anything -- see the header. Reproduced on the console, since a
        // modal box for a data error no shipped script makes would be a
        // worse trade here than a line in the log.
        if (args[0].type() == skRValue::T_String) {
            std::printf(
                "  [entity] SetName() invalid argument -- You must pass in an ID# now (got "
                "\"%s\")\n",
                ToStdString(args[0].str()).c_str());
            return true;
        }
        m_NameId = args[0].intValue();
        return true;
    }

    // ---- passability: 0x17 (M92) --------------------------------------
    //
    // The real case assigns `entity+0xd5` and then, for an entity that has
    // a footprint in the tile grid (`vtable[0xac]`), stamps or unstamps
    // collision flag 4 over it. Only a door has that footprint in this
    // port, and DoorExecutable overrides OnPassableChanged() to do it; for
    // everything else the flag is read per frame.
    if (methodName == skString("SetPassable") && args.entries() == 1) {
        m_Passable = args[0].boolValue();
        OnPassableChanged();
        return true;
    }

    // ---- rotation: 0x18 / 0x1b (M92) ----------------------------------
    //
    // Both write `entity+0xb6`; one assigns and one accumulates. A door's
    // swing has always used the accumulating form, and `monsters/azra_rat.s`
    // and `ratherb.s` use the assigning one to point Trothgar at the player
    // for the scripted herb scene (`Trthgar.SetRotationTurn(-26414)`).
    if (methodName == skString("SetRotationTurn") && args.entries() == 1) {
        m_RotationRaw = args[0].intValue();
        m_RotationDirty = true;
        return true;
    }
    if (methodName == skString("AddRotationTurn") && args.entries() == 1) {
        m_RotationRaw += args[0].intValue();
        m_RotationDirty = true;
        return true;
    }

    // ---- model: 0x23 (M92) --------------------------------------------
    //
    // A `models.txt` row index; see the header for the three live sites and
    // what each of the three numbers actually names. The case returns
    // without doing anything when called with no arguments (`if (argc == 0)
    // return 1;`), which is worth keeping -- it is an explicit early return
    // in the real code, not the argument-count Leave every other case has.
    if (methodName == skString("SetModel")) {
        if (args.entries() == 0) return true;
        m_ModelOverride = args[0].intValue();
        return true;
    }

    // ---- sound: 0x25 (M92) --------------------------------------------
    //
    // `PlaySound(id, volume = 100, directional = false, repeats = 1)` --
    // the real case builds three optional skRValues with exactly those
    // defaults and hands them to FUN_1001b198 along with the entity's
    // position and its `+0xb6` heading. `id` is the currently-loaded zone's
    // own `<zone>_sounds.txt` slot index (assets/sound_archive.h). The
    // corpus uses one argument 120 times and all four exactly once
    // (twilite/steamsound.s's `PlaySound(65, 75, 1, 255)`: a quieter,
    // directional, endlessly repeating steam hiss), which is what pinned
    // the meaning of each.
    //
    // `directional` is not panning -- see sound_mixing.h; it asks for a
    // small (<= 12.5%) cut based on the *emitter's* facing, and is applied
    // by the caller that knows the geometry, not here.
    if (methodName == skString("PlaySound") && args.entries() >= 1) {
        sk::SoundArchive* sounds = entitySounds();
        sk::AudioEngine* audio = entityAudio();
        if (sounds && audio) {
            const sk::Sound* sound = sounds->GetSound(args[0].intValue());
            const int volume = args.entries() >= 2 ? args[1].intValue() : sk::kDefaultSoundVolume;
            const int repeats = args.entries() >= 4 ? args[3].intValue() : sk::kDefaultSoundRepeats;
            if (sound) audio->PlaySfx(*sound, volume, repeats);
        }
        return true;
    }

    // ---- script swap: 0x04 (M92) --------------------------------------
    if (methodName == skString("RunScript") && args.entries() == 1) {
        m_PendingScript = ToStdString(args[0].str());
        return true;
    }

    // ---- replication: 0x32 (M92) --------------------------------------
    //
    // The entire case is inside `if (engine->inMultiplayer)`. Single player
    // here, so all 33 sites are correctly no-ops; counted so a test can see
    // the difference between that and never running. See the header.
    if (methodName == skString("MirrorMethod") && args.entries() == 1) {
        ++m_MirroredMethods;
        return true;
    }

    // ---- removal: 0x20 / 0x21 / 0x33 (M101) ---------------------------
    //
    // See the header. The target is resolved identically by all three:
    // no argument is this entity, an object argument is the entity it
    // names, and any other argument is nothing at all --
    // `crypt1/caretaker_leave_convo.s`'s `DestroyObjectMirror("ctaker")`
    // passes a string and so destroys nobody (the script is never opened
    // anyway). 0x21's extra work is a multiplayer notify, and 0x33 is only
    // that notify.
    if (methodName == skString("DestroyObject") || methodName == skString("DestroyObjectMirror") ||
        methodName == skString("MirrorDestroyObject")) {
        EntityBaseRef* target = this;
        if (args.entries() >= 1) {
            target = args[0].type() == skRValue::T_Object
                         ? dynamic_cast<EntityBaseRef*>(args[0].obj())
                         : nullptr;
        }
        if (!target) return true;
        if (methodName == skString("MirrorDestroyObject")) {
            ++target->m_MirroredDestroys;
        } else {
            target->RemoveFromWorld();
        }
        return true;
    }

    // ---- Random: 0x2c (M101) ------------------------------------------
    //
    // `FUN_100730c8(registry, lo, hi)` = `lo + rand() % (hi - lo + 1)`, the
    // same draw M21's shared helper already makes (which also tolerates the
    // bounds reversed; no shipped call passes them that way). On the base
    // so `GetPlayer().Random(40, 100)` -- `ghchestgold.s`'s gold roll --
    // stops answering 0.
    if (TryHandleRandom(methodName, args, returnValue)) return true;
    // M104: and the same for the multiplayer pair -- root bindings 0x3a/0x3b
    // plus `0x14d14` case 1, so every entity answers them. See
    // native_binding_common.h. This reaches `ratherb.s`'s OnKilled, the
    // Dragonfield and Lothna loot bags, the five Dark Star East creatures
    // and Pergan Asuul.
    if (TryHandleMultiplayerQuery(methodName, args, returnValue)) return true;
    return false;
}

void RunEntityInit(EntityBaseRef& entity, const std::string& scriptRoot,
                   skExecutableContext& ctxt) {
    skTreeNodeObject* node = entity.entityScriptNode();
    if (!node) return;
    for (int swap = 0;; ++swap) {
        skRValueArray args;
        args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
        skRValue ret;
        node->method(skString("Init"), args, ret, ctxt);

        std::string next;
        if (!entity.TakePendingScript(next)) return;
        if (swap >= kMaxScriptSwaps) {
            std::printf("  [entity] RunScript(\"%s\") -- chain too long, stopping\n",
                        next.c_str());
            return;
        }
        const std::string path = ResolveScriptPath(scriptRoot, next);
        // Only the reload is guarded: a bad name leaves the entity on the
        // script it already has (with everything its own Init() set still
        // in place) rather than taking down the whole zone load, and every
        // caller's own handler still sees an exception thrown by an Init().
        try {
            node->setNode(skString(path.c_str()),
                          skTreeNode::read(skString(path.c_str()), ctxt), true);
        } catch (skParseException& e) {
            std::printf("  [entity] RunScript(\"%s\") -- PARSE ERROR in %s: %s\n", next.c_str(),
                        path.c_str(), e.toString().ptr());
            return;
        } catch (skRuntimeException& e) {
            std::printf("  [entity] RunScript(\"%s\") -- RUNTIME ERROR in %s: %s\n", next.c_str(),
                        path.c_str(), e.toString().ptr());
            return;
        }
        std::printf("  [entity] RunScript(\"%s\") -- switched to %s\n", next.c_str(),
                    path.c_str());
    }
}

}  // namespace sk_bindings
