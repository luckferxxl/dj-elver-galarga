#include "settings.h"
#include "util.h"
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace rdj {

using json = nlohmann::json;
static std::wstring settingsFile() { return appDataDir() + L"\\ajustes.json"; }

void Settings::load() {
    std::ifstream f(std::filesystem::path(settingsFile()), std::ios::binary);
    if (!f) return;
    try {
        json j; f >> j;
        audio.deviceName = j.value("device", "");
        audio.periodMs = j.value("periodMs", 10);
        audio.sampleRate = j.value("sampleRate", 0);
        automixFadeBeats = j.value("automixFadeBeats", 16);
        // La 0.6.1 renueva los diseños: al actualizar, todos pasan una vez al nuevo por defecto.
        theme = j.value("uiVer", 0) >= 6 ? j.value("theme", 0) : 0;
        if (j.value("uiVer", 0) < 7) theme = 5;   // 0.10.3: una vez, el diseño MixLab Carbon (luego se respeta el elegido)
        keyNotation = j.value("keyNotation", 0);
        useTagKeys = j.value("useTagKeys", true); vdjOffered = j.value("vdjOffered", false); endWarnSec = j.value("endWarnSec", 30);
        automixSync = j.value("automixSync", true);
        automixRepeat = j.value("automixRepeat", false);
        audio.exclusive = j.value("exclusive", false);
        audio.hpRouting = (HpRouting)j.value("hpRouting", 0);
        audio.hpDeviceName = j.value("hpDevice", "");
        recFormat = j.value("recFormat", 0);
        recFolder = j.value("recFolder", ""); recName = j.value("recName", "Mezcla"); recTracklist = j.value("recTracklist", true);
        decksCollapsed = j.value("decksCollapsed", false); showPerformance = j.value("showPerformance", true);
        deckHeight = j.value("deckHeight", 0.f); mouseScratchSensitivity = j.value("mouseScratchSensitivity", 1.f);
        suggestDeck = j.value("suggestDeck", 0); suggestBpmPercent = j.value("suggestBpmPercent", 8.f); suggestKey = j.value("suggestKey", true);
        if (j.contains("trackTags")) trackTags = j["trackTags"].get<decltype(trackTags)>();
        if (j.contains("trackColors")) trackColors = j["trackColors"].get<decltype(trackColors)>();
        if (j.contains("smartLists")) for (auto& q : j["smartLists"]) smartLists.push_back({q.value("name", "Lista"), q.value("tag", ""), q.value("minBpm", 0.f), q.value("maxBpm", 250.f), q.value("unplayed", false), q.value("minRating", 0)});
        minBpm = j.value("minBpm", 70.0); maxBpm = j.value("maxBpm", 180.0);
        pitchRange[0] = j.value("pitchRangeA", 8.f); pitchRange[1] = j.value("pitchRangeB", 8.f);
        pitchInvertUi = j.value("pitchInvertUi", false);
        pitchOnLoadMode = std::clamp(j.value("pitchOnLoadMode", 0), 0, 2);
        if (j.value("uiVer", 0) < 8) {
            if (pitchRange[0] > 16.5f) pitchRange[0] = 8.f;
            if (pitchRange[1] > 16.5f) pitchRange[1] = 8.f;
        }
        quantize = j.value("quantize", true);
        xfCurve = j.value("xfCurve", 0);
        colorFx = std::clamp(j.value("colorFx", 0), 0, 3);
        masterVol = j.value("masterVol", 0.8f); hpVol = j.value("hpVol", 0.7f); hpMix = j.value("hpMix", 0.f);
        libraryHeight = j.value("libraryHeight", 0.f);
        winW = j.value("winW", 1440); winH = j.value("winH", 900); maximized = j.value("maximized", false);
        waveZoomSec = j.value("waveZoomSec", 8.f);
        confirmLoadWhilePlaying = j.value("confirmLoad", true);
        waveStyle = j.value("waveStyle", 0);
        showWaves = j.value("showWaves", true); waveRowH = j.value("waveRowH", 0.f);
        waveGrid = j.value("waveGrid", true); waveStructure = j.value("waveStructure", true);
        syncBar = j.value("syncBar", false); syncPhase = j.value("syncPhase", true); syncAutoMaster = j.value("syncAutoMaster", true); useVdjGrid = j.value("useVdjGrid", true); coverInJog = j.value("coverInJog", true); coverDownload = std::clamp(j.value("coverDownload", 2), 0, 2);
        uiScale = std::clamp(j.value("uiScale", 100), 75, 175);
        infoWheelOpen = j.value("infoWheelOpen", true); infoMetaOpen = j.value("infoMetaOpen", true); rowDensity = std::clamp(j.value("rowDensity", -1), -1, 2);
        showTree = j.value("showTree", true); showSide = j.value("showSide", true); showInfo = j.value("showInfo", true);
        sideTab = j.value("sideTab", 0);
        browseRecursive = j.value("browseRecursive", false);
        compactRows = j.value("compactRows", false);
        browseKind = j.value("browseKind", 0); browseRef = j.value("browseRef", "");
        treeW = j.value("treeW", 0.f); sideW = j.value("sideW", 0.f); infoW = j.value("infoW", 0.f);
        imguiIni = j.value("imguiIni", "");
        rackOpen = j.value("rackOpen", true); rackH = j.value("rackH", 0.f);
        rackLeft = std::clamp(j.value("rackLeft", 0), 0, 6); rackRight = std::clamp(j.value("rackRight", 1), 0, 6);
    } catch (...) {
    }
    recFormat = std::clamp(recFormat, 0, 1);
    waveStyle = std::clamp(waveStyle, 0, 2);
    keyNotation = std::clamp(keyNotation, 0, 5);
    endWarnSec = std::clamp(endWarnSec, 0, 120);
    if (waveRowH != 0) waveRowH = std::clamp(waveRowH, 40.f, 200.f);
    if (deckHeight != 0) deckHeight = std::clamp(deckHeight, 140.f, 560.f);
    if (rackH != 0) rackH = std::clamp(rackH, 200.f, 420.f);
    suggestDeck = std::clamp(suggestDeck, 0, 2);
    suggestBpmPercent = std::clamp(suggestBpmPercent, 1.f, 25.f);
    mouseScratchSensitivity = std::clamp(mouseScratchSensitivity, 0.25f, 4.f);
    // Un tamaño de ventana absurdo (p. ej. guardado minimizada) dejaría la ventana invisible.
    if (winW < 1024 || winH < 640 || winW > 10000 || winH > 10000) { winW = 1440; winH = 900; }
    if (minBpm < 40 || minBpm > 150) minBpm = 70;
    if (maxBpm < minBpm + 30 || maxBpm > 250) maxBpm = 180;
}

void Settings::save() const {
    json j;
    j["device"] = audio.deviceName; j["periodMs"] = audio.periodMs; j["sampleRate"] = audio.sampleRate;
    j["uiVer"] = 8; j["theme"] = theme; j["keyNotation"] = keyNotation;
    j["pitchInvertUi"] = pitchInvertUi; j["pitchOnLoadMode"] = pitchOnLoadMode;
    j["automixFadeBeats"] = automixFadeBeats; j["automixSync"] = automixSync; j["automixRepeat"] = automixRepeat; j["exclusive"] = audio.exclusive;
    j["hpRouting"] = (int)audio.hpRouting; j["hpDevice"] = audio.hpDeviceName;
    j["recFolder"] = recFolder; j["recName"] = recName; j["recTracklist"] = recTracklist;
    j["decksCollapsed"] = decksCollapsed; j["showPerformance"] = showPerformance; j["deckHeight"] = deckHeight;
    j["mouseScratchSensitivity"] = mouseScratchSensitivity;
    j["suggestDeck"] = suggestDeck; j["suggestBpmPercent"] = suggestBpmPercent; j["suggestKey"] = suggestKey;
    j["trackTags"] = trackTags; j["trackColors"] = trackColors;
    j["smartLists"] = json::array();
    for (const auto& q : smartLists) j["smartLists"].push_back({{"name",q.name},{"tag",q.tag},{"minBpm",q.minBpm},{"maxBpm",q.maxBpm},{"unplayed",q.unplayed},{"minRating",q.minRating}});
    j["recFormat"] = recFormat; j["minBpm"] = minBpm; j["maxBpm"] = maxBpm;
    j["pitchRangeA"] = pitchRange[0]; j["pitchRangeB"] = pitchRange[1];
    j["quantize"] = quantize; j["xfCurve"] = xfCurve; j["colorFx"] = colorFx;
    j["masterVol"] = masterVol; j["hpVol"] = hpVol; j["hpMix"] = hpMix;
    j["libraryHeight"] = libraryHeight;
    j["winW"] = winW; j["winH"] = winH; j["maximized"] = maximized;
    j["waveZoomSec"] = waveZoomSec; j["confirmLoad"] = confirmLoadWhilePlaying;
    j["waveStyle"] = waveStyle;
    j["showWaves"] = showWaves; j["waveRowH"] = waveRowH; j["waveGrid"] = waveGrid; j["waveStructure"] = waveStructure;
    j["rackOpen"] = rackOpen; j["rackH"] = rackH; j["rackLeft"] = rackLeft; j["rackRight"] = rackRight;
    j["syncBar"] = syncBar; j["syncPhase"] = syncPhase; j["syncAutoMaster"] = syncAutoMaster; j["useVdjGrid"] = useVdjGrid; j["coverInJog"] = coverInJog; j["coverDownload"] = coverDownload; j["uiScale"] = uiScale; j["infoWheelOpen"] = infoWheelOpen; j["infoMetaOpen"] = infoMetaOpen; j["rowDensity"] = rowDensity;
    j["useTagKeys"] = useTagKeys; j["vdjOffered"] = vdjOffered; j["endWarnSec"] = endWarnSec;
    j["showTree"] = showTree; j["showSide"] = showSide; j["showInfo"] = showInfo; j["sideTab"] = sideTab;
    j["browseRecursive"] = browseRecursive; j["compactRows"] = compactRows;
    j["browseKind"] = browseKind; j["browseRef"] = browseRef;
    j["treeW"] = treeW; j["sideW"] = sideW; j["infoW"] = infoW;
    j["imguiIni"] = imguiIni;
    std::ofstream f(std::filesystem::path(settingsFile()), std::ios::binary | std::ios::trunc);
    f << j.dump(1);
}

} // namespace rdj
