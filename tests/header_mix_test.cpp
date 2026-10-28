// The raw header and the import in one translation unit (the side header
// first, the canonical order): the module's names are the very same entities
// as the header's; the macros a C++ entity cannot be work beside them — MA_API
// on a declaration of one's own, the platform and backend tests, the token-
// pasting atomic declarations; and the adapted inline functions beside the
// header's own.
#include <mwrap/miniaudio/macros.hpp>
#include <gtest/gtest.h>

import std;
import mcpplibs.mwrap.miniaudio;

// A declaration of one's own in miniaudio's style.
extern "C" {
MA_API int mwrap_miniaudio_test_entry(int value);
}
extern "C" int mwrap_miniaudio_test_entry(int value) { return value + 1; }

// A type declared by miniaudio's own macro.
typedef ma_uint32 ma_mwrap_counter;
MA_ATOMIC_SAFE_TYPE_DECL(32, 4, mwrap_counter)

TEST(HeaderMix, SameEntities) {
    // One declaration each, whichever way it was introduced.
    static_assert(std::is_same_v<decltype(&::ma_decoder_init_memory), decltype(&ma_decoder_init_memory)>);
    static_assert(std::is_same_v<::ma_engine, ma_engine> && sizeof(::ma_device) == sizeof(ma_device));
    static_assert(std::is_same_v<::ma_result, ma_result> && ::MA_SUCCESS == MA_SUCCESS);
    EXPECT_EQ(&::ma_version_string, &ma_version_string);
    EXPECT_EQ(mwrap_miniaudio_test_entry(41), 42);
    ma_atomic_mwrap_counter counter{};
    counter.value = 7;
    EXPECT_EQ(counter.value, 7u);
    static_assert(sizeof(ma_atomic_uint32) == 4);
}

TEST(HeaderMix, MacrosBesideTheMappings) {
    namespace m = mcpplibs::mwrap::miniaudio::macros;
    // An object-like macro expands even in a qualified name: set it aside to
    // name the constant.
    constexpr auto raw = MA_MAX_CHANNELS;
#pragma push_macro("MA_MAX_CHANNELS")
#undef MA_MAX_CHANNELS
    static_assert(m::MA_MAX_CHANNELS == raw);
#pragma pop_macro("MA_MAX_CHANNELS")
    EXPECT_STREQ(MA_XSTRINGIFY(MA_VERSION_MINOR), "11");
}

TEST(HeaderMix, PlatformAndBackendTests) {
    // The header's tests, beside the run-time answer.
#if defined(MA_HAS_NULL)
    EXPECT_EQ(ma_is_backend_enabled(ma_backend_null), MA_TRUE);
#else
    EXPECT_EQ(ma_is_backend_enabled(ma_backend_null), MA_FALSE);
#endif
#if defined(MA_EMSCRIPTEN)
    EXPECT_EQ(ma_is_backend_enabled(ma_backend_webaudio), MA_TRUE);
#elif defined(MA_WIN32)
    EXPECT_EQ(ma_is_backend_enabled(ma_backend_wasapi), MA_TRUE);
#elif defined(MA_APPLE)
    EXPECT_EQ(ma_is_backend_enabled(ma_backend_coreaudio), MA_TRUE);
#elif defined(MA_LINUX)
    EXPECT_EQ(ma_is_backend_enabled(ma_backend_alsa), MA_TRUE);
#endif
#if defined(MA_POSIX)
    static_assert(std::is_same_v<ma_pthread_t, pthread_t>);
#endif
}

TEST(HeaderMix, AdaptedInlineBesideTheHeaders) {
    // The header's static inline function and the module's adaptation give
    // the same result; the qualified name selects the adaptation.
    EXPECT_EQ(::ma_get_bytes_per_frame(ma_format_s24, 2), mcpplibs::mwrap::miniaudio::ma_get_bytes_per_frame(ma_format_s24, 2));
    float f[4] = {};
    EXPECT_EQ(::ma_offset_pcm_frames_ptr_f32(f, 1, 2), mcpplibs::mwrap::miniaudio::ma_offset_pcm_frames_ptr_f32(f, 1, 2));
}
