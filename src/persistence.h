#pragma once
#include "planet.h"
#include "document.h"
#include <string>
#include <vector>

// World files: a small text file holding everything you made. The generated parts
// (terrain, plates, climate, rivers, resources) are rebuilt from the settings and seed.
struct SaveData {
    PlanetSettings settings;
    std::vector<Stroke> strokes;
    double viewLon = 0, viewLat = 15, viewZoom = 5;
    int style = 0;
    Document doc;   // nations, cities, routes, history, lore...
};

std::string worldsFolder();      // "worlds" next to the program
std::string exportsFolder();     // "exports" next to the program
bool saveWorld(const std::string& path, const SaveData& d, std::string& err);
bool loadWorld(const std::string& path, SaveData& d, std::string& err);
std::vector<std::string> listSavedWorlds();   // names without extension, newest first

// Quote/unquote strings so names and notes can contain spaces, quotes and new lines
std::string quote(const std::string& s);
bool unquote(const std::string& line, size_t& pos, std::string& out);
