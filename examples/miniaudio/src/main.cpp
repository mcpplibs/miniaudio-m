// miniaudio — an audio file tool, through import mcpplibs.mwrap.miniaudio:
//
//   miniaudio info <file>...                  channels, rate, length, peak, RMS and decoded format
//   miniaudio convert <in> <out.wav> [--rate N] [--channels N] [--format u8|s16|s24|s32|f32]
//                                             decode WAV / FLAC / MP3, convert, encode WAV
//   miniaudio tone <out.wav> [--freq HZ] [--seconds S] [--rate N] [--wave sine|square|triangle|sawtooth]
//   miniaudio play <file> [--seconds S]       on the default playback device
//   miniaudio devices                         the playback and capture devices of the default backend
//
// Everything but `play` and `devices` works without an audio device.
import std;
import mcpplibs.mwrap.miniaudio;

namespace {

int fail(std::string_view what, ma_result r) {
    std::println(std::cerr, "miniaudio: {}: {}", what, ma_result_description(r));
    return 1;
}

std::optional<std::string> option(std::span<char*> args, std::string_view name) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == name) return std::string(args[i + 1]);
    return std::nullopt;
}

std::optional<ma_format> format_named(std::string_view s) {
    for (auto [n, f] : {std::pair{"u8", ma_format_u8}, {"s16", ma_format_s16}, {"s24", ma_format_s24},
                        {"s32", ma_format_s32}, {"f32", ma_format_f32}})
        if (s == n) return f;
    return std::nullopt;
}

int info(std::span<char*> files) {
    int status = 0;
    for (const char* path : files) {
        // Decoded in the file's own format, channels and rate (0: as the file
        // has them), each block measured as f32.
        const ma_decoder_config config = ma_decoder_config_init(ma_format_unknown, 0, 0);
        ma_decoder decoder;
        if (const ma_result r = ma_decoder_init_file(path, &config, &decoder); r != MA_SUCCESS) {
            status = fail(path, r);
            continue;
        }
        const ma_format native = decoder.outputFormat;
        const ma_uint32 channels = decoder.outputChannels, rate = decoder.outputSampleRate;
        std::vector<std::byte> block(4096 * ma_get_bytes_per_sample(native) * channels);
        std::vector<float> f32(4096 * channels);
        ma_uint64 frames = 0, read = 0;
        double sum = 0, peak = 0;
        while (ma_decoder_read_pcm_frames(&decoder, block.data(), 4096, &read) == MA_SUCCESS && read > 0) {
            ma_pcm_convert(f32.data(), ma_format_f32, block.data(), native, read * channels, ma_dither_mode_none);
            for (std::size_t i = 0; i < read * channels; ++i) {
                sum += double(f32[i]) * f32[i];
                peak = std::max(peak, double(std::abs(f32[i])));
            }
            frames += read;
        }
        const double rms = frames ? std::sqrt(sum / double(frames * channels)) : 0;
        std::println("{}: {} ch, {} Hz, {} frames ({:.3f} s), peak {:.4f}, rms {:.4f}, decoded as {}", path, channels,
                     rate, frames, rate ? double(frames) / rate : 0.0, peak, rms, ma_get_format_name(native));
        ma_decoder_uninit(&decoder);
    }
    return status;
}

int convert(std::span<char*> args) {
    if (args.size() < 2) return fail("convert <in> <out.wav>", MA_INVALID_ARGS);
    ma_format format = ma_format_s16;
    if (auto f = option(args, "--format")) {
        if (!format_named(*f)) return fail("--format", MA_INVALID_ARGS);
        format = *format_named(*f);
    }
    const ma_uint32 channels = option(args, "--channels") ? std::stoul(*option(args, "--channels")) : 0;
    const ma_uint32 rate = option(args, "--rate") ? std::stoul(*option(args, "--rate")) : 0;
    // The decoder converts: 0 keeps the file's channels and rate.
    const ma_decoder_config dconfig = ma_decoder_config_init(format, channels, rate);
    ma_decoder decoder;
    if (const ma_result r = ma_decoder_init_file(args[0], &dconfig, &decoder); r != MA_SUCCESS) return fail(args[0], r);
    const ma_encoder_config econfig = ma_encoder_config_init(ma_encoding_format_wav, decoder.outputFormat,
                                                             decoder.outputChannels, decoder.outputSampleRate);
    ma_encoder encoder;
    if (const ma_result r = ma_encoder_init_file(args[1], &econfig, &encoder); r != MA_SUCCESS) {
        ma_decoder_uninit(&decoder);
        return fail(args[1], r);
    }
    std::vector<std::byte> block(4096 * ma_get_bytes_per_sample(decoder.outputFormat) * decoder.outputChannels);
    ma_uint64 read = 0, total = 0;
    while (ma_decoder_read_pcm_frames(&decoder, block.data(), 4096, &read) == MA_SUCCESS && read > 0) {
        ma_encoder_write_pcm_frames(&encoder, block.data(), read, nullptr);
        total += read;
    }
    std::println("{} -> {}: {} {} ch {} Hz, {} frames", args[0], args[1], ma_get_format_name(decoder.outputFormat),
                 decoder.outputChannels, decoder.outputSampleRate, total);
    ma_encoder_uninit(&encoder);
    ma_decoder_uninit(&decoder);
    return 0;
}

int tone(std::span<char*> args) {
    if (args.empty()) return fail("tone <out.wav>", MA_INVALID_ARGS);
    const double freq = option(args, "--freq") ? std::stod(*option(args, "--freq")) : 440.0;
    const double seconds = option(args, "--seconds") ? std::stod(*option(args, "--seconds")) : 1.0;
    const ma_uint32 rate = option(args, "--rate") ? std::stoul(*option(args, "--rate")) : 48000;
    ma_waveform_type type = ma_waveform_type_sine;
    if (auto w = option(args, "--wave")) {
        if (*w == "square") type = ma_waveform_type_square;
        else if (*w == "triangle") type = ma_waveform_type_triangle;
        else if (*w == "sawtooth") type = ma_waveform_type_sawtooth;
        else if (*w != "sine") return fail("--wave", MA_INVALID_ARGS);
    }
    const ma_waveform_config wconfig = ma_waveform_config_init(ma_format_s16, 1, rate, type, 0.5, freq);
    ma_waveform wave;
    ma_waveform_init(&wconfig, &wave);
    const ma_encoder_config econfig = ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, 1, rate);
    ma_encoder encoder;
    if (const ma_result r = ma_encoder_init_file(args[0], &econfig, &encoder); r != MA_SUCCESS) return fail(args[0], r);
    const auto frames = static_cast<ma_uint64>(seconds * rate);
    std::vector<ma_int16> block(4096);
    for (ma_uint64 done = 0; done < frames;) {
        const ma_uint64 n = std::min<ma_uint64>(block.size(), frames - done);
        ma_waveform_read_pcm_frames(&wave, block.data(), n, nullptr);
        ma_encoder_write_pcm_frames(&encoder, block.data(), n, nullptr);
        done += n;
    }
    ma_encoder_uninit(&encoder);
    ma_waveform_uninit(&wave);
    std::println("{}: {} Hz {}, {} frames at {} Hz", args[0], freq, option(args, "--wave").value_or("sine"), frames, rate);
    return 0;
}

int play(std::span<char*> args) {
    if (args.empty()) return fail("play <file>", MA_INVALID_ARGS);
    ma_engine engine;
    if (const ma_result r = ma_engine_init(nullptr, &engine); r != MA_SUCCESS) return fail("no playback device", r);
    ma_sound sound;
    if (const ma_result r = ma_sound_init_from_file(&engine, args[0], MA_SOUND_FLAG_DECODE, nullptr, nullptr, &sound);
        r != MA_SUCCESS) {
        ma_engine_uninit(&engine);
        return fail(args[0], r);
    }
    float length = 0;
    ma_sound_get_length_in_seconds(&sound, &length);
    const double limit = option(args, "--seconds") ? std::stod(*option(args, "--seconds")) : length;
    std::println("playing {} ({:.2f} s) on {}", args[0], length, ma_engine_get_device(&engine)->playback.name);
    ma_sound_start(&sound);
    const auto start = std::chrono::steady_clock::now();
    while (!ma_sound_at_end(&sound) && std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() < limit)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ma_sound_uninit(&sound);
    ma_engine_uninit(&engine);
    return 0;
}

int devices() {
    ma_context context;
    if (const ma_result r = ma_context_init(nullptr, 0, nullptr, &context); r != MA_SUCCESS) return fail("context", r);
    ma_device_info *playback = nullptr, *capture = nullptr;
    ma_uint32 playbackCount = 0, captureCount = 0;
    if (const ma_result r = ma_context_get_devices(&context, &playback, &playbackCount, &capture, &captureCount); r != MA_SUCCESS) {
        ma_context_uninit(&context);
        return fail("devices", r);
    }
    std::println("backend {}", ma_get_backend_name(context.backend));
    for (ma_uint32 i = 0; i < playbackCount; ++i)
        std::println("playback {}{}", playback[i].name, playback[i].isDefault ? " (default)" : "");
    for (ma_uint32 i = 0; i < captureCount; ++i)
        std::println("capture  {}{}", capture[i].name, capture[i].isDefault ? " (default)" : "");
    ma_context_uninit(&context);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv + std::min(argc, 1), argv + argc);
    const std::string_view command = args.empty() ? "" : args[0];
    const auto rest = args.empty() ? args : args.subspan(1);
    if (command == "info" && !rest.empty()) return info(rest);
    if (command == "convert") return convert(rest);
    if (command == "tone") return tone(rest);
    if (command == "play") return play(rest);
    if (command == "devices") return devices();
    std::println(std::cerr, "usage: miniaudio info <file>... | convert <in> <out.wav> [--rate N] [--channels N] "
                            "[--format u8|s16|s24|s32|f32] | tone <out.wav> [--freq HZ] [--seconds S] [--rate N] "
                            "[--wave sine|square|triangle|sawtooth] | play <file> [--seconds S] | devices");
    std::println(std::cerr, "miniaudio {}", ma_version_string());
    return 2;
}
