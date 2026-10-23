// Diálogos nativos de Windows y utilidades del sistema.
#pragma once
#include <string>
#include <vector>

namespace rdj::win {

std::vector<std::wstring> pickAudioFiles(void* hwnd, bool multi);
std::wstring pickFolder(void* hwnd);
std::wstring pickImage(void* hwnd);
std::wstring pickPlaylistToOpen(void* hwnd);
std::wstring pickFileToOpen(void* hwnd, const wchar_t* filterName, const wchar_t* filterSpec);
std::wstring pickSaveFile(void* hwnd, const std::wstring& defaultName, const wchar_t* filterName, const wchar_t* filterSpec, const wchar_t* defExt);
void revealInExplorer(const std::wstring& path);
void openFolder(const std::wstring& path);
bool confirm(void* hwnd, const std::wstring& text, const std::wstring& title);

// Sistema de archivos para el navegador.
struct DriveInfo {
    std::wstring root;       // "C:\"
    std::string label;       // "Disco local (C:)"
    int type = 0;            // 0 fijo, 1 extraíble, 2 red, 3 CD/DVD
};
std::vector<DriveInfo> drives();
// Carpetas del usuario: 0 Escritorio, 1 Música, 2 Descargas, 3 Documentos, 4 Vídeos. Vacía si no existe.
std::wstring userFolder(int which);
// Subcarpetas visibles (sin ocultas ni del sistema), ordenadas sin distinguir mayúsculas.
std::vector<std::wstring> subfolders(const std::wstring& dir);
bool folderExists(const std::wstring& dir);

} // namespace rdj::win
