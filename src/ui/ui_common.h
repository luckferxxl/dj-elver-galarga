// Utilidades de dibujo compartidas por las pantallas de la interfaz.
#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include "widgets.h"
#include <string>
#include <vector>

namespace rdj::uic {

inline float S() { return ImGui::GetFontSize() / 16.f; }

// Recorta texto UTF-8 con "…" para que quepa en maxW píxeles.
inline std::string ellipsize(const std::string& s, float maxW, ImFont* f, float size) {
    if (maxW <= 0) return "";
    if (f->CalcTextSizeA(size, FLT_MAX, 0, s.c_str()).x <= maxW) return s;
    std::string t = s;
    while (!t.empty()) {
        do { t.pop_back(); } while (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0x80);
        if (!t.empty() && ((unsigned char)t.back() & 0xC0) == 0xC0) t.pop_back();
        if (f->CalcTextSizeA(size, FLT_MAX, 0, (t + "…").c_str()).x <= maxW) return t + "…";
    }
    return "…";
}

// Ventana hija transparente para colocar widgets sobre un panel dibujado a mano.
inline void BeginPanel(const char* id, ImVec2 a, ImVec2 b, float pad, bool scroll = false) {
    ImGui::SetCursorScreenPos(a);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::BeginChild(id, ImVec2(b.x - a.x, b.y - a.y), ImGuiChildFlags_AlwaysUseWindowPadding,
                      (scroll ? 0 : (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
}

// Rótulo pequeño en mayúsculas (cabeceras de sección).
inline void Caption(ImDrawList* dl, ImFont* f, ImVec2 p, const char* txt, ImU32 c, float size) { dl->AddText(f, size, p, c, txt); }

// Arrastrar pistas: una o varias rutas separadas por '\n' (tipo "RDJ_PISTA").
inline void SetPathsPayload(const std::vector<std::wstring>& paths) {
    std::wstring all;
    for (size_t i = 0; i < paths.size(); ++i) { if (i) all += L'\n'; all += paths[i]; }
    ImGui::SetDragDropPayload("RDJ_PISTA", all.c_str(), (all.size() + 1) * sizeof(wchar_t));
}
inline std::vector<std::wstring> PayloadPaths(const ImGuiPayload* p) {
    std::vector<std::wstring> out;
    if (!p || !p->Data) return out;
    const wchar_t* w = (const wchar_t*)p->Data;
    const size_t n = p->DataSize / sizeof(wchar_t);
    std::wstring cur;
    for (size_t i = 0; i < n && w[i]; ++i) {
        if (w[i] == L'\n') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += w[i];
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

} // namespace rdj::uic
