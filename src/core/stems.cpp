#include "stems.h"
#include "filename.h"
#include "tags.h"
#include "util.h"
#include "../audio/ma.h"
#include <json.hpp>
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <regex>

namespace rdj {

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
// Script de separación (se escribe junto al entorno). Usa solo la API pública
// de Demucs y soundfile para leer/escribir (FLAC de 16 bits: ~5 veces menos
// espacio que WAV en coma flotante).
const char* kScript = R"PY(# Ramiro DJ - separador de pistas (Demucs, MIT)
import os, sys
def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--descargar":
        from demucs.pretrained import get_model
        print("ETAPA descargando modelo " + sys.argv[2], flush=True)
        get_model(sys.argv[2])
        print("HECHO", flush=True)
        return
    inp, outdir, model_name = sys.argv[1], sys.argv[2], sys.argv[3]
    import numpy as np, soundfile as sf, torch
    from demucs.pretrained import get_model
    from demucs.apply import apply_model
    torch.set_num_threads(max(1, (os.cpu_count() or 2) - 1))
    print("ETAPA cargando modelo", flush=True)
    model = get_model(model_name)
    model.eval()
    print("ETAPA leyendo audio", flush=True)
    data, sr = sf.read(inp, dtype="float32", always_2d=True)
    if sr != model.samplerate:
        print("ERROR frecuencia %d distinta de %d" % (sr, model.samplerate), flush=True)
        sys.exit(2)
    wav = torch.from_numpy(np.ascontiguousarray(data.T))
    ref = wav.mean(0)
    mean, std = ref.mean(), ref.std() + 1e-8
    wav = (wav - mean) / std
    print("ETAPA separando", flush=True)
    with torch.no_grad():
        out = apply_model(model, wav[None], device="cpu", shifts=1, split=True, overlap=0.25, progress=True)[0]
    out = out * std + mean
    names = {"vocals": "voz", "drums": "bateria", "bass": "bajo", "other": "otros", "guitar": "guitarra", "piano": "piano"}
    print("ETAPA guardando", flush=True)
    for src, name in zip(out, model.sources):
        sf.write(os.path.join(outdir, names.get(name, name) + ".flac"), np.clip(src.numpy().T, -1, 1), sr, subtype="PCM_16")
    print("HECHO", flush=True)
main()
)PY";

std::wstring localBase() {
    // Datos grandes (entorno de Python, modelos, stems) en LOCALAPPDATA. Con
    // RDJ_DATA_DIR (pruebas) los stems van dentro de esa carpeta.
    wchar_t buf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    std::wstring b = (n > 0 && n < MAX_PATH) ? std::wstring(buf) : appDataDir();
    b += L"\\RamiroDJ";
    CreateDirectoryW(b.c_str(), nullptr);
    return b;
}

bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::string fnv(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", (unsigned long long)h);
    return b;
}
} // namespace

const char* stemLabel(const std::string& id) {
    if (id == "voz") return "Voz";
    if (id == "bateria") return "Batería";
    if (id == "bajo") return "Bajo";
    if (id == "guitarra") return "Guitarra";
    if (id == "piano") return "Piano";
    if (id == "otros") return "Otros";
    if (id == "instrumental") return "Instrumental";
    return "Pista";
}

StemSeparator::StemSeparator() {
    // Modelos de Demucs y de Hugging Face dentro de nuestra carpeta.
    SetEnvironmentVariableW(L"TORCH_HOME", (baseDir() + L"\\modelos").c_str());
    SetEnvironmentVariableW(L"HF_HOME", (baseDir() + L"\\modelos\\hf").c_str());
    SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
    SetEnvironmentVariableW(L"PYTHONUTF8", L"1");
    checkEngine();
    workerTh_ = std::thread(&StemSeparator::workerThread, this);
}

StemSeparator::~StemSeparator() {
    quit_ = true;
    cancelInstall_ = true;
    cancelJob_ = true;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (currentProcess_) TerminateProcess((HANDLE)currentProcess_, 1);
    }
    cv_.notify_all();
    if (installTh_.joinable()) installTh_.join();
    if (workerTh_.joinable()) workerTh_.join();
}

std::wstring StemSeparator::baseDir() const {
    const std::wstring b = localBase() + L"\\separador";
    CreateDirectoryW(b.c_str(), nullptr);
    return b;
}
std::wstring StemSeparator::pythonExe() const { return baseDir() + L"\\venv\\Scripts\\python.exe"; }

std::wstring StemSeparator::stemsRoot() const {
    wchar_t over[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"RDJ_DATA_DIR", over, MAX_PATH);
    std::wstring r = (n > 0 && n < MAX_PATH) ? std::wstring(over) + L"\\stems" : localBase() + L"\\stems";
    CreateDirectoryW(r.c_str(), nullptr);
    return r;
}

std::wstring StemSeparator::folderFor(const std::wstring& source) const {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    GetFileAttributesExW(source.c_str(), GetFileExInfoStandard, &a);
    std::wstring low = source;
    for (auto& c : low) c = (wchar_t)towlower(c);
    const std::string key = toUtf8(low) + "|" + std::to_string(a.nFileSizeLow) + "|" + std::to_string(a.ftLastWriteTime.dwLowDateTime);
    // Keep existing separations readable; new folders identify the song.
    const auto root = stemsRoot();
    const auto id = fromUtf8(fnv(key));
    const auto legacy = root + L"\\" + id;
    if (exists(legacy + L"\\info.json")) return legacy;
    const auto tags = readTags(source);
    std::wstring label = fromUtf8(tags.artist.empty() ? tags.title : tags.artist + " - " + tags.title);
    if (label.empty()) label = fs::path(source).stem().wstring();
    // The suffix distinguishes identical names from different files.
    return root + L"\\" + safeFileComponent(label) + L" [" + id + L"]";
}

void StemSeparator::checkEngine() {
    if (engine_ == Engine::Instalando) return;
    engine_ = (exists(pythonExe()) && exists(baseDir() + L"\\listo.txt")) ? Engine::Listo : Engine::NoInstalado;
}

std::string StemSeparator::engineMessage() {
    std::lock_guard<std::mutex> lk(m_);
    return engineMsg_;
}

std::vector<std::string> StemSeparator::installLog() {
    std::lock_guard<std::mutex> lk(m_);
    return std::vector<std::string>(log_.begin(), log_.end());
}

void StemSeparator::log(const std::string& line) {
    std::lock_guard<std::mutex> lk(m_);
    log_.push_back(line);
    while (log_.size() > 200) log_.pop_front();
}

void StemSeparator::writeScript() {
    std::ofstream f(fs::path(baseDir() + L"\\rdj_separar.py"), std::ios::binary | std::ios::trunc);
    f << kScript;
}

bool StemSeparator::runProcess(const std::wstring& cmdLine, const std::wstring& cwd, const std::function<void(const std::string&)>& onLine,
                               std::atomic<bool>* cancel) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = cmdLine;
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr,
                                   cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return false; }
    { std::lock_guard<std::mutex> lk(m_); currentProcess_ = pi.hProcess; }
    std::string acc;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(rd, buf, sizeof buf, &got, nullptr) && got > 0) {
        for (DWORD i = 0; i < got; ++i) {
            const char c = buf[i];
            if (c == '\n' || c == '\r') { if (!acc.empty()) onLine(acc); acc.clear(); }
            else acc.push_back(c);
        }
        if (cancel && cancel->load()) TerminateProcess(pi.hProcess, 1);
    }
    if (!acc.empty()) onLine(acc);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    { std::lock_guard<std::mutex> lk(m_); currentProcess_ = nullptr; }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    return code == 0 && !(cancel && cancel->load());
}

void StemSeparator::install() {
    if (engine_ == Engine::Instalando) return;
    if (installTh_.joinable()) installTh_.join();
    cancelInstall_ = false;
    engine_ = Engine::Instalando;
    installTh_ = std::thread(&StemSeparator::installThread, this);
}

void StemSeparator::cancelInstall() {
    cancelInstall_ = true;
    std::lock_guard<std::mutex> lk(m_);
    if (currentProcess_) TerminateProcess((HANDLE)currentProcess_, 1);
}

void StemSeparator::installThread() {
    auto fail = [&](const std::string& msg) {
        { std::lock_guard<std::mutex> lk(m_); engineMsg_ = msg; }
        log("ERROR: " + msg);
        engine_ = cancelInstall_ ? Engine::NoInstalado : Engine::Error;
    };
    auto step = [&](const std::string& msg) { { std::lock_guard<std::mutex> lk(m_); engineMsg_ = msg; } log("== " + msg); };
    auto out = [&](const std::string& l) { log(l); };
    const std::wstring base = baseDir();
    // 1) Python del sistema (3.10 o superior, requisito de PyTorch).
    step("Buscando Python 3.10 o superior…");
    std::wstring sysPy;
    for (const wchar_t* cand : {L"py -3.12", L"py -3.11", L"py -3.10", L"py -3", L"python"}) {
        std::string ver;
        if (runProcess(std::wstring(cand) + L" -c \"import sys;print('%d.%d'%sys.version_info[:2])\"", L"", [&](const std::string& l) { ver = l; }, &cancelInstall_)) {
            int ma = 0, mi = 0;
            if (std::sscanf(ver.c_str(), "%d.%d", &ma, &mi) == 2 && ma == 3 && mi >= 10 && mi <= 13) { sysPy = cand; log("Python " + ver + " (" + toUtf8(cand) + ")"); break; }
        }
    }
    if (sysPy.empty()) return fail("No se encontró Python 3.10-3.13. Instálalo desde python.org (gratuito) y vuelve a pulsar Instalar.");
    // 2) Entorno virtual propio (no toca el Python del sistema).
    if (!exists(pythonExe())) {
        step("Creando el entorno de Python…");
        if (!runProcess(sysPy + L" -m venv \"" + base + L"\\venv\"", base, out, &cancelInstall_)) return fail("No se pudo crear el entorno de Python.");
    }
    const std::wstring py = L"\"" + pythonExe() + L"\"";
    // 3) PyTorch para CPU desde el índice oficial de pytorch.org (~200 MB).
    step("Descargando PyTorch (CPU) desde pytorch.org…");
    if (!runProcess(py + L" -m pip install --disable-pip-version-check torch --index-url https://download.pytorch.org/whl/cpu", base, out, &cancelInstall_))
        return fail("Falló la instalación de PyTorch (¿sin conexión?).");
    // 4) Demucs y soundfile desde PyPI.
    step("Instalando Demucs y soundfile desde PyPI…");
    if (!runProcess(py + L" -m pip install --disable-pip-version-check demucs soundfile", base, out, &cancelInstall_))
        return fail("Falló la instalación de Demucs.");
    // 5) Modelos oficiales (se descargan una vez).
    writeScript();
    for (const wchar_t* m : {L"htdemucs_6s", L"htdemucs"}) {
        step("Descargando el modelo " + toUtf8(m) + "…");
        if (!runProcess(py + L" \"" + base + L"\\rdj_separar.py\" --descargar " + m, base, out, &cancelInstall_))
            return fail("No se pudo descargar el modelo " + toUtf8(m) + ".");
    }
    std::ofstream(fs::path(base + L"\\listo.txt")) << "ok";
    step("Motor de separación listo.");
    engine_ = Engine::Listo;
}

void StemSeparator::enqueue(const std::wstring& path, const std::string& title, const std::string& model) {
    auto j = std::make_shared<StemJob>();
    j->source = path; j->title = title; j->model = model;
    j->state = "En cola";
    std::lock_guard<std::mutex> lk(m_);
    jobs_.push_back(j);
    cv_.notify_all();
}

std::vector<StemJobInfo> StemSeparator::jobs() {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<StemJobInfo> v;
    for (auto& j : jobs_) v.push_back({j->source, j->title, j->model, j->state, j->progress.load(), j->done, j->failed});
    return v;
}

void StemSeparator::cancelCurrent() {
    cancelJob_ = true;
    std::lock_guard<std::mutex> lk(m_);
    if (currentProcess_) TerminateProcess((HANDLE)currentProcess_, 1);
}

void StemSeparator::clearFinished() {
    std::lock_guard<std::mutex> lk(m_);
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [](auto& j) { return j->done || j->failed; }), jobs_.end());
}

void StemSeparator::workerThread() {
    for (;;) {
        std::shared_ptr<StemJob> job;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait_for(lk, std::chrono::milliseconds(500), [&] { return quit_.load(); });
            if (quit_) return;
            if (engine_ != Engine::Listo) continue;
            for (auto& j : jobs_) if (!j->done && !j->failed) { job = j; break; }
            if (!job) continue;
            job->state = "Preparando…";
        }
        cancelJob_ = false;
        auto setState = [&](const std::string& s) { std::lock_guard<std::mutex> lk(m_); job->state = s; };
        auto failJob = [&](const std::string& s) { std::lock_guard<std::mutex> lk(m_); job->state = s; job->failed = true; };
        const std::wstring folder = folderFor(job->source);
        CreateDirectoryW(folder.c_str(), nullptr);
        // 1) Decodificar a 44,1 kHz estéreo (lo que espera el modelo) con nuestro decodificador.
        const std::wstring tmp = folder + L"\\entrada.wav";
        {
            ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 2, 44100);
            ma_decoder dec;
            if (ma_decoder_init_file_w(job->source.c_str(), &dc, &dec) != MA_SUCCESS) { failJob("No se puede leer el archivo (formato no compatible)."); continue; }
            ma_encoder enc;
            ma_encoder_config ec = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2, 44100);
            if (ma_encoder_init_file_w(tmp.c_str(), &ec, &enc) != MA_SUCCESS) { ma_decoder_uninit(&dec); failJob("No se puede escribir en la carpeta de stems."); continue; }
            std::vector<float> b(8192 * 2);
            ma_uint64 got = 0;
            do {
                ma_decoder_read_pcm_frames(&dec, b.data(), 8192, &got);
                if (got) ma_encoder_write_pcm_frames(&enc, b.data(), got, nullptr);
            } while (got == 8192 && !cancelJob_);
            ma_encoder_uninit(&enc);
            ma_decoder_uninit(&dec);
        }
        if (cancelJob_) { DeleteFileW(tmp.c_str()); failJob("Cancelado."); continue; }
        // 2) Demucs en un proceso aparte (prioridad baja).
        writeScript();
        setState("Cargando el modelo…");
        static const std::regex pct(R"((\d+)%\|)");
        std::string lastError;
        const std::wstring cmd = L"\"" + pythonExe() + L"\" \"" + baseDir() + L"\\rdj_separar.py\" \"" + tmp + L"\" \"" + folder + L"\" " + fromUtf8(job->model);
        const bool ok = runProcess(cmd, baseDir(), [&](const std::string& l) {
            std::smatch m;
            if (std::regex_search(l, m, pct)) { job->progress = std::stoi(m[1].str()) / 100.f; setState("Separando… " + m[1].str() + " %"); }
            else if (l.rfind("ETAPA ", 0) == 0) {
                const std::string e = l.substr(6);
                setState(e == "separando" ? "Separando…" : e == "guardando" ? "Guardando las pistas…" : e == "cargando modelo" ? "Cargando el modelo…" : e);
            } else if (l.rfind("ERROR", 0) == 0 || l.find("Error") != std::string::npos) lastError = l;
        }, &cancelJob_);
        DeleteFileW(tmp.c_str());
        if (!ok) {
            failJob(cancelJob_ ? "Cancelado." : ("Falló la separación. " + lastError).substr(0, 200));
            continue;
        }
        // 3) Ficha de la canción.
        json info;
        info["source"] = toUtf8(job->source); info["title"] = job->title; info["model"] = job->model;
        info["created"] = (int64_t)std::time(nullptr);
        std::ofstream(fs::path(folder + L"\\info.json"), std::ios::binary | std::ios::trunc) << info.dump(1);
        {
            std::lock_guard<std::mutex> lk(m_);
            job->progress = 1.f; job->done = true; job->state = "Terminado";
        }
        songsVersion_++;
    }
}

std::vector<StemSong> StemSeparator::songs() {
    std::vector<StemSong> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(fs::path(stemsRoot()), ec)) {
        if (!e.is_directory()) continue;
        const fs::path info = e.path() / "info.json";
        if (!fs::exists(info)) continue;
        try {
            std::ifstream f(info, std::ios::binary);
            json j; f >> j;
            StemSong s;
            s.folder = e.path().wstring();
            s.source = fromUtf8(j.value("source", ""));
            s.title = j.value("title", "");
            s.model = j.value("model", "");
            s.created = j.value("created", (int64_t)0);
            for (const char* id : {"voz", "bateria", "bajo", "guitarra", "piano", "otros"})
                if (fs::exists(e.path() / (std::string(id) + ".flac"))) s.stems.push_back(id);
            if (!s.stems.empty()) out.push_back(std::move(s));
        } catch (...) {
        }
    }
    std::sort(out.begin(), out.end(), [](const StemSong& a, const StemSong& b) { return a.created > b.created; });
    return out;
}

void StemSeparator::removeSong(const std::wstring& folder) {
    std::error_code ec;
    if (folder.rfind(stemsRoot(), 0) != 0) return;  // solo dentro de nuestra carpeta
    fs::remove_all(fs::path(folder), ec);
    songsVersion_++;
}

} // namespace rdj
