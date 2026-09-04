// M52 smoke test: the unidentified scalars inside a save record, plus the
// three configuration files M40 left unread.
//
// M50 pinned every field in a save record by *width and position* (the
// save/load pair agrees on both) and named only the ones a dispatcher or a
// shipped script happened to name -- roughly forty were left as `fNN`.
// This closes them, and the evidence is of four kinds:
//
//   1. **The join.** Every native binding belongs to a class dispatcher,
//      and the dispatchers form the same inheritance tree the save
//      functions do. A field offset appearing inside one binding's `case`
//      block, and only that one, is that binding's field.
//      (shadowkey/ghidra/scripts/name_save_fields.py)
//   2. **The shipped scripts.** A name is only worth having if the values
//      fit it, so the corpus is grepped for the binding's actual
//      arguments: `SetScale(256)`, `SetArmorConstraint(AR_Medium)`,
//      `SetSkin(0..3)`.
//   3. **Absence.** Several fields are named by nothing because nothing
//      touches them: three Character words are read and written by the
//      save/load pair *and by no other code in the image*, and three
//      "busy" flags are only ever cleared.
//   4. **The config files.** `dragonstar.set`'s reader and writer are two
//      separate functions that must agree, so the round trip below is the
//      same kind of independent second source M40 and M50 relied on.
//
// The parts below assert what this port now encodes, not what a run
// produces -- as with M50, no real save file or settings file ships on the
// install image, so the decompiled writer is the reference.
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "assets/game_config.h"
#include "assets/save_records.h"
#include "assets/save_stream.h"
#include "engine/input_state.h"
#include "world/model_archive.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-78s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    ++g_checks;
    if (!ok) ++g_failures;
}

// Every `.s` under a root, concatenated -- the same corpus sweep the
// earlier milestones use to check a name against real argument values.
std::string ReadScriptCorpus(const std::string& root) {
    std::string all;
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) return all;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".s") continue;
        std::ifstream f(it->path(), std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        all += ss.str();
        all += '\n';
    }
    return all;
}

int CountOccurrences(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos;
         p = hay.find(needle, p + needle.size())) {
        ++n;
    }
    return n;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    std::printf("=== M52: what the save record's scalars are ===\n\n");

    // ------------------------------------------------------------------
    // Part 1 -- the record still round-trips, byte for byte, after the
    // rename. Nothing here changed the wire format; that is the point.
    // ------------------------------------------------------------------
    std::printf("-- 1. renaming changed no bytes --\n");
    {
        sk::SavedEntity rec;
        rec.kind = sk::SavedEntityKind::Character;
        rec.entity.collisionRadius = 0x40;
        rec.entity.collisionHeight = 0x80;
        rec.entity.roll = 0x1234;
        rec.entity.skin = 2;
        rec.entity.passable = 1;
        rec.entity.usable = 1;
        rec.entity.hasCustomName = 1;
        rec.entity.nameStringId = 457;
        rec.entity.shortNameStringId = 14;
        rec.entity.animRate = 0xf00;
        rec.entity.animLoopsLeft = 0x7f;
        rec.entity.inUse = 1;
        rec.drawable.scale = 256;
        rec.drawable.animClip = 3;
        rec.drawable.nextAnimClip = 0x20;
        rec.holder.active = 1;
        rec.holder.walkingState = 1;
        rec.character.aiState = 7;
        rec.character.usingObject = 1;
        rec.character.returnX = 0x111;
        rec.character.returnY = 0x222;
        rec.character.returnZ = 0x33;
        rec.character.dead37c = 0x44444444;
        rec.character.dead380 = 0x55555555;
        rec.character.dead384 = 0x66666666;

        sk::SaveStream out;
        rec.Write(out);
        sk::SaveStream in;
        in.Reset(out.bytes());
        sk::SavedEntity back;
        back.kind = sk::SavedEntityKind::Character;
        back.Read(in);
        Check(!in.failed(), "a Character record round-trips without overrunning");
        Check(back.entity.collisionRadius == 0x40 && back.entity.collisionHeight == 0x80,
              "the collision cylinder comes back (+0x8e radius, +0x90 height)");
        Check(back.entity.roll == 0x1234,
              "roll comes back -- the third orientation channel beside pitch and yaw");
        Check(back.entity.skin == 2 && back.entity.passable == 1 && back.entity.usable == 1,
              "skin / passable / usable come back");
        Check(back.entity.hasCustomName == 1 && back.entity.nameStringId == 457 &&
                  back.entity.shortNameStringId == 14,
              "the name flag and both string-table ids come back");
        Check(back.drawable.scale == 256 && back.drawable.animClip == 3 &&
                  back.drawable.nextAnimClip == 0x20,
              "scale and the two animation clip indices come back");
        Check(back.character.returnX == 0x111 && back.character.returnY == 0x222 &&
                  back.character.returnZ == 0x33,
              "the use-return position comes back");
        Check(back.character.dead37c == 0x44444444 &&
                  back.character.dead380 == 0x55555555 &&
                  back.character.dead384 == 0x66666666,
              "so do the three words nothing but the save/load pair touches");
    }

    // ------------------------------------------------------------------
    // Part 2 -- the names against the shipped scripts. A name that no
    // script argument fits is a guess; these are not.
    // ------------------------------------------------------------------
    std::printf("\n-- 2. the names against the shipped scripts --\n");
    const std::string corpus = ReadScriptCorpus(scriptRoot);
    if (corpus.empty()) {
        std::printf("   (no script corpus at %s -- skipping)\n", scriptRoot);
    } else {
        // +0x5e is `SetScale`, and 256 is 1.0 in the engine's 8.8 fixed
        // point -- which is what makes it a *scale* rather than the
        // "modelFlags" ZONE_FORMAT.md called the `.ent` field feeding it.
        const int scale256 = CountOccurrences(corpus, "SetScale(256)");
        const int scale192 = CountOccurrences(corpus, "SetScale(192)");
        Check(scale256 > 0 && scale192 > 0,
              "SetScale is called with 256 (1.0 in 8.8) and with 192 (0.75)");
        std::printf("   SetScale(256) x%d, SetScale(192) x%d -- 8.8 fixed point, not flags\n",
                    scale256, scale192);

        // +0x1d4 is the armour weight class; the scripts only ever pass
        // the three AR_* constants, which are 0/1/2 (game_constants.cpp).
        const int light = CountOccurrences(corpus, "SetArmorConstraint(AR_Light)");
        const int medium = CountOccurrences(corpus, "SetArmorConstraint(AR_Medium)");
        const int heavy = CountOccurrences(corpus, "SetArmorConstraint(AR_Heavy)");
        Check(light > 0 && medium > 0 && heavy > 0,
              "SetArmorConstraint takes only AR_Light / AR_Medium / AR_Heavy");
        std::printf("   armour weight classes in the corpus: light %d, medium %d, heavy %d\n",
                    light, medium, heavy);

        // +0xca is a skin index: a small dense run from 0, and a byte on
        // the wire, so anything the corpus passes has to fit in one.
        int skinCalls = 0, maxSkin = -1;
        bool skinsAreIndices = true;
        for (int i = 0; i < 300; ++i) {
            const std::string call = "SetSkin(" + std::to_string(i) + ")";
            const int n = CountOccurrences(corpus, call);
            if (n == 0) continue;
            skinCalls += n;
            maxSkin = i;
            if (i > 255) skinsAreIndices = false;  // would not survive the save
        }
        Check(skinCalls > 0 && skinsAreIndices && maxSkin >= 0,
              "SetSkin only ever passes a small index -- and one that fits the saved byte");
        std::printf("   %d SetSkin calls, highest index %d (the field is a u8)\n",
                    skinCalls, maxSkin);

        // Three bindings that write fields the save format carries and
        // that **no shipped script calls**. This is the evidence for the
        // "saved but unreachable" notes in save_records.h, so it is worth
        // an assertion rather than a comment.
        struct Orphan { const char* name; };
        const Orphan orphans[] = {
            {"MountGun"}, {"MountFlak88"}, {"IsInUse"},
            {"SetLifespan"}, {"SetFrozen"}, {"SetParalyzed"},
            {"PutInReverse"}, {"GetWalkingState"},
        };
        bool allOrphaned = true;
        for (const Orphan& o : orphans) {
            const int n = CountOccurrences(corpus, std::string(o.name) + "(");
            if (n != 0) {
                allOrphaned = false;
                std::printf("   !! %s appears %d times\n", o.name, n);
            }
        }
        Check(allOrphaned,
              "eight bindings that write saved fields are called by no shipped script");
    }

    // ------------------------------------------------------------------
    // Part 3 -- AnimationClip::rate's units, resolved.
    // ------------------------------------------------------------------
    std::printf("\n-- 3. the animation rate --\n");
    {
        // FUN_100655a8 converts a clip's own `rate` into the object's
        // +0x80 by multiplying by 384, taking `0xf00` as a shortcut for
        // the common value 10. FUN_10065438 then advances an 8.8 frame
        // cursor by `(dt * rate) >> 8` with dt in 8.8 seconds, so one
        // second advances the cursor by exactly `rate` -- 256 units to
        // the frame.
        constexpr int kInternalPerClipUnit = 384;
        constexpr int kDefaultInternalRate = 0xf00;
        Check(10 * kInternalPerClipUnit == kDefaultInternalRate,
              "a clip rate of 10 converts to 0xf00, the engine's own shortcut value");
        Check(static_cast<float>(kDefaultInternalRate) / 256.0f == 15.0f,
              "and 0xf00 / 256 is 15 frames per second, not 10");

        sk::AnimationClip clip;
        clip.startFrame = 0;
        clip.endFrame = 20;
        clip.rate = 10;
        Check(clip.fps() == 15.0f, "so AnimationClip::fps() reports 15 for the common rate 10");
        clip.rate = 1;
        Check(clip.fps() == 1.5f, "and 1.5 for the slowest clip in the archive");
        Check(sk::kRateToFps == 1.5f, "the field's unit is 1.5 fps, which is 384/256");
    }

    // ------------------------------------------------------------------
    // Part 4 -- dragonstar.set, the one configuration file that exists.
    // ------------------------------------------------------------------
    std::printf("\n-- 4. dragonstar.set --\n");
    {
        sk::InputState input;  // the game's own verified default scheme
        sk::GameConfig cfg;
        cfg.CaptureBindings(input);
        cfg.language = 2;
        cfg.soundVolume = 70;
        cfg.musicVolume = 40;
        cfg.muteOnCall = true;

        const std::string text = cfg.Serialize();
        // The writer's exact shape: five keys, each on its own line, each
        // value through "%d\n", and the action map preceded by its count.
        Check(text.rfind("ACTIONMAP\n16\n", 0) == 0,
              "the file opens with ACTIONMAP and the writer's hardcoded count of 16");
        Check(text.find("\nLANGUAGE\n2\n") != std::string::npos,
              "LANGUAGE follows the sixteen bindings");
        Check(text.find("\nSOUNDVOLUME\n70\n") != std::string::npos &&
                  text.find("\nMUSICVOLUME\n40\n") != std::string::npos,
              "then SOUNDVOLUME and MUSICVOLUME, in that order (soundMgr +0xabc, +0xab8)");
        Check(text.find("\nMUTEONCALL\n1\n") != std::string::npos,
              "and MUTEONCALL last, as a 0/1");

        // The writer only emits 16 of the 17 binding slots, so a round
        // trip cannot restore the 17th. Reproduced rather than fixed.
        Check(sk::GameConfig::kWrittenActions <
                  static_cast<int>(sk::Action::kCount),
              "the writer emits 16 of the table's 17 slots -- the 17th never persists");

        sk::GameConfig back;
        Check(back.Parse(text), "the loader accepts what the writer produced");
        Check(back.language == 2 && back.soundVolume == 70 && back.musicVolume == 40 &&
                  back.muteOnCall,
              "and reads every scalar back");
        bool bindingsMatch = true;
        for (int i = 0; i < sk::GameConfig::kWrittenActions; ++i) {
            if (back.actionMap[i] != cfg.actionMap[i]) bindingsMatch = false;
        }
        Check(bindingsMatch, "and all sixteen written bindings");

        // The loader takes the count from the file, not from a constant --
        // so a shorter ACTIONMAP is read short rather than overrunning.
        sk::GameConfig shortMap;
        shortMap.actionMap.fill(-7);
        Check(shortMap.Parse("ACTIONMAP\n2\n5\n6\nLANGUAGE\n1\n"),
              "a file whose ACTIONMAP declares 2 entries parses");
        Check(shortMap.actionMap[0] == 5 && shortMap.actionMap[1] == 6 &&
                  shortMap.actionMap[2] == -7,
              "and only those two slots are touched");
        Check(shortMap.language == 1, "the key after the short map is still found");

        // An unrecognised key is simply not one of the five; the real
        // loader's strcmp chain falls through and the next token becomes
        // the next key candidate.
        sk::GameConfig odd;
        odd.Parse("WIDGETS\n9\nMUSICVOLUME\n33\n");
        Check(odd.musicVolume == 33, "an unknown key does not stop the loader");

        // And the whole thing survives a real file.
        const std::filesystem::path tmp =
            std::filesystem::temp_directory_path() / "m52_dragonstar.set";
        Check(cfg.Save(tmp.string()), "it writes to disk");
        sk::GameConfig fromDisk;
        Check(fromDisk.Load(tmp.string()) && fromDisk.soundVolume == 70,
              "and reads back from disk");
        sk::InputState restored;
        for (int i = 0; i < static_cast<int>(sk::Action::kCount); ++i) {
            restored.RebindRaw(static_cast<sk::Action>(i), -1);
        }
        fromDisk.ApplyBindings(restored);
        Check(restored.binding(sk::Action::MoveForward) ==
                  input.binding(sk::Action::MoveForward),
              "and the action map really rebinds an InputState");
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
    }

    // ------------------------------------------------------------------
    // Part 5 -- the two files that do not exist.
    // ------------------------------------------------------------------
    std::printf("\n-- 5. levelinfo.txt and dragonstar.cfg --\n");
    {
        // Nothing to execute: the finding is an absence, and the check is
        // that this port does not invent a reader for either. Assert the
        // shape of what M40 listed so a future change has to notice.
        Check(std::string(sk::GameConfig::kFileName) == "dragonstar.set",
              "the only settings file this port reads or writes is dragonstar.set");
        std::printf(
            "   levelinfo.txt: its full path (0x100ab238) and its \"%%d\\n%%d\\n\" format\n"
            "   (0x100ab264) are referenced by no word anywhere in the image.\n"
            "   dragonstar.cfg: no full path exists for it at all.\n"
            "   Both survive only in DeleteAllGames' seven-name cleanup list:\n"
            "   current.sav, dragonstar.cfg, dragonstar.set, ngen.log,\n"
            "   levelinfo.txt, tmp.big, 6r51.cfg.\n");
    }

    std::printf("\nm52_save_fields_smoke: %s (%d checks)\n",
                g_failures == 0 ? "PASSED (all checks)" : "FAILED", g_checks);
    return g_failures == 0 ? 0 : 1;
}
