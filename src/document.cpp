#include "document.h"
#include "persistence.h"
#include <sstream>

const char* kGovernments[] = {"Kingdom", "Empire", "Republic", "Theocracy", "Tribal confederation", "Merchant league",
                              "Dictatorship", "Democracy", "City-state", "Magocracy"};
const int kGovernmentCount = 10;

const char* eventTypeName(EventType t) {
    static const char* n[] = {"Note", "Meteor impact", "Volcanic eruption", "Upheaval (land rises)", "Sinking (land falls)",
                              "Founding", "Fall / destruction", "War", "Treaty", "Disaster", "Magical event"};
    int i = (int)t;
    return i >= 0 && i < (int)EventType::Count ? n[i] : "?";
}
bool eventChangesTerrain(EventType t) {
    return t == EventType::Meteor || t == EventType::Eruption || t == EventType::Upheaval || t == EventType::Sinking || t == EventType::Magic;
}
const char* loreKindName(LoreKind k) {
    static const char* n[] = {"Place", "Person", "Faction", "Religion", "Creature", "Item", "Event", "Other"};
    int i = (int)k;
    return i >= 0 && i < (int)LoreKind::Count ? n[i] : "?";
}

void Document::removeNation(int n) {
    if (n < 0 || n >= (int)nations.size()) return;
    nations.erase(nations.begin() + n);
    for (auto& o : owner) {
        int v = (int)o - 1;
        if (v == n) o = 0;
        else if (v > n) o = (uint16_t)v;   // shift down by one (v is index, stored as index+1)
    }
    for (auto& c : cities) {
        if (c.nation == n) { c.nation = -1; c.capital = false; }
        else if (c.nation > n) c.nation--;
    }
}

// ---- saving ----
void writeDocument(std::ostream& o, const Document& doc) {
    for (const Culture& c : doc.cultures)
        o << "culture " << quote(c.name) << " " << (int)c.style << " " << (int)c.r << " " << (int)c.g << " " << (int)c.b << " " << quote(c.notes) << "\n";
    for (const Nation& n : doc.nations)
        o << "nation " << quote(n.name) << " " << (int)n.r << " " << (int)n.g << " " << (int)n.b << " " << n.culture << " " << n.government << " "
          << quote(n.notes) << "\n";
    for (const City& c : doc.cities)
        o << "city " << quote(c.name) << " " << c.lat << " " << c.lon << " " << c.population << " " << c.nation << " " << c.culture << " "
          << (c.capital ? 1 : 0) << " " << c.founded << " " << quote(c.notes) << " " << c.destroyed << "\n";
    o << "era " << doc.era << "\n";
    o << "years " << doc.currentYear << " " << doc.yearMin << " " << doc.yearMax << "\n";
    for (const HistEvent& e : doc.events)
        o << "event " << e.year << " " << (int)e.type << " " << e.lat << " " << e.lon << " " << e.radiusKm << " " << e.strength << " "
          << quote(e.title) << " " << quote(e.text) << "\n";
    for (const Article& a : doc.articles)
        o << "article " << (int)a.kind << " " << (a.pinned ? 1 : 0) << " " << a.lat << " " << a.lon << " " << quote(a.title) << " " << quote(a.body) << "\n";
    for (auto& r : doc.customRoutes) o << "route " << r.first << " " << r.second << "\n";
    // territory, run-length encoded row by row
    if (!doc.owner.empty()) {
        const int W = 720, H = (int)doc.owner.size() / W;
        for (int y = 0; y < H; y++) {
            bool any = false;
            for (int x = 0; x < W; x++) if (doc.owner[y * W + x]) { any = true; break; }
            if (!any) continue;
            o << "owner " << y;
            int x = 0;
            while (x < W) {
                uint16_t v = doc.owner[y * W + x];
                int run = 1;
                while (x + run < W && doc.owner[y * W + x + run] == v) run++;
                o << " " << run << " " << v;
                x += run;
            }
            o << "\n";
        }
    }
}

bool readDocumentLine(const std::string& key, std::istringstream& in, const std::string& line, Document& doc) {
    auto rest = [&](size_t& pos) { pos = (size_t)in.tellg(); if (pos == std::string::npos) pos = line.size(); };
    if (key == "culture") {
        Culture c; size_t pos; rest(pos);
        if (!unquote(line, pos, c.name)) return false;
        std::istringstream r(line.substr(pos));
        int st, cr, cg, cb; r >> st >> cr >> cg >> cb;
        c.style = (NameStyle)st; c.r = (unsigned char)cr; c.g = (unsigned char)cg; c.b = (unsigned char)cb;
        size_t p2 = pos + (size_t)r.tellg(); if ((size_t)r.tellg() == std::string::npos) p2 = line.size();
        unquote(line, p2, c.notes);
        doc.cultures.push_back(c);
        return true;
    }
    if (key == "nation") {
        Nation n; size_t pos; rest(pos);
        if (!unquote(line, pos, n.name)) return false;
        std::istringstream r(line.substr(pos));
        int cr, cg, cb; r >> cr >> cg >> cb >> n.culture >> n.government;
        n.r = (unsigned char)cr; n.g = (unsigned char)cg; n.b = (unsigned char)cb;
        size_t p2 = pos + (size_t)r.tellg(); if ((size_t)r.tellg() == std::string::npos) p2 = line.size();
        unquote(line, p2, n.notes);
        doc.nations.push_back(n);
        return true;
    }
    if (key == "city") {
        City c; size_t pos; rest(pos);
        if (!unquote(line, pos, c.name)) return false;
        std::istringstream r(line.substr(pos));
        int cap; r >> c.lat >> c.lon >> c.population >> c.nation >> c.culture >> cap >> c.founded;
        c.capital = cap != 0;
        size_t p2 = pos + (size_t)r.tellg(); if ((size_t)r.tellg() == std::string::npos) p2 = line.size();
        unquote(line, p2, c.notes);
        if (p2 < line.size()) { std::istringstream r2(line.substr(p2)); int d; if (r2 >> d) c.destroyed = d; }
        doc.cities.push_back(c);
        return true;
    }
    if (key == "era") { in >> doc.era; return true; }
    if (key == "years") { in >> doc.currentYear >> doc.yearMin >> doc.yearMax; return true; }
    if (key == "event") {
        HistEvent e; int t;
        in >> e.year >> t >> e.lat >> e.lon >> e.radiusKm >> e.strength;
        e.type = (EventType)t;
        size_t pos = (size_t)in.tellg(); if (pos == std::string::npos) return false;
        if (!unquote(line, pos, e.title)) return false;
        unquote(line, pos, e.text);
        doc.events.push_back(e);
        return true;
    }
    if (key == "article") {
        Article a; int k, pin;
        in >> k >> pin >> a.lat >> a.lon;
        a.kind = (LoreKind)k; a.pinned = pin != 0;
        size_t pos = (size_t)in.tellg(); if (pos == std::string::npos) return false;
        if (!unquote(line, pos, a.title)) return false;
        unquote(line, pos, a.body);
        doc.articles.push_back(a);
        return true;
    }
    if (key == "route") { int a, b; if (in >> a >> b) doc.customRoutes.push_back({a, b}); return true; }
    if (key == "owner") {
        const int W = 720, H = 360;
        doc.ensureOwner(W * H);
        int y; in >> y;
        if (y < 0 || y >= H) return false;
        int x = 0, run, v;
        while (x < W && (in >> run >> v)) {
            for (int k = 0; k < run && x < W; k++, x++) doc.owner[y * W + x] = (uint16_t)v;
        }
        return true;
    }
    return false;
}
