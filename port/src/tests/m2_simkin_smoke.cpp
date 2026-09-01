// M2 smoke test: loads a real game .s script through the vendored Simkin
// interpreter and runs its Init handler, with TestArmorExecutable logging
// every native call it makes. Confirms the real .s corpus parses against
// the stock interpreter with zero syntax deviation before any real
// bindings are written. See the port scaffold plan's M2 milestone.
#include <cstdio>

#include "simkin_bindings/test_armor_executable.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

int main(int argc, char** argv) {
    const char* scriptPath =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51/armor/iron_cuirass.s";

    std::printf("shadowkey-port M2 smoke test: loading %s\n", scriptPath);

    skInterpreter interpreter;
    // AR_Heavy is referenced as a bare identifier by the script
    // (SetArmorConstraint(AR_Heavy)); the real engine must register these
    // as global constants before running any script -- stand in with a
    // placeholder value just to prove the identifier resolves.
    interpreter.addGlobalVariable(skString("AR_Heavy"), skRValue(1));

    skExecutableContext loadCtxt(&interpreter);
    try {
        sk_bindings::TestArmorExecutable armor(skString(scriptPath), loadCtxt);

        skRValueArray args;
        args.append(skRValue(0));  // placeholder for Init's "(s)" parameter
        skRValue ret;
        skExecutableContext callCtxt(&interpreter);
        bool handled = armor.method(skString("Init"), args, ret, callCtxt);

        std::printf("shadowkey-port M2 smoke test: Init handled=%s\n",
                    handled ? "true" : "false");
        return handled ? 0 : 1;
    } catch (skParseException& e) {
        std::printf("shadowkey-port M2 smoke test: PARSE ERROR: %s\n", e.toString().ptr());
        return 2;
    } catch (skRuntimeException& e) {
        std::printf("shadowkey-port M2 smoke test: RUNTIME ERROR: %s\n", e.toString().ptr());
        return 2;
    }
}
