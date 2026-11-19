#pragma once
#include "document.h"
#include "planet.h"
#include "world.h"

// Terrain changes from all events up to and including `year`
std::vector<Stroke> buildEventStrokes(const Document& doc, int year, double planetRadiusKm);
// Fill the timeline with a plausible history: city foundings, wars between neighbours,
// eruptions at real volcanoes, a meteor strike or two, disasters and treaties.
void generateHistory(Document& doc, const World& w, uint32_t seed);
// Is a city standing in this year?
inline bool cityExists(const City& c, int year) { return c.founded <= year && c.destroyed > year; }
inline bool cityRuined(const City& c, int year) { return c.destroyed <= year; }
