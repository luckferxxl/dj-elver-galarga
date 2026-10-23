#include "widgets.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace rdj::ui {

ImVec4 toVec4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

namespace col {
ImU32 mix(ImU32 a, ImU32 b, float k) {
    k = std::clamp(k, 0.f, 1.f);
    auto ch = [&](int sh) { return (int)std::lround(((a >> sh) & 255) * (1 - k) + ((b >> sh) & 255) * k); };
    return IM_COL32(ch(IM_COL32_R_SHIFT), ch(IM_COL32_G_SHIFT), ch(IM_COL32_B_SHIFT), (a >> IM_COL32_A_SHIFT) & 255);
}
ImU32 onColor(ImU32 c) {
    const float r = ((c >> IM_COL32_R_SHIFT) & 255) / 255.f, g = ((c >> IM_COL32_G_SHIFT) & 255) / 255.f, b = ((c >> IM_COL32_B_SHIFT) & 255) / 255.f;
    const float lum = 0.299f * r + 0.587f * g + 0.114f * b;
    return lum > 0.55f ? IM_COL32(8, 12, 18, 255) : IM_COL32(250, 252, 255, 255);
}
ImU32 camelot(int n) {
    // n = número Camelot * 2 + (B ? 1 : 0); -1 = desconocida.
    // Colores de la rueda Camelot (los que usan Mixed In Key y VirtualDJ en modo
    // armónico): turquesa en el 1 y vuelta al arco iris hasta el cian del 12.
    // El anillo A (menores) va más saturado y el B (mayores) más claro.
    if (n < 2) return TextDim;
    static const ImU32 A[12] = {IM_COL32(0, 214, 186, 255),  IM_COL32(52, 214, 92, 255),   IM_COL32(150, 214, 46, 255),  IM_COL32(240, 200, 40, 255),
                                IM_COL32(255, 150, 44, 255), IM_COL32(255, 98, 72, 255),   IM_COL32(255, 72, 128, 255),  IM_COL32(226, 82, 208, 255),
                                IM_COL32(168, 104, 255, 255), IM_COL32(104, 124, 255, 255), IM_COL32(44, 162, 255, 255), IM_COL32(0, 200, 238, 255)};
    const int num = std::clamp(n / 2, 1, 12);
    const bool major = n & 1;
    const ImU32 c = A[num - 1];
    return major ? mix(c, IM_COL32(255, 255, 255, 255), 0.38f) : c;
}
} // namespace col

// ------------------------------------------------------------------ temas

namespace {
#define C(r, g, b) IM_COL32(r, g, b, 255)
// Diseños 0.6.1. Inspirados en el software profesional (paneles planos, ondas
// sobre fondo casi negro, dos colores de plato bien distintos) y con la
// identidad de Ramiro DJ (azul noche, turquesa y coral) como diseño por defecto.
const Theme kThemes[] = {
    {"Noche clásica", "Azul noche, turquesa y coral: el diseño clásico, limpio y con mucho contraste",
     C(8, 12, 18), C(14, 19, 27), C(21, 28, 39), C(34, 43, 57), C(234, 239, 246), C(128, 141, 160),
     C(38, 214, 186), C(255, 99, 92), C(124, 104, 255), C(54, 211, 140), C(255, 170, 64), C(255, 77, 77),
     C(9, 15, 22), C(14, 12, 19), C(52, 120, 255), C(255, 166, 60), C(236, 243, 255), C(5, 8, 13), 0.15f, 10.f, 6.f},
    {"Estudio", "Negro grafito con azul y naranja, ondas de tres bandas: estilo cabina profesional",
     C(13, 13, 15), C(22, 23, 26), C(31, 33, 37), C(45, 48, 54), C(236, 238, 242), C(138, 143, 152),
     C(37, 140, 255), C(255, 138, 30), C(172, 120, 255), C(48, 213, 120), C(255, 190, 60), C(255, 70, 70),
     C(13, 13, 15), C(13, 13, 15), C(30, 105, 255), C(255, 160, 30), C(255, 255, 255), C(4, 4, 6), 0.f, 4.f, 3.f},
    {"Club", "Negro puro, cian y magenta, ondas de colores por frecuencia (estilo club)",
     C(6, 6, 8), C(15, 15, 19), C(24, 24, 30), C(38, 38, 47), C(240, 240, 246), C(140, 140, 156),
     C(0, 190, 255), C(255, 45, 125), C(255, 206, 0), C(0, 230, 118), C(255, 196, 0), C(255, 59, 48),
     C(0, 18, 28), C(28, 0, 14), C(255, 46, 46), C(46, 255, 96), C(70, 130, 255), C(0, 0, 0), 0.35f, 6.f, 4.f},
    {"Grafito", "Gris medio, azul y rojo, letras grandes y claras: cómodo con mucha luz",
     C(28, 30, 34), C(38, 41, 47), C(49, 53, 60), C(66, 71, 80), C(244, 246, 249), C(160, 167, 178),
     C(64, 160, 255), C(255, 86, 86), C(150, 120, 255), C(70, 210, 120), C(255, 186, 70), C(240, 70, 60),
     C(28, 30, 34), C(28, 30, 34), C(64, 150, 255), C(255, 190, 90), C(250, 250, 255), C(17, 18, 21), 0.f, 6.f, 4.f},
    {"Alto contraste", "Negro y blanco con cian y amarillo: máxima legibilidad en cabinas oscuras",
     C(0, 0, 0), C(10, 10, 11), C(24, 24, 26), C(78, 78, 82), C(255, 255, 255), C(180, 180, 186),
     C(0, 220, 255), C(255, 204, 0), C(230, 230, 236), C(0, 255, 128), C(255, 204, 0), C(255, 64, 64),
     C(0, 0, 0), C(0, 0, 0), C(0, 200, 255), C(255, 204, 0), C(255, 255, 255), C(0, 0, 0), 0.f, 3.f, 2.f},
    // 0.10.3: los tres diseños de Ramiro MixLab 1.0 (mismos colores que allí).
    {"MixLab Carbon", "Grafito, verde lima y azul hielo; carátulas en los jogs",
     C(10, 12, 15), C(18, 21, 25), C(28, 32, 38), C(47, 54, 63), C(234, 239, 246), C(128, 141, 160),
     C(190, 241, 76), C(92, 193, 255), C(190, 241, 76), C(54, 211, 140), C(255, 170, 64), C(255, 77, 77),
     C(9, 15, 22), C(14, 12, 19), C(52, 120, 255), C(255, 166, 60), C(236, 243, 255), C(5, 8, 13), 0.15f, 10.f, 6.f},
    {"MixLab Aurora", "Cian y violeta sobre negro azulado; carátulas en los jogs",
     C(6, 8, 18), C(12, 16, 31), C(22, 28, 46), C(48, 58, 84), C(236, 244, 255), C(148, 163, 187),
     C(42, 222, 245), C(191, 121, 255), C(191, 121, 255), C(70, 225, 155), C(255, 188, 80), C(255, 84, 105),
     C(7, 22, 31), C(24, 12, 35), C(42, 170, 245), C(191, 121, 255), C(236, 244, 255), C(4, 6, 14), 0.5f, 3.f, 3.f},
    {"MixLab Studio", "Grafito cálido, ámbar y azul; controles planos y texto de alto contraste",
     C(18, 17, 16), C(29, 28, 26), C(42, 41, 38), C(68, 66, 60), C(246, 242, 229), C(168, 164, 153),
     C(255, 190, 88), C(100, 179, 235), C(255, 190, 88), C(120, 216, 156), C(255, 190, 88), C(255, 104, 92),
     C(24, 20, 14), C(17, 22, 28), C(100, 179, 235), C(255, 190, 88), C(246, 242, 229), C(11, 10, 9), 0.f, 2.f, 2.f},
};
#undef C
int g_theme = 0;
float S() { return ImGui::GetFontSize() / 16.f; }
} // namespace

// Logotipo de DJ ELVER GALARGA: disco con su surco de color y la aguja, «DJ» en una placa y el
// nombre en dos tonos. pos = borde izquierdo y centro vertical; h = alto del disco. Devuelve el ancho.
float DrawAppLogo(ImDrawList* dl, ImVec2 pos, float h, ImFont* bold, ImFont* head, const char* version) {
    const float r = h * 0.5f;
    const ImVec2 c(pos.x + r, pos.y);
    // Disco
    dl->AddCircleFilled(c, r, col::mix(col::Bg, IM_COL32(0, 0, 0, 255), 0.4f), 48);
    for (int i = 1; i <= 3; ++i) dl->AddCircle(c, r * (0.48f + 0.15f * i), IM_COL32(255, 255, 255, 22), 48, std::max(1.f, r * 0.04f));
    dl->PathArcTo(c, r * 0.86f, -IM_PI * 0.5f, IM_PI * 1.05f, 40);
    dl->PathStroke(col::DeckA, 0, std::max(1.5f, r * 0.16f));
    dl->AddCircleFilled(c, r * 0.34f, col::DeckA, 32);
    dl->AddCircleFilled(c, r * 0.09f, col::Bg, 16);
    // Aguja (con la punta del color del plato B)
    const ImVec2 t0(c.x + r * 0.95f, c.y - r * 0.95f), t1(c.x + r * 0.42f, c.y - r * 0.12f);
    dl->AddLine(t0, t1, col::Text, std::max(1.2f, r * 0.11f));
    dl->AddCircleFilled(t1, std::max(1.5f, r * 0.14f), col::DeckB, 12);
    dl->AddCircleFilled(t0, std::max(1.5f, r * 0.13f), col::Text, 12);
    float x = pos.x + 2 * r + r * 0.45f;
    // «DJ» en una placa
    const float fs = h * 0.78f;
    const float djFs = h * 0.5f;
    const ImVec2 djSz = head->CalcTextSizeA(djFs, FLT_MAX, 0, "DJ");
    const ImVec2 pa(x, pos.y - djFs * 0.62f), pb(x + djSz.x + djFs * 0.6f, pos.y + djFs * 0.62f);
    dl->AddRectFilled(pa, pb, col::DeckA, djFs * 0.22f);
    dl->AddText(head, djFs, ImVec2(pa.x + djFs * 0.3f, pos.y - djSz.y * 0.5f), col::onColor(col::DeckA), "DJ");
    x = pb.x + h * 0.22f;
    dl->AddText(bold, fs, ImVec2(x, pos.y - fs * 0.56f), col::Text, "ELVER");
    x += bold->CalcTextSizeA(fs, FLT_MAX, 0, "ELVER ").x;
    dl->AddText(bold, fs, ImVec2(x, pos.y - fs * 0.56f), col::DeckA, "GALARGA");
    x += bold->CalcTextSizeA(fs, FLT_MAX, 0, "GALARGA").x;
    if (version && *version) {
        x += h * 0.25f;
        const float vf = h * 0.42f;
        dl->AddText(head, vf, ImVec2(x, pos.y - vf * 0.1f), col::TextDim, version);
        x += head->CalcTextSizeA(vf, FLT_MAX, 0, version).x;
    }
    return x - pos.x;
}

int themeCount() { return (int)(sizeof(kThemes) / sizeof(kThemes[0])); }
const Theme& themeAt(int i) { return kThemes[std::clamp(i, 0, themeCount() - 1)]; }
int currentThemeIndex() { return g_theme; }
const Theme& currentTheme() { return kThemes[g_theme]; }

void applyTheme(int i) {
    g_theme = std::clamp(i, 0, themeCount() - 1);
    const Theme& t = kThemes[g_theme];
    col::Bg = t.bg; col::Panel = t.panel; col::Panel2 = t.panel2; col::Line = t.line;
    col::Text = t.text; col::TextDim = t.textDim;
    col::DeckA = t.deckA; col::DeckB = t.deckB; col::Violet = t.accent;
    col::Green = t.green; col::Amber = t.amber; col::Red = t.red;
    col::TintLeft = t.tintLeft; col::TintRight = t.tintRight;
    col::WaveLow = t.waveLow; col::WaveMid = t.waveMid; col::WaveHigh = t.waveHigh; col::WaveBg = t.waveBg;
    col::Glow = t.glow;
    col::Radius = t.radius; col::BtnRadius = t.btnRadius;
    if (!ImGui::GetCurrentContext()) return;
    auto& style = ImGui::GetStyle();
    const float sc = style.FontScaleDpi > 0 ? style.FontScaleDpi : 1.f;
    style.FrameRounding = t.btnRadius * sc;
    style.PopupRounding = std::max(2.f, t.radius * 0.8f) * sc;
    style.GrabRounding = std::max(1.f, t.btnRadius - 1.f) * sc;
    style.WindowRounding = std::max(2.f, t.radius) * sc;
    style.ChildRounding = std::max(2.f, t.radius * 0.8f) * sc;
    style.TabRounding = t.btnRadius * sc;
    style.ScrollbarRounding = t.btnRadius * sc;
    ImVec4* c = style.Colors;
    auto mix = [](ImU32 a, ImU32 b, float k) { return toVec4(col::mix(a, b, k)); };
    c[ImGuiCol_WindowBg] = toVec4(t.bg);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = toVec4(col::withAlpha(col::mix(t.panel, t.panel2, 0.6f), 252));
    c[ImGuiCol_Border] = toVec4(t.line);
    // Casillas, combos y campos: un tono más claro que el fondo de las ventanas, para que se vean sin marcar.
    const ImU32 frame = col::mix(t.bg, t.panel2, 0.8f);
    c[ImGuiCol_FrameBg] = toVec4(frame);
    c[ImGuiCol_FrameBgHovered] = mix(frame, t.accent, 0.16f);
    c[ImGuiCol_FrameBgActive] = mix(frame, t.accent, 0.26f);
    c[ImGuiCol_Button] = toVec4(t.panel2);
    c[ImGuiCol_ButtonHovered] = mix(t.panel2, t.deckA, 0.22f);
    c[ImGuiCol_ButtonActive] = mix(t.panel2, t.deckA, 0.36f);
    c[ImGuiCol_Header] = mix(t.panel2, t.deckA, 0.26f);
    c[ImGuiCol_HeaderHovered] = mix(t.panel2, t.deckA, 0.14f);
    c[ImGuiCol_HeaderActive] = mix(t.panel2, t.deckA, 0.34f);
    c[ImGuiCol_Tab] = toVec4(t.panel);
    c[ImGuiCol_TabHovered] = mix(t.panel2, t.deckA, 0.30f);
    c[ImGuiCol_TabSelected] = toVec4(t.panel2);
    c[ImGuiCol_TabSelectedOverline] = toVec4(t.deckA);
    c[ImGuiCol_TabDimmedSelected] = toVec4(t.panel2);
    c[ImGuiCol_TitleBg] = toVec4(t.panel);
    c[ImGuiCol_TitleBgActive] = mix(t.panel, t.deckA, 0.16f);
    c[ImGuiCol_TableHeaderBg] = toVec4(t.panel);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.022f);
    c[ImGuiCol_TableBorderStrong] = toVec4(t.line);
    c[ImGuiCol_TableBorderLight] = toVec4(col::withAlpha(t.line, 140));
    c[ImGuiCol_CheckMark] = toVec4(t.deckA);
    c[ImGuiCol_SliderGrab] = toVec4(t.deckA);
    c[ImGuiCol_SliderGrabActive] = toVec4(t.deckB);
    c[ImGuiCol_Separator] = toVec4(t.line);
    c[ImGuiCol_SeparatorHovered] = toVec4(t.deckA);
    c[ImGuiCol_SeparatorActive] = toVec4(t.deckA);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = toVec4(t.line);
    c[ImGuiCol_ScrollbarGrabHovered] = mix(t.line, t.textDim, 0.4f);
    c[ImGuiCol_ScrollbarGrabActive] = toVec4(t.textDim);
    c[ImGuiCol_PlotHistogram] = toVec4(t.accent);
    c[ImGuiCol_Text] = toVec4(t.text);
    c[ImGuiCol_TextDisabled] = toVec4(t.textDim);
    c[ImGuiCol_TextSelectedBg] = toVec4(col::withAlpha(t.deckA, 90));
    c[ImGuiCol_NavCursor] = toVec4(col::withAlpha(t.deckA, 160));
    c[ImGuiCol_DragDropTarget] = toVec4(t.deckA);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
}

// ------------------------------------------------------------------ iconos

void DrawIcon(ImDrawList* dl, Ic ic, ImVec2 c, float sz, ImU32 color, float thick) {
    if (ic == Ic::None) return;
    const float u = sz;
    const float th = thick > 0 ? thick : std::max(1.2f, sz * 0.085f);
    auto P = [&](float x, float y) { return ImVec2(c.x + x * u, c.y + y * u); };
    auto line = [&](float x0, float y0, float x1, float y1, float t = 0) { dl->AddLine(P(x0, y0), P(x1, y1), color, t > 0 ? t : th); };
    auto poly = [&](std::initializer_list<ImVec2> pts, bool closed, bool fill = false) {
        std::vector<ImVec2> v;
        for (auto& p : pts) v.push_back(P(p.x, p.y));
        if (fill) dl->AddConcavePolyFilled(v.data(), (int)v.size(), color);
        else dl->AddPolyline(v.data(), (int)v.size(), color, closed ? ImDrawFlags_Closed : 0, th);
    };
    switch (ic) {
    case Ic::Search:
        dl->AddCircle(P(-0.08f, -0.08f), 0.27f * u, color, 24, th);
        line(0.12f, 0.12f, 0.4f, 0.4f, th * 1.2f);
        break;
    case Ic::Headphones:
        dl->PathArcTo(P(0, 0.06f), 0.36f * u, IM_PI * 1.0f, IM_PI * 2.0f, 20);
        dl->PathStroke(color, 0, th);
        dl->AddRectFilled(P(-0.43f, 0.02f), P(-0.22f, 0.4f), color, 0.06f * u);
        dl->AddRectFilled(P(0.22f, 0.02f), P(0.43f, 0.4f), color, 0.06f * u);
        break;
    case Ic::Gear: {
        dl->AddCircle(c, 0.29f * u, color, 24, th * 1.1f);
        dl->AddCircle(c, 0.11f * u, color, 16, th);
        for (int i = 0; i < 8; ++i) {
            const float a = i * IM_PI / 4;
            dl->AddLine(ImVec2(c.x + std::cos(a) * 0.29f * u, c.y + std::sin(a) * 0.29f * u), ImVec2(c.x + std::cos(a) * 0.44f * u, c.y + std::sin(a) * 0.44f * u), color, th * 1.6f);
        }
        break;
    }
    case Ic::Heart:
    case Ic::HeartFill: {
        // Dos lóbulos (arcos) y la punta; sin juntas en ángulo agudo para que el trazo no haga picos.
        const ImVec2 L = P(-0.19f, -0.1f), R = P(0.19f, -0.1f), T = P(0, 0.38f);
        const float r = 0.21f * u;
        const float a1 = IM_PI * 0.75f, a2 = IM_PI * 1.86f, b1 = IM_PI * 1.14f, b2 = IM_PI * 2.25f;
        if (ic == Ic::HeartFill) {
            dl->AddCircleFilled(L, r, color, 24);
            dl->AddCircleFilled(R, r, color, 24);
            dl->AddRectFilled(P(-0.19f, -0.1f), P(0.19f, 0.06f), color);
            dl->AddTriangleFilled(ImVec2(L.x + std::cos(a1) * r, L.y + std::sin(a1) * r), ImVec2(R.x + std::cos(b2) * r, R.y + std::sin(b2) * r), T, color);
        } else {
            dl->PathLineTo(T);
            dl->PathArcTo(L, r, a1, a2, 18);
            dl->PathStroke(color, 0, th);
            dl->PathArcTo(R, r, b1, b2, 18);
            dl->PathLineTo(T);
            dl->PathStroke(color, 0, th);
        }
        break;
    }
    case Ic::Folder:
        poly({{-0.42f, -0.3f}, {-0.12f, -0.3f}, {-0.03f, -0.19f}, {0.42f, -0.19f}, {0.42f, 0.32f}, {-0.42f, 0.32f}}, true);
        line(-0.42f, -0.08f, 0.42f, -0.08f);
        break;
    case Ic::Star:
    case Ic::StarFill: {
        std::vector<ImVec2> v;
        for (int i = 0; i < 10; ++i) {
            const float a = -IM_PI / 2 + i * IM_PI / 5;
            const float r = (i % 2 ? 0.2f : 0.46f);
            v.push_back(P(std::cos(a) * r, std::sin(a) * r + 0.03f));
        }
        if (ic == Ic::StarFill) dl->AddConcavePolyFilled(v.data(), (int)v.size(), color);
        else dl->AddPolyline(v.data(), (int)v.size(), color, ImDrawFlags_Closed, th);
        break;
    }
    case Ic::List:
        for (int i = -1; i <= 1; ++i) {
            dl->AddCircleFilled(P(-0.34f, i * 0.24f), th * 0.9f, color);
            line(-0.16f, i * 0.24f, 0.42f, i * 0.24f);
        }
        break;
    case Ic::Clock:
        dl->AddCircle(c, 0.4f * u, color, 28, th);
        line(0, 0, 0, -0.24f);
        line(0, 0, 0.17f, 0.1f);
        break;
    case Ic::Note:
        line(-0.16f, 0.26f, -0.16f, -0.3f);
        line(0.32f, 0.18f, 0.32f, -0.38f);
        line(-0.16f, -0.3f, 0.32f, -0.38f, th * 2.0f);
        dl->AddCircleFilled(P(-0.27f, 0.27f), 0.13f * u, color);
        dl->AddCircleFilled(P(0.21f, 0.19f), 0.13f * u, color);
        break;
    case Ic::Drive:
        dl->AddRect(P(-0.43f, -0.2f), P(0.43f, 0.22f), color, 0.08f * u, 0, th);
        dl->AddCircleFilled(P(0.26f, 0.01f), th * 1.1f, color);
        line(-0.3f, 0.01f, 0.05f, 0.01f);
        break;
    case Ic::Monitor:
        dl->AddRect(P(-0.43f, -0.34f), P(0.43f, 0.16f), color, 0.05f * u, 0, th);
        line(0, 0.16f, 0, 0.32f);
        line(-0.2f, 0.34f, 0.2f, 0.34f);
        break;
    case Ic::Plus:
        line(-0.36f, 0, 0.36f, 0);
        line(0, -0.36f, 0, 0.36f);
        break;
    case Ic::Dots:
        for (int i = -1; i <= 1; ++i) dl->AddCircleFilled(P(i * 0.3f, 0), std::max(1.5f, 0.075f * u), color);
        break;
    case Ic::ChevRight: poly({{-0.12f, -0.26f}, {0.14f, 0}, {-0.12f, 0.26f}}, false); break;
    case Ic::ChevLeft: poly({{0.12f, -0.26f}, {-0.14f, 0}, {0.12f, 0.26f}}, false); break;
    case Ic::ChevDown: poly({{-0.26f, -0.12f}, {0, 0.14f}, {0.26f, -0.12f}}, false); break;
    case Ic::ArrowUp:
        line(0, 0.38f, 0, -0.34f);
        poly({{-0.26f, -0.1f}, {0, -0.36f}, {0.26f, -0.1f}}, false);
        break;
    case Ic::ArrowDown:
        line(0, -0.38f, 0, 0.34f);
        poly({{-0.26f, 0.1f}, {0, 0.36f}, {0.26f, 0.1f}}, false);
        break;
    case Ic::Play: dl->AddTriangleFilled(P(-0.24f, -0.36f), P(0.38f, 0), P(-0.24f, 0.36f), color); break;
    case Ic::Stop: dl->AddRectFilled(P(-0.28f, -0.28f), P(0.28f, 0.28f), color, 0.05f * u); break;
    case Ic::StepBack: dl->AddTriangleFilled(P(0.26f, -0.3f), P(-0.2f, 0), P(0.26f, 0.3f), color); break;
    case Ic::StepFwd: dl->AddTriangleFilled(P(-0.26f, -0.3f), P(0.2f, 0), P(-0.26f, 0.3f), color); break;
    case Ic::Rewind:
        dl->AddTriangleFilled(P(0.02f, -0.28f), P(-0.36f, 0), P(0.02f, 0.28f), color);
        dl->AddTriangleFilled(P(0.38f, -0.28f), P(0.0f, 0), P(0.38f, 0.28f), color);
        break;
    case Ic::FastFwd:
        dl->AddTriangleFilled(P(-0.02f, -0.28f), P(0.36f, 0), P(-0.02f, 0.28f), color);
        dl->AddTriangleFilled(P(-0.38f, -0.28f), P(0.0f, 0), P(-0.38f, 0.28f), color);
        break;
    case Ic::Mute:   // altavoz tachado
        poly({{-0.38f, -0.12f}, {-0.2f, -0.12f}, {0.02f, -0.32f}, {0.02f, 0.32f}, {-0.2f, 0.12f}, {-0.38f, 0.12f}}, true, true);
        line(0.14f, -0.14f, 0.38f, 0.14f);
        line(0.14f, 0.14f, 0.38f, -0.14f);
        break;
    case Ic::Solo:   // auricular con una "S": escuchar solo esta pista
        dl->AddCircle(c, 0.34f * u, color, 24, th);
        dl->AddCircleFilled(c, 0.14f * u, color, 16);
        break;
    case Ic::Spinner: {
        const float t = (float)ImGui::GetTime() * 5.f;
        dl->PathArcTo(c, 0.32f * u, t, t + IM_PI * 1.4f, 20);
        dl->PathStroke(color, 0, th * 1.2f);
        break;
    }
    case Ic::Mic2:
        dl->AddRectFilled(P(-0.13f, -0.38f), P(0.13f, 0.08f), color, 0.13f * u);
        dl->PathArcTo(P(0, -0.04f), 0.25f * u, 0, IM_PI, 12);
        dl->PathStroke(color, 0, th);
        line(0, 0.21f, 0, 0.38f);
        line(-0.14f, 0.38f, 0.14f, 0.38f);
        break;
    case Ic::Drum:
        dl->AddEllipse(P(0, -0.16f), ImVec2(0.36f * u, 0.12f * u), color, 0, 20, th);
        line(-0.36f, -0.16f, -0.36f, 0.2f); line(0.36f, -0.16f, 0.36f, 0.2f);
        dl->PathArcTo(P(0, 0.2f), 0.36f * u, 0, IM_PI, 16);
        dl->PathStroke(color, 0, th);
        break;
    case Ic::Bass:
        dl->AddCircleFilled(P(-0.14f, 0.18f), 0.16f * u, color, 16);
        line(0.0f, 0.18f, 0.0f, -0.38f);
        line(0.0f, -0.38f, 0.22f, -0.3f);
        break;
    case Ic::Guitar:
        dl->AddCircle(P(-0.1f, 0.14f), 0.2f * u, color, 16, th);
        line(0.04f, 0.0f, 0.36f, -0.34f);
        break;
    case Ic::Piano:
        dl->AddRect(P(-0.38f, -0.28f), P(0.38f, 0.28f), color, 0.04f * u, 0, th);
        for (int i = -1; i <= 1; ++i) dl->AddRectFilled(P(i * 0.25f - 0.05f, -0.28f), P(i * 0.25f + 0.05f, 0.04f), color);
        break;
    case Ic::Waveform:
        for (int i = 0; i < 7; ++i) { const float hh = (i % 3 == 0 ? 0.34f : i % 2 ? 0.2f : 0.1f); line(-0.33f + i * 0.11f, -hh, -0.33f + i * 0.11f, hh); }
        break;
    case Ic::Pause:
        dl->AddRectFilled(P(-0.3f, -0.34f), P(-0.08f, 0.34f), color, 0.03f * u);
        dl->AddRectFilled(P(0.08f, -0.34f), P(0.3f, 0.34f), color, 0.03f * u);
        break;
    case Ic::Record: dl->AddCircleFilled(c, 0.3f * u, color, 24); break;
    case Ic::Eject:
        dl->AddTriangleFilled(P(-0.36f, 0.08f), P(0, -0.34f), P(0.36f, 0.08f), color);
        dl->AddRectFilled(P(-0.36f, 0.2f), P(0.36f, 0.32f), color);
        break;
    case Ic::Bulb:
        dl->AddCircle(P(0, -0.1f), 0.27f * u, color, 24, th);
        line(-0.12f, 0.27f, 0.12f, 0.27f);
        line(-0.08f, 0.38f, 0.08f, 0.38f);
        line(-0.06f, 0.16f, -0.06f, 0.24f);
        line(0.06f, 0.16f, 0.06f, 0.24f);
        break;
    case Ic::Layers:
        poly({{0, -0.4f}, {0.42f, -0.2f}, {0, 0.0f}, {-0.42f, -0.2f}}, true);
        poly({{-0.42f, 0.02f}, {0, 0.22f}, {0.42f, 0.02f}}, false);
        poly({{-0.42f, 0.2f}, {0, 0.4f}, {0.42f, 0.2f}}, false);
        break;
    case Ic::Mic:
        dl->AddRect(P(-0.13f, -0.42f), P(0.13f, 0.1f), color, 0.13f * u, 0, th);
        dl->PathArcTo(P(0, -0.02f), 0.28f * u, 0.f, IM_PI, 16);
        dl->PathStroke(color, 0, th);
        line(0, 0.26f, 0, 0.4f);
        line(-0.16f, 0.4f, 0.16f, 0.4f);
        break;
    case Ic::Close:
        line(-0.3f, -0.3f, 0.3f, 0.3f);
        line(-0.3f, 0.3f, 0.3f, -0.3f);
        break;
    case Ic::Pencil:
        line(-0.28f, 0.28f, 0.22f, -0.22f, th * 2.2f);
        dl->AddTriangleFilled(P(-0.4f, 0.4f), P(-0.36f, 0.2f), P(-0.2f, 0.36f), color);
        break;
    case Ic::Grip:
        for (int i = -1; i <= 1; ++i)
            for (int k = 0; k < 2; ++k) dl->AddCircleFilled(P(k ? 0.12f : -0.12f, i * 0.25f), std::max(1.2f, 0.065f * u), color);
        break;
    case Ic::Disc:
        dl->AddCircle(c, 0.42f * u, color, 28, th);
        dl->AddCircle(c, 0.24f * u, col::withAlpha(color, 120), 20, th * 0.7f);
        dl->AddCircleFilled(c, 0.08f * u, color);
        break;
    case Ic::Download:
        line(0, -0.38f, 0, 0.14f);
        poly({{-0.22f, -0.06f}, {0, 0.16f}, {0.22f, -0.06f}}, false);
        poly({{-0.4f, 0.12f}, {-0.4f, 0.36f}, {0.4f, 0.36f}, {0.4f, 0.12f}}, false);
        break;
    case Ic::Document:
        poly({{-0.3f, -0.42f}, {0.12f, -0.42f}, {0.32f, -0.22f}, {0.32f, 0.42f}, {-0.3f, 0.42f}}, true);
        line(-0.16f, 0.0f, 0.18f, 0.0f);
        line(-0.16f, 0.18f, 0.18f, 0.18f);
        break;
    case Ic::Globe:
        dl->AddCircle(c, 0.42f * u, color, 28, th);
        dl->AddEllipse(c, ImVec2(0.18f * u, 0.42f * u), color, 0.f, 24, th);
        line(-0.42f, 0, 0.42f, 0);
        break;
    case Ic::PanelLeft:
    case Ic::PanelRight: {
        dl->AddRect(P(-0.42f, -0.34f), P(0.42f, 0.34f), color, 0.06f * u, 0, th);
        const float x = ic == Ic::PanelLeft ? -0.14f : 0.14f;
        line(x, -0.34f, x, 0.34f);
        if (ic == Ic::PanelLeft) dl->AddRectFilled(P(-0.42f, -0.34f), P(x, 0.34f), col::withAlpha(color, 70), 0.06f * u);
        else dl->AddRectFilled(P(x, -0.34f), P(0.42f, 0.34f), col::withAlpha(color, 70), 0.06f * u);
        break;
    }
    case Ic::Info:
        dl->AddCircle(c, 0.42f * u, color, 28, th);
        line(0, -0.04f, 0, 0.24f, th * 1.3f);
        dl->AddCircleFilled(P(0, -0.2f), th * 0.95f, color);
        break;
    case Ic::Sliders:
        for (int i = -1; i <= 1; ++i) {
            line(i * 0.28f, -0.4f, i * 0.28f, 0.4f, th * 0.8f);
            const float y = i == -1 ? 0.15f : i == 0 ? -0.18f : 0.05f;
            dl->AddRectFilled(P(i * 0.28f - 0.1f, y - 0.07f), P(i * 0.28f + 0.1f, y + 0.07f), color, 0.03f * u);
        }
        break;
    case Ic::Refresh:
        dl->PathArcTo(c, 0.34f * u, -IM_PI * 0.35f, IM_PI * 1.45f, 24);
        dl->PathStroke(color, 0, th);
        dl->AddTriangleFilled(P(0.14f, -0.44f), P(0.38f, -0.32f), P(0.14f, -0.14f), color);
        break;
    case Ic::Calendar:
        dl->AddRect(P(-0.4f, -0.3f), P(0.4f, 0.4f), color, 0.06f * u, 0, th);
        line(-0.4f, -0.08f, 0.4f, -0.08f);
        line(-0.2f, -0.42f, -0.2f, -0.2f);
        line(0.2f, -0.42f, 0.2f, -0.2f);
        break;
    case Ic::Usb:
        dl->AddRect(P(-0.22f, -0.1f), P(0.22f, 0.42f), color, 0.04f * u, 0, th);
        dl->AddRect(P(-0.14f, -0.42f), P(0.14f, -0.1f), color, 0.02f * u, 0, th);
        break;
    case Ic::Check: poly({{-0.34f, 0.0f}, {-0.1f, 0.26f}, {0.36f, -0.26f}}, false); break;
    case Ic::Stats:
        dl->AddRectFilled(P(-0.38f, 0.05f), P(-0.18f, 0.4f), color, 0.02f * u);
        dl->AddRectFilled(P(-0.1f, -0.38f), P(0.1f, 0.4f), color, 0.02f * u);
        dl->AddRectFilled(P(0.18f, -0.12f), P(0.38f, 0.4f), color, 0.02f * u);
        break;
    case Ic::Shuffle:
        poly({{-0.42f, -0.24f}, {-0.16f, -0.24f}, {0.16f, 0.24f}, {0.4f, 0.24f}}, false);
        poly({{-0.42f, 0.24f}, {-0.16f, 0.24f}, {0.16f, -0.24f}, {0.4f, -0.24f}}, false);
        dl->AddTriangleFilled(P(0.3f, -0.36f), P(0.46f, -0.24f), P(0.3f, -0.12f), color);
        dl->AddTriangleFilled(P(0.3f, 0.12f), P(0.46f, 0.24f), P(0.3f, 0.36f), color);
        break;
    case Ic::Lock:
        dl->AddRectFilled(P(-0.3f, -0.04f), P(0.3f, 0.4f), color, 0.06f * u);
        dl->PathArcTo(P(0, -0.08f), 0.2f * u, IM_PI, IM_PI * 2, 12);
        dl->PathStroke(color, 0, th * 1.2f);
        line(-0.2f, -0.08f, -0.2f, -0.02f, th * 1.2f);
        line(0.2f, -0.08f, 0.2f, -0.02f, th * 1.2f);
        break;
    case Ic::Bend:
        poly({{-0.42f, 0.2f}, {-0.14f, 0.2f}, {0.14f, -0.2f}, {0.42f, -0.2f}}, false);
        break;
    case Ic::Wave: {
        const float hs[9] = {0.12f, 0.28f, 0.18f, 0.40f, 0.22f, 0.34f, 0.14f, 0.26f, 0.10f};
        for (int i = 0; i < 9; ++i) { const float x = -0.40f + i * 0.1f; line(x, -hs[i], x, hs[i], th * 1.1f); }
        break;
    }
    case Ic::Layout:
        dl->AddRect(P(-0.42f, -0.36f), P(0.42f, 0.36f), color, 0.06f * u, 0, th);
        line(-0.42f, -0.12f, 0.42f, -0.12f);
        line(-0.42f, 0.08f, 0.42f, 0.08f);
        line(0, -0.12f, 0, 0.08f);
        break;
    case Ic::Image:
        dl->AddRect(P(-0.42f, -0.34f), P(0.42f, 0.34f), color, 0.06f * u, 0, th);
        poly({{-0.34f, 0.26f}, {-0.1f, -0.02f}, {0.06f, 0.14f}, {0.18f, 0.02f}, {0.34f, 0.26f}}, false);
        dl->AddCircleFilled(P(0.2f, -0.16f), 0.07f * u, color);
        break;
    default: break;
    }
}

bool IconButton(const char* id, Ic ic, ImVec2 size, ImU32 accent, bool on, bool enabled, float iconScale) {
    if (!enabled) ImGui::BeginDisabled();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(p.x + size.x, p.y + size.y);
    const float r = col::BtnRadius * S();
    if (on) dl->AddRectFilled(p, b, col::withAlpha(accent, act ? 200 : 235), r);
    else if (hov || act) dl->AddRectFilled(p, b, act ? col::Line : col::Panel2, r);
    const ImU32 ic_col = on ? col::onColor(accent) : (enabled ? (hov ? col::Text : col::withAlpha(col::Text, 205)) : col::withAlpha(col::TextDim, 120));
    DrawIcon(dl, ic, ImVec2((p.x + b.x) * 0.5f, (p.y + b.y) * 0.5f), std::min(size.x, size.y) * iconScale, ic_col);
    if (!enabled) ImGui::EndDisabled();
    return clicked;
}

// ------------------------------------------------------------------ controles

static bool handleWheelAndReset(float* v, float vmin, float vmax, float vdefault, bool hovered, float wheelStep) {
    bool changed = false;
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { *v = vdefault; changed = true; }
    if (hovered) {
        const float w = ImGui::GetIO().MouseWheel;
        if (w != 0.f) {
            ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetItemID());
            *v = std::clamp(*v + w * wheelStep * (vmax - vmin), vmin, vmax);
            changed = true;
        }
    }
    return changed;
}

bool Knob(const char* id, const char* label, float* v, float vmin, float vmax, float vdefault, bool bipolar,
          float radius, ImU32 accent, const char* valueText) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float ls = ImGui::GetFontSize() * 0.72f;
    ImFont* f = ImGui::GetFont();
    const float labelH = label && *label ? ls + 2 : 0.f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = std::max(radius * 2.f, label && *label ? f->CalcTextSizeA(ls, FLT_MAX, 0, label).x : 0.f);
    ImGui::InvisibleButton(id, ImVec2(w, radius * 2.f + labelH + 2));
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    bool changed = false;
    if (active && io.MouseDelta.y != 0.f) {
        const float speed = io.KeyShift ? 0.0012f : 0.006f;  // Mayús = ajuste fino
        *v = std::clamp(*v - io.MouseDelta.y * speed * (vmax - vmin), vmin, vmax);
        changed = true;
    }
    changed |= handleWheelAndReset(v, vmin, vmax, vdefault, hovered, io.KeyShift ? 0.005f : 0.03f);

    const ImVec2 c(p.x + w * 0.5f, p.y + radius);
    const float a0 = 0.75f * IM_PI, a1 = 2.25f * IM_PI;
    const float t = (*v - vmin) / (vmax - vmin);
    const float at = a0 + (a1 - a0) * t;
    const float th = std::max(2.f, radius * 0.16f);
    dl->PathArcTo(c, radius - th * 0.5f, a0, a1, 32);
    dl->PathStroke(col::mix(col::Line, accent, 0.28f), 0, th);
    const float tc = bipolar ? (vdefault - vmin) / (vmax - vmin) : 0.f;
    const float ac = a0 + (a1 - a0) * tc;
    if (std::fabs(at - ac) > 0.01f) {
        if (col::Glow > 0.3f) {
            dl->PathArcTo(c, radius - th * 0.5f, std::min(ac, at), std::max(ac, at), 32);
            dl->PathStroke(col::withAlpha(accent, (int)(60 * col::Glow)), 0, th * 2.4f);
        }
        dl->PathArcTo(c, radius - th * 0.5f, std::min(ac, at), std::max(ac, at), 32);
        dl->PathStroke(accent, 0, th);
    }
    const float ri = radius - th - 2.f;
    dl->AddCircleFilled(c, ri, hovered || active ? col::mix(col::Panel2, col::Line, 0.7f) : col::Panel2, 28);
    dl->AddCircle(c, ri, col::withAlpha(col::Line, 255), 28, 1.f);
    dl->AddLine(ImVec2(c.x + std::cos(at) * ri * 0.2f, c.y + std::sin(at) * ri * 0.2f), ImVec2(c.x + std::cos(at) * ri * 0.92f, c.y + std::sin(at) * ri * 0.92f),
                col::Text, std::max(1.5f, radius * 0.12f));
    if (label && *label) {
        const ImVec2 ts = f->CalcTextSizeA(ls, FLT_MAX, 0, label);
        dl->AddText(f, ls, ImVec2(c.x - ts.x * 0.5f, p.y + radius * 2.f + 2), col::TextDim, label);
    }
    if ((hovered || active) && valueText) ImGui::SetTooltip("%s", valueText);
    return changed;
}

bool VFader(const char* id, float* v, float vmin, float vmax, float vdefault, ImVec2 size, ImU32 accent, bool invert) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float s = S();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    const float capH = std::min(18.f * s, size.y * 0.14f);
    const float travel = size.y - capH;
    bool changed = false;
    auto toT = [&](float val) { float t = (val - vmin) / (vmax - vmin); return invert ? t : 1.f - t; };  // 0 = arriba
    if (ImGui::IsItemActivated() && !io.KeyShift) {
        // Clic fuera del tirador: saltar a esa posición.
        const float capY = p.y + toT(*v) * travel;
        if (io.MousePos.y < capY || io.MousePos.y > capY + capH) {
            float t = std::clamp((io.MousePos.y - p.y - capH * 0.5f) / travel, 0.f, 1.f);
            if (!invert) t = 1.f - t;
            *v = vmin + t * (vmax - vmin);
            changed = true;
        }
    }
    if (active && io.MouseDelta.y != 0.f) {
        float d = io.MouseDelta.y / travel * (vmax - vmin) * (io.KeyShift ? 0.2f : 1.f);
        *v = std::clamp(*v + (invert ? d : -d), vmin, vmax);
        changed = true;
    }
    changed |= handleWheelAndReset(v, vmin, vmax, vdefault, hovered, 0.02f);
    const float cx = p.x + size.x * 0.5f;
    for (int i = 0; i <= 10; ++i) {
        const float y = p.y + capH * 0.5f + travel * i / 10.f;
        const float len = (i % 5 == 0) ? size.x * 0.22f : size.x * 0.12f;
        dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + len, y), col::Line);
        dl->AddLine(ImVec2(p.x + size.x - len, y), ImVec2(p.x + size.x, y), col::Line);
    }
    if (std::fabs(toT(vdefault) - 0.5f) < 0.01f) {  // marca del centro (pitch 0)
        const float yc = p.y + capH * 0.5f + travel * 0.5f;
        dl->AddLine(ImVec2(p.x, yc), ImVec2(p.x + size.x, yc), col::withAlpha(accent, 150), 1.5f * s);
    }
    dl->AddRectFilled(ImVec2(cx - 2 * s, p.y + capH * 0.5f), ImVec2(cx + 2 * s, p.y + size.y - capH * 0.5f), col::Bg, 2.f * s);
    const float capY = p.y + toT(*v) * travel;
    // tramo coloreado desde el valor por defecto hasta el actual
    const float yDef = p.y + toT(vdefault) * travel + capH * 0.5f, yCur = capY + capH * 0.5f;
    dl->AddRectFilled(ImVec2(cx - 2 * s, std::min(yDef, yCur)), ImVec2(cx + 2 * s, std::max(yDef, yCur)), col::withAlpha(accent, 150), 2.f * s);
    const float cw = std::min(size.x - 2, 30.f * s);
    const ImVec2 ca(cx - cw * 0.5f, capY), cb(cx + cw * 0.5f, capY + capH);
    dl->AddRectFilledMultiColor(ca, cb, col::mix(col::Panel2, col::Text, hovered || active ? 0.30f : 0.20f), col::mix(col::Panel2, col::Text, hovered || active ? 0.30f : 0.20f),
                                col::mix(col::Panel2, col::Text, 0.06f), col::mix(col::Panel2, col::Text, 0.06f));
    dl->AddRect(ca, cb, col::withAlpha(col::Bg, 255), 3.f * s);
    dl->AddLine(ImVec2(ca.x + 3 * s, capY + capH * 0.5f), ImVec2(cb.x - 3 * s, capY + capH * 0.5f), accent, 2.f * s);
    return changed;
}

bool HFader(const char* id, float* v, float vmin, float vmax, float vdefault, ImVec2 size, ImU32 accent) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float s = S();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    const float capW = std::min(16.f * s, size.x * 0.1f);
    const float travel = size.x - capW;
    bool changed = false;
    if (ImGui::IsItemActivated()) {
        const float capX = p.x + (*v - vmin) / (vmax - vmin) * travel;
        if (io.MousePos.x < capX || io.MousePos.x > capX + capW) {
            *v = vmin + std::clamp((io.MousePos.x - p.x - capW * 0.5f) / travel, 0.f, 1.f) * (vmax - vmin);
            changed = true;
        }
    }
    if (active && io.MouseDelta.x != 0.f) {
        *v = std::clamp(*v + io.MouseDelta.x / travel * (vmax - vmin) * (io.KeyShift ? 0.2f : 1.f), vmin, vmax);
        changed = true;
    }
    changed |= handleWheelAndReset(v, vmin, vmax, vdefault, hovered, 0.02f);
    const float cy = p.y + size.y * 0.5f;
    dl->AddRectFilled(ImVec2(p.x + capW * 0.5f, cy - 2 * s), ImVec2(p.x + size.x - capW * 0.5f, cy + 2 * s), col::Bg, 2.f * s);
    dl->AddLine(ImVec2(p.x + size.x * 0.5f, p.y + 2 * s), ImVec2(p.x + size.x * 0.5f, p.y + size.y - 2 * s), col::Line, 1.f);
    const float capX = p.x + (*v - vmin) / (vmax - vmin) * travel;
    const ImVec2 ca(capX, p.y), cb(capX + capW, p.y + size.y);
    dl->AddRectFilledMultiColor(ca, cb, col::mix(col::Panel2, col::Text, hovered || active ? 0.32f : 0.22f), col::mix(col::Panel2, col::Text, 0.08f),
                                col::mix(col::Panel2, col::Text, 0.08f), col::mix(col::Panel2, col::Text, hovered || active ? 0.32f : 0.22f));
    dl->AddRect(ca, cb, col::Bg, 3.f * s);
    dl->AddLine(ImVec2(capX + capW * 0.5f, p.y + 3 * s), ImVec2(capX + capW * 0.5f, p.y + size.y - 3 * s), accent, 2.f * s);
    return changed;
}

static ImU32 meterColor(float db) { return db > 0.f ? col::Red : db > -6.f ? col::Amber : col::Green; }

float MeterPos(float db) { return std::clamp((db + 48.f) / 51.f, 0.f, 1.f); }

void StereoMeter(ImDrawList* dl, ImVec2 a, ImVec2 b, float l, float r, float holdL, float holdR, bool clip) {
    const float s = S();
    const float led = std::max(3.f, 4 * s);
    // Piloto de saturación arriba.
    dl->AddRectFilled(a, ImVec2(b.x, a.y + led), clip ? col::Red : col::withAlpha(col::Red, 40), 1.f * s);
    const float top = a.y + led + 2 * s;
    const float gap = std::max(1.f, 1.5f * s);
    const float bw = (b.x - a.x - gap) * 0.5f;
    const float H = b.y - top;
    const int segs = std::clamp((int)(H / (3.5f * s)), 10, 40);
    const float sh = H / segs;
    for (int ch = 0; ch < 2; ++ch) {
        const float x0 = a.x + ch * (bw + gap), x1 = x0 + bw;
        const float lvl = ch ? r : l, hold = ch ? holdR : holdL;
        const float db = lvl > 1e-6f ? 20.f * std::log10(lvl) : -100.f;
        const float pos = MeterPos(db);
        dl->AddRectFilled(ImVec2(x0, top), ImVec2(x1, b.y), col::mix(col::Bg, IM_COL32(0, 0, 0, 255), 0.3f), 1.f);
        for (int i = 0; i < segs; ++i) {
            const float y1 = b.y - i * sh, y0 = y1 - sh + std::max(1.f, 0.8f * s);
            const float segDb = -48.f + 51.f * (i + 0.5f) / segs;
            const ImU32 c = meterColor(segDb);
            const bool lit = (i + 0.5f) / segs <= pos;
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), lit ? c : col::withAlpha(c, 22));
        }
        if (hold > 1e-4f) {
            const float hp = MeterPos(20.f * std::log10(hold));
            const float yh = b.y - H * hp;
            dl->AddRectFilled(ImVec2(x0, yh - 1.f * s), ImVec2(x1, yh + 0.5f * s), hp > MeterPos(0.f) ? col::Red : col::Text);
        }
    }
}

bool ChannelFader(const char* id, float* v, float vdefault, ImVec2 size, ImU32 accent, bool scaleRight) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float s = S();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    const float capH = std::clamp(size.y * 0.12f, 14 * s, 22 * s);
    const float travel = size.y - capH;
    bool changed = false;
    if (ImGui::IsItemActivated() && !io.KeyShift) {
        const float capY = p.y + (1.f - *v) * travel;
        if (io.MousePos.y < capY || io.MousePos.y > capY + capH) {
            *v = std::clamp(1.f - (io.MousePos.y - p.y - capH * 0.5f) / travel, 0.f, 1.f);
            changed = true;
        }
    }
    if (active && io.MouseDelta.y != 0.f) {
        *v = std::clamp(*v - io.MouseDelta.y / travel * (io.KeyShift ? 0.2f : 1.f), 0.f, 1.f);
        changed = true;
    }
    changed |= handleWheelAndReset(v, 0.f, 1.f, vdefault, hovered, 0.02f);
    // Ranura a un lado y escala al otro.
    const float slotX = scaleRight ? p.x + size.x * 0.36f : p.x + size.x * 0.64f;
    const float y0 = p.y + capH * 0.5f, y1 = p.y + size.y - capH * 0.5f;
    dl->AddRectFilled(ImVec2(slotX - 2.5f * s, y0), ImVec2(slotX + 2.5f * s, y1), IM_COL32(0, 0, 0, 170), 2.5f * s);
    dl->AddRectFilled(ImVec2(slotX - 2.5f * s, y0 + (y1 - y0) * (1.f - *v)), ImVec2(slotX + 2.5f * s, y1), col::withAlpha(accent, 120), 2.5f * s);
    // Escala: 0, −6, −12, −20, −30 dB (ganancia = v²).
    struct Mark { float db; const char* t; };
    const Mark marks[] = {{0, "0"}, {-6, "6"}, {-12, "12"}, {-20, "20"}, {-30, "30"}};
    ImFont* f = ImGui::GetFont();
    const float fz = std::max(8.5f, 9.5f * s);
    for (const Mark& m : marks) {
        const float vv = std::pow(10.f, m.db / 40.f);
        const float y = y0 + (y1 - y0) * (1.f - vv);
        const float tx0 = scaleRight ? slotX + 6 * s : slotX - 6 * s;
        const float tx1 = scaleRight ? tx0 + 5 * s : tx0 - 5 * s;
        dl->AddLine(ImVec2(tx0, y), ImVec2(tx1, y), col::withAlpha(col::TextDim, 160), 1.f);
        const float tw = f->CalcTextSizeA(fz, FLT_MAX, 0, m.t).x;
        dl->AddText(f, fz, ImVec2(scaleRight ? tx1 + 2 * s : tx1 - 2 * s - tw, y - fz * 0.55f), col::withAlpha(col::TextDim, 190), m.t);
    }
    for (int i = 0; i <= 10; ++i) {  // marcas pequeñas
        const float y = y0 + (y1 - y0) * i / 10.f;
        const float tx0 = scaleRight ? slotX + 6 * s : slotX - 6 * s;
        dl->AddLine(ImVec2(tx0, y), ImVec2(tx0 + (scaleRight ? 2.5f : -2.5f) * s, y), col::withAlpha(col::Line, 255), 1.f);
    }
    // Tirador
    const float cw = std::min(size.x * 0.62f, 30 * s);
    const float capY = p.y + (1.f - *v) * travel;
    const ImVec2 ca(slotX - cw * 0.5f, capY), cb(slotX + cw * 0.5f, capY + capH);
    dl->AddRectFilled(ImVec2(ca.x + 1, ca.y + 2 * s), ImVec2(cb.x + 1, cb.y + 2 * s), IM_COL32(0, 0, 0, 90), 3 * s);  // sombra
    const ImU32 top = col::mix(col::Panel2, col::Text, hovered || active ? 0.34f : 0.24f), bot = col::mix(col::Panel2, col::Text, 0.06f);
    dl->AddRectFilledMultiColor(ca, cb, top, top, bot, bot);
    dl->AddRect(ca, cb, col::withAlpha(col::Bg, 255), 3 * s);
    for (int k = -1; k <= 1; k += 2) dl->AddLine(ImVec2(ca.x + 4 * s, capY + capH * 0.5f + k * 3.5f * s), ImVec2(cb.x - 4 * s, capY + capH * 0.5f + k * 3.5f * s), col::withAlpha(col::Bg, 160), 1.f);
    dl->AddLine(ImVec2(ca.x + 3 * s, capY + capH * 0.5f), ImVec2(cb.x - 3 * s, capY + capH * 0.5f), accent, 2.f * s);
    return changed;
}

bool XFader(const char* id, float* v, ImVec2 size, ImU32 colA, ImU32 colB) {
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float s = S();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    const float capW = std::clamp(size.x * 0.1f, 16 * s, 26 * s);
    const float travel = size.x - capW;
    bool changed = false;
    if (ImGui::IsItemActivated()) {
        const float capX = p.x + *v * travel;
        if (io.MousePos.x < capX || io.MousePos.x > capX + capW) { *v = std::clamp((io.MousePos.x - p.x - capW * 0.5f) / travel, 0.f, 1.f); changed = true; }
    }
    if (active && io.MouseDelta.x != 0.f) { *v = std::clamp(*v + io.MouseDelta.x / travel * (io.KeyShift ? 0.2f : 1.f), 0.f, 1.f); changed = true; }
    changed |= handleWheelAndReset(v, 0.f, 1.f, 0.5f, hovered, 0.02f);
    const float cy = p.y + size.y * 0.5f;
    const float x0 = p.x + capW * 0.5f, x1 = p.x + size.x - capW * 0.5f, xc = (x0 + x1) * 0.5f;
    dl->AddRectFilled(ImVec2(x0 - 3 * s, cy - 3.5f * s), ImVec2(x1 + 3 * s, cy + 3.5f * s), IM_COL32(0, 0, 0, 170), 3.5f * s);
    const float capX = p.x + *v * travel, capC = capX + capW * 0.5f;
    // Relleno desde el centro hacia el lado que gana.
    if (std::fabs(capC - xc) > 1.f) dl->AddRectFilled(ImVec2(std::min(xc, capC), cy - 2 * s), ImVec2(std::max(xc, capC), cy + 2 * s), col::withAlpha(capC < xc ? colA : colB, 200), 2 * s);
    for (int i = 0; i <= 8; ++i) {
        const float x = x0 + (x1 - x0) * i / 8.f;
        const float hh = i == 4 ? size.y * 0.42f : (i % 2 == 0 ? 6 * s : 3.5f * s);
        const ImU32 c = i == 4 ? col::withAlpha(col::Text, 150) : col::withAlpha(col::TextDim, 120);
        dl->AddLine(ImVec2(x, cy - 5 * s - hh * 0.5f), ImVec2(x, cy - 5 * s), c, 1.f);
    }
    const ImVec2 ca(capX, p.y + 1 * s), cb(capX + capW, p.y + size.y - 1 * s);
    dl->AddRectFilled(ImVec2(ca.x + 1, ca.y + 2 * s), ImVec2(cb.x + 1, cb.y + 2 * s), IM_COL32(0, 0, 0, 90), 3 * s);
    const ImU32 top = col::mix(col::Panel2, col::Text, hovered || active ? 0.36f : 0.26f), bot = col::mix(col::Panel2, col::Text, 0.06f);
    dl->AddRectFilledMultiColor(ca, cb, top, bot, bot, top);
    dl->AddRect(ca, cb, col::Bg, 3 * s);
    dl->AddLine(ImVec2(capC, ca.y + 3 * s), ImVec2(capC, cb.y - 3 * s), col::Text, 2.f * s);
    return changed;
}

void Meter(ImDrawList* dl, ImVec2 a, ImVec2 b, float level, float hold, bool clip) {
    dl->AddRectFilled(a, b, col::Bg, 2.f);
    // Escala en dB: -48..+3
    auto yOf = [&](float lin) {
        const float db = lin > 1e-6f ? 20.f * std::log10(lin) : -100.f;
        const float t = std::clamp((db + 48.f) / 51.f, 0.f, 1.f);
        return b.y - (b.y - a.y) * t;
    };
    const int segs = std::clamp((int)((b.y - a.y) / 5.f), 8, 32);
    const float h = (b.y - a.y) / segs;
    const float yl = yOf(level);
    for (int i = 0; i < segs; ++i) {
        const float y1 = b.y - i * h, y0 = y1 - h + 1;
        const bool lit = (y0 + y1) * 0.5f >= yl;
        const float db = -48.f + 51.f * (i + 0.5f) / segs;
        const ImU32 c = meterColor(db);
        dl->AddRectFilled(ImVec2(a.x, y0), ImVec2(b.x, y1), lit ? c : col::withAlpha(c, 26));
    }
    if (hold > 1e-4f) {
        const float yh = yOf(hold);
        dl->AddLine(ImVec2(a.x, yh), ImVec2(b.x, yh), col::Text, 1.5f);
    }
    if (clip) dl->AddRectFilled(ImVec2(a.x, a.y - 5), ImVec2(b.x, a.y - 1), col::Red, 1.f);
}

void HMeter(ImDrawList* dl, ImVec2 a, ImVec2 b, float level, float hold, bool clip) {
    auto xOf = [&](float lin) {
        const float db = lin > 1e-6f ? 20.f * std::log10(lin) : -100.f;
        return a.x + (b.x - a.x) * std::clamp((db + 48.f) / 51.f, 0.f, 1.f);
    };
    const int segs = std::clamp((int)((b.x - a.x) / 4.f), 8, 48);
    const float w = (b.x - a.x) / segs;
    const float xl = xOf(level);
    for (int i = 0; i < segs; ++i) {
        const float x0 = a.x + i * w, x1 = x0 + w - 1;
        const float db = -48.f + 51.f * (i + 0.5f) / segs;
        const ImU32 c = meterColor(db);
        dl->AddRectFilled(ImVec2(x0, a.y), ImVec2(x1, b.y), (x0 + x1) * 0.5f <= xl ? c : col::withAlpha(c, 30));
    }
    if (hold > 1e-4f) { const float xh = xOf(hold); dl->AddLine(ImVec2(xh, a.y), ImVec2(xh, b.y), col::Text, 1.5f); }
    if (clip) dl->AddRectFilled(ImVec2(b.x + 2, a.y), ImVec2(b.x + 5, b.y), col::Red);
}

bool IconTextButton(const char* id, Ic ic, const char* text, ImU32 c, bool on, ImVec2 size, bool enabled, ImFont* font, float fontSize, bool iconRight) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = NeonButton(id, c, on, size, enabled, font, fontSize);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float s = S();
    if (!font) font = ImGui::GetFont();
    if (fontSize <= 0) fontSize = ImGui::GetFontSize();
    const ImVec2 sz = ImGui::GetItemRectSize();
    const float isz = std::min(sz.y * 0.5f, 18.f * s);
    const float tw = text && *text ? font->CalcTextSizeA(fontSize, FLT_MAX, 0, text).x : 0.f;
    const float gap = tw > 0 ? 6 * s : 0.f;
    const float total = isz + gap + tw;
    const float x0 = p.x + (sz.x - total) * 0.5f, cy = p.y + sz.y * 0.5f;
    ImU32 tc = on ? col::onColor(c) : (enabled ? col::Text : col::withAlpha(col::TextDim, 150));
    const float ix = iconRight ? x0 + tw + gap + isz * 0.5f : x0 + isz * 0.5f;
    DrawIcon(dl, ic, ImVec2(ix, cy), isz, tc);
    if (tw > 0) dl->AddText(font, fontSize, ImVec2(iconRight ? x0 : x0 + isz + gap, cy - fontSize * 0.55f), tc, text);
    return clicked;
}

bool RatingStars(const char* id, int* value, ImVec2 pos, float size, ImU32 on, ImU32 off, bool interactive) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float step = size * 1.05f;
    int hover = -1;
    bool clicked = false;
    if (interactive) {
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton(id, ImVec2(step * 5, size));
        if (ImGui::IsItemHovered()) {
            hover = std::clamp((int)((ImGui::GetIO().MousePos.x - pos.x) / step), 0, 4);
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && hover >= 0) { *value = *value == hover + 1 ? 0 : hover + 1; clicked = true; }
    }
    const int shown = hover >= 0 ? hover + 1 : *value;
    for (int i = 0; i < 5; ++i) {
        const ImVec2 c(pos.x + step * i + size * 0.5f, pos.y + size * 0.5f);
        if (i < shown) DrawIcon(dl, Ic::StarFill, c, size, hover >= 0 ? col::mix(on, col::Text, 0.3f) : on);
        else DrawIcon(dl, Ic::Star, c, size, off, std::max(1.f, size * 0.07f));
    }
    return clicked;
}

bool StateButton(const char* label, bool on, ImU32 onColor, ImVec2 size, bool enabled) {
    if (!enabled) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, on ? toVec4(onColor) : toVec4(col::Panel2));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? toVec4(col::withAlpha(onColor, 220)) : toVec4(col::mix(col::Panel2, onColor, 0.2f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, toVec4(col::withAlpha(onColor, 180)));
    ImGui::PushStyleColor(ImGuiCol_Text, on ? toVec4(col::onColor(onColor)) : toVec4(col::Text));
    const bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    if (!enabled) ImGui::EndDisabled();
    return r;
}

void Panel(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, ImU32 fill, ImU32 border) {
    // El radio lo marca el diseño (rounding llega pensado para 10 px a escala 1).
    rounding = std::min(rounding, col::Radius * S());
    dl->AddRectFilled(a, b, fill, rounding);
    // Luz superior muy tenue: da relieve sin brillos.
    if (b.y - a.y > 24 * S()) dl->AddRectFilledMultiColor(ImVec2(a.x + rounding, a.y + 1), ImVec2(b.x - rounding, a.y + std::min(18.f * S(), (b.y - a.y) * 0.2f)),
                                                         IM_COL32(255, 255, 255, 7), IM_COL32(255, 255, 255, 7), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    dl->AddRect(a, b, border, rounding, 0, 1.f);
}

void NeonFrame(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float rounding, float glow, ImU32 fill) {
    const float s = S();
    if (col::Glow < 0.3f) {  // temas planos: panel con un toque del color en el borde
        Panel(dl, a, b, rounding, fill, col::mix(col::Line, c, 0.22f));
        return;
    }
    glow *= col::Glow;
    if (glow > 0.05f) {
        for (int i = 3; i >= 1; --i) {
            const float e = i * 2.2f * s;
            const int al = (int)(glow * (i == 3 ? 10 : i == 2 ? 20 : 38));
            dl->AddRect(ImVec2(a.x - e, a.y - e), ImVec2(b.x + e, b.y + e), col::withAlpha(c, al), rounding + e, 0, 2.2f * s);
        }
    }
    dl->AddRectFilled(a, b, fill, rounding);
    if (glow > 0.05f)
        dl->AddRectFilledMultiColor(ImVec2(a.x + rounding, a.y + 1), ImVec2(b.x - rounding, a.y + std::min(40.f * s, (b.y - a.y) * 0.3f)),
                                    col::withAlpha(c, (int)(18 * glow)), col::withAlpha(c, (int)(18 * glow)), col::withAlpha(c, 0), col::withAlpha(c, 0));
    dl->AddRect(a, b, glow > 0.05f ? col::withAlpha(c, (int)(120 + 100 * glow)) : col::mix(col::Line, c, 0.25f), rounding, 0, glow > 0.05f ? 1.4f * s : 1.f);
}

void GlowText(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 c, const char* txt, float glow) {
    const float s = S();
    const float off[4][2] = {{-1.5f, 0}, {1.5f, 0}, {0, -1.5f}, {0, 1.5f}};
    glow *= col::Glow;
    if (glow > 0.3f)
        for (auto& o : off) dl->AddText(f, size, ImVec2(pos.x + o[0] * s, pos.y + o[1] * s), col::withAlpha(c, (int)(55 * glow)), txt);
    if (col::Glow > 0.3f) {
        dl->AddText(f, size, pos, col::withAlpha(col::Text, 235), txt);
        dl->AddText(f, size, pos, col::withAlpha(c, 150), txt);
    } else dl->AddText(f, size, pos, c, txt);
}

bool NeonButton(const char* label, ImU32 c, bool on, ImVec2 size, bool enabled, ImFont* font, float fontSize) {
    if (!enabled) ImGui::BeginDisabled();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (size.x <= 0) size.x = ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
    if (size.y <= 0) size.y = ImGui::GetFrameHeight();
    const bool clicked = ImGui::InvisibleButton(label, size);
    const bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float s = S();
    const ImVec2 b(p.x + size.x, p.y + size.y);
    const float r = col::BtnRadius * s;
    const float alphaMul = enabled ? 1.f : 0.4f;
    if (on) {
        if (col::Glow > 0.3f)
            dl->AddRect(ImVec2(p.x - 2 * s, p.y - 2 * s), ImVec2(b.x + 2 * s, b.y + 2 * s), col::withAlpha(c, (int)(60 * alphaMul * col::Glow)), r + 2 * s, 0, 3.f * s);
        const ImU32 top = col::withAlpha(col::mix(c, IM_COL32(255, 255, 255, 255), act ? 0.f : 0.10f), (int)((act ? 200 : hov ? 250 : 238) * alphaMul));
        const ImU32 bot = col::withAlpha(col::mix(c, IM_COL32(0, 0, 0, 255), 0.12f), (int)((act ? 200 : hov ? 250 : 238) * alphaMul));
        dl->AddRectFilled(p, b, bot, r);
        dl->AddRectFilledMultiColor(ImVec2(p.x + r * 0.3f, p.y + 1), ImVec2(b.x - r * 0.3f, p.y + (b.y - p.y) * 0.55f), top, top, bot, bot);
    } else {
        // Reposo: gris grafito con borde fino; encima: borde del color de la función; pulsado: se oscurece.
        // Deshabilitado: sin borde y apagado.
        const ImU32 fill = act ? col::mix(col::Panel2, col::Bg, 0.45f) : hov ? col::mix(col::Panel2, c, 0.10f) : col::Panel2;
        dl->AddRectFilled(p, b, col::withAlpha(fill, (int)(255 * (enabled ? 1.f : 0.55f))), r);
        if (enabled) dl->AddRect(p, b, hov || act ? col::mix(col::Line, c, 0.75f) : col::Line, r, 0, 1.f * s);
    }
    // texto (admite varias líneas)
    if (!font) font = ImGui::GetFont();
    if (fontSize <= 0) fontSize = ImGui::GetFontSize();
    const char* end = ImGui::FindRenderedTextEnd(label);
    std::string txt(label, end);
    std::vector<std::string> lines;
    size_t st = 0;
    for (size_t i = 0; i <= txt.size(); ++i) if (i == txt.size() || txt[i] == '\n') { lines.push_back(txt.substr(st, i - st)); st = i + 1; }
    const float lh = fontSize * 1.05f;
    float y = p.y + (size.y - lh * lines.size()) * 0.5f;
    const ImU32 tc = on ? col::withAlpha(col::onColor(c), (int)(255 * alphaMul)) : col::withAlpha(col::Text, (int)(235 * alphaMul));
    for (size_t i = 0; i < lines.size(); ++i) {
        float sz = i == 0 ? fontSize : fontSize * 0.8f;
        const float measured = font->CalcTextSizeA(sz, FLT_MAX, 0, lines[i].c_str()).x;
        if (measured > size.x - 8*s && measured > 0) sz *= std::max(1.f,size.x-8*s)/measured;
        const ImVec2 ts = font->CalcTextSizeA(sz, FLT_MAX, 0, lines[i].c_str());
        dl->AddText(font, sz, ImVec2(p.x + (size.x - ts.x) * 0.5f, y), i == 0 ? tc : col::withAlpha(tc, (int)(190 * alphaMul)), lines[i].c_str());
        y += lh;
    }
    if (!enabled) ImGui::EndDisabled();
    return clicked;
}

float BadgeWidth(float h, const char* txt, ImFont* font, float fontSize) {
    if (!font) font = ImGui::GetFont();
    if (fontSize <= 0) fontSize = h * 0.68f;
    return font->CalcTextSizeA(fontSize, FLT_MAX, 0, txt).x + h * 0.7f;
}

float Badge(ImDrawList* dl, ImVec2 pos, float h, const char* txt, ImU32 c, bool filled, ImFont* font, float fontSize) {
    if (!font) font = ImGui::GetFont();
    if (fontSize <= 0) fontSize = h * 0.68f;
    const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0, txt);
    const float w = ts.x + h * 0.7f;
    const ImVec2 b(pos.x + w, pos.y + h);
    const float r = 4.f * S();
    if (filled) dl->AddRectFilled(pos, b, c, r);
    else {
        dl->AddRectFilled(pos, b, col::withAlpha(c, 26), r);
        dl->AddRect(pos, b, col::withAlpha(c, 200), r, 0, 1.f);
    }
    dl->AddText(font, fontSize, ImVec2(pos.x + (w - ts.x) * 0.5f, pos.y + (h - ts.y) * 0.5f), filled ? col::onColor(c) : c, txt);
    return w;
}

void CenterText(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, const char* txt, ImFont* font, float size) {
    if (!font) font = ImGui::GetFont();
    if (size <= 0) size = ImGui::GetFontSize();
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.f, txt);
    dl->AddText(font, size, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), c, txt);
}

void ArtPlaceholder(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* seed, float rounding) {
    uint32_t h = 2166136261u;
    for (const char* p = seed; p && *p; ++p) { h ^= (uint8_t)*p; h *= 16777619u; }
    float r, g, bl;
    ImGui::ColorConvertHSVtoRGB((h % 360) / 360.f, 0.55f, 0.75f, r, g, bl);
    const ImU32 hue = IM_COL32((int)(r * 255), (int)(g * 255), (int)(bl * 255), 255);
    const ImU32 dark = col::mix(col::Panel2, hue, 0.18f), lit = col::mix(col::Panel2, hue, 0.45f);
    if (rounding > 0) {
        dl->AddRectFilled(a, b, dark, rounding);
        dl->PushClipRect(a, b, true);
        dl->AddRectFilledMultiColor(ImVec2(a.x + rounding * 0.3f, a.y + rounding * 0.3f), ImVec2(b.x - rounding * 0.3f, b.y - rounding * 0.3f), lit, dark, dark, dark);
        dl->PopClipRect();
    } else dl->AddRectFilledMultiColor(a, b, lit, dark, dark, dark);
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float rr = std::min(b.x - a.x, b.y - a.y) * 0.34f;
    dl->AddCircleFilled(c, rr, col::withAlpha(col::Bg, 200), 32);
    for (int i = 1; i <= 3; ++i) dl->AddCircle(c, rr * (0.45f + 0.17f * i), col::withAlpha(hue, 70), 32, 1.f);
    dl->AddCircleFilled(c, rr * 0.28f, col::withAlpha(hue, 230), 20);
    dl->AddCircleFilled(c, std::max(1.f, rr * 0.06f), col::Bg, 8);
}

} // namespace rdj::ui
