#pragma once

// Combat vertical-slice: native binding for a real monster .s script
// object (monsters/Azra_Rat.s to start -- see docs/PORT_ROADMAP.md's
// combat-slice entry). Same shape as ItemExecutable (a real script's
// Init() actually runs, backed by a real TreeNode), just for the
// SimKin "Monster" class's own native surface
// (shadowkey/simkin_native_bindings.json trie 0x14da4) instead of
// Item/Weapon/Armor's -- this port doesn't reproduce the real trie-class
// split (Monster/Character-stats/generic-base all contribute methods a
// monster script calls), one object answers whatever subset
// monsters/azra_rat.s's Init/OnKilled actually use, matching
// ItemExecutable's own precedent.
//
// GetPlayer() is implemented here (not soft-failed) because
// azra_rat.s's OnKilled calls it directly -- returns the shared
// PlayerExecutable passed into the constructor, same pattern
// MenuExecutable::method()'s own GetPlayer handler already uses.
// QuestSolved/AddMonsterKilled/MonstersKilled/SetQuestSolved are real
// state on PlayerExecutable as of M17 (its own class comment) -- see
// monster_executable.cpp's OnKilled comment for what that does and
// doesn't unlock.
//
// M16: generalized past the single-hardcoded-typeId (202/azra_rat) M12
// slice -- this same class also answers for the real corpus's non-
// hostile "monster"-category NPCs (docs/PORT_ROADMAP.md's M16 entry):
// entities.txt's category 2 covers both literal monsters and named
// quest NPCs (Tanyin Aldwyr, Acolyte Menlin, ...) alike, distinguished
// only by script content -- an NPC's Init() calls SetAggressive(false)
// + SetUsable(true)/SetUseText(...) + (usually) SetInvulnerable(true),
// and its OnUse() calls OpenMenu(...) to start a real dialogue tree
// (an ordinary MenuExecutable-driven branching conversation) instead of
// participating in the AI/melee loop at all. `m_Stack` (new) is needed
// for that OpenMenu() call -- same MenuStack reference MenuExecutable's
// own OpenMenu handler already routes through.

#include <string>

#include "skScriptedExecutable.h"

class skInterpreter;

namespace sk {
class StringTable;
}

namespace sk_bindings {

class MenuStack;
class PlayerExecutable;

class MonsterExecutable : public skScriptedExecutable {
public:
    // `strings` may be null, same fallback convention as ItemExecutable.
    MonsterExecutable(const skString& filename, skExecutableContext& ctxt,
                       const sk::StringTable* strings, PlayerExecutable& player, MenuStack& stack);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    std::string name() const;

    // Real per-instance combat stats, read directly off azra_rat.s's own
    // Init() call (see class comment) -- host-side accessors, not script
    // calls, so main.cpp's combat loop doesn't need to go through the
    // interpreter to read a number.
    int attack() const { return m_Attack; }
    int defense() const { return m_Defense; }
    int damageMin() const { return m_DamageMin; }
    int damageMax() const { return m_DamageMax; }
    int armorValue() const { return m_ArmorValue; }
    float chaseRadius() const { return static_cast<float>(m_ChaseRadius); }
    bool aggressive() const { return m_Aggressive; }

    int currentHealth() const { return m_CurrentHealth; }
    int maxHealth() const { return m_MaxHealth; }
    bool alive() const { return m_Alive; }

    // M16: NPC-mode fields (see class comment) -- usable()/useTextId()
    // mirror door_executable.h's own accessors of the same name (same
    // native class, same SetUsable/SetUseText calls), invulnerable()
    // gates ApplyDamage() below and main.cpp's melee target selection so
    // an essential quest NPC can't accidentally be killed.
    bool usable() const { return m_Usable; }
    int useTextId() const { return m_UseTextId; }
    bool invulnerable() const { return m_Invulnerable; }

    // Runs the real script's OnUse() handler -- same host-triggered-call
    // pattern InvokeOnKilled() already establishes. For an NPC this is
    // what actually starts its real dialogue tree (OnUse() calls
    // OpenMenu(...) -- see the class comment).
    void InvokeOnUse();

    // Clamps m_CurrentHealth at 0 and flips alive() false there -- does
    // NOT itself invoke OnKilled(), matching ItemExecutable::
    // MarkForRemoval()'s split between "mutate state now" and "let the
    // host decide when to react to it" (main.cpp calls InvokeOnKilled()
    // explicitly once it observes alive() go false).
    void ApplyDamage(int amount);

    // Runs the real script's OnKilled() handler, same
    // skParseException/skRuntimeException-catching convention
    // PlayerExecutable::LoadStartingInventory already uses for a script
    // call that might throw -- azra_rat.s's OnKilled body calls real
    // quest-state methods as of M17 (see the .cpp for the full trace,
    // including one call chain it still can't reach).
    void InvokeOnKilled();

private:
    const sk::StringTable* m_Strings;
    PlayerExecutable& m_Player;
    MenuStack& m_Stack;
    skInterpreter* m_Interpreter;  // for InvokeOnKilled()'s own fresh
                                    // skExecutableContext -- same reason
                                    // PlayerExecutable::LoadStartingInventory
                                    // builds one, there's no live call
                                    // frame's context to reuse when the
                                    // host (not a script) triggers the call.

    int m_NameId = -1;
    std::string m_Id;
    int m_ExpWorth = 0;
    int m_Attack = 0;
    int m_Defense = 0;
    int m_Spellcast = 0;
    int m_MagicResistance = 0;
    int m_DamageMin = 0;
    int m_DamageMax = 0;
    int m_ArmorValue = 0;
    int m_MaxHealth = 1;
    int m_CurrentHealth = 1;
    int m_Wimpy = 0;
    int m_ChaseRadius = 0;
    int m_Mob = 0;
    bool m_Aggressive = false;
    bool m_Alive = true;
    bool m_Usable = false;
    int m_UseTextId = -1;
    bool m_Invulnerable = false;
};

}  // namespace sk_bindings
