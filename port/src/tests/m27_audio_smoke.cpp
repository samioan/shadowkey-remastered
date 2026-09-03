// M27 (real audio) smoke test: proves the real .wav/.ogg decode pipeline
// against real files, the real per-zone sound-slot manifest against real
// corpus PlaySound()/PlayAmbient() call sites, and that a real script's
// call genuinely reaches AudioEngine (headless-safe: AudioEngine::Init()
// may legitimately fail with no audio device in a CI/build environment --
// this test only requires that nothing throws/crashes either way).
//
// Real data, read directly off the install image:
//   door.s: `GetPlayer().PlaySound(63)` on open / `PlaySound(62)` on close
//   azra_sounds.txt: `62 door_close.wav`, `63 door_open.wav`,
//     `73 explore3.ogg`
//   azra.s: `Level.PlayAmbient(73,100)`
#include <cstdio>
#include <string>

#include "assets/sound_archive.h"
#include "assets/string_table.h"
#include "audio/audio_engine.h"
#include "audio/vorbis_decoder.h"
#include "audio/wav_file.h"
#include "simkin_bindings/door_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    bool ok = true;

    // --- Part 1: real .wav decode ---
    sk::Sound doorOpen;
    bool wavOk = sk::LoadWavFile(std::string(scriptRoot) + "/door_open.wav", doorOpen);
    std::printf("LoadWavFile(door_open.wav): %s -- %d channel(s), %dHz, %zu sample(s) %s\n",
                wavOk ? "true" : "false", doorOpen.channels, doorOpen.sampleRate,
                doorOpen.samples.size(), wavOk ? "OK" : "FAILED");
    if (!wavOk || doorOpen.empty()) ok = false;
    bool wavFormatOk = doorOpen.channels == 1 && doorOpen.sampleRate == 8000;
    std::printf("  real format: mono, 8000Hz (expected) %s\n", wavFormatOk ? "OK" : "FAILED");
    if (!wavFormatOk) ok = false;

    // --- Part 2: real .ogg decode ---
    sk::Sound explore3;
    bool oggOk = sk::DecodeOggFile(std::string(scriptRoot) + "/explore3.ogg", explore3);
    std::printf("DecodeOggFile(explore3.ogg): %s -- %d channel(s), %dHz, %zu sample(s) %s\n",
                oggOk ? "true" : "false", explore3.channels, explore3.sampleRate,
                explore3.samples.size(), oggOk ? "OK" : "FAILED");
    if (!oggOk || explore3.empty()) ok = false;

    // --- Part 3: real per-zone sound manifest ---
    sk::SoundArchive sounds;
    bool categoryOk = sounds.LoadCategory(scriptRoot, "azra");
    std::printf("SoundArchive::LoadCategory(\"azra\"): %s %s\n", categoryOk ? "true" : "false",
                categoryOk ? "OK" : "FAILED");
    if (!categoryOk) ok = false;

    const sk::Sound* slot63 = sounds.GetSound(63);  // real door_open.wav slot
    bool slot63Ok = slot63 && !slot63->empty() && !sounds.IsMusic(63);
    std::printf("GetSound(63) (real azra_sounds.txt \"63 door_open.wav\"): decoded=%s isMusic=%s "
                "%s\n",
                slot63 ? "true" : "false", sounds.IsMusic(63) ? "true" : "false",
                slot63Ok ? "OK" : "FAILED");
    if (!slot63Ok) ok = false;

    const sk::Sound* slot73 = sounds.GetSound(73);  // real explore3.ogg slot
    bool slot73Ok = slot73 && !slot73->empty() && sounds.IsMusic(73);
    std::printf("GetSound(73) (real azra_sounds.txt \"73 explore3.ogg\"): decoded=%s isMusic=%s "
                "%s\n",
                slot73 ? "true" : "false", sounds.IsMusic(73) ? "true" : "false",
                slot73Ok ? "OK" : "FAILED");
    if (!slot73Ok) ok = false;

    const sk::Sound* slotMissing = sounds.GetSound(4);  // real azra_sounds.txt "4 NULL.wav"
    bool slotMissingOk = slotMissing == nullptr;
    std::printf("GetSound(4) (real azra_sounds.txt \"4 NULL.wav\" sentinel): %s (expected null) "
                "%s\n",
                slotMissing ? "non-null" : "null", slotMissingOk ? "OK" : "FAILED");
    if (!slotMissingOk) ok = false;

    // --- Part 4: real script call sites genuinely reach the audio system
    // (headless-safe -- doesn't assert playback happened, just that a
    // real call handles cleanly whether or not a real audio device is
    // available in this environment). ---
    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.eng");
    sk::AudioEngine audio;
    bool audioInitOk = audio.Init();
    std::printf("AudioEngine::Init(): %s (informational -- may legitimately fail with no audio "
                "device)\n",
                audioInitOk ? "true" : "false");

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings, &sounds, &audio);

    // door.s's real OnUse(): GetPlayer().PlaySound(63) on open.
    std::string doorPath = std::string(scriptRoot) + "/door.s";
    bool doorRanOk = false;
    try {
        skExecutableContext loadCtxt(&interpreter);
        sk_bindings::DoorExecutable door(skString(doorPath.c_str()), loadCtxt, stack.player());
        skRValueArray initArgs;
        initArgs.append(skRValue(0));
        skRValue ret;
        skExecutableContext callCtxt(&interpreter);
        door.method(skString("Init"), initArgs, ret, callCtxt);
        skExecutableContext useCtxt(&interpreter);
        doorRanOk = door.method(skString("OnUse"), initArgs, ret, useCtxt);
        audio.Update();
    } catch (skParseException& e) {
        std::printf("m27_audio_smoke: FAILED -- PARSE ERROR loading door.s: %s\n",
                    e.toString().ptr());
        return 1;
    } catch (skRuntimeException& e) {
        std::printf("m27_audio_smoke: FAILED -- RUNTIME ERROR loading door.s: %s\n",
                    e.toString().ptr());
        return 1;
    }
    std::printf("door.s real OnUse() (GetPlayer().PlaySound(63) on open) ran without throwing: %s "
                "%s\n",
                doorRanOk ? "true" : "false", doorRanOk ? "OK" : "FAILED");
    if (!doorRanOk) ok = false;

    // azra.s's real Init(): Level.PlayAmbient(73,100).
    skRValueArray ambientArgs;
    ambientArgs.append(skRValue(73));
    ambientArgs.append(skRValue(100));
    skRValue ambientRet;
    skExecutableContext ambientCtxt(&interpreter);
    bool ambientHandled =
        stack.level().method(skString("PlayAmbient"), ambientArgs, ambientRet, ambientCtxt);
    audio.Update();
    std::printf("Level.PlayAmbient(73,100) (real azra.s Init() call) handled without throwing: %s "
                "%s\n",
                ambientHandled ? "true" : "false", ambientHandled ? "OK" : "FAILED");
    if (!ambientHandled) ok = false;

    std::printf("\nm27_audio_smoke: %s\n", ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
