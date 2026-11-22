#pragma once
// The living world: time moves on, people are born, marry, trade, scheme, steal and die,
// rulers are succeeded or overthrown, nations go to war, lose land, rebel and fall,
// and everything worth remembering is written into the history.
#include "document.h"
#include "world.h"
#include <string>
#include <vector>

extern const int kSimSpeeds[];       // multipliers: 0 (paused), 1, 2, 5, 10, 20, 50, 100
extern const int kSimSpeedCount;
// world days per real second for a base and speed index
double simDaysPerSecond(int base, int speedIdx);

const char* monthName(const Document& doc, int m);
std::string dateString(const Document& doc, int year, int month, int day);

struct SimNews { int year, month; std::string text; int character = -1; int city = -1; bool important = false; };

struct Simulation {
    std::vector<SimNews> news;        // recent happenings, newest last
    bool territoryChanged = false;    // the map needs repainting
    bool peopleChanged = false;
    uint32_t rng = 0x5eed1234u;

    // start from the present: make sure there are people to follow
    void begin(Document& doc, const World& w);
    // move time on by real seconds (uses the speed setting); returns months simulated
    int update(Document& doc, const World& w, double realSeconds);
    // jump forward a number of days (used for catching up after the app was closed)
    int advanceDays(Document& doc, const World& w, double days);
    // someone in a city to follow: an existing person of that role, or a new one
    int personToFollow(Document& doc, int city, Role role);

private:
    void monthTick(Document& doc, const World& w);
    void yearTick(Document& doc, const World& w);
    void people(Document& doc, const World& w);
    void nations(Document& doc, const World& w);
    void say(Document& doc, Character* c, const std::string& text, bool important = false, int city = -1);
    void history(Document& doc, EventType t, int city, const std::string& title, const std::string& text);
    int succession(Document& doc, int nation, int oldRuler);
    void transferLand(Document& doc, const World& w, int from, int to, int cells);
    void fallNation(Document& doc, int n, int conqueror);
    double r01();
};
