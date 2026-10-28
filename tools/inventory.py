#!/usr/bin/env python3
"""The public surface of miniaudio 0.11.25, from the pinned release.

Shared by tools/gen_exports.py (which writes the module sources and the
generated macro test) and tools/check_exports.py (which audits them). The
header comes from the upstream tag archive, downloaded once into
target/upstream and checked against upstream/documents.json: the same bytes
compat.miniaudio builds.

miniaudio.h is one file holding the API (between `#ifndef miniaudio_h` and
its `#endif`) and, behind MINIAUDIO_IMPLEMENTATION, the implementation. Only
the API section is read here, and the module includes the header without
MINIAUDIO_IMPLEMENTATION: compat.miniaudio compiles the implementation once,
from upstream's miniaudio.c.

Every declaration is recorded with the preprocessor conditions it is declared
under (the header's own `#if` nesting, as one expression), so the module can
export it under the same condition: miniaudio declares ma_pthread_t &c. only
on POSIX, ma_IMMNotificationClient only on Windows, the backend
configurations of ma_context per backend, and the engine, resource manager,
decoders and encoders unless their MA_NO_* switch is defined.
"""
from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path
import re
import sys
import tarfile
import urllib.request

API_BEGIN = '#ifndef miniaudio_h'
API_END = '#endif  /* miniaudio_h */'


def fetch(root: Path) -> Path:
    pins = json.loads((root / 'upstream/documents.json').read_text(encoding='utf-8'))
    cache = root / 'target/upstream'
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / ('miniaudio-' + pins['upstream_version'] + '.tar.gz')
    if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != pins['upstream_sha256']:
        print('+ fetch', pins['upstream'], flush=True, file=sys.stderr)
        with urllib.request.urlopen(pins['upstream'], timeout=120) as response:
            archive.write_bytes(response.read())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != pins['upstream_sha256']:
        sys.exit(f'upstream archive checksum {digest} != {pins["upstream_sha256"]}')
    with tarfile.open(archive) as tar:
        top = tar.getmembers()[0].name.split('/')[0]
        if not (cache / top / 'miniaudio.h').exists():
            tar.extractall(cache, members=[m for m in tar.getmembers()
                                           if m.name in (top, f'{top}/miniaudio.h')], filter='data')
    return cache / top


@dataclass
class Macro:
    name: str
    params: list[str] | None   # None: object-like
    bodies: list[str]          # one per #define of it (the header defines some per platform)


@dataclass
class Inline:
    """A `static MA_INLINE` function: internal linkage, adapted by the module."""
    name: str
    returns: str
    params: str                # the parameter list as declared
    args: list[str]            # the parameter names
    condition: tuple


@dataclass
class Surface:
    # name -> the conditions it is declared under (one per declaration; () is unconditional)
    types: dict[str, list[str]] = field(default_factory=dict)        # typedef names and struct tags
    enumerators: dict[str, list[str]] = field(default_factory=dict)
    enum_of: dict[str, str] = field(default_factory=dict)            # enumerator -> its enum's typedef
    functions: dict[str, list[str]] = field(default_factory=dict)    # MA_API
    inlines: list[Inline] = field(default_factory=list)
    macros: dict[str, Macro] = field(default_factory=dict)
    order: list[tuple[str, str]] = field(default_factory=list)       # (kind, name) in header order


def strip_comments(text: str) -> str:
    # Keep the newlines a comment spans, so line structure survives.
    text = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n') or ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def api_section(upstream: Path) -> str:
    text = (upstream / 'miniaudio.h').read_text(encoding='utf-8')
    begin = text.index(API_BEGIN)
    end = text.index(API_END, begin)
    # Without the include guard itself (`#ifndef miniaudio_h` / `#define
    # miniaudio_h` … `#endif`): it is defined by the time anything evaluates.
    body = text[begin + len(API_BEGIN):end]
    body = body.replace('#define miniaudio_h', '', 1)
    return strip_comments(body).replace('\\\n', ' ')


def directive_expr(kind: str, rest: str) -> str:
    rest = ' '.join(rest.split())
    if kind == 'ifdef':
        return f'defined({rest})'
    if kind == 'ifndef':
        return f'!defined({rest})'
    return rest


# The header's #if chains: frame id -> the condition of each branch (None: #else).
FRAMES: dict[int, list[str | None]] = {}

Path_ = tuple  # a condition: ((frame id, branch index), …), outermost first; () is unconditional


def lines_with_conditions(text: str):
    """Yield (code line, condition) for every non-directive line; and the
    macro definitions as ('#define', name, params, body, condition)."""
    FRAMES.clear()
    stack: list[int] = []   # the open #if chains
    for line in text.split('\n'):
        m = re.match(r'^[ \t]*#[ \t]*(\w+)(.*)$', line)
        if not m:
            yield ('code', line, condition(stack))
            continue
        kind, rest = m.group(1), m.group(2).strip()
        if kind in ('if', 'ifdef', 'ifndef'):
            FRAMES[len(FRAMES)] = [directive_expr(kind, rest)]
            stack.append(len(FRAMES) - 1)
        elif kind == 'elif':
            FRAMES[stack[-1]].append(directive_expr('if', rest))
        elif kind == 'else':
            FRAMES[stack[-1]].append(None)
        elif kind == 'endif':
            stack.pop()
        elif kind == 'define':
            d = re.match(r'(\w+)(\(([^)]*)\))?\s*(.*)$', rest)
            params = [p.strip() for p in d.group(3).split(',')] if d.group(2) else None
            yield ('define', d.group(1), params, ' '.join(d.group(4).split()), condition(stack))
    if stack:
        sys.exit('unbalanced conditionals in the API section')


def condition(stack: list[int]) -> Path_:
    return tuple((frame, len(FRAMES[frame]) - 1) for frame in stack)


def branch_expr(frame: int, branch: int) -> str:
    """The condition of one branch of an #if/#elif/#else chain."""
    branches = FRAMES[frame]
    parts = [f'!({b})' for b in branches[:branch]]
    if branches[branch] is not None:
        parts.append(f'({branches[branch]})' if parts else branches[branch])
    return ' && '.join(parts)


def render(path: Path_) -> str:
    exprs = [branch_expr(f, b) for f, b in path]
    return exprs[0] if len(exprs) == 1 else ' && '.join(f'({e})' for e in exprs)


def simplify(paths: list[Path_]) -> list[Path_]:
    """The conditions a name is declared under, with every chain whose
    branches (an #else among them) all declare it removed: `ma_int64` is
    declared in each branch of `#if defined(MA_USE_STDINT) … #else … #endif`,
    so it is declared whatever that chain selects."""
    paths = set(paths)
    changed = True
    while changed:
        changed = False
        for p in sorted(paths, key=len, reverse=True):
            if not p:
                continue
            frame = p[-1][0]
            total = FRAMES[frame]
            siblings = {q for q in paths if q[:-1] == p[:-1] and q and q[-1][0] == frame}
            if None in total and {q[-1][1] for q in siblings} == set(range(len(total))):
                paths = (paths - siblings) | {p[:-1]}
                changed = True
                break
    if () in paths:
        return [()]
    return sorted(paths)


def condition_text(paths: list[Path_]) -> str:
    """One preprocessor expression for the conditions ('' if unconditional)."""
    paths = simplify(paths)
    if paths == [()]:
        return ''
    return render(paths[0]) if len(paths) == 1 else ' || '.join(f'({render(p)})' for p in paths)


def surface(upstream: Path) -> Surface:
    s = Surface()
    code: list[tuple[str, str]] = []
    for item in lines_with_conditions(api_section(upstream)):
        if item[0] == 'define':
            _, name, params, body, _cond = item
            if name in s.macros:
                s.macros[name].bodies.append(body)
            else:
                s.macros[name] = Macro(name, params, [body])
        else:
            code.append((item[1], item[2]))

    # C++ only: the extern "C" brackets are in `#ifdef __cplusplus` blocks.
    code = [(line, cond) for line, cond in code if 'defined(__cplusplus)' not in render(cond)]

    # Statements at brace depth 0, each with the condition of its first line.
    def add(kind: str, table: dict[str, list[str]], name: str, cond: str):
        if name not in table:
            s.order.append((kind, name))
        table.setdefault(name, []).append(cond)

    statement, start_cond, depth = '', None, 0
    for line, cond in code:
        for ch in line + '\n':
            if not statement.strip() and not ch.isspace():
                start_cond = cond
            statement += ch
            if ch == '{':
                depth += 1
            elif ch == '}':
                depth -= 1
            elif ch == ';' and depth == 0:
                declaration(s, add, ' '.join(statement.split()), start_cond, statement)
                statement, start_cond = '', None
            elif ch == ')' and depth == 0 and re.fullmatch(r'\s*MA_ATOMIC_SAFE_TYPE_DECL(_PTR)?\s*\([^()]*\)\s*', statement):
                # The atomic wrappers are declared by a macro (no `;` after it).
                m = re.match(r'\s*MA_ATOMIC_SAFE_TYPE_DECL(_PTR)?\s*\(([^()]*)\)', statement)
                args = [a.strip() for a in m.group(2).split(',')]
                add('type', s.types, ('ma_atomic_ptr_' if m.group(1) else 'ma_atomic_') + args[-1], start_cond)
                statement, start_cond = '', None
        # A static inline function definition ends at its closing brace.
        if depth == 0 and re.match(r'\s*static\s+MA_INLINE\b', statement) and statement.rstrip().endswith('}'):
            inline(s, ' '.join(statement.split()), start_cond)
            statement, start_cond = '', None
    if statement.strip():
        sys.exit('trailing text in the API section: ' + ' '.join(statement.split())[:200])
    return s


def inline(s: Surface, text: str, cond: str):
    m = re.match(r'static MA_INLINE (.+?)\b(ma_\w+)\s*\((.*?)\)\s*\{', text)
    if not m:
        sys.exit('a static inline definition the inventory does not know: ' + text[:200])
    params = m.group(3).strip()
    args = [re.search(r'(\w+)\s*$', p).group(1) for p in params.split(',')] if params and params != 'void' else []
    s.inlines.append(Inline(m.group(2), m.group(1).strip(), params, args, cond))
    s.order.append(('inline', m.group(2)))


def declaration(s: Surface, add, text: str, cond: str, raw: str):
    if text in (';', ''):
        return
    if text.startswith('MA_API '):
        f = re.match(r'MA_API [^(]*?\b(\w+)\s*\(', text)
        if not f:
            sys.exit('MA_API on something that is not a function: ' + text[:200])
        add('function', s.functions, f.group(1), cond)
        return
    m = re.match(r'typedef enum\s*(\w+)?\s*\{(.*)\}\s*(\w+)\s*;$', text)
    if m:
        add('type', s.types, m.group(3), cond)
        if m.group(1):
            add('type', s.types, m.group(1), cond)
        # Enumerators, each under the enum's condition and any inside it.
        body = raw[raw.index('{') + 1:raw.rindex('}')]
        for e in re.findall(r'(?:^|[,{\s])([A-Za-z_]\w*)\s*(?:=[^,]*)?(?=,|\s*$)', body, flags=re.M):
            add('enumerator', s.enumerators, e, cond)
            s.enum_of[e] = m.group(3)
        return
    m = re.match(r'typedef (struct|union)\s*(\w+)?\s*\{.*\}\s*(\w+(?:\s*,\s*\w+)*)\s*;$', text)
    if m:
        if m.group(2):
            add('type', s.types, m.group(2), cond)
        for name in m.group(3).split(','):  # `} ma_lpf1_config, ma_lpf2_config;`
            add('type', s.types, name.strip(), cond)
        return
    m = re.match(r'typedef (struct|union) (\w+) (\w+);$', text)
    if m:
        add('type', s.types, m.group(2), cond)
        add('type', s.types, m.group(3), cond)
        return
    m = re.match(r'(struct|union) (\w+)\s*\{.*\}\s*;$', text)
    if m:
        add('type', s.types, m.group(2), cond)
        return
    m = re.match(r'typedef [^;(]*\(\s*\*\s*(\w+)\s*\)\s*\(.*\)\s*;$', text)
    if m:
        add('type', s.types, m.group(1), cond)
        return
    m = re.match(r'typedef [\w\s\*]+?\b(\w+)\s*;$', text)
    if m:
        add('type', s.types, m.group(1), cond)
        return
    m = re.match(r'typedef [\w\s]+?\b(\w+)\s*\[\s*\w+\s*\]\s*;$', text)
    if m:
        add('type', s.types, m.group(1), cond)
        return
    sys.exit('a declaration the inventory does not know: ' + text[:300])


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    sf = surface(fetch(root))
    conditional = lambda table: sum(1 for c in table.values() if condition_text(c))  # noqa: E731
    print(f'{len(sf.types)} types ({conditional(sf.types)} conditional), '
          f'{len(sf.enumerators)} enumerators ({conditional(sf.enumerators)} conditional), '
          f'{len(sf.functions)} functions ({conditional(sf.functions)} conditional), '
          f'{len(sf.inlines)} static inline functions, {len(sf.macros)} macros')
    for kind, table in (('type', sf.types), ('enumerator', sf.enumerators), ('function', sf.functions)):
        for name, conds in table.items():
            if condition_text(conds):
                print(f'  {kind} {name}: {condition_text(conds)}')
