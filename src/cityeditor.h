#pragma once
#include "raylib.h"
#include "planet.h"
#include "world.h"
#include "document.h"
#include <vector>
#include <string>

// A separate screen for designing one city: real local terrain (coast, rivers, lakes, hills)
// with a generated street plan, walls, districts, packed buildings, fields and trees, which
// you can then edit like a map-making program: stamp buildings and trees, move, rotate,
// scale and erase them, paint the ground, draw roads and write labels.
class CityEditor {
public:
    bool active = false;
    void open(int city, const Planet& planet, const World& world, Document& doc);
    void close();
    // Runs one frame (input + drawing). Returns false when the user goes back to the world map.
    bool frame(const Planet& planet, const World& world, Document& doc);

private:
    struct Road { std::vector<Vector2> pts; int kind; };   // metres; kind 0 main, 1 street, 2 alley, 3 user

    void buildTerrain(const Planet& planet, const World& world);
    void generate(Document& doc);
    void makeLandmarks(Document& doc);
    bool water(double x, double y) const;
    bool sea(double x, double y) const;
    double heightAt(double x, double y) const;
    int districtAt(double x, double y) const;
    Vector2 toScreen(double x, double y) const;
    Vector2 toLocal(Vector2 s) const;
    // building placement on a coarse occupancy grid
    void occClear();
    bool occFree(double x, double y, double w, double d, double ang) const;
    void occMark(double x, double y, double w, double d, double ang);
    void occRoad(const Road& r, double halfW);
    // editing
    void bake(CityPlan& plan);           // keep the generated buildings as the user's own from now on
    void pushUndo();
    void rebuildPaint(const CityPlan& plan);
    int pickObject(Vector2 local) const;
    void drawObject(const CityObject& o, bool selected) const;

    int city_ = -1;
    double lat0_ = 0, lon0_ = 0, sizeM_ = 6000, cityR_ = 800, wallR_ = 0, cx_ = 0, cy_ = 0;
    int era_ = 1;
    static constexpr int G = 384, DG = 96;
    std::vector<float> elev_;
    std::vector<uint8_t> waterMask_;   // 0 land, 1 sea, 2 river, 3 lake
    Texture2D terrain_{}, paintTex_{};
    bool haveTex_ = false, havePaint_ = false;
    std::vector<Road> roads_;
    std::vector<Vector2> wall_;
    std::vector<uint8_t> wallOnLand_;
    std::vector<CityObject> objects_;
    std::vector<uint8_t> district_, baseDistrict_;
    std::vector<uint8_t> occ_; int OG_ = 0; double oc_ = 4;
    // view
    double camX_ = 0, camY_ = 0, zoom_ = 0.15;   // pixels per metre
    // tools: 0 select, 1 stamp, 2 erase, 3 paint ground, 4 road, 5 label, 6 landmark, 7 district
    int tool_ = 0, districtUi_ = 5, landmarkUi_ = 0, landmarkSel_ = -1, landmarkScroll_ = 0, dragLm_ = -1;
    int stampUi_ = 0, materialUi_ = 1, selObj_ = -1, dragObj_ = -1, selLabel_ = -1, dragLabel_ = -1;
    float brushM_ = 120, stampScale_ = 1, stampAng_ = 0, labelSize_ = 40;
    bool showDistricts_ = false, showLabels_ = true, nameEdit_ = false, labelEdit_ = false;
    char nameBuf_[96] = "", labelBuf_[96] = "New label";
    int lastLmSel_ = -2;
    std::vector<std::pair<float, float>> drawing_;
    std::vector<std::vector<CityObject>> undo_;
    Vector2 dragOffset_{0, 0};
    float panelScroll_ = 0, panelH_ = 0;
};
