#pragma once
#include "world.h"
#include "document.h"

// Automatic generation of cultures, nations (territory) and cities.
void generateCultures(Document& doc, int count, uint32_t seed);
// Grows `count` nations from good seed points; borders tend to follow mountains, deserts and rivers.
void generateNations(Document& doc, const World& w, int count, uint32_t seed);
// Places cities in each nation on good sites; capitals are the biggest.
void generateCities(Document& doc, const World& w, double density, uint32_t seed);
// Best city sites not yet taken (lat, lon pairs), for the "suggest sites" markers
std::vector<std::pair<double, double>> suggestSites(const Document& doc, const World& w, int count);
// Area (km2) and city population of each nation
void nationStats(const Document& doc, const World& w, std::vector<double>& areaKm2, std::vector<long long>& population);
// Religions: founded in big cities and spread over land (and a little by sea)
void generateReligions(Document& doc, const World& w, int count, uint32_t seed);
// Rulers for each nation from its founding to the current year
void generateRulers(Document& doc, uint32_t seed);
const char* rulerTitle(int government, bool female);
