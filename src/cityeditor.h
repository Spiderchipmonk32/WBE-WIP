#pragma once
#include "raylib.h"
#include "planet.h"
#include "world.h"
#include "document.h"
#include <vector>
#include <string>

// A separate screen for designing one city: real local terrain (coast, rivers, lakes, hills)
// with a generated street plan, walls, districts, buildings and landmarks you can edit.
class CityEditor {
public:
    bool active = false;
    void open(int city, const Planet& planet, const World& world, Document& doc);
    void close();
    // Runs one frame (input + drawing). Returns false when the user goes back to the world map.
    bool frame(const Planet& planet, const World& world, Document& doc);

private:
    struct Road { std::vector<Vector2> pts; int kind; };   // metres; kind 0 main, 1 street, 2 alley, 3 user
    struct Building { float x, y, w, d, ang; uint8_t district; uint8_t shade; };

    void buildTerrain(const Planet& planet, const World& world);
    void generate(Document& doc);
    void makeLandmarks(Document& doc);
    void recolourDistricts(const CityPlan& plan);
    bool water(double x, double y) const;
    bool sea(double x, double y) const;
    double heightAt(double x, double y) const;
    int districtAt(double x, double y) const;
    Vector2 toScreen(double x, double y) const;
    Vector2 toLocal(Vector2 s) const;

    int city_ = -1;
    double lat0_ = 0, lon0_ = 0, sizeM_ = 6000, cityR_ = 800, wallR_ = 0, cx_ = 0, cy_ = 0;
    int era_ = 1;
    static constexpr int G = 384, DG = 96;
    std::vector<float> elev_;
    std::vector<uint8_t> waterMask_;   // 0 land, 1 sea, 2 river, 3 lake
    Texture2D terrain_{};
    bool haveTex_ = false;
    std::vector<Road> roads_;
    std::vector<Vector2> wall_;
    std::vector<uint8_t> wallOnLand_;
    std::vector<Building> buildings_;
    std::vector<uint8_t> district_, baseDistrict_;
    // view
    double camX_ = 0, camY_ = 0, zoom_ = 0.15;   // pixels per metre
    int tool_ = 0, districtUi_ = 5, landmarkUi_ = 0, landmarkSel_ = -1, landmarkScroll_ = 0, dragLm_ = -1;
    float brushM_ = 200;
    bool showDistricts_ = true, showLabels_ = true, nameEdit_ = false;
    char nameBuf_[96] = "";
    int lastLmSel_ = -2;
    std::vector<std::pair<float, float>> drawing_;
};
