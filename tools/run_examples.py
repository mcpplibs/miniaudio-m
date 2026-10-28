#!/usr/bin/env python3
"""Build examples/miniaudio, an audio file tool written against the module, and
check what it does on this target without an audio device: a tone written as
WAV, the decoder fixtures of tests/data (WAV, FLAC, MP3) measured, and each
converted to WAV at another rate, channel count and sample format, then
measured again. `devices` and `play` run on the default backend, which on a
CI machine is the Null backend.

  --target <triple>   build and run for that target (wasm32-emscripten: under node)
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mcpp', default='mcpp')
    parser.add_argument('--target')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    example = root / 'examples/miniaudio'
    target = ['--target', args.target] if args.target else []
    subprocess.run([args.mcpp, 'build', *target], cwd=example, check=True)

    def run(*argv: str) -> str:
        out = subprocess.run([args.mcpp, 'run', *target, '--', *argv], cwd=example, check=True,
                             capture_output=True, text=True)
        print('$ miniaudio', ' '.join(argv), '\n' + out.stdout.rstrip(), flush=True)
        return out.stdout

    def measured(line: str) -> dict:
        m = re.search(r': (\d+) ch, (\d+) Hz, (\d+) frames \(([\d.]+) s\), peak ([\d.]+), rms ([\d.]+)', line)
        assert m, line
        return dict(channels=int(m[1]), rate=int(m[2]), frames=int(m[3]), peak=float(m[5]), rms=float(m[6]))

    data = root / 'tests/data'
    with tempfile.TemporaryDirectory(prefix='miniaudio-examples-') as tmp:
        tone = Path(tmp) / 'tone.wav'
        run('tone', str(tone), '--freq', '1000', '--seconds', '0.5', '--rate', '48000')
        t = measured(run('info', str(tone)))
        assert (t['channels'], t['rate'], t['frames']) == (1, 48000, 24000), t
        assert abs(t['peak'] - 0.5) < 1e-3 and abs(t['rms'] - 0.5 / 2 ** 0.5) < 2e-3, t
        for name in ('tone.wav', 'tone.flac', 'tone.mp3'):
            src = measured(run('info', str(data / name)))
            assert (src['channels'], src['rate']) == (1, 44100), src
            out = Path(tmp) / (name + '.wav')
            run('convert', str(data / name), str(out), '--rate', '48000', '--channels', '2', '--format', 'f32')
            dst = measured(run('info', str(out)))
            assert (dst['channels'], dst['rate']) == (2, 48000), dst
            assert abs(dst['frames'] - src['frames'] * 48000 / 44100) <= 2, (src, dst)
            assert abs(dst['rms'] - src['rms']) < 0.01, (src, dst)
        # The default backend's devices and playback: reported, not required —
        # whether a CI machine has an audio endpoint is the machine's business.
        if not args.target or 'emscripten' not in args.target:
            for argv in (['devices'], ['play', str(tone), '--seconds', '0.2']):
                out = subprocess.run([args.mcpp, 'run', *target, '--', *argv], cwd=example,
                                     capture_output=True, text=True)
                print('$ miniaudio', ' '.join(argv), f'(exit {out.returncode})\n' + (out.stdout + out.stderr).rstrip())
    print('examples ok')


if __name__ == '__main__':
    sys.exit(main())
