#include "persistence.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

void writeDocument(std::ostream& o, const Document& doc);
bool readDocumentLine(const std::string& key, std::istringstream& in, const std::string& line, Document& doc);

std::string worldsFolder() {
    fs::path p = fs::current_path() / "worlds";
    std::error_code ec; fs::create_directories(p, ec);
    return p.string();
}
std::string exportsFolder() {
    fs::path p = fs::current_path() / "exports";
    std::error_code ec; fs::create_directories(p, ec);
    return p.string();
}

std::string quote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') continue;
        else o += c;
    }
    return o + "\"";
}

bool unquote(const std::string& line, size_t& pos, std::string& out) {
    out.clear();
    while (pos < line.size() && line[pos] == ' ') pos++;
    if (pos >= line.size() || line[pos] != '"') return false;
    pos++;
    while (pos < line.size()) {
        char c = line[pos++];
        if (c == '"') return true;
        if (c == '\\' && pos < line.size()) {
            char n = line[pos++];
            out += n == 'n' ? '\n' : n;
        } else out += c;
    }
    return false;
}

bool saveWorld(const std::string& path, const SaveData& d, std::string& err) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream o(tmp);
        if (!o) { err = "Couldn't write " + path; return false; }
        o.precision(10);
        const PlanetSettings& s = d.settings;
        o << "WORLDBUILDER 1\n";
        o << "seed " << s.seed << "\n" << "radius " << s.radiusKm << "\n" << "ocean " << s.oceanPercent << "\n"
          << "mountains " << s.mountains << "\n" << "continents " << s.continentSize << "\n" << "plates " << s.plateCount << "\n"
          << "activity " << s.tectonicActivity << "\n" << "tilt " << s.axialTilt << "\n"
          << "climate " << s.tempOffset << " " << s.rainMultiplier << " " << s.seaLevel << "\n"
          << "erosion " << s.erosion << "\n";
        if (!s.importPath.empty()) o << "import " << quote(s.importPath) << "\n";
        if (s.driftMa > 0) o << "drift " << s.driftMa << "\n";
        for (const PlateEdit& e : s.plateEdits)
            o << "plateedit " << e.plate << " " << (e.moveSeed ? 1 : 0) << " " << e.seed.x << " " << e.seed.y << " " << e.seed.z << " "
              << (e.moveMotion ? 1 : 0) << " " << e.pole.x << " " << e.pole.y << " " << e.pole.z << " " << e.speed << " " << e.type << " " << (e.removed ? 1 : 0) << "\n";
        o << "view " << d.viewLon << " " << d.viewLat << " " << d.viewZoom << " " << d.style << "\n";
        for (const Stroke& st : d.strokes) {
            o << "stroke " << (int)st.type << " " << st.dir.x << " " << st.dir.y << " " << st.dir.z << " " << st.radiusKm << " "
              << st.amount << " " << st.targetM << " " << st.group << " " << st.hardness << " " << st.noise << "\n";
            if (st.poly) {
                o << "strokepoly " << st.radiusKm << " " << st.e1.x << " " << st.e1.y << " " << st.e1.z << " " << st.e2.x << " " << st.e2.y << " " << st.e2.z
                  << " " << st.poly->size();
                for (double v : *st.poly) o << " " << v;
                o << "\n";
            }
        }
        writeDocument(o, d.doc);
        o << "END\n";
        if (!o) { err = "Error while writing " + path; return false; }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);   // replace the old file only once the new one is complete
    if (ec) { fs::remove(path, ec); fs::rename(tmp, path, ec); }
    if (ec) { err = "Couldn't replace " + path; return false; }
    return true;
}

bool loadWorld(const std::string& path, SaveData& d, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "Couldn't open " + path; return false; }
    std::string line;
    if (!std::getline(in, line) || line.rfind("WORLDBUILDER", 0) != 0) { err = "Not a Worldbuilder file"; return false; }
    d = SaveData{};
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string key; ls >> key;
        PlanetSettings& s = d.settings;
        if (key == "seed") ls >> s.seed;
        else if (key == "radius") ls >> s.radiusKm;
        else if (key == "ocean") ls >> s.oceanPercent;
        else if (key == "mountains") ls >> s.mountains;
        else if (key == "continents") ls >> s.continentSize;
        else if (key == "plates") ls >> s.plateCount;
        else if (key == "activity") ls >> s.tectonicActivity;
        else if (key == "tilt") ls >> s.axialTilt;
        else if (key == "climate") ls >> s.tempOffset >> s.rainMultiplier >> s.seaLevel;
        else if (key == "erosion") ls >> s.erosion;
        else if (key == "drift") ls >> s.driftMa;
        else if (key == "plateedit") {
            PlateEdit e; int ms, mm, rm;
            ls >> e.plate >> ms >> e.seed.x >> e.seed.y >> e.seed.z >> mm >> e.pole.x >> e.pole.y >> e.pole.z >> e.speed >> e.type >> rm;
            e.moveSeed = ms != 0; e.moveMotion = mm != 0; e.removed = rm != 0;
            if (ls) s.plateEdits.push_back(e);
        }
        else if (key == "import") { size_t pos = (size_t)ls.tellg(); if (pos != std::string::npos) unquote(line, pos, s.importPath); }
        else if (key == "view") ls >> d.viewLon >> d.viewLat >> d.viewZoom >> d.style;
        else if (key == "stroke") {
            Stroke st{}; int t;
            ls >> t >> st.dir.x >> st.dir.y >> st.dir.z >> st.radiusKm >> st.amount >> st.targetM >> st.group;
            st.type = (BrushType)t;
            bool okLine = (bool)ls;
            double hd, nz;
            if (ls >> hd >> nz) { st.hardness = hd; st.noise = nz; }   // newer files
            ls.clear();
            if (!okLine) ls.setstate(std::ios::failbit);
            st.dir = st.dir.normalized();
            st.angRadius = std::min(kPi * 0.95, st.radiusKm / std::max(1.0, (double)s.radiusKm));
            st.cosRadius = std::cos(st.angRadius);
            if (ls) d.strokes.push_back(st);
        } else if (key == "strokepoly" && !d.strokes.empty()) {
            Stroke& st = d.strokes.back();
            size_t n = 0; double rk;
            ls >> rk >> st.e1.x >> st.e1.y >> st.e1.z >> st.e2.x >> st.e2.y >> st.e2.z >> n;
            auto v = std::make_shared<std::vector<double>>();
            for (size_t k = 0; k < n && ls; k++) { double x; ls >> x; v->push_back(x); }
            if (ls && v->size() >= 8) {
                st.poly = v; st.radiusKm = rk;
                st.angRadius = std::min(kPi * 0.95, rk / std::max(1.0, (double)s.radiusKm));
                st.cosRadius = std::cos(st.angRadius);
            }
        } else if (key == "END") break;
        else readDocumentLine(key, ls, line, d.doc);   // unknown keys are ignored, for forward compatibility
    }
    return true;
}

std::vector<std::string> listSavedWorlds() {
    std::vector<std::pair<fs::file_time_type, std::string>> files;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(worldsFolder(), ec)) {
        if (e.path().extension() == ".world") files.push_back({e.last_write_time(ec), e.path().stem().string()});
    }
    std::sort(files.begin(), files.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<std::string> out;
    for (auto& f : files) out.push_back(f.second);
    return out;
}
