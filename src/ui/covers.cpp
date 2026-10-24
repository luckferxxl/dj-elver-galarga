#include "covers.h"
#include "widgets.h"
#include "../core/tags.h"
#include "../core/util.h"
#include <windows.h>
#include <wincodec.h>
#include <winhttp.h>
#include <json.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <memory>
#include <condition_variable>
#include <ctime>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rdj::ui {

namespace {
TextureApi g_api;

template <class T> void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Decodifica una imagen (JPEG/PNG/WebP…) a una miniatura RGBA cuadrada (recorte centrado).
bool decodeThumb(IWICImagingFactory* f, const std::vector<uint8_t>& bytes, std::vector<uint8_t>& out) {
    if (!f || bytes.size() < 16) return false;
    IWICStream* st = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* fr = nullptr;
    IWICBitmapClipper* clip = nullptr;
    IWICBitmapScaler* sc = nullptr;
    IWICFormatConverter* cv = nullptr;
    bool ok = false;
    do {
        if (FAILED(f->CreateStream(&st)) || FAILED(st->InitializeFromMemory((BYTE*)bytes.data(), (DWORD)bytes.size()))) break;
        if (FAILED(f->CreateDecoderFromStream(st, nullptr, WICDecodeMetadataCacheOnDemand, &dec))) break;
        if (FAILED(dec->GetFrame(0, &fr))) break;
        UINT w = 0, h = 0;
        fr->GetSize(&w, &h);
        if (!w || !h) break;
        const UINT side = std::min(w, h);
        WICRect rc{(INT)((w - side) / 2), (INT)((h - side) / 2), (INT)side, (INT)side};
        if (FAILED(f->CreateBitmapClipper(&clip)) || FAILED(clip->Initialize(fr, &rc))) break;
        if (FAILED(f->CreateBitmapScaler(&sc)) || FAILED(sc->Initialize(clip, kCoverPx, kCoverPx, WICBitmapInterpolationModeFant))) break;
        if (FAILED(f->CreateFormatConverter(&cv)) ||
            FAILED(cv->Initialize(sc, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
            break;
        out.resize((size_t)kCoverPx * kCoverPx * 4);
        if (FAILED(cv->CopyPixels(nullptr, kCoverPx * 4, (UINT)out.size(), out.data()))) break;
        for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
        ok = true;
    } while (false);
    rel(cv); rel(sc); rel(clip); rel(fr); rel(dec); rel(st);
    return ok;
}
} // namespace

void setTextureApi(const TextureApi& api) { g_api = api; }

struct CoverCache::Impl {
    enum class St { Pending, Ready, None, Bad };
    struct Entry { St st = St::Pending; ImTextureID tex = 0; uint64_t lastUse = 0; std::string source, format; size_t bytes = 0; };
    struct Done { uint64_t epoch = 0; std::wstring path; std::vector<uint8_t> rgba; bool ok = false; std::string source, format; size_t bytes = 0; };
    std::unordered_map<std::wstring, Entry> map;   // solo hilo de la interfaz
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::wstring> queue;                // el más reciente, primero
    std::deque<Done> done;
    std::thread th;
    bool quit = false;
    uint64_t frame = 0, epoch = 0;
    size_t live = 0;

    void worker() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IWICImagingFactory* f = nullptr;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f));
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        for (;;) {
            std::wstring path;
            uint64_t generation;
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [&] { return quit || !queue.empty(); });
                if (quit) break;
                generation = epoch;
                path = queue.back();
                queue.pop_back();
            }
            Done d;
            d.path = path; d.epoch = generation;
            const auto bytes = readCoverArt(path, &d.source);
            d.format = imageFormatName(bytes);
            d.bytes = bytes.size();
            d.ok = !bytes.empty() && decodeThumb(f, bytes, d.rgba);
            if (!d.ok && !bytes.empty()) {
                // La imagen incrustada no se puede decodificar: probar la de la carpeta.
                std::string src2;
                const auto side = readSidecarCover(path, &src2);
                if (!side.empty() && decodeThumb(f, side, d.rgba)) { d.ok = true; d.source = src2; d.format = imageFormatName(side); d.bytes = side.size(); }
            }
            std::lock_guard<std::mutex> lk(m);
            done.push_back(std::move(d));
        }
        rel(f);
        CoUninitialize();
    }
};

CoverCache::CoverCache() : d_(new Impl) { d_->th = std::thread(&Impl::worker, d_); }

CoverCache::~CoverCache() {
    {
        std::lock_guard<std::mutex> lk(d_->m);
        d_->quit = true;
    }
    d_->cv.notify_all();
    if (d_->th.joinable()) d_->th.join();
    delete d_;  // las texturas ya se liberaron con clear() (el dispositivo ya no existe aquí)
}

ImTextureID CoverCache::get(const std::wstring& path) {
    if (path.empty() || !g_api.create) return 0;
    auto it = d_->map.find(path);
    if (it != d_->map.end()) {
        it->second.lastUse = d_->frame;
        return it->second.st == Impl::St::Ready ? it->second.tex : 0;
    }
    Impl::Entry e;
    e.lastUse = d_->frame;
    d_->map[path] = e;
    std::wstring dropped;
    {
        std::lock_guard<std::mutex> lk(d_->m);
        d_->queue.push_back(path);
        if (d_->queue.size() > 96) { dropped = d_->queue.front(); d_->queue.pop_front(); }
    }
    if (!dropped.empty()) d_->map.erase(dropped);  // se volverá a pedir si sigue a la vista
    d_->cv.notify_one();
    return 0;
}

CoverStatus CoverCache::status(const std::wstring& path) {
    CoverStatus s;
    auto it = d_->map.find(path);
    if (it == d_->map.end()) { get(path); s.state = CoverStatus::Loading; return s; }
    const Impl::Entry& e = it->second;
    s.source = e.source; s.format = e.format; s.bytes = e.bytes;
    switch (e.st) {
    case Impl::St::Pending: s.state = CoverStatus::Loading; break;
    case Impl::St::Ready: s.state = CoverStatus::Ready; break;
    case Impl::St::None: s.state = CoverStatus::NoImage; break;
    case Impl::St::Bad: s.state = CoverStatus::Undecodable; break;
    }
    return s;
}

void CoverCache::tick() {
    ++d_->frame;
    std::deque<Impl::Done> batch;
    {
        std::lock_guard<std::mutex> lk(d_->m);
        for (int i = 0; i < 12 && !d_->done.empty(); ++i) { batch.push_back(std::move(d_->done.front())); d_->done.pop_front(); }
    }
    for (auto& r : batch) {
        if (r.epoch != d_->epoch) continue;
        auto it = d_->map.find(r.path);
        if (it == d_->map.end()) continue;
        it->second.source = r.source; it->second.format = r.format; it->second.bytes = r.bytes;
        if (r.ok && g_api.create) {
            it->second.tex = g_api.create(r.rgba.data(), kCoverPx, kCoverPx);
            it->second.st = it->second.tex ? Impl::St::Ready : Impl::St::Bad;
            if (it->second.tex) ++d_->live;
        } else it->second.st = r.bytes ? Impl::St::Bad : Impl::St::None;
    }
    // Los resultados negativos también tienen límite: una biblioteca grande no debe crecer sin fin.
    if (d_->map.size() > 1024) for (auto it = d_->map.begin(); it != d_->map.end();) {
        if ((it->second.st == Impl::St::None || it->second.st == Impl::St::Bad) && d_->frame - it->second.lastUse > 600) it = d_->map.erase(it); else ++it;
    }
    // Liberar las que llevan más tiempo sin dibujarse.
    if (d_->live > 260 && g_api.destroy) {
        std::vector<std::pair<uint64_t, std::wstring>> old;
        for (auto& [k, e] : d_->map)
            if (e.st == Impl::St::Ready && d_->frame - e.lastUse > 90) old.push_back({e.lastUse, k});
        std::sort(old.begin(), old.end());
        for (auto& [t, k] : old) {
            if (d_->live <= 200) break;
            auto it = d_->map.find(k);
            g_api.destroy(it->second.tex);
            d_->map.erase(it);
            --d_->live;
        }
    }
}

void CoverCache::invalidate(const std::wstring& path) {
    auto it = d_->map.find(path);
    if (it == d_->map.end()) return;
    if (it->second.st == Impl::St::Ready && it->second.tex && g_api.destroy) { g_api.destroy(it->second.tex); --d_->live; }
    d_->map.erase(it);
    {
        // Una lectura en curso de esta ruta podría traer la imagen antigua: se descarta al llegar.
        std::lock_guard<std::mutex> lk(d_->m);
        for (auto& dn : d_->done) if (dn.path == path) dn.epoch = ~0ull;
    }
}

void CoverCache::clear() {
    {
        std::lock_guard<std::mutex> lk(d_->m);
        ++d_->epoch;
        d_->queue.clear();
        d_->done.clear();
    }
    if (g_api.destroy)
        for (auto& [k, e] : d_->map)
            if (e.st == Impl::St::Ready && e.tex) g_api.destroy(e.tex);
    d_->map.clear();
    d_->live = 0;
}

CoverCache& covers() {
    static CoverCache c;
    return c;
}

void DrawCover(ImDrawList* dl, const std::wstring& path, ImVec2 a, ImVec2 b, float rounding, const char* seed) {
    const ImTextureID t = covers().get(path);
    if (t) dl->AddImageRounded(ImTextureRef(t), a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
    else ArtPlaceholder(dl, a, b, seed, rounding);
}

// =================================================================== carátulas de Internet

namespace {

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') o.push_back((char)c);
        else { o.push_back('%'); o.push_back(hex[c >> 4]); o.push_back(hex[c & 15]); }
    }
    return o;
}

// Quita lo que estorba en la búsqueda: "(Extended Mix)", "[Official Video]", "ft. …", extensiones.
std::string cleanTitle(std::string t) {
    for (char open : {'(', '['}) {
        const char close = open == '(' ? ')' : ']';
        for (;;) {
            const size_t a = t.find(open);
            if (a == std::string::npos) break;
            const size_t b = t.find(close, a);
            t.erase(a, b == std::string::npos ? std::string::npos : b - a + 1);
        }
    }
    for (const char* cut : {" ft. ", " feat. ", " ft ", " feat ", " Ft. ", " Feat. ", " FT. ", " FEAT. ", " | "}) {
        const size_t p = t.find(cut);
        if (p != std::string::npos) t.erase(p);
    }
    while (!t.empty() && (t.back() == ' ' || t.back() == '-')) t.pop_back();
    while (!t.empty() && t.front() == ' ') t.erase(t.begin());
    return t;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : foldForSearch(s)) {
        if (std::isalnum((unsigned char)c) || (unsigned char)c >= 0x80) cur.push_back(c);
        else { if (cur.size() > 1) out.push_back(cur); cur.clear(); }
    }
    if (cur.size() > 1) out.push_back(cur);
    return out;
}

// Parecido entre lo buscado y un resultado (0..1).
double similarity(const std::string& want, const std::string& got) {
    const auto a = words(want), b = words(got);
    if (a.empty() || b.empty()) return 0;
    int hit = 0;
    for (auto& w : a) if (std::find(b.begin(), b.end(), w) != b.end()) ++hit;
    return (double)hit / a.size();
}

struct Http {
    HINTERNET ses = nullptr;
    Http() {
        ses = WinHttpOpen(L"DJElverGalarga/0.11.3", 4 /*WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY*/, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!ses) ses = WinHttpOpen(L"DJElverGalarga/0.11.3", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (ses) WinHttpSetTimeouts(ses, 6000, 6000, 8000, 10000);
    }
    ~Http() { if (ses) WinHttpCloseHandle(ses); }
    // GET https. Devuelve el código HTTP (0 si no hubo conexión).
    int get(const std::string& urlUtf8, std::vector<uint8_t>& out, size_t maxBytes) {
        out.clear();
        if (!ses) return 0;
        const std::wstring url = fromUtf8(urlUtf8);
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof uc;
        wchar_t host[256], path[2048], extra[2048];
        uc.lpszHostName = host; uc.dwHostNameLength = 256;
        uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
        uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2048;
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return 0;
        const std::wstring full = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
        HINTERNET con = WinHttpConnect(ses, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
        if (!con) return 0;
        int code = 0;
        HINTERNET req = WinHttpOpenRequest(con, L"GET", full.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
        if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr)) {
            DWORD status = 0, sz = sizeof status;
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
            code = (int)status;
            for (;;) {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
                const size_t at = out.size();
                if (at + avail > maxBytes) { out.clear(); code = -1; break; }
                out.resize(at + avail);
                DWORD got = 0;
                if (!WinHttpReadData(req, out.data() + at, avail, &got)) { out.clear(); code = 0; break; }
                out.resize(at + got);
            }
        }
        if (req) WinHttpCloseHandle(req);
        WinHttpCloseHandle(con);
        return code;
    }
};

bool looksLikeImage(const std::vector<uint8_t>& b) {
    const std::string f = imageFormatName(b);
    return b.size() > 512 && (f == "JPEG" || f == "PNG");
}

} // namespace

struct CoverFetcher::Impl {
    struct Job { std::wstring path; std::string artist, title; bool manual; };
    struct Mem { int state = 0; int64_t when = 0; };   // state: 1 tenía, 2 encontrada, 3 no encontrada
    std::unordered_map<std::wstring, Mem> mem;
    bool memLoaded = false, memDirty = false;
    std::mutex m;
    std::condition_variable cv;
    std::deque<Job> queue;
    std::unordered_map<std::wstring, State> states;
    std::vector<Done> finished;
    std::thread th;
    bool quit = false;
    std::atomic<bool> cancel{false};
    std::chrono::steady_clock::time_point lastRequest{};

    void setState(const std::wstring& p, State s) { std::lock_guard<std::mutex> lk(m); states[p] = s; }

    static std::wstring memKey(std::wstring p) { for (auto& c : p) { if (c == L'/') c = L'\\'; c = (wchar_t)std::towlower(c); } return p; }
    static std::wstring memFile() { return appDataDir() + L"\\caratulas\\busquedas.json"; }
    void loadMemoryLocked() {
        if (memLoaded) return;
        memLoaded = true;
        FILE* f = _wfopen(memFile().c_str(), L"rb");
        if (!f) return;
        std::string txt;
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) txt.append(buf, n);
        fclose(f);
        try {
            const auto j = nlohmann::json::parse(txt);
            for (auto it = j.begin(); it != j.end(); ++it)
                if (it.value().is_array() && it.value().size() == 2) mem[fromUtf8(it.key())] = Mem{it.value()[0].get<int>(), it.value()[1].get<int64_t>()};
        } catch (...) {}
    }

    // Pausa entre peticiones (los servicios gratuitos limitan el ritmo).
    void pace() {
        const auto gap = std::chrono::milliseconds(1300);
        const auto now = std::chrono::steady_clock::now();
        if (now - lastRequest < gap) std::this_thread::sleep_for(gap - (now - lastRequest));
        lastRequest = std::chrono::steady_clock::now();
    }

    // Devuelve la URL de la imagen más parecida, o "" si no hay una fiable.
    std::string search(Http& http, const std::string& artist, const std::string& title, std::string& detail, bool manual) {
        const std::string t = cleanTitle(title);
        const std::string query = artist.empty() ? t : artist + " " + t;
        if (words(query).empty()) { detail = "sin artista ni título"; return {}; }
        std::vector<uint8_t> body;
        std::string bestUrl;
        double bestScore = 0;
        auto consider = [&](const std::string& gotArtist, const std::string& gotTitle, const std::string& url, const char* from) {
            if (url.empty()) return;
            const double st = similarity(t, gotTitle), sa = artist.empty() ? 0.6 : similarity(artist, gotArtist);
            const double score = st * 0.6 + sa * 0.4;
            // La búsqueda automática (toda la colección) pide más parecido que la que lanzas tú.
            const double minT = manual ? 0.5 : 0.65, minA = manual ? 0.34 : 0.5;
            if (st >= minT && (artist.empty() || sa >= minA) && score > bestScore) { bestScore = score; bestUrl = url; detail = from; }
        };
        // 1) iTunes Search API
        pace();
        if (http.get("https://itunes.apple.com/search?media=music&entity=song&limit=12&term=" + urlEncode(query), body, 2u << 20) == 200) {
            try {
                const auto j = nlohmann::json::parse(body.begin(), body.end());
                for (const auto& r : j.value("results", nlohmann::json::array())) {
                    std::string u = r.value("artworkUrl100", "");
                    const size_t k = u.find("100x100");
                    if (k != std::string::npos) u.replace(k, 7, "600x600");
                    consider(r.value("artistName", ""), r.value("trackName", ""), u, "iTunes");
                }
            } catch (...) {}
        }
        if (bestScore >= 0.85 || cancel) return bestUrl;
        // 2) Deezer
        pace();
        const std::string dq = artist.empty() ? "track:\"" + t + "\"" : "artist:\"" + artist + "\" track:\"" + t + "\"";
        if (http.get("https://api.deezer.com/search?limit=12&q=" + urlEncode(dq), body, 2u << 20) == 200) {
            try {
                const auto j = nlohmann::json::parse(body.begin(), body.end());
                for (const auto& r : j.value("data", nlohmann::json::array())) {
                    const auto& alb = r.contains("album") ? r["album"] : nlohmann::json::object();
                    std::string u = alb.value("cover_xl", "");
                    if (u.empty()) u = alb.value("cover_big", "");
                    const std::string an = r.contains("artist") ? r["artist"].value("name", "") : "";
                    consider(an, r.value("title", ""), u, "Deezer");
                }
            } catch (...) {}
        }
        if (bestUrl.empty() && detail.empty()) detail = "sin resultados parecidos";
        return bestUrl;
    }

    void worker() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        std::unique_ptr<Http> http;
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [&] { return quit || !queue.empty(); });
                if (quit) break;
                job = std::move(queue.front());
                queue.pop_front();
                states[job.path] = State::Searching;
            }
            Done d{job.path, State::NotFound, job.manual, {}};
            if (!readCoverArt(job.path).empty()) d.state = State::HasCover;  // ya tiene: no se busca
            else {
                if (!http) http = std::make_unique<Http>();
                std::string detail;
                const std::string url = search(*http, job.artist, job.title, detail, job.manual);
                d.detail = detail;
                if (!url.empty() && !cancel) {
                    std::vector<uint8_t> img;
                    const int code = http->get(url, img, 8u << 20);
                    if (code == 200 && looksLikeImage(img)) {
                        const std::filesystem::path dest(coverOverridePath(job.path));
                        std::error_code ec;
                        std::filesystem::create_directories(dest.parent_path(), ec);
                        const auto tmp = dest.wstring() + L".tmp";
                        if (FILE* f = _wfopen(tmp.c_str(), L"wb")) {
                            const bool ok = fwrite(img.data(), 1, img.size(), f) == img.size();
                            fclose(f);
                            if (ok) { std::filesystem::rename(tmp, dest, ec); d.state = ec ? State::Failed : State::Found; }
                            else { std::filesystem::remove(tmp, ec); d.state = State::Failed; }
                        } else d.state = State::Failed;
                    } else d.state = code == 0 ? State::Failed : State::NotFound;
                } else if (detail.empty()) d.state = State::Failed;
                if (d.state == State::Failed && d.detail.empty()) d.detail = "sin conexión con el servicio";
            }
            std::lock_guard<std::mutex> lk(m);
            states[d.path] = d.state;
            if (!cancel && (d.state == State::HasCover || d.state == State::Found || d.state == State::NotFound)) {
                loadMemoryLocked();
                mem[memKey(d.path)] = Mem{d.state == State::HasCover ? 1 : d.state == State::Found ? 2 : 3, (int64_t)std::time(nullptr)};
                memDirty = true;
            }
            finished.push_back(std::move(d));
        }
    }
};

CoverFetcher::CoverFetcher() : d_(new Impl) { d_->th = std::thread(&Impl::worker, d_); }
CoverFetcher::~CoverFetcher() {
    { std::lock_guard<std::mutex> lk(d_->m); d_->quit = true; d_->queue.clear(); }
    d_->cancel = true;
    d_->cv.notify_all();
    if (d_->th.joinable()) d_->th.detach();  // no esperar a una descarga en curso al cerrar
}

CoverFetcher& CoverFetcher::get() {
    static CoverFetcher* f = new CoverFetcher();  // vive hasta el final del proceso
    return *f;
}

void CoverFetcher::request(const std::wstring& path, const std::string& artist, const std::string& title, bool manual) {
    if (path.empty()) return;
    std::lock_guard<std::mutex> lk(d_->m);
    auto it = d_->states.find(path);
    if (it != d_->states.end()) {
        if (it->second == State::Queued || it->second == State::Searching) return;
        if (!manual && it->second != State::None) return;  // la búsqueda automática no insiste
    }
    if (d_->queue.size() >= 600) return;
    d_->cancel = false;
    d_->states[path] = State::Queued;
    Impl::Job j{path, artist, title, manual};
    if (manual) d_->queue.push_front(std::move(j)); else d_->queue.push_back(std::move(j));
    d_->cv.notify_one();
}

bool CoverFetcher::skipAuto(const std::wstring& path) {
    std::lock_guard<std::mutex> lk(d_->m);
    d_->loadMemoryLocked();
    auto it = d_->mem.find(Impl::memKey(path));
    if (it == d_->mem.end()) return false;
    if (it->second.state == 3) return std::time(nullptr) - it->second.when < 30 * 24 * 3600;
    return true;  // ya tenía o ya se descargó
}

void CoverFetcher::saveMemory() {
    std::string out;
    {
        std::lock_guard<std::mutex> lk(d_->m);
        if (!d_->memDirty) return;
        d_->memDirty = false;
        nlohmann::json j = nlohmann::json::object();
        for (auto& [k, v] : d_->mem) j[toUtf8(k)] = nlohmann::json::array({v.state, v.when});
        out = j.dump();
    }
    const std::filesystem::path dest(Impl::memFile());
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    const std::wstring tmp = dest.wstring() + L".tmp";
    if (FILE* f = _wfopen(tmp.c_str(), L"wb")) {
        const bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
        fclose(f);
        if (ok) std::filesystem::rename(tmp, dest, ec);
    }
}

void CoverFetcher::resetFailed() {
    std::lock_guard<std::mutex> lk(d_->m);
    for (auto& [p, st] : d_->states) if (st == State::Failed) st = State::None;
    d_->cancel = false;
}

CoverFetcher::State CoverFetcher::state(const std::wstring& path) {
    std::lock_guard<std::mutex> lk(d_->m);
    auto it = d_->states.find(path);
    return it == d_->states.end() ? State::None : it->second;
}

int CoverFetcher::pending() {
    std::lock_guard<std::mutex> lk(d_->m);
    int n = (int)d_->queue.size();
    for (auto& [p, s] : d_->states) if (s == State::Searching) ++n;
    return n;
}

void CoverFetcher::cancelAll() {
    std::lock_guard<std::mutex> lk(d_->m);
    for (auto& j : d_->queue) d_->states[j.path] = State::None;
    d_->queue.clear();
    d_->cancel = true;
}

std::vector<CoverFetcher::Done> CoverFetcher::takeFinished() {
    std::lock_guard<std::mutex> lk(d_->m);
    std::vector<Done> out;
    out.swap(d_->finished);
    return out;
}

} // namespace rdj::ui
