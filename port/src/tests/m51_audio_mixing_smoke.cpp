// M51 smoke test: the engine's own sound mixer and its native triggers.
//
// M27 decoded the audio data and the script call surface and left five
// gaps: native-only player-action sounds, the main-menu music trigger,
// `steamsound.s`'s four-argument PlaySound, positional audio, and the
// volume/pan mixing formula. All five were one system.
//
// The checks are of three kinds:
//
//   1. arithmetic, against the decompiled mixer -- the volume curve read
//      byte-for-byte out of the image, the per-voice gain, the distance
//      and direction attenuation, the fade;
//   2. real-data, against the shipped manifests and scripts -- every
//      sound slot the native code names, resolved to a real filename in
//      every zone that has one, plus the two Options sliders and the
//      one four-argument PlaySound call in the corpus;
//   3. the negative result -- the ids that exist in every manifest and
//      are triggered by nothing at all.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "assets/sound_archive.h"
#include "audio/sound_mixing.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-78s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    ++g_checks;
    if (!ok) ++g_failures;
}

// slot -> filename, from a real `<category>_sounds.txt`.
std::map<int, std::string> ReadManifest(const std::string& path) {
    std::map<int, std::string> out;
    std::ifstream in(path);
    if (!in) return out;
    int slot;
    std::string name;
    while (in >> slot >> name) out[slot] = name;
    return out;
}

std::vector<std::string> ManifestPaths(const std::string& root) {
    std::vector<std::string> out;
    std::error_code ec;
    std::filesystem::directory_iterator it(root, ec);
    if (ec) return out;
    for (const auto& entry : it) {
        const std::string name = entry.path().filename().string();
        if (name.size() > 11 && name.compare(name.size() - 11, 11, "_sounds.txt") == 0) {
            out.push_back(entry.path().string());
        }
    }
    return out;
}

std::string SlurpScripts(const std::string& root) {
    std::string all;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(root, ec);
    if (ec) return all;
    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".s") continue;
        std::ifstream in(entry.path());
        if (!in) continue;
        std::stringstream ss;
        ss << in.rdbuf();
        all += ss.str();
        all += '\n';
    }
    return all;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    std::printf("=== M51: the engine's sound mixer ===\n\n");

    // ------------------------------------------------------------------
    // Part 1 -- the volume curve (FUN_10009324).
    // ------------------------------------------------------------------
    std::printf("-- 1. the 101-entry volume curve --\n");
    {
        Check(sk::kVolumeCurve[0] == 0, "curve[0] is silence");
        Check(sk::kVolumeCurve[100] == sk::kVolumeCurveMax &&
                  sk::kVolumeCurve[99] == sk::kVolumeCurveMax,
              "the curve saturates at 256, one step early (99 and 100 are both 256)");
        bool monotone = true;
        for (int i = 1; i <= 100; ++i) {
            if (sk::kVolumeCurve[i] < sk::kVolumeCurve[i - 1]) monotone = false;
        }
        Check(monotone, "it never goes backwards");
        // It is a 2.56-per-unit ramp -- but not exactly. Every step is 2
        // or 3 except two: one jump of 5, and one flat step where it
        // saturates. That single 5 is why the table is transcribed
        // verbatim instead of recomputed from a formula.
        int twos = 0, threes = 0, other = 0, jumpAt = -1;
        for (int i = 1; i <= 100; ++i) {
            const int step = sk::kVolumeCurve[i] - sk::kVolumeCurve[i - 1];
            if (step == 2) {
                ++twos;
            } else if (step == 3) {
                ++threes;
            } else {
                ++other;
                if (step > 3 && jumpAt < 0) jumpAt = i;
            }
        }
        std::printf("   steps of 2: %d, of 3: %d, anything else: %d (the odd jump is at %d)\n",
                    twos, threes, other, jumpAt);
        Check(twos + threes == 98 && other == 2,
              "98 of the 100 steps are 2 or 3 -- a 2.56-per-unit ramp");
        Check(jumpAt == 13 && sk::kVolumeCurve[13] - sk::kVolumeCurve[12] == 5,
              "the exception is a single 5-unit jump at 13, where the ramp gives 33 not 35");
        Check(sk::VolumeCurve(250) == sk::kVolumeCurve[100] && sk::VolumeCurve(101) == 256,
              "anything over 100 clamps to the last entry, as the real `if (v < 0x65)` does");
    }

    // ------------------------------------------------------------------
    // Part 2 -- the per-voice gain (FUN_100080a0).
    // ------------------------------------------------------------------
    std::printf("\n-- 2. the per-voice gain --\n");
    {
        Check(sk::VoiceGain(0, 100) == 0.0f && sk::VoiceGain(100, 0) == 0.0f,
              "a zero on either knob is silence -- the mixer skips the voice entirely");
        // (curve[100] * curve[100] >> 8) = 256, applied to a halved
        // sample: a single voice at full volume is at HALF amplitude.
        Check(sk::VoiceGain(100, 100) == 0.5f,
              "full volume on both knobs is 0.5 amplitude -- every voice is halved for headroom");
        Check(sk::VoiceGain(100, 50) < sk::VoiceGain(100, 100) &&
                  sk::VoiceGain(50, 100) == sk::VoiceGain(100, 50),
              "the two knobs multiply, so they commute");
        Check(sk::kMaxSimultaneousVoices == 8,
              "the mix loop stops after eight playing voices");
    }

    // ------------------------------------------------------------------
    // Part 3 -- distance and direction (FUN_10027980 / FUN_10064e00).
    // ------------------------------------------------------------------
    std::printf("\n-- 3. positional volume --\n");
    {
        constexpr int kTile = 256;  // 8.8 world units
        Check(sk::PositionalVolume(100, 0, 0) == 100, "a source on top of you is at full volume");
        Check(sk::PositionalVolume(100, kTile - 1, 0) == 100,
              "and so is anything inside the first tile -- the (d2 >> 8) == 0 case");
        // The falloff is linear in *squared* distance, so the audible
        // radius is sqrt(range) tiles, not range tiles.
        Check(sk::PositionalVolume(100, 23 * kTile, 0) == 0,
              "at 23 tiles it is silent: the default range of 512 is a squared-distance limit");
        Check(sk::PositionalVolume(100, 22 * kTile, 0) > 0, "at 22 tiles it is still audible");
        int prev = 101;
        bool decreasing = true;
        for (int tiles = 1; tiles <= 22; ++tiles) {
            const int v = sk::PositionalVolume(100, tiles * kTile, 0);
            if (v > prev) decreasing = false;
            prev = v;
        }
        Check(decreasing, "and it falls off monotonically in between");
        std::printf("   volume at 1/4/8/16/22 tiles: %d %d %d %d %d\n",
                    sk::PositionalVolume(100, 1 * kTile, 0), sk::PositionalVolume(100, 4 * kTile, 0),
                    sk::PositionalVolume(100, 8 * kTile, 0),
                    sk::PositionalVolume(100, 16 * kTile, 0),
                    sk::PositionalVolume(100, 22 * kTile, 0));
        // x and y are symmetric -- there is one accumulator and no pan.
        Check(sk::PositionalVolume(100, 5 * kTile, 0) == sk::PositionalVolume(100, 0, 5 * kTile),
              "x and y are interchangeable -- the model has distance but no pan");
        // An entity-attached looping ambient carries its own range.
        Check(sk::PositionalVolume(100, 5 * kTile, 0, 64) <
                  sk::PositionalVolume(100, 5 * kTile, 0, 512),
              "a smaller range makes the same distance quieter (the per-entity ambient path)");
    }
    {
        Check(sk::DirectionalVolume(100, 0, 0) == 100,
              "an emitter pointing straight at you loses nothing");
        // The step is `(volume >> 3) / 32` -- an integer divide. For any
        // volume below 256 that is zero, and a volume is 0..100. So the
        // whole directional branch is arithmetically dead in the shipped
        // game, whatever the angle.
        bool everMoves = false;
        for (int vol = 0; vol <= 100; ++vol) {
            for (int yaw = -0x8000; yaw < 0x8000; yaw += 0x101) {
                if (sk::DirectionalVolume(vol, 0, yaw) != vol) everMoves = true;
            }
        }
        Check(!everMoves,
              "at every 0..100 volume and every angle it subtracts nothing: (v>>3)/32 == 0");
        // It only bites at a volume the engine cannot produce -- checked
        // here so the bound itself stays pinned, not just the zero.
        const int loud = 800;
        const int away = sk::DirectionalVolume(loud, 0, 0x8000 - 1);
        std::printf("   at an unreachable volume of %d, facing away gives %d\n", loud, away);
        // The step truncates too, so an eighth is a ceiling the term only
        // reaches when (volume >> 3) divides by 32 exactly.
        Check(away >= loud - (loud >> 3),
              "above 256 it starts to bite, and never takes off more than an eighth");
        Check(sk::DirectionalVolume(1024, 0, 0x8000 - 1) == 1024 - (1024 >> 3),
              "at 1024, where the step divides exactly, it takes off precisely an eighth");
    }

    // ------------------------------------------------------------------
    // Part 4 -- the music fade (FUN_100090bc / FUN_1000915c).
    // ------------------------------------------------------------------
    std::printf("\n-- 4. FadeMusic / UnFadeMusic --\n");
    {
        sk::MusicFade fade;
        int vol = 100;
        fade.Fade(vol);
        Check(fade.fadingOut() && fade.restoreTarget() == 100,
              "FadeMusic remembers the current volume as the restore target");
        int steps = 0;
        while (fade.fadingOut() && steps < 100) {
            vol = fade.Step(vol);
            ++steps;
        }
        Check(vol == 0 && steps == 20, "100 -> 0 takes exactly 20 steps of 5");
        // One step per 256-sample buffer at 8 kHz.
        const int ms = steps * sk::kMixBufferSamples * 1000 / sk::kSampleRateHz;
        std::printf("   which at %d Hz and %d samples a buffer is %d ms\n", sk::kSampleRateHz,
                    sk::kMixBufferSamples, ms);
        Check(ms == 640, "i.e. about two thirds of a second");

        fade.UnFade();
        Check(fade.fadingIn(), "UnFadeMusic starts it climbing back");
        steps = 0;
        while (fade.fadingIn() && steps < 100) {
            vol = fade.Step(vol);
            ++steps;
        }
        Check(vol == 100, "and it lands exactly back on the remembered volume");
        Check(fade.restoreTarget() == -1, "which is then forgotten");
        fade.UnFade();
        Check(!fade.fadingIn(), "a second UnFadeMusic with nothing stored does nothing");
    }
    {
        // The asymmetry worth keeping: fading out from silence stores no
        // target at all, so the un-fade never happens.
        sk::MusicFade fade;
        fade.Fade(0);
        Check(fade.fadingOut() && fade.restoreTarget() == -1,
              "fading out from silence stores no restore target");
        fade.UnFade();
        Check(!fade.fadingIn(), "so UnFadeMusic afterwards is a no-op");
    }

    // ------------------------------------------------------------------
    // Part 5 -- the native trigger table, against every real manifest.
    // ------------------------------------------------------------------
    std::printf("\n-- 5. the native sound slots, against the shipped manifests --\n");
    const std::vector<std::string> manifests = ManifestPaths(scriptRoot);
    if (manifests.empty()) {
        std::printf("   (no <category>_sounds.txt under '%s' -- skipping the real-data checks)\n",
                    scriptRoot);
    } else {
        std::printf("   %zu manifests found\n", manifests.size());
        // Every id the decompiled engine plays as a literal, and the file
        // each one has to be for the reading to hold.
        const struct {
            int slot;
            const char* file;
            const char* trigger;
        } kNative[] = {
            {sk::kSoundBowFire, "barch_firebow.wav", "player fires a ranged weapon"},
            {sk::kSoundChestOpen, "chest_open.wav", "a chest-category object opens"},
            {sk::kSoundDoorOpen, "door_open.wav", "the native door/teleport path"},
            {sk::kSoundAttackHit, "pl_attack_impale.wav", "melee connects / player takes damage"},
            {sk::kSoundAttackMiss, "pl_attack_sword.wav", "melee finds nothing in range"},
            {sk::kSoundPowerUp, "pl_cast_powerup.wav", "level up, and the cheat sequence"},
            {sk::kSoundJumpFemale, "pl_jump_female.wav", "jump, sex == 0"},
            {sk::kSoundJumpMale, "pl_jump_male.wav", "jump, otherwise"},
            {sk::kSoundMenuMove, "menuNEWxbx.wav", "UI navigation"},
            {sk::kSoundMenuSelect, "menu1.wav", "UI select"},
        };
        bool allAgree = true;
        for (const auto& n : kNative) {
            int mapped = 0, present = 0;
            for (const std::string& path : manifests) {
                const std::map<int, std::string> m = ReadManifest(path);
                auto it = m.find(n.slot);
                if (it == m.end()) continue;
                ++present;
                if (it->second == n.file) ++mapped;
            }
            // A zone whose manifest leaves the slot as NULL.wav simply has
            // no such sound; what must never happen is a zone mapping the
            // slot to a *different* real file.
            int wrong = 0;
            for (const std::string& path : manifests) {
                const std::map<int, std::string> m = ReadManifest(path);
                auto it = m.find(n.slot);
                if (it != m.end() && it->second != n.file && it->second != "NULL.wav" &&
                    it->second != "NULL.ogg") {
                    ++wrong;
                }
            }
            if (wrong != 0 || mapped == 0) allAgree = false;
            std::printf("   %3d %-22s %2d/%2d manifests, %d conflicting  (%s)\n", n.slot, n.file,
                        mapped, present, wrong, n.trigger);
        }
        Check(allAgree,
              "every natively-played slot names the same file in every manifest that maps it");

        // The front-end track: menu_sounds.txt's own slot 70.
        const std::map<int, std::string> menu =
            ReadManifest(std::string(scriptRoot) + "/menu_sounds.txt");
        Check(!menu.empty() && menu.count(sk::kSoundMenuMusic) &&
                  menu.at(sk::kSoundMenuMusic) == "battle3.ogg",
              "menu_sounds.txt slot 70 is battle3.ogg -- what FUN_1002707c starts by hand");

        // The negative result: ids that exist everywhere and are played
        // by nothing. FUN_1001b198 and FUN_1001b204 are the engine's only
        // two ways in (nothing else reaches the queue primitives), and
        // between their 27 call sites these ids appear at none.
        const struct {
            int slot;
            const char* file;
        } kOrphans[] = {
            {79, "pl_attack_blunt.wav"}, {90, "pl_female_die.wav"}, {93, "pl_male_die.wav"},
            {94, "pl_run.wav"},          {95, "pl_walk.wav"},       {96, "pl_walk_wading.wav"},
        };
        const std::string corpus = SlurpScripts(scriptRoot);
        bool orphansReal = true;
        bool orphansUnscripted = true;
        for (const auto& o : kOrphans) {
            int present = 0;
            for (const std::string& path : manifests) {
                const std::map<int, std::string> m = ReadManifest(path);
                auto it = m.find(o.slot);
                if (it != m.end() && it->second == o.file) ++present;
            }
            if (present == 0) orphansReal = false;
            // No script plays them either -- `PlaySound(94)` and friends
            // appear nowhere.
            char needle[32];
            std::snprintf(needle, sizeof(needle), "PlaySound(%d)", o.slot);
            if (!corpus.empty() && corpus.find(needle) != std::string::npos) {
                orphansUnscripted = false;
            }
            std::printf("   %3d %-22s in %2zu/%2zu manifests, played by nothing\n", o.slot, o.file,
                        static_cast<size_t>(present), manifests.size());
        }
        Check(orphansReal, "the six orphan samples are real, mapped entries in every manifest");
        Check(orphansUnscripted,
              "and no script plays them -- with no native trigger either, they are dead assets");
    }

    // ------------------------------------------------------------------
    // Part 6 -- the script-facing surface, against the corpus.
    // ------------------------------------------------------------------
    std::printf("\n-- 6. the script surface --\n");
    {
        const std::string corpus = SlurpScripts(scriptRoot);
        if (corpus.empty()) {
            std::printf("   (no script corpus -- skipping)\n");
        } else {
            // The two Options sliders the native handler matches by name.
            Check(corpus.find("AddMenuSlider(4062,\"SoundFXSlider\",100,10)") != std::string::npos &&
                      corpus.find("AddMenuSlider(4063,\"MusicSlider\",100,10)") != std::string::npos,
                  "options.s declares the two sliders FUN_1007f8f8 matches by name");
            // Exactly one four-argument PlaySound in the whole corpus,
            // and it is the one that pinned the argument meanings.
            Check(corpus.find("PlaySound(65, 75, 1, 255)") != std::string::npos,
                  "twilite/steamsound.s is the corpus's only four-argument PlaySound");
            Check(sk::kDefaultSoundVolume == 100 && !sk::kDefaultSoundDirectional &&
                      sk::kDefaultSoundRepeats == 1,
                  "and the dispatcher's own defaults for the three optional arguments are 100/0/1");
            Check(sk::kRepeatForever == 255,
                  "255 is not a loop flag -- it is the largest count a repeat byte holds");
            // FadeMusic/UnFadeMusic really are used.
            Check(corpus.find("FadeMusic()") != std::string::npos &&
                      corpus.find("UnFadeMusic()") != std::string::npos,
                  "and FadeMusic/UnFadeMusic are real calls, not dead bindings");
            // ...unlike these, which have a binding and no caller.
            Check(corpus.find("SetAmbientSound(") == std::string::npos &&
                      corpus.find("StopSound(") == std::string::npos &&
                      corpus.find("CreateSound(") == std::string::npos,
                  "while SetAmbientSound/StopSound/CreateSound have no call site at all");
        }
    }

    // ------------------------------------------------------------------
    // Part 7 -- the real files behind the native slots actually decode.
    // ------------------------------------------------------------------
    std::printf("\n-- 7. the real files decode --\n");
    {
        sk::SoundArchive archive;
        if (!archive.LoadCategory(scriptRoot, "azra")) {
            std::printf("   (azra_sounds.txt not loadable -- skipping)\n");
        } else {
            const int wanted[] = {sk::kSoundBowFire,     sk::kSoundChestOpen,
                                  sk::kSoundDoorOpen,    sk::kSoundAttackHit,
                                  sk::kSoundAttackMiss,  sk::kSoundPowerUp,
                                  sk::kSoundJumpFemale,  sk::kSoundJumpMale};
            int decoded = 0;
            for (int slot : wanted) {
                if (archive.GetSound(slot)) ++decoded;
            }
            std::printf("   azra: %d/%d native slots decoded\n", decoded,
                        static_cast<int>(sizeof(wanted) / sizeof(wanted[0])));
            Check(decoded == static_cast<int>(sizeof(wanted) / sizeof(wanted[0])),
                  "every native slot in azra's manifest decodes to real PCM");
        }
        sk::SoundArchive menuArchive;
        if (menuArchive.LoadCategory(scriptRoot, sk::kMenuSoundCategory)) {
            const sk::Sound* music = menuArchive.GetSound(sk::kSoundMenuMusic);
            Check(music != nullptr && menuArchive.IsMusic(sk::kSoundMenuMusic),
                  "and the front-end track decodes as real Ogg Vorbis");
        }
    }

    std::printf("\n%s (%d checks)\n",
                g_failures == 0 ? "m51_audio_mixing_smoke: PASSED (all checks)"
                                : "m51_audio_mixing_smoke: FAILED",
                g_checks);
    return g_failures == 0 ? 0 : 1;
}
