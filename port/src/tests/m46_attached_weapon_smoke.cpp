// M46 smoke test: what SetAttachedWeapon actually writes.
//
// The roadmap carried this as a curiosity with two claims attached, and
// both were wrong. M39 read the field as `monster+0x304`; M43 showed that
// offset belongs to SetMeleeRoll. And the entry said the corpus had zero
// call sites; it has **92, across 86 shipped creature scripts**.
//
// The real field is `monster+0x2c2`, a signed 16-bit models.idx archive
// index that the constructor (FUN_100815e0) initialises to -1. It is
// missed by pyghidra_find_reads.py for the same reason M39's read was
// wrong: every consumer loads it with LDRSH, and that script only matches
// LDR with an immediate offset. Grepping the decompiled corpus instead
// turns up exactly five sites -- the constructor, the dispatcher's own
// write, and three consumers:
//
//   * FUN_10083490 -- the monster's render override. Draws the weapon as
//     a whole second model sharing the actor's transform, scale and
//     absolute animation frame, then falls through to the ordinary actor
//     draw for the body.
//   * FUN_10082224 -- the AI tick's ranged test, which hardcodes 225.
//   * FUN_1003c1c0 -- a multiplayer receive handler (one slot in the
//     48-entry table at 0x100fdf34), so the field is replicated over the
//     wire.
//
// What makes the render side provable rather than plausible is the model
// archive itself: the five weapon models are frame-aligned exports of the
// humanoid rig, and this test checks that directly against models.huge.
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/model_archive.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-76s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

// Reads one `<zone>_models.txt` row. M55 corrected the columns: the row is
// "<index> <solid> <halfExtentX> <halfExtentY> <name>", the model's own
// collision box, not the "<skins> <w> <h>" guessed here -- see
// world/model_collision.h. Only the name is used below.
std::string ModelNameAt(const std::string& path, int index) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        int idx = 0, skins = 0, w = 0, h = 0;
        std::string name;
        if (!(ss >> idx >> skins >> w >> h >> name)) continue;
        if (idx == index) return name;
    }
    return std::string();
}

}  // namespace

int main(int argc, char** argv) {
    using Monster = sk_bindings::MonsterExecutable;

    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.eng");
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m46_attached_weapon_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);

    auto loadMonster = [&](const std::string& rel) -> std::unique_ptr<Monster> {
        skExecutableContext loadCtxt(&interpreter);
        try {
            auto m = std::make_unique<Monster>(
                skString((std::string(scriptRoot) + "/" + rel).c_str()), loadCtxt, &strings,
                stack.player(), stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext callCtxt(&interpreter);
            m->method(skString("Init"), args, ret, callCtxt);
            return m;
        } catch (skParseException& e) {
            std::printf("  (parse error loading %s: %s)\n", rel.c_str(), e.toString().ptr());
            return nullptr;
        } catch (skRuntimeException& e) {
            std::printf("  (runtime error loading %s: %s)\n", rel.c_str(), e.toString().ptr());
            return nullptr;
        }
    };

    std::printf("=== M46: what SetAttachedWeapon actually writes ===\n\n");

    // ---- 1. The binding is real, and so are its five values ----
    //
    // One real script per weapon, each running its own untouched Init().
    // The whole corpus only ever passes these five numbers.
    struct Case {
        const char* script;
        int expected;
        bool expectRanged;
        const char* what;
    };
    const Case cases[] = {
        {"delfhide/archer_guard.s", Monster::kWeaponModelBow, true, "bow"},
        {"dstar_e/dse_skyrim_archer.s", Monster::kWeaponModelBow, true, "bow"},
        {"delfhide/lt_breser.s", Monster::kWeaponModelSword, false, "sword"},
        {"delfhide/dh_guard_talk.s", Monster::kWeaponModelMace, false, "mace"},
        {"delfhide/makor.s", Monster::kWeaponModelDagger, false, "dagger"},
        {"delfhide/bandit_thug.s", Monster::kWeaponModelAxe, false, "ax"},
    };
    for (const Case& c : cases) {
        std::unique_ptr<Monster> m = loadMonster(c.script);
        Check(m != nullptr, std::string(c.script) + " loads and runs its real Init()");
        if (!m) continue;
        Check(m->attachedWeaponModel() == c.expected,
              std::string(c.script) + " attaches " + c.what + " (" +
                  std::to_string(c.expected) + ")");
        // FUN_10082224's `bVar1`: the bow's archive index is hardcoded in
        // the AI, so attaching 225 -- and only 225 -- makes a creature
        // count as ranged. None of these scripts calls AddSpell (no
        // shipped script does both), so the bow is the only term in play.
        Check(m->ranged() == c.expectRanged,
              std::string(c.script) + (c.expectRanged ? " counts as ranged (== 225)"
                                                       : " does not count as ranged"));
    }

    // ---- 2. The default really is -1, not 0 ----
    //
    // 0 would be a valid archive index, so this distinction matters: it is
    // what stops every creature that never calls the binding from drawing
    // slot 0's model welded to its hand.
    std::unique_ptr<Monster> rat = loadMonster("monsters/azra_rat.s");
    Check(rat != nullptr, "monsters/azra_rat.s loads (a creature that never sets a weapon)");
    if (rat) {
        Check(rat->attachedWeaponModel() == Monster::kNoAttachedWeapon,
              "a script that never calls SetAttachedWeapon keeps the constructor's -1");
        Check(!rat->ranged(), "...and is not ranged");
        Check(Monster::kNoAttachedWeapon == -1, "the sentinel is -1 (FUN_100815e0's 0xffff)");
    }

    // ---- 3. The five indices really are the five weapon models ----
    //
    // `<zone>_models.txt` names every archive slot. The mapping is
    // identical in all 21 real zones; delfhide's is the one checked here.
    const std::string modelsTxt = std::string(scriptRoot) + "/delfhide_models.txt";
    struct NameCase {
        int index;
        const char* name;
    };
    const NameCase names[] = {
        {Monster::kWeaponModelSword, "sword.bin"}, {Monster::kWeaponModelMace, "mace.bin"},
        {Monster::kWeaponModelDagger, "dagger.bin"}, {Monster::kWeaponModelBow, "bow.bin"},
        {Monster::kWeaponModelAxe, "ax.bin"},
    };
    for (const NameCase& n : names) {
        Check(ModelNameAt(modelsTxt, n.index) == n.name,
              "delfhide_models.txt names archive slot " + std::to_string(n.index) + " " + n.name);
    }

    // ---- 4. Why sharing the body's animation frame is correct ----
    //
    // The render override copies the actor's whole animation block into
    // the weapon's transform, frame index included. That is only coherent
    // if the weapon is authored against the same rig -- and it is. All
    // five weapons carry 144 frames and a clip table byte-identical to the
    // humanoid bodies', and those eleven models are the only 144-frame
    // entries in the whole archive.
    sk::ModelArchive models;
    Check(models.Load(scriptRoot), "models.idx/.huge load");
    const sk::Model* body = models.GetModel(22);  // male_long_tunic.bin
    Check(body != nullptr, "archive slot 22 (male_long_tunic.bin) parses");
    if (body) {
        Check(body->frameCount == 144, "the humanoid body rig has 144 frames");
        Check(body->clips.size() == 11, "...and 11 animation clips");
    }
    for (const NameCase& n : names) {
        const sk::Model* w = models.GetModel(n.index);
        Check(w != nullptr, std::string("archive slot ") + std::to_string(n.index) + " (" +
                                n.name + ") parses");
        if (!w || !body) continue;
        Check(w->frameCount == body->frameCount,
              std::string(n.name) + " has the body rig's frame count");
        // A single skin, which is why the render override never needs to
        // carry the body's skin across.
        Check(w->skinCount == 1, std::string(n.name) + " has exactly one skin");
        bool clipsMatch = w->clips.size() == body->clips.size();
        for (size_t i = 0; clipsMatch && i < w->clips.size(); ++i) {
            clipsMatch = w->clips[i].startFrame == body->clips[i].startFrame &&
                         w->clips[i].endFrame == body->clips[i].endFrame &&
                         w->clips[i].rate == body->clips[i].rate;
        }
        Check(clipsMatch, std::string(n.name) + "'s clip table is identical to the body's");
    }
    // The negative half: a creature that is *not* on the humanoid rig has a
    // completely different clip table, so this is a property of these
    // models specifically and not of the format.
    const sk::Model* ratModel = models.GetModel(18);  // rat.bin
    Check(ratModel != nullptr, "archive slot 18 (rat.bin) parses");
    if (ratModel && body) {
        Check(ratModel->frameCount != body->frameCount,
              "a non-humanoid creature model does NOT share the rig (rat: 57 frames, 4 clips)");
    }

    std::printf("\nm46_attached_weapon_smoke: %s (%d failure(s))\n",
                g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
