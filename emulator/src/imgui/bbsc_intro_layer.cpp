// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The BBSC intro: a ~7.5 s title card on the foreground draw list, laid out for 1080p and scaled
// to the window. Everything is drawn from shapes and the overlay's own NotoSans font, so there are
// no extra assets. Timeline (seconds):
//   0.0  black cover fades in over the boot screen; embers start rising
//   0.6  a blood-red thread draws out from the centre; drips fall from it from 1.9 on
//   0.9  "BLOODBORNE" appears letter by letter
//   2.0  "SEAMLESS  CO-OP"      3.0  the tagline      3.4  build, credits and notice
//   6.3  everything fades out (0.6 s after any key / button / click instead)

// When BBSC-intro.mp4 sits next to shadPS4.exe, the cinematic plays instead (optionally led by shots
// from the game's own trailer, see BuildPlaylist): FFmpeg decodes on its own thread, frames stream
// into a Vulkan texture drawn over everything, the sound plays through SDL, any key skips, and it
// fades into the game when it ends.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_init.h>
#include <imgui.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <nlohmann/json.hpp>

#include "common/logging/log.h"
#include "common/singleton.h"
#include "core/bbsc_persistence.h"
#include "core/file_sys/fs.h"
#include "imgui/bbsc_intro_layer.h"
#include "imgui/imgui_layer.h"
#include "imgui/imgui_std.h"
#include "imgui/renderer/imgui_impl_vulkan.h"

namespace Core::Bbsc {
namespace {

constexpr float IntroSeconds = 7.5f;
constexpr float FadeOutSeconds = 1.2f;
constexpr float SkipFadeSeconds = 0.6f;
constexpr int EmberCount = 48;

float Clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}
// 0 before start, 1 after start + length, linear in between.
float Ramp(float t, float start, float length) {
    return Clamp01((t - start) / length);
}
float EaseOut(float x) {
    return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x);
}
ImU32 Rgba(int r, int g, int b, float a) {
    return IM_COL32(r, g, b, static_cast<int>(Clamp01(a) * 255.0f));
}
// Stable pseudo-random numbers in [0, 1) for the embers, so they do not jump between frames.
float Hash(std::uint32_t n) {
    n = (n << 13) ^ n;
    return static_cast<float>((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu) /
           2147483648.0f;
}

bool AnyInputPressed() {
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false)) {
            return true;
        }
    }
    return ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

float TextWidth(ImFont* font, float px, const char* begin, const char* end = nullptr) {
    return font->CalcTextSizeA(px, FLT_MAX, 0.0f, begin, end).x;
}

// Centred text; UTF-8 is fine here (no letter spacing).
void CenteredText(ImDrawList* list, ImFont* font, float px, float centre_x, float y, ImU32 colour,
                  const char* text) {
    list->AddText(font, px, ImVec2{centre_x - TextWidth(font, px, text) * 0.5f, y}, colour, text);
}

// Centred, letter-spaced ASCII text; each letter can fade in on its own.
template <typename Alpha>
void SpacedText(ImDrawList* list, ImFont* font, float px, float spacing, float centre_x, float y,
                const std::string& text, int r, int g, int b, Alpha&& alpha_of, float rise) {
    float width = 0.0f;
    for (const char& ch : text) {
        width += TextWidth(font, px, &ch, &ch + 1) + spacing;
    }
    width -= spacing;
    float x = centre_x - width * 0.5f;
    for (size_t i = 0; i < text.size(); ++i) {
        const char* ch = &text[i];
        const float a = alpha_of(i);
        list->AddText(font, px, ImVec2{x, y + (1.0f - a) * rise}, Rgba(r, g, b, a), ch, ch + 1);
        x += TextWidth(font, px, ch, ch + 1) + spacing;
    }
}

class IntroLayer final : public ImGui::Layer {
public:
    IntroLayer() {
        AddLayer(this);
    }

    void Draw() override;

    bool ShouldKeepDrawing() override {
        return !done;
    }

private:
    float t = 0.0f;
    float skip_at = -1.0f;
    bool done = false;
};

void IntroLayer::Draw() {
    if (done) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    // A long first frame (shaders compiling) must not eat the start of the animation.
    t += std::min(io.DeltaTime, 1.0f / 20.0f);
    if (skip_at < 0.0f && t > 0.5f && AnyInputPressed()) {
        skip_at = t;
        LOG_INFO(Debug, "BBSC intro: skipped after {:.1f} s", t);
    }
    const float global = skip_at >= 0.0f
                             ? 1.0f - Ramp(t, skip_at, SkipFadeSeconds)
                             : 1.0f - Ramp(t, IntroSeconds - FadeOutSeconds, FadeOutSeconds);
    if (global <= 0.0f) {
        done = true;
        RemoveLayer(this);
        return;
    }

    ImDrawList* list = ImGui::GetForegroundDrawList();
    const ImVec2 size = io.DisplaySize;
    const float s = size.y / 1080.0f;
    const float cx = size.x * 0.5f;
    // The serif from the Windows font folder when imgui_core found one, else the UI font.
    ImFont* big = io.Fonts->Fonts.Size > IMGUI_FONT_BBSC_TITLE ? io.Fonts->Fonts[IMGUI_FONT_BBSC_TITLE]
                                                               : io.Fonts->Fonts[IMGUI_FONT_TEXT_BIG];
    ImFont* text = io.Fonts->Fonts[IMGUI_FONT_TEXT];

    // 1. Black cover with a faint dark-red glow behind the title.
    const float cover = Ramp(t, 0.0f, 0.35f) * global;
    list->AddRectFilled(ImVec2{0.0f, 0.0f}, size, Rgba(0, 0, 0, cover));
    const float glow = Ramp(t, 0.4f, 1.6f) * global;
    for (int ring = 24; ring >= 1; --ring) {
        list->AddCircleFilled(ImVec2{cx, size.y * 0.47f}, ring * 28.0f * s,
                              Rgba(60, 0, 2, 0.024f * glow), 96);
    }

    // 2. Embers rising from the bottom.
    const float embers = Ramp(t, 0.3f, 1.0f) * global;
    for (int i = 0; i < EmberCount; ++i) {
        const float speed = (25.0f + Hash(i + 101) * 55.0f) * s;
        const float travel = size.y * 1.1f;
        const float y = size.y - std::fmod(t * speed + Hash(i + 202) * travel, travel);
        const float x = Hash(i) * size.x + std::sin(t * (0.6f + Hash(i + 303)) + i) * 12.0f * s;
        const float flicker = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(t * (4.0f + Hash(i + 404) * 5.0f) + i));
        const float radius = (0.8f + Hash(i + 505) * 1.8f) * s;
        list->AddCircleFilled(ImVec2{x, y}, radius, Rgba(210, 70, 25, 0.55f * flicker * embers), 8);
    }

    // 3. Title, letter by letter.
    const float title_px = 104.0f * s;
    const float title_y = size.y * 0.40f - title_px * 0.5f;
    SpacedText(
        list, big, title_px, title_px * 0.16f, cx, title_y, "BLOODBORNE", 226, 214, 190,
        [&](size_t i) { return EaseOut(Ramp(t, 0.9f + i * 0.07f, 0.6f)) * global; }, 14.0f * s);

    // 4. The blood thread under it (just below the letters' baseline), and its drips.
    const float line_y = title_y + big->Ascent * (title_px / big->FontSize) + title_px * 0.2f;
    const float half = EaseOut(Ramp(t, 0.6f, 1.3f)) * size.x * 0.30f;
    const float thick = 1.6f * s;
    const ImU32 blood = Rgba(165, 12, 12, global);
    const ImU32 clear = Rgba(165, 12, 12, 0.0f);
    list->AddRectFilledMultiColor(ImVec2{cx - half, line_y - thick}, ImVec2{cx, line_y + thick},
                                  clear, blood, blood, clear);
    list->AddRectFilledMultiColor(ImVec2{cx, line_y - thick}, ImVec2{cx + half, line_y + thick},
                                  blood, clear, clear, blood);
    static constexpr std::array<float, 6> DripX{-0.21f, -0.12f, -0.05f, 0.03f, 0.11f, 0.19f};
    for (size_t k = 0; k < DripX.size(); ++k) {
        const float x = cx + DripX[k] * size.x;
        if (std::abs(x - cx) > half * 0.9f) {
            continue;
        }
        const float since = t - (1.9f + k * 0.23f);
        if (since <= 0.0f) {
            continue;
        }
        const float length = std::min(since * 45.0f, 16.0f) * s;
        const float fall = since > 0.4f ? 0.5f * 900.0f * s * (since - 0.4f) * (since - 0.4f) : 0.0f;
        const float alpha = (1.0f - Ramp(since, 0.8f, 0.8f)) * global;
        const float top = line_y + fall;
        list->AddRectFilled(ImVec2{x - 1.0f * s, top}, ImVec2{x + 1.0f * s, top + length},
                            Rgba(150, 8, 8, alpha));
        list->AddCircleFilled(ImVec2{x, top + length}, 2.4f * s, Rgba(150, 8, 8, alpha), 12);
    }

    // 5. Subtitle, tagline, footer.
    const float sub_px = 40.0f * s;
    const float sub_alpha = EaseOut(Ramp(t, 2.0f, 0.8f)) * global;
    SpacedText(
        list, big, sub_px, sub_px * 0.45f, cx, line_y + 24.0f * s, "SEAMLESS  CO-OP", 185, 22, 22,
        [&](size_t) { return sub_alpha; }, 6.0f * s);
    CenteredText(list, text, 27.0f * s, cx, line_y + 24.0f * s + sub_px * 1.55f,
                 Rgba(170, 160, 148, Ramp(t, 3.0f, 0.9f) * global),
                 "A hunter must hunt. Together.");

    const float foot_alpha = Ramp(t, 3.4f, 0.8f) * global * 0.85f;
    const std::string build = std::string{"BBSC alpha  \xC2\xB7  built "} + __DATE__ +
                              "  \xC2\xB7  a fan-made mod, not affiliated with FromSoftware or "
                              "Sony Interactive Entertainment";
    CenteredText(list, text, 17.0f * s, cx, size.y - 70.0f * s, Rgba(120, 110, 100, foot_alpha),
                 build.c_str());
    CenteredText(list, text, 17.0f * s, cx, size.y - 46.0f * s, Rgba(120, 110, 100, foot_alpha),
                 "shadPS4  \xC2\xB7  shadp2p by Wozzardman  \xC2\xB7  game server groundwork by "
                 "fhody125");
    const char* hint = "any key to skip";
    list->AddText(text, 15.0f * s,
                  ImVec2{size.x - TextWidth(text, 15.0f * s, hint) - 24.0f * s, size.y - 34.0f * s},
                  Rgba(110, 100, 90, Ramp(t, 1.5f, 0.5f) * global * 0.6f), hint);
}

void StartCard() {
    static IntroLayer layer; // lives for the whole run; it stops drawing itself when done
    LOG_INFO(Debug, "BBSC intro: title card queued (SHADPS4_BBSC_INTRO=0 turns it off)");
}

// ---------------------------------------------------------------- the cinematic

constexpr const char* IntroVideoFile = "BBSC-intro.mp4";
constexpr const char* IntroCutsFile = "BBSC-intro-cuts.json";
constexpr size_t MaxQueuedFrames = 12;
constexpr int AudioRate = 48000;
constexpr double AudioEdgeFadeSeconds = 0.03;
constexpr float VideoFadeOutSeconds = 0.7f;
constexpr float VideoSkipFadeSeconds = 0.45f;
constexpr float VideoStartTimeoutSeconds = 8.0f;

// One piece of the timeline: [in, out) of a file shown at `speed`. Its audio plays at normal speed
// from `in` for as long as the piece lasts. `letterbox` (e.g. 2.39) masks the picture to that aspect.
struct PlayItem {
    std::string path;
    double in = 0.0;
    double out = -1.0; // -1: to the end of the file
    double speed = 1.0;
    float letterbox = 0.0f;
    bool audio = true;   // false: silent here (a continuous music bed plays instead)
    double start = 0.0;  // on the timeline
    double length = 0.0; // on the timeline
};

struct VideoFrame {
    double t = 0.0; // timeline seconds
    int item = 0;
    std::vector<std::uint8_t> rgba;
};

double FileDuration(const std::string& path) {
    AVFormatContext* f = nullptr;
    double d = 0.0;
    if (avformat_open_input(&f, path.c_str(), nullptr, nullptr) >= 0) {
        if (avformat_find_stream_info(f, nullptr) >= 0 && f->duration > 0) {
            d = f->duration / static_cast<double>(AV_TIME_BASE);
        }
        avformat_close_input(&f);
    }
    return d;
}

bool VideoSize(const std::string& path, int& w, int& h) {
    AVFormatContext* f = nullptr;
    bool ok = false;
    if (avformat_open_input(&f, path.c_str(), nullptr, nullptr) >= 0) {
        if (avformat_find_stream_info(f, nullptr) >= 0) {
            const int s = av_find_best_stream(f, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            if (s >= 0) {
                w = f->streams[s]->codecpar->width;
                h = f->streams[s]->codecpar->height;
                ok = w > 0 && h > 0;
            }
        }
        avformat_close_input(&f);
    }
    return ok;
}

// The audio of [in, in + length) as 48 kHz stereo float (5.1 is folded down); silence where the file
// has none. Starts and ends with a short fade so cuts do not click.
std::vector<float> DecodeAudioSpan(const std::string& path, double in, double length) {
    const size_t want = static_cast<size_t>(std::llround(length * AudioRate));
    std::vector<float> pcm;
    pcm.reserve(want * 2);
    AVFormatContext* fmt = nullptr;
    AVCodecContext* actx = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    const AVCodec* codec = nullptr;
    int astream = -1;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) >= 0 &&
        avformat_find_stream_info(fmt, nullptr) >= 0) {
        astream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    }
    if (astream >= 0 && codec != nullptr) {
        actx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(actx, fmt->streams[astream]->codecpar);
        AVChannelLayout stereo;
        av_channel_layout_default(&stereo, 2);
        if (avcodec_open2(actx, codec, nullptr) >= 0 &&
            swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, AudioRate, &actx->ch_layout,
                                actx->sample_fmt, actx->sample_rate, 0, nullptr) >= 0 &&
            swr_init(swr) >= 0) {
            const double tb = av_q2d(fmt->streams[astream]->time_base);
            if (in > 0.0) {
                av_seek_frame(fmt, astream, static_cast<std::int64_t>(in / tb), AVSEEK_FLAG_BACKWARD);
            }
            std::vector<float> chunk;
            bool full = false;
            auto drain = [&] {
                while (!full && avcodec_receive_frame(actx, frame) >= 0) {
                    const std::int64_t ts = frame->best_effort_timestamp;
                    const double frame_t = ts != AV_NOPTS_VALUE ? ts * tb : in;
                    const int out_max = swr_get_out_samples(swr, frame->nb_samples);
                    chunk.resize(static_cast<size_t>(out_max) * 2);
                    std::uint8_t* out = reinterpret_cast<std::uint8_t*>(chunk.data());
                    const int got = std::max(
                        0, swr_convert(swr, &out, out_max,
                                       const_cast<const std::uint8_t**>(frame->extended_data),
                                       frame->nb_samples));
                    // drop what lies before `in`
                    const long skip = std::clamp<long>(std::lround((in - frame_t) * AudioRate), 0, got);
                    for (long i = skip; i < got && pcm.size() < want * 2; ++i) {
                        pcm.push_back(chunk[i * 2]);
                        pcm.push_back(chunk[i * 2 + 1]);
                    }
                    full = pcm.size() >= want * 2;
                    av_frame_unref(frame);
                }
            };
            while (!full && av_read_frame(fmt, pkt) >= 0) {
                if (pkt->stream_index == astream && avcodec_send_packet(actx, pkt) >= 0) {
                    drain();
                }
                av_packet_unref(pkt);
            }
        }
    }
    swr_free(&swr);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&actx);
    avformat_close_input(&fmt);
    pcm.resize(want * 2, 0.0f);
    const size_t edge = std::min(want / 2, static_cast<size_t>(AudioEdgeFadeSeconds * AudioRate));
    for (size_t i = 0; i < edge; ++i) {
        const float g = static_cast<float>(i) / static_cast<float>(edge);
        pcm[i * 2] *= g;
        pcm[i * 2 + 1] *= g;
        pcm[(want - 1 - i) * 2] *= g;
        pcm[(want - 1 - i) * 2 + 1] *= g;
    }
    return pcm;
}

// Plays a list of pieces as one timeline: the whole soundtrack is decoded up front (it is short),
// video frames on a thread into a queue that the render thread drains by presentation time.
class PlaylistDecoder {
public:
    explicit PlaylistDecoder(std::vector<PlayItem> list) : items(std::move(list)) {}

    ~PlaylistDecoder() {
        Stop();
    }

    bool Prepare() {
        double t = 0.0;
        for (auto& it : items) {
            if (it.out < 0.0) {
                it.out = FileDuration(it.path);
            }
            it.speed = std::clamp(it.speed, 0.1, 8.0);
            it.length = std::max(0.0, (it.out - it.in) / it.speed);
            it.start = t;
            t += it.length;
        }
        duration = t;
        if (items.empty() || duration <= 0.0 || !VideoSize(items.back().path, width, height)) {
            return false;
        }
        double bed_length = 0.0;
        for (const auto& it : items) {
            std::vector<float> span = it.audio ? DecodeAudioSpan(it.path, it.in, it.length)
                                               : std::vector<float>(static_cast<size_t>(std::llround(
                                                                        it.length * AudioRate)) * 2,
                                                                    0.0f);
            if (!it.audio) {
                bed_length = it.start + it.length;
            }
            pcm.insert(pcm.end(), span.begin(), span.end());
        }
        if (!bed_path.empty() && bed_length > 0.0) {
            const std::vector<float> bed = DecodeAudioSpan(bed_path, bed_from, bed_length);
            for (size_t i = 0; i < bed.size() && i < pcm.size(); ++i) {
                pcm[i] += bed[i];
            }
        }
        return true;
    }

    void Start() {
        thread = std::thread([this] { Run(); });
    }

    void Stop() {
        stop = true;
        cv.notify_all();
        if (thread.joinable()) {
            thread.join();
        }
    }

    bool HasFrame() {
        std::scoped_lock lk{mutex};
        return !queue.empty();
    }

    // The newest frame due at `t`; older ones are dropped. False when nothing new is due.
    bool Take(double t, VideoFrame& out) {
        std::scoped_lock lk{mutex};
        bool got = false;
        while (!queue.empty() && queue.front().t <= t) {
            out = std::move(queue.front());
            queue.pop_front();
            got = true;
        }
        if (got) {
            cv.notify_all();
        }
        return got;
    }

    bool Drained() {
        std::scoped_lock lk{mutex};
        return eof && queue.empty();
    }

    std::vector<PlayItem> items;
    std::string bed_path; // a continuous stretch of sound under the silent pieces
    double bed_from = 0.0;
    int width = 0;
    int height = 0;
    double duration = 0.0;
    std::vector<float> pcm; // interleaved stereo, 48 kHz, the whole timeline

private:
    bool Push(VideoFrame&& f) {
        std::unique_lock lk{mutex};
        cv.wait(lk, [&] { return stop || queue.size() < MaxQueuedFrames; });
        if (stop) {
            return false;
        }
        queue.push_back(std::move(f));
        return true;
    }

    void PlayPiece(int index) {
        const PlayItem& it = items[index];
        AVFormatContext* fmt = nullptr;
        AVCodecContext* vctx = nullptr;
        SwsContext* sws = nullptr;
        AVPacket* pkt = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        const AVCodec* codec = nullptr;
        int vstream = -1;
        if (avformat_open_input(&fmt, it.path.c_str(), nullptr, nullptr) >= 0 &&
            avformat_find_stream_info(fmt, nullptr) >= 0) {
            vstream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        }
        if (vstream >= 0 && codec != nullptr) {
            vctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(vctx, fmt->streams[vstream]->codecpar);
            vctx->thread_count = 4;
        }
        if (vctx && avcodec_open2(vctx, codec, nullptr) >= 0) {
            const double tb = av_q2d(fmt->streams[vstream]->time_base);
            if (it.in > 0.0) {
                av_seek_frame(fmt, vstream, static_cast<std::int64_t>(it.in / tb), AVSEEK_FLAG_BACKWARD);
            }
            bool done = false;
            auto drain = [&] {
                while (!done && !stop && avcodec_receive_frame(vctx, frame) >= 0) {
                    const std::int64_t ts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                                                ? frame->best_effort_timestamp
                                                : frame->pts;
                    const double pts = ts != AV_NOPTS_VALUE ? ts * tb : it.in;
                    if (pts >= it.out - 1e-4) {
                        done = true;
                    } else if (pts >= it.in - 1e-3) {
                        if (!sws) {
                            sws = sws_getContext(frame->width, frame->height,
                                                 static_cast<AVPixelFormat>(frame->format), width,
                                                 height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr,
                                                 nullptr, nullptr);
                            if (sws) {
                                const int matrix =
                                    frame->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_DEFAULT;
                                const int* coefs = sws_getCoefficients(matrix);
                                sws_setColorspaceDetails(sws, coefs,
                                                         frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0,
                                                         coefs, 1, 0, 1 << 16, 1 << 16);
                            }
                        }
                        if (sws) {
                            VideoFrame out;
                            out.t = it.start + std::max(0.0, pts - it.in) / it.speed;
                            out.item = index;
                            out.rgba.resize(static_cast<size_t>(width) * height * 4);
                            std::uint8_t* dst[1] = {out.rgba.data()};
                            const int stride[1] = {width * 4};
                            sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, stride);
                            if (!Push(std::move(out))) {
                                done = true;
                            }
                        }
                    }
                    av_frame_unref(frame);
                }
            };
            while (!done && !stop && av_read_frame(fmt, pkt) >= 0) {
                if (pkt->stream_index == vstream && avcodec_send_packet(vctx, pkt) >= 0) {
                    drain();
                }
                av_packet_unref(pkt);
            }
            if (!done && !stop) {
                avcodec_send_packet(vctx, nullptr);
                drain();
            }
        } else {
            LOG_WARNING(Debug, "BBSC intro: cannot decode video of {}", it.path);
        }
        if (sws) {
            sws_freeContext(sws);
        }
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&vctx);
        avformat_close_input(&fmt);
    }

    void Run() {
        for (int i = 0; i < static_cast<int>(items.size()) && !stop; ++i) {
            PlayPiece(i);
        }
        std::scoped_lock lk{mutex};
        eof = true;
    }

    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<VideoFrame> queue;
    bool eof = false;
    std::atomic<bool> stop{false};
};

class VideoLayer final : public ImGui::Layer {
public:
    VideoLayer(std::vector<PlayItem> list, std::string bed_path, double bed_from) : decoder(std::move(list)) {
        decoder.bed_path = std::move(bed_path);
        decoder.bed_from = bed_from;
        if (!decoder.Prepare()) {
            LOG_WARNING(Debug, "BBSC intro: cannot prepare the intro video, showing the title card");
            failed = true;
            return;
        }
        decoder.Start();
        LOG_INFO(Debug, "BBSC intro: playing {} piece(s), {:.1f} s, {}x{}", decoder.items.size(),
                 decoder.duration, decoder.width, decoder.height);
        AddLayer(this);
    }

    bool Failed() const {
        return failed;
    }

    void Draw() override;

    bool ShouldKeepDrawing() override {
        return !done;
    }

private:
    bool Begin() {
        if (!ImGui::Vulkan::CreateStreamingTexture(texture, decoder.width, decoder.height)) {
            return false;
        }
        if (!decoder.pcm.empty()) {
            if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
                own_audio = SDL_InitSubSystem(SDL_INIT_AUDIO);
            }
            const SDL_AudioSpec spec{SDL_AUDIO_F32LE, 2, AudioRate};
            audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (audio) {
                SDL_PutAudioStreamData(audio, decoder.pcm.data(),
                                       static_cast<int>(decoder.pcm.size() * sizeof(float)));
                SDL_FlushAudioStream(audio);
                SDL_ResumeAudioStreamDevice(audio);
            } else {
                LOG_WARNING(Debug, "BBSC intro: no audio device: {}", SDL_GetError());
            }
        }
        start = std::chrono::steady_clock::now();
        return true;
    }

    void End() {
        if (audio) {
            SDL_DestroyAudioStream(audio);
            audio = nullptr;
        }
        if (own_audio) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            own_audio = false;
        }
        ImGui::Vulkan::DestroyStreamingTexture(texture);
        decoder.Stop();
        decoder.pcm.clear();
        decoder.pcm.shrink_to_fit();
        done = true;
        RemoveLayer(this);
    }

    PlaylistDecoder decoder;
    ImGui::Vulkan::StreamingTexture texture;
    SDL_AudioStream* audio = nullptr;
    bool own_audio = false;
    bool failed = false;
    bool playing = false;
    bool has_image = false;
    int shown_item = -1;
    bool done = false;
    float wait = 0.0f;
    float fade_from = -1.0f;
    float fade_length = VideoFadeOutSeconds;
    std::chrono::steady_clock::time_point start;
};

void VideoLayer::Draw() {
    if (done) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    ImDrawList* list = ImGui::GetForegroundDrawList();
    const ImVec2 size = io.DisplaySize;

    if (!playing) {
        // Black over the boot screen until the first frame is decoded.
        list->AddRectFilled(ImVec2{0.0f, 0.0f}, size, IM_COL32(0, 0, 0, 255));
        wait += std::min(io.DeltaTime, 0.1f);
        if (decoder.HasFrame()) {
            playing = Begin();
            if (!playing) {
                LOG_WARNING(Debug, "BBSC intro: could not create the video texture");
                End();
                StartCard();
                return;
            }
        } else if (wait > VideoStartTimeoutSeconds || decoder.Drained()) {
            LOG_WARNING(Debug, "BBSC intro: no video frames, showing the title card");
            End();
            StartCard();
            return;
        }
        if (!playing) {
            return;
        }
    }

    const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
    if (fade_from < 0.0f && t > 0.5f && AnyInputPressed()) {
        fade_from = t;
        fade_length = VideoSkipFadeSeconds;
        LOG_INFO(Debug, "BBSC intro: skipped after {:.1f} s", t);
    }
    const float length = static_cast<float>(decoder.duration);
    if (fade_from < 0.0f && t >= length) {
        fade_from = t;
        fade_length = VideoFadeOutSeconds;
    }
    const float alpha = fade_from < 0.0f ? 1.0f : 1.0f - Ramp(t, fade_from, fade_length);
    if (audio) {
        SDL_SetAudioStreamGain(audio, alpha);
    }
    if (alpha <= 0.0f) {
        LOG_INFO(Debug, "BBSC intro: finished");
        End();
        return;
    }

    VideoFrame frame;
    if (decoder.Take(t, frame)) {
        ImGui::Vulkan::UpdateStreamingTexture(texture, frame.rgba.data());
        has_image = true;
        shown_item = frame.item;
    }
    list->AddRectFilled(ImVec2{0.0f, 0.0f}, size, Rgba(0, 0, 0, alpha));
    if (has_image) {
        // Fit the frame inside the window, keeping its aspect ratio.
        const float aspect = static_cast<float>(decoder.width) / static_cast<float>(decoder.height);
        float w = size.x;
        float h = w / aspect;
        if (h > size.y) {
            h = size.y;
            w = h * aspect;
        }
        const ImVec2 p0{(size.x - w) * 0.5f, (size.y - h) * 0.5f};
        const ImVec2 p1{p0.x + w, p0.y + h};
        list->AddImage(texture.im_texture, p0, p1, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f},
                       Rgba(255, 255, 255, alpha));
        // Pieces taken from the game's own trailer get the same 2.39:1 bars as our film.
        const float box = shown_item >= 0 ? decoder.items[shown_item].letterbox : 0.0f;
        if (box > 0.0f && w / box < h) {
            const float bar = (h - w / box) * 0.5f;
            list->AddRectFilled(p0, ImVec2{p1.x, p0.y + bar}, Rgba(0, 0, 0, alpha));
            list->AddRectFilled(ImVec2{p0.x, p1.y - bar}, p1, Rgba(0, 0, 0, alpha));
        }
    }
    const float s = size.y / 1080.0f;
    ImFont* text = io.Fonts->Fonts[IMGUI_FONT_TEXT];
    const char* hint = "any key to skip";
    const float hint_alpha = Ramp(t, 1.0f, 0.5f) * (1.0f - Ramp(t, 4.0f, 1.0f)) * alpha * 0.5f;
    if (hint_alpha > 0.0f) {
        list->AddText(text, 15.0f * s,
                      ImVec2{size.x - TextWidth(text, 15.0f * s, hint) - 24.0f * s, size.y - 34.0f * s},
                      Rgba(150, 140, 130, hint_alpha), hint);
    }
}

std::filesystem::path ExeDir() {
    const char* base = SDL_GetBasePath();
    return base ? std::filesystem::path{base} : std::filesystem::current_path();
}

// BBSC-intro.mp4 (ours) closes the intro. BBSC-intro-cuts.json, when present, puts shots from the
// player's own copy of the game in front of it - nothing of the game is shipped with BBSC:
//   {"source": "/app0/dvdroot_ps4/movie/sprj_advertise.mp4", "letterbox": 2.39,
//    "music_from": 97.9 (optional), "shots": [{"in": 31.2, "out": 33.2, "speed": 1.0}, ...]}
std::vector<PlayItem> BuildPlaylist(std::string& bed_path, double& bed_from) {
    std::vector<PlayItem> list;
    std::error_code ec;
    const auto own = ExeDir() / IntroVideoFile;
    if (!std::filesystem::is_regular_file(own, ec)) {
        return list;
    }
    const auto cuts = ExeDir() / IntroCutsFile;
    if (std::filesystem::is_regular_file(cuts, ec)) {
        try {
            std::ifstream in(cuts);
            const nlohmann::json j = nlohmann::json::parse(in);
            const std::string guest = j.value("source", std::string{});
            const float box = j.value("letterbox", 0.0f);
            // "music_from": seconds - play the source's sound continuously from there under all the
            // shots instead of each shot's own sound
            const double music_from = j.value("music_from", -1.0);
            auto* mnt = Common::Singleton<Core::FileSys::MntPoints>::Instance();
            const std::filesystem::path host = mnt->GetHostPath(guest);
            if (!guest.empty() && std::filesystem::is_regular_file(host, ec)) {
                for (const auto& shot : j.at("shots")) {
                    PlayItem it;
                    it.path = host.string();
                    it.in = shot.value("in", 0.0);
                    it.out = shot.value("out", 0.0);
                    it.speed = shot.value("speed", 1.0);
                    it.letterbox = box;
                    it.audio = music_from < 0.0;
                    if (it.out > it.in) {
                        list.push_back(it);
                    }
                }
                if (music_from >= 0.0) {
                    bed_path = host.string();
                    bed_from = music_from;
                }
                LOG_INFO(Debug, "BBSC intro: {} shot(s) from {}", list.size(), guest);
            } else {
                LOG_WARNING(Debug, "BBSC intro: {} not found in this game copy, skipping those shots", guest);
            }
        } catch (const std::exception& e) {
            LOG_WARNING(Debug, "BBSC intro: cannot read {}: {}", IntroCutsFile, e.what());
            list.clear();
        }
    }
    PlayItem ours;
    ours.path = own.string();
    list.push_back(ours);
    return list;
}

} // namespace

void StartIntro(std::string_view serial) {
    if (serial != "CUSA03173" || !SwitchEnabled("SHADPS4_BBSC_INTRO")) {
        return;
    }
    std::string bed_path;
    double bed_from = 0.0;
    std::vector<PlayItem> list = BuildPlaylist(bed_path, bed_from);
    if (!list.empty()) {
        // lives for the whole run; frees its resources when done
        static VideoLayer layer{std::move(list), std::move(bed_path), bed_from};
        if (!layer.Failed()) {
            return;
        }
    }
    StartCard();
}

} // namespace Core::Bbsc
