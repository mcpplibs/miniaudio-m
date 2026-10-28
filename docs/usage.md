# Using mcpplibs.mwrap.miniaudio

`import mcpplibs.mwrap.miniaudio;` declares miniaudio 0.11.25's API as `<miniaudio.h>` does. Types, struct tags, enumerators and functions are the upstream globals, the very same entities as the header's, so a translation unit may include the header too. The library is `compat.miniaudio` 0.11.25, which compiles upstream's `miniaudio.c` (the header with `MINIAUDIO_IMPLEMENTATION`) once; the module includes the header without it, so it declares and never defines.

## Modules

| Module | What it holds |
|---|---|
| `mcpplibs.mwrap.miniaudio` | every type and struct tag of the API section of `<miniaudio.h>` (266: `ma_context`, `ma_device`, `ma_engine`, `ma_sound`, `ma_node_graph`, `ma_resource_manager`, `ma_decoder`, `ma_encoder`, `ma_data_source`, `ma_waveform`, `ma_noise`, `ma_data_converter`, `ma_resampler`, `ma_lpf` …, the configurations `ma_*_config`, the callback types, the atomic wrappers `ma_atomic_uint32` …), the enumerators of its 48 enums (368: `MA_SUCCESS` … `MA_UNAVAILABLE`, `ma_format_*`, `ma_backend_*`, `MA_SOUND_FLAG_*` …), all 931 `MA_API` functions; the 3 adapted inline functions in `mcpplibs::mwrap::miniaudio`; re-exports the module below |
| `mcpplibs.mwrap.miniaudio.macros` | the value macros and compatibility names, in `mcpplibs::mwrap::miniaudio::macros` |

```cpp
import std;
import mcpplibs.mwrap.miniaudio;
using namespace mcpplibs::mwrap::miniaudio;          // ma_get_bytes_per_frame … (the adapted inline functions)
using namespace mcpplibs::mwrap::miniaudio::macros;  // MA_MAX_CHANNELS, MA_VERSION_STRING …
```

Either directive may be left out when its names are not used; the C API itself needs neither.

Every declaration is exported under the preprocessor condition the header declares it under, evaluated in the module as in the header: `ma_pthread_t`, `ma_pthread_mutex_t` and `ma_pthread_cond_t` on POSIX targets, `ma_IMMNotificationClient` where WASAPI is supported (Windows). The engine, node graph, resource manager, decoders, encoders, generators and device API are all there, as compat.miniaudio defines none of the `MA_NO_*` switches. `tools/check_exports.py` checks this on every CI row from the compiler's own preprocessing of the header.

### The API in one paragraph

The **engine** (`ma_engine_init`, `ma_engine_play_sound`, `ma_sound_init_from_file`, `ma_sound_group_init`) is the high-level API: it owns a device (or none, `noDevice`, and then `ma_engine_read_pcm_frames` mixes into the caller's buffer), a node graph and a resource manager that loads and decodes files. The **low-level API** is a `ma_context` (one backend) and its `ma_device`s, whose `dataCallback` the backend calls with frames to fill or read. Between them are **data sources** (`ma_decoder`, `ma_audio_buffer`, `ma_waveform`, `ma_noise`, `ma_paged_audio_buffer`, the resource manager's), all readable through `ma_data_source_read_pcm_frames`, and the **DSP** (`ma_data_converter`, `ma_resampler`, `ma_channel_converter`, the biquad, low/high/band-pass, shelf, notch and peaking filters, `ma_delay`, `ma_gainer`, `ma_panner`, `ma_spatializer`), each also a node (`ma_lpf_node` …). `ma_encoder` writes WAV.

## The inline functions

`ma_get_bytes_per_frame`, `ma_offset_pcm_frames_ptr_f32` and `ma_offset_pcm_frames_const_ptr_f32` are `static MA_INLINE` upstream: internal linkage, not exportable. They are adapted in `mcpplibs::mwrap::miniaudio` under the same names and signatures, each defined in `src/inline.cpp` by calling upstream's. With the header included as well, the header's functions are `::ma_get_bytes_per_frame` …, and a `using namespace mcpplibs::mwrap::miniaudio;` would make an unqualified call ambiguous: qualify one of them (see [tests/header_mix_test.cpp](../tests/header_mix_test.cpp)).

## Macros

A named module cannot export a macro. `mcpplibs::mwrap::miniaudio::macros` has them under their original names:

- **Constants** (27) as `inline constexpr` values of the macro's own type: `MA_VERSION_MAJOR`, `MA_VERSION_MINOR`, `MA_VERSION_REVISION` (`int`), `MA_VERSION_STRING` (`const char[8]`), `MA_TRUE`, `MA_FALSE`, `MA_SIZEOF_PTR`, `MA_SIZE_MAX`, `MA_UINT64_MAX`, `MA_SIMD_ALIGNMENT`, `MA_MIN_CHANNELS`, `MA_MAX_CHANNELS`, `MA_MAX_FILTER_ORDER`, `MA_MAX_LOG_CALLBACKS`, `MA_CHANNEL_INDEX_NULL`, `MA_MAX_DEVICE_NAME_LENGTH`, `MA_BACKEND_COUNT`, `MA_DATA_FORMAT_FLAG_EXCLUSIVE_MODE` (`unsigned`), `MA_DATA_SOURCE_SELF_MANAGED_RANGE_AND_LOOP_POINT`, `MA_RESOURCE_MANAGER_MAX_JOB_THREAD_COUNT`, `MA_MAX_NODE_BUS_COUNT`, `MA_MAX_NODE_LOCAL_BUS_COUNT`, `MA_NODE_BUS_COUNT_UNKNOWN`, `MA_ENGINE_MAX_LISTENERS`, `MA_LISTENER_INDEX_CLOSEST` (`ma_uint8`), `MA_SOUND_SOURCE_CHANNEL_COUNT`, `MA_JOB_TYPE_RESOURCE_MANAGER_QUEUE_FLAG_NON_BLOCKING` (the enumerator it names).
- **Compatibility names** the header keeps as macros, since the resource manager's job queue became the general `ma_job` queue: `ma_resource_manager_job`, `ma_resource_manager_job_queue_config` and `ma_resource_manager_job_queue` as aliases of `ma_job`, `ma_job_queue_config` and `ma_job_queue`; `ma_resource_manager_job_init`, `ma_resource_manager_job_queue_config_init`, `ma_resource_manager_job_queue_get_heap_size`, `ma_resource_manager_job_queue_init_preallocated`, `ma_resource_manager_job_queue_init`, `ma_resource_manager_job_queue_uninit`, `ma_resource_manager_job_queue_post` and `ma_resource_manager_job_queue_next` as constexpr references to the functions they name (a call is the same call, the address the same address).

`MA_SIZE_MAX` is `SIZE_MAX` where the header sees `<stdint.h>` and `0xFFFFFFFF` where it does not (upstream's fallback); the module includes `<miniaudio.h>` alone, so on the targets CI runs it is the fallback, as it is for a C file that includes only `<miniaudio.h>`. Prefer `std::numeric_limits<std::size_t>::max()` where the full range is meant.

miniaudio's enums are typedef'd (`typedef enum { … } ma_result;`), so they have linkage and their enumerators are exported as they are: `MA_SUCCESS`, `ma_format_f32`, `MA_CHANNEL_FRONT_LEFT`, `MA_SOUND_FLAG_DECODE` and the rest need no mapping.

### What stays a macro

These cannot be a constant or a function without changing what they do. They are macros in `<mwrap/miniaudio/macros.hpp>`, which is `<miniaudio.h>` (declarations only); include it before the import:

| Macros | Why |
|---|---|
| `MA_STRINGIFY`, `MA_XSTRINGIFY` | stringification |
| `MA_API`, `MA_PRIVATE`, `MA_DLL_IMPORT`, `MA_DLL_EXPORT`, `MA_DLL_PRIVATE`, `MA_INLINE`, `MA_NO_INLINE`, `MA_GNUC_INLINE_HINT`, `MA_ATTRIBUTE_FORMAT`, `MA_FALLTHROUGH`, `MA_ATOMIC` | attributes, storage classes and specifiers of declarations and statements |
| `MA_ATOMIC_SAFE_TYPE_DECL`, `MA_ATOMIC_SAFE_TYPE_DECL_PTR` | declare a type by token pasting (the types they declare upstream, `ma_atomic_uint32` …, are exported) |
| `MA_WIN32`, `MA_WIN32_DESKTOP`, `MA_POSIX`, `MA_UNIX`, `MA_LINUX`, `MA_APPLE`, `MA_BSD`, `MA_ANDROID`, `MA_EMSCRIPTEN` … (21) | platform tests for `#ifdef` |
| `MA_SUPPORT_*`, `MA_HAS_*` (30) | backend tests for `#ifdef`; at run time `ma_is_backend_enabled` and `ma_get_enabled_backends` answer the same question |
| `MA_NO_THREADING`, `MA_NO_RUNTIME_LINKING`, `MA_NO_RESOURCE_MANAGER` | configuration switches the header derives (DOS, the Web's runtime linking …) |

Not mapped: `NULL`, the C library's (the header defines it only where `<stddef.h>` did not), and the include guard. While a header macro is defined it expands even inside a qualified name, so `macros::MA_MAX_CHANNELS` is not reachable where the header is included; use the plain `MA_MAX_CHANNELS` there (see [tests/header_mix_test.cpp](../tests/header_mix_test.cpp)). [tools/gen_exports.py](../tools/gen_exports.py) lists every macro with its reason and [tools/check_exports.py](../tools/check_exports.py) accounts for each.

## Do not define MINIAUDIO_IMPLEMENTATION

compat.miniaudio has compiled the implementation already. Defining `MINIAUDIO_IMPLEMENTATION` (or `MA_IMPLEMENTATION`) before including the side header, or `<miniaudio.h>`, in a program that links this package gives every miniaudio symbol twice: a link error, not a compile error. The same goes for configuration switches (`MA_NO_DEVICE_IO`, `MA_ENABLE_ONLY_SPECIFIC_BACKENDS` …): they are compat.miniaudio's build, and the structures the module exports have the layout that build gives them.

## Platforms and backends

| Target | Enabled backends | |
|---|---|---|
| Linux (glibc) | PulseAudio, ALSA, JACK, Custom, Null | the libraries are loaded with `dlopen` at run time: a machine without them builds and runs, and falls back to the next backend |
| macOS | Core Audio, Custom, Null | CoreFoundation, CoreAudio and AudioToolbox are loaded at run time; no framework is linked |
| Windows | WASAPI, DirectSound, WinMM, Custom, Null | `ole32`, `user32`, `advapi32` are loaded at run time |
| wasm32-emscripten | Web Audio, Custom | the Null backend needs a background thread and is not built on Emscripten upstream |

### The Web

The Web Audio backend is JavaScript in the program (`EM_ASM`); it needs an `AudioContext`, which a page has and node does not (there `ma_context_init` returns `MA_NO_BACKEND`). Without `-pthread`, a program has no threads: initialise the engine with a resource manager of your own whose `flags` include `MA_RESOURCE_MANAGER_FLAG_NO_THREADING` and whose `jobThreadCount` is 0 (as [tests/api_test.cpp](../tests/api_test.cpp) `EngineWithoutADevice` does). Browsers start an `AudioContext` only after a user gesture. AudioWorklets (`MA_ENABLE_AUDIO_WORKLETS`, `-sAUDIO_WORKLET=1 -sWASM_WORKERS=1 -sASYNCIFY`) are a configuration of compat.miniaudio's build, not of this module; the default is the ScriptProcessorNode path.

### Decoders

miniaudio carries dr_wav, dr_flac and dr_mp3 inside `miniaudio.h`, renamed `ma_dr_wav_*`, `ma_dr_flac_*`, `ma_dr_mp3_*`: a program may also link the separate dr_libs (`drwav_*`, `drflac_*`, `drmp3_*`) without a clash. Other formats (Vorbis, Opus) plug in as custom decoders (`ma_decoding_backend_vtable`).
