// Aplicación Ramiro DJ: estado de la interfaz y coordinación entre motor,
// biblioteca, cargador, grabador, dispositivos y MIDI.
#pragma once
#include "../audio/audio_io.h"
#include "../audio/engine.h"
#include "../audio/loader.h"
#include "../audio/recorder.h"
#include "../core/library.h"
#include "../core/midi.h"
#include "../core/settings.h"
#include "../core/stems.h"
#include "../core/vdjimport.h"
#include <array>
#include <unordered_map>
#include "imgui.h"
#include "../audio/analysis.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <string>
#include <vector>

namespace rdj {

// Empuje de tempo con el jog: cuánto acelera una vuelta por segundo y el máximo (como en los platos de DJ,
// un toque suave da un 1–3 % y un giro rápido no pasa del 12 %).
constexpr float kJogBendPerRevSec = 0.08f;
constexpr float kJogBendMax = 0.12f;

struct DeckUi {
    TrackPtr track;                                   // pista confirmada por el motor
    std::vector<std::pair<uint32_t, TrackPtr>> pending;  // enviadas, a la espera de confirmación
    uint32_t gen = 0;
    std::wstring path;
    std::string title, artist;
    TrackMeta meta;          // cuadrícula y cues (se guardan en la biblioteca)
    bool sync = false;
    float pitch = 0.f;       // fracción
    std::string error;
    double errorTime = 0;
    std::vector<double> taps;
    bool scratching = false;
    double scratchLastTime = 0;
    float meterHold[2] = {0, 0};
    double holdTime[2] = {0, 0};
    float meterL = 0, meterR = 0;
    int fxBeatsIdx = 3;
    bool cueHeldByMouse = false;
    float jogBend = 0.f, buttonBend = 0.f;   // empuje de tempo (jog / botones)
    float rangeBeforeAuto = 0.f;             // escala del pitch antes de que el SYNC la ampliara (0 = no ampliada)
    double fxPressAt = -1;                   // ACTIVAR del efecto: cuándo se pulsó (para el modo mantener)
    bool fxPressTurnedOn = false;
    bool platterNudge = false;               // el ratón empuja el aro del plato en pantalla
    float nudgeAcc = 0.f;                    // empuje del jog recibido en este fotograma (en «empuje × segundos»)
    bool jogTouched = false;                 // plato del jog tocado (MIDI)
    int scratchTicks = 0;                    // pasos del plato acumulados en este fotograma
    float platterAngle = 0.f;                // último ángulo del ratón sobre el plato
    double playedSec = 0;                    // tiempo sonando de la pista actual
    bool historyLogged = false;
    double restorePos = -1;                  // recuperación de sesión: posición al cargar (s)
    std::wstring restorePath;
    std::wstring reanalyzing;                // pista en reanálisis
    bool endHandled = false;
    double syncBadSince = -1;                // SYNC sin poder cuadrar la fase (¿rejilla mal?)
    // Mandos con mapeo integrado (mandos_*.cpp)
    float mxBend = 0.f, mxTempBend = 0.f;    // jog/wheel y botones de tempo temporales
    float mxWheel = 0.f;                     // "wheel" de Mixxx (desplazamiento que se mantiene)
    int mxSeekDir = 0;                       // REW / FF mantenidos
    bool mxKill[3] = {false, false, false};  // graves, medios, agudos a cero
    float mxKillSaved[3] = {0.5f, 0.5f, 0.5f};
    bool mxCueHeld = false;
    std::wstring mxPlayOnLoad;               // cargar y reproducir
    double mxBeatloopSize = 4, mxBeatjumpSize = 4;
};

class MandoPuente;   // mandos con mapeo de Mixxx (mandos_puente.cpp)
class MandoActivo;

class App {
    friend class MandoPuente;
public:
    bool init(void* hwnd, bool forceNullAudio);
    void frame();               // un fotograma de interfaz
    void idle();                // lógica sin dibujar (ventana oculta)
    bool confirmClose();
    void shutdown();
    void onDropFiles(const std::vector<std::wstring>& files, ImVec2 pos);
    bool wantsClose() const { return quit_; }
    Settings& settings() { return settings_; }

    // Prueba automática integrada (--autotest): ver app.cpp.
    void startAutotest(const std::wstring& a, const std::wstring& b, const std::wstring& logPath);
    bool autotestDone() const { return autotest_.done; }
    // --load A B: carga dos pistas sin reproducir (capturas de pantalla).
    void demoLoad(const std::wstring& a, const std::wstring& b) { loadToDeck(0, a, true); loadToDeck(1, b, true); }
    bool decksLoaded() const { return deck_[0].track && deck_[1].track; }
    // Capturas: coloca los platos, los pone a sonar y sincroniza B con A.
    void demoStart(double posA, double posB, bool play, bool syncB);
    void openMidiDialog() { showMidi_ = true; }
    void setMode(int m) { mode_ = m; }
    // Capturas de pantalla: --add-folder, --browse, --sidelist
    void testAddFolder(const std::wstring& f);
    void testBrowse(const std::wstring& f);
    void testSidelist(const std::wstring& m3u);
    void testSelect(const std::wstring& path) { libSelectedPath_ = path; }
    // --dump-state: escribe el estado de los platos (prueba de recuperación de sesión).
    void dumpState(const std::wstring& file);
    bool loading() const { return loader_.busy(0) || loader_.busy(1); }
    int autotestResult() const { return autotest_.failures; }
    // --mandos-prueba informe.txt: arranca los mapeos de mandos incluidos sin el mando y los prueba.
    int mandosPrueba(const std::wstring& out);
    // Capturas del rack: --rack "preset" izquierda derecha macro [--rack-loop compases]
    void instrDemo();   // capturas: estilos cargados y caja de ritmos y bajo sonando
    void rackDemo(const std::string& preset, int left, int right, float macro, int loopBars, bool touchXy);
    void fxDemo(int fxA, int fxB, int color, float knobA, int tab = 5);   // 0.11: capturas de la pestaña FX y de la ruleta de color

    ImFont* fontBig = nullptr;   // = fontHead (compatibilidad)
    ImFont* fontHead = nullptr;  // Barlow Semi Condensed seminegrita: rótulos, títulos, BPM
    ImFont* fontBold = nullptr;  // Barlow Semi Condensed negrita: logotipo y cifras grandes
    ImFont* fontMono = nullptr;

private:
    // Lógica
    void logic();
    void pollLoader();
    void pollEngineAcks();
    void pollDevice();
    void pollMidi();
    void syncFollow();
    void persistDeckMeta(int d, bool force = false);
    void loadToDeck(int d, const std::wstring& path, bool force = false);
    void ejectDeck(int d);
    void pushGrid(int d);
    void setSync(int d, bool on);
    void setSyncMaster(int d);              // con los dos en SYNC: este plato manda
    int syncMaster() const { return syncMaster_; }
    double syncFactorFor(int follower, int master) const;
    void syncRealign(int d);                // realinear ya (clic en el medidor de fase)
    void keyMatch(int d);                   // transponer a una tonalidad compatible con el otro plato
    void togglePlay(int d);
    void applyAudioConfig();
    void startRecording();
    void stopRecording();
    void notify(const std::string& msg, bool error = false);
    void send(int d, Cmd c, double v = 0, int i = 0);
    double deckPosSec(int d) const;
    double deckLenSec(int d) const;
    double beatSec(int d) const;
    float pitchRange(int d) const { return settings_.pitchRange[d] / 100.f; }
    void tapTempo(int d);
    void updateAutotest();

    // Interfaz
    void drawTopBar(float h);
    void drawCompactDecks(ImVec2 a, ImVec2 b);
    void drawPerformance(int d);
    void drawRecordingDialog();
    void drawLibraryTools();
    std::wstring recordingFolder() const;
    void recordingTick();
    void drawWaveRows(ImVec2 a, ImVec2 b);
    void drawZoomWave(int d, ImVec2 a, ImVec2 b);
    void drawOverview(int d, ImVec2 a, ImVec2 b);
    // Ondas 0.6.1 (ui_waves.cpp)
    float waveRate(int d) const;                       // velocidad de reproducción (pitch + empujón)
    double phaseOffset(int d, bool* valid) const;      // desfase con el otro plato, en pulsos del otro (−0,5..0,5)
    void drawPhaseMeter(int d, ImVec2 a, ImVec2 b);
    void drawViewMenu();
    void drawCoverInfo(const std::wstring& path, const std::string& artist, const std::string& title, float w);
    void pollCoverFetch();
    void coverSweepTick();   // carátulas de toda la colección en segundo plano (como VirtualDJ)
    void fetchCovers(const std::vector<std::wstring>& paths, bool manual);
    void drawDeck(int d, ImVec2 a, ImVec2 b);
    void drawMixer(ImVec2 a, ImVec2 b);
    void drawColorFxPopup(const char* id);
    void drawChannelStrip(int d, float x, float y, float w, float h);
    void drawDeckTabs(int d, float x, float y, float w, float h);
    void drawDeckMenu(int d);
    void drawSyncMenu(int d);
    void hotCuePopup(int d);                // nombre / borrar un hot cue (clic derecho)
    int hcMenuCue_[kNumDecks] = {0, 0};
    std::string keyText(int key) const;   // según la notación elegida
    std::string keyCode(int key) const;   // código corto: 8A, 08A (VirtualDJ) o 1m (Open Key)
    std::string keyShort(int key) const;  // nombre corto para insignias (Am / La m)
    int camelotIndex(int key) const;      // número Camelot * 2 + (B ? 1 : 0); -1 si no hay
    float drawKeyBadges(ImDrawList* dl, ImVec2 p, float h, int key, bool compat = false);
    // Tonalidad 0.6.2 (ui_keys.cpp)
    struct KeyRelation { int level = 3; const char* name = ""; const char* hint = ""; };  // level: 0 encaja, 1 con cuidado, 2 casi (±1 semitono), 3 choca
    static KeyRelation keyRelation(int from, int to);
    int soundingKey(int d) const;          // tonalidad que suena en el plato (con Tono y pitch sin key lock)
    const char* keySourceText(int keySrc) const;
    // Rueda Camelot: devuelve la tonalidad pulsada (clic izquierdo) o -1; hovered = la que está bajo el ratón.
    // picker = true: el clic pone la tonalidad (menú "Ponerla a mano") en vez de filtrar.
    int drawCamelotWheel(const char* id, int key, ImVec2 tl, float D, int* hovered = nullptr, bool picker = false);
    void drawKeySection(const std::wstring& path, int key, float w);   // rueda + relación con los platos (Detalles)
    void setKeyFilter(int key);
    void setManualKey(const std::wstring& path, int key);
    void syncDeckKey(int d);               // la biblioteca manda en la tonalidad (etiquetas, VirtualDJ, a mano)
    void openVdjImport();
    void drawVdjImportDialog();
    void pollVdjImport();
    void drawMixAssist(float x, float y, float w);   // píldoras del mezclador: tempo y tonalidad entre platos
    bool viewFiltered() const { return search_[0] != 0 || keyFilter_ >= 0; }
    bool endWarning(int d) const;          // últimos segundos de la pista (aviso que parpadea)
    void drawPlatter(int d, ImVec2 c, float r);
    void drawMidiWizard();
    void drawMidiAssignments();
    void drawMidiSettings();
    void drawMidiMonitor();
    void drawMandosCompatibles();   // lista de los mandos que se reconocen solos
    int ensurePlaylist(const std::string& name);
    // Automix, sesión y programador (app_features.cpp)
    void automixTick();
    void setAutomix(bool on);
    void saveSession(bool clean);
    void restoreSession();
    void schedulerTick();
    void loadSchedule();
    void saveSchedule();
    void drawAutomixPanel();
    void drawStatsPanel();
    void drawSchedulerDialog();
    // Separador de pistas (ui_separator.cpp)
    void drawSeparator(ImVec2 a, ImVec2 b);
    void drawSeparatorSide(ImVec2 a, ImVec2 b);
    void drawStemPlayer(ImVec2 a, ImVec2 b);
    void separatorTick();
    void openStemSong(const StemSong& s);
    void separateFile(const std::wstring& path);
    void loadStemToDeck(int deck, const std::string& stemId);
    void exportStemMix(const std::wstring& outFile, bool onlyInstrumental, int loadDeckAfter);
    // Navegador tipo VirtualDJ (ui_browser.cpp)
    void drawBrowser(ImVec2 a, ImVec2 b);
    void drawTree(ImVec2 a, ImVec2 b);
    void drawTrackList(ImVec2 a, ImVec2 b);
    void drawSideView(ImVec2 a, ImVec2 b);
    void drawInfoView(ImVec2 a, ImVec2 b);
    void drawSidelistPanel();
    void refreshView();
    void addToSidelist(const std::vector<std::wstring>& paths);
    std::vector<std::wstring> selectedPaths();
    bool isFavorite(const std::wstring& path);
    void toggleFavorite(const std::wstring& path);
    void loadFree(const std::wstring& path);
    void buildFsTree();
    void deletePlaylistAt(int i);
    void rememberSource();
    void restoreSource();
    int treeRow(const char* id, int depth, int icon, ImU32 iconCol, const std::string& label, const char* count, bool selected, bool expandable, bool* open);
    void openNameDialog(int mode, const std::string& initial, std::vector<std::wstring> paths = {});
    void drawNameDialog();
    void listMenu(int list);
    void trackMenu(const std::vector<std::wstring>& paths, bool inList);
    void removeRowsFromList(int list, std::vector<int> positions);
    static ImGuiID rowStorageId(ImGuiSelectionBasicStorage* self, int idx);
    void drawAudioDialog();
    void drawMidiDialog();
    void drawPrefsDialog();
    void drawAboutDialog();
    void drawConfirmLoad();
    void drawStatusBar(ImVec2 a, ImVec2 b);
    // Rack de hardware virtual (ui_rack.cpp; el audio está en audio/rack.*)
    void drawRack(ImVec2 a, ImVec2 b, bool open);
    void drawRackStation(ImVec2 a, ImVec2 b);
    void drawRackXy(ImVec2 a, ImVec2 b);
    void drawRackIsolator(ImVec2 a, ImVec2 b);
    void drawRackLooper(ImVec2 a, ImVec2 b);
    bool rackSource(const char* id, std::atomic<int>& v, float w, float h);
    void rackMidiMenu(const char* popup, std::initializer_list<int> actions);
    void rackMidi(const MidiCommand& c, const std::function<void(const MidiCommand&, float, const std::function<void(float)>&)>& absolute);
    void rackLeds();
    void rackTick();
    void rackLoop(int cmd, int arg = 0);
    void rackLoopExport();
    std::string rackToJson() const;
    bool rackFromJson(const std::string& text);
    void rackBuiltinPresets();
    void loadRack();
    void saveRack();
    void rackApplyPreset(int i);
    void rackPresetStep(int dir);
    struct RackPreset { std::string name, data; bool builtin = false; std::string note; };
    std::vector<RackPreset> rackPresets_;
    std::string rackPresetName_, rackSaved_;
    double rackSaveCheck_ = 0, rackMidiXyAt_ = -10;
    bool rackMidiXyTouch_ = false, rackMidiTouchHeld_ = false, rackMouseXy_ = false;
    int rackLearning_ = -1;
    char rackNameBuf_[64] = {};
    struct XyTrail { float x, y; double t; };
    std::deque<XyTrail> rackTrail_;
    float rackMeter_[3] = {0, 0, 0};
    // Grabación de movimientos del rack (ui_motion.cpp): MACRO y pad XY en bucle de 1, 2 o 4 compases.
    struct Motion {
        enum State { Vacio, Armado, Grabando, Parado, Sonando };
        State state = Vacio;
        int bars = 2, deck = 0;
        double start = 0;              // pulso (de la rejilla del plato) en que empieza el bucle
        double lastBeat = -1e9;
        std::vector<std::array<float, 4>> buf;   // macro, X, Y, tocando — 32 por pulso
        int lastIdx = -1;
        bool hasXy = false, xyOwned = false;
        float macroBefore = 0, lastMacro = -1;
        double userMacroAt = -10;
    } motion_;
    void motionTick();
    // Botón «QUITAR FX» de la barra superior (ui_fxclear.cpp, 0.11.1).
    struct ActiveFx {
        std::string what;   // frase completa (tooltip y lista)
        std::string tag;    // nombre corto para el botón del plato
        enum Kind { Ruleta, FxPlato, Ranura, Macro, PadXy, Aislador, Movimiento, Reverso } kind;
        int index;
    };
    std::vector<ActiveFx> activeEffects(int onlyDeck = -1) const;
    void clearEffect(const ActiveFx& e);
    int clearEffectsOf(int d);   // d < 0: todo
    int clearAllEffects();
    void drawFxClearButton(float& rx, float by, float bh);
    // maxW: lo más ancho que puede ser; roomyW: ancho que deja sitio al contador de compases.
    float drawDeckFxButton(int d, float right, float y, float h, float maxW, float roomyW);   // 0.11.3: botón de efectos de cada plato
    void drawMotionControls(ImVec2 a, float w, float h = 0);
    bool motionBeat(int d, double& beat) const;
    // Instrumentos del rack (ui_instruments.cpp; el audio está en audio/instruments.*)
    void drawRackDrums(ImVec2 a, ImVec2 b);
    void drawRackBass(ImVec2 a, ImVec2 b);
    void drawRackSampler(ImVec2 a, ImVec2 b);
    void instrClockBar(ImVec2 a, float w, float h);
    std::string instrToJson() const;
    bool instrFromJson(const std::string& text);
    void setPad(int k, TrackPtr t, const std::string& name, const std::wstring& path, int builtin);
    void loadPadFile(int k, const std::wstring& path);
    void padsTick();
    void padCmd(int pad, int action);
    void initPads();
    struct PadSlot { TrackPtr track; std::string name; std::wstring path; int builtin = -1; bool loading = false, cleared = false; };
    struct PadLoad { int pad; std::wstring path; std::future<TrackPtr> f; };
    struct PadRetire { int pad; uint32_t gen; TrackPtr track; };
    std::array<PadSlot, kPads> pads_{};
    std::vector<PadLoad> padLoads_;
    std::vector<PadRetire> padRetire_;
    uint32_t padGenCounter_ = 0;
    int padSel_ = 0;
    bool padHeld_[kPads] = {};

    void* hwnd_ = nullptr;
    bool quit_ = false;
    Settings settings_;
    Engine engine_;
    AudioIO audio_;
    TrackLoader loader_;
    Recorder recorder_;
    Library library_;
    MidiManager midi_;
    float masterMeter_[2] = {0, 0};          // nivel del master (con caída), para los VU de los mandos
    MandoPuente* mandos_ = nullptr;          // se crea en init y se destruye en shutdown
    double midiScanAt_ = 0;                  // detección de mandos conectados o desconectados
    void midiRescan(bool notifyNew);         // reabre los puertos y arranca o para los mapeos
    void browseMove(int step);               // mueve la selección de la lista central
    void browseSourceStep(int step);         // pasa a la lista anterior o siguiente del árbol (mandos)
    void loadSelectedInto(int deck, bool play);  // deck -1: el primer plato parado
    DeckUi deck_[kNumDecks];

    // Estado de interfaz
    bool showRecording_ = false, showLibraryTools_ = false;
    std::string recLastPath_[2];
    bool recLastPlaying_[2] = {};
    std::vector<std::string> recLog_;
    bool recordingTracklist_ = false;
    int recordingFormat_ = 0;
    bool rollHeld_[2] = {}, reverseHeld_[2] = {};
    bool showAudio_ = false, showMidi_ = false, showPrefs_ = false, showAbout_ = false;
    AudioConfig pendingAudio_;
    std::string statusMsg_;
    bool statusErr_ = false;
    double statusTime_ = -100;
    double lostSince_ = -1;
    double lastRetry_ = 0;
    ImVec2 deckRect_[kNumDecks][2]{};
    ImVec2 waveRect_[kNumDecks][2]{};
    // Estructura de cada pista cargada (cortes y subidones) y vista general precalculada.
    struct OverviewData { int width = 0; std::vector<uint8_t> low, mid, high, peak; };
    struct TrackShape {
        const Track* track = nullptr;
        double bpm = 0, anchor = 0;
        int firstBar = 0;                  // índice del primer compás (puede ser negativo)
        std::vector<uint8_t> barKind;      // por compás: 0 normal, 1 corte sin bombo, 2 silencio
        std::vector<int> drops;            // compases donde vuelve el bombo tras un corte (subidón)
        OverviewData ov[3];
        int ovNext = 0;
    };
    TrackShape shape_[kNumDecks];
    double menuTime_[kNumDecks] = {0, 0};   // posición del clic derecho en la onda grande
    const TrackShape& trackShape(int d);
    const OverviewData& overviewData(int d, int width);
    // Navegador: origen de la lista central (nodo del árbol)
public:
    enum class Src { Coleccion, Carpeta, Lista, Sugerencias, Stems, Smart };
    struct BrowseSrc {
        Src kind = Src::Coleccion;
        std::wstring folder;      // Carpeta / Stems
        int list = -1;            // Lista
        bool operator==(const BrowseSrc& o) const { return kind == o.kind && folder == o.folder && list == o.list; }
    };
    struct ViewRow {
        int track = -1;           // índice en library_.tracks(); -1 = fuera de la colección
        int listPos = -1;         // posición en la lista (orígenes de tipo Lista)
        std::wstring path;        // ruta (siempre)
        std::string title;        // para filas fuera de la colección
    };
    struct FsNode {
        std::wstring path;
        std::string name;
        int icon = 0;             // ui::Ic
        bool listed = false;
        std::vector<FsNode> kids;
    };
private:
    BrowseSrc src_;
    uint32_t srcGen_ = 1, viewSrcGen_ = 0;
    char search_[256] = {};
    bool searchAll_ = false;      // buscar en toda la colección
    std::vector<ViewRow> view_;
    uint64_t viewVersion_ = 0;
    std::string viewQuery_;
    int autoHidCols_ = 0;                       // columnas ocultadas solas por falta de sitio (bit 1 género, 2 valoración)
    int syncMaster_ = -1;                       // plato que marca el tempo (-1 = sin SYNC)
    int syncFollower_ = -1;
    int keyFilter_ = -1, viewKeyFilter_ = -1;   // filtro armónico: solo las compatibles con esta tonalidad
    int wheelMenuKey_ = -1;
    // Importación de VirtualDJ
    bool showVdj_ = false, vdjFirstRun_ = false;
    std::vector<VdjDatabase> vdjDbs_;
    int vdjSel_ = 0;
    VdjImportOptions vdjOpt_;
    std::future<VdjImportReport> vdjFuture_;
    std::future<std::vector<VdjDatabase>> vdjFind_;
    std::atomic<float> vdjProgress_{0.f};
    std::atomic<bool> vdjCancel_{false};
    std::string vdjResult_;
    bool vdjResultErr_ = false;
    bool viewSearchAll_ = false, viewRecursive_ = false;
    int viewSortCol_ = -1;
    bool viewSortAsc_ = true;
    uint64_t viewSuggestKey_ = 0;
    ImGuiSelectionBasicStorage sel_;
    int cursorRow_ = -1;          // fila con el foco (MIDI, información)
    std::wstring libSelectedPath_;
    bool scrollToCursor_ = false;
    std::vector<FsNode> fsRoots_, favRoots_;
    bool fsBuilt_ = false;
    bool listView_ = false;       // la vista actual es una lista editable (orden natural)
    float treeX0_ = 0, treeX1_ = 0, treeRowH_ = 0;
    int nameMode_ = 0;            // 0 renombrar, 1 lista nueva, 2 guardar Sidelist como, 3 lista nueva con pistas
    bool openNamePopup_ = false;
    std::vector<std::wstring> namePaths_;
    bool sortDirty_ = false;
    bool autoAnalyze_ = false;    // --add-folder: analizar al terminar el escaneo
    void selectSource(const BrowseSrc& s);
    void drawFsNode(FsNode& n, int depth, const char* prefix);
    void suggestions(std::vector<ViewRow>& out);
    std::unordered_map<std::string, bool> treeOpen_;
    char treeFilter_[64] = {};
    int renameList_ = -1;         // lista que se está renombrando (ventana emergente)
    char renameBuf_[96] = {};
    int dragList_ = -2;           // lista de origen del arrastre (-2 = ninguno)
    std::vector<int> dragRows_;   // posiciones arrastradas dentro de esa lista
    int sideSel_ = -1;
    ImVec2 sideRect_[2]{}, listRect_[2]{}, browserRect_[2]{};
    // Carga pendiente de confirmación
    int confirmDeck_ = -1;
    std::wstring confirmPath_;
    // Medidores master
    float masterHold_[2] = {0, 0};
    double masterHoldTime_[2] = {0, 0};
    double limitFlashUntil_ = 0;
    double clipFlashUntil_ = 0;
    std::wstring lastRecording_;
    bool coverBatch_ = false;
    size_t coverSweepPos_ = 0, coverSweepCount_ = 0;
    bool coverSweepDone_ = false;
    double coverSweepLast_ = 0, coverOfflineUntil_ = 0, coverMemSaved_ = 0;
    int coverFailRun_ = 0, coverSweepFound_ = 0;
    int coverFound_ = 0, coverMissing_ = 0;
    struct Pickup { bool engaged = false, hasPrev = false; float prev = 0, lastApplied = 0; };
    Pickup pickup_[MA_Count * 2];
    double midiHit_[MA_Count * 2] = {};
    double midiPickupHint_ = -10;
    double ledTestUntil_ = 0;
    // Automix
    bool automixOn_ = false;
    int automixIndex_ = 0;        // siguiente pista de la lista Automix
    int automixPhase_ = 0;        // 0 = sonando, 1 = transición
    int automixFrom_ = 0, automixTo_ = 1;
    double automixStart_ = 0, automixDur_ = 0;
    float automixXfFrom_ = 0.5f;
    // Sesión
    double lastSessionSave_ = 0;
    bool sessionRecovered_ = false;
    // Programador de eventos
    struct SchedEvent { int hour = 22, minute = 0; int action = 0; bool enabled = true; std::string lastFired; };
    std::vector<SchedEvent> schedule_;
    bool showScheduler_ = false;
    double fadeOutStart_ = -1; float fadeOutFrom_ = 0;
    // Modo: 0 = Mezcla, 1 = Separador, 2 = Biblioteca (navegador a pantalla completa)
    int mode_ = 0;
    // Separador
    StemSeparator stems_;
    std::vector<StemSong> stemSongs_;
    uint64_t stemSongsVer_ = 0;
    std::wstring stemOpenFolder_;
    StemSong stemOpen_;
    std::array<TrackPtr, kStemSlots> stemTracks_{};
    std::array<std::string, kStemSlots> stemIds_{};
    int stemCount_ = 0;
    std::future<std::vector<TrackPtr>> stemLoadFuture_;
    StemSet stemSets_[4];
    uint32_t stemGen_ = 0;
    std::vector<std::pair<uint32_t, std::array<TrackPtr, kStemSlots>>> stemPending_;
    std::array<TrackPtr, kStemSlots> stemActive_{};  // los que usa el motor
    bool stemModel6_ = true;
    std::future<std::string> stemExport_;
    int stemExportLoadDeck_ = -1;
    std::wstring stemExportFile_;
    std::unordered_map<std::wstring, std::string> stemTitles_;  // ruta de stem -> "Título (Voz)"
    std::wstring separatorDropHint_;
    ImVec2 sepRect_[2]{};
    // Asistente del mando
    int wizStep_ = -1;
    double wizOkUntil_ = 0;
    std::string wizOkText_;
    // Platos: modo de la fila de pads (0 cues, 1 loop, 2 salto, 3 rejilla, 4 tono, 5 efecto)
    int padMode_[kNumDecks] = {1, 1};
    bool showHp_ = false;
    std::future<BeatInfo> reanalysis_[kNumDecks];
    std::wstring reanalysisPath_[kNumDecks];
    void reanalyze(int d);
    std::wstring selectedLibraryPath();
    double lastSave_ = 0;
    double now_ = 0;

    struct Autotest {
        bool active = false, done = false;
        int failures = 0;
        int step = 0;
        double t0 = 0, stepT = 0;
        std::wstring a, b, log, badFile;
        std::vector<std::string> lines;
        uint64_t frames0 = 0;
        std::vector<MidiBinding> midiBackup;
        double posMark = 0, amTransitionAt = -1, syncBpm = 0;
        bool amChecked = false;
        double amDoneAt = -1;
        std::vector<SchedEvent> schedBackup;
        MidiSettings midiSettingsBackup;
        StemSong stemSong;
        std::wstring stemExportPath;
        // Mandos simulados (mapeos de Mixxx): acción, espera y comprobación.
        struct MxPaso { std::string file; double wait; std::function<void()> act; std::function<bool()> ok; std::string desc; };
        std::vector<MxPaso> mx;
        size_t mxIdx = 0;
        double mxAt = -1;
        MandoActivo* mxSim = nullptr;
        double jogSimUntil = -1, jogSimRate = 0, jogSimAcc = 0;   // giro simulado del aro del jog (pasos por segundo)
        uint8_t jogSimCc = 0x21;
        float jogSimPeak = 0;
        std::string mxFile;
        float mxFader = 0.8f, mxXf = 0.5f;
        std::string rackBackup;   // estado del rack antes de la prueba
        std::string instrBackup;
        float macroBefore = 0;
        uint32_t instrHits[8] = {};
        std::wstring rackExport;
    } autotest_;
    std::wstring rackLastExport_;
};

} // namespace rdj
