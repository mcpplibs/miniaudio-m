#!/usr/bin/env python3
"""Audit the module against miniaudio 0.11.25's miniaudio.h, on this machine's target.

1. The generated sources are what tools/gen_exports.py writes from the
   pinned header (src/miniaudio.cppm, src/inline.cpp, src/macros.cppm,
   tests/macros_mix_test.cpp).
2. The raw header, every #if branch read: each declaration of the API
   section is exported (or listed in NOT_EXPORTED with its reason), each
   `static MA_INLINE` function adapted, each macro mapped, kept as a macro in
   <mwrap/miniaudio/macros.hpp> with its reason, or listed as not mapped; and
   every mapping is checked against the header by tests/macros_mix_test.cpp.
3. This target, as the compiler sees it: the header is preprocessed with the
   module's own compile command (from compile_commands.json, so run `mcpp
   build` first), and so are the module sources. Every function, type, struct
   tag and enumerator the header declares here must be exported here, and
   every macro it leaves defined here must be mapped here or listed. Run on
   every CI row: POSIX declares the pthread types, Windows
   ma_IMMNotificationClient, Emscripten no Null backend.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from inventory import fetch, surface  # noqa: E402
import gen_exports  # noqa: E402


def compile_command(root: Path) -> list[str]:
    # The configuration databases mcpp keeps in target/<triple>/<fingerprint>/:
    # the newest one is the last `mcpp build`'s.
    found = sorted(list(root.glob('target/*/*/compile_commands.json')) + list(root.glob('compile_commands.json')),
                   key=lambda p: p.stat().st_mtime, reverse=True)
    norm = lambda p: os.path.normcase(str(Path(p).resolve()))  # noqa: E731
    module = norm(root / 'src/miniaudio.cppm')
    for db in found:
        entries = [e for e in json.loads(db.read_text(encoding='utf-8'))
                   if norm(Path(e['directory'], e['file'])) == module]
        if entries:
            entry = entries[0]
            break
    else:
        sys.exit('no compile command for src/miniaudio.cppm (target/*/*/compile_commands.json): run `mcpp build` first')
    args = entry.get('arguments') or shlex.split(entry['command'], posix=os.name != 'nt')
    out, skip = [], False
    for a in args:
        if skip:
            skip = False
            continue
        if a in ('-c', '-o', '-x', '-MF', '-MT', '-MQ'):
            skip = a != '-c'
            continue
        if a == entry['file'] or a.endswith('miniaudio.cppm') or a.startswith(
                ('-fmodule', '-fprebuilt-module-path', '-fmodules', '-fdeps-', '--precompile', '-MD', '-MMD')):
            continue
        out.append(a)
    return out


def preprocess(cmd: list[str], source: str, work: Path, name: str) -> str:
    work.mkdir(parents=True, exist_ok=True)
    path = work / name
    path.write_text(source, encoding='utf-8')
    run = subprocess.run(cmd + ['-x', 'c++', '-E', '-dD', str(path)],
                         capture_output=True, text=True, encoding='utf-8', errors='replace')
    if run.returncode != 0:
        sys.exit(f'preprocessing {name} failed:\n{run.stderr[-4000:]}')
    return run.stdout


def from_miniaudio_h(text: str) -> tuple[str, set[str]]:
    """The code miniaudio.h contributes and the macros it leaves defined, from -E -dD output."""
    code, macros, current = [], set(), False
    for line in text.splitlines():
        m = re.match(r'#\s*(?:line\s+)?\d+\s+"([^"]*)"', line)
        if m:
            current = m.group(1).replace('\\\\', '/').replace('\\', '/').endswith('/miniaudio.h')
            continue
        d = re.match(r'#\s*(define|undef)\s+(\w+)', line)
        if d:
            if d.group(1) == 'define' and current:
                macros.add(d.group(2))
            elif d.group(1) == 'undef':
                macros.discard(d.group(2))
            continue
        if line.startswith('#'):
            continue
        if current:
            code.append(line)
    return '\n'.join(code), macros


def module_text(path: Path) -> str:
    """A module source without its module and import declarations, to preprocess."""
    return '\n'.join(line for line in path.read_text(encoding='utf-8').splitlines()
                     if not re.match(r'\s*(export\s+)?(module|import)\b[^;]*;\s*$', line)) + '\n'


def declared(code: str) -> tuple[set[str], set[str]]:
    """(exportable names, static inline functions) of preprocessed C declarations."""
    names, inlines = set(), set()
    # The C++ view of the header is inside `extern "C" { … }`: read it as C.
    code = re.sub(r'extern\s+"C"\s*\{', ' ', code)
    statement, depth = '', 0
    for ch in code:
        if ch == '}' and depth == 0:
            continue  # the extern "C" block's closing brace
        statement += ch
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0 and re.match(r'\s*static\b', statement) and '(' in statement.split('{', 1)[0]:
                head = statement.split('{', 1)[0]
                inlines.add(re.findall(r'\b(\w+)\s*\(', head)[-1])
                statement = ''
        elif ch == ';' and depth == 0:
            text = ' '.join(statement.split())
            statement = ''
            if not text or text == ';' or text.startswith(('extern "C"', 'namespace ')):
                continue
            text = re.sub(r'__attribute__\s*\(\(.*?\)\)\s*', '', text)
            text = re.sub(r'__declspec\s*\(\w+\)\s*', '', text)
            if text.startswith('extern ') and '(' in text:
                names.add(re.match(r'extern [^(]*?\b(\w+)\s*\(', text).group(1))
                continue
            if not text.startswith(('typedef ', 'struct ', 'union ')):
                sys.exit('a preprocessed declaration this audit does not know: ' + text[:200])
            for tag in re.findall(r'^(?:typedef\s+)?(?:struct|union|enum)\s+(\w+)', text):
                names.add(tag)
            if '{' in text:
                if re.match(r'typedef enum\b', text):
                    body = text[text.index('{') + 1:text.rindex('}')]
                    names.update(re.findall(r'(?:^|,)\s*([A-Za-z_]\w*)', body))
                tail = text[text.rindex('}') + 1:]
            else:
                tail = text
            if text.startswith('typedef'):
                fp = re.search(r'\(\s*\*\s*(\w+)\s*\)\s*\(', tail)
                if fp:
                    names.add(fp.group(1))
                else:
                    for decl in tail.rstrip(';').split(','):
                        m = re.search(r'(\w+)\s*(?:\[[^\]]*\])?\s*$', decl)
                        if m:
                            names.add(m.group(1))
    names.discard('typedef')
    return names, inlines


def main():
    root = Path(__file__).resolve().parents[1]
    problems = []

    # 1. generated sources
    generated = subprocess.run([sys.executable, str(root / 'tools/gen_exports.py'), '--check'],
                               capture_output=True, text=True)
    if generated.returncode != 0:
        problems.append(generated.stderr.strip() or generated.stdout.strip())

    # 2. the raw header, every branch
    sf = surface(fetch(root))
    module = (root / 'src/miniaudio.cppm').read_text(encoding='utf-8')
    exported = set(re.findall(r'^export using ::(\w+);', module, flags=re.M))
    raw = (set(sf.types) | set(sf.enumerators) | set(sf.functions)) - gen_exports.NOT_EXPORTED.keys()
    if raw - exported:
        problems.append('declared but not exported: ' + ', '.join(sorted(raw - exported)))
    if exported - raw:
        problems.append('exported but not declared: ' + ', '.join(sorted(exported - raw)))
    adapted = set(re.findall(r'^[\w\s\*]+?\b(ma_\w+)\(', module.split('export namespace mcpplibs::mwrap::miniaudio {')[1],
                             flags=re.M))
    if adapted != {f.name for f in sf.inlines}:
        problems.append(f'static inline functions {sorted(f.name for f in sf.inlines)} != adapted {sorted(adapted)}')
    constants, alias_types, alias_functions = gen_exports.classify(sf)
    mapped = set(constants) | set(alias_types) | set(alias_functions)
    listed = gen_exports.SIDE_HEADER.keys() | gen_exports.NOT_MAPPED.keys()
    if sf.macros.keys() - mapped - listed:
        problems.append('macros neither mapped nor listed: ' + ', '.join(sorted(sf.macros.keys() - mapped - listed)))
    if listed - sf.macros.keys():
        problems.append('listed but not in the header: ' + ', '.join(sorted(listed - sf.macros.keys())))
    test = (root / 'tests/macros_mix_test.cpp').read_text(encoding='utf-8')
    untested = {n for n in mapped if f'm::{n}' not in test}
    if untested:
        problems.append('mapped but not checked against the header: ' + ', '.join(sorted(untested)))
    side = (root / 'include/mwrap/miniaudio/macros.hpp').read_text(encoding='utf-8')
    if '#include <miniaudio.h>' not in side:
        problems.append('<mwrap/miniaudio/macros.hpp> does not include <miniaudio.h>')

    # 3. this target
    cmd = compile_command(root)
    work = root / 'target/check-exports'
    header_code, header_macros = from_miniaudio_h(preprocess(cmd, '#include <miniaudio.h>\n', work, 'header.cpp'))
    here, here_inline = declared(header_code)
    here -= gen_exports.NOT_EXPORTED.keys()
    module_pp = preprocess(cmd, module_text(root / 'src/miniaudio.cppm'), work, 'module.cpp')
    exported_here = set(re.findall(r'^\s*export using ::(\w+);', module_pp, flags=re.M))
    if here - exported_here:
        problems.append('declared on this target but not exported here: ' + ', '.join(sorted(here - exported_here)))
    if exported_here - here:
        problems.append('exported here but not declared on this target: ' + ', '.join(sorted(exported_here - here)))
    if here_inline != adapted:
        problems.append(f'static inline functions on this target {sorted(here_inline)} != adapted {sorted(adapted)}')
    macros_pp = preprocess(cmd, module_text(root / 'src/macros.cppm'), work, 'macros.cpp')
    defined_here = set(re.findall(r'^inline constexpr auto&? (\w+) = c::value_\1;', macros_pp, flags=re.M))
    defined_here |= set(re.findall(r'^using (\w+) = c::type_\1;', macros_pp, flags=re.M))
    header_macros.discard('miniaudio_h')
    unaccounted = header_macros - mapped - listed
    if unaccounted:
        problems.append('macros defined on this target neither mapped nor listed: ' + ', '.join(sorted(unaccounted)))
    missing = (header_macros & mapped) - defined_here
    if missing:
        problems.append('macros defined on this target but not mapped here: ' + ', '.join(sorted(missing)))

    if problems:
        sys.exit('\n'.join(problems))
    print(f'{len(sf.types)} types and struct tags, {len(sf.enumerators)} enumerators and {len(sf.functions)} MA_API '
          f'functions in the header (every branch), each exported under its condition; {len(sf.inlines)} static '
          f'inline functions adapted; {len(sf.macros)} macros: {len(constants)} constants, {len(alias_types)} type '
          f'and {len(alias_functions)} function compatibility names mapped (each checked against the header), '
          f'{len(gen_exports.SIDE_HEADER)} kept as macros, {len(gen_exports.NOT_MAPPED)} not mapped. On this target: '
          f'{len(here)} declarations, all exported; {len(header_macros)} macros, all accounted for '
          f'({len(header_macros & mapped)} mapped).')


if __name__ == '__main__':
    main()
