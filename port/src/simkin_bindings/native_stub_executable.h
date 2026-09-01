#pragma once

// Base class for lightweight native objects handed back to scripts that
// don't need a .s file's TreeNode backing -- popup menus, floating text,
// the player object, etc. -- they only need to answer .Method(...) calls.
// Implements skiExecutable's boilerplate with soft-fail defaults so
// subclasses only have to override method().

#include "skiExecutable.h"

namespace sk_bindings {

class NativeStubExecutable : public skiExecutable {
public:
    explicit NativeStubExecutable(const char* debugTypeName) : m_TypeName(debugTypeName) {}
    ~NativeStubExecutable() override = default;

    int executableType() const override { return START_USER_TYPES; }
    int intValue() const override { return 0; }
    bool boolValue() const override { return true; }
    Char charValue() const override { return 0; }
    skString strValue() const override { return skString(m_TypeName); }
#ifdef USE_FLOATING_POINT
    float floatValue() const override { return 0.0f; }
#endif
    bool setValue(const skString&, const skString&, const skRValue&) override { return false; }
    bool setValueAt(const skRValue&, const skString&, const skRValue&) override { return false; }
    bool getValue(const skString&, const skString&, skRValue&) override { return false; }
    bool getValueAt(const skRValue&, const skString&, skRValue&) override { return false; }
    bool equals(const skiExecutable* other) const override { return other == this; }
    skExecutableIterator* createIterator(const skString&) override { return nullptr; }
    skExecutableIterator* createIterator() override { return nullptr; }
    skString getSource(const skString&) override { return skString(""); }
    void getInstanceVariables(skRValueTable&) override {}
    void getAttributes(skRValueTable&) override {}

    const char* debugTypeName() const { return m_TypeName; }

private:
    const char* m_TypeName;
};

}  // namespace sk_bindings
