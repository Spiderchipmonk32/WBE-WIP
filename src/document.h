#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Everything the user creates on top of the generated world.

enum class NameStyle : uint8_t { Nordic, Latin, Slavic, Eastern, Desert, Elvish, Celtic, Mesoamerican, Count };
const char* nameStyleName(NameStyle s);
// Make a place or person name in a style; seed makes it repeatable
std::string makeName(NameStyle style, uint32_t& seed, bool place = true);

struct Culture {
    std::string name;
    NameStyle style = NameStyle::Latin;
    unsigned char r = 200, g = 200, b = 200;
    std::string notes;
};

struct Ruler { std::string name; int start = 0, end = 0; };

struct Religion {
    std::string name;
    unsigned char r = 200, g = 200, b = 120;
    int founded = 0;
    double lat = 0, lon = 0;   // where it began
    std::string notes;
};

struct Nation {
    std::vector<Ruler> rulers;
    std::string name;
    unsigned char r = 200, g = 120, b = 120;
    int culture = 0;
    int government = 0;   // index into kGovernments
    std::string notes;
};
extern const char* kGovernments[];
extern const int kGovernmentCount;

struct City {
    std::string name;
    double lat = 0, lon = 0;
    int population = 1000;
    int nation = -1;      // index into nations, -1 = independent
    int culture = 0;
    bool capital = false;
    int founded = 0;      // year (used by the timeline)
    int destroyed = 99999;   // year it fell (99999 = still standing)
    std::string notes;
};

enum class EventType : uint8_t { Note, Meteor, Eruption, Upheaval, Sinking, Founding, Fall, War, Treaty, Disaster, Magic, Count };
const char* eventTypeName(EventType t);
bool eventChangesTerrain(EventType t);

struct HistEvent {
    int year = 0;
    EventType type = EventType::Note;
    double lat = 0, lon = 0;
    double radiusKm = 50;     // size of the effect on the map
    double strength = 1;      // 0.2 .. 3
    std::string title, text;
};

enum class LoreKind : uint8_t { Place, Person, Faction, Religion, Creature, Item, Event, Other, Count };
const char* loreKindName(LoreKind k);

struct Article {
    std::string title, body;
    LoreKind kind = LoreKind::Other;
    bool pinned = false;      // shown on the map
    double lat = 0, lon = 0;
};

// City editor data. The street layout is rebuilt from the seed; what the user adds or changes is stored.
enum class District : uint8_t { None, Market, OldTown, Noble, Temple, Residential, Docks, Industry, Farms, Slums, Park, Count };
const char* districtName(District d);
enum class LandmarkType : uint8_t { Market, Castle, Palace, Temple, Harbour, Gate, Guildhall, University, Tower, Monument, Inn, Other, Count };
const char* landmarkName(LandmarkType t);
struct Landmark { LandmarkType type = LandmarkType::Other; double x = 0, y = 0; std::string name; };   // metres east/north of the centre
struct CityPlan {
    int city = -1;
    uint32_t seed = 1;
    bool walls = true;
    std::vector<Landmark> landmarks;
    std::vector<std::vector<std::pair<float, float>>> extraRoads;   // user-drawn roads (metres)
    std::vector<std::pair<int, uint8_t>> districtEdits;              // painted cells
    bool landmarksMade = false;
};

struct Document {
    int version = 1;
    std::vector<Culture> cultures;
    std::vector<Nation> nations;
    std::vector<City> cities;
    std::vector<uint16_t> owner;   // per World cell: 0 = unclaimed, otherwise nation index + 1
    int era = 1;                   // Era for trade (see trade.h)
    int currentYear = 0, yearMin = -3000, yearMax = 3000;
    std::vector<HistEvent> events;
    std::vector<Article> articles;
    std::vector<Religion> religions;
    std::vector<uint8_t> faith;    // per World cell: 0 = none, otherwise religion index + 1
    std::vector<CityPlan> plans;
    CityPlan* planFor(int city) { for (auto& p : plans) if (p.city == city) return &p; return nullptr; }
    std::vector<std::pair<int, int>> customRoutes;   // city index pairs

    void ensureOwner(int cells) {
        if ((int)owner.size() != cells) owner.assign(cells, 0);
        if ((int)faith.size() != cells) faith.assign(cells, 0);
    }
    const Ruler* rulerIn(int nation, int year) const {
        if (nation < 0 || nation >= (int)nations.size()) return nullptr;
        for (const Ruler& r : nations[nation].rulers) if (r.start <= year && year < r.end) return &r;
        return nullptr;
    }
    void removeNation(int n);      // keeps indices in cities and territory consistent
    void removeCity(int c) {
        if (c < 0 || c >= (int)cities.size()) return;
        cities.erase(cities.begin() + c);
        std::vector<std::pair<int, int>> keep;
        for (auto& r : customRoutes) {
            if (r.first == c || r.second == c) continue;
            keep.push_back({r.first > c ? r.first - 1 : r.first, r.second > c ? r.second - 1 : r.second});
        }
        customRoutes.swap(keep);
        std::vector<CityPlan> kp;
        for (auto& p : plans) { if (p.city == c) continue; if (p.city > c) p.city--; kp.push_back(p); }
        plans.swap(kp);
    }
    int nationAt(int cell) const { return cell >= 0 && cell < (int)owner.size() ? (int)owner[cell] - 1 : -1; }
};
