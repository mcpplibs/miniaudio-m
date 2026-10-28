// The miniaudio API through the module, spelled as <miniaudio.h> documents it:
// the version, the backends of this target, a context and a playback device on
// the Null backend pulling a waveform, the Web Audio backend's own probe on
// wasm32-emscripten, waveform and noise data sources, WAV encoding into memory
// and decoding it back, decoding WAV, FLAC and MP3 from memory and from a file,
// the resampler and the data and channel converters, a low-pass filter, the
// PCM ring buffer, the high-level engine without a device (ma_engine_read_
// pcm_frames) mixing a sound and a sound group, and the adapted inline
// functions. Only the module is imported; no miniaudio header is included.
//
// No test needs an audio device: CI has none. The Null backend (every target
// but Emscripten, where upstream does not build it: it needs a background
// thread) runs a device in real time without one.
#include <gtest/gtest.h>

import std;
import mcpplibs.mwrap.miniaudio;

using namespace mcpplibs::mwrap::miniaudio::macros;  // MA_VERSION_STRING, MA_MAX_CHANNELS …

namespace {

#include "data/fixtures.inc"

constexpr ma_uint32 kToneFrames = 4410;   // tools/gen_fixtures.py: 0.1 s of 440 Hz at 44.1 kHz
constexpr ma_uint32 kToneRate = 44100;

std::vector<ma_int16> decode_s16(ma_decoder& decoder) {
    std::vector<ma_int16> out;
    ma_int16 block[512];
    for (;;) {
        ma_uint64 read = 0;
        const ma_result r = ma_decoder_read_pcm_frames(&decoder, block, 512 / decoder.outputChannels, &read);
        out.insert(out.end(), block, block + read * decoder.outputChannels);
        if (r != MA_SUCCESS || read == 0) break;
    }
    return out;
}

std::vector<ma_int16> decode_memory(const unsigned char* data, std::size_t size, ma_encoding_format format) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 1, kToneRate);
    config.encodingFormat = format;
    ma_decoder decoder;
    if (ma_decoder_init_memory(data, size, &config, &decoder) != MA_SUCCESS) return {};
    auto pcm = decode_s16(decoder);
    ma_decoder_uninit(&decoder);
    return pcm;
}

// Sign changes of a signal louder than `floor` (a Schmitt trigger: the
// near-silence of an MP3's encoder delay does not count).
int zero_crossings(std::span<const float> s, float floor = 0.05f) {
    int n = 0, side = 0;
    for (float v : s) {
        const int now = v > floor ? 1 : v < -floor ? -1 : side;
        n += side != 0 && now != side;
        side = now;
    }
    return n;
}

float peak(std::span<const float> s) {
    float p = 0;
    for (float v : s) p = std::max(p, std::abs(v));
    return p;
}

}  // namespace

TEST(Api, Version) {
    ma_uint32 major = 0, minor = 0, revision = 0;
    ma_version(&major, &minor, &revision);
    EXPECT_EQ(major, 0u);
    EXPECT_EQ(minor, 11u);
    EXPECT_EQ(revision, 25u);
    EXPECT_STREQ(ma_version_string(), "0.11.25");
    EXPECT_STREQ(ma_version_string(), MA_VERSION_STRING);
}

TEST(Api, BackendsOfThisTarget) {
    ma_backend enabled[MA_BACKEND_COUNT];
    std::size_t count = 0;
    ASSERT_EQ(ma_get_enabled_backends(enabled, MA_BACKEND_COUNT, &count), MA_SUCCESS);
    const std::span<const ma_backend> backends(enabled, count);
    const auto has = [&](ma_backend b) { return std::ranges::find(backends, b) != backends.end(); };
    EXPECT_TRUE(has(ma_backend_custom));
    EXPECT_STREQ(ma_get_backend_name(ma_backend_null), "Null");
#if defined(__EMSCRIPTEN__)
    EXPECT_TRUE(has(ma_backend_webaudio));
    EXPECT_FALSE(has(ma_backend_null));
    EXPECT_EQ(ma_is_backend_enabled(ma_backend_webaudio), MA_TRUE);
#else
    EXPECT_TRUE(has(ma_backend_null));
#endif
#if defined(_WIN32)
    EXPECT_TRUE(has(ma_backend_wasapi));
#elif defined(__APPLE__)
    EXPECT_TRUE(has(ma_backend_coreaudio));
#elif defined(__linux__)
    EXPECT_TRUE(has(ma_backend_alsa));
    EXPECT_TRUE(has(ma_backend_pulseaudio));
#endif
    for (ma_backend b : backends) EXPECT_EQ(ma_is_backend_enabled(b), MA_TRUE);
}

#if defined(__EMSCRIPTEN__)
TEST(Api, WebAudioProbesItsHost) {
    // The Web Audio backend is JavaScript (EM_ASM) that asks the host for an
    // AudioContext. node has none, so the context reports no backend: the
    // backend's code was linked and ran. A page has one, and the same call
    // succeeds there.
    ma_backend webaudio = ma_backend_webaudio;
    ma_context context;
    EXPECT_EQ(ma_context_init(&webaudio, 1, nullptr, &context), MA_NO_BACKEND);
}
#endif

TEST(Api, NullDevicePullsAWaveform) {
    if (!ma_is_backend_enabled(ma_backend_null)) GTEST_SKIP() << "no Null backend on this target";
    ma_backend null = ma_backend_null;
    ma_context context;
    ASSERT_EQ(ma_context_init(&null, 1, nullptr, &context), MA_SUCCESS);
    EXPECT_EQ(context.backend, ma_backend_null);

    ma_device_info* playback = nullptr;
    ma_uint32 playbackCount = 0;
    ASSERT_EQ(ma_context_get_devices(&context, &playback, &playbackCount, nullptr, nullptr), MA_SUCCESS);
    ASSERT_GE(playbackCount, 1u);

    struct Feed {
        ma_waveform wave;
        std::atomic<ma_uint64> frames{0};
    } feed;
    const ma_waveform_config waveConfig = ma_waveform_config_init(ma_format_f32, 2, 48000, ma_waveform_type_sine, 0.25, 440);
    ASSERT_EQ(ma_waveform_init(&waveConfig, &feed.wave), MA_SUCCESS);

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 48000;
    config.pUserData = &feed;
    config.dataCallback = [](ma_device* device, void* output, const void*, ma_uint32 frameCount) {
        auto* f = static_cast<Feed*>(device->pUserData);
        ma_waveform_read_pcm_frames(&f->wave, output, frameCount, nullptr);
        f->frames += frameCount;
    };
    ma_device device;
    ASSERT_EQ(ma_device_init(&context, &config, &device), MA_SUCCESS);
    EXPECT_EQ(ma_device_get_state(&device), ma_device_state_stopped);
    ASSERT_EQ(ma_device_start(&device), MA_SUCCESS);
    EXPECT_EQ(ma_device_is_started(&device), MA_TRUE);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (feed.frames < 4800 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_EQ(ma_device_stop(&device), MA_SUCCESS);
    EXPECT_GE(feed.frames.load(), 4800u);  // 0.1 s, at the Null backend's real-time pace
    ma_device_uninit(&device);
    ma_waveform_uninit(&feed.wave);
    EXPECT_EQ(ma_context_uninit(&context), MA_SUCCESS);
}

TEST(Api, WaveformAndNoise) {
    const ma_waveform_config config = ma_waveform_config_init(ma_format_f32, 1, 48000, ma_waveform_type_sine, 0.5, 480);
    ma_waveform wave;
    ASSERT_EQ(ma_waveform_init(&config, &wave), MA_SUCCESS);
    std::vector<float> s(4800);  // 0.1 s: 48 periods
    ma_uint64 read = 0;
    ASSERT_EQ(ma_waveform_read_pcm_frames(&wave, s.data(), s.size(), &read), MA_SUCCESS);
    EXPECT_EQ(read, s.size());
    EXPECT_NEAR(peak(s), 0.5f, 1e-3f);
    EXPECT_NEAR(zero_crossings(s), 96, 1);
    ma_waveform_uninit(&wave);

    // White noise from a seed is the same sequence each time.
    const ma_noise_config noiseConfig = [] {
        ma_noise_config c = ma_noise_config_init(ma_format_f32, 1, ma_noise_type_white, 1234, 0.5);
        return c;
    }();
    std::array<std::vector<float>, 2> runs;
    for (auto& run : runs) {
        ma_noise noise;
        ASSERT_EQ(ma_noise_init(&noiseConfig, nullptr, &noise), MA_SUCCESS);
        run.resize(256);
        ASSERT_EQ(ma_noise_read_pcm_frames(&noise, run.data(), run.size(), nullptr), MA_SUCCESS);
        ma_noise_uninit(&noise, nullptr);
    }
    EXPECT_EQ(runs[0], runs[1]);
    EXPECT_LE(peak(runs[0]), 0.5f);
    EXPECT_GT(peak(runs[0]), 0.1f);
}

TEST(Api, EncodeWavIntoMemoryAndDecodeIt) {
    struct Sink {
        std::vector<unsigned char> bytes;
        std::size_t at = 0;
    } sink;
    const auto write = [](ma_encoder* e, const void* data, std::size_t n, std::size_t* written) -> ma_result {
        auto* s = static_cast<Sink*>(e->pUserData);
        if (s->bytes.size() < s->at + n) s->bytes.resize(s->at + n);
        std::memcpy(s->bytes.data() + s->at, data, n);
        s->at += n;
        *written = n;
        return MA_SUCCESS;
    };
    const auto seek = [](ma_encoder* e, ma_int64 offset, ma_seek_origin origin) -> ma_result {
        auto* s = static_cast<Sink*>(e->pUserData);
        const ma_int64 base = origin == ma_seek_origin_start ? 0 : origin == ma_seek_origin_current
                                  ? static_cast<ma_int64>(s->at) : static_cast<ma_int64>(s->bytes.size());
        s->at = static_cast<std::size_t>(base + offset);
        return MA_SUCCESS;
    };
    std::vector<ma_int16> pcm(2 * 1000);
    for (std::size_t i = 0; i < pcm.size(); ++i) pcm[i] = static_cast<ma_int16>((i * 37) % 20000 - 10000);

    const ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, 2, 22050);
    ma_encoder encoder;
    ASSERT_EQ(ma_encoder_init(write, seek, &sink, &config, &encoder), MA_SUCCESS);
    ma_uint64 written = 0;
    ASSERT_EQ(ma_encoder_write_pcm_frames(&encoder, pcm.data(), 1000, &written), MA_SUCCESS);
    EXPECT_EQ(written, 1000u);
    ma_encoder_uninit(&encoder);
    ASSERT_EQ(sink.bytes.size(), 44u + pcm.size() * 2);
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(sink.bytes.data()), 4), "RIFF");
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(sink.bytes.data()) + 8, 4), "WAVE");

    ma_decoder_config dconfig = ma_decoder_config_init(ma_format_s16, 0, 0);  // the file's own channels and rate
    ma_decoder decoder;
    ASSERT_EQ(ma_decoder_init_memory(sink.bytes.data(), sink.bytes.size(), &dconfig, &decoder), MA_SUCCESS);
    ma_format format;
    ma_uint32 channels = 0, rate = 0;
    ASSERT_EQ(ma_decoder_get_data_format(&decoder, &format, &channels, &rate, nullptr, 0), MA_SUCCESS);
    EXPECT_EQ(format, ma_format_s16);
    EXPECT_EQ(channels, 2u);
    EXPECT_EQ(rate, 22050u);
    ma_uint64 length = 0;
    ASSERT_EQ(ma_decoder_get_length_in_pcm_frames(&decoder, &length), MA_SUCCESS);
    EXPECT_EQ(length, 1000u);
    EXPECT_EQ(decode_s16(decoder), pcm);
    ma_decoder_uninit(&decoder);
}

TEST(Api, DecodeWavFlacMp3FromMemory) {
    const auto wav = decode_memory(fixture_tone_wav, sizeof fixture_tone_wav, ma_encoding_format_wav);
    const auto flac = decode_memory(fixture_tone_flac, sizeof fixture_tone_flac, ma_encoding_format_flac);
    const auto mp3 = decode_memory(fixture_tone_mp3, sizeof fixture_tone_mp3, ma_encoding_format_mp3);
    ASSERT_EQ(wav.size(), kToneFrames);
    EXPECT_EQ(flac, wav);  // lossless
    // The MP3 is lossy and framed (1152 samples a frame, an encoder delay): the
    // same tone, about as long, at about the same level.
    ASSERT_GE(mp3.size(), kToneFrames - 1152);
    ASSERT_LE(mp3.size(), kToneFrames + 2 * 1152);
    std::vector<float> a(wav.begin(), wav.end()), b(mp3.begin(), mp3.end());
    for (float& v : a) v /= 32768;
    for (float& v : b) v /= 32768;
    EXPECT_NEAR(peak(b), peak(a), 0.08f);
    EXPECT_NEAR(zero_crossings(b), zero_crossings(a), 4);
    // Any format, recognised from the bytes.
    EXPECT_EQ(decode_memory(fixture_tone_flac, sizeof fixture_tone_flac, ma_encoding_format_unknown), wav);
}

TEST(Api, DecodeFromAFile) {
    const auto path = (std::filesystem::temp_directory_path() / "mwrap-miniaudio-tone.flac").string();
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(fixture_tone_flac), sizeof fixture_tone_flac);
    }
    const ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 0, 0);
    ma_decoder decoder;
    ASSERT_EQ(ma_decoder_init_file(path.c_str(), &config, &decoder), MA_SUCCESS);
    EXPECT_EQ(decoder.outputFormat, ma_format_s16);
    EXPECT_EQ(decoder.outputChannels, 1u);
    EXPECT_EQ(decoder.outputSampleRate, kToneRate);
    EXPECT_EQ(decode_s16(decoder), decode_memory(fixture_tone_wav, sizeof fixture_tone_wav, ma_encoding_format_wav));
    ma_decoder_uninit(&decoder);
    std::filesystem::remove(path);
}

TEST(Api, ResamplerAndConverters) {
    // 0.1 s at 48 kHz becomes 0.1 s at 44.1 kHz.
    const ma_resampler_config rconfig = ma_resampler_config_init(ma_format_f32, 1, 48000, 44100, ma_resample_algorithm_linear);
    ma_resampler resampler;
    ASSERT_EQ(ma_resampler_init(&rconfig, nullptr, &resampler), MA_SUCCESS);
    ma_uint64 expected = 0;
    ASSERT_EQ(ma_resampler_get_expected_output_frame_count(&resampler, 4800, &expected), MA_SUCCESS);
    EXPECT_NEAR(static_cast<double>(expected), 4410.0, 1.0);
    std::vector<float> in(4800, 0.25f), out(4500);
    ma_uint64 frameCountIn = in.size(), frameCountOut = out.size();
    ASSERT_EQ(ma_resampler_process_pcm_frames(&resampler, in.data(), &frameCountIn, out.data(), &frameCountOut), MA_SUCCESS);
    EXPECT_EQ(frameCountIn, 4800u);
    EXPECT_NEAR(static_cast<double>(frameCountOut), 4410.0, 2.0);
    EXPECT_NEAR(out[frameCountOut / 2], 0.25f, 1e-4f);
    ma_resampler_uninit(&resampler, nullptr);

    // f32 stereo at 48 kHz to s16 mono at 44.1 kHz in one converter.
    const ma_data_converter_config cconfig = ma_data_converter_config_init(ma_format_f32, ma_format_s16, 2, 1, 48000, 44100);
    ma_data_converter converter;
    ASSERT_EQ(ma_data_converter_init(&cconfig, nullptr, &converter), MA_SUCCESS);
    std::vector<float> stereo(2 * 4800, 0.5f);
    std::vector<ma_int16> mono(4500);
    frameCountIn = 4800;
    frameCountOut = mono.size();
    ASSERT_EQ(ma_data_converter_process_pcm_frames(&converter, stereo.data(), &frameCountIn, mono.data(), &frameCountOut), MA_SUCCESS);
    EXPECT_NEAR(static_cast<double>(frameCountOut), 4410.0, 2.0);
    EXPECT_NEAR(mono[frameCountOut / 2], 16384, 2);  // 0.5 in both channels: 0.5 in the mix
    ma_data_converter_uninit(&converter, nullptr);

    // Mono to stereo duplicates the channel.
    const ma_channel_converter_config chconfig = ma_channel_converter_config_init(ma_format_f32, 1, nullptr, 2, nullptr, ma_channel_mix_mode_default);
    ma_channel_converter channels;
    ASSERT_EQ(ma_channel_converter_init(&chconfig, nullptr, &channels), MA_SUCCESS);
    const float one[3] = {0.1f, -0.2f, 0.3f};
    float two[6] = {};
    ASSERT_EQ(ma_channel_converter_process_pcm_frames(&channels, two, one, 3), MA_SUCCESS);
    for (int i = 0; i < 3; ++i) {
        EXPECT_FLOAT_EQ(two[2 * i], one[i]);
        EXPECT_FLOAT_EQ(two[2 * i + 1], one[i]);
    }
    ma_channel_converter_uninit(&channels, nullptr);

    // And sample formats on their own.
    float f[2] = {0.5f, -1.0f};
    ma_int16 s[2] = {};
    ma_pcm_convert(s, ma_format_s16, f, ma_format_f32, 2, ma_dither_mode_none);
    EXPECT_EQ(s[0], 16383);
    EXPECT_EQ(s[1], -32767);
}

TEST(Api, LowPassFilter) {
    // A 2nd-order low-pass at 1 kHz passes 100 Hz and attenuates 10 kHz.
    const auto through = [](double frequency) {
        const ma_lpf_config config = ma_lpf_config_init(ma_format_f32, 1, 48000, 1000, 2);
        ma_lpf lpf;
        EXPECT_EQ(ma_lpf_init(&config, nullptr, &lpf), MA_SUCCESS);
        const ma_waveform_config wc = ma_waveform_config_init(ma_format_f32, 1, 48000, ma_waveform_type_sine, 1.0, frequency);
        ma_waveform wave;
        ma_waveform_init(&wc, &wave);
        std::vector<float> in(9600), out(9600);
        ma_waveform_read_pcm_frames(&wave, in.data(), in.size(), nullptr);
        ma_lpf_process_pcm_frames(&lpf, out.data(), in.data(), in.size());
        ma_lpf_uninit(&lpf, nullptr);
        ma_waveform_uninit(&wave);
        return peak(std::span(out).subspan(4800));
    };
    EXPECT_GT(through(100), 0.9f);
    EXPECT_LT(through(10000), 0.05f);
}

TEST(Api, PcmRingBuffer) {
    ma_pcm_rb rb;
    ASSERT_EQ(ma_pcm_rb_init(ma_format_s16, 2, 256, nullptr, nullptr, &rb), MA_SUCCESS);
    EXPECT_EQ(ma_pcm_rb_get_subbuffer_size(&rb), 256u);
    ma_uint32 frames = 100;
    void* p = nullptr;
    ASSERT_EQ(ma_pcm_rb_acquire_write(&rb, &frames, &p), MA_SUCCESS);
    ASSERT_EQ(frames, 100u);
    auto* w = static_cast<ma_int16*>(p);
    for (int i = 0; i < 200; ++i) w[i] = static_cast<ma_int16>(i);
    ASSERT_EQ(ma_pcm_rb_commit_write(&rb, 100), MA_SUCCESS);
    EXPECT_EQ(ma_pcm_rb_available_read(&rb), 100u);
    frames = 100;
    ASSERT_EQ(ma_pcm_rb_acquire_read(&rb, &frames, &p), MA_SUCCESS);
    EXPECT_EQ(static_cast<ma_int16*>(p)[199], 199);
    ASSERT_EQ(ma_pcm_rb_commit_read(&rb, frames), MA_SUCCESS);
    EXPECT_EQ(ma_pcm_rb_available_read(&rb), 0u);
    ma_pcm_rb_uninit(&rb);
}

TEST(Api, EngineWithoutADevice) {
    // The high-level engine mixes into a caller's buffer when it has no
    // device. Its resource manager runs without job threads, as a program
    // without threads (the Web) needs.
    ma_resource_manager_config rmConfig = ma_resource_manager_config_init();
    rmConfig.flags = MA_RESOURCE_MANAGER_FLAG_NO_THREADING;
    rmConfig.jobThreadCount = 0;
    ma_resource_manager rm;
    ASSERT_EQ(ma_resource_manager_init(&rmConfig, &rm), MA_SUCCESS);

    ma_engine_config config = ma_engine_config_init();
    config.noDevice = MA_TRUE;
    config.channels = 2;
    config.sampleRate = 48000;
    config.pResourceManager = &rm;
    ma_engine engine;
    ASSERT_EQ(ma_engine_init(&config, &engine), MA_SUCCESS);
    EXPECT_EQ(ma_engine_get_channels(&engine), 2u);
    EXPECT_EQ(ma_engine_get_sample_rate(&engine), 48000u);
    EXPECT_EQ(ma_engine_get_device(&engine), nullptr);

    const ma_waveform_config wc = ma_waveform_config_init(ma_format_f32, 2, 48000, ma_waveform_type_square, 0.5, 100);
    ma_waveform wave;
    ASSERT_EQ(ma_waveform_init(&wc, &wave), MA_SUCCESS);
    ma_sound_group group;
    ASSERT_EQ(ma_sound_group_init(&engine, 0, nullptr, &group), MA_SUCCESS);
    ma_sound sound;
    ASSERT_EQ(ma_sound_init_from_data_source(&engine, &wave, MA_SOUND_FLAG_NO_SPATIALIZATION, &group, &sound), MA_SUCCESS);
    ASSERT_EQ(ma_sound_start(&sound), MA_SUCCESS);
    EXPECT_EQ(ma_sound_is_playing(&sound), MA_TRUE);

    std::vector<float> mix(2 * 480);
    ma_uint64 read = 0;
    ASSERT_EQ(ma_engine_read_pcm_frames(&engine, mix.data(), 480, &read), MA_SUCCESS);
    EXPECT_EQ(read, 480u);
    EXPECT_NEAR(peak(mix), 0.5f, 1e-3f);
    EXPECT_EQ(ma_engine_get_time_in_pcm_frames(&engine), 480u);

    ma_sound_group_set_volume(&group, 0.5f);  // the group's volume scales its sounds
    ASSERT_EQ(ma_engine_read_pcm_frames(&engine, mix.data(), 480, &read), MA_SUCCESS);
    EXPECT_NEAR(peak(mix), 0.25f, 1e-3f);

    ma_sound_stop(&sound);  // with the engine's short stop fade, then silence
    EXPECT_EQ(ma_sound_is_playing(&sound), MA_FALSE);
    for (int i = 0; i < 4; ++i) ASSERT_EQ(ma_engine_read_pcm_frames(&engine, mix.data(), 480, &read), MA_SUCCESS);
    EXPECT_EQ(peak(mix), 0.0f);

    ma_sound_uninit(&sound);
    ma_sound_group_uninit(&group);
    ma_waveform_uninit(&wave);
    ma_engine_uninit(&engine);
    ma_resource_manager_uninit(&rm);
}

TEST(Api, AdaptedInlineFunctions) {
    namespace ma = mcpplibs::mwrap::miniaudio;
    EXPECT_EQ(ma::ma_get_bytes_per_frame(ma_format_s16, 2), 4u);
    EXPECT_EQ(ma::ma_get_bytes_per_frame(ma_format_f32, 6), 24u);
    float frames[8] = {};
    EXPECT_EQ(ma::ma_offset_pcm_frames_ptr_f32(frames, 3, 2), frames + 6);
    const float* cframes = frames;
    EXPECT_EQ(ma::ma_offset_pcm_frames_const_ptr_f32(cframes, 1, 4), cframes + 4);
    EXPECT_EQ(ma_get_bytes_per_sample(ma_format_s24), 3u);
}

TEST(Api, ResultsAndFormats) {
    EXPECT_STREQ(ma_result_description(MA_SUCCESS), "No error");
    EXPECT_EQ(MA_INVALID_ARGS, -2);
    EXPECT_STREQ(ma_get_format_name(ma_format_f32), "32-bit IEEE Floating Point");
    static_assert(MA_MAX_CHANNELS == 254 && MA_MIN_CHANNELS == 1);
    EXPECT_EQ(sizeof(ma_uint64), 8u);
}
