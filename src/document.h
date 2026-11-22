#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "world.h"

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
    int founded = -99999;  // year (for the living world; -99999 = since before records)
    int fell = 99999;      // year it fell (99999 = still standing)
    float stability = 0.7f, treasury = 1000.f;   // living-world state
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

enum class LoreKind : uint8_t { Place, Person, Faction, Religion, Creature, Item, Event, Other,
                                Settlement, Organization, Species, Language, Magic, Myth, Tradition, Material, Technology, Conflict, Quest, Session, Document, Title, Count };
const char* loreKindName(LoreKind k);

struct Article {
    std::string title, body;
    std::string secret;       // notes only the world's author sees
    std::string tags;         // comma separated
    std::vector<std::pair<std::string, std::string>> fields;   // the infobox: name, value
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
// Things placed on a city map (generated or stamped by hand), like map-making stamps
enum class CityObj : uint8_t { House, Hall, Tower, Temple, Keep, Tree, Orchard, Well, Statue, Stall, Windmill, Ship, Tent, Rock, Field, Count };
const char* cityObjName(CityObj t);
struct CityObject { CityObj type = CityObj::House; float x = 0, y = 0, ang = 0, w = 10, d = 8; uint8_t shade = 0, district = 0; };
struct CityLabel { float x = 0, y = 0, size = 30; std::string text; };
struct CityPlan {
    int city = -1;
    bool baked = false;                  // the user has edited buildings: keep these instead of generating
    std::vector<CityObject> objects;
    std::vector<CityLabel> labels;
    std::vector<uint8_t> paint;          // ground painted by hand (material per terrain pixel, 0 = none)
    uint32_t seed = 1;
    bool walls = true;
    std::vector<Landmark> landmarks;
    std::vector<std::vector<std::pair<float, float>>> extraRoads;   // user-drawn roads (metres)
    std::vector<std::pair<int, uint8_t>> districtEdits;              // painted cells
    bool landmarksMade = false;
};

enum class Relation : uint8_t { Alliance, TradePact, Rivalry, War, Vassal, Count };
const char* relationName(Relation r);
struct NationRelation { int a = 0, b = 0; Relation type = Relation::Alliance; };
struct Bookmark { std::string name; double lat = 0, lon = 0, zoom = 5; };

// ---- Characters and the relationship web ----
enum class Role : uint8_t { Commoner, Ruler, Noble, Merchant, Criminal, Soldier, Priest, Scholar, Artisan, Farmer, Adventurer, Mage, Count };
const char* roleName(Role r);
extern const char* kTraits[];
extern const int kTraitCount;

struct LifeEvent { int year = 0, month = 0; std::string text; };

struct Character {
    int id = 0;                 // stable id (links refer to it)
    std::string name, title, house, occupation, appearance, notes;
    int sex = 0;                // 0 man, 1 woman, 2 other
    int born = 0, died = 99999;
    Role role = Role::Commoner;
    int culture = -1, nation = -1, religion = -1, city = -1;
    std::vector<int> traits;    // indices into kTraits
    double wealth = 100, renown = 0, health = 1;   // coins, fame 0..100, 0..1
    bool followed = false, userMade = true;
    uint32_t look = 1;          // portrait seed
    std::vector<LifeEvent> log;
    // where they are now (merchants travel between cities)
    int travelTo = -1; float travelT = 0;
    bool hasTrait(int t) const { return std::find(traits.begin(), traits.end(), t) != traits.end(); }
    bool alive(int year) const { return year >= born && year < died; }
};

enum class EntKind : uint8_t { Character, Nation, City, Religion, Culture };
struct EntityRef {
    EntKind kind = EntKind::Character; int id = -1;   // character id, or index for the others
    bool operator==(const EntityRef& o) const { return kind == o.kind && id == o.id; }
};
// Parent, Mentor, Employs, Serves, Rules and MemberOf read "A is ... of/to B"; the rest go both ways
enum class RelType : uint8_t { Parent, Spouse, Sibling, Lover, Friend, Rival, Enemy, Mentor, Employs, Serves, Rules, MemberOf, Ally, Owes, Count };
const char* relTypeName(RelType t);
bool relTypeDirected(RelType t);
struct Link {
    EntityRef a, b;
    RelType type = RelType::Friend;
    int strength = 3;           // 1..5
    int since = 0;
    std::string notes;
};

// A trade route the user drew or edited. It runs from one city to another through optional
// waypoints; between points it either finds the best way over the terrain or goes straight.
struct CustomRoute {
    int from = -1, to = -1;
    std::vector<std::pair<double, double>> via;      // waypoints (lat, lon) in order
    std::string name;
    int mode = 0;                                    // 0 follow terrain, 1 straight lines as drawn
    std::vector<std::pair<int, double>> goods;       // good index, tonnes per year (empty = from the simulation)
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
    std::vector<NationRelation> relations;
    std::vector<CustomResource> customResources;
    std::vector<Bookmark> bookmarks;
    int presentYear = 0;           // the year city populations are given for
    // Population of a city in a given year (grows from its founding to the present)
    double populationAt(const City& c, int year) const {
        if (year >= presentYear || c.founded >= presentYear) return c.population;
        double t = (double)(year - c.founded) / (double)(presentYear - c.founded);
        if (t <= 0) return 0;
        return c.population * std::pow(std::max(0.02, std::min(1.0, t)), 1.4);
    }
    CityPlan* planFor(int city) { for (auto& p : plans) if (p.city == city) return &p; return nullptr; }
    std::vector<CustomRoute> customRoutes;
    std::vector<Character> characters;
    struct Age { std::string name; int start = 0, end = 100; unsigned char r = 180, g = 140, b = 90; };
    std::vector<Age> ages;          // named eras on the timeline
    // ---- living world ----
    int simYear = 0, simMonth = 0, simDay = 0;   // where the simulation has reached
    bool simStarted = false;
    int simSpeed = 0;              // index into kSimSpeeds (0 = paused)
    int simBase = 1;               // 0: real time (1 s = 1 s), 1: story time (1 s = 1 day), 2: fast (1 s = 1 month)
    bool simCatchUp = true;        // keep going while the app is closed
    long long simWallClock = 0;    // unix time when last saved
    double simCarryDays = 0;
    std::vector<std::string> monthNames;   // the calendar (empty = default)
    int daysPerMonth = 30;
    std::vector<Link> links;
    int nextCharId = 1;
    Character* character(int id) { for (auto& c : characters) if (c.id == id) return &c; return nullptr; }
    const Character* character(int id) const { for (auto& c : characters) if (c.id == id) return &c; return nullptr; }
    int addCharacter(Character c) { c.id = nextCharId++; characters.push_back(c); return c.id; }
    void removeCharacter(int id) {
        characters.erase(std::remove_if(characters.begin(), characters.end(), [&](const Character& c) { return c.id == id; }), characters.end());
        links.erase(std::remove_if(links.begin(), links.end(), [&](const Link& l) {
            return (l.a.kind == EntKind::Character && l.a.id == id) || (l.b.kind == EntKind::Character && l.b.id == id); }), links.end());
    }
    void addLink(EntityRef a, EntityRef b, RelType t, int since, int strength = 3) {
        for (auto& l : links) if (l.type == t && ((l.a == a && l.b == b) || (!relTypeDirected(t) && l.a == b && l.b == a))) return;
        Link l; l.a = a; l.b = b; l.type = t; l.since = since; l.strength = strength;
        links.push_back(l);
    }
    // keeps links and characters pointing at the right nation/city after one is removed
    void reindexLinks(EntKind kind, int removed) {
        links.erase(std::remove_if(links.begin(), links.end(), [&](const Link& l) {
            return (l.a.kind == kind && l.a.id == removed) || (l.b.kind == kind && l.b.id == removed); }), links.end());
        for (auto& l : links) {
            if (l.a.kind == kind && l.a.id > removed) l.a.id--;
            if (l.b.kind == kind && l.b.id > removed) l.b.id--;
        }
        for (auto& c : characters) {
            int* f = kind == EntKind::Nation ? &c.nation : kind == EntKind::City ? &c.city : kind == EntKind::Religion ? &c.religion : &c.culture;
            if (*f == removed) *f = -1; else if (*f > removed) (*f)--;
        }
    }
    std::vector<std::pair<int, int>> hiddenRoutes;   // simulated routes (city pairs) the user removed

    void ensureOwner(int cells) {
        if ((int)owner.size() != cells) owner.assign(cells, 0);
        if ((int)faith.size() != cells) faith.assign(cells, 0);
    }
    const Ruler* rulerIn(int nation, int year) const {
        if (nation < 0 || nation >= (int)nations.size()) return nullptr;
        for (const Ruler& r : nations[nation].rulers) if (r.start <= year && year < r.end) return &r;
        return nullptr;
    }
    void removeNation(int n);      // keeps indices in cities, territory and relations consistent
    void removeCity(int c) {
        if (c < 0 || c >= (int)cities.size()) return;
        cities.erase(cities.begin() + c);
        std::vector<CustomRoute> keep;
        for (auto r : customRoutes) {
            if (r.from == c || r.to == c) continue;
            if (r.from > c) r.from--;
            if (r.to > c) r.to--;
            keep.push_back(r);
        }
        customRoutes.swap(keep);
        std::vector<std::pair<int, int>> kh;
        for (auto r : hiddenRoutes) {
            if (r.first == c || r.second == c) continue;
            kh.push_back({r.first > c ? r.first - 1 : r.first, r.second > c ? r.second - 1 : r.second});
        }
        hiddenRoutes.swap(kh);
        reindexLinks(EntKind::City, c);
        std::vector<CityPlan> kp;
        for (auto& p : plans) { if (p.city == c) continue; if (p.city > c) p.city--; kp.push_back(p); }
        plans.swap(kp);
    }
    int nationAt(int cell) const { return cell >= 0 && cell < (int)owner.size() ? (int)owner[cell] - 1 : -1; }
};
