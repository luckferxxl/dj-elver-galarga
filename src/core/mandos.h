// Mapeos de mandos de Mixxx incluidos en Ramiro DJ (res/mandos, GPL-2.0-o-posterior):
// lectura de los archivos (recursos del .exe o carpeta), XML, catálogo y reconocimiento
// del mando por el nombre de su puerto MIDI. La parte que los hace funcionar (scripts
// y traducción de controles) está en src/ui/mandos_*.cpp.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rdj::mandos {

// ------------------------------------------------------------------ XML mínimo
struct XmlNode {
    std::string name, text;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<XmlNode> kids;
    const XmlNode* child(const char* n) const;          // primer hijo con ese nombre
    std::string attr(const char* n) const;
    std::string childText(const char* n) const;         // texto recortado del hijo
};
bool parseXml(const std::string& src, XmlNode& root, std::string* err = nullptr);

// ------------------------------------------------------------------ mapeo
enum Opt : uint32_t {
    OptInvert = 1u << 0, OptRot64 = 1u << 1, OptRot64Inv = 1u << 2, OptRot64Fast = 1u << 3, OptDiff = 1u << 4,
    OptButton = 1u << 5, OptSwitch = 1u << 6, OptHercJog = 1u << 7, OptSpread64 = 1u << 8, OptSelectKnob = 1u << 9,
    OptSoftTakeover = 1u << 10, OptScript = 1u << 11, Opt14Msb = 1u << 12, Opt14Lsb = 1u << 13, OptHercJogFast = 1u << 14,
};

struct InputMap {
    std::string group, key;
    uint8_t status = 0, midino = 0;   // status 0xF0: SysEx (va a incomingData)
    uint32_t opts = 0;
};
struct OutputMap {
    std::string group, key;
    uint8_t status = 0, midino = 0, on = 0x7F, off = 0;
    double minimum = 0.5, maximum = 1.0;
    bool sendOff = true;
};
struct ScriptFile { std::string file, prefix; };
struct Setting {
    std::string variable, type, value;   // valor por defecto como texto
};
struct Mapping {
    std::string file, name, author, description, forum, controllerId;
    std::vector<ScriptFile> scripts;     // en orden de carga (sin common-controller-scripts.js)
    std::vector<InputMap> inputs;
    std::vector<OutputMap> outputs;
    std::vector<Setting> settings;
};

struct CatalogEntry {
    std::string file, name, author, description;
    bool generic = false;                // mapeos genéricos (teclados, luces…): nunca se eligen solos
};

// Archivos: primero %APPDATA%\RamiroDJ\mandos (los del usuario), después RDJ_MANDOS_DIR
// (pruebas) y por último los incluidos en el ejecutable.
bool readFile(const std::string& name, std::string& out);
std::vector<std::string> listFiles();
const std::vector<CatalogEntry>& catalog();
bool loadMapping(const std::string& file, Mapping& m, std::string* err = nullptr);

// Reconocimiento: puntuación > 0 si el mapeo encaja con el nombre del puerto.
int matchScore(const std::string& portName, const CatalogEntry& e);
std::string bestMatch(const std::string& portName, int* score = nullptr);   // archivo o ""


struct UniversalControllerModel {
    std::string brand;             // Pioneer DJ / AlphaTheta, Denon DJ, Numark, Hercules, Native Instruments, Reloop, Roland, Allen & Heath, Akai Professional, Behringer, Vestax
    std::string model;             // Modelo comercial
    std::string protocol;          // MIDI USB / MIDI 14-bit / MIDI + HID
    std::string validationStatus;  // "Verificado Hardware", "Mapeo Nativo Mixxx XML", "Perfil MIDI Estándar Auto-Adaptativo"
    std::string mappingFile;       // Archivo .midi.xml asociado o "preset:wego" / "auto:standard"
    int decks = 2;
    int padsPerDeck = 8;
    bool hasStemPads = true;
    bool hasLedFeedback = true;
};

const std::vector<UniversalControllerModel>& universalCatalog();
const UniversalControllerModel* identifyController(const std::string& portName);

} // namespace rdj::mandos
