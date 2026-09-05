"""Per-receiver native-binding coverage of the PC port against the real corpus.

Plain Python, no Ghidra needed -- the sibling of
`analyze_simkin_script_corpus.py`, asking the other direction's question.
That one asked "which native class does a given script belong to"; this
one asks **"how much of what the shipped scripts actually call does the
port implement, and where is the biggest hole"**.

Why per-receiver rather than per-name: SimKin dispatches on the object,
so each of the 28 classes has its own independent trie and the same name
can mean different things on different receivers (43 names are shared --
see SIMKIN_NATIVE_API.md). A flat "is this name implemented anywhere in
port/src" check therefore gives the wrong answer, and gives it
optimistically: it was what hid `GetPlayer().OpenMenu()`, 332 real call
sites, behind the `OpenMenu` that Item/Menu/Monster already had.

Method:
  * for the port side, collect the `skString("Name")` literals in each
    binding class's own .cpp/.h -- that is how every handler in
    port/src/simkin_bindings matches a method name, so the literal set is
    the implemented set;
  * for the corpus side, count `Receiver.Method(` call sites across all
    1,535 shipped .s files, with `//` comments stripped;
  * report, per receiver, the call sites that land on nothing.

Usage:
    python shadowkey/ghidra/scripts/analyze_port_native_coverage.py [repo-root]

Caveats, both deliberate:
  * only the receivers listed in RECEIVERS below are analysed. Those are
    the ones a script names explicitly. A *bare* call (`SetName(1234)`,
    the implicit-self call into the script's own class) can't be
    attributed by syntax alone -- `analyze_simkin_script_corpus.py` is
    the tool for that direction.
  * `GetOpener()`'s residual is mostly not native at all. Roughly half of
    what scripts call on an opener (`LockPicked`, `UseKey`, `OpenChest`,
    `MagicDamage`, ...) are handler names declared in that placed
    object's own .s file, i.e. SimKin-to-SimKin dispatch. Names like
    those in the output are expected, not gaps.
"""
import collections
import os
import re
import sys

REPO = sys.argv[1] if len(sys.argv) > 1 else "."
SRC = os.path.join(REPO, "port", "src", "simkin_bindings")
SCRIPTS = os.path.join(
    REPO,
    "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-EnFrDeEsIt-26102004",
    "system", "apps", "6r51",
)


def skstrings(*files):
    """Every skString("Name") literal in the given binding sources."""
    names = set()
    for f in files:
        path = os.path.join(SRC, f)
        if not os.path.exists(path):
            print("missing: %s" % path, file=sys.stderr)
            continue
        text = open(path, encoding="utf-8", errors="replace").read()
        names |= set(re.findall(r'skString\("([A-Za-z_][A-Za-z0-9_]*)"\)', text))
    return names


def main():
    player = skstrings("player_executable.cpp", "player_executable.h")
    level = skstrings("level_executable.cpp", "level_executable.h")
    monster = skstrings("monster_executable.cpp", "monster_executable.h")
    every = skstrings(*[f for f in os.listdir(SRC) if f.endswith((".cpp", ".h"))])

    # Receiver expression as it is written in a .s file -> (label, the set
    # of names the port implements *on that receiver*).
    receivers = {
        "GetPlayer()": ("Player/GameState + Character stats", player),
        "Level": ("Zone/Level", level),
        "GetOwner()": ("Character stats (the wielder)", player | monster),
        "GetTarget()": ("the AI's current target", monster | player),
        "GetOpener()": ("generic Object/Entity -- see the caveat", every),
    }

    calls = collections.Counter()
    sites = collections.defaultdict(set)
    nfiles = 0
    pattern = re.compile(
        r"\b(GetPlayer\(\)|GetOwner\(\)|GetOpener\(\)|GetTarget\(\)|Level)"
        r"\s*\.\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(")
    for dirpath, _, files in os.walk(SCRIPTS):
        for name in files:
            if not name.endswith(".s"):
                continue
            nfiles += 1
            rel = os.path.relpath(os.path.join(dirpath, name), SCRIPTS)
            text = open(os.path.join(dirpath, name), encoding="utf-8",
                        errors="replace").read()
            text = re.sub(r"//[^\n]*", "", text)
            for m in pattern.finditer(text):
                calls[(m.group(1), m.group(2))] += 1
                sites[(m.group(1), m.group(2))].add(rel)

    print("scripts scanned: %d" % nfiles)
    print()
    for recv, (label, impl) in receivers.items():
        total = sum(c for (r, _), c in calls.items() if r == recv)
        if total == 0:
            continue
        hit = sum(c for (r, n), c in calls.items() if r == recv and n in impl)
        gaps = sorted(((c, n) for (r, n), c in calls.items()
                       if r == recv and n not in impl), reverse=True)
        print("==== %s -- %s" % (recv, label))
        print("     %d call sites, %d handled (%d%%), %d unhandled names"
              % (total, hit, round(100.0 * hit / total), len(gaps)))
        for count, name in gaps:
            elsewhere = " [implemented on another receiver]" if name in every else ""
            examples = ", ".join(sorted(sites[(recv, name)])[:2])
            print("  %5d  %-24s%s   e.g. %s" % (count, name, elsewhere, examples))
        print()


if __name__ == "__main__":
    main()
