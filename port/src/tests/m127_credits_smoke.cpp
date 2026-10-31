// M127: the credits screen -- `FUN_1006b168` (the load) and `FUN_1006b3fc`
// (screen mode 0xb's frame). See engine/credits_roll.h.
#include <cstdio>
#include <string>

#include "assets/string_table.h"
#include "engine/credits_roll.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "skInterpreter.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-86s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    std::printf("\n-- 1. the roll's arithmetic --\n");
    {
        Check(sk::CreditsAdvance(0, sk_bindings::kAiFrameDeltaUnits) == 2,
              "scroll += delta >> 2: 2px a 40ms tick");
        Check(sk::CreditsLineY(0, 0) == 0 && sk::CreditsLineY(3, 10) == 35,
              "lines are 15px apart, starting at -scroll");
        Check(!sk::CreditsLineVisible(0) && sk::CreditsLineVisible(1) &&
                  sk::CreditsLineVisible(164) && !sk::CreditsLineVisible(165) &&
                  !sk::CreditsLineVisible(-5),
              "drawn only for 1 <= y <= 164 (`y - 1U < 0xa4`)");
        Check(!sk::CreditsEnded(150, 10) && sk::CreditsEnded(151, 10),
              "it ends once the scroll passes lineCount * 15");
        Check(sk::CreditsLineFromRaw("Todd Howard\r\n") == "Todd Howard  ",
              "'\\r' and '\\n' are kept as spaces, as the engine keeps them");
    }

    std::printf("\n-- 2. ShowCredits reads credits.txt the engine's way --\n");
    {
        sk::StringTable strings;
        if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
            std::printf("m127_credits_smoke: no game data\n");
            return 1;
        }
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        stack.ShowCredits();
        const auto& lines = stack.creditsLines();
        Check(stack.creditsActive() && lines.size() > 50, "the screen opens with the whole file: " +
                                                             std::to_string(lines.size()) + " lines");
        bool foundTitle = false;
        for (const std::string& l : lines) foundTitle = foundTitle || l == "Shadowkey(TM)  ";
        Check(foundTitle, "a CRLF line keeps its two trailing spaces");
        stack.CloseCredits();
        stack.ShowCredits();
        Check(stack.creditsLines().size() == lines.size(),
              "a second visit re-reads it rather than appending");
        stack.CloseCredits();

        sk_bindings::MenuStack noFile("no/such/root", interpreter, &strings);
        noFile.ShowCredits();
        Check(!noFile.creditsActive(), "with no credits.txt the screen does not open at all");
    }

    std::printf("\nm127_credits_smoke: %d/%d checks passed -- %s\n", g_checks - g_failures,
                g_checks, g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
