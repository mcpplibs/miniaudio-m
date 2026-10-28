# miniaudio

> miniaudio, audio playback, capture, decoding and mixing, through C++23 modules.

**English** · [简体中文](README.zh.md) · [Upstream](https://github.com/mackron/miniaudio) · [Releases](https://github.com/mcpplibs/miniaudio-m/releases) · [mcpp](https://github.com/mcpp-community/mcpp) · [Index](https://github.com/mcpplibs/mcpp-index)

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![miniaudio](https://img.shields.io/badge/miniaudio-MIT--0%20%7C%20Unlicense-blue.svg)](upstream/miniaudio/LICENSE)
[![mcpp-community](https://img.shields.io/badge/mcpp--community-mwrap-blue)](https://github.com/mcpp-community)
[![CI](https://github.com/mcpplibs/miniaudio-m/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/mcpplibs/miniaudio-m/actions/workflows/ci.yml)

miniaudio plays and captures audio on every platform's native backend
(WASAPI, DirectSound, WinMM, Core Audio, ALSA, PulseAudio, JACK, AAudio,
OpenSL, Web Audio …), decodes WAV, FLAC and MP3, encodes WAV, converts and
resamples, and mixes through a node graph and a high-level engine. It is one C
file under MIT No Attribution or the Unlicense, with no dependencies. The
upstream C API is unchanged: `ma_engine_init`, `ma_device_init`,
`ma_decoder_init_file` and `MA_SUCCESS` are the upstream globals of
`<miniaudio.h>`. The library is `compat.miniaudio` 0.11.25. This package maps
miniaudio's value macros to constants, each checked against the header, and
runs on Linux, macOS, Windows and the Web (wasm32-emscripten, the Web Audio
backend).

## Modules

- `import mcpplibs.mwrap.miniaudio;`: the whole API of `<miniaudio.h>` (context, device, engine, sound and sound group, node graph, resource manager, decoder, encoder, data sources, waveform and noise, converters, resamplers, filters, effects, ring buffers, logging), its enumerators, and the three inline helpers adapted in `mcpplibs::mwrap::miniaudio`; re-exports the module below
- `import mcpplibs.mwrap.miniaudio.macros;`: `MA_VERSION_STRING`, `MA_MAX_CHANNELS`, `MA_ENGINE_MAX_LISTENERS`, `MA_LISTENER_INDEX_CLOSEST` … in `mcpplibs::mwrap::miniaudio::macros`

> More Modules API -> [Usage and API](docs/usage.md)

## Getting started

```sh
mcpp new tone --template mcpplibs.mwrap.miniaudio@0.11.25
cd tone
mcpp run
```

The default template needs no audio device: it reads a 440 Hz tone from a
waveform data source, encodes it as WAV in memory, decodes it back and
measures it. `mcpp run -- --play` then plays it on the default device.

```toml
[dependencies]
"mcpplibs.mwrap.miniaudio" = "0.11.25"
```

```cpp
import std;
import mcpplibs.mwrap.miniaudio;

int main(int argc, char** argv) {
    ma_engine engine;
    if (argc < 2 || ma_engine_init(nullptr, &engine) != MA_SUCCESS) return 1;
    ma_engine_play_sound(&engine, argv[1], nullptr);  // WAV, FLAC or MP3
    std::println("playing {}; press Enter to quit", argv[1]);
    std::cin.get();
    ma_engine_uninit(&engine);
}
```

## Macros

Constants such as `MA_MAX_CHANNELS` keep the type their macro has (`int`),
`MA_LISTENER_INDEX_CLOSEST` is an `ma_uint8`, and the version string is a
`const char[]`. miniaudio's enums are named by their typedefs, so their
enumerators (`MA_SUCCESS`, `ma_format_f32`, `MA_SOUND_FLAG_DECODE` …) are
exported as they are. The compatibility names the header keeps as macros
(`ma_resource_manager_job` …) are aliases of what they name. Stringification,
attributes, `MA_API` and the platform and backend tests (`MA_WIN32`,
`MA_HAS_WASAPI` …) stay macros in `<mwrap/miniaudio/macros.hpp>`, included
before the import. Details: [docs/usage.md](docs/usage.md).

## Platforms

| Target | Backends (in priority order) | Notes |
|---|---|---|
| Linux | PulseAudio, ALSA, JACK, Null | loaded at run time: no -lasound, -lpulse |
| macOS | Core Audio, Null | frameworks loaded at run time |
| Windows | WASAPI, DirectSound, WinMM, Null | |
| wasm32-emscripten | Web Audio | no Null backend upstream (it needs a thread) |

The Custom backend is everywhere. On the Web, give the engine a resource
manager with `MA_RESOURCE_MANAGER_FLAG_NO_THREADING` unless the program is
built with threads.

## Examples

- [examples/miniaudio](examples/miniaudio): an audio file tool (`info`, `convert` WAV / FLAC / MP3 to WAV at
  any rate, channel count and format, `tone`, `play`, `devices`), written against the module;
  [tools/run_examples.py](tools/run_examples.py) runs it on every CI row.

## Build and testing

Linux (x86_64 GCC and LLVM, arm64 GCC), macOS and Windows CI build the
modules, run the tests (a device on the Null backend, the engine without a
device, WAV encoding and WAV / FLAC / MP3 decoding, converters, filters) and
the export audit ([tools/check_exports.py](tools/check_exports.py)), which
checks on each target, from the compiler's own preprocessing, that every
declaration of the header is exported and every macro mapped or listed. A
Web row builds for wasm32-emscripten and runs the same tests under node. A
separate job scaffolds the default template as an installed package and runs
it.

## License

[Apache-2.0](LICENSE) for the wrapper, by mcpp-community ([NOTICE](NOTICE)).
miniaudio is a choice of the Unlicense or MIT No Attribution
([upstream/miniaudio/LICENSE](upstream/miniaudio/LICENSE)); its README and
license are deployed beside every program that depends on this package.
