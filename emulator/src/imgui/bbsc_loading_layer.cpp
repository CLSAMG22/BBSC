// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The BBSC loading screen (2026-09-27). A guest's summon load, the cross-map second reload and every
// follow load run behind the load cover (bloodborne_re.cpp, TickBbscLoadCover), which holds the
// game's fade plate black - 20-30 s of plain black for a cross-map summon. The user asked for a
// loading screen there instead. The game's own "Now Loading" screen cannot be opened on demand (the
// death curtain's calls to 0x0176D380 never showed it), so this one is drawn by the overlay, in the
// intro card's style: dark cover with a faint red glow and embers, a line saying where we are going,
// a co-op hint that changes every few seconds, and "Now Loading" with a pulsing blood drop at the
// bottom right. It fades in over the cover and out as the cover lifts.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

#include <imgui.h>

#include "common/logging/log.h"
#include "imgui/bbsc_loading_layer.h"
#include "imgui/imgui_layer.h"
#include "imgui/imgui_std.h"

namespace Core::Bbsc {
namespace {

constexpr float FadeInSeconds = 0.6f;
constexpr float FadeOutSeconds = 0.8f;
constexpr float HintSeconds = 7.0f;
constexpr int EmberCount = 36;

constexpr std::array<const char*, 8> Hints{{
    "When the host travels by lamp, you travel with them.",
    "The host's world is theirs: its bosses, doors and shortcuts are as they left them.",
    "Your own world is untouched while you hunt in another's.",
    "What you pick up in the host's world is yours to keep.",
    "The Beckoning Bell costs no Insight here.",
    "Up to five hunters may share one night.",
    "Fall in the hunt and you wake at the host's lamp, still together.",
    "In the host's Hunter's Dream, the Doll and the workshop serve you too.",
}};

float Clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}
ImU32 Rgba(int r, int g, int b, float a) {
    return IM_COL32(r, g, b, static_cast<int>(Clamp01(a) * 255.0f));
}
float Hash(std::uint32_t n) {
    n = (n << 13) ^ n;
    return static_cast<float>((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu) /
           2147483648.0f;
}
float TextWidth(ImFont* font, float px, const char* text) {
    return font->CalcTextSizeA(px, FLT_MAX, 0.0f, text).x;
}

std::atomic<bool> wanted{false};
std::atomic<int> wanted_kind{0};

class LoadingLayer final : public ImGui::Layer {
public:
    void Draw() override;
    bool ShouldKeepDrawing() override {
        return wanted.load(std::memory_order_relaxed) || alpha > 0.0f;
    }

private:
    float t = 0.0f;     // seconds since this screen came up
    float alpha = 0.0f; // the whole screen's opacity
    int first_hint = 0;
};

void LoadingLayer::Draw() {
    const ImGuiIO& io = ImGui::GetIO();
    const float dt = std::min(io.DeltaTime, 1.0f / 20.0f);
    const bool on = wanted.load(std::memory_order_relaxed);
    if (on && alpha <= 0.0f) {
        t = 0.0f; // a new load: start the hints over at a different one
        first_hint = (first_hint + 3) % static_cast<int>(Hints.size());
    }
    alpha = Clamp01(alpha + (on ? dt / FadeInSeconds : -dt / FadeOutSeconds));
    if (alpha <= 0.0f) {
        return;
    }
    t += dt;

    ImDrawList* list = ImGui::GetForegroundDrawList();
    const ImVec2 size = io.DisplaySize;
    const float s = size.y / 1080.0f;
    const float cx = size.x * 0.5f;
    ImFont* big = io.Fonts->Fonts.Size > IMGUI_FONT_BBSC_TITLE ? io.Fonts->Fonts[IMGUI_FONT_BBSC_TITLE]
                                                               : io.Fonts->Fonts[IMGUI_FONT_TEXT_BIG];
    ImFont* text = io.Fonts->Fonts[IMGUI_FONT_TEXT];

    // Cover, glow and embers.
    list->AddRectFilled(ImVec2{0.0f, 0.0f}, size, Rgba(0, 0, 0, alpha));
    for (int ring = 20; ring >= 1; --ring) {
        list->AddCircleFilled(ImVec2{cx, size.y * 0.44f}, ring * 30.0f * s,
                              Rgba(55, 0, 2, 0.02f * alpha), 96);
    }
    for (int i = 0; i < EmberCount; ++i) {
        const float speed = (20.0f + Hash(i + 101) * 45.0f) * s;
        const float travel = size.y * 1.1f;
        const float y = size.y - std::fmod(t * speed + Hash(i + 202) * travel, travel);
        const float x = Hash(i) * size.x + std::sin(t * (0.6f + Hash(i + 303)) + i) * 12.0f * s;
        const float flicker =
            0.35f + 0.65f * (0.5f + 0.5f * std::sin(t * (4.0f + Hash(i + 404) * 5.0f) + i));
        list->AddCircleFilled(ImVec2{x, y}, (0.8f + Hash(i + 505) * 1.6f) * s,
                              Rgba(210, 70, 25, 0.45f * flicker * alpha), 8);
    }

    // Where we are going.
    const bool summon = wanted_kind.load(std::memory_order_relaxed) == 0;
    const char* title = summon ? "Crossing into another hunter's world" : "Following the hunt";
    const float title_px = 46.0f * s;
    const float title_y = size.y * 0.42f;
    list->AddText(big, title_px, ImVec2{cx - TextWidth(big, title_px, title) * 0.5f, title_y},
                  Rgba(214, 202, 180, alpha), title);
    const float line_y = title_y + title_px * 1.35f;
    const float half = size.x * 0.16f;
    const ImU32 blood = Rgba(160, 12, 12, alpha);
    const ImU32 clear = Rgba(160, 12, 12, 0.0f);
    list->AddRectFilledMultiColor(ImVec2{cx - half, line_y - 1.2f * s}, ImVec2{cx, line_y + 1.2f * s},
                                  clear, blood, blood, clear);
    list->AddRectFilledMultiColor(ImVec2{cx, line_y - 1.2f * s}, ImVec2{cx + half, line_y + 1.2f * s},
                                  blood, clear, clear, blood);

    // The hint, crossfading every HintSeconds.
    const int step = static_cast<int>(t / HintSeconds);
    const float within = std::fmod(t, HintSeconds);
    const float hint_alpha =
        Clamp01(within / 0.8f) * Clamp01((HintSeconds - within) / 0.8f) * alpha;
    const char* hint = Hints[(first_hint + step) % Hints.size()];
    const float hint_px = 25.0f * s;
    list->AddText(text, hint_px, ImVec2{cx - TextWidth(text, hint_px, hint) * 0.5f, line_y + 26.0f * s},
                  Rgba(165, 155, 142, 0.9f * hint_alpha), hint);

    // "Now Loading" and a pulsing blood drop, bottom right, like the game's own.
    const char* loading = "Now Loading";
    const float load_px = 26.0f * s;
    const float lx = size.x - TextWidth(big, load_px, loading) - 70.0f * s;
    const float ly = size.y - 80.0f * s;
    list->AddText(big, load_px, ImVec2{lx, ly}, Rgba(200, 190, 172, 0.85f * alpha), loading);
    const float pulse = 0.5f + 0.5f * std::sin(t * 3.2f);
    const ImVec2 drop{lx - 26.0f * s, ly + load_px * 0.62f};
    list->AddCircleFilled(drop, (5.0f + 1.5f * pulse) * s, Rgba(150, 8, 8, (0.55f + 0.45f * pulse) * alpha),
                          20);
    list->AddTriangleFilled(ImVec2{drop.x - 4.2f * s, drop.y - 2.5f * s},
                            ImVec2{drop.x + 4.2f * s, drop.y - 2.5f * s},
                            ImVec2{drop.x, drop.y - 13.0f * s},
                            Rgba(150, 8, 8, (0.55f + 0.45f * pulse) * alpha));
}

LoadingLayer& Layer() {
    static LoadingLayer layer;
    static const bool added = [] {
        ImGui::Layer::AddLayer(&layer); // stays; draws nothing while no load is on
        return true;
    }();
    (void)added;
    return layer;
}

} // namespace

void ShowLoadingScreen(LoadingScreenKind kind) {
    Layer();
    wanted_kind.store(kind == LoadingScreenKind::Summon ? 0 : 1, std::memory_order_relaxed);
    if (!wanted.exchange(true, std::memory_order_relaxed)) {
        LOG_INFO(Debug, "BBSC loading screen: shown ({})",
                 kind == LoadingScreenKind::Summon ? "summon" : "follow");
    }
}

void HideLoadingScreen() {
    if (wanted.exchange(false, std::memory_order_relaxed)) {
        LOG_INFO(Debug, "BBSC loading screen: hidden");
    }
}

} // namespace Core::Bbsc
