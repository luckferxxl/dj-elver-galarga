#include <set>
#include "mandos.h"
#include "util.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace rdj::mandos {

// ================================================================== XML

const XmlNode* XmlNode::child(const char* n) const {
    for (auto& k : kids) if (k.name == n) return &k;
    return nullptr;
}
std::string XmlNode::attr(const char* n) const {
    for (auto& a : attrs) if (a.first == n) return a.second;
    return {};
}
static std::string trimmed(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
std::string XmlNode::childText(const char* n) const {
    const XmlNode* c = child(n);
    return c ? trimmed(c->text) : std::string();
}

namespace {

void appendUtf8(std::string& o, unsigned cp) {
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
}

std::string decodeEntities(const std::string& s, const std::map<std::string, std::string>* custom = nullptr) {
    if (s.find('&') == std::string::npos) return s;
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { o += s[i]; continue; }
        const size_t e = s.find(';', i);
        if (e == std::string::npos || e - i > 10) { o += s[i]; continue; }
        const std::string ent = s.substr(i + 1, e - i - 1);
        if (ent == "lt") o += '<';
        else if (ent == "gt") o += '>';
        else if (ent == "amp") o += '&';
        else if (ent == "quot") o += '"';
        else if (ent == "apos") o += '\'';
        else if (custom && custom->count(ent)) o += custom->at(ent);
        else if (!ent.empty() && ent[0] == '#') {
            const unsigned cp = (unsigned)(ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? std::strtoul(ent.c_str() + 2, nullptr, 16) : std::strtoul(ent.c_str() + 1, nullptr, 10));
            appendUtf8(o, cp);
        } else { o += s.substr(i, e - i + 1); }
        i = e;
    }
    return o;
}

struct XmlParser {
    const std::string& s;
    size_t p = 0;
    std::string err;
    std::map<std::string, std::string> ents;   // <!ENTITY nombre "valor"> del DOCTYPE
    explicit XmlParser(const std::string& src) : s(src) {}

    bool startsWith(const char* t) const { return s.compare(p, std::strlen(t), t) == 0; }
    void skipWs() { while (p < s.size() && std::isspace((unsigned char)s[p])) ++p; }
    bool skipPast(const char* t) {
        const size_t e = s.find(t, p);
        if (e == std::string::npos) { err = std::string("falta ") + t; return false; }
        p = e + std::strlen(t);
        return true;
    }
    // Salta prólogo, comentarios, DOCTYPE e instrucciones.
    bool skipMisc() {
        for (;;) {
            skipWs();
            if (startsWith("<?")) { if (!skipPast("?>")) return false; }
            else if (startsWith("<!--")) { if (!skipPast("-->")) return false; }
            else if (startsWith("<!DOCTYPE")) { if (!doctype()) return false; }
            else if (startsWith("<!")) { if (!skipPast(">")) return false; }
            else return true;
        }
    }
    // DOCTYPE con subconjunto interno: se guardan las entidades (algunos mapeos las usan).
    bool doctype() {
        const size_t gt = s.find('>', p), br = s.find('[', p);
        if (br == std::string::npos || (gt != std::string::npos && gt < br)) return skipPast(">");
        const size_t end = s.find("]", br);
        if (end == std::string::npos) { err = "DOCTYPE sin cerrar"; return false; }
        const std::string sub = s.substr(br + 1, end - br - 1);
        size_t q = 0;
        while ((q = sub.find("<!ENTITY", q)) != std::string::npos) {
            q += 8;
            while (q < sub.size() && std::isspace((unsigned char)sub[q])) ++q;
            size_t ne = q;
            while (ne < sub.size() && !std::isspace((unsigned char)sub[ne])) ++ne;
            const std::string name = sub.substr(q, ne - q);
            size_t v = sub.find_first_of("\"'", ne);
            if (v == std::string::npos) break;
            const char quote = sub[v];
            const size_t ve = sub.find(quote, v + 1);
            if (ve == std::string::npos) break;
            ents[name] = sub.substr(v + 1, ve - v - 1);
            q = ve + 1;
        }
        p = end + 1;
        return skipPast(">");
    }
    std::string readName() {
        const size_t a = p;
        while (p < s.size() && !std::isspace((unsigned char)s[p]) && s[p] != '>' && s[p] != '/' && s[p] != '=') ++p;
        return s.substr(a, p - a);
    }
    bool element(XmlNode& n, int depth) {
        if (depth > 64) { err = "demasiado anidado"; return false; }
        if (p >= s.size() || s[p] != '<') { err = "se esperaba <"; return false; }
        ++p;
        n.name = readName();
        for (;;) {
            skipWs();
            if (p >= s.size()) { err = "fin inesperado"; return false; }
            if (s[p] == '/') { if (p + 1 < s.size() && s[p + 1] == '>') { p += 2; return true; } err = "/ suelto"; return false; }
            if (s[p] == '>') { ++p; break; }
            std::string an = readName();
            skipWs();
            if (p >= s.size() || s[p] != '=') { n.attrs.push_back({an, ""}); continue; }
            ++p;
            skipWs();
            if (p >= s.size() || (s[p] != '"' && s[p] != '\'')) { err = "atributo sin comillas"; return false; }
            const char q = s[p++];
            const size_t e = s.find(q, p);
            if (e == std::string::npos) { err = "atributo sin cerrar"; return false; }
            n.attrs.push_back({an, decodeEntities(s.substr(p, e - p), &ents)});
            p = e + 1;
        }
        // Contenido
        for (;;) {
            if (p >= s.size()) { err = "sin cerrar <" + n.name + ">"; return false; }
            if (startsWith("<!--")) { if (!skipPast("-->")) return false; continue; }
            if (startsWith("<![CDATA[")) {
                p += 9;
                const size_t e = s.find("]]>", p);
                if (e == std::string::npos) { err = "CDATA sin cerrar"; return false; }
                n.text += s.substr(p, e - p);
                p = e + 3;
                continue;
            }
            if (startsWith("</")) {
                p += 2;
                readName();
                skipWs();
                if (p < s.size() && s[p] == '>') ++p;
                return true;
            }
            if (startsWith("<?")) { if (!skipPast("?>")) return false; continue; }
            if (s[p] == '<') {
                n.kids.emplace_back();
                if (!element(n.kids.back(), depth + 1)) return false;
                continue;
            }
            const size_t e = s.find('<', p);
            const size_t end = e == std::string::npos ? s.size() : e;
            n.text += decodeEntities(s.substr(p, end - p), &ents);
            p = end;
        }
    }
};

} // namespace

bool parseXml(const std::string& src, XmlNode& root, std::string* err) {
    XmlParser x(src);
    // BOM UTF-8
    if (src.size() >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) x.p = 3;
    root = XmlNode{};
    bool ok = x.skipMisc() && x.element(root, 0);
    if (!ok && err) *err = x.err.empty() ? "XML no válido" : x.err;
    return ok;
}

// ================================================================== archivos

namespace {

std::wstring userDir() { return appDataDir() + L"\\mandos"; }

std::wstring envDir() {
#ifdef _WIN32
    wchar_t b[1024];
    const DWORD n = GetEnvironmentVariableW(L"RDJ_MANDOS_DIR", b, 1024);
    return n > 0 && n < 1024 ? std::wstring(b, n) : std::wstring();
#else
    const char* v = std::getenv("RDJ_MANDOS_DIR");
    return v ? std::filesystem::path(v).wstring() : std::wstring();
#endif
}

bool readDisk(const std::wstring& dir, const std::string& name, std::string& out) {
    if (dir.empty()) return false;
    std::ifstream f(std::filesystem::path(dir) / std::filesystem::path(fromUtf8(name)), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

void listDisk(const std::wstring& dir, std::vector<std::string>& out) {
    if (dir.empty()) return;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), e; !ec && it != e; it.increment(ec)) {
        std::error_code ec2;
        if (it->is_regular_file(ec2)) out.push_back(toUtf8(it->path().filename().wstring()));
    }
}

#ifdef _WIN32
// Índice de los archivos incluidos: recurso 2999 con líneas "id<TAB>nombre".
constexpr int kIndexResource = 2999;
const std::map<std::string, int>& embeddedIndex() {
    static const std::map<std::string, int> idx = [] {
        std::map<std::string, int> m;
        HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(kIndexResource), MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
        if (!r) return m;
        HGLOBAL g = LoadResource(nullptr, r);
        const char* data = g ? (const char*)LockResource(g) : nullptr;
        const DWORD n = SizeofResource(nullptr, r);
        if (!data) return m;
        std::istringstream is(std::string(data, n));
        std::string line;
        while (std::getline(is, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t t = line.find('\t');
            if (t == std::string::npos) continue;
            m[line.substr(t + 1)] = std::atoi(line.substr(0, t).c_str());
        }
        return m;
    }();
    return idx;
}
bool readEmbedded(const std::string& name, std::string& out) {
    const auto& idx = embeddedIndex();
    auto it = idx.find(name);
    if (it == idx.end()) return false;
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(it->second), MAKEINTRESOURCEW(10));
    if (!r) return false;
    HGLOBAL g = LoadResource(nullptr, r);
    const char* data = g ? (const char*)LockResource(g) : nullptr;
    if (!data) return false;
    out.assign(data, SizeofResource(nullptr, r));
    return true;
}
#else
const std::map<std::string, int>& embeddedIndex() { static const std::map<std::string, int> m; return m; }
bool readEmbedded(const std::string&, std::string&) { return false; }
#endif

bool endsWith(const std::string& s, const char* suf) {
    const size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

} // namespace

bool readFile(const std::string& name, std::string& out) {
    if (name.find("..") != std::string::npos || name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
    return readDisk(userDir(), name, out) || readDisk(envDir(), name, out) || readEmbedded(name, out);
}

std::vector<std::string> listFiles() {
    std::vector<std::string> v;
    listDisk(userDir(), v);
    listDisk(envDir(), v);
    for (auto& kv : embeddedIndex()) v.push_back(kv.first);
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

// ================================================================== mapeo

namespace {

int parseInt(const std::string& t, int def) {
    const std::string s = trimmed(t);
    if (s.empty()) return def;
    char* e = nullptr;
    const long v = std::strtol(s.c_str(), &e, 0);
    if (e == s.c_str()) return def;
    return (int)v;
}

double parseDouble(const std::string& t, double def) {
    const std::string s = trimmed(t);
    if (s.empty()) return def;
    char* e = nullptr;
    const double v = std::strtod(s.c_str(), &e);
    return e == s.c_str() ? def : v;
}

std::string lowerAscii(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

uint32_t parseOptions(const XmlNode* opts) {
    if (!opts) return 0;
    uint32_t o = 0;
    for (auto& k : opts->kids) {
        const std::string n = lowerAscii(k.name);
        if (n == "invert") o |= OptInvert;
        else if (n == "rot64") o |= OptRot64;
        else if (n == "rot64inv") o |= OptRot64Inv;
        else if (n == "rot64fast") o |= OptRot64Fast;
        else if (n == "diff") o |= OptDiff;
        else if (n == "button") o |= OptButton;
        else if (n == "switch") o |= OptSwitch;
        else if (n == "hercjog") o |= OptHercJog;
        else if (n == "hercjogfast") o |= OptHercJogFast;
        else if (n == "spread64") o |= OptSpread64;
        else if (n == "selectknob") o |= OptSelectKnob;
        else if (n == "soft-takeover") o |= OptSoftTakeover;
        else if (n == "script-binding") o |= OptScript;
        else if (n == "fourteen-bit-msb") o |= Opt14Msb;
        else if (n == "fourteen-bit-lsb") o |= Opt14Lsb;
    }
    return o;
}

void collectSettings(const XmlNode& n, std::vector<Setting>& out) {
    for (auto& k : n.kids) {
        if (k.name == "option" && !k.attr("variable").empty()) {
            Setting st;
            st.variable = k.attr("variable");
            st.type = lowerAscii(k.attr("type"));
            st.value = k.attr("default");
            if (st.type == "enum") {
                std::string first;
                for (auto& v : k.kids) {
                    if (v.name != "value") continue;
                    if (first.empty()) first = trimmed(v.text);
                    if (lowerAscii(v.attr("default")) == "true") st.value = trimmed(v.text);
                }
                if (st.value.empty()) st.value = first;
            }
            out.push_back(st);
        } else collectSettings(k, out);
    }
}

} // namespace

bool loadMapping(const std::string& file, Mapping& m, std::string* err) {
    std::string src;
    if (!readFile(file, src)) { if (err) *err = "No se encuentra " + file; return false; }
    XmlNode root;
    if (!parseXml(src, root, err)) return false;
    m = Mapping{};
    m.file = file;
    if (const XmlNode* info = root.child("info")) {
        m.name = info->childText("name");
        m.author = info->childText("author");
        m.description = info->childText("description");
        m.forum = info->childText("forums");
        if (m.forum.empty()) m.forum = info->childText("forum");
    }
    if (const XmlNode* st = root.child("settings")) collectSettings(*st, m.settings);
    const XmlNode* ctl = root.child("controller");
    if (!ctl) { if (err) *err = "Sin <controller>"; return false; }
    m.controllerId = ctl->attr("id");
    if (const XmlNode* sf = ctl->child("scriptfiles"))
        for (auto& f : sf->kids)
            if (f.name == "file" && !f.attr("filename").empty()) m.scripts.push_back({f.attr("filename"), f.attr("functionprefix")});
    if (const XmlNode* cs = ctl->child("controls"))
        for (auto& c : cs->kids) {
            if (c.name != "control") continue;
            InputMap in;
            in.group = c.childText("group");
            in.key = c.childText("key");
            in.status = (uint8_t)parseInt(c.childText("status"), 0);
            in.midino = (uint8_t)parseInt(c.childText("midino"), 0);
            in.opts = parseOptions(c.child("options"));
            if (in.status == 0xF0) in.opts |= OptScript;
            m.inputs.push_back(in);
        }
    if (const XmlNode* os = ctl->child("outputs"))
        for (auto& c : os->kids) {
            if (c.name != "output") continue;
            OutputMap o;
            o.group = c.childText("group");
            o.key = c.childText("key");
            o.status = (uint8_t)parseInt(c.childText("status"), 0);
            o.midino = (uint8_t)parseInt(c.childText("midino"), 0);
            o.on = (uint8_t)parseInt(c.childText("on"), 0x7F);
            o.off = (uint8_t)parseInt(c.childText("off"), 0x00);
            o.minimum = parseDouble(c.childText("minimum"), 0.0);
            o.maximum = parseDouble(c.childText("maximum"), 1.0);
            m.outputs.push_back(o);
        }
    return true;
}

const std::vector<CatalogEntry>& catalog() {
    static std::mutex mu;
    static std::vector<CatalogEntry> cat;
    static bool built = false;
    std::lock_guard<std::mutex> lk(mu);
    if (built) return cat;
    built = true;
    for (auto& f : listFiles()) {
        if (!endsWith(f, ".midi.xml")) continue;
        std::string src;
        if (!readFile(f, src)) continue;
        // Solo la cabecera <info> (rápido: no hace falta el mapeo entero).
        CatalogEntry e;
        e.file = f;
        const size_t a = src.find("<info>"), b = src.find("</info>");
        if (a != std::string::npos && b != std::string::npos && b > a) {
            XmlNode info;
            if (parseXml(src.substr(a, b - a + 7), info)) {
                e.name = info.childText("name");
                e.author = info.childText("author");
                e.description = info.childText("description");
            }
        }
        if (e.name.empty()) e.name = f.substr(0, f.size() - 9);
        const std::string lf = lowerAscii(f);
        e.generic = lf.find("midi-keyboard") != std::string::npos || lf.find("midi_for_light") != std::string::npos ||
                    lf.find("wireless dj") != std::string::npos || lf.find("trakprodj") != std::string::npos ||
                    lf.find("miditech") != std::string::npos || lf.find("joymidi") != std::string::npos ||
                    lf.find("dummy") != std::string::npos;
        cat.push_back(e);
    }
    std::sort(cat.begin(), cat.end(), [](const CatalogEntry& x, const CatalogEntry& y) { return lowerAscii(x.name) < lowerAscii(y.name); });
    return cat;
}

// ================================================================== reconocimiento

namespace {

// Palabras que no distinguen un mando de otro (marcas y relleno).
bool isFiller(const std::string& t) {
    static const char* const w[] = {"pioneer", "hercules", "numark", "denon", "reloop", "behringer", "roland", "vestax", "stanton", "gemini",
                                    "korg", "akai", "novation", "american", "audio", "allen", "and", "heath", "native", "instruments", "ni",
                                    "mixvibes", "icon", "ion", "dj", "tech", "m", "arturia", "electrix", "evolution", "faderfox", "midi",
                                    "controller", "usb", "device", "port", "in", "out", "input", "output", "the", "edition", "for",
                                    "linux", "windows", "os", "x", "mac", "macos", "scripts", "script", "mapping", "djtechtools", "kane",
                                    "intech", "kontrol", "soundless", "techtools", "djcontrol", "mvave", "yaeltex", "dn", "enhanced", "alternate", "rk"};
    for (const char* x : w) if (t == x) return true;
    return false;
}

std::vector<std::string> tokens(std::string s) {
    s = lowerAscii(s);
    // "2- DDJ-400", "MIDIIN2 (DDJ-400)", "DDJ-400 MIDI 1": se quitan los añadidos de Windows.
    for (const char* pre : {"midiin", "midiout"}) {
        size_t p;
        while ((p = s.find(pre)) != std::string::npos) {
            size_t e = p + std::strlen(pre);
            while (e < s.size() && std::isdigit((unsigned char)s[e])) ++e;
            s.erase(p, e - p);
        }
    }
    {
        size_t i = 0;
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        size_t j = i;
        while (j < s.size() && std::isdigit((unsigned char)s[j])) ++j;
        if (j > i && j < s.size() && s[j] == '-') s.erase(0, j + 1);
    }
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        if (cur.empty()) return;
        // Variantes de "MK2": mkii -> mk2
        if (cur == "mkii") cur = "mk2";
        else if (cur == "mkiii") cur = "mk3";
        else if (cur == "ii") cur = "2";
        out.push_back(cur);
        cur.clear();
    };
    for (char c : s) {
        if (std::isalnum((unsigned char)c)) cur += c;
        else flush();
    }
    flush();
    // "mk" + "2" -> "mk2"
    for (size_t i = 0; i + 1 < out.size(); ++i)
        if (out[i] == "mk" && std::isdigit((unsigned char)out[i + 1][0])) { out[i] = "mk" + out[i + 1]; out.erase(out.begin() + (long)i + 1); }
    return out;
}

std::string joinTokens(const std::vector<std::string>& t) {
    std::string s;
    for (auto& x : t) s += x;
    return s;
}

bool hasDigit(const std::string& t) {
    for (char c : t) if (std::isdigit((unsigned char)c)) return true;
    return false;
}

} // namespace

int matchScore(const std::string& portName, const CatalogEntry& e) {
    std::vector<std::string> port = tokens(portName);
    std::vector<std::string> portKey;
    for (auto& t : port) if (!isFiller(t)) portKey.push_back(t);
    if (portKey.empty()) return 0;
    const std::string portJoined = joinTokens(portKey);
    int best = 0;
    // Se prueba con el nombre del mapeo y con el del archivo.
    for (const std::string& cand : {e.name, e.file.substr(0, e.file.size() > 9 ? e.file.size() - 9 : e.file.size())}) {
        std::vector<std::string> mk;
        for (auto& t : tokens(cand)) if (!isFiller(t)) mk.push_back(t);
        if (mk.empty()) continue;
        int score = 0;
        bool all = true;
        size_t usedChars = 0;
        std::vector<bool> portUsed(portKey.size(), false);
        for (auto& t : mk) {
            bool found = false;
            for (size_t i = 0; i < portKey.size(); ++i)
                if (!portUsed[i] && portKey[i] == t) { portUsed[i] = found = true; break; }
            if (!found) {
                // "ddj400" frente a "ddj" "400", o "inpulse200" frente a "inpulse" "200"
                if (portJoined.find(t) != std::string::npos) found = true;
            }
            if (!found) { all = false; break; }
            score += (int)t.size() + (hasDigit(t) ? 12 : 0);
            usedChars += t.size();
        }
        if (!all) {
            // Nombre compacto: "DDJ-FLX4" frente a "ddjflx4"
            const std::string mj = joinTokens(mk);
            if (mj.size() >= 4 && portJoined.find(mj) != std::string::npos) { score = (int)mj.size() + (hasDigit(mj) ? 12 : 0); usedChars = mj.size(); all = true; }
        }
        if (!all) continue;
        // Cifras del puerto que el mapeo no menciona: probablemente es otro modelo (Mixtrack Pro frente a Mixtrack Pro 3).
        const std::string mjoined = joinTokens(mk);
        for (size_t i = 0; i < portKey.size(); ++i)
            if (!portUsed[i] && hasDigit(portKey[i]) && mjoined.find(portKey[i]) == std::string::npos) score -= 15;
        // Lo que sobra del puerto resta un poco: gana el mapeo más concreto.
        score -= (int)(portJoined.size() > usedChars ? (portJoined.size() - usedChars) / 4 : 0);
        best = std::max(best, score);
    }
    return best;
}

std::string bestMatch(const std::string& portName, int* scoreOut) {
    int best = 0;
    std::string file;
    for (auto& e : catalog()) {
        if (e.generic) continue;
        const int sc = matchScore(portName, e);
        if (sc > best) { best = sc; file = e.file; }
    }
    // Un parecido mínimo (al menos 5 letras del modelo: "ddj sb", "mixage", "party mix").
    if (best < 5) { best = 0; file.clear(); }
    if (scoreOut) *scoreOut = best;
    return file;
}


const std::vector<UniversalControllerModel>& universalCatalog() {
    static std::mutex uMu;
    static std::vector<UniversalControllerModel> uCat;
    static bool uBuilt = false;
    std::lock_guard<std::mutex> lk(uMu);
    if (uBuilt) return uCat;
    uBuilt = true;

    struct BrandSeed { const char* brand; const char* models[64]; };
    static const BrandSeed kSeeds[] = {
        {"Pioneer DJ / AlphaTheta", {
            "DDJ-WeGO", "DDJ-WeGO2", "DDJ-WeGO3", "DDJ-WeGO4", "DDJ-200", "DDJ-400", "DDJ-FLX2", "DDJ-FLX4",
            "DDJ-FLX6", "DDJ-FLX6-GT", "DDJ-FLX10", "DDJ-REV1", "DDJ-REV5", "DDJ-REV7", "DDJ-SB", "DDJ-SB2",
            "DDJ-SB3", "DDJ-RB", "DDJ-RR", "DDJ-RX", "DDJ-RZ", "DDJ-RZX", "DDJ-SR", "DDJ-SR2", "DDJ-SX",
            "DDJ-SX2", "DDJ-SX3", "DDJ-SZ", "DDJ-SZ2", "DDJ-800", "DDJ-1000", "DDJ-1000SRT", "DDJ-ERGO",
            "DDJ-T1", "DDJ-S1", "DDJ-SP1", "DDJ-XP1", "DDJ-XP2", "XDJ-R1", "XDJ-AERO", "XDJ-RX", "XDJ-RX2",
            "XDJ-RX3", "XDJ-RR", "XDJ-XZ", "XDJ-AZ", "OPUS-QUAD", "OMNIS-DUO", "CDJ-350", "CDJ-400",
            "CDJ-850", "CDJ-900", "CDJ-900NXS", "CDJ-2000", "CDJ-2000NXS", "CDJ-2000NXS2", "CDJ-3000",
            "XDJ-700", "XDJ-1000", "XDJ-1000MK2", nullptr
        }},
        {"Denon DJ", {
            "MC2000", "MC3000", "MC4000", "MC6000", "MC6000MK2", "MC7000", "MCX8000", "Prime GO", "Prime GO+",
            "Prime 2", "Prime 4", "Prime 4+", "SC Live 2", "SC Live 4", "SC5000 Prime", "SC5000M Prime",
            "SC6000 Prime", "SC6000M Prime", "LC6000 Prime", "DN-S1200", "DN-S3700", "DN-S3500", "DN-HC4500",
            "DN-HC1000S", "DN-MC6000", "DN-X1600", "DN-X1700", "X1800 Prime", "X1850 Prime", "DN-SC2000",
            "DN-SC2900", "DN-SC3900", "Axis 12", "Envio", "Prime Deck", nullptr
        }},
        {"Numark", {
            "Mixtrack", "Mixtrack II", "Mixtrack 3", "Mixtrack Pro", "Mixtrack Pro II", "Mixtrack Pro 3",
            "Mixtrack Platinum", "Mixtrack Platinum FX", "Mixtrack Pro FX", "Mixtrack Quad", "Mixtrack Edge",
            "Party Mix", "Party Mix II", "Party Mix Live", "Party Mix MKII", "DJ2GO", "DJ2GO2", "DJ2GO2 Touch",
            "Mixstream Pro", "Mixstream Pro+", "Mixstream Pro Go", "NS4FX", "NV", "NVII", "NS6", "NS6II",
            "NS7", "NS7II", "NS7III", "4Trak", "N4", "iDJ3", "iDJ Pro", "iDJ Live", "iDJ Live II",
            "Total Control", "Stealth Control", "Omni Control", "Mixdeck", "Mixdeck Express", "Mixdeck Quad",
            "V7", "NDX400", "NDX500", "NDX800", "NDX900", "Orbit", "Scratch", nullptr
        }},
        {"Hercules", {
            "DJControl Inpulse 200", "DJControl Inpulse 200 MK2", "DJControl Inpulse 300", "DJControl Inpulse 300 MK2",
            "DJControl Inpulse 500", "DJControl Inpulse T7", "DJControl Starlight", "DJControl Mix", "DJControl Mix Ultra",
            "DJControl Jogvision", "DJControl Air", "DJControl Air+", "DJControl Air+ S", "DJControl Instinct",
            "DJControl Instinct P8", "DJControl Compact", "DJControl MP3 e2", "DJControl MP3 LE", "DJControl Glow",
            "DJControl Wave", "DJ Console RMX", "DJ Console RMX2", "DJ Console 4-Mx", "DJ Console MK2",
            "DJ Console MK4", "DJ Control Steel", "P32 DJ", "Universal DJ", "DJControl Pro", nullptr
        }},
        {"Native Instruments", {
            "Traktor Kontrol S2 MK1", "Traktor Kontrol S2 MK2", "Traktor Kontrol S2 MK3", "Traktor Kontrol S3",
            "Traktor Kontrol S4 MK1", "Traktor Kontrol S4 MK2", "Traktor Kontrol S4 MK3", "Traktor Kontrol S5",
            "Traktor Kontrol S8", "Traktor Kontrol Z1", "Traktor Kontrol Z1 MK2", "Traktor Kontrol Z2",
            "Traktor Kontrol X1 MK1", "Traktor Kontrol X1 MK2", "Traktor Kontrol X1 MK3", "Traktor Kontrol F1",
            "Traktor Kontrol D2", "Maschine Mikro MK3", "Maschine MK3", "Maschine Jam", "Audio 2 DJ", nullptr
        }},
        {"Reloop", {
            "Beatmix", "Beatmix 2", "Beatmix 2 MK2", "Beatmix 4", "Beatmix 4 MK2", "Mixon 4", "Mixon 8 Pro",
            "Buddy", "Ready", "Terminal Mix 2", "Terminal Mix 4", "Terminal Mix 8", "Digital Jockey 2",
            "Digital Jockey 2 IE", "Digital Jockey 2 ME", "Digital Jockey 3 ME", "Jockey 3 Remix", "Touch",
            "Neon", "Contour", "Mixage IE", "Mixage MK2", "Keypad", "Keypad Pro", "Stand Hub Pro",
            "RMX-95", "RMX-90 DVS", "RMX-60", "Elite", "RP-8000 MK2", nullptr
        }},
        {"Roland", {
            "DJ-202", "DJ-505", "DJ-707M", "DJ-808", "DJ-99", "SP-404MKII", "SP-404SX", "SP-404A",
            "TR-8S", "TR-8", "TR-6S", "MX-1", "Aira Compact E-4", "Aira Compact T-8", "MV-1 Verselab", nullptr
        }},
        {"Allen & Heath", {
            "Xone:K1", "Xone:K2", "Xone:DX", "Xone:4D", "Xone:3D", "Xone:2D", "Xone:1D", "Xone:DB2",
            "Xone:DB4", "Xone:PX5", "Xone:96", "Xone:92", "Xone:92 MK2", "Xone:43C", "Xone:23C", nullptr
        }},
        {"Akai Professional", {
            "AMX", "AFX", "APC40", "APC40 MKII", "APC Mini", "APC Mini MK2", "APC Key 25", "APC Key 25 MK2",
            "APC20", "LPD8", "LPD8 MK2", "MPD218", "MPD226", "MPD232", "MPD18", "MPD24", "MPD32",
            "Midimix", "MPK Mini MK3", "MPK Mini Plus", "Fire", "Force", "MPC One+", "MPC Live II", nullptr
        }},
        {"Behringer", {
            "CMD Studio 2A", "CMD Studio 4A", "CMD PL-1", "CMD MM-1", "CMD DC-1", "CMD DV-1", "CMD LC-1",
            "CMD Micro", "BCD2000", "BCD3000", "DDM4000", "DJX900USB", "DJX750", "NOX606", "BCR2000",
            "BCF2000", "X-Touch Mini", "X-Touch Compact", "UMA25S", nullptr
        }},
        {"Vestax", {
            "VCI-100", "VCI-100 MKII", "VCI-300", "VCI-300 MKII", "VCI-380", "VCI-400", "VCI-400DJ",
            "Typhoon", "Spin", "Spin 2", "TR-1", "VFX-1", "VCM-100", "VCM-600", "Pad-One", "PMC-05ProIV", nullptr
        }}
    };

    const auto& emb = catalog();
    std::set<std::string> seen;

    for (const auto& bs : kSeeds) {
        for (int i = 0; i < 64 && bs.models[i] != nullptr; ++i) {
            UniversalControllerModel m;
            m.brand = bs.brand;
            m.model = bs.models[i];
            const std::string full = std::string(bs.brand) + " " + m.model;
            std::string matchedXml = bestMatch(full);
            if (matchedXml.empty()) matchedXml = bestMatch(m.model);
            if (m.model.find("WeGO") != std::string::npos) {
                m.validationStatus = "Verificado Hardware (Preajuste Nativo + 14-bit)";
                m.mappingFile = "preset:wego";
                m.protocol = "MIDI USB 14-bit + LEDs";
            } else if (!matchedXml.empty()) {
                m.validationStatus = "Mapeo Nativo Mixxx XML Incluido";
                m.mappingFile = matchedXml;
                m.protocol = "MIDI USB + Script JS";
            } else {
                m.validationStatus = "Perfil MIDI Estándar Auto-Adaptativo + Learn";
                m.mappingFile = "auto:standard";
                m.protocol = "MIDI USB / HID Estándar";
            }
            m.decks = (m.model.find("4") != std::string::npos || m.model.find("SZ") != std::string::npos ||
                       m.model.find("RZ") != std::string::npos || m.model.find("FLX10") != std::string::npos ||
                       m.model.find("FLX6") != std::string::npos || m.model.find("1000") != std::string::npos ||
                       m.model.find("S8") != std::string::npos || m.model.find("Mixon") != std::string::npos) ? 4 : 2;
            uCat.push_back(m);
            seen.insert(lowerAscii(m.model));
        }
    }

    // Añadir también todos los mapeos XML de Mixxx embebidos que no estuvieran ya en la semilla
    for (const auto& ce : emb) {
        if (ce.generic) continue;
        std::string lowN = lowerAscii(ce.name);
        bool already = false;
        for (const auto& s : seen) if (lowN.find(s) != std::string::npos) { already = true; break; }
        if (already) continue;
        UniversalControllerModel m;
        m.brand = "Mixxx Open Library";
        for (const char* b : {"Pioneer", "Denon", "Numark", "Hercules", "Traktor", "Reloop", "Roland", "Allen", "Akai", "Behringer", "Vestax", "Stanton", "Korg", "Novation", "Gemini"}) {
            if (lowN.find(lowerAscii(b)) != std::string::npos) { m.brand = b; break; }
        }
        m.model = ce.name;
        m.protocol = "MIDI USB + Script JS";
        m.validationStatus = "Mapeo Nativo Mixxx XML Incluido";
        m.mappingFile = ce.file;
        uCat.push_back(m);
    }
    return uCat;
}

const UniversalControllerModel* identifyController(const std::string& portName) {
    if (portName.empty()) return nullptr;
    const std::string lp = lowerAscii(portName);
    const auto& uCat = universalCatalog();
    const UniversalControllerModel* best = nullptr;
    size_t bestLen = 0;
    for (const auto& m : uCat) {
        const std::string lm = lowerAscii(m.model);
        if (lm.size() >= 4 && lp.find(lm) != std::string::npos && lm.size() > bestLen) {
            best = &m;
            bestLen = lm.size();
        }
    }
    if (best) return best;
    const std::string xml = bestMatch(portName);
    if (!xml.empty()) {
        for (const auto& m : uCat) if (m.mappingFile == xml) return &m;
    }
    return nullptr;
}

} // namespace rdj::mandos
