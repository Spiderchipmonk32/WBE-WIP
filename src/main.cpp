// Worldbuilder: 2D world map of a round planet, with simulations layered on top.
#include "raylib.h"
#include "raygui.h"
#include "planet.h"
#include "world.h"
#include "tilemap.h"
#include "ui.h"
#include "persistence.h"
#include "politics.h"
#include <vector>
#include <thread>
#include <fstream>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>

namespace {

const char* formatKm(double km) {
    if (km < 1.0) return TextFormat("%.0f m", km * 1000);
    if (km < 100.0) return TextFormat("%.1f km", km);
    return TextFormat("%.0f km", km);
}

double wrapLon(double lon) { return lon - 360.0 * std::floor((lon + 180.0) / 360.0); }

const char* formatTonnes(double t) {
    if (t >= 1e9) return TextFormat("%.1f billion t", t / 1e9);
    if (t >= 1e6) return TextFormat("%.1f million t", t / 1e6);
    if (t >= 1e3) return TextFormat("%.1f thousand t", t / 1e3);
    return TextFormat("%.0f t", t);
}

struct Theme { Color bg, grid, text; };
Theme themeFor(MapStyle s) {
    if (s == MapStyle::Fantasy) return {{214, 200, 164, 255}, {110, 80, 50, 90}, {70, 50, 30, 255}};
    return {{12, 16, 24, 255}, {255, 255, 255, 50}, {235, 238, 245, 255}};
}

void drawGrid(const MapView& v, int sw, int sh, int left, Color c) {
    static const double steps[] = {0.01, 0.02, 0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30};
    double step = 30;
    for (double s : steps) if (s * v.pxPerDeg >= 90) { step = s; break; }
    double lonMin = v.centerLon - sw / 2.0 / v.pxPerDeg, lonMax = v.centerLon + sw / 2.0 / v.pxPerDeg;
    double latMin = std::max(-90.0, v.centerLat - sh / 2.0 / v.pxPerDeg), latMax = std::min(90.0, v.centerLat + sh / 2.0 / v.pxPerDeg);
    int yTop = (int)(sh / 2.0 - (latMax - v.centerLat) * v.pxPerDeg), yBot = (int)(sh / 2.0 - (latMin - v.centerLat) * v.pxPerDeg);
    const char* fmt = step < 0.1 ? "%.2f" : (step < 1 ? "%.1f" : "%.0f");
    for (double lon = std::floor(lonMin / step) * step; lon <= lonMax; lon += step) {
        int x = (int)(sw / 2.0 + (lon - v.centerLon) * v.pxPerDeg);
        DrawLine(x, yTop, x, yBot, c);
        if (x > left) DrawText(TextFormat(fmt, std::fabs(wrapLon(lon))), x + 3, std::max(yTop, 0) + 50, 14, c);
    }
    for (double lat = std::ceil(latMin / step) * step; lat <= latMax; lat += step) {
        int y = (int)(sh / 2.0 - (lat - v.centerLat) * v.pxPerDeg);
        DrawLine(0, y, sw, y, c);
        DrawText(TextFormat(fmt, std::fabs(lat)), left + 6, y + 3, 14, c);
    }
}

void drawScaleBar(const MapView& v, double R, int x, int y, Color c) {
    double kmPerPx = 111.195 * R / 6371.0 * std::cos(v.centerLat * kPi / 180) / v.pxPerDeg;
    double target = kmPerPx * 160, nice = 1;
    double mag = std::pow(10.0, std::floor(std::log10(target)));
    for (double m : {1.0, 2.0, 5.0, 10.0}) if (m * mag <= target) nice = m * mag;
    int len = (int)(nice / kmPerPx);
    DrawRectangle(x, y, len, 4, c);
    DrawRectangle(x, y - 6, 2, 10, c);
    DrawRectangle(x + len - 2, y - 6, 2, 10, c);
    DrawText(formatKm(nice), x, y - 26, 18, c);
}

void drawArrow(Vector2 a, Vector2 b, Color c, float thick) {
    DrawLineEx(a, b, thick, c);
    float dx = b.x - a.x, dy = b.y - a.y, l = std::sqrt(dx * dx + dy * dy);
    if (l < 1) return;
    dx /= l; dy /= l;
    float hs = std::min(8.f, l * 0.45f);
    Vector2 p1{b.x - dx * hs - dy * hs * 0.6f, b.y - dy * hs + dx * hs * 0.6f};
    Vector2 p2{b.x - dx * hs + dy * hs * 0.6f, b.y - dy * hs - dx * hs * 0.6f};
    DrawTriangle(b, p2, p1, c);
    DrawTriangle(b, p1, p2, c);
}

// East and north unit vectors at a point, to turn 3D velocities into map directions
void localAxes(double lat, double lon, Vec3d& east, Vec3d& north) {
    double la = lat * kPi / 180, lo = lon * kPi / 180;
    east = {std::cos(lo), 0, -std::sin(lo)};
    north = {-std::sin(la) * std::sin(lo), std::cos(la), -std::sin(la) * std::cos(lo)};
}

} // namespace

enum Tab { TabWorld, TabMap, TabSculpt, TabResources, TabClimate, TabNations, TabCities, TabTrade, TabHistory, TabLore, TabFile };

struct App {
    PlanetSettings ui;
    Planet planet;
    World world;
    Document doc;
    int styleUi = 0;
    bool labelsDirty = true;
    struct Label { double lat, lon, areaKm2; };
    std::vector<Label> nationLabels;
    void updateLabels() {
        if (!labelsDirty) return;
        labelsDirty = false;
        nationLabels.assign(doc.nations.size(), Label{0, 0, 0});
        std::vector<Vec3d> sum(doc.nations.size(), Vec3d{0, 0, 0});
        double ck = world.cellKm();
        for (int i = 0; i < (int)doc.owner.size(); i++) {
            int n = doc.nationAt(i);
            if (n < 0 || n >= (int)sum.size()) continue;
            double a = ck * ck * std::max(0.02, std::cos(World::cellLat(i / World::W) * kPi / 180));
            sum[n] += world.cellDir[i] * a;
            nationLabels[n].areaKm2 += a;
        }
        for (size_t n = 0; n < sum.size(); n++) {
            Vec3d d = sum[n].normalized();
            nationLabels[n].lat = std::asin(clampd(d.y, -1, 1)) * 180 / kPi;
            nationLabels[n].lon = std::atan2(d.x, d.z) * 180 / kPi;
        }
    }
    TileMap* map = nullptr;
    PlanetStats stats;
    MapView view;

    void regenerate() {
        map->waitIdle();   // background threads must stop reading the old world first
        planet.generate(ui);
        world.generateTectonics(planet, ui.seed, ui.plateCount, ui.tectonicActivity);
        planet.setUplift(&world.uplift);
        world.sampleElevation(planet);
        world.generateClimate(ui.axialTilt);
        planet.setClimate(&world.tempSeaLevel, &world.rainfallYear);
        world.generateRivers();
        world.generateHabitability();
        world.generateResources(ui.seed);
        doc = Document{};
        doc.ensureOwner(World::W * World::H);
        labelsDirty = true;
        world.selectedFavor.clear();
        if (world.selectedRes >= 0) world.selectedFavor = world.favorability(world.selectedRes);
        map->reset();
        stats = planet.computeStats();
    }
    SaveData makeSave() const {
        SaveData d;
        d.settings = ui; d.strokes = planet.strokes();
        d.viewLon = view.centerLon; d.viewLat = view.centerLat; d.viewZoom = view.pxPerDeg; d.style = styleUi;
        d.doc = doc;
        return d;
    }
    void applySave(const SaveData& d) {
        ui = d.settings;
        regenerate();
        if (!d.strokes.empty()) { planet.setStrokes(d.strokes); recalcClimate(); stats = planet.computeStats(); }
        view.centerLon = d.viewLon; view.centerLat = d.viewLat; view.pxPerDeg = d.viewZoom;
        styleUi = d.style;
        doc = d.doc;
        doc.ensureOwner(World::W * World::H);
        labelsDirty = true;
    }
    // After sculpting or changing the tilt: re-run the climate on the new terrain
    void recalcClimate() {
        map->waitIdle();
        world.sampleElevation(planet);
        world.generateClimate(ui.axialTilt);
        world.generateRivers();
        world.generateHabitability();
        if (world.selectedRes >= 0) world.selectedFavor = world.favorability(world.selectedRes);
        map->invalidateAll();
    }
};

static const char* kMonths[13] = {"January", "February", "March", "April", "May", "June", "July", "August",
                                  "September", "October", "November", "December", "Whole year"};

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1600, 900, "Worldbuilder");
    SetTargetFPS(144);
    GuiSetStyle(DEFAULT, TEXT_SIZE, 18);

    App app;
    TileMap map(&app.planet);
    app.map = &map;
    map.setWorld(&app.world);
    map.setDocument(&app.doc);
    app.regenerate();

    MapView& view = app.view;
    view.pxPerDeg = GetScreenHeight() / 180.0 * 1.1;
    bool seedEdit = false, painting = false, panning = false, showGrid = true, showVolcanoes = true, showRivers = true;
    int tab = 0;
    int tool = 0;
    int& styleUi = app.styleUi;
    int overlayUi = 0, overlayScroll = 0;
    int resGroupUi = 0, resListScroll = 0, resListActive = -1;
    float radiusUi = app.ui.radiusKm, oceanUi = app.ui.oceanPercent, mountUi = app.ui.mountains, contUi = app.ui.continentSize;
    float platesUi = (float)app.ui.plateCount, activityUi = app.ui.tectonicActivity;
    float monthUi = 12, tiltUi = app.ui.axialTilt;
    char nameBuf[64] = "my world";
    bool nameEdit = false, autosave = true;
    int savedScroll = 0, savedActive = -1;
    std::vector<std::string> savedWorlds = listSavedWorlds();
    std::string status;
    double statusTime = -100, lastAutosave = GetTime();
    int pendingExport = 0;   // runs on the frame after the button, so the "working" message shows
    auto syncSliders = [&]() {
        radiusUi = app.ui.radiusKm; oceanUi = app.ui.oceanPercent; mountUi = app.ui.mountains; contUi = app.ui.continentSize;
        platesUi = (float)app.ui.plateCount; activityUi = app.ui.tectonicActivity; tiltUi = app.ui.axialTilt;
    };
    auto setStatus = [&](const std::string& t) { status = t; statusTime = GetTime(); };
    int nationSel = -1, nationScroll = 0, polTool = 0;
    float nationCountUi = 40, cityDensityUi = 1;
    char nationNameBuf[64] = "", nationNotesBuf[256] = "";
    bool nationNameEdit = false, nationNotesEdit = false;
    int citySel = -1, cityScroll = 0, cityTool = 0;
    char cityNameBuf[64] = "", cityNotesBuf[256] = "";
    bool cityNameEdit = false, cityNotesEdit = false, showSites = false, showCities = true, showLabels = true;
    std::vector<std::pair<double, double>> sites;
    int lastNationSel = -2, lastCitySel = -2;
    auto randomColor = [](unsigned char& r, unsigned char& g, unsigned char& b) {
        double hue = GetRandomValue(0, 5999) / 1000.0; int hi = (int)hue; double f = hue - hi;
        double rgb[6][3] = {{1, f, 0}, {1 - f, 1, 0}, {0, 1, f}, {0, 1 - f, 1}, {f, 0, 1}, {1, 0, 1 - f}};
        r = (unsigned char)(70 + 160 * rgb[hi][0]); g = (unsigned char)(70 + 160 * rgb[hi][1]); b = (unsigned char)(70 + 160 * rgb[hi][2]);
    };
    auto cleanList = [](std::string t) { for (char& ch : t) if (ch == ';' || ch == '\n') ch = ','; return t; };
    int climateViewUi = -1;
    float brushT = 0.80f, strengthT = 0.851f;

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        const int sw = GetScreenWidth(), sh = GetScreenHeight();
        const double R = app.planet.radiusKm();
        const double brushKm = 0.05 * std::pow(60000.0, (double)brushT);
        const double strength = 0.1 * std::pow(50000.0, (double)strengthT);
        const Rectangle panel = {10, 10, 340, (float)sh - 20};
        const Vector2 mouse = GetMousePosition();
        const bool overPanel = CheckCollisionPointRec(mouse, panel);
        const double minZoom = sh / 180.0, maxZoom = TileMap::TILE / 90.0 * (1 << TileMap::MAX_LEVEL) * 2;
        const bool sculptTab = (tab == TabSculpt);
        const int activeTool = sculptTab ? tool : 0;
        const bool paintingTerritory = tab == TabNations && polTool > 0 && nationSel >= 0;
        const bool placingCity = tab == TabCities && cityTool == 1;
        const bool anyTool = activeTool > 0 || paintingTerritory || placingCity;
        app.updateLabels();
        // city under the mouse (for selecting and tooltips)
        int hoverCity = -1;
        if (showCities && !overPanel) {
            float best = 10;
            for (int i = 0; i < (int)app.doc.cities.size(); i++) {
                const City& c = app.doc.cities[i];
                double dl = std::remainder(c.lon - view.centerLon, 360.0);
                float x = (float)(sw / 2.0 + dl * view.pxPerDeg), y = (float)(sh / 2.0 - (c.lat - view.centerLat) * view.pxPerDeg);
                float d = std::sqrt((x - mouse.x) * (x - mouse.x) + (y - mouse.y) * (y - mouse.y));
                if (d < best) { best = d; hoverCity = i; }
            }
        }

        // ---- Navigation ----
        float wheel = overPanel ? 0 : GetMouseWheelMove();
        if (wheel != 0) {
            double lon0 = view.centerLon + (mouse.x - sw / 2.0) / view.pxPerDeg;
            double lat0 = view.centerLat - (mouse.y - sh / 2.0) / view.pxPerDeg;
            view.pxPerDeg = clampd(view.pxPerDeg * std::pow(1.2, wheel), minZoom, maxZoom);
            view.centerLon = lon0 - (mouse.x - sw / 2.0) / view.pxPerDeg;
            view.centerLat = lat0 + (mouse.y - sh / 2.0) / view.pxPerDeg;
        }
        bool clickedCity = false;
        if (!overPanel && !anyTool && hoverCity >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            citySel = hoverCity; clickedCity = true;
            if (tab != TabCities) tab = TabCities;
        }
        if (!overPanel && (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) ||
                           (!anyTool && !clickedCity && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))))
            panning = true;
        if (!IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && !IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
            panning = false;
        if (panning && !painting) {
            Vector2 md = GetMouseDelta();
            view.centerLon -= md.x / view.pxPerDeg;
            view.centerLat += md.y / view.pxPerDeg;
        }
        if (!seedEdit) {
            double k = 600.0 * dt / view.pxPerDeg;
            if (IsKeyDown(KEY_W)) view.centerLat += k;
            if (IsKeyDown(KEY_S)) view.centerLat -= k;
            if (IsKeyDown(KEY_A)) view.centerLon -= k;
            if (IsKeyDown(KEY_D)) view.centerLon += k;
            if (sculptTab) {
                if (IsKeyPressed(KEY_ONE)) tool = 0;
                if (IsKeyPressed(KEY_TWO)) tool = 1;
                if (IsKeyPressed(KEY_THREE)) tool = 2;
                if (IsKeyPressed(KEY_FOUR)) tool = 3;
                if (IsKeyPressed(KEY_FIVE)) tool = 4;
            }
            if (IsKeyPressed(KEY_LEFT_BRACKET)) brushT = std::max(0.f, brushT - 0.03f);
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) brushT = std::min(1.f, brushT + 0.03f);
        }
        view.centerLon = wrapLon(view.centerLon);
        double halfH = sh / 2.0 / view.pxPerDeg;
        view.centerLat = halfH >= 90 ? 0 : clampd(view.centerLat, -90 + halfH, 90 - halfH);

        double mLon = wrapLon(view.centerLon + (mouse.x - sw / 2.0) / view.pxPerDeg);
        double mLat = view.centerLat - (mouse.y - sh / 2.0) / view.pxPerDeg;
        bool hit = !overPanel && std::fabs(mLat) <= 90;
        Vec3d hitDir = TileMap::dirFromLatLon(clampd(mLat, -90, 90), mLon);
        auto toScreen = [&](double lat, double lon) {
            double dl = std::remainder(lon - view.centerLon, 360.0);
            return Vector2{(float)(sw / 2.0 + dl * view.pxPerDeg), (float)(sh / 2.0 - (lat - view.centerLat) * view.pxPerDeg)};
        };

        // ---- Sculpting ----
        if (activeTool > 0 && hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            painting = true;
            app.planet.beginStrokeGroup((BrushType)activeTool, hitDir, brushKm);
        }
        if (painting && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && hit) {
            double amount = (activeTool == 1 || activeTool == 2) ? strength * dt : std::min(1.0, dt * 3 * strength / 1000);
            app.planet.addDab((BrushType)activeTool, hitDir, brushKm, amount);
            map.invalidate(hitDir, brushKm * 1.1);
        }
        if (painting && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            painting = false;
            app.stats = app.planet.computeStats();
        }
        if (paintingTerritory && hit && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            double ang = brushKm / R;
            int cx, cy; World::cellOf(mLat, mLon, cx, cy);
            int ry = (int)std::ceil(brushKm / app.world.cellKm()) + 1;
            int rx = std::min(World::W / 2, (int)std::ceil(ry / std::max(0.05, std::cos(mLat * kPi / 180))) + 1);
            uint16_t v = polTool == 1 ? (uint16_t)(nationSel + 1) : 0;
            for (int dy = -ry; dy <= ry; dy++)
                for (int dx = -rx; dx <= rx; dx++) {
                    int c = World::idx(cx + dx, cy + dy);
                    if (app.world.elev[c] <= 0) continue;
                    if (angleBetween(app.world.cellDir[c], hitDir) <= ang) app.doc.owner[c] = v;
                }
            map.invalidate(hitDir, brushKm * 1.3 + 60);
            app.labelsDirty = true;
        }
        if (placingCity && hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            City c;
            c.lat = mLat; c.lon = mLon;
            c.nation = map.nationAt(mLat, mLon);
            c.culture = c.nation >= 0 ? app.doc.nations[c.nation].culture : 0;
            uint32_t ns = (uint32_t)GetRandomValue(1, 1 << 30);
            NameStyle st = c.culture >= 0 && c.culture < (int)app.doc.cultures.size() ? app.doc.cultures[c.culture].style : NameStyle::Latin;
            c.name = makeName(st, ns, true);
            int cell = World::nearestCell(mLat, mLon);
            c.population = app.world.siteScore.empty() ? 5000 : (int)(2000 + 200000 * app.world.siteScore[cell]);
            app.doc.cities.push_back(c);
            citySel = (int)app.doc.cities.size() - 1;
        }
        auto doUndo = [&]() {
            for (auto& a : app.planet.undo()) map.invalidate(a.first, a.second * 1.1);
            app.stats = app.planet.computeStats();
        };
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_Z)) doUndo();

        map.update(view, sw, sh);

        // ---- Draw the map ----
        Theme th = themeFor(map.style());
        BeginDrawing();
        ClearBackground(th.bg);
        map.draw(view, sw, sh);
        if (showRivers) {
            double kmPerPxR = 111.195 * R / 6371.0 * std::cos(view.centerLat * kPi / 180) / view.pxPerDeg;
            double thr = std::max(40.0, 2500.0 * std::pow(5.0 / view.pxPerDeg, 1.3));
            Color rc = map.style() == MapStyle::Fantasy ? Color{70, 95, 125, 255} : Color{45, 105, 200, 255};
            for (const World::River& r : app.world.rivers) {
                if (r.q.back() < thr) continue;
                Vector2 prev{0, 0}; bool havePrev = false;
                for (size_t k = 0; k < r.pts.size(); k++) {
                    const Vec3d& p = r.pts[k];
                    if (r.q[k] < thr) { havePrev = false; continue; }
                    double lat = std::asin(clampd(p.y, -1, 1)) * 180 / kPi, lon = std::atan2(p.x, p.z) * 180 / kPi;
                    Vector2 sp = toScreen(lat, lon);
                    if (havePrev && std::fabs(sp.x - prev.x) < sw / 2 &&
                        !((sp.x < 0 && prev.x < 0) || (sp.x > sw && prev.x > sw) || (sp.y < 0 && prev.y < 0) || (sp.y > sh && prev.y > sh))) {
                        double widthKm = 0.008 * std::sqrt(r.q[k]);
                        float wpx = (float)std::max(clampd(0.8 + std::log10(r.q[k] / thr + 1) * 2.0, 1.0, 4.5), widthKm / kmPerPxR);
                        DrawLineEx(prev, sp, wpx, rc);
                    }
                    prev = sp; havePrev = true;
                }
            }
        }
        if (showGrid) drawGrid(view, sw, sh, (int)(panel.x + panel.width), th.grid);
        Overlay ov = map.overlay();
        // ---- Nation names ----
        if (showLabels && ov == Overlay::Political) {
            double kmPerPxL = 111.195 * R / 6371.0 / view.pxPerDeg;
            for (size_t n = 0; n < app.doc.nations.size() && n < app.nationLabels.size(); n++) {
                const auto& lb = app.nationLabels[n];
                if (lb.areaKm2 <= 0) continue;
                float size = (float)clampd(std::sqrt(lb.areaKm2) / kmPerPxL * 0.11, 0, 64);
                if (size < 11) continue;
                Vector2 p = toScreen(lb.lat, lb.lon);
                std::string up = app.doc.nations[n].name;
                for (char& ch : up) ch = (char)std::toupper((unsigned char)ch);
                float spacing = size * 0.35f;
                Vector2 ts = MeasureTextEx(GetFontDefault(), up.c_str(), size, spacing);
                Vector2 at{p.x - ts.x / 2, p.y - ts.y / 2};
                if (at.x + ts.x < panel.x + panel.width || at.x > sw) continue;
                bool fantasy = map.style() == MapStyle::Fantasy;
                if (!fantasy) DrawTextEx(GetFontDefault(), up.c_str(), {at.x + 1.5f, at.y + 1.5f}, size, spacing, Fade(BLACK, 0.6f));
                DrawTextEx(GetFontDefault(), up.c_str(), at, size, spacing, fantasy ? Color{80, 50, 30, 220} : Fade(RAYWHITE, 0.92f));
            }
        }
        // ---- Cities ----
        if (showCities) {
            bool fantasy = map.style() == MapStyle::Fantasy;
            for (int i = 0; i < (int)app.doc.cities.size(); i++) {
                const City& c = app.doc.cities[i];
                // show bigger places first as you zoom in
                double need = c.capital ? 3.0 : (c.population > 500000 ? 8 : (c.population > 100000 ? 14 : (c.population > 20000 ? 24 : 40)));
                if (view.pxPerDeg < need && i != citySel) continue;
                Vector2 p = toScreen(c.lat, c.lon);
                if (p.x < panel.x + panel.width || p.x > sw || p.y < 0 || p.y > sh) continue;
                float r = (float)clampd(2 + std::log10(std::max(100, c.population)) - 3, 2.5, 6);
                Color fill = fantasy ? Color{60, 40, 25, 255} : RAYWHITE;
                if (c.capital) {
                    DrawCircleV(p, r + 3, BLACK); DrawCircleV(p, r + 2, fill); DrawCircleV(p, r - 0.5f, BLACK);
                } else {
                    DrawCircleV(p, r + 1, BLACK); DrawCircleV(p, r, fill);
                }
                if (i == citySel) DrawCircleLines((int)p.x, (int)p.y, r + 7, YELLOW);
                if (showLabels && view.pxPerDeg >= need * 1.6) {
                    int fsz = c.capital ? 18 : (c.population > 100000 ? 16 : 14);
                    if (!fantasy) DrawText(c.name.c_str(), (int)p.x + 9, (int)p.y - 7, fsz, BLACK);
                    DrawText(c.name.c_str(), (int)p.x + 8, (int)p.y - 8, fsz, fantasy ? Color{60, 40, 25, 255} : RAYWHITE);
                }
            }
        }
        if (showSites && tab == TabCities) {
            for (auto& st : sites) {
                Vector2 p = toScreen(st.first, st.second);
                DrawCircleLines((int)p.x, (int)p.y, 9, GREEN);
                DrawCircleLines((int)p.x, (int)p.y, 10, DARKGREEN);
            }
        }

        if (ov == Overlay::Plates) {   // plate motion arrows
            const float stepPx = 70;
            for (float sy = stepPx / 2; sy < sh; sy += stepPx)
                for (float sx = panel.x + panel.width + stepPx / 2; sx < sw; sx += stepPx) {
                    double lat = view.centerLat - (sy - sh / 2.0) / view.pxPerDeg;
                    if (std::fabs(lat) > 85) continue;
                    double lon = wrapLon(view.centerLon + (sx - sw / 2.0) / view.pxPerDeg);
                    Vec3d v = app.world.plateVelocity(World::nearestCell(lat, lon)), e, n;
                    localAxes(lat, lon, e, n);
                    float ve = (float)v.dot(e), vn = (float)v.dot(n);
                    float k = 28.f;
                    drawArrow({sx, sy}, {sx + ve * k, sy - vn * k}, Fade(BLACK, 0.7f), 2.f);
                }
        }
        if (ov == Overlay::Wind || ov == Overlay::Currents) {
            const float stepPx = 56;
            int m = ov == Overlay::Wind ? std::min(11, (int)std::round(monthUi) % 12) : 0;
            for (float sy = stepPx / 2; sy < sh; sy += stepPx)
                for (float sx = panel.x + panel.width + stepPx / 2; sx < sw; sx += stepPx) {
                    double lat = view.centerLat - (sy - sh / 2.0) / view.pxPerDeg;
                    if (std::fabs(lat) > 88) continue;
                    double lon = wrapLon(view.centerLon + (sx - sw / 2.0) / view.pxPerDeg);
                    float u, v, k;
                    if (ov == Overlay::Wind) {
                        u = (float)World::sample(app.world.windU[m], lat, lon); v = (float)World::sample(app.world.windV[m], lat, lon); k = 3.2f;
                    } else {
                        if (app.world.elev[World::nearestCell(lat, lon)] > 0) continue;
                        u = (float)World::sample(app.world.currentU, lat, lon); v = (float)World::sample(app.world.currentV, lat, lon); k = 30.f;
                    }
                    float len = std::sqrt(u * u + v * v) * k;
                    if (len < 3) continue;
                    if (len > stepPx * 0.9f) { u *= stepPx * 0.9f / len; v *= stepPx * 0.9f / len; }
                    Vector2 a{sx - u * k / 2, sy + v * k / 2}, b{sx + u * k / 2, sy - v * k / 2};
                    drawArrow(a, b, ov == Overlay::Wind ? Fade(BLACK, 0.65f) : Fade(Color{10, 40, 90, 255}, 0.85f), 2.f);
                }
        }
        if (showVolcanoes && (ov == Overlay::Plates || ov == Overlay::Rock || ov == Overlay::Earthquakes || view.pxPerDeg > 12)) {
            for (const Volcano& v : app.world.volcanoes) {
                Vector2 p = toScreen(v.lat, v.lon);
                if (p.x < panel.x + panel.width || p.x > sw || p.y < 0 || p.y > sh) continue;
                DrawTriangle({p.x, p.y - 7}, {p.x - 6, p.y + 5}, {p.x + 6, p.y + 5}, v.hotspot ? ORANGE : RED);
                DrawTriangleLines({p.x, p.y - 7}, {p.x - 6, p.y + 5}, {p.x + 6, p.y + 5}, BLACK);
            }
        }
        int hoverDeposit = -1;
        if (ov == Overlay::Resource && app.world.selectedRes >= 0) {
            const ResourceDef& d = resourceDefs()[app.world.selectedRes];
            float bestD = 12;
            for (int i = 0; i < (int)app.world.deposits.size(); i++) {
                const Deposit& dp = app.world.deposits[i];
                if (dp.res != app.world.selectedRes) continue;
                Vector2 p = toScreen(dp.lat, dp.lon);
                if (p.x < panel.x + panel.width || p.x > sw || p.y < 0 || p.y > sh) continue;
                float r = (float)clampd(2 + std::log10(std::max(1.0, dp.tonnes / d.typicalTonnes * 10)) * 3, 3, 9);
                Vector2 a{p.x, p.y - r}, b{p.x + r, p.y}, c{p.x, p.y + r}, e{p.x - r, p.y};
                Color col{d.r, d.g, d.b, 255};
                DrawTriangle(a, e, c, col); DrawTriangle(a, c, b, col);
                DrawLineV(a, b, BLACK); DrawLineV(b, c, BLACK); DrawLineV(c, e, BLACK); DrawLineV(e, a, BLACK);
                float dx = p.x - mouse.x, dy = p.y - mouse.y, dd = std::sqrt(dx * dx + dy * dy);
                if (dd < bestD) { bestD = dd; hoverDeposit = i; }
            }
        }
        if (hit && (activeTool > 0 || paintingTerritory)) {
            double kmPerDeg = 111.195 * R / 6371.0;
            float ry = (float)(brushKm / kmPerDeg * view.pxPerDeg);
            float rx = (float)(ry / std::max(0.02, std::cos(mLat * kPi / 180)));
            DrawEllipseLines((int)mouse.x, (int)mouse.y, rx, ry, map.style() == MapStyle::Fantasy ? Color{60, 40, 25, 255} : RAYWHITE);
        }

        // ---- Panel ----
        GuiPanel(panel, NULL);
        Layout L{panel.x + 14, panel.y + 12, panel.width - 28};
        GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 12) / 4, 28}, "World;Map;Sculpt;Resources\nClimate;Nations;Cities;Trade\nHistory;Lore;File", &tab);
        L.y += 104;
        const int month = (int)std::round(monthUi);

        if (tab == 0) {
            L.heading("PLANET");
            GuiLabel(Rectangle{L.x, L.y, 60, 28}, "Seed");
            if (GuiValueBox(Rectangle{L.x + 60, L.y, 130, 28}, NULL, &app.ui.seed, 0, 999999, seedEdit)) seedEdit = !seedEdit;
            bool randomClicked = GuiButton(Rectangle{L.x + 198, L.y, L.w - 198, 28}, "Random");
            L.y += 36;
            L.label(TextFormat("Radius: %.0f km", radiusUi)); L.slider(&radiusUi, 1000, 20000);
            L.label(TextFormat("Ocean cover: %.0f%%", oceanUi)); L.slider(&oceanUi, 0, 95);
            L.label(TextFormat("Mountains: %.1f", mountUi)); L.slider(&mountUi, 0, 2.5f);
            L.label(TextFormat("Continent size: %.1f", contUi)); L.slider(&contUi, 0.4f, 2.5f);
            L.heading("TECTONICS");
            L.label(TextFormat("Plates: %d", (int)platesUi)); L.slider(&platesUi, 4, 30);
            L.label(TextFormat("Tectonic activity: %.1f", activityUi)); L.slider(&activityUi, 0, 2);
            bool genClicked = L.button("Generate planet", 34);
            L.heading("STATS");
            L.label(TextFormat("Land: %.1f%%", app.stats.landPercent));
            L.label(TextFormat("Highest: %.0f m   Deepest: %.0f m", app.stats.highestM, app.stats.deepestM));
            L.label(TextFormat("Area: %.1fM km2   Plates: %zu", app.stats.areaMkm2, app.world.plates.size()));
            L.label(TextFormat("Volcanoes: %zu", app.world.volcanoes.size()));
            if (randomClicked) { app.ui.seed = GetRandomValue(0, 999999); genClicked = true; }
            if (genClicked) {
                app.ui.radiusKm = std::round(radiusUi); app.ui.oceanPercent = oceanUi; app.ui.mountains = mountUi;
                app.ui.continentSize = contUi; app.ui.plateCount = (int)platesUi; app.ui.tectonicActivity = activityUi;
                app.regenerate();
            }
        } else if (tab == 1) {
            L.heading("MAP STYLE");
            GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 8) / 3, 28}, "Realistic;Fantasy;Elevation", &styleUi);
            L.y += 38;
            L.check("Latitude/longitude grid", &showGrid);
            L.check("Volcano markers", &showVolcanoes);
            L.check("Rivers", &showRivers);
            L.heading("OVERLAY");
            GuiListView(Rectangle{L.x, L.y, L.w, 200}, "None;Tectonic plates;Rock types;Earthquake zones;Resource (see Resources tab);Temperature;Rainfall;Biomes;Wind;Ocean currents;Nations (political);Population density", &overlayScroll, &overlayUi);
            L.y += 210;
            L.check("City markers", &showCities);
            L.check("Names and labels", &showLabels);
            // legend
            if (overlayUi == 1) {
                L.legend(Color{200, 30, 30, 255}, "Converging (mountains, trenches)");
                L.legend(Color{30, 90, 210, 255}, "Spreading (rifts, ridges)");
                L.legend(Color{230, 190, 30, 255}, "Sliding (transform faults)");
                L.note("Arrows show which way each plate moves.");
            } else if (overlayUi == 2) {
                const Color pal[6] = {{70, 82, 100, 255}, {214, 188, 132, 255}, {178, 58, 48, 255}, {232, 150, 168, 255}, {132, 104, 176, 255}, {96, 140, 84, 255}};
                for (int r = 0; r < 6; r++) L.legend(pal[r], World::rockName((Rock)r));
            } else if (overlayUi == 3) {
                L.legend(Color{255, 210, 40, 255}, "Some earthquake risk");
                L.legend(Color{255, 30, 40, 255}, "High earthquake risk");
            }
        } else if (tab == 3) {
            L.heading("RESOURCES");
            GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 8) / 3, 26}, "Metals;Gems;Fuels\nIndustrial;Building", &resGroupUi);
            L.y += 62;
            const auto& defs = resourceDefs();
            std::vector<int> ids;
            std::string items;
            for (int r = 0; r < (int)defs.size(); r++)
                if ((int)defs[r].group == resGroupUi) { if (!items.empty()) items += ";"; items += defs[r].name; ids.push_back(r); }
            int prevActive = resListActive;
            GuiListView(Rectangle{L.x, L.y, L.w, 230}, items.c_str(), &resListScroll, &resListActive);
            L.y += 240;
            if (resListActive >= (int)ids.size()) resListActive = -1;
            int chosen = resListActive >= 0 ? ids[resListActive] : -1;
            if ((resListActive != prevActive || chosen != app.world.selectedRes) && chosen >= 0 && chosen != app.world.selectedRes) {
                map.waitIdle();
                app.world.selectedRes = chosen;
                app.world.selectedFavor = app.world.favorability(chosen);
                map.invalidateAll();
                overlayUi = 4;
            }
            if (chosen >= 0) {
                const ResourceDef& d = defs[chosen];
                int count = 0; double biggest = 0;
                for (const Deposit& dp : app.world.deposits) if (dp.res == chosen) { count++; biggest = std::max(biggest, dp.tonnes); }
                L.label(TextFormat("%d deposits, %s in total", count, formatTonnes(app.world.totalTonnes(chosen))));
                L.label(TextFormat("Largest: %s", formatTonnes(biggest)));
                L.note(TextFormat("Forms in: %s.", d.setting));
                L.note("Coloured areas show where the geology suits it; diamonds mark deposits. Hover a deposit for details.");
            } else {
                L.note("Pick a resource to see where it forms and where the deposits are.");
            }
        } else if (tab == 4) {
            L.heading("CLIMATE");
            L.label(TextFormat("Axial tilt: %.1f degrees", tiltUi)); L.slider(&tiltUi, 0, 60);
            if (L.button("Recalculate climate", 30)) { app.ui.axialTilt = tiltUi; app.recalcClimate(); }
            L.label(TextFormat("Month: %s", kMonths[month])); L.slider(&monthUi, 0, 12);
            L.heading("SHOW");
            int prevView = climateViewUi;
            GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 8) / 3, 28}, "Temperature;Rainfall;Biomes\nWind;Currents", &climateViewUi);
            L.y += 66;
            if (climateViewUi != prevView && climateViewUi >= 0) overlayUi = 5 + climateViewUi;
            if (overlayUi == 5) {
                const Color c[7] = {{120, 40, 160, 255}, {40, 70, 220, 255}, {90, 200, 230, 255}, {80, 190, 90, 255}, {240, 220, 60, 255}, {240, 120, 40, 255}, {180, 20, 30, 255}};
                const char* t[7] = {"-40 C", "-20 C", "0 C", "10 C", "20 C", "30 C", "40 C"};
                for (int i = 0; i < 7; i++) L.legend(c[i], t[i]);
            } else if (overlayUi == 6) {
                const Color c[5] = {{170, 110, 50, 255}, {230, 200, 90, 255}, {120, 190, 80, 255}, {40, 150, 160, 255}, {30, 60, 190, 255}};
                const char* t[5] = {"Under 100 mm/yr (desert)", "250 mm/yr (dry)", "700 mm/yr", "1,500 mm/yr (wet)", "3,000+ mm/yr (very wet)"};
                for (int i = 0; i < 5; i++) L.legend(c[i], t[i]);
            } else if (overlayUi == 7) {
                for (int b = 1; b < (int)Biome::Count; b++) { unsigned char rgb[3]; biomeColor((Biome)b, rgb); L.legend(Color{rgb[0], rgb[1], rgb[2], 255}, biomeName((Biome)b)); }
            } else if (overlayUi == 8) {
                L.note("Arrows show the prevailing wind for the chosen month. Brighter areas are windier. Watch the monsoons flip between summer and winter.");
            } else if (overlayUi == 9) {
                L.legend(Color{230, 70, 50, 255}, "Warm current");
                L.legend(Color{60, 140, 255, 255}, "Cold current");
                L.note("Arrows show surface currents. Cold currents make coastal deserts; warm ones mild, wet coasts.");
            }
        } else if (tab == TabFile) {
            L.heading("SAVE");
            GuiLabel(Rectangle{L.x, L.y, 60, 28}, "Name");
            if (GuiTextBox(Rectangle{L.x + 60, L.y, L.w - 60, 28}, nameBuf, 63, nameEdit)) nameEdit = !nameEdit;
            L.y += 36;
            if (L.button("Save world")) {
                std::string err, name = nameBuf;
                for (char& ch : name) if (std::strchr("\\/:*?\"<>|", ch)) ch = '_';
                if (name.empty()) name = "untitled";
                if (saveWorld(worldsFolder() + "/" + name + ".world", app.makeSave(), err)) setStatus("Saved " + name);
                else setStatus(err);
                savedWorlds = listSavedWorlds();
            }
            L.check("Autosave every 3 minutes", &autosave);
            L.heading("LOAD");
            std::string items;
            for (auto& w : savedWorlds) { if (!items.empty()) items += ";"; items += w; }
            GuiListView(Rectangle{L.x, L.y, L.w, 150}, items.c_str(), &savedScroll, &savedActive);
            L.y += 160;
            if (L.button("Load selected world") && savedActive >= 0 && savedActive < (int)savedWorlds.size()) {
                SaveData d; std::string err;
                if (loadWorld(worldsFolder() + "/" + savedWorlds[savedActive] + ".world", d, err)) {
                    app.applySave(d);
                    syncSliders();
                    std::strncpy(nameBuf, savedWorlds[savedActive].c_str(), 63);
                    setStatus("Loaded " + savedWorlds[savedActive]);
                } else setStatus(err);
            }
            L.heading("EXPORT");
            if (L.button("Screenshot of this view (PNG)")) pendingExport = 1;
            if (L.button("Whole world map image (PNG)")) pendingExport = 2;
            if (L.button("Heightmap (16-bit RAW + PNG)")) pendingExport = 3;
            if (L.button("Resource deposits (CSV)")) pendingExport = 4;
            L.note(("Files go in: " + exportsFolder()).c_str());
        } else if (tab == TabNations) {
            L.heading("GENERATE");
            L.label(TextFormat("Number of nations: %d", (int)nationCountUi)); L.slider(&nationCountUi, 3, 120);
            L.label(TextFormat("Cities per nation: x%.1f", cityDensityUi)); L.slider(&cityDensityUi, 0.2f, 3);
            if (L.button("Generate nations, cultures and cities")) {
                map.waitIdle();
                generateCultures(app.doc, std::max(3, (int)nationCountUi / 6), (uint32_t)app.ui.seed + 5u);
                generateNations(app.doc, app.world, (int)nationCountUi, (uint32_t)app.ui.seed + 17u);
                generateCities(app.doc, app.world, cityDensityUi, (uint32_t)app.ui.seed + 29u);
                app.labelsDirty = true; nationSel = -1; citySel = -1;
                overlayUi = 10;
                map.invalidateAll();
            }
            L.heading("NATIONS");
            std::string items;
            for (auto& n : app.doc.nations) { if (!items.empty()) items += ";"; items += cleanList(n.name); }
            GuiListView(Rectangle{L.x, L.y, L.w, 130}, items.c_str(), &nationScroll, &nationSel);
            L.y += 138;
            if (nationSel >= (int)app.doc.nations.size()) nationSel = -1;
            float bw = (L.w - 8) / 2;
            if (GuiButton(Rectangle{L.x, L.y, bw, 28}, "New nation")) {
                Nation n; uint32_t ns = (uint32_t)GetRandomValue(1, 1 << 30);
                if (app.doc.cultures.empty()) generateCultures(app.doc, 4, (uint32_t)app.ui.seed + 5u);
                n.culture = GetRandomValue(0, (int)app.doc.cultures.size() - 1);
                n.name = makeName(app.doc.cultures[n.culture].style, ns, true);
                randomColor(n.r, n.g, n.b);
                app.doc.nations.push_back(n);
                nationSel = (int)app.doc.nations.size() - 1;
                polTool = 1; overlayUi = 10; app.labelsDirty = true;
            }
            if (GuiButton(Rectangle{L.x + bw + 8, L.y, bw, 28}, "Delete nation") && nationSel >= 0) {
                map.waitIdle();
                app.doc.removeNation(nationSel);
                nationSel = -1; app.labelsDirty = true;
                map.invalidateAll();
            }
            L.y += 36;
            if (nationSel != lastNationSel) {
                lastNationSel = nationSel; nationNameEdit = nationNotesEdit = false;
                if (nationSel >= 0) {
                    std::strncpy(nationNameBuf, app.doc.nations[nationSel].name.c_str(), 63);
                    std::strncpy(nationNotesBuf, app.doc.nations[nationSel].notes.c_str(), 255);
                }
            }
            if (nationSel >= 0) {
                Nation& n = app.doc.nations[nationSel];
                GuiLabel(Rectangle{L.x, L.y, 60, 28}, "Name");
                if (GuiTextBox(Rectangle{L.x + 60, L.y, L.w - 60 - 36, 28}, nationNameBuf, 63, nationNameEdit)) nationNameEdit = !nationNameEdit;
                if (nationNameEdit || n.name != nationNameBuf) { n.name = nationNameBuf; app.labelsDirty = true; }
                DrawRectangle((int)(L.x + L.w - 28), (int)L.y, 28, 28, Color{n.r, n.g, n.b, 255});
                L.y += 34;
                if (GuiButton(Rectangle{L.x, L.y, bw, 26}, "New colour")) { map.waitIdle(); randomColor(n.r, n.g, n.b); map.invalidateAll(); }
                int gov = n.government;
                GuiComboBox(Rectangle{L.x + bw + 8, L.y, bw, 26}, "Kingdom;Empire;Republic;Theocracy;Tribal conf.;Merchant league;Dictatorship;Democracy;City-state;Magocracy", &gov);
                n.government = gov;
                L.y += 32;
                std::string cults;
                for (auto& c : app.doc.cultures) { if (!cults.empty()) cults += ";"; cults += cleanList(c.name) + " (" + nameStyleName(c.style) + ")"; }
                if (!app.doc.cultures.empty()) {
                    GuiLabel(Rectangle{L.x, L.y, 70, 26}, "Culture");
                    GuiComboBox(Rectangle{L.x + 70, L.y, L.w - 70, 26}, cults.c_str(), &n.culture);
                    L.y += 32;
                }
                GuiLabel(Rectangle{L.x, L.y, 60, 28}, "Notes");
                if (GuiTextBox(Rectangle{L.x + 60, L.y, L.w - 60, 28}, nationNotesBuf, 255, nationNotesEdit)) nationNotesEdit = !nationNotesEdit;
                n.notes = nationNotesBuf;
                L.y += 34;
                std::vector<double> area; std::vector<long long> pop;
                nationStats(app.doc, app.world, area, pop);
                int ncity = 0; std::string cap = "none";
                for (auto& c : app.doc.cities) if (c.nation == nationSel) { ncity++; if (c.capital) cap = c.name; }
                L.label(TextFormat("Area: %.0f km2   Cities: %d", area[nationSel], ncity));
                L.label(TextFormat("Population: %lld   Capital: %s", pop[nationSel], cap.c_str()));
                L.heading("TERRITORY BRUSH");
                GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 8) / 3, 28}, "Off;Paint;Erase", &polTool);
                L.y += 36;
                L.label(TextFormat("Brush radius: %s", formatKm(brushKm))); L.slider(&brushT, 0, 1);
            } else {
                polTool = 0;
                L.note("Select a nation to rename it, change its colour, government and culture, or paint its territory.");
            }
        } else if (tab == TabCities) {
            L.heading("CITIES");
            GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 4) / 2, 28}, "Select;Place new city", &cityTool);
            L.y += 36;
            bool prevSites = showSites;
            L.check("Show good sites for new cities", &showSites);
            if (showSites && (!prevSites || sites.empty())) sites = suggestSites(app.doc, app.world, 40);
            std::vector<int> order;
            for (int i = 0; i < (int)app.doc.cities.size(); i++) order.push_back(i);
            std::sort(order.begin(), order.end(), [&](int a, int b) { return app.doc.cities[a].population > app.doc.cities[b].population; });
            std::string items;
            int activeRow = -1;
            for (size_t k = 0; k < order.size(); k++) {
                const City& c = app.doc.cities[order[k]];
                if (!items.empty()) items += ";";
                items += cleanList(c.name) + (c.capital ? " *" : "");
                if (order[k] == citySel) activeRow = (int)k;
            }
            int prevRow = activeRow;
            GuiListView(Rectangle{L.x, L.y, L.w, 170}, items.c_str(), &cityScroll, &activeRow);
            L.y += 178;
            if (activeRow != prevRow) citySel = activeRow >= 0 && activeRow < (int)order.size() ? order[activeRow] : -1;
            if (citySel >= (int)app.doc.cities.size()) citySel = -1;
            if (citySel != lastCitySel) {
                lastCitySel = citySel; cityNameEdit = cityNotesEdit = false;
                if (citySel >= 0) {
                    std::strncpy(cityNameBuf, app.doc.cities[citySel].name.c_str(), 63);
                    std::strncpy(cityNotesBuf, app.doc.cities[citySel].notes.c_str(), 255);
                }
            }
            if (citySel >= 0) {
                City& c = app.doc.cities[citySel];
                GuiLabel(Rectangle{L.x, L.y, 60, 28}, "Name");
                if (GuiTextBox(Rectangle{L.x + 60, L.y, L.w - 60, 28}, cityNameBuf, 63, cityNameEdit)) cityNameEdit = !cityNameEdit;
                c.name = cityNameBuf;
                L.y += 34;
                float lp = (float)std::log10(std::max(100, c.population));
                L.label(TextFormat("Population: %d", c.population)); L.slider(&lp, 2, 7.2f);
                c.population = (int)std::pow(10.0, (double)lp);
                bool cap = c.capital;
                L.check("Capital of its nation", &cap);
                if (cap != c.capital) {
                    if (cap) for (auto& o : app.doc.cities) if (o.nation == c.nation) o.capital = false;
                    c.capital = cap;
                }
                L.label(TextFormat("Nation: %s", c.nation >= 0 && c.nation < (int)app.doc.nations.size() ? app.doc.nations[c.nation].name.c_str() : "independent"));
                L.label(TextFormat("Location: %.2f, %.2f", c.lat, c.lon));
                GuiLabel(Rectangle{L.x, L.y, 60, 28}, "Notes");
                if (GuiTextBox(Rectangle{L.x + 60, L.y, L.w - 60, 28}, cityNotesBuf, 255, cityNotesEdit)) cityNotesEdit = !cityNotesEdit;
                c.notes = cityNotesBuf;
                L.y += 34;
                float bw2 = (L.w - 8) / 2;
                if (GuiButton(Rectangle{L.x, L.y, bw2, 28}, "Go to city")) { view.centerLat = c.lat; view.centerLon = c.lon; view.pxPerDeg = std::max(view.pxPerDeg, 40.0); }
                if (GuiButton(Rectangle{L.x + bw2 + 8, L.y, bw2, 28}, "Delete city")) { app.doc.removeCity(citySel); citySel = -1; }
                L.y += 36;
            } else {
                L.note("Click a city on the map or in the list to edit it. Use 'Place new city' and click the map to found one.");
            }
        } else if (tab >= TabTrade && tab <= TabLore) {
            L.heading("COMING SOON");
            L.note("This part of the world builder is still being built. See PROGRESS.md in the project for what's done.");
        } else if (tab == 2) {
            L.heading("SCULPT");
            GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 8) / 3, 28}, "Pan;Raise;Lower\nSmooth;Flatten", &tool);
            L.y += 64;
            L.label(TextFormat("Brush radius: %s", formatKm(brushKm))); L.slider(&brushT, 0, 1);
            L.label(TextFormat("Strength: %.1f m/s", strength)); L.slider(&strengthT, 0, 1);
            if (L.button("Undo (Ctrl+Z)", 30)) doUndo();
            if (L.button("Update climate for new terrain", 30)) app.recalcClimate();
            L.note("Left-drag on the map to sculpt. Keys 1-5 switch tools, [ and ] resize the brush.");
        }
        if ((MapStyle)styleUi != map.style()) map.setStyle((MapStyle)styleUi);
        static const Overlay overlayList[] = {Overlay::None, Overlay::Plates, Overlay::Rock, Overlay::Earthquakes, Overlay::Resource,
                                              Overlay::Temperature, Overlay::Rainfall, Overlay::Biomes, Overlay::Wind, Overlay::Currents,
                                              Overlay::Political, Overlay::Population};
        const int overlayCount = (int)(sizeof(overlayList) / sizeof(overlayList[0]));
        if (overlayUi < 0) overlayUi = 0;
        {
            Overlay o = overlayList[std::min(overlayUi, overlayCount - 1)];
            bool monthly = o == Overlay::Temperature || o == Overlay::Rainfall || o == Overlay::Wind;
            map.setOverlay(o, monthly ? month : app.world.selectedRes);
        }

        // ---- Readouts ----
        const int fs = 18;
        double kmPerPx = 111.195 * R / 6371.0 * std::cos(view.centerLat * kPi / 180) / view.pxPerDeg;
        const char* info = TextFormat("1 px = %s   Tiles %d   Drawing %d   %d FPS", formatKm(kmPerPx), map.cached(), map.pending(), GetFPS());
        int iw = MeasureText(info, fs);
        DrawRectangle(sw - iw - 30, 10, iw + 20, 32, Fade(BLACK, 0.55f));
        DrawText(info, sw - iw - 20, 17, fs, RAYWHITE);
        if (hit) {
            double e = app.planet.elevation(hitDir, Planet::detailOctavesFor(std::max(0.0005, kmPerPx), R));
            int c = World::nearestCell(mLat, mLon);
            const Plate& pl = app.world.plates[app.world.plateId[c]];
            Vec3d v = app.world.plateVelocity(c);
            std::string clim = app.world.biome.empty() ? std::string("") :
                std::string(TextFormat("%s: %.0f C avg (%.0f to %.0f), %.0f mm rain/yr", biomeName((Biome)app.world.biome[c]), app.world.tempYear[c],
                    [&]{ float lo = 1e9f; for (int m = 0; m < 12; m++) lo = std::min(lo, app.world.tempMonth[m][c]); return lo; }(),
                    [&]{ float hi = -1e9f; for (int m = 0; m < 12; m++) hi = std::max(hi, app.world.tempMonth[m][c]); return hi; }(),
                    app.world.rainfallYear[c]));
            int nat = map.nationAt(mLat, mLon);
            if (nat >= 0 && nat < (int)app.doc.nations.size())
                clim += std::string("   |   ") + app.doc.nations[nat].name + " (" + kGovernments[app.doc.nations[nat].government] + ")";
            std::string lines[6] = {
                TextFormat("%.4f %c, %.4f %c", std::fabs(mLat), mLat >= 0 ? 'N' : 'S', std::fabs(mLon), mLon >= 0 ? 'E' : 'W'),
                TextFormat("%s %.0f m", e > 0 ? "Elevation" : "Depth", std::fabs(e)),
                TextFormat("Plate %d (%s), moving %.1f cm/yr", app.world.plateId[c] + 1, pl.continental ? "continental" : "oceanic", v.length() * 5),
                TextFormat("Rock: %s, %.0f million yrs", World::rockName((Rock)app.world.rock[c]), app.world.rockAge[c]),
                TextFormat("Nearest plate edge: %.0f km%s", app.world.distBoundaryKm[c],
                    app.world.discharge.empty() || app.world.elev[c] <= 0 ? "" :
                    app.world.lakeType[c] == 1 ? "   Lake" : app.world.lakeType[c] == 2 ? "   Salt flat" :
                    app.world.discharge[c] > 40 ? TextFormat("   River flow %.0f m3/s", app.world.discharge[c]) : ""), clim};
            int tw = 0;
            for (auto& s : lines) tw = std::max(tw, MeasureText(s.c_str(), fs));
            int bh = 6 * 22 + 14;
            DrawRectangle(sw - tw - 30, sh - bh - 10, tw + 20, bh, Fade(BLACK, 0.6f));
            for (int i = 0; i < 6; i++) DrawText(lines[i].c_str(), sw - tw - 20, sh - bh - 3 + i * 22, fs, RAYWHITE);
        }
        if (hoverCity >= 0 && hoverDeposit < 0) {
            const City& c = app.doc.cities[hoverCity];
            std::string t1 = c.name + (c.capital ? " (capital)" : "");
            std::string t2 = TextFormat("Population %d", c.population);
            std::string t3 = c.nation >= 0 && c.nation < (int)app.doc.nations.size() ? app.doc.nations[c.nation].name : std::string("Independent");
            int tw = std::max({MeasureText(t1.c_str(), 18), MeasureText(t2.c_str(), 18), MeasureText(t3.c_str(), 18)});
            int bx = (int)mouse.x + 16, by = (int)mouse.y + 10;
            if (bx + tw + 20 > sw) bx = (int)mouse.x - tw - 36;
            DrawRectangle(bx, by, tw + 20, 3 * 22 + 12, Fade(BLACK, 0.75f));
            DrawText(t1.c_str(), bx + 10, by + 6, 18, YELLOW);
            DrawText(t2.c_str(), bx + 10, by + 28, 18, RAYWHITE);
            DrawText(t3.c_str(), bx + 10, by + 50, 18, RAYWHITE);
        }
        if (hoverDeposit >= 0) {
            const Deposit& dp = app.world.deposits[hoverDeposit];
            const ResourceDef& d = resourceDefs()[dp.res];
            std::string t[4] = {std::string(d.name) + (dp.offshore ? " (offshore)" : ""),
                                TextFormat("Size: %s", formatTonnes(dp.tonnes)),
                                d.gradeLo >= 100 || d.group == ResGroup::Building ? std::string("")
                                    : std::string(TextFormat("Grade: %.4g%%", dp.grade)),
                                TextFormat("Depth: %.0f m", dp.depthM)};
            int tw = 0;
            for (auto& x : t) tw = std::max(tw, MeasureText(x.c_str(), 18));
            int bx = (int)mouse.x + 16, by = (int)mouse.y + 10;
            if (bx + tw + 20 > sw) bx = (int)mouse.x - tw - 36;
            DrawRectangle(bx, by, tw + 20, 4 * 22 + 12, Fade(BLACK, 0.75f));
            for (int i = 0; i < 4; i++) DrawText(t[i].c_str(), bx + 10, by + 6 + i * 22, 18, i == 0 ? YELLOW : RAYWHITE);
        }
        drawScaleBar(view, R, (int)(panel.x + panel.width) + 24, sh - 60, th.text);
        if (pendingExport > 0) {
            DrawRectangle(sw / 2 - 160, sh / 2 - 30, 320, 60, Fade(BLACK, 0.8f));
            DrawText("Exporting...", sw / 2 - MeasureText("Exporting...", 24) / 2, sh / 2 - 12, 24, RAYWHITE);
        }
        if (GetTime() - statusTime < 6) {
            int tw = MeasureText(status.c_str(), 20);
            DrawRectangle(sw / 2 - tw / 2 - 14, 54, tw + 28, 36, Fade(BLACK, 0.75f));
            DrawText(status.c_str(), sw / 2 - tw / 2, 62, 20, YELLOW);
        }
        DrawText("Scroll: zoom   Right/middle-drag or WASD: move", (int)(panel.x + panel.width) + 24, sh - 28, 16, Fade(th.text, 0.8f));
        EndDrawing();

        // ---- Exports (after the frame, so the message above is visible while it works) ----
        static int exportFrames = 0;
        if (pendingExport > 0 && ++exportFrames >= 2) {
            exportFrames = 0;
            int kind = pendingExport;
            pendingExport = 0;
            std::string base = exportsFolder() + "/" + std::string(nameBuf);
            for (char& ch : base) if (ch == ' ') ch = '_';
            if (kind == 1) {
                Image img = LoadImageFromScreen();
                std::string f = base + TextFormat("_view_%d.png", (int)GetTime());
                ExportImage(img, f.c_str());
                UnloadImage(img);
                setStatus("Saved " + f);
            } else if (kind == 2) {
                std::vector<unsigned char> px; int w, h;
                map.renderWorld(3, px, w, h);   // 8192 x 4096
                Image img{px.data(), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                if (showRivers) {
                    Color rc = map.style() == MapStyle::Fantasy ? Color{70, 95, 125, 255} : Color{45, 105, 200, 255};
                    for (const World::River& r : app.world.rivers) {
                        if (r.q.back() < 250) continue;
                        for (size_t k = 1; k < r.pts.size(); k++) {
                            if (r.q[k] < 250) continue;
                            auto pos = [&](const Vec3d& p) {
                                double lat = std::asin(clampd(p.y, -1, 1)) * 180 / kPi, lon = std::atan2(p.x, p.z) * 180 / kPi;
                                return Vector2{(float)((lon + 180) / 360 * w), (float)((90 - lat) / 180 * h)};
                            };
                            Vector2 a = pos(r.pts[k - 1]), b = pos(r.pts[k]);
                            if (std::fabs(a.x - b.x) > w / 2) continue;
                            int thick = r.q[k] > 5000 ? 3 : (r.q[k] > 1200 ? 2 : 1);
                            for (int t = 0; t < thick; t++) ImageDrawLine(&img, (int)a.x + t, (int)a.y, (int)b.x + t, (int)b.y, rc);
                        }
                    }
                }
                std::string f = base + "_world_map.png";
                ExportImage(img, f.c_str());
                setStatus("Saved " + f);
            } else if (kind == 3) {
                const int w = 4096, h = 2048;
                std::vector<float> hm((size_t)w * h);
                int oct = Planet::detailOctavesFor(2 * kPi * app.planet.radiusKm() / w, app.planet.radiusKm());
                std::vector<std::thread> th;
                unsigned n = std::max(2u, std::thread::hardware_concurrency());
                for (unsigned t = 0; t < n; t++)
                    th.emplace_back([&, t]() {
                        for (int y = (int)t; y < h; y += (int)n) {
                            double lat = 90 - (y + 0.5) * 180.0 / h;
                            for (int x = 0; x < w; x++) hm[(size_t)y * w + x] = (float)app.planet.elevation(TileMap::dirFromLatLon(lat, -180 + (x + 0.5) * 360.0 / w), oct);
                        }
                    });
                for (auto& t : th) t.join();
                float lo = 1e9f, hi = -1e9f;
                for (float v : hm) { lo = std::min(lo, v); hi = std::max(hi, v); }
                std::vector<uint16_t> raw(hm.size());
                std::vector<unsigned char> gray(hm.size());
                for (size_t i = 0; i < hm.size(); i++) {
                    double k = (hm[i] - lo) / std::max(1.0f, hi - lo);
                    raw[i] = (uint16_t)(k * 65535); gray[i] = (unsigned char)(k * 255);
                }
                std::ofstream o(base + "_heightmap_4096x2048.r16", std::ios::binary);
                o.write((const char*)raw.data(), raw.size() * 2);
                std::ofstream info(base + "_heightmap_info.txt");
                info << "4096 x 2048, 16-bit little-endian, equirectangular (longitude -180..180, latitude 90..-90)\n"
                     << "0 = " << lo << " m, 65535 = " << hi << " m\n";
                Image img{gray.data(), w, h, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE};
                ExportImage(img, (base + "_heightmap_preview.png").c_str());
                setStatus("Saved heightmap to " + exportsFolder());
            } else if (kind == 4) {
                std::ofstream o(base + "_deposits.csv");
                o << "resource,group,latitude,longitude,tonnes,grade_percent,depth_m,offshore\n";
                for (const Deposit& d : app.world.deposits) {
                    const ResourceDef& rd = resourceDefs()[d.res];
                    o << "\"" << rd.name << "\"," << resGroupName(rd.group) << "," << d.lat << "," << d.lon << "," << d.tonnes << ","
                      << d.grade << "," << d.depthM << "," << (d.offshore ? "yes" : "no") << "\n";
                }
                setStatus("Saved " + base + "_deposits.csv");
            }
        }
        if (autosave && GetTime() - lastAutosave > 180) {
            lastAutosave = GetTime();
            std::string err;
            saveWorld(worldsFolder() + "/autosave.world", app.makeSave(), err);
        }
    }

    map.shutdown();
    CloseWindow();
    return 0;
}
