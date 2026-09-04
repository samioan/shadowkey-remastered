#include "simkin_bindings/monster_executable.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "assets/sound_archive.h"
#include "assets/string_table.h"
#include "audio/audio_engine.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/item_executable.h"
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

MonsterExecutable::MonsterExecutable(const skString& filename, skExecutableContext& ctxt,
                                      const sk::StringTable* strings, PlayerExecutable& player,
                                      MenuStack& stack)
    : skScriptedExecutable(filename, ctxt),
      m_Strings(strings),
      m_Player(player),
      m_Stack(stack),
      m_Interpreter(ctxt.getInterpreter()) {}

MonsterExecutable::~MonsterExecutable() = default;

std::string MonsterExecutable::name() const {
    if (m_Strings && m_NameId >= 0) return m_Strings->Get(m_NameId);
    return m_Id.empty() ? std::string("?") : m_Id;
}

void MonsterExecutable::PlayNoise(int soundId) {
    // M28: -1 covers both "SetXNoise() never called" (e.g. an NPC that
    // never sets one) and any script that genuinely passes a negative id
    // -- neither should reach SoundArchive::GetSound(), same defensive
    // shape the real slot-index convention already assumes elsewhere.
    if (soundId < 0 || !m_Stack.sounds() || !m_Stack.audio()) return;
    const sk::Sound* sound = m_Stack.sounds()->GetSound(soundId);
    if (sound) m_Stack.audio()->PlaySfx(*sound);
}

void MonsterExecutable::PlayAttackNoise() { PlayNoise(m_AttackNoiseId); }

// M37: see monster_executable.h / combat.h.
int MonsterExecutable::spellResistance() const {
    return SpellResistance(m_MagicResistance, m_Will);
}

int MonsterExecutable::spellToHit() const { return SpellToHit(m_Spellcast, m_Will); }

void MonsterExecutable::ApplyDamage(int amount) {
    // m_Invulnerable (M16): real essential-NPC scripts (Tanyin Aldwyr and
    // the other named quest NPCs) call SetInvulnerable(true) in Init() --
    // honoring it here means main.cpp's melee target selection doesn't
    // even need its own separate check for the common case, though it
    // still skips them too (see main.cpp) so they're never shown as a
    // combat target in the first place.
    if (!m_Alive || amount <= 0 || m_Invulnerable) return;
    m_CurrentHealth -= amount;
    if (m_CurrentHealth <= 0) {
        m_CurrentHealth = 0;
        m_Alive = false;
        // M28: death noise wins over hit noise -- both fire from the same
        // single ApplyDamage() choke point (melee and spell HitTarget
        // both route through here), so there's no risk of ever double-
        // playing.
        PlayNoise(m_DeathNoiseId);
    } else {
        PlayNoise(m_IsHitNoiseId);
    }
}

void MonsterExecutable::InvokeOnUse() {
    if (!m_Interpreter) return;
    skRValueArray args;
    args.append(skRValue(0));  // placeholder for OnUse's "(s)" parameter, same convention every
                                // Init(s)/OnUse(s)/OnKilled(s) caller in this codebase already
                                // uses (main.cpp's zone-load block, etc.) -- M17 found this one
                                // and InvokeOnKilled() below had drifted from it (empty args),
                                // latent until a real script actually referenced its own `s`
                                // parameter on a path this port hadn't exercised before.
    skRValue ret;
    skExecutableContext ctxt(m_Interpreter);
    try {
        method(skString("OnUse"), args, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("MonsterExecutable: PARSE ERROR in OnUse(): %s\n", e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("MonsterExecutable: RUNTIME ERROR in OnUse(): %s\n", e.toString().ptr());
    }
}

void MonsterExecutable::InvokeOnKilled() {
    // azra_rat.s's OnKilled body: `if (GetPlayer().QuestSolved(0)) {
    // return; } else { GetPlayer().AddMonsterKilled(203); if
    // (GetPlayer().MonstersKilled(203) >= 8) { ...if(not
    // GetPlayer().QuestSolved(0)) { if(s=false){Delay(2,0);} }
    // Trthgar=Level.GetEntity("trthgar"); Trthgar.SetPositionMirror(...);
    // ... SetQuestSolved(0,true); } }`. M17: QuestSolved/
    // AddMonsterKilled/MonstersKilled are now real state on
    // PlayerExecutable (see its class comment) -- the kill counter
    // genuinely reaches 8 real rat kills. M18 (simkin_bindings/
    // level_executable.h) closed the remaining gap: a real `Level` global
    // now exists, and `GetEntity("trthgar")` resolves to a real live
    // `Gravel_Trothgar.s`-backed object whenever the host has registered
    // it (main.cpp's zone-load block does, for any zone with that real
    // placement) -- so `SetQuestSolved(0,true)` genuinely runs and quest
    // id 0 flips solved (verified end to end, `m18_level_smoke.cpp`). A
    // host that never registers "trthgar" (`quest_smoke`'s own isolated
    // setup, which doesn't load a full zone) still sees `GetEntity`
    // resolve gracefully to "not found," so
    // `Trthgar.SetPositionMirror(...)` throws its own clean "Cannot call
    // Method ... on a non object" instead -- same net effect (quest stays
    // unsolved there), just no longer because `Level` itself is missing.
    // `InvokeOnKilled()`'s existing try/catch already logs and swallows
    // either exception cleanly rather than crashing, same as any other
    // script error this host-triggered call might hit.
    if (!m_Interpreter) return;
    skRValueArray args;
    args.append(skRValue(0));  // placeholder for OnKilled's "(s)" parameter, see InvokeOnUse()'s
                                // comment above -- this call site had the same missing-argument
                                // bug, found by the same M17 test.
    skRValue ret;
    skExecutableContext ctxt(m_Interpreter);
    try {
        method(skString("OnKilled"), args, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("MonsterExecutable: PARSE ERROR in OnKilled(): %s\n", e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("MonsterExecutable: RUNTIME ERROR in OnKilled(): %s\n", e.toString().ptr());
    }
}

// M34's SetHealth (FUN_1004bb88), which is the same three-instruction
// clamp on either side of the fight -- Absorb heals whoever cast it, and
// as of M43 that can be a creature.
void MonsterExecutable::SetActorHealth(int value) {
    m_CurrentHealth = value;
    if (m_CurrentHealth > m_MaxHealth) m_CurrentHealth = m_MaxHealth;
    if (m_CurrentHealth < 0) m_CurrentHealth = 0;
}

// ---- M43: SetMob's stat template ----

int MonsterExecutable::ZoneDifficulty(const std::string& zoneName) {
    // FUN_1008467c: a flat strcasecmp chain over 21 zone names, returning
    // 1..21 in the game's own progression order, and 1 for anything not
    // listed. Transcribed verbatim, order included -- the order *is* the
    // data, and it doubles as a statement about how the game intends its
    // zones to be sequenced.
    static const char* const kZones[] = {
        "azra",     "delfhide", "erthcave", "ghstpass",     "twilite", "snowline", "fearfrst",
        "ffarena",  "drgnfld",  "raiders",  "dstar_w",      "dstar_e", "LothCav",  "lakvan",
        "broken1",  "broken2",  "stouttp",  "GlacierCrawl", "crypt1",  "crypt2",   "crypt3",
    };
    for (size_t i = 0; i < sizeof(kZones) / sizeof(kZones[0]); ++i) {
        const char* candidate = kZones[i];
        size_t n = 0;
        bool equal = true;
        for (; candidate[n] && n < zoneName.size(); ++n) {
            if (std::tolower(static_cast<unsigned char>(candidate[n])) !=
                std::tolower(static_cast<unsigned char>(zoneName[n]))) {
                equal = false;
                break;
            }
        }
        if (equal && candidate[n] == '\0' && n == zoneName.size()) {
            return static_cast<int>(i) + 1;
        }
    }
    return 1;
}

void MonsterExecutable::ApplyMobTemplate(int tier, int bonus) {
    // Dispatcher 0x10084924 case 0, the nested `switch (tier)`. The whole
    // template is driven by one number:
    //
    //   L = ZoneDifficulty(<current level name>) + <optional 2nd argument>
    //   H = L >> 1
    //
    // -- so the same creature script is weaker in azra (L = 1) than in
    // crypt3 (L = 21), and every one of these writes lands *after* the
    // script's own SetAttack/SetDefense/SetMaxHealth/etc., overwriting
    // them. That is why the 21 shipped caster scripts that call SetMob all
    // pass SetSpellcast(0) and placeholder damage values, while the 11 that
    // do not call SetMob all set real ones: the split is exact, with no
    // exceptions in either direction across the whole corpus.
    //
    // Willpower is the field that makes this a prerequisite for creature
    // casting at all. The magic to-hit gate is `spellcast + 2*willpower`,
    // and `chance = 0` whenever that is <= 0 -- so a SetSpellcast(0)
    // creature with no willpower could never land a spell. It is SetMob
    // that gives it one.
    if (tier < 1 || tier > 4) return;  // the real `if (3 < tier - 1u)` bail
    const int L = ZoneDifficulty(m_Stack.requestedZone()) + bonus;
    const int H = L >> 1;
    m_ArmorValue = 1;  // every tier, unconditionally
    switch (tier) {
        case 1:
            m_Attack = H * 8 + 8;
            m_Defense = L * 2 + 60;
            m_DamageMin = H * 3 + 6;
            m_DamageMax = H * 4 + 8;
            m_Will = 1;
            m_MagicResistance = L * 3 + 20;
            m_MaxHealth = L * 10 + 20;
            break;
        case 2:
            // The caster tier: attack and both damage bounds pinned to 1,
            // and by far the largest willpower term. bandit_mage.s,
            // highwaymage.s and drgnfld/bandit_mage_q31.s are exactly the
            // scripts that use it.
            m_Attack = 1;
            m_Defense = H * 2 + 40;
            m_DamageMin = 1;
            m_DamageMax = 1;
            m_Will = H * 3 + 15;
            m_MagicResistance = H * 2 + 20;
            m_MaxHealth = L * 7 + 15;
            break;
        case 3:
            m_Attack = H * 6 + 6;
            m_Defense = L * 2 + 50;
            m_DamageMin = H * 2 + 5;
            m_DamageMax = H * 3 + 6;
            // The one division in the whole template, and a magic multiply
            // in the binary rather than a written divide.
            m_Will = (L / 3) * 4 + 15;
            m_MagicResistance = L * 3 + 20;
            m_MaxHealth = L * 8 + 15;
            break;
        case 4:
            m_Attack = H * 8 + 8;
            m_Defense = L * 2 + 60;
            m_DamageMin = H * 3 + 6;
            m_DamageMax = H * 4 + 8;
            m_Will = 1;
            // The one place tier 4 differs from tier 1: L*2 rather than
            // L*3. Otherwise the two share every value but max health.
            m_MagicResistance = L * 2 + 20;
            m_MaxHealth = L * 8 + 15;
            break;
        default:
            break;
    }
    // The real tail: SetHealth(maxHealth) through FUN_1004bb88, i.e. the
    // creature also arrives at full health for its new maximum.
    m_CurrentHealth = m_MaxHealth;
}

// ---- M43: the creature's own casting ----

bool MonsterExecutable::RollForMelee() const {
    // FUN_100835b8's own branch condition, transcribed:
    //
    //   if (slot0 == 0 || (meleeRoll != 0 && meleeRoll <= rand(0,100)))
    //        melee
    //   else cast
    //
    // rand(0,100) is inclusive at both ends -- FUN_100730c8 is
    // `lo + rand % (hi - lo + 1)` -- so 101 outcomes, which is also what
    // makes the cumulative AddSpell thresholds in ChooseSpell() land on the
    // percentages the shipped scripts' comments claim.
    if (!hasSpells()) return true;
    if (m_MeleeRoll == 0) return false;
    return m_MeleeRoll <= (std::rand() % 101);
}

ItemExecutable* MonsterExecutable::ChooseSpell(bool targetBlinded) const {
    // FUN_1008457c. The special case first: a creature that knows Blind and
    // whose target is already blind skips the roll and casts the first
    // non-Blind spell it has instead -- the real test is
    // `monster+0x20c != 0 && monster+0x32c != -1 && (targetStats+0x44 & 4)`,
    // i.e. "I have a target, I have a Blind spell, and the target is
    // already under effect flag 4".
    if (m_BlindSpellSlot >= 0 && targetBlinded) {
        for (int i = 0; i < kSpellSlots; ++i) {
            ItemExecutable* spell = m_SpellSlots[i].spell;
            // The real comparison is against the spell entity's own
            // entities.txt typeId (`spell+0xc8`), which in this port is the
            // typeId Level.CreateEntity() stamped on it.
            if (spell && spell->templateId() != kBlindSpellTypeId) return spell;
        }
    }

    // The cumulative table: walk until a slot's threshold *reaches* the
    // roll. Running off the end returns nothing at all, which is the real
    // `if (3 < iVar3) return 0` -- reachable whenever a script's thresholds
    // stop short of 100.
    int roll = std::rand() % 101;
    int index = 0;
    while (m_SpellSlots[index].chance < roll) {
        if (++index >= kSpellSlots) return nullptr;
    }
    return m_SpellSlots[index].spell;
}

MonsterExecutable::CastAttempt MonsterExecutable::CastSpellAt(SpellActor* target) {
    CastAttempt attempt;
    if (!target) return attempt;
    ItemExecutable* spell = ChooseSpell(target->actorStats().blinded());
    if (!spell) return attempt;
    // M48: the real cast, FUN_10046764. It runs entirely on the *caster* --
    // this creature -- and hands back whatever the world still has to do.
    // Note it does not consult `target` at all beyond the AI's own choice
    // of spell above: an offensive spell reaches its target only if the
    // projectile the caller launches actually gets there.
    attempt.spell = spell;
    attempt.result = CastSpell(*spell, *this);
    if (attempt.result.cast && m_PlaySpellCasting) PlayNoise(kSpellCastSoundId);
    return attempt;
}

void MonsterExecutable::SetAiPackageTimed(int package, int durationUnits) {
    // FUN_10086b98, byte for byte in behaviour: set the package, arm the
    // countdown as `duration << 8`, and -- only for the flee package --
    // forget the current target and record idle as what to return to.
    m_AiPackage = package;
    m_AiPackageTimer = durationUnits << 8;
    if (package != kAiFlee) return;
    m_SavedAiPackage = kAiIdle;
}

void MonsterExecutable::TickAi(int deltaUnits) {
    if (m_AiPackageTimer > 0) {
        m_AiPackageTimer -= deltaUnits;
        if (m_AiPackageTimer <= 0) {
            m_AiPackageTimer = 0;
            // The real tick's `if (timer expired) package = saved` --
            // this is what ends a Fear effect.
            m_AiPackage = m_SavedAiPackage;
        }
    }
    // M43: everything below the AI package -- the paralysis lockout, the
    // timed stat modifiers, the effect flags and both periodic channels --
    // lives on the stats block now (actor_stats.h), because in the real
    // engine it always did. The damage callback routes through
    // ApplyDamage() so that a real SetInvulnerable(true) quest NPC still
    // cannot be poisoned or burned to death.
    // M48: the regeneration half of the second periodic channel. A creature
    // has no fatigue pool worth speaking of (nothing sets one), but health
    // regeneration is real -- and a creature *can* arm it, because
    // `spells\AzraSustenance.s` is an ordinary spell entity that a script
    // could hand to one with AddSpell.
    m_Stats.Tick(
        deltaUnits, [this](int damage) { ApplyDamage(damage); },
        [this](int kind) {
            if (kind == ActorStats::kPeriodicFatigueRegen) {
                m_Fatigue += m_Level;
            } else if (kind == ActorStats::kPeriodicHealthRegen) {
                SetActorHealth(m_CurrentHealth + m_Level);
            }
        });
}

bool MonsterExecutable::ConsumeAttackCadence(int deltaUnits) {
    m_AttackCadence += deltaUnits;
    if (m_AttackCadence <= kAiAttackCadenceThreshold) return false;
    // Real reset: `rand & 0x1f`. Uses the same host RNG the rest of this
    // port's combat rolls use rather than reproducing the engine's own
    // generator, which is not decompiled.
    m_AttackCadence = std::rand() & 0x1f;
    return true;
}

bool MonsterExecutable::method(const skString& methodName, skRValueArray& args,
                                skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("PlaySound") && args.entries() >= 1) {
        // M27: a real monster script's own bare self-call (e.g. monsters/
        // umbra_keth.s's `PlaySound(83)`) -- same real per-zone-manifest
        // slot-index convention PlayerExecutable::PlaySound() documents in
        // full (assets/sound_archive.h).
        if (m_Stack.sounds() && m_Stack.audio()) {
            const sk::Sound* sound = m_Stack.sounds()->GetSound(args[0].intValue());
            if (sound) m_Stack.audio()->PlaySfx(*sound);
        }
        return true;
    }
    if (methodName == skString("SetAggressive") && args.entries() == 1) {
        m_Aggressive = args[0].boolValue();
        return true;
    }
    if (methodName == skString("SetName") && args.entries() == 1) {
        m_NameId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetID") && args.entries() == 1) {
        m_Id = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("SetExpWorth") && args.entries() == 1) {
        m_ExpWorth = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetAttack") && args.entries() == 1) {
        m_Attack = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDefense") && args.entries() == 1) {
        m_Defense = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetSpellcast") && args.entries() == 1) {
        m_Spellcast = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetMagicResistance") && args.entries() == 1) {
        m_MagicResistance = args[0].intValue();
        return true;
    }
    if ((methodName == skString("SetWillpower") || methodName == skString("SetWill")) &&
        args.entries() == 1) {
        m_Will = args[0].intValue();
        return true;
    }
    if ((methodName == skString("GetWill") || methodName == skString("GetWil") ||
         methodName == skString("GetWillpower")) &&
        args.entries() == 0) {
        returnValue = skRValue(m_Will);
        return true;
    }
    // M37: the real creature-kind field -- see monster_executable.h.
    // Both setters take a bool in the corpus (`SetUndead(true)`), but the
    // real handler ignores the argument and just assigns the kind, so a
    // hypothetical SetUndead(false) would still mark the creature undead;
    // transcribed as-is rather than "fixed", since nothing calls it that
    // way.
    if (methodName == skString("SetUndead") && args.entries() == 1) {
        m_CreatureKind = kCreatureUndead;
        return true;
    }
    if (methodName == skString("SetSpider") && args.entries() == 1) {
        m_CreatureKind = kCreatureSpider;
        return true;
    }
    if (methodName == skString("IsUndead") && args.entries() == 0) {
        returnValue = skRValue(undead());
        return true;
    }
    if (methodName == skString("GetSpellToHit") && args.entries() == 0) {
        returnValue = skRValue(spellToHit());
        return true;
    }
    if (methodName == skString("GetSpellResistance") && args.entries() == 0) {
        returnValue = skRValue(spellResistance());
        return true;
    }
    // ---- M43: the creature's own spell loadout ----
    //
    // Dispatcher 0x10084924 case 8, whose body is duplicated verbatim as
    // the standalone FUN_10086ab0. The two forms differ only in the stored
    // chance: `AddSpell(Item)` stores 100 and `AddSpell(Item, n)` stores n.
    if (methodName == skString("AddSpell") &&
        (args.entries() == 1 || args.entries() == 2)) {
        // Every real call site is `Item = Level.CreateEntity(<typeId>);
        // Item.SetLevel(n); AddSpell(Item[, chance]);` -- so the object
        // being handed over is exactly the one LevelExecutable is still
        // holding, and the monster takes ownership of it here (the real
        // AddSpell adds it to the creature's own object collection and
        // makes the creature its owner).
        auto* spell = dynamic_cast<ItemExecutable*>(args[0].obj());
        if (!spell) return true;
        std::unique_ptr<ItemExecutable> owned = m_Stack.level().TakePendingCreatedEntity();
        // Refuse anything this creature would not actually own. The slots
        // hold raw pointers (the real table does too), so storing one whose
        // lifetime belongs to somebody else is a dangling read waiting to
        // happen. Every real call site is a fresh CreateEntity handed
        // straight over, so this only rejects hypotheticals.
        bool alreadyOwned = false;
        for (const auto& held : m_OwnedSpells) {
            if (held.get() == spell) alreadyOwned = true;
        }
        if (owned.get() != spell && !alreadyOwned) {
            std::printf("MonsterExecutable: AddSpell -- ignoring a spell this creature does not "
                        "own\n");
            return true;
        }
        int chance = args.entries() == 2 ? args[1].intValue() : 100;

        // The real slot search: take the first slot that is empty **or
        // already holds this same spell typeId** -- so re-adding a spell
        // replaces it instead of consuming a second slot, and a fifth
        // distinct spell is dropped (the real code logs and returns).
        int index = 0;
        for (; index < kSpellSlots; ++index) {
            if (!m_SpellSlots[index].spell) break;
            if (m_SpellSlots[index].spell->templateId() == spell->templateId()) break;
        }
        if (index >= kSpellSlots) {
            std::printf("MonsterExecutable: AddSpell -- %s already has %d spells, dropping one\n",
                        name().c_str(), kSpellSlots);
            return true;
        }
        m_SpellSlots[index].spell = spell;
        m_SpellSlots[index].chance = chance;
        // `FUN_1006d510(spell, monster)` -- the owner write the whole
        // caster half of FUN_100458e4 reads back.
        spell->SetSpellOwner(this);
        if (owned) m_OwnedSpells.push_back(std::move(owned));
        // `if (spell->typeId == 0xfaa) monster+0x32c = slot` -- the cached
        // Blind slot, see ChooseSpell().
        if (spell->templateId() == kBlindSpellTypeId) m_BlindSpellSlot = index;
        return true;
    }
    if (methodName == skString("SetMeleeRoll") && args.entries() == 1) {
        // `monster+0x304`. See meleeRoll()/RollForMelee() for the direction
        // this reads in, which is the opposite of what the name suggests.
        m_MeleeRoll = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetPlaySpellCasting") && args.entries() == 1) {
        m_PlaySpellCasting = args[0].boolValue();
        return true;
    }
    if (methodName == skString("SetAttachedWeapon") && args.entries() == 1) {
        // M46: `monster+0x2c2`, a signed 16-bit models.idx index. 92 call
        // sites across 86 shipped creature scripts, and the only values
        // any of them pass are 222/223/224/225/226 -- sword, mace, dagger,
        // bow, ax. See attachedWeaponModel().
        m_AttachedWeaponModel = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDamageMin") && args.entries() == 1) {
        m_DamageMin = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDamageMax") && args.entries() == 1) {
        m_DamageMax = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetArmorValue") && args.entries() == 1) {
        m_ArmorValue = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetMaxHealth") && args.entries() == 1) {
        m_MaxHealth = args[0].intValue();
        m_CurrentHealth = m_MaxHealth;
        return true;
    }
    if (methodName == skString("SetWimpy") && args.entries() == 1) {
        m_Wimpy = args[0].intValue();
        return true;
    }
    // ---- M32: the real Ai* package calls ----
    //
    // Only AiDetect() and AiSleep() are ever called by any script in the
    // corpus; the rest exist for completeness and for the native paths
    // (the Fear spell reaches AiFlee's package through
    // SetAiPackageTimed()). Package values are the decompiled ones -- see
    // monster_executable.h's AiPackage enum.
    if (methodName == skString("AiDetect") && args.entries() == 0) {
        m_AiPackage = kAiIdle;
        return true;
    }
    if (methodName == skString("AiSleep") && args.entries() == 0) {
        m_AiPackage = kAiAsleep;
        return true;
    }
    if (methodName == skString("AiAttack") && args.entries() >= 1) {
        m_AiPackage = kAiPursue;
        return true;
    }
    if (methodName == skString("AiFlee") && args.entries() >= 1) {
        // The dispatcher's own flee case sets the package with no timer --
        // the timed form is what the Fear spell uses.
        m_AiPackage = kAiFlee;
        m_SavedAiPackage = kAiIdle;
        return true;
    }
    if (methodName == skString("AiSpellAssistTarget") && args.entries() >= 1) {
        // Faithfully inert: the dispatcher stores package 6 and a target,
        // and **nothing anywhere in the binary ever reads package 6** --
        // a creature left in it matches neither branch of the real AI tick
        // and simply stops acting. Reproduced rather than invented.
        m_AiPackage = kAiSpellAssist;
        return true;
    }
    if ((methodName == skString("AiActivate") || methodName == skString("AiWounded") ||
         methodName == skString("AiPursue")) &&
        args.entries() >= 0) {
        // Genuine no-ops: each of these dispatcher cases falls straight
        // through to the shared `break` and stores nothing.
        return true;
    }
    if (methodName == skString("SetParalyzed") && args.entries() >= 1) {
        SetParalyzed(args[0].intValue());
        return true;
    }
    // M31: the real stand-off distance (monster+0x2dc). Same
    // scaled-squared units as SetChaseRadius -- see monster_executable.h.
    if (methodName == skString("SetAttackRange") && args.entries() == 1) {
        m_AttackRange = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetMeleeAttackRange") && args.entries() == 1) {
        // Accepted and deliberately ignored: the real dispatcher's case for
        // this name falls straight through to its shared `break` and stores
        // nothing, so it is a genuine no-op in the shipped game too (only
        // one script in the whole corpus calls it). Handled explicitly so
        // it stops being reported as "not implemented" when reproducing it
        // faithfully means doing nothing.
        return true;
    }
    if (methodName == skString("SetChaseRadius") && args.entries() == 1) {
        m_ChaseRadius = args[0].intValue();
        return true;
    }
    // M30: a creature's level. Real spell scripts scale their damage off
    // it (`Random(3, (GetOwner().GetLevel() + 1) * 2)`), so leaving the
    // setter soft-failing meant GetLevel() had nothing to return.
    if (methodName == skString("SetLevel") && args.entries() == 1) {
        m_Level = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetLevel") && args.entries() == 0) {
        returnValue = skRValue(m_Level);
        return true;
    }
    if (methodName == skString("SetMob") && (args.entries() == 1 || args.entries() == 2)) {
        // M39: the real handler's very first side effect is
        // `monster+0x2ef = 1`, and that byte is read by exactly one other
        // place in the binary -- SetZone's own XP distribution (see
        // ZoneScriptExecutable::SetZone). So calling SetMob is what opts a
        // creature into the zone's experience budget; a scripted NPC that
        // never calls it is skipped.
        m_Mob = args[0].intValue();
        m_CountsForZoneExperience = true;
        // M43: and the rest of it. The XP flag is only the handler's first
        // line -- the body is a **per-tier stat template scaled to the zone
        // the creature is standing in**, and it overwrites most of what the
        // script set by hand a few lines earlier.
        ApplyMobTemplate(m_Mob, args.entries() == 2 ? args[1].intValue() : 0);
        return true;
    }
    // M28: the real animation clip numbers. Every monster script sets all
    // four; they index the model resource's own clip table (see
    // world/model_archive.h's AnimationClip -- decoded this session). All
    // of these soft-failed before, so creatures rendered as static
    // resting-pose statues sliding around the level.
    if (methodName == skString("SetIdleAnimation") && args.entries() == 1) {
        m_IdleAnim = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetWalkAnimation") && args.entries() == 1) {
        m_WalkAnim = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetSwingAnimation") && args.entries() == 1) {
        m_SwingAnim = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDeathAnimation") && args.entries() == 1) {
        m_DeathAnim = args[0].intValue();
        return true;
    }
    // `PlayAnimation(n)` / `PlayAnimationOffset(n)` -- both appear in real
    // Init() bodies to set the creature's starting pose (e.g. arat.s's
    // `PlayAnimation(0)`). Treated identically here: the host AI loop
    // overrides the clip every tick from the creature's own state anyway,
    // so this only decides what it looks like before it first acts.
    if ((methodName == skString("PlayAnimation") ||
         methodName == skString("PlayAnimationOffset")) &&
        args.entries() == 1) {
        m_CurrentAnim = args[0].intValue();
        return true;
    }
    // Real per-instance appearance, previously soft-failed and therefore
    // invisible in the port (every creature rendered as skin 0 at 1:1
    // scale). Both showed up in a real play session's own soft-fail log.
    if (methodName == skString("SetSkin") && args.entries() == 1) {
        // A models.idx resource genuinely carries several skins
        // (Model::skinCount, docs/MODEL_FORMAT.md) -- e.g. the real
        // corpus picks skin 9/10 for some creature variants, which the
        // port was drawing with skin 0's texture.
        m_Skin = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetScale") && args.entries() == 1) {
        // 8.8 fixed point, so 256 == 1:1 -- consistent with every real
        // call site: literal values cluster at 230-306, and azra_rat.s
        // uses `SetScale(Random(206,306))` to vary rat size around 256.
        // A value of exactly 256 being the identity is what makes that
        // range read as "0.8x to 1.2x".
        m_Scale = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetPlayer") && args.entries() == 0) {
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Player), false);
        return true;
    }
    if (TryHandleRandom(methodName, args, returnValue)) {
        // M21: azra_rat.s's own `SetScale(Random(206,306))` -- bare, same
        // real gap native_binding_common.h's TryHandleRandom() comment
        // explains (previously soft-failed to 0 on every monster).
        return true;
    }
    // M16: NPC-mode fields/calls -- see class comment.
    if (methodName == skString("SetUsable") && args.entries() == 1) {
        m_Usable = args[0].boolValue();
        return true;
    }
    if (methodName == skString("SetUseText") && args.entries() == 1) {
        m_UseTextId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetInvulnerable") && args.entries() == 1) {
        m_Invulnerable = args[0].boolValue();
        return true;
    }
    if (methodName == skString("OpenMenu") && args.entries() == 1) {
        // M17: ReopenMenu(), not OpenMenu() -- an NPC's OnUse() call here
        // starts a fresh conversation each time, and the real quest-state
        // branching lives in the target menu's Init() (see menu_stack.h's
        // comment), which needs to rerun on every visit, not just the
        // first.
        m_Stack.ReopenMenu(ToStdString(args[0].str()));
        return true;
    }
    if (methodName == skString("SetLoot") && args.entries() >= 2) {
        // M21: see lootTag()'s comment -- main.cpp's death handling
        // resolves and spawns this as a real world pickup.
        //
        // M39: the trailing pair is a **drop chance**, not a quantity.
        // The real handler (monster dispatcher case 0xd) rolls
        // `rand(min, max)` and assigns the loot only when the roll comes
        // up equal to `min`, discarding the name otherwise -- a 1-in-
        // (max - min + 1) chance, decided once at Init, not at death. So
        // `SetLoot(300, "Loot_ratseye", 1, 8)` is a one-in-eight rat eye
        // and `SetLoot(300, "loot_gold25-35")` (no trailing pair) always
        // drops. This port used to ignore the pair entirely, which made
        // every creature in the game a guaranteed drop.
        if (args.entries() >= 4) {
            int lo = args[2].intValue();
            int hi = args[3].intValue();
            int roll = hi > lo ? lo + std::rand() % (hi - lo + 1) : lo;
            if (roll != lo) return true;  // no loot on this instance
        }
        m_LootTag = ToStdString(args[1].str());
        return true;
    }
    // M39: `SetPosition(x, y, z)` -- not a decoration. azra.s's
    // `Birg.SummonMe()` / `Skelos.SummonMe()` / `Vil1..4.SummonMe()` /
    // `Heather.SummonMe()` calls are the zone's whole "the cast arrives
    // for this scene" mechanism, and SummonMe turns out **not to be a
    // native at all**: it is an ordinary script handler each of those
    // NPCs' own .s files declares, whose entire body is a single
    // SetPosition (monsters/birgiddaazra.s: `SummonMe[() {
    // SetPosition(31083, 3483, -2816); }]`). So the only thing that was
    // missing was the setter itself -- which soft-failed, leaving every
    // summoned NPC standing wherever the .ent file put them.
    //
    // SetPositionMirror is the network-replicated twin (same
    // DoorOpened()/DestroyObjectMirror() pattern -- no multiplayer here),
    // and azra_rat.s's own OnKilled uses it to move Trothgar.
    if ((methodName == skString("SetPosition") ||
         methodName == skString("SetPositionMirror")) &&
        args.entries() >= 3) {
        m_PositionX = args[0].intValue();
        m_PositionY = args[1].intValue();
        m_PositionZ = args[2].intValue();
        m_PositionDirty = true;
        return true;
    }
    if (methodName == skString("DestroyObjectMirror")) {
        // M23: see destroyed()'s comment -- azra.s's own
        // `M1.DestroyObjectMirror(M1)` (self-passed, network/replication
        // bookkeeping in the original, same `DoorOpened()`-style pattern
        // -- this port has no multiplayer) after a real save flag says
        // an entity is no longer relevant.
        m_Destroyed = true;
        return true;
    }
    if (methodName == skString("SetAttackNoise") && args.entries() == 1) {
        // M28: see PlayAttackNoise()'s comment.
        m_AttackNoiseId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDeathNoise") && args.entries() == 1) {
        m_DeathNoiseId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetIsHitNoise") && args.entries() == 1) {
        m_IsHitNoiseId = args[0].intValue();
        return true;
    }
    // SetWalkAnimation/SetSwingAnimation/SetDeathAnimation/
    // SetIdleAnimation/PlayAnimationOffset/SetScale/AiDetect -- azra_rat.s
    // calls all of these, but this port has no skeletal animation and
    // renders every entity at a fixed scale/skin (consistent with every
    // prior milestone -- see docs/PORT_ROADMAP.md's M12 entry), so they
    // fall through to the soft-fail below rather than getting dedicated
    // no-op handlers -- there's nothing meaningful to store them into yet.
    if (skScriptedExecutable::method(methodName, args, returnValue, context)) {
        return true;
    }
    return SoftFailNativeCall("Monster", methodName, args, returnValue);
}


// M38: see native_binding_common.h's StoreScriptObjectField().
bool MonsterExecutable::setValue(const skString& fieldName, const skString& attribute,
                     const skRValue& value) {
    if (StoreScriptObjectField(m_ObjectFields, fieldName, value)) return true;
    return skScriptedExecutable::setValue(fieldName, attribute, value);
}

bool MonsterExecutable::getValue(const skString& fieldName, const skString& attribute, skRValue& value) {
    if (LoadScriptObjectField(m_ObjectFields, fieldName, value)) return true;
    return skScriptedExecutable::getValue(fieldName, attribute, value);
}

}  // namespace sk_bindings
