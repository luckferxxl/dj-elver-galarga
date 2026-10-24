// Punto de entrada: ventana Win32, Direct3D 11 e ImGui.
// Opciones: --separador, --biblioteca, --theme N, --null-audio, --autotest A B registro, --screenshot archivo.png,
//           --pos segA segB, --play, --sync, --no-waves, --collapse (capturas de las ondas y del SYNC),
//           --load A B, --browse carpeta, --add-folder carpeta, --sidelist archivo.m3u8 (capturas de pantalla),
//           --fx-demo fxA fxB color ruletaA (0.11: pestaña FX de los platos y ruleta de color), --fx-demo-tab N
#include "app.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "covers.h"
#include "widgets.h"
#include "../core/util.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <wincodec.h>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
UINT g_resizeW = 0, g_resizeH = 0;
float g_dpiScale = 1.f;      // escala de Windows del monitor donde está la ventana
bool g_occluded = false;
rdj::App* g_app = nullptr;
HWND g_hwnd = nullptr;

// Registro de fallos: si el programa se cae, deja "fallo.txt" en la carpeta de datos con la
// dirección y la pila (módulo+desplazamiento) en vez de quedarse colgado en el depurador.
wchar_t g_crashPath[MAX_PATH] = {};
void writeCrashLog(DWORD code, const EXCEPTION_RECORD* rec, CONTEXT ctx) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) return;
    HANDLE f = CreateFileW(g_crashPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    char line[512];
    DWORD wr = 0;
    auto put = [&](int n) { if (n > 0) WriteFile(f, line, (DWORD)std::min(n, (int)sizeof(line) - 1), &wr, nullptr); };
    auto where = [&](DWORD64 addr) {
        HMODULE mod = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &mod) && mod) {
            char full[MAX_PATH];
            if (GetModuleFileNameA(mod, full, MAX_PATH)) { const char* b = strrchr(full, '\\'); lstrcpynA(name, b ? b + 1 : full, MAX_PATH); }
        }
        put(snprintf(line, sizeof(line), "  %s+0x%llx\r\n", name, (unsigned long long)(addr - (DWORD64)mod)));
    };
    SYSTEMTIME st;
    GetLocalTime(&st);
    put(snprintf(line, sizeof(line), "DJ Elver Galarga 0.11.3 · fallo %04d-%02d-%02d %02d:%02d:%02d · código 0x%08lx · hilo %lu\r\n",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, code, GetCurrentThreadId()));
    if (rec && rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && rec->NumberParameters >= 2)
        put(snprintf(line, sizeof(line), "  %s en 0x%llx\r\n", rec->ExceptionInformation[0] ? "escritura" : "lectura",
                     (unsigned long long)rec->ExceptionInformation[1]));
#if defined(_M_X64) || defined(__x86_64__)
    for (int i = 0; i < 40 && ctx.Rip; ++i) {
        where(ctx.Rip);
        DWORD64 base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
        if (!fn) { ctx.Rip = *(DWORD64*)ctx.Rsp; ctx.Rsp += 8; continue; }
        void* handlerData = nullptr;
        DWORD64 frame = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, fn, &ctx, &handlerData, &frame, nullptr);
    }
#endif
    put(snprintf(line, sizeof(line), "\r\n"));
    CloseHandle(f);
}
LONG WINAPI crashFilter(EXCEPTION_POINTERS* ep) {
    writeCrashLog(ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord, *ep->ContextRecord);
    return EXCEPTION_EXECUTE_HANDLER;
}
#ifdef RDJ_IMASSERT_DIAG
// Compilación de comprobación (docs/COMPILAR.md): los IM_ASSERT de Dear ImGui quedan en fallo.txt
// con la pila, en vez de desaparecer en la versión optimizada.
extern "C" void rdj_imassert(const char* expr, const char* file, int line) {
    char b[600];
    snprintf(b, sizeof b, "IM_ASSERT: %s (%s:%d)\r\n", expr, file, line);
    HANDLE f = CreateFileW(g_crashPath, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) { DWORD wr; WriteFile(f, b, (DWORD)strlen(b), &wr, nullptr); CloseHandle(f); }
    abort();
}
#endif
// abort() (comprobaciones internas): también queda registrado.
void abortHandler(int) {
    CONTEXT ctx{};
    RtlCaptureContext(&ctx);
    writeCrashLog(0x40000015, nullptr, ctx);
    _exit(3);
}

void createRT() {
    ID3D11Texture2D* bb = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&bb));
    if (bb) { g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv); bb->Release(); }
}
void cleanupRT() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

bool createDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx);
    if (hr == DXGI_ERROR_UNSUPPORTED || FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx);
    if (FAILED(hr)) return false;
    createRT();
    return true;
}

// Texturas para las carátulas (ImTextureID = ID3D11ShaderResourceView*).
ImTextureID createTexture(const uint8_t* rgba, int w, int h) {
    if (!g_dev) return 0;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{rgba, (UINT)w * 4, 0};
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&td, &sd, &tex))) return 0;
    ID3D11ShaderResourceView* srv = nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
    vd.Format = td.Format;
    vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    vd.Texture2D.MipLevels = 1;
    g_dev->CreateShaderResourceView(tex, &vd, &srv);
    tex->Release();  // la vista conserva la textura
    return (ImTextureID)(intptr_t)srv;
}
void destroyTexture(ImTextureID id) {
    if (auto* srv = (ID3D11ShaderResourceView*)(intptr_t)id) srv->Release();
}

void cleanupDevice() {
    cleanupRT();
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx) { g_ctx->Release(); g_ctx = nullptr; }
    if (g_dev) { g_dev->Release(); g_dev = nullptr; }
}

// Guarda el búfer trasero como PNG (verificación visual automatizada).
bool saveScreenshot(const std::wstring& path) {
    ID3D11Texture2D* bb = nullptr;
    if (FAILED(g_swap->GetBuffer(0, IID_PPV_ARGS(&bb)))) return false;
    D3D11_TEXTURE2D_DESC td;
    bb->GetDesc(&td);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
    ID3D11Texture2D* st = nullptr;
    bool ok = false;
    if (SUCCEEDED(g_dev->CreateTexture2D(&td, nullptr, &st))) {
        g_ctx->CopyResource(st, bb);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
            IWICImagingFactory* f = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) {
                IWICStream* s = nullptr; IWICBitmapEncoder* e = nullptr; IWICBitmapFrameEncode* fr = nullptr;
                f->CreateStream(&s);
                if (s && SUCCEEDED(s->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) && SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &e))) {
                    e->Initialize(s, WICBitmapEncoderNoCache);
                    e->CreateNewFrame(&fr, nullptr);
                    fr->Initialize(nullptr);
                    fr->SetSize(td.Width, td.Height);
                    // El búfer es RGBA; WIC/PNG espera BGRA: intercambiar R y B.
                    std::vector<BYTE> px(m.RowPitch * td.Height);
                    memcpy(px.data(), m.pData, px.size());
                    for (UINT yy = 0; yy < td.Height; ++yy)
                        for (UINT xx = 0; xx < td.Width; ++xx) { BYTE* q = &px[yy * m.RowPitch + xx * 4]; std::swap(q[0], q[2]); q[3] = 255; }
                    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
                    fr->SetPixelFormat(&fmt);
                    fr->WritePixels(td.Height, m.RowPitch, (UINT)px.size(), px.data());
                    fr->Commit();
                    e->Commit();
                    ok = true;
                }
                if (fr) fr->Release();
                if (e) e->Release();
                if (s) s->Release();
                f->Release();
            }
            g_ctx->Unmap(st, 0);
        }
        st->Release();
    }
    bb->Release();
    return ok;
}

LRESULT WINAPI wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_resizeW = LOWORD(lParam); g_resizeH = HIWORD(lParam);
        if (g_app) g_app->settings().maximized = wParam == SIZE_MAXIMIZED;
        return 0;
    case WM_DPICHANGED: {
        // Ventana llevada a un monitor con otra escala: nuevo tamaño sugerido por Windows y estilo a la nueva escala.
        g_dpiScale = HIWORD(wParam) / 96.f;
        const RECT* r = (const RECT*)lParam;
        SetWindowPos(hWnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)lParam;
        mm->ptMinTrackSize.x = 1024; mm->ptMinTrackSize.y = 640;
        return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wParam;
        POINT pt{};
        DragQueryPoint(drop, &pt);
        const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> files;
        for (UINT i = 0; i < n; ++i) {
            const UINT len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring s(len, L'\0');
            DragQueryFileW(drop, i, s.data(), len + 1);
            files.push_back(s);
        }
        DragFinish(drop);
        if (g_app) g_app->onDropFiles(files, ImVec2((float)pt.x, (float)pt.y));
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_CLOSE:
        if (g_app && !g_app->confirmClose()) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

void setupStyle(float scale, int theme) {
    using namespace rdj::ui;
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 10.f; st.ChildRounding = 8.f; st.FrameRounding = 6.f; st.PopupRounding = 8.f;
    st.GrabRounding = 4.f; st.TabRounding = 6.f; st.ScrollbarRounding = 6.f; st.ScrollbarSize = 10.f;
    st.WindowPadding = ImVec2(10, 10); st.FramePadding = ImVec2(8, 5); st.ItemSpacing = ImVec2(6, 5);
    st.CellPadding = ImVec2(6, 3); st.PopupBorderSize = 1.f; st.WindowTitleAlign = ImVec2(0.02f, 0.5f);
    st.WindowBorderSize = 1.f; st.ChildBorderSize = 1.f; st.FrameBorderSize = 0.f; st.TabBarBorderSize = 0.f;
    st.ScaleAllSizes(scale);
    st.FontScaleDpi = scale;
    applyTheme(theme);
}

void loadFonts(rdj::App& app) {
    ImGuiIO& io = ImGui::GetIO();
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    const std::string fonts = rdj::toUtf8(std::wstring(win) + L"\\Fonts\\");
    auto exists = [](const std::string& p) { return GetFileAttributesW(rdj::fromUtf8(p).c_str()) != INVALID_FILE_ATTRIBUTES; };
    // Fuentes de la marca incrustadas como recurso (OFL): Hanken Grotesk (texto) y Unbounded (rótulos).
    auto fromResource = [&](const wchar_t* name, float size, const ImFontConfig* base) -> ImFont* {
        HRSRC r = FindResourceW(nullptr, name, MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
        if (!r) return nullptr;
        HGLOBAL g = LoadResource(nullptr, r);
        void* data = g ? LockResource(g) : nullptr;
        if (!data) return nullptr;
        ImFontConfig cfg = base ? *base : ImFontConfig();
        cfg.FontDataOwnedByAtlas = false;  // la memoria del recurso pertenece al módulo
        return io.Fonts->AddFontFromMemoryTTF(data, (int)SizeofResource(nullptr, r), size, &cfg);
    };
    ImFontConfig merge;
    merge.MergeMode = true;
    auto addSymbols = [&] {
        // Símbolos (▶ ❚❚ ◀ ⏏ ☰ …) y acentos que falten: fuentes del sistema.
        if (exists(fonts + "seguisym.ttf")) io.Fonts->AddFontFromFileTTF((fonts + "seguisym.ttf").c_str(), 16.f, &merge);
        if (exists(fonts + "segoeui.ttf")) io.Fonts->AddFontFromFileTTF((fonts + "segoeui.ttf").c_str(), 16.f, &merge);
    };
    ImFont* main = fromResource(L"FONT_TEXT", 16.f, nullptr);
    if (!main && exists(fonts + "segoeui.ttf")) main = io.Fonts->AddFontFromFileTTF((fonts + "segoeui.ttf").c_str(), 16.f);
    if (!main) main = io.Fonts->AddFontDefault();
    addSymbols();
    // Rótulos, BPM y tiempos: Barlow Semi Condensed (OFL), seminegrita y negrita.
    app.fontHead = fromResource(L"FONT_HEAD", 16.f, nullptr);
    if (app.fontHead) addSymbols();
    else app.fontHead = main;
    app.fontBold = fromResource(L"FONT_BOLD", 16.f, nullptr);
    if (app.fontBold) addSymbols();
    else app.fontBold = app.fontHead;
    app.fontBig = app.fontHead;
    app.fontMono = exists(fonts + "consola.ttf") ? io.Fonts->AddFontFromFileTTF((fonts + "consola.ttf").c_str(), 16.f) : main;
}
} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    lstrcpynW(g_crashPath, (rdj::appDataDir() + L"\\fallo.txt").c_str(), MAX_PATH);
    SetUnhandledExceptionFilter(crashFilter);
    signal(SIGABRT, abortHandler);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool nullAudio = false, openMidi = false, sepMode = false, libMode = false;
    int themeArg = -1;
    std::wstring mandosTest;
    std::wstring atA, atB, atLog, shot, loadA, loadB, dumpFile, browse, addFolder, sideM3u, selectPath;
    double posA = -1, posB = -1;
    bool playBoth = false, syncB = false, noWaves = false, collapse = false;
    std::wstring rackPreset;
    int rackL = 0, rackR = 1, rackLoopBars = 0;
    float rackMacro = 0.f;
    bool rackDemo = false, rackXy = false, rackInstr = false;
    int fxDemoA = -1, fxDemoB = 0, fxDemoColor = 0;
    float fxDemoKnob = 0.f;
    int fxDemoTab = 5;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--null-audio") nullAudio = true;
        else if (a == L"--autotest" && i + 3 < argc) { atA = argv[i + 1]; atB = argv[i + 2]; atLog = argv[i + 3]; i += 3; }
        else if (a == L"--screenshot" && i + 1 < argc) shot = argv[++i];
        else if (a == L"--load" && i + 2 < argc) { loadA = argv[i + 1]; loadB = argv[i + 2]; i += 2; }
        else if (a == L"--midi") openMidi = true;
        else if (a == L"--separador") sepMode = true;
        else if (a == L"--biblioteca") libMode = true;
        else if (a == L"--browse" && i + 1 < argc) browse = argv[++i];
        else if (a == L"--add-folder" && i + 1 < argc) addFolder = argv[++i];
        else if (a == L"--sidelist" && i + 1 < argc) sideM3u = argv[++i];
        else if (a == L"--select" && i + 1 < argc) selectPath = argv[++i];
        else if (a == L"--theme" && i + 1 < argc) themeArg = _wtoi(argv[++i]);
        else if (a == L"--dump-state" && i + 1 < argc) dumpFile = argv[++i];
        else if (a == L"--pos" && i + 2 < argc) { posA = _wtof(argv[i + 1]); posB = _wtof(argv[i + 2]); i += 2; }
        else if (a == L"--play") playBoth = true;
        else if (a == L"--sync") syncB = true;
        else if (a == L"--no-waves") noWaves = true;
        else if (a == L"--collapse") collapse = true;
        else if (a == L"--mandos-prueba" && i + 1 < argc) mandosTest = argv[++i];
        else if (a == L"--rack" && i + 4 < argc) { rackPreset = argv[i + 1]; rackL = _wtoi(argv[i + 2]); rackR = _wtoi(argv[i + 3]); rackMacro = (float)_wtof(argv[i + 4]); i += 4; rackDemo = true; }
        else if (a == L"--rack-loop" && i + 1 < argc) rackLoopBars = _wtoi(argv[++i]);
        else if (a == L"--rack-xy") rackXy = true;
        else if (a == L"--rack-instr") rackInstr = true;
        else if (a == L"--fx-demo" && i + 4 < argc) { fxDemoA = _wtoi(argv[i + 1]); fxDemoB = _wtoi(argv[i + 2]); fxDemoColor = _wtoi(argv[i + 3]); fxDemoKnob = (float)_wtof(argv[i + 4]); i += 4; }
        else if (a == L"--fx-demo-tab" && i + 1 < argc) { fxDemoTab = _wtoi(argv[++i]); }
    }

    ImGui_ImplWin32_EnableDpiAwareness();
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    g_dpiScale = scale;

    rdj::App app;
    app.settings().load();
    if (themeArg >= 0) app.settings().theme = themeArg;
    WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, wndProc, 0, 0, hInst, LoadIconW(hInst, L"IDI_APP"), LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"RamiroDJ", LoadIconW(hInst, L"IDI_APP")};
    RegisterClassExW(&wc);
    const int w = (int)(app.settings().winW * scale), h = (int)(app.settings().winH * scale);
    g_hwnd = CreateWindowW(wc.lpszClassName, L"DJ Elver Galarga", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr, hInst, nullptr);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    if (!createDevice(g_hwnd)) {
        MessageBoxW(nullptr, L"No se pudo iniciar Direct3D 11.", L"DJ Elver Galarga", MB_ICONERROR);
        return 1;
    }
    ShowWindow(g_hwnd, app.settings().maximized && atA.empty() ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);
    DragAcceptFiles(g_hwnd, TRUE);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Escala final = la de Windows × el tamaño de la interfaz elegido en Ramiro DJ.
    float appliedScale = scale * app.settings().uiScale / 100.f;
    setupStyle(appliedScale, app.settings().theme);
    loadFonts(app);
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    rdj::ui::setTextureApi({createTexture, destroyTexture});
    // Columnas de las tablas (anchos, orden, visibles) guardadas en ajustes.json.
    if (!app.settings().imguiIni.empty()) ImGui::LoadIniSettingsFromMemory(app.settings().imguiIni.c_str(), app.settings().imguiIni.size());

    g_app = &app;
    app.init(g_hwnd, nullAudio);
    if (!atA.empty()) app.startAutotest(atA, atB, atLog);
    const int mandosResult = mandosTest.empty() ? -1 : app.mandosPrueba(mandosTest);
    if (!loadA.empty()) app.demoLoad(loadA, loadB);
    if (noWaves) app.settings().showWaves = false;
    if (collapse) app.settings().decksCollapsed = true;
    if (openMidi) app.openMidiDialog();
    if (sepMode) app.setMode(1);
    if (libMode) app.setMode(2);
    if (!addFolder.empty()) app.testAddFolder(addFolder);
    if (!sideM3u.empty()) app.testSidelist(sideM3u);
    if (!browse.empty()) app.testBrowse(browse);
    if (!selectPath.empty()) app.testSelect(selectPath);

    int frameNo = 0;
    int loadedAt = -1;
    // Capturas: RDJ_SHOT_SECONDS retrasa la captura (p. ej. para que termine el análisis).
    double shotDelay = 0;
    {
        wchar_t v[32];
        if (GetEnvironmentVariableW(L"RDJ_SHOT_SECONDS", v, 32) > 0) shotDelay = _wtof(v);
    }
    const ULONGLONG startTick = GetTickCount64();
    auto elapsed = [&] { return (GetTickCount64() - startTick) / 1000.0; };
    bool done = mandosResult >= 0;   // --mandos-prueba: sin bucle
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (g_occluded && g_swap->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) { Sleep(10); app.idle(); continue; }
        g_occluded = false;
        if (g_resizeW && g_resizeH) {
            cleanupRT();
            g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            createRT();
        }
        {
            // Tamaño de la interfaz cambiado en Preferencias o ventana en otro monitor.
            const float want = g_dpiScale * app.settings().uiScale / 100.f;
            if (std::fabs(want - appliedScale) > 1e-3f) { appliedScale = want; setupStyle(appliedScale, rdj::ui::currentThemeIndex()); }
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        rdj::ui::covers().tick();
        ImGui::NewFrame();
        app.frame();
        ImGui::Render();
        const float clear[4] = {13 / 255.f, 15 / 255.f, 19 / 255.f, 1.f};
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        ++frameNo;
        if (!dumpFile.empty()) {
            static int idleFrames = 0;
            idleFrames = app.loading() ? 0 : idleFrames + 1;
            if ((frameNo > 120 && idleFrames > 30) || frameNo > 60 * 30) { app.dumpState(dumpFile); done = true; }
        }
        if (!loadA.empty()) {
            // Con --load, la captura se hace cuando las dos pistas están cargadas.
            if (loadedAt < 0 && app.decksLoaded()) {
                loadedAt = frameNo;
                app.demoStart(posA, posB, playBoth, syncB);
                if (rackDemo) app.rackDemo(rdj::toUtf8(rackPreset), rackL, rackR, rackMacro, rackLoopBars, rackXy);
                if (rackInstr) app.instrDemo();
                if (fxDemoA >= 0) app.fxDemo(fxDemoA, fxDemoB, fxDemoColor, fxDemoKnob, fxDemoTab);
            }
            if (!shot.empty() && loadedAt >= 0 && frameNo >= loadedAt + 30 && elapsed() >= shotDelay) { saveScreenshot(shot); done = true; }
            if (frameNo > 60 * 60) done = true;
        } else if (!shot.empty() && atA.empty() && frameNo >= 120 && elapsed() >= shotDelay) { saveScreenshot(shot); done = true; }
        const HRESULT hr = g_swap->Present(1, 0);
        g_occluded = hr == DXGI_STATUS_OCCLUDED;
        if (app.wantsClose()) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
        if (!atA.empty() && app.autotestDone()) {
            if (!shot.empty()) saveScreenshot(shot);
            done = true;
        }
    }
    // Tamaño "normal" de la ventana (también si se cerró maximizada o minimizada).
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (GetWindowPlacement(g_hwnd, &wp)) {
        const int w = (int)((wp.rcNormalPosition.right - wp.rcNormalPosition.left) / g_dpiScale);
        const int h = (int)((wp.rcNormalPosition.bottom - wp.rcNormalPosition.top) / g_dpiScale);
        if (w >= 1024 && h >= 640) { app.settings().winW = w; app.settings().winH = h; }
        app.settings().maximized = wp.showCmd == SW_SHOWMAXIMIZED || (wp.flags & WPF_RESTORETOMAXIMIZED);
    }
    {
        size_t n = 0;
        const char* ini = ImGui::SaveIniSettingsToMemory(&n);
        app.settings().imguiIni.assign(ini ? ini : "", n);
    }
    app.shutdown();
    rdj::ui::covers().clear();
    const int result = mandosResult >= 0 ? mandosResult : atA.empty() ? 0 : app.autotestResult();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanupDevice();
    DestroyWindow(g_hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    CoUninitialize();
    return result;
}
