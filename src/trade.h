#pragma once
#include "world.h"
#include "document.h"
#include <string>
#include <vector>

// Economy and trade. Cities produce goods from the land and mines around them, make
// crafted goods from inputs, and consume by population. Each good flows from the cheapest
// supplier (local price + transport) along a pathfound route whose cost depends on the
// terrain, rivers, sea lanes, wind and the era's transport.

enum class Era : int { Ancient, Medieval, EarlyModern, Industrial, Modern, Count };
const char* eraName(Era e);

struct GoodDef {
    const char* name;
    double basePrice;       // coins per tonne
    double perCapita;       // tonnes per person per year demanded
    bool crafted;
    int minEra;             // demanded from this era on
};
const std::vector<GoodDef>& goodDefs();

struct TradeRoute {
    int from, to;                       // city indices
    std::vector<int> cells;             // path on the World grid
    std::vector<std::pair<int, double>> goods;   // good index, tonnes per year
    double tonnes = 0, value = 0;       // per year
    double days = 0;                    // travel time
    double seaShare = 0;                // fraction of the trip by sea or river
    bool custom = false;
    int customIndex = -1;               // index into Document::customRoutes
    double km = 0;                      // length
    std::vector<double> legDays;        // travel days for each leg between waypoints
    std::vector<int> legStart;          // index into cells where each leg starts
};

struct Economy {
    Era era = Era::Medieval;
    int goodCount = 0;
    // per city, per good (tonnes per year)
    std::vector<std::vector<double>> production, demand, price;
    std::vector<TradeRoute> routes;
    std::vector<double> totalProduction;   // per good
    double totalTradeValue = 0;
    bool valid = false;

    void simulate(const World& w, const Document& doc, Era era);
    // Find a path between two cities (for custom routes); returns travel days, or -1
    double findPath(const World& w, const Document& doc, int a, int b, std::vector<int>& cells, double& seaShare) const;
    // Route through a list of points (cells). mode 0: best path over the terrain between each pair;
    // mode 1: straight lines exactly as drawn. Fills cells, sea share, length and days per leg. Returns days or -1.
    double pathVia(const World& w, const std::vector<std::pair<double, double>>& pts, int mode, TradeRoute& r) const;
    bool ready() const { return !speed.empty(); }
    void prepare(const World& w, Era e) { era = e; buildTravelGrid(w, e); }
    std::vector<std::pair<int, double>> topExports(int city, int n) const;
    std::vector<std::pair<int, double>> topImports(int city, int n) const;
private:
    std::vector<float> speed;   // km/day by cell (land, river or sea), 0 = impassable
    std::vector<uint8_t> kind;  // 0 land, 1 river, 2 sea
    std::vector<float> windU, windV;
    void buildTravelGrid(const World& w, Era e);
};
