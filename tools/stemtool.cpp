// Prueba sin interfaz del separador: rdj_stemtool [--instalar] [archivo [modelo]]
// Instala el motor si hace falta y separa el archivo, mostrando el progreso.
#include "../src/core/stems.h"
#include "../src/core/util.h"
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace rdj;

int wmain(int argc, wchar_t** argv) {
    StemSeparator s;
    s.checkEngine();
    const bool forceInstall = argc > 1 && std::wstring(argv[1]) == L"--instalar";
    const int fileArg = forceInstall ? 2 : 1;
    size_t shown = 0;
    if (s.engine() != StemSeparator::Engine::Listo || forceInstall) {
        s.install();
        while (s.engine() == StemSeparator::Engine::Instalando || s.engine() == StemSeparator::Engine::Desconocido) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            auto log = s.installLog();
            for (size_t i = shown < log.size() ? shown : log.size(); i < log.size(); ++i) std::printf("[instalar] %s\n", log[i].c_str());
            shown = log.size();
            std::fflush(stdout);
        }
        std::printf("Motor: %s\n", s.engineMessage().c_str());
        if (s.engine() != StemSeparator::Engine::Listo) return 1;
    }
    if (argc <= fileArg) return 0;
    const std::string model = argc > fileArg + 1 ? toUtf8(argv[fileArg + 1]) : "htdemucs_6s";
    s.enqueue(argv[fileArg], "prueba", model);
    std::string last;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        auto j = s.jobs();
        if (j.empty()) continue;
        char line[256];
        std::snprintf(line, sizeof line, "%3.0f %%  %s", j[0].progress * 100, j[0].state.c_str());
        if (last != line) { std::printf("%s\n", line); std::fflush(stdout); last = line; }
        if (j[0].done || j[0].failed) {
            for (auto& song : s.songs()) {
                std::printf("Carpeta: %s\n", toUtf8(song.folder).c_str());
                for (auto& st : song.stems) std::printf("  %s\n", st.c_str());
            }
            return j[0].failed ? 2 : 0;
        }
    }
}
