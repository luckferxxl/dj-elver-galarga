// Lectura mínima de etiquetas: ID3v2/ID3v1 (MP3), comentarios Vorbis (FLAC,
// OGG) y LIST/INFO (WAV). Si no hay etiquetas se usa "Artista - Título" del nombre.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rdj {

struct TagInfo {
    std::string title, artist;  // UTF-8
    double bpm = 0;             // de la etiqueta, si existe (informativo)
    int key = -1;               // tonalidad de la etiqueta (TKEY / INITIALKEY), como en analysis.h; -1 = no hay
    std::string genre;          // TCON / GENRE
    int rating = 0;             // 0 = sin valorar, 1..5 estrellas (POPM, RATING, FMPS_RATING)
};

TagInfo readTags(const std::wstring& path);

// Carátula: la asignada a mano en Ramiro DJ, la incrustada (ID3v2 APIC/PIC
// —también etiquetas mal formadas—, APEv2, FLAC PICTURE, OGG
// METADATA_BLOCK_PICTURE) o, si no hay, una imagen junto a la pista o de su
// carpeta (cover, folder, front, AlbumArt_…_Large, "portada" o la única imagen).
// Devuelve los bytes de la imagen o vacío; source (opcional) dice de dónde salió.
std::vector<uint8_t> readCoverArt(const std::wstring& path, std::string* source = nullptr);
std::vector<uint8_t> readSidecarCover(const std::wstring& path, std::string* source = nullptr);
// Carátula que VirtualDJ descargó de Internet (Documentos\VirtualDJ\Cache\Covers),
// emparejada por nombre ("Artista - Título" o el nombre del archivo). Vacío si no hay.
std::vector<uint8_t> readVdjCachedCover(const std::wstring& path, std::string* source = nullptr);
// Valoración de 0..255 (POPM) o 0..100 a estrellas 0..5.
int ratingFromPopm(int v);
std::wstring coverOverridePath(const std::wstring& path);
// "JPEG", "PNG", "WebP", "GIF", "BMP", "desconocido" o "" si está vacío.
const char* imageFormatName(const std::vector<uint8_t>& bytes);

} // namespace rdj
