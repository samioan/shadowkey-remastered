#include "simkin_bindings/monster_executable.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "assets/sound_archive.h"
#include "assets/string_table.h"
#include "audio/audio_engine.h"
#include "simkin_bindings/combat.h"
#include "simkin_bindings/effects.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/stats_damage.h"
#include "simkin_bindings/use_prompt.h"
#include "skExecutableContext.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace sk_bindings {

sk::SoundArchive* MonsterExecutable::entitySounds() const { return m_Stack.sounds(); }
sk::AudioEngine* MonsterExecutable::entityAudio() const { return m_Stack.audio(); }

MonsterExecutable::MonsterExecutable(const skString& filename, skExecutableContext& ctxt,
                                      const sk::StringTable* strings, PlayerExecutable& player,
                                      MenuStack& stack)
    : skScriptedExecutable(filename, ctxt),
      m_Strings(strings),
      m_Player(player),
      m_Stack(stack),
      m_Interpreter(ctxt.getInterpreter()) {
    // M92: RunScript() swaps this object's script file out from under it;
    // the base needs the skTreeNodeObject half of `this` to do that.
    AttachEntityScript(this);
    // M59: every creature carries a shop (monster+0x330); only the ones
    // whose script calls AddProduct ever fill it. See store.h.
    m_Store.SetDatabase(&m_Stack.products());
    m_Store.SetStrings(strings);
}

MonsterExecutable::~MonsterExecutable() = default;

std::string MonsterExecutable::name() const {
    if (m_Strings && entityNameId() >= 0) return m_Strings->Get(entityNameId());
    return entityId().empty() ? std::string("?") : entityId();
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

namespace {
// M100: entities.txt `1015 140 2 lothna\fortifyingcrystal.s`, the one typeId
// FUN_10081844 tests.
constexpr int kFortifyingCrystalTypeId = 0x3f7;
// M100: FindPathNode's `monster->z += 300` after the surface snap.
constexpr int kPathNodeSurfaceLift = 300;
}  // namespace

int MonsterExecutable::ApplyDamage(int amount, SpellActor* attacker, bool ranged) {
    // m_Invulnerable (M16): real essential-NPC scripts (Tanyin Aldwyr and
    // the other named quest NPCs) call SetInvulnerable(true) in Init() --
    // honoring it here means main.cpp's melee target selection doesn't
    // even need its own separate check for the common case, though it
    // still skips them too (see main.cpp) so they're never shown as a
    // combat target in the first place.
    // M58: FUN_10049e78's first line -- the stats block's own DoDamage
    // refuses outright while the second periodic channel's kind is 4. That
    // is Sanctuary, whose channel M48 could only describe as "purely a
    // duration"; this is the gate it was for. It sits before every other
    // check because in the engine it is the whole function's `if`.
    if (m_Stats.periodicKind() == ActorStats::kPeriodicSanctuaryTimer) return 0;
    if (!m_Alive || m_Invulnerable) return 0;
    // M100: FUN_10081844, the creature's own damage slot, between its gates
    // and the stats function:
    //
    //     if (monster->+0x2e8 == 0 && attacker && monster->typeId != 0x3f7) {
    //         ...take the attacker as the target, package 3...
    //         if (attacker == player + 0x3ac) {
    //             if (monster->vtable[0xf0]()) return 0;
    //             args = { (int)damage };
    //             script->OnHit(args);
    //         }
    //     }
    //     ...hit noise, red flash...
    //     return FUN_10049e78(stats, damage, attacker, p4, p5);
    //
    // So OnHit hears only the *player's* hits, before any of the damage is
    // applied and before the attacker's terms, with the site's damage as its
    // one argument (every shipped handler declares none, which Simkin
    // allows). Type 0x3f7 is `lothna\fortifyingcrystal.s`. `+0x2e8` is
    // GuardPlayer's pointer (M97: never set) and `vtable[0xf0]` answers true
    // only for the unplaced category-13 class, so neither is modelled.
    //
    // Whatever the handler does, the damage still lands: the gates above ran
    // before it. A handler that marks the creature dead (Umbra Keth's
    // SetDead(true)) makes the hit that follows unable to kill it -- see the
    // fatal branch below.
    if (attacker && attacker->isPlayerActor() && m_EntityTypeId != kFortifyingCrystalTypeId) {
        skRValueArray hitArgs;
        hitArgs.append(skRValue(static_cast<int>(static_cast<int16_t>(amount))));
        InvokeScriptEvent("OnHit", hitArgs);
    }
    // M98: the attacker's terms (stats_damage.h). This used to refuse
    // `amount <= 0` up front, which the engine does not: a player swing the
    // armour fully absorbs still reaches FUN_10049e78 and still lands the
    // strength term. What is left at zero changes nothing and makes no
    // noise, as before.
    const StatsDamage resolved = ResolveStatsDamage(*this, amount, attacker, ranged);
    if (resolved.refused || resolved.damage == 0) return 0;
    amount = resolved.damage;
    // `if (health <= dmg) die; else health -= dmg` -- the same test as
    // subtracting and checking for <= 0.
    m_CurrentHealth -= amount;
    if (m_CurrentHealth <= 0) {
        m_CurrentHealth = 0;
        // M100: the death routine's first line, `if (+0x1e4) return;`. A
        // creature its own OnHit just marked dead takes the health loss and
        // nothing else.
        if (!m_Alive) return amount;
        m_Alive = false;
        m_DeathOwed = true;
        // M97: FUN_10049e78's fatal branch, `stats->vtable[0x28](stats,
        // attacker, p4)` -- the attacker of *this* hit goes to the death
        // routine, and it is the only record of a killer the engine keeps.
        m_Killer = attacker;
        // M75: a corpse offers no use prompt. The engine's own fatal-hit
        // tail (0x10083e2c, inside the creature damage path 0x10083c04)
        // writes `entity+0xd8 = 0` right beside the `+0xd5` dead flag.
        // main.cpp's target search already skips dead creatures, so this
        // changes nothing on its own -- it keeps the flag itself honest
        // for anything that reads it directly (use_prompt.h).
        m_Usable = false;
        // M28: death noise wins over hit noise -- both fire from the same
        // single ApplyDamage() choke point (melee and spell HitTarget
        // both route through here), so there's no risk of ever double-
        // playing.
        PlayNoise(m_DeathNoiseId);
    } else {
        PlayNoise(m_IsHitNoiseId);
    }
    return amount;
}

// M100: see the header for FUN_10005d60, FUN_10068480 and FUN_1006410c.
void MonsterExecutable::SetDead(bool dead, int decaySeconds) {
    m_Alive = !dead;
    if (!dead) {
        m_DecayArmed = false;
        return;
    }
    if (decaySeconds != 0) {
        m_DecayArmed = true;
        const long long now = m_WallClock ? m_WallClock() : static_cast<long long>(std::time(nullptr));
        m_DecayDeadline = now + decaySeconds;
    }
}

bool MonsterExecutable::TakeDeathOwed() {
    const bool owed = m_DeathOwed;
    m_DeathOwed = false;
    return owed;
}

bool MonsterExecutable::TickDecay() {
    if (!m_DecayArmed) return false;
    const long long now = m_WallClock ? m_WallClock() : static_cast<long long>(std::time(nullptr));
    if (now < m_DecayDeadline) return false;
    m_DecayArmed = false;
    // FUN_1001b484 -- the same world removal an arrow's despawn and
    // DestroyObjectMirror use, which this port spells m_Destroyed.
    m_Destroyed = true;
    InvokeScriptEvent("OnDecay", skRValueArray());
    return true;
}

void MonsterExecutable::InvokeScriptEvent(const char* name, const skRValueArray& args) {
    if (!m_Interpreter) return;
    skRValueArray callArgs = args;
    skRValue ret;
    skExecutableContext ctxt(m_Interpreter);
    try {
        skScriptedExecutable::method(skString(name), callArgs, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("MonsterExecutable: PARSE ERROR in %s(): %s\n", name, e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("MonsterExecutable: RUNTIME ERROR in %s(): %s\n", name, e.toString().ptr());
    }
}

// M97: FUN_1004a104 on a creature's stats block. It is the same function
// the player's AddExperience transcribes; what differs is which arm it
// takes. The class-table threshold is guarded by `owner->vtable[0xcc]()`,
// the player predicate, so a creature keeps the function's opening value:
//
//     threshold = (stats->level + 1) * 1000;
//
// And the level-up slot it then calls, `stats->vtable[0x24]`, is 0x100a17dc
// in the creature's stats vtable -- a bare `bx lr`. So a creature's level
// (the halfword the regeneration channels add per second) goes up and
// nothing else happens: no point, no sound, no recompute.
//
// A creature only ever gets here as a killer, and in this port that means
// its arrow or spell killed another creature -- the port's creature melee
// has exactly one target, the player, whose death is not this path.
void MonsterExecutable::AddActorExperience(int amount) {
    const int delta = static_cast<int16_t>(amount);
    const int threshold = (static_cast<int16_t>(m_Level) + 1) * 1000;
    if (threshold < m_Experience + delta) m_Level = static_cast<int16_t>(m_Level + 1);
    m_Experience += delta;
}

// M97: FUN_10083c04, the creature death routine, and the one line of it
// that is about experience:
//
//     if (attacker != 0 && monster->+0x2e8 == 0)
//         attacker->vtable[0x20](attacker, (short)monster->stats.expWorth);
//
// `expWorth` is the stats block's `+0x0e`, read at the moment of death, so
// what gets paid is M39's SetZone share if the zone applied one, and it
// includes any ExpWorth effect still running. `+0x2e8` is GuardPlayer's
// "whose side am I on" pointer: a creature fighting for the player pays
// nothing for dying. No shipped script calls GuardPlayer and this port
// stores nothing for it, so the guard never refuses here.
//
// The routine's other early exit, `vtable[0xf0]`, answers true only for
// the category-13 creature class, which no shipped zone places.
void MonsterExecutable::PayKillExperience() {
    if (m_Killer) m_Killer->AddActorExperience(m_ExpWorth);
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
        // M75: the base class directly, not this->method() -- an NPC
        // with no OnUse handler is an ordinary shape (bled.s, lt_breser.s,
        // diamond_spider_queen.s all name a use text and define nothing),
        // and routing it through the native dispatcher logged a bogus
        // "OnUse(0) -- not implemented" soft-fail on every Use press. Same
        // one-line fix M35 already made in ItemExecutable::InvokeOnUse().
        skScriptedExecutable::method(skString("OnUse"), args, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("MonsterExecutable: PARSE ERROR in OnUse(): %s\n", e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("MonsterExecutable: RUNTIME ERROR in OnUse(): %s\n", e.toString().ptr());
    }
}

bool MonsterExecutable::InvokeOnDetect(skiExecutable* detected) {
    if (!m_Interpreter) return false;
    skRValueArray args;
    // The real call appends exactly one argument: the detected entity,
    // boxed as a SimKin object (0x100831ac builds it from `target + 0x14`,
    // the skiExecutable base sub-object, and 0x10083208 appends it). Every
    // shipped OnDetect declares the parameter and none of them reads it --
    // they all call GetPlayer() instead -- but passing it is free and it is
    // what the engine does.
    args.append(detected ? skRValue(detected, false) : skRValue(0));
    skRValue ret;
    skExecutableContext ctxt(m_Interpreter);
    try {
        // Base class directly, for the same reason InvokeOnUse() does it:
        // the great majority of creatures define no OnDetect, and routing
        // a per-tick call through the native dispatcher would log a
        // soft-fail 25 times a second for every one of them.
        return skScriptedExecutable::method(skString("OnDetect"), args, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("MonsterExecutable: PARSE ERROR in OnDetect(): %s\n", e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("MonsterExecutable: RUNTIME ERROR in OnDetect(): %s\n", e.toString().ptr());
    }
    return true;
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
        // M81: the base class directly, for the reason InvokeOnUse() above
        // already spells out. Most creature scripts define no OnKilled at
        // all -- of the corpus's monster scripts only a handful do -- and
        // the native dispatcher logged a bogus "OnKilled(0) -- not
        // implemented" soft-fail for every one of the others. Invisible
        // while only four call sites reached this; a hundred lines of noise
        // per fight once every death did. Third instance of the same fix
        // (M35's ItemExecutable::InvokeOnUse, M75's here).
        skScriptedExecutable::method(skString("OnKilled"), args, ret, ctxt);
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
    // M97: the damage callback passes no attacker, and that is the engine's
    // answer too. FUN_10049780's poison tick calls the damage slot with
    // `(stats, n, 0, 0, 0)`, and its burn arm (kind 8) skips the damage slot
    // altogether and calls the death slot with `(stats, 0, 0)`. The spell
    // that set either one does not own it. A creature poisoned or burned to
    // death is worth nothing to anyone, even though the same spell's own
    // hit would have paid in full.
    m_Stats.Tick(
        *this, deltaUnits, [this](int damage) { ApplyDamage(damage); },
        [this](int kind) {
            if (kind == ActorStats::kPeriodicFatigueRegen) {
                m_Fatigue += m_Level;
            } else if (kind == ActorStats::kPeriodicHealthRegen) {
                SetActorHealth(m_CurrentHealth + m_Level);
            }
        });
}

// M58: FUN_1004ad40's field map for a creature -- see spell_actor.h. The
// eight attributes and the 32-bit tail fields other than experience have no
// storage here, which makes an `AddEffect(..., Luck, ...)` on a creature a
// no-op; in the engine it writes a field nothing reads, which is the same
// thing. M97 gave experience storage, because AddActorExperience reads it.
int* MonsterExecutable::EffectStatSlot(int stat) {
    switch (stat) {
        case kEffectStatAttack: return &m_Attack;                   // +0x00
        case kEffectStatDefense: return &m_Defense;                 // +0x02
        case kEffectStatSpellcast: return &m_Spellcast;             // +0x04
        case kEffectStatMagicResistance: return &m_MagicResistance; // +0x06
        case kEffectStatMinDamage: return &m_DamageMin;             // +0x08
        case kEffectStatMaxDamage: return &m_DamageMax;             // +0x0a
        case kEffectStatArmorValue: return &m_ArmorValue;           // +0x0c
        case kEffectStatExpWorth: return &m_ExpWorth;               // +0x0e
        case kEffectStatWill: return &m_Will;                       // +0x1a
        case kEffectStatMaxHealth: return &m_MaxHealth;             // +0x24
        case kEffectStatHealth: return &m_CurrentHealth;            // +0x2a
        case kEffectStatFatigue: return &m_Fatigue;                 // +0x2c
        case kEffectStatMagicka: return &m_Magicka;                 // +0x2e
        case kEffectStatExperience: return &m_Experience;           // +0x30, M97
        case kEffectStatLevel: return &m_Level;                     // +0x34
        default: return nullptr;
    }
}

void MonsterExecutable::ApplyStatModifier(int statIndex, int delta, int durationSeconds) {
    AddEffect(*this, kDurationTimed, statIndex, kOpIncrement, delta, durationSeconds);
}

void MonsterExecutable::JitterAttackCadence() {
    // Real reset: `rand & 0x1f`. Uses the same host RNG the rest of this
    // port's combat rolls use rather than reproducing the engine's own
    // generator, which is not decompiled. It makes the next decision land
    // 22.5 to 25.6 ticks later instead of exactly 25.6, which is the whole
    // point -- a pack that acquired the player on one frame would
    // otherwise swing in lockstep forever.
    m_AttackCadence = std::rand() & 0x1f;
}

bool MonsterExecutable::TickLifespan(int deltaUnits) {
    if (m_LifespanUnits <= 0) return false;
    // The real line is `+0x2ec -= 3 * FUN_1001afa4(engine)` -- a lifespan
    // burns three times as fast as every other timer in the tick. Nothing
    // in the corpus arms it, so this is transcription, not observation.
    m_LifespanUnits -= 3 * deltaUnits;
    if (m_LifespanUnits > 0) return false;
    m_LifespanUnits = 0;
    return true;
}

bool MonsterExecutable::method(const skString& methodName, skRValueArray& args,
                                skRValue& returnValue, skExecutableContext& context) {
    // M76: Monster(AI) bindings 4 and 3 (dispatcher cases 4 and 3),
    // `monster+0x306` and `monster+0x307`. Both were soft-failing; both are
    // multiplayer-only in effect. See on_detect.h.
    if (methodName == skString("SetAlwaysOnDetect") && args.entries() == 1) {
        m_AlwaysOnDetect = args[0].boolValue();
        return true;
    }
    if (methodName == skString("DetectOnKilled") && args.entries() == 1) {
        m_DetectOnKilled = args[0].boolValue();
        return true;
    }
    if (methodName == skString("SetAggressive") && args.entries() == 1) {
        m_Aggressive = args[0].boolValue();
        return true;
    }
    // ---- M77: the remaining Monster(AI) bindings ----
    //
    // With these the class answers all 55 names in the real trie
    // (docs/SIMKIN_NATIVE_API.md's `0x14da4`). The ones with no corpus
    // caller are here because "stored and inert" and "soft-failed" are
    // different claims and only the first one is checkable.
    //
    // SetBoss (15 scripts): the second attack throttle, and the
    // SetCanTeleport block's implicit target. See monster_ai.h.
    if (methodName == skString("SetBoss") && args.entries() == 1) {
        m_Boss = args[0].boolValue();
        return true;
    }
    // SetAttackSpeed (0 scripts): seconds between a boss's swings. The
    // constructor's 2 is therefore what every boss in the game uses.
    if (methodName == skString("SetAttackSpeed") && args.entries() == 1) {
        m_AttackSpeedSeconds = args[0].intValue();
        return true;
    }
    // SetCanTeleport (4 scripts) / ReplicateTeleport (1). The second only
    // ever broadcasts the first over a multiplayer session
    // (`if (engine+0x5c0 && self+0x2ad) FUN_1003cbd0(...)`), so it is a
    // real no-op here rather than an unimplemented one.
    if (methodName == skString("SetCanTeleport") && args.entries() == 1) {
        m_CanTeleport = args[0].boolValue();
        return true;
    }
    if (methodName == skString("ReplicateTeleport")) {
        return true;
    }
    // SetImmobile (3 scripts): dispatcher case 0xf writes the same byte
    // (`monster+0x2bc`) SetParalyzed's countdown clears, so an immobile
    // creature is one holding the pose a paralysed one is put into.
    if (methodName == skString("SetImmobile") && args.entries() == 1) {
        m_Immobile = args[0].boolValue();
        return true;
    }
    // StopAnimating (1 script): `+0x302 = !b`, and `+0x12c = -1` when
    // switching it off, so the creature also drops whatever clip it was
    // playing. Every PlayAnimation call in the tick and in the attack is
    // guarded by this byte.
    if (methodName == skString("StopAnimating") && args.entries() == 1) {
        const bool stop = args[0].boolValue();
        if (stop) m_CurrentAnim = -1;
        m_Animating = !stop;
        return true;
    }
    // SetHealth (30 scripts) is byte-for-byte the same dispatcher case as
    // SetMaxHealth (both write the stats block's +0x12, +0x24 and +0x2a),
    // i.e. a pure alias. It was soft-failing, which left 30 creatures on
    // whatever SetMaxHealth happened to leave behind.
    if (methodName == skString("SetHealth") && args.entries() == 1) {
        m_MaxHealth = args[0].intValue();
        m_CurrentHealth = m_MaxHealth;
        return true;
    }
    // DoDamage (20 scripts): `stats->vtable[0x10](stats, n, 0, 0, 0)` --
    // the creature damages *itself*, unsourced. Routed through ApplyDamage
    // so invulnerability, death and OnKilled all behave as they would from
    // any other source. M97: "unsourced" is the null attacker, so a
    // creature a script kills this way is worth no experience to anyone.
    if (methodName == skString("DoDamage") && args.entries() >= 1) {
        ApplyDamage(args[0].intValue());
        return true;
    }
    // SetLifespan (0 scripts): `+0x2ec = seconds << 8`. See TickLifespan().
    if (methodName == skString("SetLifespan") && args.entries() == 1) {
        m_LifespanUnits = args[0].intValue() << 8;
        return true;
    }
    // SetItemRequiredToHit (0 scripts): `+0x2e0`, read by the attack
    // resolution as "only this item id can hurt me".
    if (methodName == skString("SetItemRequiredToHit") && args.entries() == 1) {
        m_ItemRequiredToHit = args[0].intValue();
        return true;
    }
    // SetEnemy / Follow (0 scripts each) both write `monster+0x20c`, the
    // target, without touching the package; GuardPlayer (0) writes
    // `monster+0x2e8`, the "whose side am I on" pointer whose only reader
    // is the creature-vs-creature scan in the look arm. This port's tick
    // has exactly one possible target (the player, as in singleplayer) and
    // no second faction, so all three are stored-and-inert -- accepting
    // them is what makes that a statement rather than a silent miss.
    if ((methodName == skString("SetEnemy") || methodName == skString("Follow") ||
         methodName == skString("GuardPlayer")) &&
        args.entries() == 1) {
        return true;
    }
    // FindPathNode(name) (1 script): looks up a named node in the zone's
    // `.pth` table and moves the creature onto the one nearest its target.
    // The `.pth` format is still undecoded past its header
    // (docs/ZONE_FORMAT.md), so this returns the engine's own
    // node-not-found result, 0, rather than pretending.
    //
    // M100: decoded (path_table.h), so no longer always 0. Case 5:
    //
    //     path = FUN_1001ada0(engine, name);               // by strcmp
    //     if (!path) return 0;
    //     if (!monster->target) monster->target = player;  // +0x20c
    //     wp = FUN_10086970(monster, path, target);        // nearest to target
    //     if (!wp) return 0;
    //     monster->vtable[0x14](wp.x, wp.y, 0);            // move
    //     FUN_100686e0(monster, tile);  monster->z += 300; // onto the surface
    //     return 1;
    //
    // The target is the player in this port whatever the AI thinks, because
    // the player is the only thing a creature ever targets here. Umbra
    // Keth's DelayReached is the one caller, and it is what brings the boss
    // back from its fake death: the path is `UmbraKeth` in all three crypts.
    if (methodName == skString("FindPathNode") && args.entries() >= 1) {
        int found = 0;
        if (const PathNode* path =
                FindPath(m_Stack.level().paths(), ToStdString(args[0].str()))) {
            if (const PathWaypoint* wp =
                    NearestWaypoint(*path, m_Player.positionX(), m_Player.positionY())) {
                RequestSurfaceMove(wp->x, wp->y, kPathNodeSurfaceLift);
                found = 1;
            }
        }
        returnValue = skRValue(found);
        return true;
    }
    // SetState(package, seconds) (0 scripts) is the script-callable form of
    // the timed package the Fear spell uses -- dispatcher case 0xb, the
    // same FUN_10086b98 body SetAiPackageTimed() already implements.
    if (methodName == skString("SetState") && args.entries() >= 2) {
        SetAiPackageTimed(args[0].intValue(), args[1].intValue());
        return true;
    }
    // The getters, none of which any shipped script calls.
    if (methodName == skString("GetAttackNoise")) {
        returnValue = skRValue(m_AttackNoiseId);
        return true;
    }
    if (methodName == skString("GetDeathNoise")) {
        returnValue = skRValue(m_DeathNoiseId);
        return true;
    }
    if (methodName == skString("Aggressive")) {
        returnValue = skRValue(m_Aggressive);
        return true;
    }
    if (methodName == skString("GetWimpy")) {
        returnValue = skRValue(m_Wimpy);
        return true;
    }
    if (methodName == skString("GetChaseRadius")) {
        returnValue = skRValue(m_ChaseRadius);
        return true;
    }
    if (methodName == skString("GetCurrentAIPackage")) {
        returnValue = skRValue(m_AiPackage);
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
    // ---- M59: the three merchant natives ----
    //
    // These are the ones that are *not* in the native trie: FUN_1008607c
    // tests them by name with a wcscmp chain and tail-calls the creature's
    // real dispatcher for everything else, which is why the 703-binding
    // enumeration never saw them (store.h).
    if (methodName == skString("AddProduct") && args.entries() >= 2) {
        // Five arguments in every shipped call, and the handler reads two
        // of them: the template id, and the quantity -- from args[3] when
        // there are four or more, args[1] otherwise. The name, price and
        // category arguments are ignored outright; products.dat is where
        // those come from, and it disagrees with the scripts.
        const int templateId = args[0].intValue();
        const int quantity = args.entries() >= 4 ? args[3].intValue() : args[1].intValue();
        m_Store.Add(templateId, quantity);
        return true;
    }
    if (methodName == skString("ClearProducts") && args.entries() == 0) {
        m_Store.Clear();
        return true;
    }
    if (methodName == skString("ReducePrices") && args.entries() == 1) {
        m_Store.ReducePrices(args[0].intValue());
        return true;
    }

    // M58: the same Character-stats effect bindings the player answers --
    // `GetOwner()` resolves to this class for a creature's own items, and
    // 63 of the corpus's 83 AddEffect sites are on GetOwner().
    if (HandleEffectNative(*this, methodName, args, returnValue)) return true;

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
    // M77 -- CORRECTED. AiPursue was grouped with the no-ops. It is not
    // one: dispatcher case 0x2d sets the package to **5** and stores a
    // target, exactly like AiAttack's case 0x2a does with 3. What makes it
    // look inert is the other end -- no arm of the tick reads package 5,
    // so a creature put into it does stop acting. That is a different
    // claim, and GetCurrentAIPackage() can tell them apart.
    if (methodName == skString("AiPursue") && args.entries() >= 1) {
        m_AiPackage = kAiFollow;
        return true;
    }
    if ((methodName == skString("AiActivate") || methodName == skString("AiWounded")) &&
        args.entries() >= 0) {
        // Genuine no-ops: both of these dispatcher cases fall straight
        // through to the shared `break` and store nothing.
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
    // M49: `monster+0x2d8`. FUN_100835b8 gates its whole ranged branch on
    // this not being -1, and its *value* never reaches the art -- the spawn
    // hardcodes entities.txt typeId 599 and resolves the model from that.
    // All twelve shipped callers pass 175, which is models.txt's own index
    // for arrow.bin, so the number is the author writing a model index into
    // a slot the engine treats as a draw parameter. Stored as written.
    if (methodName == skString("SetProjectile") && args.entries() == 1) {
        m_ProjectileArt = args[0].intValue();
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
    if (TryHandleDelay(m_Delay, m_Stack.gameClock(), methodName, args, returnValue)) {
        // M53: the entity script timer -- azra_rat.s's `Delay(2, 0)` on
        // the eighth rat kill, and umbra_keth.s's whole phase cycle.
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
        // M75: and it makes the entity usable. The shipped handler
        // (0x10068418, shared by 31 entity vtables) writes the id, the
        // "has use text" flag and `entity+0xd8` in three consecutive
        // stores -- see use_prompt.h. 54 placed NPCs, Gravel Trothgar and
        // Dark Star West's whole market among them, set a use text and
        // never call SetUsable(true); without this they have no prompt
        // and Use does nothing on them.
        m_Usable = kSetUseTextImpliesUsable;
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
        //
        // M75: with `this` as the new menu's opener. The entity class's
        // OpenMenu is `FUN_100779b8(menuManager, name, 1, self)` -- the
        // caller is always the opener (the same shape ItemExecutable's
        // handler has passed since M21, and the same one
        // PlayerExecutable's comment quotes). Passing nothing was not a
        // cosmetic gap: `fearfrst/Ivgrizt_convo2.s` opens with
        // `if (GetOpener().saved_WeTalked = 0)`, where `saved_WeTalked[0]`
        // is declared at the top of `fearfrst/Ivgrizt.s` itself, so with
        // no opener the very first line of the conversation raised
        // "Cannot get field saved_WeTalked from a non-object" and the
        // menu never opened. `ghstpass/Trailslag_convo.s`,
        // `StoutTP/OldTrinketConvo3.s` and `erthcave/EC_Menu3.s` fail the
        // same way. 210 real call sites read GetOpener().
        m_Stack.ReopenMenu(ToStdString(args[0].str()), static_cast<skiExecutable*>(this));
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
        // M81: both fields, in the order the real handler writes them --
        // the typeId to `monster+0x2f0` (what the death handler tests, and
        // what the spawn creates) and the tag to `monster+0x2f4` (the
        // script that typeId's spawn loads instead of its own). See
        // lootTypeId()'s comment.
        m_LootTypeId = args[0].intValue();
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
    //
    // M62: this handler moved to the shared EntityBaseRef -- the engine
    // has one implementation, on the Object/Entity base class every placed
    // thing derives from, and so does this port now. M92 moved
    // `PlaySound`, `SetName` and `SetID` after it, and brought
    // `SetPassable` (12 soft-fail lines on a monster: `dstar_w.s` and
    // `fearfrst.s` hide an NPC and make it walk-through in the same
    // breath), `ShowEntity`, `GetID`, `SetRotationTurn`, `SetModel`,
    // `RunScript` and `MirrorMethod` with it. Two real corrections
    // came with the move: `z` is optional (the real case branches on
    // `argc == 3` and passes 0 otherwise, so this used to reject the
    // two-argument form outright), and the three matching getters
    // `GetPositionX/Y/Z` exist and were missing here.
    if (HandleEntityBaseNative(methodName, args, returnValue)) return true;
    // M100: Actor dispatcher `FUN_10003810` case 0x20. The seconds are read
    // only when a second argument is present, and default to 0 -- no decay.
    // Umbra Keth is the one caller: SetDead(true) to fake its death in
    // OnHit, SetDead(false) to come back in DelayReached.
    if (methodName == skString("SetDead") && args.entries() >= 1) {
        SetDead(args[0].boolValue(), args.entries() >= 2 ? args[1].intValue() : 0);
        return true;
    }
    // M100: the Character-stats `GetHealth`, stats+0x2a. A creature owns the
    // same stats block the player does and answers it the same way; the port
    // had it on the player only, so a creature's own `GetHealth()` soft-failed
    // to 0. `monsters\umbra_keth.s`'s OnHit tests `GetHealth() < 375`, and
    // `fearfrst\sergeant_convo2.s` reads the sergeant's.
    if (methodName == skString("GetHealth") && args.entries() == 0) {
        returnValue = skRValue(m_CurrentHealth);
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
