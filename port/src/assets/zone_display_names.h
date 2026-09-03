#pragma once

// M26: real per-zone display-name string ids -- found while tracing the
// real zone-transition loading screen (see main.cpp's RenderLoadingScreen()
// comment and docs/PORT_ROADMAP.md's M26 entry for the full writeup). A
// stringtable.eng scan for the real "Travel to: " prefix (id 3950,
// confirmed verbatim) led to a real contiguous run at 3821-3840: exactly
// one entry per real zone (`azra.zmp` etc., 21 real zones total under
// system/apps/6r51), in a fixed order that reads like a real zone-index
// table (3820 itself is "Loading...", the generic/no-destination-named
// variant). `ffarena` is the one real zone with no entry in this run --
// left unmapped rather than guessing a slot that isn't actually there.
//
// Factored out from main.cpp (which also needs sk::StringTable::Get() to
// turn an id into real text -- not duplicated here) so the lookup table
// itself is unit-testable without the windowed game loop, same "small
// pure logic, testable in isolation" precedent simkin_bindings/combat.h
// and simkin_bindings/weapon_viewmodel.h already established.

#include <string>

namespace sk {

// Returns the real stringtable.eng id for `zoneName`'s display name
// (case-insensitive match against the real internal zone names, e.g.
// "azra", "GhstPass"), or -1 if `zoneName` isn't in the real table above.
int ZoneDisplayNameStringId(const std::string& zoneName);

}  // namespace sk
