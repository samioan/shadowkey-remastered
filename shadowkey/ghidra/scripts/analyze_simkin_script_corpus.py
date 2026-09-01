"""
Cross-check the real .s SimKin script corpus against the 702-entry native
binding table (shadowkey/simkin_native_bindings.json) to:

  1. Validate each of the 28 classes' hypothesized identity by comparing its
     binding-name set against the "bare call" set of scripts whose directory
     already strongly implies a type (armor/, weapons/, items/, monsters/,
     spells/, menus/).
  2. Guess what object each "GetXxx()" factory call returns, by comparing the
     set of methods called on its result (`GetXxx().Method(...)`) against
     each class's binding set.
  3. Compute which of the 702 bindings are ever referenced (by name) anywhere
     in the real script corpus -> a "used vs. dead for a minimal port" list.

Pure text analysis over the plaintext .s corpus - no Ghidra, no RE. Run with
a plain `python` interpreter (not pyghidra).
"""
import json
import re
import sys
from collections import defaultdict, Counter
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
BINDINGS_JSON = REPO / "shadowkey" / "simkin_native_bindings.json"
SCRIPT_ROOT = REPO / "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-EnFrDeEsIt-26102004" / "system" / "apps" / "6r51"

# offset -> hypothesized class name, from docs/SIMKIN_NATIVE_API.md's table.
HYPOTHESIZED_NAMES = {
    "0x14cf0": "GameEngine (root)",
    "0x14dbc": "Player/GameState",
    "0x14db0": "Character stats",
    "0x14d08": "Object/Entity (world base)",
    "0x14da4": "Monster (AI)",
    "0x14d68": "Actor (AI movement)",
    "0x14d38": "Zone/Level",
    "0x14d74": "Item",
    "0x14d44": "Weapon",
    "0x14dd4": "Character-manager menu",
    "0x14ce4": "Table/grid widget",
    "0x14cfc": "Menu (generic)",
    "0x14d20": "Widget (UI base)",
    "0x14ccc": "Door/trap trigger",
    "0x14d5c": "Camera/player-feedback",
    "0x14dc8": "Spell",
    "0x14de0": "Store/shop menu",
    "0x14d2c": "Icon/sprite widget",
    "0x14d50": "Collection",
    "0x14df8": "Zone effects",
    "0x14cd8": "Dropdown/slider widget",
    "0x14d14": "Sprite-attach mixin",
    "0x14d80": "Weapon-damage mixin",
    "0x14e04": "Armor",
    "0x14d8c": "Menu-stack manager",
    "0x14d98": "Encounter spawner",
    "0x14dec": "Action-queue HUD",
    "0x14e10": "Spell-damage mixin",
}

# Directories (relative to SCRIPT_ROOT) whose contents strongly imply a
# native class by construction (per docs/shadowkey ROADMAP conventions).
DIR_HINTS = {
    "armor": "Armor",
    "weapons": "Weapon",
    "items": "Item",
    "monsters": "Monster (AI)",
    "spells": "Spell",
    "menus": "Menu (generic)",
}

# Classes already confirmed (by name or offset) as of the first corpus-cross-
# check pass - see docs/SIMKIN_NATIVE_API.md's "Cross-checked against the
# real .s script corpus" section. Everything else is still open; the extra
# analysis passes below focus signal-hunting on those.
CONFIRMED_OFFSETS = {
    "0x14cf0",  # GameEngine (root) - ConfigKeysMenu/Default
    "0x14dc8",  # Spell - SetSpellType fallthrough + directory-hint
    "0x14e04",  # Armor - directory-hint
    "0x14d44",  # Weapon - directory-hint
    "0x14d80",  # Weapon-damage mixin - directory-hint (dominant for weapons/)
    "0x14d74",  # Item - directory-hint
    "0x14da4",  # Monster (AI) - directory-hint
    "0x14db0",  # Character stats - GetOwner() factory-call
    "0x14dbc",  # Player/GameState - GetPlayer() factory-call (composite)
}

KEYWORDS = {
    "if", "else", "while", "for", "return", "true", "false", "null",
    "and", "or", "not", "new", "break", "continue",
}

BARE_CALL_RE = re.compile(r"(?<![.\w])([A-Za-z_][A-Za-z0-9_]*)\s*\(")
DOTTED_CALL_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*\)\s*\.\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(")
VAR_DOTTED_CALL_RE = re.compile(r"(?<![.\w)])([A-Za-z_][A-Za-z0-9_]*)\s*\.\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(")
# NOTE: the trailing \b is load-bearing, not decorative - without it the
# \w* capture can backtrack one character short (e.g. "OpenMenu(" would
# wrongly capture "OpenMen") to satisfy the negative lookahead, since the
# lookahead alone doesn't stop the regex engine from trying a truncated
# match. \b forces the capture to stop only at a real word boundary.
DOTTED_PROP_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*\)\s*\.\s*([A-Za-z_][A-Za-z0-9_]*)\b(?!\s*\()")

# Event-handler declaration syntax: `HandlerName[ (args) { ... } ]` - the
# `[` right after the name means BARE_CALL_RE (which requires an immediate
# `(`) never matches these, but strip comments first regardless.
LINE_COMMENT_RE = re.compile(r"//.*")


def strip_comments(text):
    return LINE_COMMENT_RE.sub("", text)


def load_bindings():
    data = json.loads(BINDINGS_JSON.read_text(encoding="utf-8"))
    classes = {}
    for c in data["classes"]:
        off = c["trie_root_offset"]
        classes[off] = {
            "name": HYPOTHESIZED_NAMES.get(off, off),
            "bindings": set(c["bindings"].keys()),
            "count": len(c["bindings"]),
        }
    return classes


def score_overlap(call_set, binding_set):
    if not call_set:
        return 0, 0.0
    inter = call_set & binding_set
    return len(inter), len(inter) / len(call_set)


def dice(call_set, binding_set):
    if not call_set or not binding_set:
        return 0.0
    inter = len(call_set & binding_set)
    if inter == 0:
        return 0.0
    return 2 * inter / (len(call_set) + len(binding_set))


def main():
    classes = load_bindings()
    all_binding_names = set()
    name_to_classes = defaultdict(list)
    for off, c in classes.items():
        all_binding_names |= c["bindings"]
        for n in c["bindings"]:
            name_to_classes[n].append(off)

    files = sorted(SCRIPT_ROOT.rglob("*.s"))
    print(f"# {len(files)} .s files under {SCRIPT_ROOT.relative_to(REPO)}", file=sys.stderr)

    # Per-file bare-call sets, keyed by relative path.
    file_bare_calls = {}
    # dir hint bucket -> union of bare-call names across every file in it
    dir_bucket_union = defaultdict(set)
    # dir hint bucket -> Counter of best-matching class name (per-file, Dice score)
    dir_bucket_matches = defaultdict(Counter)
    dir_bucket_files = defaultdict(list)

    # Factory-call receivers: "GetXxx" -> Counter of methods called on result
    factory_methods = defaultdict(Counter)
    factory_props = defaultdict(Counter)
    # Plain local-variable dotted calls: var name -> Counter of methods
    # (weaker signal - var could hold any object type, but still useful in
    # aggregate, e.g. "funtext" showing up with widget-shaped methods).
    var_methods = defaultdict(Counter)

    used_names_global = Counter()
    # name (bare, dotted-var method, or dotted-factory method) -> set of
    # files it was seen called in, regardless of receiver shape. Used for
    # the direct-name-search pass (unconfirmed classes' distinctive members).
    name_call_files = defaultdict(set)
    # var name (any local, project-wide, not just menus/) -> Counter of
    # methods called on it.
    var_methods_global = defaultdict(Counter)

    for f in files:
        rel = f.relative_to(SCRIPT_ROOT)
        try:
            text = f.read_text(encoding="utf-8", errors="replace")
        except Exception as e:
            print(f"skip {rel}: {e}", file=sys.stderr)
            continue
        text = strip_comments(text)

        bare = set()
        for m in BARE_CALL_RE.finditer(text):
            name = m.group(1)
            if name in KEYWORDS:
                continue
            bare.add(name)
            used_names_global[name] += 1
            name_call_files[name].add(rel)

        for m in DOTTED_CALL_RE.finditer(text):
            factory, method = m.group(1), m.group(2)
            factory_methods[factory][method] += 1
            used_names_global[method] += 1
            name_call_files[method].add(rel)

        for m in DOTTED_PROP_RE.finditer(text):
            factory, prop = m.group(1), m.group(2)
            factory_props[factory][prop] += 1

        for m in VAR_DOTTED_CALL_RE.finditer(text):
            var, method = m.group(1), m.group(2)
            if var in KEYWORDS:
                continue
            var_methods[var][method] += 1
            var_methods_global[var][method] += 1
            used_names_global[method] += 1
            name_call_files[method].add(rel)

        file_bare_calls[rel] = bare

        # directory-hint bucketing: look at the first path component that's
        # a known hint dir (armor/, weapons/, items/, monsters/, spells/,
        # menus/), including nested (menus/*.s).
        parts = rel.parts
        hint_dir = None
        if len(parts) > 1 and parts[0] in DIR_HINTS:
            hint_dir = parts[0]
        if hint_dir:
            dir_bucket_union[hint_dir] |= bare
            best = None
            best_score = 0.0
            for off, c in classes.items():
                s = dice(bare, c["bindings"])
                if s > best_score:
                    best_score = s
                    best = c["name"]
            if best and best_score > 0:
                dir_bucket_matches[hint_dir][best] += 1
                dir_bucket_files[hint_dir].append((rel, best, best_score))

    # ---- Report 1: directory-hint validation ----
    print("=" * 100)
    print("DIRECTORY-HINT VALIDATION (best-matching class per file, Dice coefficient)")
    print("=" * 100)
    for d, expected in DIR_HINTS.items():
        counts = dir_bucket_matches.get(d, Counter())
        total = sum(counts.values())
        print(f"\n{d}/  (expected: {expected}, hint files with >=1 match: {total})")
        for cls, n in counts.most_common(5):
            flag = "  <-- expected" if cls == expected else ""
            print(f"    {n:4d}  {cls}{flag}")

    print("\n" + "-" * 100)
    print("DIRECTORY-HINT VALIDATION, take 2: per-class RECALL against the directory's union call-set")
    print("(what fraction of a class's OWN binding vocabulary shows up anywhere in this directory -")
    print(" the strongest signal for small/specific classes like Armor/Weapon/Spell)")
    print("-" * 100)
    for d, expected in DIR_HINTS.items():
        union = dir_bucket_union.get(d, set())
        print(f"\n{d}/  ({len(union)} distinct bare-call names across the directory)")
        scored = []
        for off, c in classes.items():
            inter, recall = score_overlap(c["bindings"], union)  # recall wrt class's own set
            if inter > 0:
                scored.append((recall, inter, c["name"], c["count"]))
        scored.sort(reverse=True)
        for recall, inter, name, count in scored[:6]:
            flag = "  <-- expected" if name == expected else ""
            print(f"    {inter:3d}/{count:3d} ({recall:.0%} of class's own vocab)  {name}{flag}")

    # ---- Report 2: factory-call return-type identification ----
    print("\n" + "=" * 100)
    print("FACTORY CALL RETURN-TYPE IDENTIFICATION (GetXxx().Method() pattern)")
    print("=" * 100)
    print("\nAll factory-call names seen in Name().Method()/Name().prop form, by call-site count:")
    all_factory_totals = sorted(
        ((f, sum(m.values())) for f, m in factory_methods.items()), key=lambda kv: -kv[1]
    )
    print("  " + ", ".join(f"{f}({n})" for f, n in all_factory_totals))

    for factory, methods in sorted(factory_methods.items(), key=lambda kv: -sum(kv[1].values())):
        total = sum(methods.values())
        if total < 2:
            continue
        call_set = set(methods.keys()) | set(factory_props.get(factory, {}).keys())
        scored = []
        for off, c in classes.items():
            inter, frac = score_overlap(call_set, c["bindings"])
            d = dice(call_set, c["bindings"])
            if inter > 0:
                scored.append((d, inter, frac, c["name"]))
        scored.sort(reverse=True)
        print(f"\n{factory}()  [{total} call sites, {len(call_set)} distinct methods]")
        for d, inter, frac, name in scored[:3]:
            print(f"    dice={d:.2f}  {inter:3d}/{len(call_set)} of call-set overlap ({frac:.0%})  {name}")
        print(f"    methods seen: {', '.join(m for m, _ in methods.most_common(12))}")

    # ---- Report 3: used vs dead bindings ----
    print("\n" + "=" * 100)
    print("USED VS DEAD BINDINGS (matched by name against the 702-entry table)")
    print("=" * 100)
    used_in_table = set(used_names_global) & all_binding_names
    dead_in_table = all_binding_names - used_in_table
    print(f"\n{len(used_in_table)} / {len(all_binding_names)} unique binding names appear "
          f"(as a bare call or a .Method() call) somewhere in the {len(files)}-file script corpus.")
    print(f"{len(dead_in_table)} binding names never appear as a call in any script "
          f"(may still be used via reflection/string dispatch, or may be genuinely dead for a minimal port).")

    per_class_used = {}
    for off, c in classes.items():
        u = c["bindings"] & used_in_table
        per_class_used[off] = u
        print(f"\n  {c['name']:32s} ({off}): {len(u):3d}/{c['count']:3d} bindings attested in scripts")

    dead_path = REPO / "shadowkey" / "simkin_bindings_unused_in_scripts.json"
    dead_out = {}
    for off, c in classes.items():
        dead = sorted(c["bindings"] - used_in_table)
        if dead:
            dead_out[off] = {"name": c["name"], "unused": dead}
    dead_path.write_text(json.dumps(dead_out, indent=2), encoding="utf-8")
    print(f"\nWrote per-class unused-binding lists to {dead_path.relative_to(REPO)}")

    # ---- Report 4: reused-name ambiguity actually observed ----
    print("\n" + "=" * 100)
    print("REUSED BINDING NAMES ACTUALLY CALLED IN SCRIPTS (ambiguous by name alone)")
    print("=" * 100)
    for name in sorted(used_in_table):
        owners = name_to_classes.get(name, [])
        if len(owners) > 1:
            owner_names = [classes[o]["name"] for o in owners]
            print(f"  {name:28s} -> {', '.join(owner_names)}")

    # ---- Report 5: every factory-call name, any count, scored only ----
    # against still-unconfirmed classes (broadens report 2's total<2 cutoff,
    # which was tuned for the giant root/Player/Character-stats classes and
    # hides real signal for small unconfirmed classes where even 2-3 call
    # sites matching most of a 5-9-member class is meaningful).
    print("\n" + "=" * 100)
    print("REPORT 5: EVERY FACTORY-CALL NAME vs. UNCONFIRMED CLASSES ONLY (any call count)")
    print("=" * 100)
    unconfirmed = {off: c for off, c in classes.items() if off not in CONFIRMED_OFFSETS}
    for factory, methods in sorted(factory_methods.items(), key=lambda kv: -sum(kv[1].values())):
        call_set = set(methods.keys()) | set(factory_props.get(factory, {}).keys())
        if not call_set:
            continue
        scored = []
        for off, c in unconfirmed.items():
            inter, frac = score_overlap(call_set, c["bindings"])
            d = dice(call_set, c["bindings"])
            if inter > 0:
                scored.append((d, inter, frac, c["name"]))
        if not scored:
            continue
        scored.sort(reverse=True)
        total = sum(methods.values())
        print(f"\n{factory}()  [{total} call sites, {len(call_set)} distinct methods: "
              f"{', '.join(sorted(call_set))[:200]}]")
        for d, inter, frac, name in scored[:3]:
            print(f"    dice={d:.2f}  {inter:3d}/{len(call_set)} overlap ({frac:.0%})  {name}")

    # ---- Report 6: direct name search for each unconfirmed class's most ----
    # distinctive (non-reused) members - do they appear anywhere at all,
    # and in which files?
    print("\n" + "=" * 100)
    print("REPORT 6: DISTINCTIVE-MEMBER DIRECT SEARCH FOR UNCONFIRMED CLASSES")
    print("(non-reused member names of each unconfirmed class, checked against every call")
    print(" site found anywhere in the corpus - bare, var.Method(), or Factory().Method())")
    print("=" * 100)
    for off, c in unconfirmed.items():
        distinctive = sorted(n for n in c["bindings"] if len(name_to_classes[n]) == 1)
        hits = [(n, name_call_files.get(n, set())) for n in distinctive]
        hits = [(n, files_) for n, files_ in hits if files_]
        print(f"\n{c['name']} ({off}, {c['count']} members, {len(distinctive)} distinctive/non-reused):")
        if not hits:
            print("    NO distinctive member found as a call anywhere in the corpus - no direct signal.")
            continue
        for n, files_ in sorted(hits, key=lambda kv: -len(kv[1])):
            sample = sorted(str(p) for p in files_)[:4]
            print(f"    {n:24s} in {len(files_):3d} file(s), e.g. {', '.join(sample)}")

    # ---- Report 7: project-wide local-variable receiver clustering ----
    print("\n" + "=" * 100)
    print("REPORT 7: PROJECT-WIDE LOCAL-VARIABLE RECEIVER CLUSTERING (var.Method() pattern)")
    print("(which unconfirmed class's vocabulary do the METHODS CALLED ON A GIVEN VARIABLE")
    print(" NAME cluster toward, aggregated across the whole corpus - weak per-instance signal,")
    print(" but variable names are often reused for the same conceptual role script to script)")
    print("=" * 100)
    for var, methods in sorted(var_methods_global.items(), key=lambda kv: -sum(kv[1].values())):
        total = sum(methods.values())
        if total < 5:
            continue
        call_set = set(methods.keys())
        scored = []
        for off, c in unconfirmed.items():
            d = dice(call_set, c["bindings"])
            inter, frac = score_overlap(call_set, c["bindings"])
            if inter > 0:
                scored.append((d, inter, frac, c["name"]))
        if not scored:
            continue
        scored.sort(reverse=True)
        print(f"\n{var}  [{total} call sites, {len(call_set)} distinct methods]")
        for d, inter, frac, name in scored[:2]:
            print(f"    dice={d:.2f}  {inter:3d}/{len(call_set)} overlap ({frac:.0%})  {name}")
        print(f"    methods seen: {', '.join(m for m, _ in methods.most_common(10))}")


if __name__ == "__main__":
    main()
