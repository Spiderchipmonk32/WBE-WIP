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

struct Nation {
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
    std::string notes;
};

struct Document {
    int version = 1;
    std::vector<Culture> cultures;
    std::vector<Nation> nations;
    std::vector<City> cities;
    std::vector<uint16_t> owner;   // per World cell: 0 = unclaimed, otherwise nation index + 1

    void ensureOwner(int cells) { if ((int)owner.size() != cells) owner.assign(cells, 0); }
    void removeNation(int n);      // keeps indices in cities and territory consistent
    void removeCity(int c) { if (c >= 0 && c < (int)cities.size()) cities.erase(cities.begin() + c); }
    int nationAt(int cell) const { return cell >= 0 && cell < (int)owner.size() ? (int)owner[cell] - 1 : -1; }
};
