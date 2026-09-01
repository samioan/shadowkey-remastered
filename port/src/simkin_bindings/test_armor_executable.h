#pragma once

// M2 smoke-test binding: logs every native call the real armor/*.s
// scripts make from their Init handler (SetName, SetArmorValue, ...)
// instead of actually implementing them, to prove the vendored Simkin
// interpreter round-trips a real game script end to end before any real
// game-object bindings exist. See port/third_party/simkin/PROVENANCE.md
// and the port scaffold plan's M2 milestone.

#include "skScriptedExecutable.h"

namespace sk_bindings {

class TestArmorExecutable : public skScriptedExecutable {
public:
    TestArmorExecutable(const skString& filename, skExecutableContext& ctxt)
        : skScriptedExecutable(filename, ctxt) {}

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;
};

}  // namespace sk_bindings
