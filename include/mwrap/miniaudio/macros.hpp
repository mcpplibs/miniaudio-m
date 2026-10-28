// <mwrap/miniaudio/macros.hpp> — miniaudio's macros as macros, for the ones a
// C++ entity cannot be: stringification (MA_STRINGIFY), the attributes and
// storage classes of declarations (MA_API, MA_INLINE, MA_ATTRIBUTE_FORMAT …),
// the token-pasting type declarations (MA_ATOMIC_SAFE_TYPE_DECL), and the
// platform and backend tests of conditional compilation (MA_WIN32, MA_POSIX,
// MA_APPLE, MA_EMSCRIPTEN, MA_SUPPORT_*, MA_HAS_* …).
//
// Include it before `import mcpplibs.mwrap.miniaudio;`: it is <miniaudio.h>
// as the module sees it — the declarations only. Do not define
// MINIAUDIO_IMPLEMENTATION: compat.miniaudio already compiles the
// implementation, and a second copy duplicates every symbol at link time.
// Every other macro is defined too, and while a macro is defined it expands
// even inside `mcpplibs::mwrap::miniaudio::macros::NAME`. The header's
// enumerators (MA_SUCCESS, ma_format_f32 …) are the module's own, the same
// entities.
#pragma once
#include <miniaudio.h>
