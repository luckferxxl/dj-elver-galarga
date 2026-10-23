// Carátulas: se leen de las etiquetas (o de cover.jpg/folder.jpg) en un hilo
// aparte, se decodifican con WIC a miniaturas cuadradas y se suben a la GPU
// desde el hilo de la interfaz. La caché libera las que llevan tiempo sin verse.
#pragma once
#include "imgui.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rdj::ui {

// Funciones de textura que aporta el backend gráfico (main.cpp).
struct TextureApi {
    ImTextureID (*create)(const uint8_t* rgba, int w, int h) = nullptr;
    void (*destroy)(ImTextureID) = nullptr;
};
void setTextureApi(const TextureApi& api);

constexpr int kCoverPx = 256;  // lado de las miniaturas

// Estado de la carátula de una pista (para el panel de detalles).
struct CoverStatus {
    enum State { Unknown, Loading, Ready, NoImage, Undecodable } state = Unknown;
    std::string source;   // de dónde salió ("ID3v2.3 (portada)", "imagen de la carpeta: …")
    std::string format;   // "JPEG", "PNG", "WebP"…
    size_t bytes = 0;
};

class CoverCache {
public:
    CoverCache();
    ~CoverCache();
    // Textura de la carátula (0 si no tiene o si aún se está leyendo).
    ImTextureID get(const std::wstring& path);
    CoverStatus status(const std::wstring& path);
    // Sube las miniaturas decodificadas (llamar una vez por fotograma).
    void tick();
    // Vuelve a leer la carátula de una pista (tras asignarla o descargarla).
    void invalidate(const std::wstring& path);
    // Libera todas las texturas (antes de destruir el dispositivo gráfico).
    void clear();

private:
    struct Impl;
    Impl* d_;
};

CoverCache& covers();

// Dibuja la carátula de una pista (o una de sustitución) en el rectángulo.
void DrawCover(ImDrawList* dl, const std::wstring& path, ImVec2 a, ImVec2 b, float rounding, const char* seed);

// ------------------------------------------------------------------ carátulas de Internet
// Busca la portada por artista y título (iTunes y Deezer, sin clave) y la guarda
// como carátula asignada (%APPDATA%\RamiroDJ\caratulas). Nunca modifica el audio.
// Si la pista ya tiene carátula (incrustada o de carpeta) no se busca.
class CoverFetcher {
public:
    enum class State { None, Queued, Searching, Found, NotFound, Failed, HasCover };
    static CoverFetcher& get();
    void request(const std::wstring& path, const std::string& artist, const std::string& title, bool manual);
    State state(const std::wstring& path);
    int pending();
    void cancelAll();
    // Memoria de búsquedas (caratulas\busquedas.json): la búsqueda automática no
    // repite pistas que ya tenían carátula ni las no encontradas en los últimos 30 días.
    bool skipAuto(const std::wstring& path);
    void saveMemory();
    void resetFailed();   // tras una pausa sin conexión, se puede volver a intentar
    // Resultados terminados desde la última llamada: (ruta, estado, manual).
    struct Done { std::wstring path; State state; bool manual; std::string detail; };
    std::vector<Done> takeFinished();
    ~CoverFetcher();

private:
    CoverFetcher();
    struct Impl;
    Impl* d_;
};

} // namespace rdj::ui
