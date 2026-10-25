// Preferencias persistentes (%APPDATA%\RamiroDJ\ajustes.json).
#pragma once
#include "../audio/audio_io.h"
#include <string>
#include <map>
#include <vector>

namespace rdj {

struct SmartList { std::string name, tag; float minBpm = 0, maxBpm = 250; bool unplayed = false; int minRating = 0; };
struct Settings {
    AudioConfig audio;
    std::string recFolder, recName = "Mezcla";
    bool recTracklist = true;
    bool decksCollapsed = false, showPerformance = true;
    float deckHeight = 0, mouseScratchSensitivity = 1;
    int suggestDeck = 0; // 0 auto, 1 A, 2 B
    float suggestBpmPercent = 8;
    bool suggestKey = true;
    std::map<std::string, std::string> trackTags;
    std::map<std::string, int> trackColors;
    std::vector<SmartList> smartLists;
    int recFormat = 0;          // RecFormat
    double minBpm = 70, maxBpm = 180;
    float pitchRange[2] = {8.f, 8.f};  // ± %
    bool pitchInvertUi = false;        // false = arriba más rápido (+), true = arriba más lento (-)
    int pitchOnLoadMode = 0;           // 0 = mantener los 2 igualados y avisar, 1 = poner a 0% y avisar, 2 = conservar pitch anterior
    bool quantize = true;
    int xfCurve = 0;
    int colorFx = 0;            // 0.11: color de las ruletas FILTER de la mesa (ColorFxType)
    float masterVol = 0.8f, hpVol = 0.7f, hpMix = 0.f;
    float libraryHeight = 0.f;  // 0 = automático
    int winW = 1440, winH = 900;
    bool maximized = false;
    float waveZoomSec = 8.f;
    bool confirmLoadWhilePlaying = true;
    int theme = 5;              // diseño (5 = MixLab Carbon desde la 0.10.3) (ver ui::themeAt); 0 = "Ramiro" (0.5)
    int keyNotation = 0;        // 0 = Camelot + español, 1 = Camelot, 2 = musical (Am), 3 = español, 4 = VirtualDJ (08A), 5 = Open Key (1m)
    bool useTagKeys = true;     // la tonalidad de la etiqueta del archivo manda sobre la detectada
    bool vdjOffered = false;    // ya se avisó de que hay una biblioteca de VirtualDJ que importar
    int endWarnSec = 30;        // aviso de final de pista (0 = nunca)
    int automixFadeBeats = 16;
    bool automixSync = true;
    bool automixRepeat = false;
    int waveStyle = 0;          // 0 = color del plato (capas), 1 = tres bandas (graves/medios/agudos), 2 = espectro RGB
    // Vista (0.6.1)
    bool showWaves = true;      // ondas grandes arriba
    float waveRowH = 0;         // alto de cada onda grande (0 = automático)
    bool waveGrid = true;       // rejilla de pulsos y compases sobre las ondas
    bool waveStructure = true;  // marcar cortes (sin bombo) y subidones
    bool syncBar = false;       // SYNC alinea compases completos (no solo pulsos)
    bool syncPhase = true;      // SYNC de tempo y fase (false = solo tempo, la fase a mano)
    bool coverInJog = true;     // carátula en el centro del plato (0.10.3, de Ramiro MixLab)
    bool useVdjGrid = true;     // al cargar, usar la rejilla de VirtualDJ si la tiene analizada
    bool syncAutoMaster = true; // con los dos platos en SYNC, el maestro pasa al que sigue sonando
    // Carátulas de Internet: 0 = solo cuando lo pido, 1 = lo que cargo en los platos,
    // 2 = toda la colección en segundo plano (como coverDownload de VirtualDJ).
    int coverDownload = 2;
    bool infoWheelOpen = true, infoMetaOpen = true;   // secciones plegables de Detalles
    int uiScale = 100;          // tamaño de la interfaz sobre la escala de Windows (%): 90, 100, 110, 125, 150
    int rowDensity = -1;        // filas del navegador: 0 cómodas, 1 compactas, 2 muy compactas (-1 = de compactRows)
    // Navegador (tipo VirtualDJ)
    bool showTree = true, showSide = true, showInfo = true;
    int sideTab = 0;            // 0 = Sidelist, 1 = Automix, 2 = Estadísticas
    bool browseRecursive = false;
    bool compactRows = false;
    int browseKind = 0;         // origen al abrir: 0 colección, 1 carpeta, 2 lista, 3 sugerencias
    std::string browseRef;      // carpeta (UTF-8) o nombre de la lista
    float treeW = 0, sideW = 0, infoW = 0;  // anchos de los paneles (0 = automático)
    std::string imguiIni;       // columnas de las tablas
    // Rack de hardware virtual: desplegado, alto y módulo de cada panel (0 efectos, 1 pad XY, 2 aislador, 3 loop, 4 ritmos, 5 sampler, 6 bajo)
    bool rackOpen = true;
    float rackH = 0;            // 0 = automático
    int rackLeft = 0, rackRight = 1;

    void load();
    void save() const;
};

} // namespace rdj
