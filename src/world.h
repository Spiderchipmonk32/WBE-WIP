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
    std::vector<Volcano> volcanoes;
    Vec3d plateVelocity(int cell) const;  // surface velocity, cm/year-ish

    // Coarse elevation (metres) sampled from the full planet, including tectonics and sculpting
    void sampleElevation(const Planet& planet);
    std::vector<float> elev;

    double radiusKm = 6371;
    std::vector<Vec3d> cellDir;         // unit direction per cell
    void initGrid(double radius);

    static const char* rockName(Rock r);
    static const char* boundaryName(Boundary b);
};
