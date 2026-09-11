"""What the port doesn't implement, measured against the real binding table.

The third measure, and the sibling of
`analyze_port_native_coverage.py`. That one asks "of the calls a script
writes with an explicit receiver, how many land on something" -- a
question whose blind spot the roadmap has warned about since M60: a
menu script's *bare* calls are invisible to it, and so is a name
implemented on one receiver but missing on the one the script actually
calls it on.

This one asks the question from the binary's side instead, where there
is no such blind spot: **the shipped game registers 648 unique native
names across its 28 tries. How many does this port implement at all,
and of the ones it doesn't, how many does a shipped script ever call?**

That last clause is what makes the output actionable. Most of what is
unimplemented is never called by anything: the binding tables carry a
large dead inheritance from the studio's own FPS engine (`MountFlak88`,
`SetNationality`, `MPToggleKillcam`, `SetClipSize`, `ShotsIgnoreWalls`,
...) that ships registered and unused. Those cost nothing and are listed
separately at the end, not ranked.

Method:
  * implemented set = every `skString("Name")` literal anywhere in
    port/src/simkin_bindings, which is how every handler in this port
    matches a method name;
  * registered set = `shadowkey/simkin_native_bindings.json`, the
    enumeration of all 28 tries (see SIMKIN_NATIVE_API.md);
  * call sites = every `Name(` and `X.Name(` across the 1,535 shipped
    `.s` files, with `//` comments stripped.

Usage:
    python shadowkey/ghidra/scripts/analyze_port_native_gaps.py [repo-root]

Caveats, both deliberate:
  * the implemented set is **receiver-agnostic**, so a name this port
    handles on one class and not on another counts as implemented. That
    is the M62 trap, and it is real: `SetName` lives on Item and Monster
    but not Door, `SetPassable` on Door but not Monster, `PlaySound` on
    Monster and Player but not Item -- all three are `0x14d08`
    Object/Entity-base bindings every entity should answer. This tool
    cannot see those; the **soft-fail log** is the measure that can, so
    read the two together.
  * call sites are counted receiver-agnostically too, so a name shared
    by two classes (43 of them are) pools its counts. Good enough for
    ranking, not for attribution -- the `class` column says which trie
    each name is actually registered in.
"""
import collections
import json
import os
import re
import sys

REPO = sys.argv[1] if len(sys.argv) > 1 else "."
SRC = os.path.join(REPO, "port", "src", "simkin_bindings")
BINDINGS = os.path.join(REPO, "shadowkey", "simkin_native_bindings.json")
SCRIPTS = os.path.join(
    REPO,
    "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-EnFrDeEsIt-26102004",
    "system", "apps", "6r51",
)

# Trie root offset -> what that class is, from SIMKIN_NATIVE_API.md. Only
# the ones that actually turn up in the output are named; anything else
# prints as its raw offset.
CLASS_NAMES = {
    "0x14cf0": "GameEngine (root)",
    "0x14cfc": "Popup",
    "0x14d08": "Object/Entity base",
    "0x14d38": "Multiplayer session",
    "0x14d68": "Actor",
    "0x14d74": "Item",
    "0x14da4": "Character stats (armor)",
    "0x14db0": "Character stats",
    "0x14dbc": "GameState / player manager",
    "0x14dd4": "DragonStarGeneralMenu",
    "0x14e10": "Trap / magic-damage mixin",
}


def main():
    implemented = set()
    for name in os.listdir(SRC):
        if not name.endswith((".cpp", ".h")):
            continue
        text = open(os.path.join(SRC, name), encoding="utf-8", errors="replace").read()
        implemented |= set(re.findall(r'skString\("([A-Za-z_][A-Za-z0-9_]*)"\)', text))

    registered = {}
    for cls in json.load(open(BINDINGS, encoding="utf-8"))["classes"]:
        for method in cls["bindings"]:
            registered.setdefault(method, []).append(cls["trie_root_offset"])

    # A bare `Name(` or a `Receiver.Name(` -- the receiver is matched but
    # discarded, see the second caveat.
    call = collections.Counter()
    where = collections.defaultdict(set)
    scanned = 0
    pattern = re.compile(
        r"(?:\b[A-Za-z_][A-Za-z0-9_]*\s*(?:\(\s*\))?\s*\.\s*)?"
        r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
    for dirpath, _, files in os.walk(SCRIPTS):
        for name in files:
            if not name.endswith(".s"):
                continue
            scanned += 1
            rel = os.path.relpath(os.path.join(dirpath, name), SCRIPTS)
            text = open(os.path.join(dirpath, name), encoding="utf-8",
                        errors="replace").read()
            text = re.sub(r"//[^\n]*", "", text)
            for match in pattern.finditer(text):
                call[match.group(1)] += 1
                where[match.group(1)].add(rel)

    missing = [n for n in registered if n not in implemented]
    live = sorted(((call[n], n) for n in missing if call[n]), reverse=True)
    dead = sorted(n for n in missing if not call[n])

    print("scripts scanned: %d" % scanned)
    print("registered native names: %d   implemented in the port: %d (%d%%)"
          % (len(registered), len(registered) - len(missing),
             round(100.0 * (len(registered) - len(missing)) / len(registered))))
    print("no handler anywhere: %d, of which a shipped script calls: %d"
          % (len(missing), len(live)))
    print()

    print("==== unimplemented AND called -- ranked by call sites")
    for count, name in live:
        classes = ", ".join(CLASS_NAMES.get(c, c) for c in registered[name])
        examples = ", ".join(sorted(where[name])[:2])
        print("  %4d  %-24s %-28s  e.g. %s" % (count, name, classes, examples))
    print()

    print("==== the same, totalled per class")
    per_class = collections.Counter()
    members = collections.defaultdict(list)
    for count, name in live:
        # A shared name is attributed to its first-registered class, which
        # is enough for ranking the buckets.
        key = CLASS_NAMES.get(registered[name][0], registered[name][0])
        per_class[key] += count
        members[key].append((count, name))
    for key, total in per_class.most_common():
        print("  %4d  %s" % (total, key))
        print("        " + ", ".join("%s(%d)" % (n, c)
                                      for c, n in sorted(members[key], reverse=True)))
    print()

    print("==== unimplemented and never called by any shipped script (%d)" % len(dead))
    print("     -- mostly the FPS-engine inheritance; no work implied.")
    print("     " + ", ".join(dead))


if __name__ == "__main__":
    main()
