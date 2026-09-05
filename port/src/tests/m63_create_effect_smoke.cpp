// M63 smoke test: `Level.CreateEffect(...)` and the animated-sprite entity.
//
// Thirteen shipped call sites, all ten-argument, all in a zone script's
// own `Init()`. What each part checks, and why it is the check that could
// fail:
//
//   1. The shipped corpus: how many sites, in which scripts, and that
//      every one of the ten argument slots holds what the case body reads
//      it as -- in particular that all thirteen pass lifetime 0, which is
//      what makes every scripted effect permanent.
//   2. The real `twilite.s` `Init()` end to end: five steam vents at their
//      exact shipped coordinates, through the real interpreter.
//   3. The binding's own argument mapping, and the real case's
//      `if (argc < 10) return 1` -- a nine-argument call is *accepted* and
//      ignored, not soft-failed.
//   4. That `+0x134` indexes `global.spr`. This is M48's open question,
//      and the check is the real archive's own geometry: two twelve-frame
//      16x64 runs at exactly 181-192 and 193-204, bounded on both sides by
//      slots of other sizes, one warm and one grey.
//   5. The animation and the lifetime, at this port's real frame delta --
//      including that `blaze.s`'s commented-out impact dies before its
//      eight frames finish.
//   6. The draw's two half-extents, against the real sprites' pixel sizes.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "assets/sprite_archive.h"
#include "assets/string_table.h"
#include "simkin_bindings/effect_entity.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/zone_script_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%-84s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

// One `Level.CreateEffect(...)` call site, with its ten arguments.
struct Site {
    std::string file;
    bool commentedOut = false;
    std::vector<int> args;
};

void CollectSites(const std::string& root, const std::string& relative, std::vector<Site>& out) {
    std::ifstream in(root + "/" + relative);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        size_t at = line.find("CreateEffect");
        if (at == std::string::npos) continue;
        size_t open = line.find('(', at);
        if (open == std::string::npos) continue;
        // The matching close paren, not the first one -- blaze.s's line
        // passes `target.GetPositionX()` for three of its ten arguments,
        // and taking the first `)` would truncate it to four.
        size_t close = std::string::npos;
        int depth = 0;
        for (size_t i = open; i < line.size(); ++i) {
            if (line[i] == '(') ++depth;
            if (line[i] == ')' && --depth == 0) {
                close = i;
                break;
            }
        }
        if (close == std::string::npos) continue;

        Site site;
        site.file = relative;
        size_t comment = line.find("//");
        site.commentedOut = comment != std::string::npos && comment < at;

        // Split on depth-0 commas; a non-numeric argument parses as 0 but
        // still counts, which is all part 1 needs from it.
        std::string inner = line.substr(open + 1, close - open - 1);
        std::string token;
        inner.push_back(',');
        depth = 0;
        for (char c : inner) {
            if (c == '(') ++depth;
            if (c == ')') --depth;
            if (c == ',' && depth == 0) {
                site.args.push_back(std::atoi(token.c_str()));
                token.clear();
            } else if (c != ' ' && c != '\t') {
                token.push_back(c);
            }
        }
        out.push_back(site);
    }
}

const char* kScriptFiles[] = {"crypt1.s", "twilite.s", "blaze.s"};

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";
    const std::string root = scriptRoot;

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m63_create_effect_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    // ---------------------------------------------------------------
    // Part 1: the shipped corpus.
    // ---------------------------------------------------------------
    std::printf("--- Part 1: every Level.CreateEffect() in the shipped scripts ---\n");
    std::vector<Site> sites;
    for (const char* file : kScriptFiles) CollectSites(root, file, sites);

    int live = 0, commented = 0, tenArg = 0, lifetimeZero = 0, rate128 = 0, arg2One = 0;
    int crypt1Fire = 0, twiliteSteam = 0;
    for (const Site& s : sites) {
        if (s.commentedOut) {
            ++commented;
            continue;
        }
        ++live;
        if (s.args.size() == 10) ++tenArg;
        if (s.args.size() < 10) continue;
        if (s.args[7] == 0) ++lifetimeZero;
        if (s.args[6] == 128) ++rate128;
        if (s.args[2] == 1) ++arg2One;
        if (s.args[0] == 181 && s.args[1] == 192) ++crypt1Fire;
        if (s.args[0] == 193 && s.args[1] == 204) ++twiliteSteam;
    }
    std::printf("   %d live call sites, %d commented out\n", live, commented);
    Check(live == 13, "13 live Level.CreateEffect() sites in the shipped corpus");
    Check(commented == 1, "one more is commented out (blaze.s, superseded by the native impact)");
    Check(tenArg == 13, "every live site passes exactly ten arguments");
    Check(arg2One == 13, "argument 3 (the draw flag at entity+0x58) is always 1");
    Check(rate128 == 13, "argument 7 (the animation rate) is always 128, i.e. 7.5 fps");
    Check(lifetimeZero == 13, "argument 8 (the lifetime) is always 0 -- every shipped effect is permanent");
    Check(crypt1Fire == 8, "crypt1.s spawns 8 effects over slots 181-192");
    Check(twiliteSteam == 5, "twilite.s spawns 5 effects over slots 193-204");

    // The commented-out blaze line is the only shipped call with a real
    // lifetime, and the only one whose position is an expression.
    const Site* blaze = nullptr;
    for (const Site& s : sites) {
        if (s.commentedOut) blaze = &s;
    }
    Check(blaze != nullptr && blaze->args.size() == 10 && blaze->args[0] == 12 &&
              blaze->args[1] == 19 && blaze->args[7] == 16,
          "blaze.s's commented line is slots 12-19 with lifetime 16, the one timed effect");

    // ---------------------------------------------------------------
    // Part 2: the real twilite.s Init(), through the interpreter.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 2: twilite.s Init() -- five real steam vents ---\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        std::unique_ptr<sk_bindings::ZoneScriptExecutable> script;
        skExecutableContext loadCtxt(&interpreter);
        try {
            script = std::make_unique<sk_bindings::ZoneScriptExecutable>(
                skString((root + "/twilite.s").c_str()), loadCtxt, stack);
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR loading twilite.s: %s)\n", e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR loading twilite.s: %s)\n", e.toString().ptr());
        }
        Check(script != nullptr, "twilite.s loads");
        if (script) {
            skRValueArray args;
            args.append(skRValue(skString("")));
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            try {
                script->method(skString("Init"), args, ret, ctxt);
            } catch (skRuntimeException& e) {
                std::printf("   (RUNTIME ERROR in Init: %s)\n", e.toString().ptr());
            }
            const std::vector<sk_bindings::EffectEntity>& effects = stack.level().effects();
            Check(effects.size() == 5, "the real Init() created five effects");
            bool placed = effects.size() == 5;
            // The five shipped coordinates, in script order.
            const int kExpected[5][3] = {{17290, 18560, 0},
                                         {15242, 17018, 0},
                                         {13696, 17022, 0},
                                         {15756, 14716, 0},
                                         {18304, 15224, 0}};
            for (size_t i = 0; placed && i < effects.size(); ++i) {
                if (effects[i].x != kExpected[i][0] || effects[i].y != kExpected[i][1] ||
                    effects[i].z != kExpected[i][2]) {
                    placed = false;
                }
            }
            Check(placed, "all five land at their exact shipped world coordinates");
            bool steam = !effects.empty();
            for (const sk_bindings::EffectEntity& e : effects) {
                if (e.firstSprite != 193 || e.lastSprite != 204 || e.sprite != 193 ||
                    !e.immortal || !e.looping) {
                    steam = false;
                }
            }
            Check(steam, "all five are permanent looping animations over slots 193-204");
        }
    }

    // ---------------------------------------------------------------
    // Part 3: the binding's argument mapping, and the argc < 10 rule.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 3: the ten arguments, and what a short call does ---\n");
    {
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(root, interpreter, &strings);
        skExecutableContext ctxt(&interpreter);

        // Nine arguments: the real case returns 1 without touching
        // anything. Accepted (not soft-failed), and nothing created.
        {
            skRValueArray args;
            for (int i = 0; i < 9; ++i) args.append(skRValue(1));
            skRValue ret;
            bool handled = stack.level().method(skString("CreateEffect"), args, ret, ctxt);
            Check(handled, "a nine-argument call is accepted, matching `if (argc < 10) return 1`");
            Check(stack.level().effects().empty(), "...and creates nothing");
        }

        // crypt1.s's first line, argument for argument.
        {
            const int kArgs[10] = {181, 192, 1, 26600, 21239, -1152, 128, 0, 512, 40};
            skRValueArray args;
            for (int v : kArgs) args.append(skRValue(v));
            skRValue ret;
            stack.level().method(skString("CreateEffect"), args, ret, ctxt);
            Check(stack.level().effects().size() == 1, "a ten-argument call creates one effect");
            if (!stack.level().effects().empty()) {
                const sk_bindings::EffectEntity& e = stack.level().effects().front();
                Check(e.firstSprite == 181 && e.lastSprite == 192 && e.sprite == 181,
                      "args 1-2 are the inclusive global.spr slot range, and seed the drawn slot");
                Check(e.drawFlags == 1, "arg 3 lands at entity+0x58");
                Check(e.x == 26600 && e.y == 21239 && e.z == -1152,
                      "args 4-6 are the world position (z stored 16-bit)");
                Check(e.animRate == 128 * sk_bindings::kEffectTimeScale,
                      "arg 7 is scaled by 15 into the animation rate");
                Check(e.immortal && e.life == 0,
                      "arg 8 == 0 arms the no-lifetime flag rather than a zero-length life");
                Check(e.sizeX == 512 && e.sizeY == 40, "args 9-10 are the two size scalars");
                Check(e.scale == sk_bindings::kEffectDefaultScale && e.scaleStart == e.scaleEnd,
                      "the scale ramp is flat at 0x80 -- only engine-spawned effects ramp");
            }
        }
    }

    // ---------------------------------------------------------------
    // Part 4: the art. `+0x134` indexes global.spr -- M48's open question.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 4: the art is global.spr, checked against the real archive ---\n");
    {
        sk::SpriteArchive archive;
        if (!archive.Load(root)) {
            std::printf("m63_create_effect_smoke: FAILED to load global.spr\n");
            return 1;
        }

        auto meanChannels = [&](int slot, int& w, int& h, int& r, int& g, int& b) {
            const sk::Sprite* s = archive.GetSprite(slot);
            w = h = r = g = b = -1;
            if (!s) return false;
            w = s->width;
            h = s->height;
            long rs = 0, gs = 0, bs = 0, n = 0;
            for (size_t k = 0; k < s->pixels.size(); ++k) {
                if (!s->opaque[k]) continue;
                uint16_t p = s->pixels[k];
                rs += ((p >> 11) & 0x1f) << 3;
                gs += ((p >> 5) & 0x3f) << 2;
                bs += (p & 0x1f) << 3;
                ++n;
            }
            if (n == 0) return false;
            r = static_cast<int>(rs / n);
            g = static_cast<int>(gs / n);
            b = static_cast<int>(bs / n);
            return true;
        };

        auto runIsUniform = [&](int first, int last, int w, int h) {
            for (int i = first; i <= last; ++i) {
                int sw = 0, sh = 0, r = 0, g = 0, bl = 0;
                if (!meanChannels(i, sw, sh, r, g, bl)) return false;
                if (sw != w || sh != h) return false;
            }
            return true;
        };

        Check(runIsUniform(181, 192, 16, 64),
              "crypt1.s's slots 181-192 are twelve 16x64 frames");
        Check(runIsUniform(193, 204, 16, 64),
              "twilite.s's slots 193-204 are twelve more 16x64 frames");
        Check(runIsUniform(12, 19, 32, 32), "blaze.s's slots 12-19 are eight 32x32 frames");

        // The runs are exactly bounded -- they are whole animations, not
        // arbitrary windows into a longer sequence of same-sized art.
        int w = 0, h = 0, r = 0, g = 0, b = 0;
        Check(meanChannels(180, w, h, r, g, b) && (w != 16 || h != 64),
              "slot 180 is a different size, so the fire run starts exactly at 181");
        Check(meanChannels(205, w, h, r, g, b) && (w != 16 || h != 64),
              "slot 205 is a different size, so the steam run ends exactly at 204");

        // Fire is warm, steam is not. Averaged over the whole run so a
        // single odd frame cannot decide it.
        auto runMean = [&](int first, int last, int& mr, int& mg, int& mb) {
            long rs = 0, gs = 0, bs = 0;
            int n = 0;
            for (int i = first; i <= last; ++i) {
                int sw, sh, r2, g2, b2;
                if (!meanChannels(i, sw, sh, r2, g2, b2)) continue;
                rs += r2;
                gs += g2;
                bs += b2;
                ++n;
            }
            if (n == 0) return false;
            mr = static_cast<int>(rs / n);
            mg = static_cast<int>(gs / n);
            mb = static_cast<int>(bs / n);
            return true;
        };
        int fr = 0, fg = 0, fb = 0, sr = 0, sg = 0, sb = 0, br = 0, bg = 0, bb = 0;
        bool haveFire = runMean(181, 192, fr, fg, fb);
        bool haveSteam = runMean(193, 204, sr, sg, sb);
        bool haveBlast = runMean(12, 19, br, bg, bb);
        std::printf("   mean RGB: fire (%d, %d, %d)  steam (%d, %d, %d)  blast (%d, %d, %d)\n", fr,
                    fg, fb, sr, sg, sb, br, bg, bb);
        Check(haveFire && fr - fb > 60, "the 181-192 run is warm -- fire, which is what crypt1 is");
        Check(haveSteam && std::abs(sr - sb) < 20,
              "the 193-204 run is neutral grey -- steam, in the zone that ships steamsound.s");
        Check(haveBlast && br - bb > 60, "the 12-19 run is warm too -- blaze's impact blast");

        // The blast dissipates: its opaque pixel count falls away over the
        // run, which a static icon set would not do.
        auto opaqueCount = [&](int slot) {
            const sk::Sprite* s = archive.GetSprite(slot);
            if (!s) return -1;
            int n = 0;
            for (size_t k = 0; k < s->opaque.size(); ++k) {
                if (s->opaque[k]) ++n;
            }
            return n;
        };
        int early = opaqueCount(13), late = opaqueCount(19);
        std::printf("   blast coverage: slot 13 = %d px, slot 19 = %d px\n", early, late);
        Check(early > 0 && late > 0 && late * 4 < early,
              "the blast run thins out to under a quarter coverage -- it is an explosion fading");

        // M48's own two slots, the spell projectile's `+0x134` -- the same
        // field, so the same table. Both are real 32x32 sprites.
        int pw2 = 0, ph2 = 0, pr2 = 0, pg2 = 0, pb2 = 0;
        int pw5 = 0, ph5 = 0, pr5 = 0, pg5 = 0, pb5 = 0;
        bool haveTwo = meanChannels(2, pw2, ph2, pr2, pg2, pb2);
        bool haveFive = meanChannels(5, pw5, ph5, pr5, pg5, pb5);
        std::printf("   projectiles: slot 2 %dx%d (%d, %d, %d), slot 5 %dx%d (%d, %d, %d)\n", pw2,
                    ph2, pr2, pg2, pb2, pw5, ph5, pr5, pg5, pb5);
        Check(haveTwo && haveFive && pw2 == 32 && ph2 == 32 && pw5 == 32 && ph5 == 32,
              "M48's projectile art selectors 2 and 5 are real 32x32 slots");
        Check(haveTwo && pr2 - pb2 > 60, "slot 2 (blaze/Blind/DoomHammer) is a warm fireball");

        // And the one the engine spawns itself, for the same reason.
        bool haveBlood = meanChannels(71, w, h, r, g, b);
        std::printf("   blood: slot 71 is %dx%d, mean RGB (%d, %d, %d)\n", w, h, r, g, b);
        Check(haveBlood && w == 32 && h == 32 && r > 100 && g < 40 && b < 40,
              "slot 71 (FUN_10081e5c's spurt) is a 32x32 dark red -- blood");
    }

    // ---------------------------------------------------------------
    // Part 5: the animation and the lifetime, at the real frame delta.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 5: the frame advance and the countdown ---\n");
    {
        const int dt = sk_bindings::kAiFrameDeltaUnits;  // 10, i.e. 0x100 units a second

        // crypt1's flame: twelve frames at rate 128.
        sk_bindings::EffectEntity fire =
            sk_bindings::MakeEffect(181, 192, 1, 26600, 21239, -1152, 128, 0, 512, 40);
        int ticksToWrap = 0;
        int highest = fire.sprite;
        for (int i = 0; i < 500; ++i) {
            sk_bindings::TickEffect(fire, dt);
            ++ticksToWrap;
            if (fire.sprite < highest) break;
            highest = fire.sprite;
        }
        std::printf("   flame: highest slot %d, wrapped after %d ticks (%.2f s at 25 Hz)\n", highest,
                    ticksToWrap, ticksToWrap / 25.0);
        Check(highest == 192, "the flame reaches its last slot, 192");
        Check(fire.sprite == 181, "and wraps back to its first, rather than expiring");
        Check(ticksToWrap == 41, "a twelve-frame loop takes 41 ticks -- 1.64 s, i.e. 7.5 fps");
        Check(fire.alive, "a lifetime-0 effect is still alive after a full loop");

        for (int i = 0; i < 100000; ++i) sk_bindings::TickEffect(fire, dt);
        Check(fire.alive, "...and after a hundred thousand more ticks: it is scenery, not an event");

        // blaze.s's commented-out impact: the one timed effect that ships.
        sk_bindings::EffectEntity blast = sk_bindings::MakeEffect(12, 19, 1, 0, 0, 0, 128, 16, 128, 128);
        Check(!blast.immortal && blast.life == 16 * sk_bindings::kEffectTimeScale,
              "lifetime 16 becomes 240 units");
        int ticksAlive = 0, lastSlot = blast.sprite;
        while (blast.alive && ticksAlive < 1000) {
            sk_bindings::TickEffect(blast, dt);
            ++ticksAlive;
            if (blast.alive) lastSlot = blast.sprite;
        }
        std::printf("   blast: died on tick %d, last slot drawn %d\n", ticksAlive, lastSlot);
        Check(ticksAlive == 24, "240 units at 10 a tick is 24 ticks -- 0.94 s, not a full second");
        // Eight frames at 75 cursor units a tick from 12*256 needs
        // (20*256 - 12*256)/75 = 28 ticks, so the impact is cut short.
        Check(lastSlot == 18,
              "the blast dies on slot 18: 24 ticks is short of the 28 its eight frames need");

        // The scale ramp, which only an engine-spawned effect uses.
        sk_bindings::EffectEntity spurt = sk_bindings::MakeEffect(71, 71, 1, 0, 0, 0, 128, 16, 48, 48);
        spurt.scaleStart = 0x100;
        spurt.scaleEnd = 0x40;
        spurt.gravity = true;
        sk_bindings::TickEffect(spurt, dt, 0);
        int firstScale = spurt.scale;
        while (spurt.alive) sk_bindings::TickEffect(spurt, dt, 0);
        Check(firstScale < 0x100 && firstScale > 0xf0,
              "FUN_10081e5c's spurt starts at 1:1 and has barely shrunk after one tick");
        Check(!spurt.immortal, "a spurt is not permanent, unlike everything a script makes");
    }

    // ---------------------------------------------------------------
    // Part 6: the draw's two half-extents.
    // ---------------------------------------------------------------
    std::printf("\n--- Part 6: FUN_1008b25c's half-extents on the real sprite sizes ---\n");
    {
        // crypt1's four distinct size pairs against the 16x64 flame.
        struct Case {
            int sizeX, sizeY, halfW, halfH;
        };
        const Case kCases[] = {
            {512, 40, 512, 640},
            {300, 64, 300, 1024},
            {256, 32, 256, 512},
            {512, 64, 512, 1024},
        };
        bool all = true;
        for (const Case& c : kCases) {
            sk_bindings::EffectEntity e =
                sk_bindings::MakeEffect(181, 192, 1, 0, 0, 0, 128, 0, c.sizeX, c.sizeY);
            int hw = sk_bindings::EffectHalfWidth(e, 16);
            int hh = sk_bindings::EffectHalfHeight(e, 64);
            if (hw != c.halfW || hh != c.halfH) {
                std::printf("   size (%d, %d): got half-extents (%d, %d), expected (%d, %d)\n",
                            c.sizeX, c.sizeY, hw, hh, c.halfW, c.halfH);
                all = false;
            }
        }
        Check(all, "all four shipped size pairs give the derived half-extents");

        // The constructor's own defaults, against the 32x32 art the engine
        // spawns -- an eighth of a tile either side.
        sk_bindings::EffectEntity dflt;
        Check(sk_bindings::EffectHalfWidth(dflt, 32) == 32 &&
                  sk_bindings::EffectHalfHeight(dflt, 32) == 32,
              "the default size of 8 on a 32x32 sprite is a 32-unit half-extent");

        // blaze's impact is square, as its 32x32 art and equal scalars say.
        sk_bindings::EffectEntity blast =
            sk_bindings::MakeEffect(12, 19, 1, 0, 0, 0, 128, 16, 128, 128);
        Check(sk_bindings::EffectHalfWidth(blast, 32) == sk_bindings::EffectHalfHeight(blast, 32),
              "the blaze impact is square");

        // The intermediate is a signed 16-bit field in the real code. No
        // shipped combination overflows it, which is worth knowing before
        // trusting any of the numbers above.
        bool safe = true;
        for (const Case& c : kCases) {
            if (c.sizeX * 16 > 32767 || c.sizeY * 64 > 32767) safe = false;
        }
        Check(safe, "no shipped size scalar overflows the 16-bit intermediate at entity+0x5a");
    }

    std::printf("\nm63_create_effect_smoke: %s (%d checks)\n",
                g_failures == 0 ? "PASSED (all checks)" : "FAILED", g_checks);
    return g_failures == 0 ? 0 : 1;
}
