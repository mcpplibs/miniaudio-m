# miniaudio

> 以 C++23 模块使用 miniaudio——音频播放、采集、解码与混音。

[English](README.md) · **简体中文** · [Upstream](https://github.com/mackron/miniaudio) · [Releases](https://github.com/mcpplibs/miniaudio-m/releases) · [mcpp](https://github.com/mcpp-community/mcpp) · [Index](https://github.com/mcpplibs/mcpp-index)

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![miniaudio](https://img.shields.io/badge/miniaudio-MIT--0%20%7C%20Unlicense-blue.svg)](upstream/miniaudio/LICENSE)
[![mcpp-community](https://img.shields.io/badge/mcpp--community-mwrap-blue)](https://github.com/mcpp-community)
[![CI](https://github.com/mcpplibs/miniaudio-m/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/mcpplibs/miniaudio-m/actions/workflows/ci.yml)

miniaudio 在各平台的原生后端上播放和采集音频（WASAPI、DirectSound、WinMM、
Core Audio、ALSA、PulseAudio、JACK、AAudio、OpenSL、Web Audio 等），解码
WAV、FLAC 与 MP3，编码 WAV，做格式转换与重采样，并通过节点图和高层引擎混音。
它是一个 C 文件，以 MIT No Attribution 或 Unlicense 许可（二选一），没有依赖。
上游 C API 保持不变：`ma_engine_init`、`ma_device_init`、`ma_decoder_init_file`
与 `MA_SUCCESS` 都是 `<miniaudio.h>` 的上游全局名字。库本身是
`compat.miniaudio` 0.11.25。本包把 miniaudio 的数值宏映射为常量，并逐一与头文件
对照检查；支持 Linux、macOS、Windows 与 Web（wasm32-emscripten，Web Audio 后端）。

## 模块

- `import mcpplibs.mwrap.miniaudio;`：`<miniaudio.h>` 的完整 API（context、device、engine、sound 与 sound group、节点图、资源管理器、解码器、编码器、数据源、波形与噪声、转换器、重采样器、滤波器、效果、环形缓冲、日志）及其枚举值，三个内联辅助函数在 `mcpplibs::mwrap::miniaudio` 中适配；并重新导出下面的模块
- `import mcpplibs.mwrap.miniaudio.macros;`：`MA_VERSION_STRING`、`MA_MAX_CHANNELS`、`MA_ENGINE_MAX_LISTENERS`、`MA_LISTENER_INDEX_CLOSEST` 等，位于 `mcpplibs::mwrap::miniaudio::macros`

> 更多模块 API -> [用法与 API](docs/usage.md)

## 快速开始

```sh
mcpp new tone --template mcpplibs.mwrap.miniaudio@0.11.25
cd tone
mcpp run
```

默认模板不需要音频设备：它从波形数据源读出 440 Hz 的音调，在内存中编码为
WAV，再解码回来并测量。`mcpp run -- --play` 会在默认设备上播放它。

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
    ma_engine_play_sound(&engine, argv[1], nullptr);  // WAV、FLAC 或 MP3
    std::println("playing {}; press Enter to quit", argv[1]);
    std::cin.get();
    ma_engine_uninit(&engine);
}
```

## 宏

`MA_MAX_CHANNELS` 等常量保持其宏的类型（`int`），`MA_LISTENER_INDEX_CLOSEST`
是 `ma_uint8`，版本字符串是 `const char[]`。miniaudio 的枚举由其 typedef 命名，
因此枚举值（`MA_SUCCESS`、`ma_format_f32`、`MA_SOUND_FLAG_DECODE` 等）按原样
导出。头文件以宏保留的兼容名字（`ma_resource_manager_job` 等）映射为其所指的
类型别名与函数引用。字符串化、属性、`MA_API` 以及平台与后端测试（`MA_WIN32`、
`MA_HAS_WASAPI` 等）仍是宏，位于 `<mwrap/miniaudio/macros.hpp>`，在 import 之前
包含。详见 [docs/usage.md](docs/usage.md)。

## 平台

| 目标 | 后端（按优先级） | 说明 |
|---|---|---|
| Linux | PulseAudio、ALSA、JACK、Null | 运行时加载：无需 -lasound、-lpulse |
| macOS | Core Audio、Null | 运行时加载框架 |
| Windows | WASAPI、DirectSound、WinMM、Null | |
| wasm32-emscripten | Web Audio | 上游在此不构建 Null 后端（它需要线程） |

Custom 后端在所有平台上都可用。在 Web 上，除非程序启用了线程，否则请给引擎
一个带 `MA_RESOURCE_MANAGER_FLAG_NO_THREADING` 的资源管理器。

## 示例

- [examples/miniaudio](examples/miniaudio)：基于模块编写的音频文件工具（`info`、把 WAV / FLAC / MP3
  按任意采样率、声道数和格式 `convert` 为 WAV、`tone`、`play`、`devices`）；
  [tools/run_examples.py](tools/run_examples.py) 在每个 CI 平台上运行它。

## 构建与测试

Linux（x86_64 GCC 与 LLVM、arm64 GCC）、macOS 与 Windows 的 CI 构建模块，运行
测试（Null 后端上的设备、无设备的引擎、WAV 编码与 WAV / FLAC / MP3 解码、转换器、
滤波器）和导出审计（[tools/check_exports.py](tools/check_exports.py)）：在每个
目标上，依据编译器自身的预处理结果，检查头文件的每个声明都已导出、每个宏都已映射
或列出。Web 行为 wasm32-emscripten 构建，并在 node 下运行同样的测试。另有一个
任务把默认模板作为已安装的包生成并运行。

## 许可

包装代码为 [Apache-2.0](LICENSE)，作者 mcpp-community（[NOTICE](NOTICE)）。
miniaudio 以 Unlicense 或 MIT No Attribution 许可（二选一，
[upstream/miniaudio/LICENSE](upstream/miniaudio/LICENSE)）；其 README 与许可
文件随依赖本包的每个程序一起部署。
