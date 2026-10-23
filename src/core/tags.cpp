#include "tags.h"
#include "util.h"
#include "../audio/analysis.h"
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <vector>
#include <filesystem>
#include <cwctype>
#include <cctype>
#include <chrono>
#include <mutex>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace rdj {

int ratingFromPopm(int v) {
    if (v <= 0) return 0;
    if (v < 32) return 1;
    if (v < 96) return 2;
    if (v < 160) return 3;
    if (v < 224) return 4;
    return 5;
}

namespace {

std::vector<uint8_t> readRange(FILE* f, long off, size_t n, int whence = SEEK_SET) {
    std::vector<uint8_t> b;
    if (fseek(f, off, whence) != 0) return b;
    b.resize(n);
    b.resize(fread(b.data(), 1, n, f));
    return b;
}

std::string latin1ToUtf8(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n && p[i]; ++i) {
        const uint8_t c = p[i];
        if (c < 0x80) s.push_back((char)c);
        else { s.push_back((char)(0xC0 | (c >> 6))); s.push_back((char)(0x80 | (c & 0x3F))); }
    }
    return s;
}

std::string utf16ToUtf8(const uint8_t* p, size_t n, bool bigEndian) {
    std::wstring w;
    for (size_t i = 0; i + 1 < n; i += 2) {
        const wchar_t c = bigEndian ? (wchar_t)((p[i] << 8) | p[i + 1]) : (wchar_t)(p[i] | (p[i + 1] << 8));
        if (c == 0) break;
        w.push_back(c);
    }
    return toUtf8(w);
}

std::string id3Text(const uint8_t* p, size_t n) {
    if (n < 1) return {};
    const uint8_t enc = p[0];
    ++p; --n;
    switch (enc) {
    case 0: return latin1ToUtf8(p, n);
    case 1: {
        if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) return utf16ToUtf8(p + 2, n - 2, true);
        if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) return utf16ToUtf8(p + 2, n - 2, false);
        return utf16ToUtf8(p, n, false);
    }
    case 2: return utf16ToUtf8(p, n, true);
    default: {
        std::string s((const char*)p, n);
        const size_t z = s.find('\0');
        return z == std::string::npos ? s : s.substr(0, z);
    }
    }
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

// Géneros ID3v1 (los números que algunas etiquetas guardan en TCON como "(17)").
const char* const kGenres[] = {
    "Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk", "Grunge", "Hip-Hop", "Jazz", "Metal", "New Age", "Oldies", "Other", "Pop", "R&B",
    "Rap", "Reggae", "Rock", "Techno", "Industrial", "Alternative", "Ska", "Death Metal", "Pranks", "Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop",
    "Vocal", "Jazz+Funk", "Fusion", "Trance", "Classical", "Instrumental", "Acid", "House", "Game", "Sound Clip", "Gospel", "Noise", "AlternRock", "Bass",
    "Soul", "Punk", "Space", "Meditative", "Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic", "Darkwave", "Techno-Industrial", "Electronic",
    "Pop-Folk", "Eurodance", "Dream", "Southern Rock", "Comedy", "Cult", "Gangsta", "Top 40", "Christian Rap", "Pop/Funk", "Jungle", "Native American",
    "Cabaret", "New Wave", "Psychedelic", "Rave", "Showtunes", "Trailer", "Lo-Fi", "Tribal", "Acid Punk", "Acid Jazz", "Polka", "Retro", "Musical",
    "Rock & Roll", "Hard Rock", "Folk", "Folk-Rock", "National Folk", "Swing", "Fast Fusion", "Bebop", "Latin", "Revival", "Celtic", "Bluegrass",
    "Avantgarde", "Gothic Rock", "Progressive Rock", "Psychedelic Rock", "Symphonic Rock", "Slow Rock", "Big Band", "Chorus", "Easy Listening", "Acoustic",
    "Humour", "Speech", "Chanson", "Opera", "Chamber Music", "Sonata", "Symphony", "Booty Bass", "Primus", "Porn Groove", "Satire", "Slow Jam", "Club",
    "Tango", "Samba", "Folklore", "Ballad", "Power Ballad", "Rhythmic Soul", "Freestyle", "Duet", "Punk Rock", "Drum Solo", "A cappella", "Euro-House",
    "Dance Hall", "Goa", "Drum & Bass", "Club-House", "Hardcore", "Terror", "Indie", "BritPop", "Afro-Punk", "Polsk Punk", "Beat", "Christian Gangsta Rap",
    "Heavy Metal", "Black Metal", "Crossover", "Contemporary Christian", "Christian Rock", "Merengue", "Salsa", "Thrash Metal", "Anime", "JPop", "Synthpop"};

// "(17)", "17", "(17)Rock", "Electronic; House"… a texto legible.
std::string genreText(std::string g) {
    while (!g.empty() && g[0] == '(' && g.size() > 1 && std::isdigit((unsigned char)g[1])) {
        const size_t close = g.find(')');
        if (close == std::string::npos) break;
        const int n = std::atoi(g.c_str() + 1);
        const std::string rest = g.substr(close + 1);
        if (!rest.empty()) { g = rest; continue; }
        return n >= 0 && n < (int)(sizeof kGenres / sizeof *kGenres) ? kGenres[n] : "";
    }
    if (!g.empty() && std::all_of(g.begin(), g.end(), [](char c) { return std::isdigit((unsigned char)c); })) {
        const int n = std::atoi(g.c_str());
        return n >= 0 && n < (int)(sizeof kGenres / sizeof *kGenres) ? kGenres[n] : "";
    }
    if (g == "RX") return "Remix";
    if (g == "CR") return "Cover";
    // Varios géneros separados por NUL (ID3v2.4): se muestran con " / ".
    for (auto& c : g) if (c == '\0') c = '/';
    return g;
}

void parseId3v1(FILE* f, TagInfo& t) {
    auto b = readRange(f, -128, 128, SEEK_END);
    if (b.size() != 128 || memcmp(b.data(), "TAG", 3) != 0) return;
    if (t.title.empty()) t.title = trim(latin1ToUtf8(&b[3], 30));
    if (t.artist.empty()) t.artist = trim(latin1ToUtf8(&b[33], 30));
    if (t.genre.empty() && b[127] < (int)(sizeof kGenres / sizeof *kGenres)) t.genre = kGenres[b[127]];
}

void parseVorbisComments(const uint8_t* p, size_t n, TagInfo& t) {
    auto le32 = [&](size_t o) -> uint32_t { return o + 4 <= n ? (p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | ((uint32_t)p[o + 3] << 24)) : 0; };
    size_t o = 0;
    const uint32_t vendor = le32(o); o += 4 + vendor;
    const uint32_t count = le32(o); o += 4;
    for (uint32_t i = 0; i < count && o + 4 <= n; ++i) {
        const uint32_t len = le32(o); o += 4;
        if (o + len > n) break;
        std::string kv((const char*)p + o, len);
        o += len;
        const size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        std::string k = kv.substr(0, eq);
        for (auto& c : k) c = (char)toupper((unsigned char)c);
        const std::string v = kv.substr(eq + 1);
        if (k == "TITLE" && t.title.empty()) t.title = trim(v);
        else if (k == "ARTIST" && t.artist.empty()) t.artist = trim(v);
        else if ((k == "BPM" || k == "TEMPO") && t.bpm == 0) t.bpm = std::atof(v.c_str());
        else if ((k == "INITIALKEY" || k == "KEY") && t.key < 0) t.key = parseKey(v);
        else if (k == "GENRE" && t.genre.empty()) t.genre = trim(v);
        else if (k == "FMPS_RATING" && t.rating == 0) t.rating = ratingFromPopm((int)std::lround(std::atof(v.c_str()) * 255));
        else if (k == "RATING" && t.rating == 0) {
            const double r = std::atof(v.c_str());
            t.rating = r <= 5 ? (int)std::lround(r) : r <= 100 ? ratingFromPopm((int)std::lround(r * 2.55)) : ratingFromPopm((int)r);
        }
    }
}

long flacStart(FILE* f);  // definida más abajo (salta una etiqueta ID3 delante de "fLaC")

void parseFlac(FILE* f, TagInfo& t) {
    const long start = flacStart(f);
    if (start < 0) return;
    long off = start + 4;
    for (int guard = 0; guard < 64; ++guard) {
        auto bh = readRange(f, off, 4);
        if (bh.size() < 4) return;
        const bool last = bh[0] & 0x80;
        const int type = bh[0] & 0x7F;
        const uint32_t len = (bh[1] << 16) | (bh[2] << 8) | bh[3];
        if (type == 4) { auto b = readRange(f, off + 4, len); parseVorbisComments(b.data(), b.size(), t); return; }
        off += 4 + (long)len;
        if (last) return;
    }
}

void parseOgg(FILE* f, TagInfo& t) {
    auto b = readRange(f, 0, 128 * 1024);
    static const uint8_t sig[7] = {0x03, 'v', 'o', 'r', 'b', 'i', 's'};
    for (size_t i = 0; i + 7 < b.size(); ++i)
        if (memcmp(&b[i], sig, 7) == 0) { parseVorbisComments(&b[i + 7], b.size() - i - 7, t); return; }
}

void parseWav(FILE* f, TagInfo& t) {
    auto h = readRange(f, 0, 12);
    if (h.size() < 12 || memcmp(h.data(), "RIFF", 4) != 0 || memcmp(&h[8], "WAVE", 4) != 0) return;
    long off = 12;
    for (int guard = 0; guard < 256; ++guard) {
        auto ch = readRange(f, off, 8);
        if (ch.size() < 8) return;
        const uint32_t len = ch[4] | (ch[5] << 8) | (ch[6] << 16) | ((uint32_t)ch[7] << 24);
        if (memcmp(ch.data(), "LIST", 4) == 0 && len >= 4 && len < 1024 * 1024) {
            auto b = readRange(f, off + 8, len);
            if (b.size() >= 4 && memcmp(b.data(), "INFO", 4) == 0) {
                size_t p = 4;
                while (p + 8 <= b.size()) {
                    const uint32_t sl = b[p + 4] | (b[p + 5] << 8) | (b[p + 6] << 16) | ((uint32_t)b[p + 7] << 24);
                    if (p + 8 + sl > b.size()) break;
                    const std::string id((const char*)&b[p], 4);
                    const std::string v = trim(std::string((const char*)&b[p + 8], sl));
                    if (id == "INAM") t.title = v;
                    else if (id == "IART") t.artist = v;
                    p += 8 + sl + (sl & 1);
                }
            }
        }
        off += 8 + (long)len + (len & 1);
    }
}

// =================================================================== ID3v2 (texto y carátulas)
// Lector tolerante: muchos programas escriben etiquetas "casi" correctas y un
// lector estricto se rinde al primer fallo (y la carátula no aparece).
//  - ID3v2.4 con tamaños de marco no synchsafe (iTunes y otros): se prueban las
//    dos lecturas y se queda la que recorre la etiqueta limpiamente.
//  - Marcos vacíos: se saltan en lugar de abandonar la etiqueta.
//  - Varias etiquetas ID3 seguidas (o separadas por ceros), y etiqueta al final
//    del archivo con pie "3DI".
//  - Desincronización por etiqueta (v2.3) o por marco (v2.4), agrupación y DLI.

std::vector<uint8_t> deUnsync(const uint8_t* p, size_t n) {
    std::vector<uint8_t> o;
    o.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        o.push_back(p[i]);
        if (p[i] == 0xFF && i + 1 < n && p[i + 1] == 0) ++i;
    }
    return o;
}

uint32_t synchsafe(const uint8_t* b) { return ((b[0] & 0x7F) << 21) | ((b[1] & 0x7F) << 14) | ((b[2] & 0x7F) << 7) | (b[3] & 0x7F); }
uint32_t be32(const uint8_t* b) { return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]; }

struct Id3Tag {
    int ver = 0;
    uint8_t flags = 0;
    std::vector<uint8_t> body;   // tras la cabecera (ya sin desincronizar si v2.2/v2.3)
    size_t frameStart = 0;       // tras la cabecera extendida
    long totalLen = 0;           // bytes que ocupa en el archivo (cabecera + cuerpo + pie)
};

bool parseId3Header(const std::vector<uint8_t>& h, Id3Tag& t, uint32_t& size) {
    if (h.size() < 10 || memcmp(h.data(), "ID3", 3) != 0) return false;
    t.ver = h[3]; t.flags = h[5];
    if (t.ver < 2 || t.ver > 4 || h[4] == 0xFF) return false;
    if ((h[6] | h[7] | h[8] | h[9]) & 0x80) return false;  // el tamaño de la etiqueta siempre es synchsafe
    size = synchsafe(&h[6]);
    return size <= 64u * 1024 * 1024;
}

bool readId3At(FILE* f, long off, Id3Tag& t) {
    uint32_t size = 0;
    if (!parseId3Header(readRange(f, off, 10), t, size)) return false;
    t.body = readRange(f, off + 10, size);
    if (t.body.empty()) return false;
    t.totalLen = 10 + (long)size + ((t.ver == 4 && (t.flags & 0x10)) ? 10 : 0);
    if (t.ver == 2 && (t.flags & 0x40)) return false;  // v2.2 comprimida: sin uso real
    if (t.ver <= 3 && (t.flags & 0x80)) t.body = deUnsync(t.body.data(), t.body.size());
    t.frameStart = 0;
    if (t.ver >= 3 && (t.flags & 0x40) && t.body.size() >= 4) {
        const size_t ext = t.ver == 4 ? synchsafe(t.body.data()) : be32(t.body.data()) + 4;
        if (ext < t.body.size()) t.frameStart = ext;
    }
    return true;
}

// Todas las etiquetas ID3v2 del principio del archivo (y la del final, si la hay).
std::vector<Id3Tag> readAllId3(FILE* f, long base = 0) {
    std::vector<Id3Tag> out;
    long off = base;
    for (int guard = 0; guard < 4; ++guard) {
        Id3Tag t;
        if (!readId3At(f, off, t)) {
            // Relleno de ceros entre etiquetas: buscar otra cabecera en los 4 KB siguientes.
            if (out.empty()) break;
            const auto win = readRange(f, off, 4096);
            size_t k = 0;
            while (k < win.size() && win[k] == 0) ++k;
            if (k == 0 || k + 10 > win.size() || memcmp(&win[k], "ID3", 3) != 0) break;
            off += (long)k;
            continue;
        }
        off += t.totalLen;
        out.push_back(std::move(t));
    }
    if (base == 0) {
        // ID3v2.4 al final del archivo (pie "3DI"), antes de una posible ID3v1.
        for (long tail : {10L, 138L}) {
            const auto foot = readRange(f, -tail, 10, SEEK_END);
            if (foot.size() == 10 && memcmp(foot.data(), "3DI", 3) == 0 && !((foot[6] | foot[7] | foot[8] | foot[9]) & 0x80)) {
                if (fseek(f, 0, SEEK_END) != 0) break;
                const long end = ftell(f);
                const long start = end - tail - (long)synchsafe(&foot[6]) - 10;
                Id3Tag t;
                if (start > 0 && readId3At(f, start, t)) out.push_back(std::move(t));
                break;
            }
        }
    }
    return out;
}

bool validFrameId(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (!((p[i] >= 'A' && p[i] <= 'Z') || (p[i] >= '0' && p[i] <= '9'))) return false;
    return true;
}

struct Id3Frame {
    std::string id;
    size_t off = 0;     // inicio de los datos en Id3Tag::body
    uint32_t size = 0;
    uint8_t fmt = 0;    // banderas de formato del marco (v2.3/v2.4)
};

std::vector<Id3Frame> id3Frames(const Id3Tag& t) {
    const size_t idLen = t.ver == 2 ? 3 : 4, hdr = t.ver == 2 ? 6 : 10;
    const auto& b = t.body;
    auto walk = [&](bool synch, bool& clean) {
        std::vector<Id3Frame> out;
        clean = false;
        size_t p = t.frameStart;
        while (p + hdr <= b.size()) {
            if (b[p] == 0) { clean = true; return out; }  // relleno
            if (!validFrameId(&b[p], idLen)) return out;
            uint32_t fs;
            if (t.ver == 2) fs = (b[p + 3] << 16) | (b[p + 4] << 8) | b[p + 5];
            else if (synch) {
                if ((b[p + 4] | b[p + 5] | b[p + 6] | b[p + 7]) & 0x80) return out;  // no puede ser synchsafe
                fs = synchsafe(&b[p + 4]);
            } else fs = be32(&b[p + 4]);
            if (p + hdr + (size_t)fs > b.size()) return out;
            out.push_back(Id3Frame{std::string((const char*)&b[p], idLen), p + hdr, fs, t.ver >= 3 ? b[p + 9] : (uint8_t)0});
            p += hdr + fs;  // un marco vacío solo avanza la cabecera
        }
        clean = p >= b.size() || (b.size() - p < hdr);
        return out;
    };
    bool c1 = false, c2 = false;
    if (t.ver != 4) return walk(false, c1);
    auto a = walk(true, c1);
    if (c1) return a;
    auto nb = walk(false, c2);
    return (c2 || nb.size() > a.size()) ? nb : a;
}

// Datos útiles de un marco (sin desincronizar, sin byte de grupo ni DLI). false si está comprimido o cifrado.
bool frameData(const Id3Tag& t, const Id3Frame& fr, std::vector<uint8_t>& data) {
    data.assign(t.body.begin() + fr.off, t.body.begin() + fr.off + fr.size);
    if (t.ver == 4) {
        if ((fr.fmt & 0x02) || (t.flags & 0x80)) data = deUnsync(data.data(), data.size());
        if (fr.fmt & 0x0C) return false;
        if (fr.fmt & 0x40) { if (data.empty()) return false; data.erase(data.begin()); }
        if (fr.fmt & 0x01) { if (data.size() < 4) return false; data.erase(data.begin(), data.begin() + 4); }
    } else if (t.ver == 3) {
        if (fr.fmt & 0xC0) return false;
        if (fr.fmt & 0x20) { if (data.empty()) return false; data.erase(data.begin()); }
    }
    return true;
}

// Salta la descripción terminada en 0 (o 00 00 en UTF-16) de APIC/PIC.
size_t skipDesc(const uint8_t* d, size_t i, size_t n, uint8_t enc) {
    if (enc == 1 || enc == 2) {
        while (i + 1 < n && !(d[i] == 0 && d[i + 1] == 0)) i += 2;
        return i + 2;
    }
    while (i < n && d[i]) ++i;
    return i + 1;
}

bool imageSignature(const uint8_t* p, size_t n) {
    if (n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return true;                   // JPEG
    if (n >= 8 && memcmp(p, "\x89PNG\r\n\x1a\n", 8) == 0) return true;                          // PNG
    if (n >= 12 && memcmp(p, "RIFF", 4) == 0 && memcmp(p + 8, "WEBP", 4) == 0) return true;     // WebP
    if (n >= 6 && (memcmp(p, "GIF87a", 6) == 0 || memcmp(p, "GIF89a", 6) == 0)) return true;    // GIF
    if (n >= 2 && p[0] == 'B' && p[1] == 'M') return true;                                      // BMP
    return false;
}

// Imagen de un marco APIC (v2.3/2.4) o PIC (v2.2). type = tipo de imagen (3 = portada).
bool apicImage(const std::vector<uint8_t>& data, bool pic, std::vector<uint8_t>& img, int& type) {
    const uint8_t* d = data.data();
    const size_t n = data.size();
    if (n < 6) return false;
    const uint8_t enc = d[0];
    size_t i = 1;
    if (!pic) { while (i < n && d[i]) ++i; ++i; }
    else i += 3;
    if (i >= n) return false;
    type = d[i++];
    const size_t descStart = i;
    i = skipDesc(d, i, n, enc);
    if (i > n) return false;
    if (!imageSignature(d + std::min(i, n), n - std::min(i, n))) {
        // Descripciones mal terminadas: buscar la firma de la imagen cerca.
        bool found = false;
        for (size_t k = descStart; k < n && k < descStart + 512; ++k)
            if (imageSignature(d + k, n - k)) { i = k; found = true; break; }
        if (!found && i >= n) return false;
    }
    img.assign(d + i, d + n);
    return img.size() >= 16;
}

// Primera portada (tipo 3) de las etiquetas, o la primera imagen que haya.
std::vector<uint8_t> id3CoverFrom(const std::vector<Id3Tag>& tags, std::string* source) {
    std::vector<uint8_t> first;
    std::string firstSrc;
    std::vector<uint8_t> data;
    for (const auto& t : tags) {
        for (const auto& fr : id3Frames(t)) {
            if (fr.id != "APIC" && fr.id != "PIC") continue;
            if (!frameData(t, fr, data)) continue;
            std::vector<uint8_t> img;
            int type = 0;
            if (!apicImage(data, fr.id == "PIC", img, type)) continue;
            const std::string src = "ID3v2." + std::to_string(t.ver) + (type == 3 ? " (portada)" : " (imagen)");
            if (type == 3) { if (source) *source = src; return img; }
            if (first.empty()) { first = std::move(img); firstSrc = src; }
        }
    }
    if (!first.empty()) { if (source) *source = firstSrc; return first; }
    // Último recurso: etiquetas dañadas con la imagen dentro. Se busca la firma
    // JPEG/PNG en el cuerpo de la etiqueta y se toma hasta el final de esta.
    for (const auto& t : tags) {
        const auto& b = t.body;
        for (size_t k = 0; k + 8 < b.size(); ++k) {
            const bool jpg = b[k] == 0xFF && b[k + 1] == 0xD8 && b[k + 2] == 0xFF;
            const bool png = !jpg && memcmp(&b[k], "\x89PNG\r\n\x1a\n", 8) == 0;
            if (!jpg && !png) continue;
            std::vector<uint8_t> img(b.begin() + k, b.end());
            if (t.ver == 4) img = deUnsync(img.data(), img.size());
            if (source) *source = "ID3v2." + std::to_string(t.ver) + " (recuperada de una etiqueta dañada)";
            return img;
        }
    }
    return {};
}

std::string id3TextFrame(const Id3Tag& t, const Id3Frame& fr) {
    std::vector<uint8_t> data;
    if (!frameData(t, fr, data) || data.empty()) return {};
    return trim(id3Text(data.data(), data.size()));
}

void parseId3Tags(const std::vector<Id3Tag>& tags, TagInfo& ti) {
    for (const auto& t : tags) {
        for (const auto& fr : id3Frames(t)) {
            if ((fr.id == "TIT2" || fr.id == "TT2") && ti.title.empty()) ti.title = id3TextFrame(t, fr);
            else if ((fr.id == "TPE1" || fr.id == "TP1") && ti.artist.empty()) ti.artist = id3TextFrame(t, fr);
            else if ((fr.id == "TBPM" || fr.id == "TBP") && ti.bpm == 0) ti.bpm = std::atof(id3TextFrame(t, fr).c_str());
            else if ((fr.id == "TKEY" || fr.id == "TKE") && ti.key < 0) ti.key = parseKey(id3TextFrame(t, fr));
            else if ((fr.id == "TCON" || fr.id == "TCO") && ti.genre.empty()) ti.genre = trim(genreText(id3TextFrame(t, fr)));
            else if ((fr.id == "POPM" || fr.id == "POP") && ti.rating == 0) {
                // POPM: correo (terminado en 0), valoración 0..255, contador.
                std::vector<uint8_t> data;
                if (frameData(t, fr, data)) {
                    size_t z = 0;
                    while (z < data.size() && data[z]) ++z;
                    if (z + 1 < data.size()) ti.rating = ratingFromPopm(data[z + 1]);
                }
            }
        }
    }
}

// Posición del bloque "fLaC" (algunos programas ponen una etiqueta ID3 delante).
long flacStart(FILE* f) {
    auto h = readRange(f, 0, 4);
    if (h.size() == 4 && memcmp(h.data(), "fLaC", 4) == 0) return 0;
    long off = 0;
    for (int guard = 0; guard < 4; ++guard) {
        Id3Tag t;
        if (!readId3At(f, off, t)) break;
        off += t.totalLen;
        h = readRange(f, off, 4);
        if (h.size() == 4 && memcmp(h.data(), "fLaC", 4) == 0) return off;
    }
    return -1;
}

// "id3 " dentro de un WAV (RIFF): posición de la etiqueta o -1.
long wavId3Offset(FILE* f) {
    auto h = readRange(f, 0, 12);
    if (h.size() != 12 || memcmp(h.data(), "RIFF", 4) != 0 || memcmp(h.data() + 8, "WAVE", 4) != 0) return -1;
    if (fseek(f, 0, SEEK_END) != 0) return -1;
    const long end = ftell(f);
    long off = 12;
    for (int guard = 0; guard < 4096 && off <= end - 8; ++guard) {
        auto c = readRange(f, off, 8);
        if (c.size() != 8) break;
        const uint32_t n = (uint32_t)c[4] | ((uint32_t)c[5] << 8) | ((uint32_t)c[6] << 16) | ((uint32_t)c[7] << 24);
        if (n > (uint32_t)(end - off - 8)) break;
        if (!memcmp(c.data(), "id3 ", 4) || !memcmp(c.data(), "ID3 ", 4)) return off + 8;
        const uint64_t next = (uint64_t)off + 8 + n + (n & 1);
        if (next > (uint64_t)end) break;
        off = (long)next;
    }
    return -1;
}

// Carátula en una etiqueta APEv2 (foobar2000 y otros la usan también en MP3).
std::vector<uint8_t> apeCover(FILE* f) {
    if (fseek(f, 0, SEEK_END) != 0) return {};
    const long end = ftell(f);
    for (long footAt : {end - 32, end - 128 - 32}) {
        if (footAt < 0) continue;
        const auto ft = readRange(f, footAt, 32);
        if (ft.size() != 32 || memcmp(ft.data(), "APETAGEX", 8) != 0) continue;
        auto le = [](const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); };
        const uint32_t size = le(&ft[12]), count = le(&ft[16]);
        if (size < 32 || size > 32u * 1024 * 1024 || (long)size - 32 > footAt) return {};
        const auto items = readRange(f, footAt - ((long)size - 32), size - 32);
        size_t p = 0;
        for (uint32_t i = 0; i < count && p + 8 < items.size(); ++i) {
            const uint32_t vlen = le(&items[p]);
            size_t k = p + 8;
            while (k < items.size() && items[k]) ++k;
            if (k >= items.size()) break;
            std::string key((const char*)&items[p + 8], k - p - 8);
            for (auto& c : key) c = (char)std::tolower((unsigned char)c);
            const size_t v = k + 1;
            if (v + vlen > items.size()) break;
            if (key.rfind("cover art", 0) == 0) {
                size_t z = v;
                while (z < v + vlen && items[z]) ++z;  // nombre de archivo + 0 + imagen
                if (z + 1 < v + vlen) return std::vector<uint8_t>(items.begin() + z + 1, items.begin() + v + vlen);
            }
            p = v + vlen;
        }
        return {};
    }
    return {};
}

std::vector<uint8_t> flacPicture(const uint8_t* p, size_t n, uint32_t* typeOut) {
    auto be32 = [&](size_t o) -> uint32_t { return o + 4 <= n ? ((uint32_t)p[o] << 24) | (p[o + 1] << 16) | (p[o + 2] << 8) | p[o + 3] : 0xFFFFFFFFu; };
    size_t o = 0;
    const uint32_t type = be32(o); o += 4;
    const uint32_t ml = be32(o); if (ml > n) return {}; o += 4 + ml;
    const uint32_t dl = be32(o); if (dl > n) return {}; o += 4 + dl;
    o += 16;
    const uint32_t len = be32(o); o += 4;
    if (len == 0xFFFFFFFFu || o + len > n) return {};
    if (typeOut) *typeOut = type;
    return std::vector<uint8_t>(p + o, p + o + len);
}

std::vector<uint8_t> flacCover(FILE* f) {
    const long start = flacStart(f);
    if (start < 0) return {};
    long off = start + 4;
    std::vector<uint8_t> first;
    for (int guard = 0; guard < 128; ++guard) {
        auto bh = readRange(f, off, 4);
        if (bh.size() < 4) break;
        const bool last = bh[0] & 0x80;
        const int type = bh[0] & 0x7F;
        const uint32_t len = (bh[1] << 16) | (bh[2] << 8) | bh[3];
        if (type == 6) {
            auto b = readRange(f, off + 4, len);
            uint32_t pt = 0;
            auto img = flacPicture(b.data(), b.size(), &pt);
            if (!img.empty() && pt == 3) return img;
            if (first.empty()) first = std::move(img);
        }
        off += 4 + (long)len;
        if (last) break;
    }
    return first;
}

std::vector<uint8_t> base64Decode(const std::string& s) {
    static int8_t T[256];
    static bool init = false;
    if (!init) {
        for (auto& v : T) v = -1;
        const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) T[(uint8_t)a[i]] = (int8_t)i;
        init = true;
    }
    std::vector<uint8_t> o;
    o.reserve(s.size() * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    for (unsigned char c : s) {
        if (T[c] < 0) continue;
        acc = (acc << 6) | T[c];
        bits += 6;
        if (bits >= 8) { bits -= 8; o.push_back((uint8_t)((acc >> bits) & 0xFF)); }
    }
    return o;
}

// OGG Vorbis: reconstruye el paquete de comentarios (puede ocupar varias páginas).
std::vector<uint8_t> oggCover(FILE* f) {
    std::vector<uint8_t> packet;
    int packetNo = 0;
    long off = 0;
    for (int guard = 0; guard < 8192; ++guard) {
        auto hdr = readRange(f, off, 27);
        if (hdr.size() < 27 || memcmp(hdr.data(), "OggS", 4) != 0) return {};
        const int nseg = hdr[26];
        auto lace = readRange(f, off + 27, nseg);
        if ((int)lace.size() < nseg) return {};
        size_t bodyLen = 0;
        for (uint8_t l : lace) bodyLen += l;
        auto body = readRange(f, off + 27 + nseg, bodyLen);
        size_t bo = 0;
        for (int i = 0; i < nseg && bo <= body.size(); ++i) {
            const size_t len = std::min<size_t>(lace[i], body.size() - bo);
            if (packetNo == 1) packet.insert(packet.end(), body.begin() + bo, body.begin() + bo + len);
            bo += len;
            if (lace[i] < 255) {
                if (packetNo == 1) {
                    if (packet.size() < 7 || packet[0] != 3) return {};
                    const uint8_t* p = packet.data() + 7;
                    const size_t n = packet.size() - 7;
                    auto le32 = [&](size_t o) -> uint32_t { return o + 4 <= n ? (p[o] | (p[o + 1] << 8) | (p[o + 2] << 16) | ((uint32_t)p[o + 3] << 24)) : 0; };
                    size_t o = 0;
                    o += 4 + le32(o);
                    const uint32_t count = le32(o); o += 4;
                    for (uint32_t c = 0; c < count && o + 4 <= n; ++c) {
                        const uint32_t len2 = le32(o); o += 4;
                        if (o + len2 > n) break;
                        const std::string kv((const char*)p + o, len2);
                        o += len2;
                        const size_t eq = kv.find('=');
                        if (eq == std::string::npos) continue;
                        std::string k = kv.substr(0, eq);
                        for (auto& ch : k) ch = (char)toupper((unsigned char)ch);
                        if (k == "METADATA_BLOCK_PICTURE") {
                            auto blk = base64Decode(kv.substr(eq + 1));
                            auto img = flacPicture(blk.data(), blk.size(), nullptr);
                            if (!img.empty()) return img;
                        } else if (k == "COVERART") {
                            auto img = base64Decode(kv.substr(eq + 1));
                            if (!img.empty()) return img;
                        }
                    }
                    return {};
                }
                ++packetNo;
            }
        }
        off += 27 + nseg + (long)bodyLen;
        if (packet.size() > 24u * 1024 * 1024) return {};
    }
    return {};
}

std::vector<uint8_t> readWholeFile(const std::wstring& p, size_t maxBytes) {
    std::vector<uint8_t> out;
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f) return out;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    if (n > 0 && (size_t)n <= maxBytes) {
        out.resize(n);
        fseek(f, 0, SEEK_SET);
        out.resize(fread(out.data(), 1, n, f));
    }
    fclose(f);
    return out;
}

bool isImageExt(const std::wstring& e) { return e == L".jpg" || e == L".jpeg" || e == L".png" || e == L".webp" || e == L".bmp" || e == L".gif"; }

std::wstring lowerW(std::wstring s) { for (auto& c : s) c = (wchar_t)std::towlower(c); return s; }

// Imagen de carpeta elegida para un directorio (caché: una carpeta con mil
// pistas sin carátula no se lista mil veces).
std::wstring folderImage(const std::filesystem::path& dir) {
    static std::mutex m;
    static std::unordered_map<std::wstring, std::pair<std::wstring, std::chrono::steady_clock::time_point>> cache;
    const std::wstring key = lowerW(dir.wstring());
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(m);
        auto it = cache.find(key);
        if (it != cache.end() && now - it->second.second < std::chrono::seconds(20)) return it->second.first;
    }
    std::wstring best;
    int bestScore = 0, images = 0, audio = 0;
    std::wstring onlyImage;
    std::error_code ec;
    int seen = 0;
    for (std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), endIt; !ec && it != endIt; it.increment(ec)) {
        if (++seen > 3000) break;
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        const std::wstring ext = lowerW(it->path().extension().wstring());
        if (isSupportedAudioExt(ext)) { ++audio; continue; }
        if (!isImageExt(ext)) continue;
        ++images;
        onlyImage = it->path().wstring();
        const std::wstring stem = lowerW(it->path().stem().wstring());
        int score = 0;
        if (stem == L"cover" || stem == L"folder" || stem == L"front") score = 100;
        else if (stem.find(L"_large") != std::wstring::npos && stem.rfind(L"albumart", 0) == 0) score = 90;   // Windows Media Player
        else if (stem.find(L"front") != std::wstring::npos || stem.find(L"cover") != std::wstring::npos) score = 80;
        else if (stem.find(L"portada") != std::wstring::npos || stem.find(L"caratula") != std::wstring::npos || stem.find(L"carátula") != std::wstring::npos) score = 80;
        else if (stem.find(L"folder") != std::wstring::npos || stem.find(L"albumart") != std::wstring::npos) score = 60;
        if (ext == L".jpg" || ext == L".jpeg") score += 2;
        if (score > bestScore) { bestScore = score; best = it->path().wstring(); }
    }
    // Una sola imagen en una carpeta tipo álbum (pocas pistas): es la portada.
    // En carpetas grandes (Descargas…) una imagen suelta no se toma por carátula.
    if (best.empty() && images == 1 && audio <= 40) best = onlyImage;
    std::lock_guard<std::mutex> lk(m);
    if (cache.size() > 256) cache.clear();
    cache[key] = {best, now};
    return best;
}

} // namespace

const char* imageFormatName(const std::vector<uint8_t>& b) {
    const uint8_t* p = b.data();
    const size_t n = b.size();
    if (n >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF) return "JPEG";
    if (n >= 8 && memcmp(p, "\x89PNG\r\n\x1a\n", 8) == 0) return "PNG";
    if (n >= 12 && memcmp(p, "RIFF", 4) == 0 && memcmp(p + 8, "WEBP", 4) == 0) return "WebP";
    if (n >= 6 && (memcmp(p, "GIF87a", 6) == 0 || memcmp(p, "GIF89a", 6) == 0)) return "GIF";
    if (n >= 2 && p[0] == 'B' && p[1] == 'M') return "BMP";
    return n ? "desconocido" : "";
}

std::wstring coverOverridePath(const std::wstring& path) {
    uint64_t hash = 14695981039346656037ull;
    std::wstring key = path;
    for (auto& c : key) { if (c == L'/') c = L'\\'; c = std::towlower(c); }
    for (unsigned char c : toUtf8(key)) { hash ^= c; hash *= 1099511628211ull; }
    return appDataDir() + L"\\caratulas\\" + std::to_wstring(hash) + L".img";
}

std::vector<uint8_t> readSidecarCover(const std::wstring& path, std::string* source) {
    const auto file = std::filesystem::path(path);
    // Imagen con el mismo nombre que la pista: "tema.jpg" o "tema.mp3.jpg" (así las
    // dejan VirtualDJ y algunos reproductores, a veces como archivo oculto).
    for (const wchar_t* ext : {L".jpg", L".jpeg", L".png", L".webp"}) {
        for (int full = 0; full < 2; ++full) {
            std::filesystem::path candidate = file;
            if (full) candidate += ext; else candidate.replace_extension(ext);
            auto img = readWholeFile(candidate.wstring(), 16u * 1024 * 1024);
            if (!img.empty()) { if (source) *source = "imagen junto a la pista: " + toUtf8(candidate.filename().wstring()); return img; }
        }
    }
    // Imagen de la carpeta: cover/folder/front, AlbumArt_{…}_Large (WMP), "portada"… o la única imagen.
    const std::wstring img = folderImage(file.parent_path());
    if (!img.empty()) {
        auto bytes = readWholeFile(img, 16u * 1024 * 1024);
        if (!bytes.empty()) { if (source) *source = "imagen de la carpeta: " + toUtf8(std::filesystem::path(img).filename().wstring()); return bytes; }
    }
    return {};
}

namespace {
// Nombre normalizado para emparejar: minúsculas, sin acentos simples ni signos, espacios simples.
std::string normName(const std::wstring& w) {
    std::string o;
    bool sp = false;
    for (wchar_t c : w) {
        c = (wchar_t)std::towlower(c);
        static const wchar_t* from = L"áàäâãéèëêíìïîóòöôõúùüûñç";
        static const char* to = "aaaaaeeeeiiiiooooouuuunc";
        if (const wchar_t* q = std::wcschr(from, c); q && c) { o += to[q - from]; sp = false; continue; }
        if (std::iswalnum(c) && c < 128) { o += (char)c; sp = false; }
        else if (!sp && !o.empty()) { o += ' '; sp = true; }
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    return o;
}

// Índice de la caché de carátulas de VirtualDJ (se rehace cada 5 minutos).
// Variable de entorno (vacía si no existe).
std::wstring envW(const wchar_t* name) {
#ifdef _WIN32
    wchar_t buf[1024];
    const DWORD n = GetEnvironmentVariableW(name, buf, 1024);
    return n > 0 && n < 1024 ? std::wstring(buf, n) : std::wstring();
#else
    const char* v = std::getenv(std::filesystem::path(name).string().c_str());
    return v ? std::filesystem::path(v).wstring() : std::wstring();
#endif
}

struct VdjCoverIndex {
    std::mutex m;
    std::unordered_map<std::string, std::wstring> byName;
    std::chrono::steady_clock::time_point built{};
    bool everBuilt = false;

    void rebuild() {
        byName.clear();
        namespace fs = std::filesystem;
        std::vector<fs::path> dirs;
        if (auto v = envW(L"RDJ_VDJ_DIR"); !v.empty()) dirs.push_back(fs::path(v) / L"Cache" / L"Covers");
#ifdef _WIN32
        PWSTR docs = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) dirs.push_back(fs::path(docs) / L"VirtualDJ" / L"Cache" / L"Covers");
        if (docs) CoTaskMemFree(docs);
        if (auto v = envW(L"USERPROFILE"); !v.empty()) {
            dirs.push_back(fs::path(v) / L"OneDrive" / L"Documents" / L"VirtualDJ" / L"Cache" / L"Covers");
            dirs.push_back(fs::path(v) / L"OneDrive" / L"Documentos" / L"VirtualDJ" / L"Cache" / L"Covers");
        }
        if (auto v = envW(L"LOCALAPPDATA"); !v.empty()) dirs.push_back(fs::path(v) / L"VirtualDJ" / L"Cache" / L"Covers");
#endif
        std::error_code ec;
        int seen = 0;
        for (auto& d : dirs) {
            if (!std::filesystem::is_directory(d, ec)) continue;
            for (std::filesystem::recursive_directory_iterator it(d, std::filesystem::directory_options::skip_permission_denied, ec), e; !ec && it != e; it.increment(ec)) {
                if (++seen > 200000) break;
                std::error_code ec2;
                if (!it->is_regular_file(ec2)) continue;
                const std::wstring ext = lowerW(it->path().extension().wstring());
                if (!isImageExt(ext)) continue;
                // "Artista - Título.mp3.jpg" -> se quita también la extensión de audio.
                std::filesystem::path stem = it->path().stem();
                if (isSupportedAudioExt(lowerW(stem.extension().wstring()))) stem = stem.stem();
                const std::string k = normName(stem.wstring());
                if (k.size() >= 3) byName.emplace(k, it->path().wstring());
            }
        }
        built = std::chrono::steady_clock::now();
        everBuilt = true;
    }
    std::wstring envSeen;
    std::wstring find(const std::vector<std::string>& keys) {
        std::lock_guard<std::mutex> lk(m);
        const std::wstring env = envW(L"RDJ_VDJ_DIR");
        if (!everBuilt || env != envSeen || std::chrono::steady_clock::now() - built > std::chrono::minutes(5)) { envSeen = env; rebuild(); }
        if (byName.empty()) return {};
        for (auto& k : keys) {
            if (k.size() < 3) continue;
            auto it = byName.find(k);
            if (it != byName.end()) return it->second;
        }
        return {};
    }
};
VdjCoverIndex& vdjCovers() { static VdjCoverIndex i; return i; }
} // namespace

std::vector<uint8_t> readVdjCachedCover(const std::wstring& path, std::string* source) {
    const std::filesystem::path p(path);
    std::vector<std::string> keys;
    keys.push_back(normName(p.stem().wstring()));
    keys.push_back(normName(p.filename().wstring()));
    const std::wstring img = vdjCovers().find(keys);
    if (img.empty()) return {};
    auto bytes = readWholeFile(img, 16u * 1024 * 1024);
    if (!bytes.empty() && source) *source = "caché de VirtualDJ: " + toUtf8(std::filesystem::path(img).filename().wstring());
    return bytes;
}

std::vector<uint8_t> readCoverArt(const std::wstring& path, std::string* source) {
    if (source) source->clear();
    auto img = readWholeFile(coverOverridePath(path), 16u * 1024 * 1024);
    if (!img.empty()) { if (source) *source = "asignada en DJ Elver Galarga"; return img; }
    if (FILE* f = _wfopen(path.c_str(), L"rb")) {
        const std::wstring e = extensionLower(path);
        if (e == L".mp3") {
            img = id3CoverFrom(readAllId3(f), source);
            if (img.empty()) { img = apeCover(f); if (!img.empty() && source) *source = "APEv2"; }
        } else if (e == L".wav") {
            const long at = wavId3Offset(f);
            img = id3CoverFrom(readAllId3(f, at >= 0 ? at : 0), source);
        } else if (e == L".flac") {
            img = flacCover(f);
            if (!img.empty()) { if (source) *source = "FLAC (PICTURE)"; }
            else img = id3CoverFrom(readAllId3(f), source);
        } else if (e == L".ogg") {
            img = oggCover(f);
            if (!img.empty() && source) *source = "OGG (METADATA_BLOCK_PICTURE)";
        }
        fclose(f);
    }
    if (img.empty()) img = readSidecarCover(path, source);
    if (img.empty()) img = readVdjCachedCover(path, source);
    return img;
}

TagInfo readTags(const std::wstring& path) {
    TagInfo t;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (f) {
        const std::wstring e = extensionLower(path);
        if (e == L".mp3") { parseId3Tags(readAllId3(f), t); parseId3v1(f, t); }
        else if (e == L".flac") { parseFlac(f, t); if (t.title.empty() || t.artist.empty()) parseId3Tags(readAllId3(f), t); }
        else if (e == L".ogg") parseOgg(f, t);
        else if (e == L".wav") { parseWav(f, t); const long at = wavId3Offset(f); if (at >= 0) parseId3Tags(readAllId3(f, at), t); }
        fclose(f);
    }
    if (t.title.empty()) {
        std::wstring n = fileNameOf(path);
        const size_t dot = n.find_last_of(L'.');
        if (dot != std::wstring::npos) n = n.substr(0, dot);
        const size_t sep = n.find(L" - ");
        if (sep != std::wstring::npos) {
            if (t.artist.empty()) t.artist = toUtf8(n.substr(0, sep));
            t.title = toUtf8(n.substr(sep + 3));
        } else t.title = toUtf8(n);
    }
    return t;
}

} // namespace rdj
