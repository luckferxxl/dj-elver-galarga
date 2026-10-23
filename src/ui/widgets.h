// Controles propios dibujados con ImGui: potenciómetros, faders, medidores,
// botones, insignias e iconos vectoriales.
#pragma once
#include "imgui.h"

namespace rdj::ui {

// Diseños (temas) seleccionables. La paleta activa vive en col:: y se cambia
// en caliente con applyTheme().
struct Theme {
    const char* name;
    const char* description;
    ImU32 bg, panel, panel2, line, text, textDim;
    ImU32 deckA, deckB, accent, green, amber, red;
    ImU32 tintLeft, tintRight;            // luz de fondo a cada lado
    ImU32 waveLow, waveMid, waveHigh;     // colores de la forma de onda por banda
    ImU32 waveBg;                         // fondo de las ondas
    float glow;                           // 0 = plano, 1 = neón completo
    float radius, btnRadius;              // esquinas de paneles y botones (px a escala 1)
};
int themeCount();
float DrawAppLogo(ImDrawList* dl, ImVec2 pos, float h, ImFont* bold, ImFont* head, const char* version = nullptr);
const Theme& themeAt(int i);
void applyTheme(int i);                   // paleta + colores de ImGui
int currentThemeIndex();
const Theme& currentTheme();

namespace col {
inline ImU32 Bg = IM_COL32(9, 13, 19, 255);
inline ImU32 Panel = IM_COL32(16, 21, 29, 255);
inline ImU32 Panel2 = IM_COL32(24, 30, 40, 255);
inline ImU32 Line = IM_COL32(38, 46, 60, 255);
inline ImU32 Text = IM_COL32(232, 237, 243, 255);
inline ImU32 TextDim = IM_COL32(132, 145, 163, 255);
inline ImU32 DeckA = IM_COL32(43, 227, 192, 255);
inline ImU32 DeckB = IM_COL32(255, 92, 88, 255);
inline ImU32 Violet = IM_COL32(155, 110, 255, 255);  // acento general del tema
inline ImU32 Green = IM_COL32(52, 211, 153, 255);
inline ImU32 Amber = IM_COL32(255, 166, 77, 255);
inline ImU32 Red = IM_COL32(255, 72, 72, 255);
inline ImU32 TintLeft = IM_COL32(9, 13, 19, 255);
inline ImU32 TintRight = IM_COL32(9, 13, 19, 255);
inline ImU32 WaveLow = IM_COL32(255, 70, 110, 255);
inline ImU32 WaveMid = IM_COL32(255, 190, 90, 255);
inline ImU32 WaveHigh = IM_COL32(80, 210, 255, 255);
inline ImU32 WaveBg = IM_COL32(6, 9, 14, 255);
inline float Glow = 0.15f;
inline float Radius = 10.f, BtnRadius = 6.f;  // a escala 1
inline ImU32 deck(int d) { return d == 0 ? DeckA : DeckB; }
inline ImU32 withAlpha(ImU32 c, int a) { return (c & 0x00FFFFFF) | ((ImU32)(a < 0 ? 0 : a > 255 ? 255 : a) << 24); }
ImU32 mix(ImU32 a, ImU32 b, float k);     // interpolación de color (alfa de a)
ImU32 onColor(ImU32 c);                    // texto legible sobre un relleno c
ImU32 camelot(int key);                    // color de la rueda Camelot (-1 = gris)
} // namespace col

ImVec4 toVec4(ImU32 c);

// ------------------------------------------------------------------ iconos
enum class Ic {
    None, Search, Headphones, Gear, Heart, HeartFill, Folder, Star, StarFill, List, Clock, Note, Drive, Monitor,
    Plus, Dots, ChevRight, ChevDown, ChevLeft, ArrowUp, ArrowDown, Play, Pause, Record, Eject, Bulb, Layers, Mic, Close,
    Pencil, Grip, Disc, Download, Document, Globe, PanelLeft, PanelRight, Info, Sliders, Refresh, Calendar, Usb, Check,
    Stats, Shuffle, Lock, Bend, Wave, Layout, Image,
    Stop, StepBack, StepFwd, Rewind, FastFwd, Mute, Solo, Spinner, Mic2, Drum, Bass, Guitar, Piano, Waveform,
};
// Dibuja un icono vectorial centrado en c dentro de un cuadrado de lado sz.
void DrawIcon(ImDrawList* dl, Ic ic, ImVec2 c, float sz, ImU32 color, float thick = 0.f);
// Botón con icono (sin texto). on = resaltado. Tras llamarlo valen IsItemActivated/Deactivated.
bool IconButton(const char* id, Ic ic, ImVec2 size, ImU32 accent, bool on = false, bool enabled = true, float iconScale = 0.55f);

// ------------------------------------------------------------------ controles
// Potenciómetro giratorio. Arrastrar en vertical, rueda del ratón, doble clic = valor por defecto.
// bipolar dibuja el arco desde el centro (EQ, filtro, ganancia).
bool Knob(const char* id, const char* label, float* v, float vmin, float vmax, float vdefault, bool bipolar,
          float radius, ImU32 accent, const char* valueText = nullptr);

// Fader vertical. Doble clic = valor por defecto.
bool VFader(const char* id, float* v, float vmin, float vmax, float vdefault, ImVec2 size, ImU32 accent, bool invert = false);

// Fader horizontal (crossfader).
bool HFader(const char* id, float* v, float vmin, float vmax, float vdefault, ImVec2 size, ImU32 accent);

// Fader de canal de mesa: ranura, escala en dB (0, −6, −12, −20, −30) a un lado, tirador con
// agarre y línea del color del canal. v en 0..1 (ganancia = v²). Doble clic = valor por defecto.
bool ChannelFader(const char* id, float* v, float vdefault, ImVec2 size, ImU32 accent, bool scaleRight);
// Medidor estéreo de canal (L y R) segmentado con retención de pico y piloto de saturación arriba.
void StereoMeter(ImDrawList* dl, ImVec2 a, ImVec2 b, float l, float r, float holdL, float holdR, bool clip);
// Posición en el medidor (0 abajo .. 1 arriba) de un nivel en dB (escala −48..+3 dB).
float MeterPos(float db);
// Crossfader con ranura ancha, marca central, relleno hacia el lado activo y tirador grande.
bool XFader(const char* id, float* v, ImVec2 size, ImU32 colA, ImU32 colB);

// Medidor vertical: nivel lineal (0..1+). Retiene el pico y marca recorte.
void Meter(ImDrawList* dl, ImVec2 a, ImVec2 b, float level, float hold, bool clip);
// Medidor horizontal segmentado (master en la barra superior).
void HMeter(ImDrawList* dl, ImVec2 a, ImVec2 b, float level, float hold, bool clip);

// Botón con icono vectorial y texto opcional (izquierda o derecha del icono). Mismo aspecto y
// estados que NeonButton. iconRight: el icono va detrás del texto.
bool IconTextButton(const char* id, Ic ic, const char* text, ImU32 c, bool on, ImVec2 size, bool enabled = true, ImFont* font = nullptr,
                    float fontSize = 0, bool iconRight = false);
// Estrellas de valoración (0..5). Devuelve true si se ha hecho clic; *value cambia (clic en la misma = 0).
bool RatingStars(const char* id, int* value, ImVec2 pos, float size, ImU32 on, ImU32 off, bool interactive = true);

// Botón con color de estado.
bool StateButton(const char* label, bool on, ImU32 onColor, ImVec2 size, bool enabled = true);

// Panel plano: relleno + borde fino (diseño 0.5).
void Panel(ImDrawList* dl, ImVec2 a, ImVec2 b, float rounding, ImU32 fill = col::Panel, ImU32 border = col::Line);

// Marco con brillo de neón (relleno oscuro + halo del color indicado).
void NeonFrame(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float rounding, float glow = 1.f, ImU32 fill = col::Panel);

// Botón: relleno del color c cuando "on"; si no, fondo oscuro con borde. El
// texto admite varias líneas. Tras llamarlo valen IsItemActivated/Deactivated.
bool NeonButton(const char* label, ImU32 c, bool on, ImVec2 size, bool enabled = true, ImFont* font = nullptr, float fontSize = 0);

// Insignia (tonalidad, Camelot…). Devuelve el ancho dibujado.
float Badge(ImDrawList* dl, ImVec2 pos, float h, const char* txt, ImU32 c, bool filled = false, ImFont* font = nullptr, float fontSize = 0);
float BadgeWidth(float h, const char* txt, ImFont* font = nullptr, float fontSize = 0);

// Texto con halo de neón.
void GlowText(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 c, const char* txt, float glow = 1.f);

// Texto centrado en un rectángulo.
void CenterText(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, const char* txt, ImFont* font = nullptr, float size = 0);

// Carátula de sustitución: degradado con un color derivado del texto y un disco.
void ArtPlaceholder(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* seed, float rounding);

} // namespace rdj::ui
