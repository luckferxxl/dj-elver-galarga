// Utilidades comunes de la aplicación (cadenas, rutas, búsqueda).
#pragma once
#include <string>
#include <vector>

namespace rdj {

std::string toUtf8(const std::wstring& w);
std::wstring fromUtf8(const std::string& s);

// %APPDATA%\RamiroDJ (se crea si no existe).
std::wstring appDataDir();
// Carpeta "Música\Ramiro DJ - Grabaciones" del usuario (se crea si no existe).
std::wstring recordingsDir();

// Minúsculas y sin tildes, para comparar en búsquedas ("Canción" ~ "cancion").
std::string foldForSearch(const std::string& utf8);
// Divide una búsqueda en palabras ya normalizadas.
std::vector<std::string> searchTerms(const std::string& query);

std::wstring fileNameOf(const std::wstring& path);
std::wstring extensionLower(const std::wstring& path);
bool isSupportedAudioExt(const std::wstring& extLower);

std::string formatTime(double sec, bool tenths = true);

} // namespace rdj
