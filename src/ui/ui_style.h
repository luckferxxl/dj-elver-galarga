// Sistema de diseño de Ramiro DJ (0.7): escalas de texto, espacios, radios y alturas
// de control, a escala 1 (se multiplican por S()). Los colores viven en widgets.h (col::).
#pragma once

namespace rdj::ds {

// Tipografía (px). Barlow Semi Condensed para rótulos, cifras y títulos; Hanken Grotesk para texto.
constexpr float kFsMicro = 10.f;     // marcas de escala, chips diminutos
constexpr float kFsCaption = 11.f;   // rótulos de controles (GAIN, HI…), cabeceras de tabla
constexpr float kFsSmall = 12.f;     // chips, metadatos secundarios
constexpr float kFsBody = 13.5f;     // texto de listas y paneles
constexpr float kFsBodyL = 15.f;     // títulos de fila, valores destacados
constexpr float kFsTitle = 18.f;     // títulos de panel y de pista en Detalles
constexpr float kFsHeadline = 22.f;  // título de la lista
constexpr float kFsDisplay = 30.f;   // BPM grande del plato

// Espacios
constexpr float kSp1 = 4.f, kSp2 = 8.f, kSp3 = 12.f, kSp4 = 16.f, kSp5 = 24.f;

// Radios
constexpr float kR1 = 4.f, kR2 = 6.f, kR3 = 10.f;

// Alturas mínimas de control
constexpr float kHMin = 24.f;     // nada clicable por debajo de esto
constexpr float kHPad = 28.f;     // pads
constexpr float kHButton = 32.f;  // botones normales
constexpr float kHMain = 40.f;    // CUE · PLAY · SYNC

} // namespace rdj::ds
