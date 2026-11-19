#include "history.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace {
Vec3d dirOf(double lat, double lon) {
    double la = lat * kPi / 180, lo = lon * kPi / 180;
    return {std::cos(la) * std::sin(lo), std::sin(la), std::cos(la) * std::cos(lo)};
}
Stroke mk(BrushType t, const Vec3d& d, double rKm, double amount, double R, double target = 0) {
    Stroke s{};
    s.dir = d.normalized(); s.radiusKm = rKm; s.type = t; s.amount = amount; s.targetM = target; s.group = -1;
    s.angRadius = std::min(kPi * 0.95, rKm / std::max(1.0, R));
    s.cosRadius = std::cos(s.angRadius);
    return s;
}
uint32_t rng(uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }
double rnd01(uint32_t& s) { return rng(s) / 16777216.0; }
}

std::vector<Stroke> buildEventStrokes(const Document& doc, int year, double R) {
    std::vector<Stroke> out;
    std::vector<const HistEvent*> ev;
    for (const HistEvent& e : doc.events) if (e.year <= year && eventChangesTerrain(e.type)) ev.push_back(&e);
    std::sort(ev.begin(), ev.end(), [](auto* a, auto* b) { return a->year < b->year; });
    for (const HistEvent* e : ev) {
        Vec3d c = dirOf(e->lat, e->lon);
        double r = std::max(0.2, e->radiusKm), k = e->strength;
        Vec3d east = Vec3d{0, 1, 0}.cross(c).normalized();
        if (east.length() < 0.5) east = {1, 0, 0};
        Vec3d north = c.cross(east);
        switch (e->type) {
            case EventType::Meteor: {
                // bowl, raised rim, and a central peak for big craters
                out.push_back(mk(BrushType::Lower, c, r * 0.95, 900 * k * std::min(3.0, r / 10.0 + 0.5), R));
                const int n = 24;
                for (int i = 0; i < n; i++) {
                    double a = 2 * kPi * i / n, ang = r / R;
                    Vec3d p = (c * std::cos(ang) + (east * std::cos(a) + north * std::sin(a)) * std::sin(ang)).normalized();
                    out.push_back(mk(BrushType::Raise, p, r * 0.35, 260 * k * std::min(3.0, r / 10.0 + 0.5), R));
                }
                if (r > 15) out.push_back(mk(BrushType::Raise, c, r * 0.18, 350 * k, R));
                break;
            }
            case EventType::Eruption:
                out.push_back(mk(BrushType::Raise, c, r, 2200 * k, R));
                out.push_back(mk(BrushType::Lower, c, r * 0.18, 500 * k, R));   // summit crater
                break;
            case EventType::Upheaval: out.push_back(mk(BrushType::Raise, c, r, 1500 * k, R)); break;
            case EventType::Sinking: out.push_back(mk(BrushType::Lower, c, r, 1500 * k, R)); break;
            case EventType::Magic:
                out.push_back(mk(BrushType::Raise, c, r * 0.6, 1800 * k, R));
                out.push_back(mk(BrushType::Lower, c, r * 0.25, 2600 * k, R));   // a strange chasm in the middle
                break;
            default: break;
        }
    }
    return out;
}

void generateHistory(Document& doc, const World& w, uint32_t seed) {
    uint32_t s = seed * 2246822519u + 77u;
    doc.events.clear();
    doc.presentYear = doc.currentYear;
    int y0 = doc.yearMin, y1 = doc.currentYear;
    if (y1 <= y0) y1 = y0 + 1000;
    auto year = [&](double t) { return y0 + (int)std::round((y1 - y0) * t); };
    // City foundings (oldest first); capitals tend to be oldest
    for (City& c : doc.cities) {
        double t = c.capital ? rnd01(s) * 0.35 : 0.15 + rnd01(s) * 0.8;
        c.founded = year(t);
        c.destroyed = 99999;
    }
    for (size_t i = 0; i < doc.cities.size(); i++) {
        const City& c = doc.cities[i];
        if (!c.capital && c.population < 150000) continue;
        HistEvent e; e.type = EventType::Founding; e.year = c.founded; e.lat = c.lat; e.lon = c.lon; e.radiusKm = 20;
        e.title = "Founding of " + c.name;
        e.text = c.capital ? "Founded as the seat of power of " + (c.nation >= 0 && c.nation < (int)doc.nations.size() ? doc.nations[c.nation].name : std::string("its people")) + "." : "A settlement grows into a city.";
        doc.events.push_back(e);
    }
    // Neighbouring nations (share a border)
    std::map<std::pair<int, int>, int> border;
    for (int y = 0; y < World::H; y++)
        for (int x = 0; x < World::W; x++) {
            int a = doc.nationAt(World::idx(x, y)), b = doc.nationAt(World::idx(x + 1, y)), c = doc.nationAt(World::idx(x, y + 1));
            if (a >= 0 && b >= 0 && a != b) border[{std::min(a, b), std::max(a, b)}]++;
            if (a >= 0 && c >= 0 && a != c) border[{std::min(a, c), std::max(a, c)}]++;
        }
    static const char* warNames[] = {"War of the %s Succession", "The %s Border War", "The Long War with %s", "The %s Rebellion", "The Siege of %s"};
    for (auto& b : border) {
        if (rnd01(s) > 0.55) continue;
        int na = b.first.first, nb = b.first.second;
        // battle site: a border cell between them
        double lat = 0, lon = 0; int cnt = 0;
        for (int i = 0; i < World::W * World::H && cnt < 400; i += 7) {
            int x = i % World::W, y = i / World::W;
            int a = doc.nationAt(i), c = doc.nationAt(World::idx(x + 1, y));
            if ((a == na && c == nb) || (a == nb && c == na)) { lat += World::cellLat(y); lon += World::cellLon(x); cnt++; }
        }
        if (!cnt) continue;
        HistEvent e; e.type = EventType::War; e.year = year(0.3 + rnd01(s) * 0.65);
        e.lat = lat / cnt; e.lon = lon / cnt; e.radiusKm = 100;
        char buf[160];
        std::snprintf(buf, sizeof buf, warNames[rng(s) % 5], doc.nations[rnd01(s) < 0.5 ? na : nb].name.c_str());
        e.title = buf;
        e.text = doc.nations[na].name + " and " + doc.nations[nb].name + " fight over their shared border.";
        doc.events.push_back(e);
        if (rnd01(s) < 0.6) {
            HistEvent t = e; t.type = EventType::Treaty; t.year = e.year + 2 + (int)(rnd01(s) * 25);
            t.title = "Treaty of " + doc.nations[na].name + " and " + doc.nations[nb].name;
            t.text = "Peace returns, for now.";
            doc.events.push_back(t);
        }
        // sometimes a city on the border falls
        if (rnd01(s) < 0.35) {
            int best = -1; double bd = 1e9;
            for (size_t i = 0; i < doc.cities.size(); i++) {
                const City& c = doc.cities[i];
                if (c.capital || (c.nation != na && c.nation != nb) || c.founded >= e.year) continue;
                double d = std::hypot(c.lat - e.lat, (c.lon - e.lon) * std::cos(e.lat * kPi / 180));
                if (d < bd) { bd = d; best = (int)i; }
            }
            if (best >= 0 && bd < 8) {
                City& c = doc.cities[best];
                c.destroyed = e.year + 1;
                HistEvent f; f.type = EventType::Fall; f.year = c.destroyed; f.lat = c.lat; f.lon = c.lon; f.radiusKm = 15;
                f.title = "Sack of " + c.name; f.text = c.name + " is burned during the war and left in ruins.";
                doc.events.push_back(f);
            }
        }
    }
    // Eruptions at real volcanoes
    for (const Volcano& v : w.volcanoes) {
        if (rnd01(s) > 0.12) continue;
        HistEvent e; e.type = EventType::Eruption; e.year = year(rnd01(s)); e.lat = v.lat; e.lon = v.lon; e.radiusKm = 25; e.strength = 0.6;
        e.title = "Great eruption"; e.text = "Ash darkens the sky for a season.";
        doc.events.push_back(e);
    }
    // A meteor strike
    {
        int tries = 0;
        while (tries++ < 200) {
            int i = (int)(rnd01(s) * World::W * World::H) % (World::W * World::H);
            if (w.elev[i] <= 0) continue;
            HistEvent e; e.type = EventType::Meteor; e.year = year(0.05 + rnd01(s) * 0.5);
            e.lat = World::cellLat(i / World::W); e.lon = World::cellLon(i % World::W); e.radiusKm = 18 + rnd01(s) * 25; e.strength = 1;
            e.title = "The Falling Star"; e.text = "A star falls from the sky and leaves a great crater.";
            doc.events.push_back(e);
            break;
        }
    }
    // Disasters: plague, famine, floods
    static const char* dis[] = {"The Great Plague", "The Hungry Years", "The Great Flood", "The Year Without Summer"};
    for (int k = 0; k < 4; k++) {
        if (doc.cities.empty()) break;
        const City& c = doc.cities[rng(s) % doc.cities.size()];
        HistEvent e; e.type = EventType::Disaster; e.year = year(0.2 + rnd01(s) * 0.8); e.lat = c.lat; e.lon = c.lon; e.radiusKm = 300;
        e.title = dis[k]; e.text = "Strikes the lands around " + c.name + ".";
        doc.events.push_back(e);
    }
    std::sort(doc.events.begin(), doc.events.end(), [](const HistEvent& a, const HistEvent& b) { return a.year < b.year; });
}
