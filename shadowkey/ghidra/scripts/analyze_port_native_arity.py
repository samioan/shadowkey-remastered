"""Arity audit: does a shipped script ever call an implemented native with
an argument count the port's guard rejects?

The fourth measure, after `analyze_port_native_gaps.py` (what the port does
not implement at all) and `analyze_port_native_coverage.py` (what a call
with an explicit receiver lands on). Both of those are **name-only**, and
the soft-fail log only sees what a test actually exercises -- so a native
implemented as `args.entries() == 1` that a shipped script calls with two
arguments falls straight through the chain, silently, unless that exact
line runs under a test.

Written for M106, which is what it found: the popup class's
`SetSelectable(idx, flag)`. The port matched the name but had only the
one-argument widget form in mind, no-opped the two-argument call, and
derived selectability from "has a callback" instead -- which `buysell.s`
breaks by building one popup with three callbacks and reusing it as a
message box.

Method:
  port side  -- for every `skString("Name")` literal in port/src/simkin_bindings,
                take the whole enclosing `if (...)` condition and collect every
                `args.entries() <op> N` in it. Union over all occurrences = the
                arities the port accepts for that name.
  script side -- every `Name(...)` call in the 1,535 shipped .s files, counting
                top-level commas (strings and nested parens respected), with
                `//` comments stripped first.

Usage:
    python shadowkey/ghidra/scripts/analyze_port_native_arity.py [repo-root]

Caveats -- read every row before believing it:
  * **Receivers are pooled**, the same blind spot the gaps tool documents.
    A name handled with one arity on class A and another on class B looks
    like a mismatch on whichever class the example call site belongs to.
    Four of the five rows this printed on first run were exactly that
    (`OpenDoor` on Door vs Trigger, `GetItemDescription` on Player vs Item,
    ...). Confirm the receiver before acting.
  * A guard written as a **rejection** in the handler body
    (`if (args.entries() != 2) return false;`) rather than in the `if`
    condition is read inverted here; `Random` shows up for that reason.
  * A handler with no `args.entries()` test at all accepts everything and
    is skipped rather than reported.
"""
import os, re, sys, collections

REPO = sys.argv[1] if len(sys.argv) > 1 else "."
SRC = os.path.join(REPO, "port", "src", "simkin_bindings")
SCRIPTS = os.path.join(
    REPO,
    "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-EnFrDeEsIt-26102004",
    "system", "apps", "6r51")

# ---------- port side ----------

def enclosing_condition(text, pos):
    """Expand from a skString(...) at `pos` out to the enclosing if(...)."""
    # walk backwards to an unmatched '('
    depth = 0
    i = pos
    while i > 0:
        c = text[i]
        if c == ')':
            depth += 1
        elif c == '(':
            if depth == 0:
                break
            depth -= 1
        i -= 1
    start = i
    # confirm it is an `if`
    head = text[max(0, start - 8):start]
    if not re.search(r'\b(if|while)\s*$', head):
        return None
    # forward to the matching ')'
    depth = 0
    j = start
    while j < len(text):
        c = text[j]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                break
        j += 1
    return text[start:j + 1]


accepts = collections.defaultdict(set)   # name -> set of ints
unguarded = set()                        # name seen with no entries() guard

for root, _dirs, files in os.walk(SRC):
    for fn in files:
        if not fn.endswith(('.cpp', '.h')):
            continue
        text = open(os.path.join(root, fn), encoding='utf-8', errors='replace').read()
        # strip // comments so commented-out code doesn't count
        text = re.sub(r'//[^\n]*', '', text)
        for m in re.finditer(r'skString\("([A-Za-z_][A-Za-z0-9_]*)"\)', text):
            name = m.group(1)
            cond = enclosing_condition(text, m.start())
            if cond is None:
                unguarded.add(name)
                continue
            found = re.findall(r'args\.entries\(\)\s*(==|>=|<=|!=|>|<)\s*(\d+)', cond)
            if not found:
                unguarded.add(name)
                continue
            for op, num in found:
                n = int(num)
                if op == '==':
                    accepts[name].add(n)
                elif op == '>=':
                    accepts[name].update(range(n, 9))
                elif op == '>':
                    accepts[name].update(range(n + 1, 9))
                elif op == '<=':
                    accepts[name].update(range(0, n + 1))
                elif op == '<':
                    accepts[name].update(range(0, n))
                elif op == '!=':
                    accepts[name].update(x for x in range(0, 9) if x != n)

# ---------- script side ----------

def strip_comments(s):
    out = []
    i = 0
    while i < len(s):
        if s[i] == '"':
            j = i + 1
            while j < len(s) and s[j] != '"':
                j += 1
            out.append(s[i:j + 1])
            i = j + 1
        elif s.startswith('//', i):
            while i < len(s) and s[i] != '\n':
                i += 1
        else:
            out.append(s[i])
            i += 1
    return ''.join(out)


def arg_count(s, open_paren):
    """Count arguments of the call whose '(' is at open_paren."""
    depth = 0
    i = open_paren
    commas = 0
    body = []
    while i < len(s):
        c = s[i]
        if c == '"':
            j = i + 1
            while j < len(s) and s[j] != '"':
                j += 1
            body.append(s[i:j + 1])
            i = j + 1
            continue
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                inner = ''.join(body[1:])
                if inner.strip() == '':
                    return 0
                return commas + 1
        elif c == ',' and depth == 1:
            commas += 1
        body.append(c)
        i += 1
    return None


calls = collections.defaultdict(collections.Counter)   # name -> {argc: count}
examples = {}                                          # (name, argc) -> path:line

for root, _dirs, files in os.walk(SCRIPTS):
    for fn in files:
        if not fn.endswith('.s'):
            continue
        path = os.path.join(root, fn)
        raw = open(path, encoding='utf-8', errors='replace').read()
        text = strip_comments(raw)
        for m in re.finditer(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\(', text):
            name = m.group(1)
            if name in ('if', 'while', 'for', 'return', 'switch'):
                continue
            n = arg_count(text, m.end() - 1)
            if n is None:
                continue
            calls[name][n] += 1
            key = (name, n)
            if key not in examples:
                line = text[:m.start()].count('\n') + 1
                examples[key] = '%s:%d' % (os.path.relpath(path, SCRIPTS).replace('\\', '/'), line)

# ---------- compare ----------

print("port names with an explicit entries() guard: %d" % len(accepts))
print("names whose guard could not be read (skipped): %d" % len(unguarded - set(accepts)))
print()
print("==== implemented, but a shipped script calls it with an arity the port rejects")
rows = []
for name, ok in sorted(accepts.items()):
    if name not in calls:
        continue
    for argc, n in sorted(calls[name].items()):
        if argc not in ok:
            rows.append((n, name, argc, sorted(ok), examples[(name, argc)]))
rows.sort(reverse=True)
if not rows:
    print("  (none)")
for n, name, argc, ok, ex in rows:
    print("  %4d call(s)  %-26s script passes %d, port accepts %s   e.g. %s"
          % (n, name, argc, ok, ex))
