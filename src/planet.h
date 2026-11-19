#pragma once
#include "vec3d.h"
#include "noise.h"
#include <vector>
#include <utility>

// Everything about the planet that doesn't depend on graphics.
// Elevation is a pure function of direction, so it can be sampled at any resolution:
// coarse for the whole globe, metre-scale when you zoom into a city.

struct PlanetSettings {
    int   seed = 1337;
    float radiusKm = 6371.f;
    float oceanPercent = 70.f;
    float mountains = 1.f;
    float continentSize = 1.f;
};

enum class BrushType { Raise = 1, Lower = 2, Smooth = 3, Flatten = 4 };

// One brush dab, stored as an edit layer and applied on top of the generated terrain.
struct Stroke {
    Vec3d dir;          // unit direction of the dab centre
    double radiusKm;
    double cosRadius;   // cached cos(angular radius)
    double angRadius;   // radians
    BrushType type;
    double amount;      // metres for raise/lower, 0..1 blend for smooth/flatten
    double targetM;     // target height for smooth/flatten
    int group;          // one mouse drag = one group (one undo step)
};

struct PlanetStats { double landPercent = 0, highestM = 0, deepestM = 0, areaMkm2 = 0; };

class Planet {
public:
    void generate(const PlanetSettings& s);
    const PlanetSettings& settings() const { return settings_; }
    double radiusKm() const { return settings_.radiusKm; }

    // Elevation in metres relative to sea level. detailOctaves adds fine detail
    // (use detailOctavesFor to match the sample spacing). strokeFilter limits which
    // edit strokes are tested (nullptr = all).
    double elevation(const Vec3d& dir, int detailOctaves, const std::vector<int>* strokeFilter = nullptr) const;
    static int detailOctavesFor(double spacingKm, double radiusKm);

    // Sculpting
    void beginStrokeGroup(BrushType type, const Vec3d& dir, double radiusKm);
    void addDab(BrushType type, const Vec3d& dir, double radiusKm, double amount);
    // Removes the last group; returns the areas it touched so they can be redrawn.
    std::vector<std::pair<Vec3d, double>> undo();
    void strokesNear(const Vec3d& dir, double angRadius, std::vector<int>& out) const;
    size_t strokeCount() const { return strokes_.size(); }

    PlanetStats computeStats() const;
    static void colorFor(double elevM, unsigned char rgb[3]);

private:
    double baseRaw(const Vec3d& d) const;
    double baseElevation(const Vec3d& d) const;
    double detail(const Vec3d& d, double baseM, int octaves) const;

    PlanetSettings settings_;
    Simplex3 nCont_, nMtn_, nSmall_, nWarp_, nDetail_;
    double sea_ = 0, hi_ = 1, lo_ = -1, contScale_ = 1.1;
    std::vector<Stroke> strokes_;
    int group_ = 0;
    double groupTarget_ = 0;
};
