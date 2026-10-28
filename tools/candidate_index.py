#!/usr/bin/env python3
"""The compat namespace from mcpp-index's candidate branch, while the published
index does not yet carry compat.miniaudio's gnu11 build.

compat.miniaudio 0.11.25 is on the published index, compiled at c11 — which
Emscripten refuses for miniaudio's Web Audio backend (EM_ASM needs a GNU C
dialect). mcpplibs/mcpp-index#609 compiles it at gnu11. Until that descriptor
is on the index's main branch, this tool clones the PR's branch, prepares the
unchanged namespace views of its descriptors (the index's own
tests/prepare_mwrap_indices.py) and prints the checkout.

  --checkout-only   print the candidate checkout (nothing once published),
                    for tools/test_template.py --index
  --ci              also append `[indices] compat = { path = <view> }` to this
                    checkout's manifests (the root and examples/miniaudio), in
                    CI's working copy only

Once the published index has the gnu11 descriptor, it does nothing.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import urllib.error
import urllib.request

INDEX = 'https://github.com/mcpplibs/mcpp-index'
BRANCH = 'compat-miniaudio-web'
PUBLISHED = 'https://raw.githubusercontent.com/mcpplibs/mcpp-index/main/pkgs/c/compat.miniaudio.lua'
MANIFESTS = ['mcpp.toml', 'examples/miniaudio/mcpp.toml']


def published() -> bool:
    try:
        with urllib.request.urlopen(PUBLISHED, timeout=60) as response:
            return 'c_standard   = "gnu11"' in response.read().decode('utf-8')
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return False
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--checkout-only', action='store_true')
    mode.add_argument('--ci', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if published():
        print('compat.miniaudio (gnu11) is published: the published index is used', file=sys.stderr)
        return
    checkout = root / 'target/candidate-index'
    if not (checkout / 'index.toml').exists():
        subprocess.run(['git', 'clone', '--quiet', '--depth', '1', '--branch', BRANCH, INDEX + '.git', str(checkout)],
                       check=True, stdout=sys.stderr)
    subprocess.run([sys.executable, str(checkout / 'tests/prepare_mwrap_indices.py')], cwd=checkout, check=True,
                   stdout=sys.stderr)
    view = checkout / 'tests/mwrap/.indices/compat'
    if not (view / 'pkgs/view/compat.miniaudio.lua').exists():
        sys.exit(f'{INDEX} {BRANCH} has no compat.miniaudio')
    if args.ci:
        for manifest in MANIFESTS:
            with (root / manifest).open('a', encoding='utf-8', newline='\n') as out:
                out.write('\n# CI only (tools/candidate_index.py): compat.miniaudio (gnu11) from ' + BRANCH + '\n')
                out.write('[indices]\ncompat = { path = "' + view.as_posix() + '" }\n')
            print('candidate compat index for', manifest, file=sys.stderr)
    print(checkout.as_posix())


if __name__ == '__main__':
    main()
