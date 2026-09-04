#include "simkin_bindings/zone_script_executable.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace sk_bindings {

bool TriggerExecutable::method(const skString& methodName, skRValueArray& args,
                                skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("SetEntityID") && args.entries() == 1) {
        m_EntityId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetLimit") && args.entries() == 1) {
        m_Flags |= kFlagLimit;
        m_Limit = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetCallback") && args.entries() == 1) {
        m_Flags |= kFlagCallback;
        m_Callback = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("RemainActive")) {
        // M38: the real handler is exactly `flags &= ~kFlagOneShot`, and
        // the constructor starts with that bit *set* -- so this is how a
        // trigger opts out of deactivating itself after it fires.
        m_Flags &= ~static_cast<unsigned>(kFlagOneShot);
        return true;
    }
    // --- M38: the physical-trap half ---
    if (methodName == skString("AddEntity") && args.entries() == 1) {
        // Real handler: an int argument is stored as an entities.txt
        // typeId, a string argument as an entity name, and the match test
        // (FUN_1009138c) compares whichever kind each list entry is.
        if (args[0].type() == skRValue::T_String) {
            m_EntityNames.push_back(ToStdString(args[0].str()));
        } else {
            m_EntityTypeIds.push_back(args[0].intValue());
        }
        return true;
    }
    if (methodName == skString("SetTrap") && args.entries() == 3) {
        m_Flags |= kFlagTrap;
        m_TrapDamageMin = args[0].intValue();
        m_TrapDamageMax = args[1].intValue();
        m_TrapSaveId = args[2].intValue();
        return true;
    }
    if (methodName == skString("ShowDamageMessage") && args.entries() == 1) {
        m_ShowDamageMessage = args[0].boolValue();
        return true;
    }
    if (methodName == skString("IsActive") && args.entries() == 0) {
        returnValue = skRValue(m_Active);
        return true;
    }
    if (methodName == skString("SetDoor")) {
        // The real handler sets kFlagDoor (and kFlagDoorArg when given an
        // argument) and stores the door object it should match against.
        // No shipped zone script calls it -- every real door trigger in
        // the corpus (crypt1.s's five) matches by *name* through the trap
        // path instead -- so the object itself is not stored here; the
        // flag is, because it is what the door branch tests.
        m_Flags |= kFlagDoor;
        if (args.entries() >= 1) m_Flags |= kFlagDoorArg;
        return true;
    }
    if (methodName == skString("OpenDoor") && args.entries() == 0) {
        Fire(kNotifyDoorOpened, -1, m_Name);
        return true;
    }
    return SoftFailNativeCall("Trigger", methodName, args, returnValue);
}

bool TriggerExecutable::WatchesEntity(int typeId, const std::string& entityName) const {
    for (int id : m_EntityTypeIds) {
        if (id == typeId) return true;
    }
    for (const std::string& n : m_EntityNames) {
        if (n == entityName) return true;
    }
    return !m_Name.empty() && m_Name == entityName;
}

TriggerExecutable::FireResult TriggerExecutable::Fire(int mode, int typeId,
                                                       const std::string& entityName) {
    FireResult result;
    // The real function's very first test.
    if (!m_Active) return result;

    // `bVar14` -- the notifying entity's own name against this trigger's.
    const bool nameMatch = !m_Name.empty() && m_Name == entityName;
    // The AddEntity list test (FUN_1009138c's body, inlined twice in the
    // real function).
    auto listMatch = [&] {
        for (int id : m_EntityTypeIds) {
            if (id == typeId) return true;
        }
        for (const std::string& n : m_EntityNames) {
            if (n == entityName) return true;
        }
        return false;
    };

    // --- the kill-count half: only on mode 0, only with SetLimit ---
    if ((m_Flags & kFlagLimit) && mode == kNotifyEntityKilled && listMatch()) {
        ++m_KillCount;
        result.runCallback = m_Limit <= m_KillCount;
    }

    // --- the trap half: SetTrap, and mode 1 or 2 ---
    //
    // The real guard is `if (!(flags & 0x10) || (mode - 1u > 1) || !active)
    // skip` -- an unsigned compare, so mode 0 (which underflows) is
    // excluded along with anything above 2.
    if ((m_Flags & kFlagTrap) && (mode == kNotifyDoorOpened || mode == kNotifyTrapProximity)) {
        const bool matched = (typeId < 0 && entityName.empty()) ? nameMatch
                                                                 : (listMatch() || nameMatch);
        if (matched) {
            // The saving throws go here in the real code -- see
            // FireResult's comment for what they read and why neither is
            // reproduced. Without them the damage always lands.
            int lo = (std::min)(m_TrapDamageMin, m_TrapDamageMax);
            int hi = (std::max)(m_TrapDamageMin, m_TrapDamageMax);
            result.damage = hi > lo ? lo + std::rand() % (hi - lo + 1) : lo;
            result.trapFired = true;
            result.runCallback = true;
            // `if ((flags & kFlagOneShot) == 0) active = 1;` -- literally
            // what the decompile says, i.e. a trap never deactivates
            // itself either way. Left as-is rather than "corrected" to a
            // one-shot: every trap trigger in the shipped corpus calls
            // RemainActive() anyway (lothcav.s x3, broken1.s x1,
            // crypt1.s x5), so no real zone exercises the other path.
            if (!(m_Flags & kFlagOneShot)) m_Active = true;
        }
    }

    // The callback only runs when the trigger actually has one.
    if (!(m_Flags & kFlagCallback)) result.runCallback = false;
    return result;
}

bool TriggerExecutable::NotifyKilled() {
    if (m_Fired || m_Limit <= 0) return false;
    ++m_KillCount;
    if (m_KillCount < m_Limit) return false;
    m_Fired = true;
    return true;
}


// M38: the Encounter spawner -- see zone_script_executable.h.
bool EncounterExecutable::method(const skString& methodName, skRValueArray& args,
                                  skRValue& returnValue, skExecutableContext& context) {
    (void)context;
    if (methodName == skString("AddRandomSets") && args.entries() >= 2) {
        // The real handler walks the argument list two at a time
        // (`iVar7 += 0x28` over 0x14-byte skRValues == one pair per step)
        // and builds a single set object out of the whole run.
        std::vector<SpawnEntry> set;
        for (unsigned i = 0; i + 1 < args.entries(); i += 2) {
            set.push_back({args[i].intValue(), args[i + 1].intValue()});
        }
        m_Sets.push_back(std::move(set));
        return true;
    }
    if (methodName == skString("SetActive") && args.entries() == 1) {
        m_Active = args[0].boolValue();
        return true;
    }
    if (methodName == skString("SetLimit") && args.entries() == 2) {
        // Indexes the *region* list. The real handler logs an error and
        // does nothing when the index is out of range, which is exactly
        // what happens here.
        size_t index = static_cast<size_t>(args[0].intValue());
        if (index < m_RegionLimits.size()) m_RegionLimits[index] = args[1].intValue();
        return true;
    }
    if (methodName == skString("SetRespawnSeconds") && args.entries() == 1) {
        m_RespawnSeconds = args[0].intValue();
        return true;
    }
    if (methodName == skString("SpawnEncounter") && args.entries() == 1) {
        // `if (!active) return` is the real handler's first line.
        if (!m_Active) return true;
        m_PendingSpawn = ToStdString(args[0].str());
        return true;
    }
    return SoftFailNativeCall("Encounter", methodName, args, returnValue);
}

void EncounterExecutable::AddRegions(skRValueArray& args) {
    for (unsigned i = 0; i < args.entries(); ++i) {
        m_Regions.push_back(ToStdString(args[i].str()));
        m_RegionLimits.push_back(0);
    }
}

ZoneScriptExecutable::ZoneScriptExecutable(const skString& filename, skExecutableContext& ctxt,
                                            MenuStack& stack)
    : skScriptedExecutable(filename, ctxt), m_Stack(stack) {}

bool ZoneScriptExecutable::method(const skString& methodName, skRValueArray& args,
                                   skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("GetPlayer") && args.entries() == 0) {
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Stack.player()), false);
        return true;
    }
    if (methodName == skString("GetEntity") && args.entries() == 1) {
        // M23: azra.s calls this both bare (`Trinket = GetEntity(
        // "trinket")`) and Level-qualified (`M1 = Level.GetEntity("m1")`)
        // -- both need to work; delegate to the same registry either way.
        skRValueArray levelArgs;
        levelArgs.append(args[0]);
        return m_Stack.level().method(skString("GetEntity"), levelArgs, returnValue, context);
    }
    if (methodName == skString("isObject") && args.entries() == 1) {
        // M23: a plain "is this a real object reference" check -- e.g.
        // `if (isObject(Trinket) = true)`. True only for a genuine
        // T_Object (a found Level.GetEntity() result); the "null" global
        // (game_constants.cpp) is a blank T_String, so this is exactly
        // the same real/not-found distinction `!= null` checks already
        // rely on, just phrased as a function instead of a comparison.
        returnValue = skRValue(args[0].type() == skRValue::T_Object);
        return true;
    }
    if (methodName == skString("SetZone") && args.entries() == 2) {
        // M39 -- resolved. This was stored-and-ignored ("real meaning
        // unconfirmed, no getter anywhere reads it back"). The Zone-effects
        // dispatcher's case 0 shows it is the zone's **experience budget**:
        //
        //   level->zoneId = arg0;
        //   count = number of creatures in the zone with `+0x2ef` set;
        //   share = count ? arg1 / count : 0;
        //   for each of them: creatureStats->expWorth (+0x0e) = share;
        //
        // and `+0x2ef` is set by exactly one thing, the monster class's own
        // SetMob() (see MonsterExecutable's handler). So a zone author
        // writes one total -- azra.s's 2000, lothcav.s's 21500, crypt1.s's
        // 25000 -- and the engine divides it evenly across however many
        // creatures that zone happens to place, overwriting each script's
        // own SetExpWorth(). Which is why the per-script values vary so
        // little: they are placeholders the zone overwrites.
        //
        // Applied by the host after Init() returns, because the creature
        // list lives in main.cpp (see pendingZoneExperience()).
        m_ZoneId = args[0].intValue();
        m_ZoneExperience = args[1].intValue();
        m_ZoneExperiencePending = true;
        return true;
    }
    if (methodName == skString("Log") && args.entries() >= 1) {
        // M39: `Level.Log("...")` -- a real debug trace with ~100 call
        // sites across the corpus ("About to run CountInventory",
        // "Crystals are at " # num_crystals, ...). Genuinely useful now
        // that whole zone scripts run: it is the script author's own
        // commentary on what their logic thinks it is doing, and it lands
        // in shadowkey_port.log next to everything else.
        std::printf("  [zone log] %s\n", ToStdString(args[0].str()).c_str());
        return true;
    }
    if (methodName == skString("AddTrigger") && args.entries() == 1) {
        // M38: the name is kept now. It is not a label -- it is one of the
        // two ways a trigger identifies the entity it guards (the real
        // fire routine compares it against the notifying entity's own
        // name), and it is the *only* way crypt1.s's five trapped doors
        // are matched. Owned here for the zone's whole lifetime; the
        // script only holds a non-owning reference (see class comment).
        auto trigger = std::make_unique<TriggerExecutable>(ToStdString(args[0].str()));
        returnValue = skRValue(static_cast<skiExecutable*>(trigger.get()), false);
        m_Triggers.push_back(std::move(trigger));
        return true;
    }
    if (methodName == skString("AddEncounters")) {
        // M38: a real object now (see EncounterExecutable). Each argument
        // names one spawn region -- ghstpass.s passes three, lothcav.s one.
        auto encounter = std::make_unique<EncounterExecutable>();
        encounter->AddRegions(args);
        returnValue = skRValue(static_cast<skiExecutable*>(encounter.get()), false);
        m_Encounters.push_back(std::move(encounter));
        return true;
    }
    if (skScriptedExecutable::method(methodName, args, returnValue, context)) {
        return true;
    }
    return SoftFailNativeCall("ZoneScript", methodName, args, returnValue);
}

void ZoneScriptExecutable::RunCallback(const std::string& callback, TriggerExecutable* trigger,
                                       skiExecutable* entity) {
    if (callback.empty()) return;
    skRValueArray args;
    // See the header: (trigger, who). A callback that declares fewer
    // parameters (ghstpass.s's `GPZombiesKilled(s)`) simply ignores the
    // extra one.
    if (trigger) {
        args.append(skRValue(static_cast<skiExecutable*>(trigger), false));
    } else {
        args.append(skRValue(0));
    }
    if (entity) {
        args.append(skRValue(entity, false));
    } else {
        args.append(skRValue(0));
    }
    skRValue ret;
    skExecutableContext ctxt(&m_Stack.interpreter());
    try {
        method(skString(callback.c_str()), args, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("ZoneScript: PARSE ERROR in trigger callback \"%s\": %s\n", callback.c_str(),
                    e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("ZoneScript: RUNTIME ERROR in trigger callback \"%s\": %s\n", callback.c_str(),
                    e.toString().ptr());
    }
}

void ZoneScriptExecutable::NotifyKilled(int typeId) {
    for (auto& trigger : m_Triggers) {
        if (trigger->entityId() != typeId) continue;
        if (!trigger->NotifyKilled()) continue;
        RunCallback(trigger->callback(), trigger.get(), nullptr);
    }
}

bool ZoneScriptExecutable::Notify(int mode, int typeId, const std::string& entityName,
                                   skiExecutable* entity) {
    bool anyFired = false;
    for (auto& trigger : m_Triggers) {
        TriggerExecutable::FireResult r = trigger->Fire(mode, typeId, entityName);
        if (r.trapFired) {
            anyFired = true;
            if (r.damage > 0) m_Stack.player().ApplyDamage(r.damage);
            if (trigger->showDamageMessage()) {
                // The real code routes this through the on-screen message
                // system with one of four strings (avoided / avoided-by-a-
                // creature / hit / hit-a-creature). This port has no
                // floating combat-message channel to put it on, so it goes
                // where every other host-side combat trace already goes.
                std::printf("Trap \"%s\" hit the player for %d\n", trigger->name().c_str(),
                            r.damage);
            }
        }
        if (r.runCallback) RunCallback(trigger->callback(), trigger.get(), entity);
    }
    return anyFired;
}

// ---- M44: entering a named region. See the header for FUN_1002ef44. ----

void ZoneScriptExecutable::InvokeEnterZone(const std::string& regionName) {
    skRValueArray args;
    args.append(skRValue(skString(regionName.c_str())));
    skRValue ret;
    skExecutableContext ctxt(&m_Stack.interpreter());
    try {
        // Calls the base class directly, the same way ItemExecutable::
        // InvokeOnUse() does: a zone whose script declares no EnterZone
        // handler is the normal case (8 of the 21 shipped zone scripts
        // have none), and should answer "no" rather than log an
        // unresolved-native soft-fail every time the player crosses a
        // region boundary.
        skScriptedExecutable::method(skString("EnterZone"), args, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("ZoneScript: PARSE ERROR in EnterZone(\"%s\"): %s\n", regionName.c_str(),
                    e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("ZoneScript: RUNTIME ERROR in EnterZone(\"%s\"): %s\n", regionName.c_str(),
                    e.toString().ptr());
    }
}

EncounterExecutable* ZoneScriptExecutable::EnterRegion(const std::string& regionName) {
    // Step 1: an encounter that names this region takes it, and the script
    // never sees it. The real walk is over the level's encounter list in
    // registration order and stops at the first match.
    for (auto& encounter : m_Encounters) {
        const std::vector<std::string>& regions = encounter->regions();
        for (const std::string& r : regions) {
            if (r != regionName) continue;
            return encounter.get();
        }
    }
    // Step 2: the script's own handler.
    InvokeEnterZone(regionName);
    // Step 3 is **deliberately not reproduced**, and that is a finding
    // rather than an omission. The real walk over the trigger list
    // (`level+0x420`) selects triggers with flag 8 whose name matches the
    // region, then calls the fire routine as
    // `FUN_10090818(trigger, 1, *(engine+0x618))` -- mode 1, with the
    // **world object** as the notifying entity (confirmed in the
    // disassembly: `ldr r3,[r10,#0x34]; ldr r2,[r3,#0x618]; movne r1,#1`).
    // Inside, mode 1 requires `entity == trigger->doorObject` (SetDoor's
    // own argument) and the trap branch requires the entity to match the
    // trigger's AddEntity list. The world object is neither, for any
    // trigger any shipped script builds -- so this walk selects triggers
    // it then cannot fire. Reproducing it would be reproducing a no-op
    // with a misleading amount of machinery.
    return nullptr;
}

bool ZoneScriptExecutable::AnyTrapWatches(int typeId, const std::string& entityName) const {
    for (const auto& trigger : m_Triggers) {
        if (trigger->trap() && trigger->WatchesEntity(typeId, entityName)) return true;
    }
    return false;
}


// M38: see native_binding_common.h's StoreScriptObjectField().
bool ZoneScriptExecutable::setValue(const skString& fieldName, const skString& attribute,
                     const skRValue& value) {
    if (StoreScriptObjectField(m_ObjectFields, fieldName, value)) return true;
    return skScriptedExecutable::setValue(fieldName, attribute, value);
}

bool ZoneScriptExecutable::getValue(const skString& fieldName, const skString& attribute, skRValue& value) {
    if (LoadScriptObjectField(m_ObjectFields, fieldName, value)) return true;
    return skScriptedExecutable::getValue(fieldName, attribute, value);
}

}  // namespace sk_bindings
