#pragma once
#include "vec3d.h"
#include "noise.h"
#include <vector>
#include <string>
#include <cstdint>

class Planet;

// A coarse global grid (0.5 degree cells, about 55 km at the equator) that the
// simulations run on: tectonics, rock, resources, climate, rivers, nations...
// The fine map detail is still generated on demand by Planet; this grid supplies
// the large-scale structure.

enum class Rock : uint8_t { OceanicBasalt, Sedimentary, Volcanic, Plutonic, Metamorphic, Shield, Count };
enum class Boundary : uint8_t { None, Collision, Subduction, IslandArc, Rift, Ridge, Transform };

struct Plate {
    Vec3d seed;          // centre direction
    Vec3d pole;          // rotation axis (Euler pole)
    double speed;        // radians per million years (scaled)
    bool continental;
    unsigned char r, g, b;
};

struct Volcano { double lat, lon; bool hotspot; };

enum class ResGroup : uint8_t { Metal, Gem, Fuel, Industrial, Building, Count };
struct ResourceDef {
    const char* name;
    ResGroup group;
    unsigned char r, g, b;
    double typicalTonnes;   // typical deposit size (ore or material, tonnes)
    double gradeLo, gradeHi; // % metal/content in the ore (or purity)
    double depthLo, depthHi; // metres below the surface
    int abundance;          // how many deposits on an Earth-sized planet
    bool offshoreOk;
    const char* setting;    // where it forms, in words
};
struct Deposit { int res; double lat, lon; double tonnes, grade, depthM; bool offshore; };

const std::vector<ResourceDef>& resourceDefs();

enum class Biome : uint8_t { Ocean, SeaIce, IceSheet, Tundra, BorealForest, TemperateForest, TemperateRainforest,
    Grassland, Mediterranean, ColdDesert, HotDesert, Savanna, TropicalSeasonalForest, TropicalRainforest, Wetland, Count };
const char* biomeName(Biome b);
void biomeColor(Biome b, unsigned char rgb[3]);
const char* resGroupName(ResGroup g);

class World {
public:
    static constexpr int W = 720, H = 360;   // cells (0.5 degrees)

    // ---- helpers ----
    static int wrapX(int x) { return ((x % W) + W) % W; }
    static int clampY(int y) { return y < 0 ? 0 : (y >= H ? H - 1 : y); }
    static int idx(int x, int y) { return clampY(y) * W + wrapX(x); }
    static double cellLat(int y) { return 90.0 - (y + 0.5) * 180.0 / H; }
    static double cellLon(int x) { return -180.0 + (x + 0.5) * 360.0 / W; }
    static void cellOf(double lat, double lon, int& x, int& y);
    double cellKm() const { return radiusKm * kPi / H; }   // north-south size of a cell
    // Bilinear sample of a float field (wraps east-west)
    static double sample(const std::vector<float>& f, double lat, double lon);
    static int nearestCell(double lat, double lon) { int x, y; cellOf(lat, lon, x, y); return idx(x, y); }

    // ---- tectonics (tectonics.cpp) ----
    void generateTectonics(const Planet& planet, int seed, int plateCount, double activity);
    std::vector<Plate> plates;
    std::vector<uint8_t> plateId;       // plate per cell
    std::vector<uint8_t> boundary;      // Boundary per cell (only on boundary cells)
    std::vector<float> stress;          // 0..1 boundary activity (earthquake risk), spread a little
    std::vector<float> uplift;          // metres added to elevation by tectonics
    std::vector<uint8_t> rock;          // Rock per cell
    std::vector<float> rockAge;         // million years
    std::vector<float> distBoundaryKm;  // distance to the nearest plate boundary
    std::vector<uint8_t> nearBoundary;  // type of that nearest boundary
    std::vector<Volcano> volcanoes;
    Vec3d plateVelocity(int cell) const;  // surface velocity, cm/year-ish

    // Coarse elevation (metres) sampled from the full planet, including tectonics and sculpting
    void sampleElevation(const Planet& planet);
    std::vector<float> elev;

    // ---- resources (resources.cpp) ----
    void generateResources(int seed);
    std::vector<float> favorability(int res) const;    // 0..1 per cell: how likely the geology suits it
    std::vector<Deposit> deposits;
    int selectedRes = -1;                  // resource shown by the Resource overlay
    std::vector<float> selectedFavor;      // its favourability field
    std::vector<int> depositsNear(double lat, double lon, double km, size_t maxCount) const;
    double totalTonnes(int res) const;

    // ---- climate (climate.cpp); empty until generated ----
    void generateClimate(double axialTiltDeg);
    std::vector<float> rainfallYear;   // mm per year
    std::vector<float> tempYear;       // mean degC
    std::vector<float> tempSeaLevel;   // annual mean reduced to sea level (for fine-detail colouring)
    std::vector<float> tempMonth[12];  // degC
    std::vector<float> rainMonth[12];  // mm per month
    std::vector<float> windU[12], windV[12];   // east and north wind components, m/s
    std::vector<float> currentU, currentV;     // ocean surface currents (annual), m/s
    std::vector<float> seaTempAnomaly;         // warm (+) and cold (-) currents, degC
    std::vector<uint8_t> biome;                // Biome per cell
    std::vector<float> distOceanKm;            // land: distance to the sea
    double tiltDeg = 23.4;

    // ---- rivers and lakes (rivers.cpp) ----
    void generateRivers();
    std::vector<int> flowTo;           // downstream cell, -1 at the sea
    std::vector<float> discharge;      // m3/s flowing through each cell
    std::vector<float> lakeSurface;    // lake water level (m); below the ground where there's no lake
    std::vector<uint8_t> lakeType;     // 0 none, 1 lake, 2 salt flat
    std::vector<float> saltMask;       // 1 on salt flats (for smooth drawing)
    struct River { std::vector<Vec3d> pts; std::vector<float> q; int mouthCell; };
    std::vector<River> rivers;
    float maxDischarge = 0;

    double radiusKm = 6371;
    std::vector<Vec3d> cellDir;         // unit direction per cell
    void initGrid(double radius);

    static const char* rockName(Rock r);
    static const char* boundaryName(Boundary b);
};
