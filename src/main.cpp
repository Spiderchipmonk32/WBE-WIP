// Worldbuilder: 2D world map of a round planet, with simulations layered on top.
#include "raylib.h"
#include "raygui.h"
#include "planet.h"
#include "world.h"
#include "tilemap.h"
#include "ui.h"
#include "persistence.h"
#include "politics.h"
#include "trade.h"
#include "history.h"
#include "extras.h"
#include <climits>
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

enum Tab { TabWorld, TabMap, TabSculpt, TabResources, TabClimate, TabNations, TabCities, TabTrade, TabHistory, TabLore, TabFile, TabPeoples };

struct App {
    PlanetSettings ui;
    Planet planet;
    World world;
    Document doc;
    Economy econ;
    size_t econCities = 0;
    int styleUi = 0;
    int appliedYear = INT_MIN;
    bool eventsDirty = true;
    std::vector<Stroke> lastEventStrokes;
    void applyYear() {
        if (appliedYear == doc.currentYear && !eventsDirty) return;
        std::vector<Stroke> st = buildEventStrokes(doc, doc.currentYear, planet.radiusKm());
        bool same = st.size() == lastEventStrokes.size();
        for (size_t i = 0; same && i < st.size(); i++)
            same = st[i].dir.x == lastEventStrokes[i].dir.x && st[i].dir.y == lastEventStrokes[i].dir.y && st[i].radiusKm == lastEventStrokes[i].radiusKm &&
                   st[i].amount == lastEventStrokes[i].amount && st[i].type == lastEventStrokes[i].type;
        appliedYear = doc.currentYear; eventsDirty = false;
        if (same) return;
        for (auto& s0 : lastEventStrokes) map->invalidate(s0.dir, s0.radiusKm * 1.3);
        for (auto& s1 : st) map->invalidate(s1.dir, s1.radiusKm * 1.3);
        planet.setEventStrokes(st);
        lastEventStrokes = st;
    }
    void simulateTrade() {
        econ.simulate(world, doc, (Era)std::max(0, std::min(4, doc.era)));
        econCities = doc.cities.size();
        for (auto& cr : doc.customRoutes) {
            TradeRoute r; r.from = cr.first; r.to = cr.second; r.custom = true;
            r.days = econ.findPath(world, doc, cr.first, cr.second, r.cells, r.seaShare);
            if (r.days >= 0) econ.routes.push_back(r);
        }
    }
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
        world.generateClimate(ui.axialTilt, ui.tempOffset, ui.rainMultiplier);
        planet.setClimate(&world.tempSeaLevel, &world.rainfallYear);
        world.generateRivers();
        world.generateHabitability();
        world.generateResources(ui.seed);
        doc = Document{};
        doc.ensureOwner(World::W * World::H);
        labelsDirty = true;
        econ = Economy{};
        appliedYear = INT_MIN; lastEventStrokes.clear();
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
        econ = Economy{};
        appliedYear = INT_MIN; lastEventStrokes.clear();
    }
    // After sculpting or changing the tilt: re-run the climate on the new terrain
    void recalcClimate() {
        map->waitIdle();
        world.sampleElevation(planet);
        world.generateClimate(ui.axialTilt, ui.tempOffset, ui.rainMultiplier);
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
    SetExitKey(KEY_NULL);   // Escape shouldn't close the app (it's used to stop typing)
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
    float tempOffUi = app.ui.tempOffset, rainMulUi = app.ui.rainMultiplier, seaUi = app.ui.seaLevel;
    int templateUi = 0;
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
        tempOffUi = app.ui.tempOffset; rainMulUi = app.ui.rainMultiplier; seaUi = app.ui.seaLevel;
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
    bool showRoutes = true, showEvents = true, showPins = true, typing = false;
    bool showMini = true, showCompass = false, showTitle = false, findEdit = false;
    bool showStats = false, showHelp = false, showHex = false;
    float hexKmT = 0.5f;   // log slider for hex size
    char findBuf[64] = "";
    int measureTool = 0;
    int cultureSel = -1, cultureScroll = 0, religionSel = -1, religionScroll = 0, rulerScroll = 0, rulerActive = -1, lastCultureSel = -2, lastReligionSel = -2;
    char cultureNameBuf[64] = "", religionNameBuf[96] = "", religionNotesBuf[256] = "";
    bool cultureNameEdit = false, religionNameEdit = false, religionNotesEdit = false;
    float religionCountUi = 6;
    std::vector<std::pair<double, double>> measurePts;
    int eventSel = -1, eventScroll = 0, eventTool = 0, eventTypeUi = (int)EventType::Note, lastEventSel = -2;
    char evTitleBuf[128] = "", evTextBuf[256] = "";
    bool evTitleEdit = false, evTextEdit = false;
    int loreSel = -1, loreScroll = 0, loreTool = 0, lastLoreSel = -2, loreBodyScroll = 0;
    char loreTitleBuf[128] = "", loreSearchBuf[64] = "";
    bool loreTitleEdit = false, loreBodyEdit = false, loreSearchEdit = false;
    int goodSel = -1, goodScroll = 0, routeSel = -1, routeScroll = 0, routeTool = 0, routePickA = -1;
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
        const bool placingEvent = tab == TabHistory && eventTool == 1;
        const bool pinningLore = tab == TabLore && loreTool == 1 && loreSel >= 0;
        const bool measuring = tab == TabMap && measureTool == 1;
        const bool anyTool = activeTool > 0 || paintingTerritory || placingCity || placingEvent || pinningLore || measuring;
        const Rectangle miniRect{(float)sw - 270, 50, 256, 128};
        const bool overMini = showMini && CheckCollisionPointRec(mouse, miniRect);
        app.applyYear();
        const int year = app.doc.currentYear;
        // events and lore pins under the mouse
        int hoverEvent = -1, hoverPin = -1;
        if (!overPanel) {
            float best = 10;
            for (int i = 0; i < (int)app.doc.events.size() && (showEvents || tab == TabHistory); i++) {
                const HistEvent& e = app.doc.events[i];
                if (e.year > year) continue;
                double dl = std::remainder(e.lon - view.centerLon, 360.0);
                float x = (float)(sw / 2.0 + dl * view.pxPerDeg), y = (float)(sh / 2.0 - (e.lat - view.centerLat) * view.pxPerDeg);
                float d = std::hypot(x - mouse.x, y - mouse.y);
                if (d < best) { best = d; hoverEvent = i; }
            }
            best = 10;
            for (int i = 0; i < (int)app.doc.articles.size() && showPins; i++) {
                const Article& a = app.doc.articles[i];
                if (!a.pinned) continue;
                double dl = std::remainder(a.lon - view.centerLon, 360.0);
                float x = (float)(sw / 2.0 + dl * view.pxPerDeg), y = (float)(sh / 2.0 - (a.lat - view.centerLat) * view.pxPerDeg);
                float d = std::hypot(x - mouse.x, y - mouse.y);
                if (d < best) { best = d; hoverPin = i; }
            }
        }
        app.updateLabels();
        if (app.econ.valid && app.doc.cities.size() != app.econCities) app.econ.valid = false;
        const bool pickingRoute = tab == TabTrade && routeTool == 1;
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
        if (!overPanel && pickingRoute && hoverCity >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            clickedCity = true;
            if (routePickA < 0) { routePickA = hoverCity; setStatus("Now click the destination city"); }
            else if (hoverCity != routePickA) {
                app.doc.customRoutes.push_back({routePickA, hoverCity});
                TradeRoute r; r.from = routePickA; r.to = hoverCity; r.custom = true;
                if (!app.econ.valid) app.simulateTrade();
                r.days = app.econ.findPath(app.world, app.doc, routePickA, hoverCity, r.cells, r.seaShare);
                if (r.days >= 0) { app.econ.routes.push_back(r); setStatus(TextFormat("Route added: %.0f days", r.days)); }
                else setStatus("No way to get there in this era");
                routePickA = -1;
            }
        } else if (!overPanel && !anyTool && hoverPin >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            loreSel = hoverPin; clickedCity = true; tab = TabLore;
        } else if (!overPanel && !anyTool && hoverEvent >= 0 && hoverCity < 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            eventSel = hoverEvent; clickedCity = true; tab = TabHistory;
        } else if (!overPanel && !anyTool && hoverCity >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            citySel = hoverCity; clickedCity = true;
            if (tab != TabCities && tab != TabTrade) tab = TabCities;
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
        if (IsKeyPressed(KEY_F1)) showHelp = !showHelp;
        if (!typing) {
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
        bool hit = !overPanel && std::fabs(mLat) <= 90 && !(showMini && CheckCollisionPointRec(mouse, Rectangle{(float)sw - 270, 50, 256, 128}));
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
        if (placingEvent && hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            HistEvent e; e.type = (EventType)eventTypeUi; e.year = year; e.lat = mLat; e.lon = mLon;
            e.radiusKm = e.type == EventType::Meteor ? 25 : (e.type == EventType::Eruption ? 30 : (eventChangesTerrain(e.type) ? 120 : 50));
            e.title = eventTypeName(e.type);
            app.doc.events.push_back(e);
            eventSel = (int)app.doc.events.size() - 1;
            app.eventsDirty = true;
            eventTool = 0;
        }
        if (pinningLore && hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Article& a = app.doc.articles[loreSel];
            a.lat = mLat; a.lon = mLon; a.pinned = true;
            loreTool = 0;
        }
        if (overMini && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !painting) {
            view.centerLon = -180 + (mouse.x - miniRect.x) / miniRect.width * 360;
            view.centerLat = 90 - (mouse.y - miniRect.y) / miniRect.height * 180;
            panning = false;
        } else if (measuring && hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            measurePts.push_back({mLat, mLon});
        }
        if (measuring && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) measurePts.clear();
        auto doUndo = [&]() {
            for (auto& a : app.planet.undo()) map.invalidate(a.first, a.second * 1.1);
            app.stats = app.planet.computeStats();
        };
        if (!typing && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_Z)) doUndo();

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
        if (showHex) {
            // flat-topped hexes, sized in km at the centre of the view (for tabletop games)
            double hexKm = std::pow(10.0, 0.5 + hexKmT * 2.5);
            double kmPerPxH = 111.195 * R / 6371.0 * std::cos(view.centerLat * kPi / 180) / view.pxPerDeg;
            double rpx = hexKm / 2 / kmPerPxH;   // centre to corner
            if (rpx >= 6) {
                double hw = rpx * 1.5, hh = rpx * std::sqrt(3.0);
                // anchor the grid to the world so it doesn't slide when panning
                double ox = std::fmod(-view.centerLon * view.pxPerDeg * (111.195 * R / 6371.0 * std::cos(view.centerLat * kPi / 180)) / kmPerPxH / view.pxPerDeg, hw * 2);
                double oy = std::fmod(view.centerLat * view.pxPerDeg, hh);
                Color hc = map.style() == MapStyle::Fantasy ? Color{90, 60, 35, 110} : Color{255, 255, 255, 70};
                for (int col = -2; col * hw < sw + hw * 2; col++) {
                    double cx = sw / 2.0 + ox + (col - (int)(sw / 2.0 / hw) - 2) * hw;
                    for (int row = -2; row * hh < sh + hh * 2; row++) {
                        double cy = sh / 2.0 + oy + (row - (int)(sh / 2.0 / hh) - 2) * hh + ((col & 1) ? hh / 2 : 0);
                        if (cx < panel.x + panel.width - hw) continue;
                        Vector2 pts[7];
                        for (int k = 0; k <= 6; k++) pts[k] = {(float)(cx + rpx * std::cos(k * kPi / 3)), (float)(cy + rpx * std::sin(k * kPi / 3))};
                        for (int k = 0; k < 3; k++) DrawLineV(pts[k], pts[k + 1], hc);   // three edges per hex is enough to draw the grid
                    }
                }
            }
        }
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
        // ---- Trade routes ----
        if (showRoutes && app.econ.valid && (tab == TabTrade || ov == Overlay::Political)) {
            const auto& E = app.econ;
            double minV = E.routes.size() > 300 ? E.routes[std::min<size_t>(299, E.routes.size() - 1)].value : 0;
            double maxV = E.routes.empty() ? 1 : std::max(1e-9, E.routes.front().value);
            for (int ri = 0; ri < (int)E.routes.size(); ri++) {
                const TradeRoute& r = E.routes[ri];
                bool carries = goodSel < 0;
                if (!carries) for (auto& g : r.goods) if (g.first == goodSel) { carries = true; break; }
                bool sel = ri == routeSel || (citySel >= 0 && (r.from == citySel || r.to == citySel));
                if (!r.custom && !sel && (!carries || (goodSel < 0 && r.value < minV))) continue;
                if (r.from >= (int)app.doc.cities.size() || r.to >= (int)app.doc.cities.size()) continue;
                float wpx = r.custom ? 2.5f : (float)clampd(1 + 3.5 * std::log10(1 + 9 * r.value / maxV), 1, 5);
                Vector2 prev{0, 0}; bool have = false; int k = 0;
                for (int c : r.cells) {
                    Vector2 p = toScreen(World::cellLat(c / World::W), World::cellLon(c % World::W));
                    bool water = app.world.elev[c] <= 0 || (!app.world.discharge.empty() && app.world.discharge[c] > 400);
                    Color col = r.custom ? Color{250, 210, 40, 230} : (water ? Color{30, 90, 200, 210} : Color{150, 90, 40, 210});
                    if (sel) col = Color{255, 60, 200, 240};
                    if (have && std::fabs(p.x - prev.x) < sw / 2 && !(r.custom && (k / 2) % 2 == 1)) DrawLineEx(prev, p, wpx, col);
                    prev = p; have = true; k++;
                }
            }
        }
        // ---- Cities ----
        if (showCities) {
            bool fantasy = map.style() == MapStyle::Fantasy;
            for (int i = 0; i < (int)app.doc.cities.size(); i++) {
                const City& c = app.doc.cities[i];
                if (!cityExists(c, year)) {
                    if (cityRuined(c, year) && view.pxPerDeg >= 10) {
                        Vector2 p = toScreen(c.lat, c.lon);
                        if (p.x < panel.x + panel.width || p.x > sw || p.y < 0 || p.y > sh) continue;
                        DrawLineEx({p.x - 4, p.y - 4}, {p.x + 4, p.y + 4}, 2, DARKGRAY);
                        DrawLineEx({p.x - 4, p.y + 4}, {p.x + 4, p.y - 4}, 2, DARKGRAY);
                        if (showLabels && view.pxPerDeg >= 30) DrawText((c.name + " (ruins)").c_str(), (int)p.x + 8, (int)p.y - 7, 14, DARKGRAY);
                    }
                    continue;
                }
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
        if (showEvents || tab == TabHistory) {
            for (int i = 0; i < (int)app.doc.events.size(); i++) {
                const HistEvent& e = app.doc.events[i];
                if (e.year > year) continue;
                Vector2 p = toScreen(e.lat, e.lon);
                if (p.x < panel.x + panel.width || p.x > sw || p.y < 0 || p.y > sh) continue;
                static const Color col[] = {{120, 120, 120, 255}, {255, 140, 0, 255}, {220, 50, 30, 255}, {150, 100, 50, 255}, {40, 80, 160, 255},
                                            {40, 170, 70, 255}, {40, 40, 40, 255}, {200, 20, 20, 255}, {40, 120, 220, 255}, {140, 60, 160, 255}, {170, 80, 255, 255}};
                static const char* sym[] = {"i", "*", "^", "+", "-", "F", "x", "!", "T", "~", "M"};
                int t = std::min((int)e.type, 10);
                bool recent = year - e.year < 50;
                DrawCircleV(p, 9, recent ? WHITE : Fade(WHITE, 0.6f));
                DrawCircleV(p, 7.5f, recent ? col[t] : Fade(col[t], 0.55f));
                DrawText(sym[t], (int)p.x - MeasureText(sym[t], 14) / 2, (int)p.y - 7, 14, WHITE);
                if (i == eventSel) DrawCircleLines((int)p.x, (int)p.y, 13, YELLOW);
            }
        }
        if (showPins) {
            for (int i = 0; i < (int)app.doc.articles.size(); i++) {
                const Article& a = app.doc.articles[i];
                if (!a.pinned) continue;
                Vector2 p = toScreen(a.lat, a.lon);
                if (p.x < panel.x + panel.width || p.x > sw || p.y < 0 || p.y > sh) continue;
                DrawRectangle((int)p.x - 6, (int)p.y - 8, 12, 15, Color{110, 60, 30, 255});
                DrawRectangle((int)p.x - 4, (int)p.y - 6, 9, 11, Color{240, 225, 190, 255});
                DrawLine((int)p.x - 2, (int)p.y - 3, (int)p.x + 3, (int)p.y - 3, Color{110, 60, 30, 255});
                DrawLine((int)p.x - 2, (int)p.y, (int)p.x + 3, (int)p.y, Color{110, 60, 30, 255});
                if (i == loreSel) DrawCircleLines((int)p.x, (int)p.y, 13, YELLOW);
                if (showLabels && view.pxPerDeg >= 10) {
                    DrawText(a.title.c_str(), (int)p.x + 10, (int)p.y - 6, 15, BLACK);
                    DrawText(a.title.c_str(), (int)p.x + 9, (int)p.y - 7, 15, Color{255, 235, 190, 255});
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
        GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 12) / 4, 28}, "World;Map;Sculpt;Resources\nClimate;Nations;Cities;Trade\nHistory;Lore;File;Peoples", &tab);
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
            L.label(TextFormat("Sea level: %+.0f m", seaUi)); L.slider(&seaUi, -1000, 1000);
            {
                int prevT = templateUi;
                GuiComboBox(Rectangle{L.x, L.y, L.w, 28}, "Template: custom;Template: Earth-like;Template: archipelago;Template: Pangaea;Template: desert world;Template: ice world;Template: water world;Template: hothouse", &templateUi);
                L.y += 34;
                if (templateUi != prevT && templateUi > 0) {
                    struct T { float ocean, mount, cont, plates, act, tilt, temp, rain, sea; };
                    static const T tp[] = {{70, 1, 1, 14, 1, 23.4f, 0, 1, 0}, {70, 1, 1, 14, 1, 23.4f, 0, 1, 0}, {86, 1.2f, 0.5f, 22, 1.3f, 23.4f, 2, 1.2f, 0},
                                           {62, 1, 2.5f, 9, 1, 23.4f, 1, 0.85f, 0}, {28, 1.2f, 1.2f, 14, 1, 20, 9, 0.25f, 0}, {62, 1, 1, 14, 1, 15, -22, 0.6f, 0},
                                           {95, 0.8f, 0.6f, 18, 1.2f, 23.4f, 3, 1.3f, 0}, {75, 1, 1, 14, 1, 23.4f, 13, 1.5f, 0}};
                    const T& t = tp[templateUi];
                    oceanUi = t.ocean; mountUi = t.mount; contUi = t.cont; platesUi = t.plates; activityUi = t.act; tiltUi = t.tilt;
                    tempOffUi = t.temp; rainMulUi = t.rain; seaUi = t.sea;
                    setStatus("Template set: press Generate planet");
                }
            }
            L.heading("TECTONICS");
            L.label(TextFormat("Plates: %d", (int)platesUi)); L.slider(&platesUi, 4, 30);
            L.label(TextFormat("Tectonic activity: %.1f", activityUi)); L.slider(&activityUi, 0, 2);
            bool genClicked = L.button("Generate planet", 34);
            L.heading("STATS");
            L.label(TextFormat("Land: %.1f%%", app.stats.landPercent));
            L.label(TextFormat("Highest: %.0f m   Deepest: %.0f m", app.stats.highestM, app.stats.deepestM));
            L.label(TextFormat("Area: %.1fM km2   Plates: %zu", app.stats.areaMkm2, app.world.plates.size()));
            L.label(TextFormat("Volcanoes: %zu   Rivers: %zu", app.world.volcanoes.size(), app.world.rivers.size()));
            if (L.button("Show world statistics")) showStats = true;
            L.note("Press F1 any time for help and keyboard shortcuts.");
            if (randomClicked) { app.ui.seed = GetRandomValue(0, 999999); genClicked = true; }
            if (genClicked) {
                app.ui.radiusKm = std::round(radiusUi); app.ui.oceanPercent = oceanUi; app.ui.mountains = mountUi;
                app.ui.continentSize = contUi; app.ui.plateCount = (int)platesUi; app.ui.tectonicActivity = activityUi;
                app.ui.axialTilt = tiltUi; app.ui.tempOffset = tempOffUi; app.ui.rainMultiplier = rainMulUi; app.ui.seaLevel = seaUi;
                app.regenerate();
            }
        } else if (tab == 1) {
            L.heading("MAP STYLE");
            GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 8) / 3, 28}, "Realistic;Fantasy;Elevation", &styleUi);
            L.y += 36;
            auto check2 = [&](const char* a, bool* va, const char* b, bool* vb) {
                GuiCheckBox(Rectangle{L.x, L.y, 20, 20}, a, va);
                GuiCheckBox(Rectangle{L.x + L.w / 2, L.y, 20, 20}, b, vb);
                L.y += 26;
            };
            check2("Grid", &showGrid, "Rivers", &showRivers);
            check2("Cities", &showCities, "Labels", &showLabels);
            check2("Volcanoes", &showVolcanoes, "Events", &showEvents);
            check2("Lore pins", &showPins, "Minimap", &showMini);
            check2("Compass", &showCompass, "Map title", &showTitle);
            {
                double hexKm = std::pow(10.0, 0.5 + hexKmT * 2.5);
                GuiCheckBox(Rectangle{L.x, L.y, 20, 20}, TextFormat("Hex grid (%s)", formatKm(hexKm)), &showHex);
                GuiSliderBar(Rectangle{L.x + L.w / 2 + 10, L.y + 2, L.w / 2 - 10, 16}, NULL, NULL, &hexKmT, 0, 1);
                L.y += 26;
            }
            L.heading("OVERLAY");
            GuiListView(Rectangle{L.x, L.y, L.w, 170}, "None;Tectonic plates;Rock types;Earthquake zones;Resource (see Resources tab);Temperature;Rainfall;Biomes;Wind;Ocean currents;Nations (political);Population density;Religions;Cultures", &overlayScroll, &overlayUi);
            L.y += 176;
            if (overlayUi == 1) {
                L.legend(Color{200, 30, 30, 255}, "Converging (mountains, trenches)");
                L.legend(Color{30, 90, 210, 255}, "Spreading (rifts, ridges)");
                L.legend(Color{230, 190, 30, 255}, "Sliding (transform faults)");
            } else if (overlayUi == 2) {
                const Color pal[6] = {{70, 82, 100, 255}, {214, 188, 132, 255}, {178, 58, 48, 255}, {232, 150, 168, 255}, {132, 104, 176, 255}, {96, 140, 84, 255}};
                for (int r = 0; r < 6; r++) L.legend(pal[r], World::rockName((Rock)r));
            } else if (overlayUi == 3) {
                L.legend(Color{255, 210, 40, 255}, "Some earthquake risk");
                L.legend(Color{255, 30, 40, 255}, "High earthquake risk");
            }
            L.heading("FIND");
            if (GuiTextBox(Rectangle{L.x, L.y, L.w, 28}, findBuf, 63, findEdit)) findEdit = !findEdit;
            L.y += 32;
            {
                std::string q = findBuf;
                for (char& ch : q) ch = (char)std::tolower((unsigned char)ch);
                struct Hit { std::string label; double lat, lon; };
                std::vector<Hit> hits;
                auto match = [&](const std::string& n) { std::string t = n; for (char& ch : t) ch = (char)std::tolower((unsigned char)ch); return t.find(q) != std::string::npos; };
                if (q.size() >= 2) {
                    for (size_t i = 0; i < app.doc.nations.size() && hits.size() < 6; i++)
                        if (match(app.doc.nations[i].name) && i < app.nationLabels.size()) hits.push_back({"Nation: " + app.doc.nations[i].name, app.nationLabels[i].lat, app.nationLabels[i].lon});
                    for (auto& c : app.doc.cities) if (hits.size() < 6 && match(c.name)) hits.push_back({"City: " + c.name, c.lat, c.lon});
                    for (auto& a : app.doc.articles) if (hits.size() < 6 && a.pinned && match(a.title)) hits.push_back({"Lore: " + a.title, a.lat, a.lon});
                    for (auto& e : app.doc.events) if (hits.size() < 6 && match(e.title)) hits.push_back({TextFormat("Event %d: ", e.year) + e.title, e.lat, e.lon});
                }
                for (auto& h : hits) {
                    if (GuiButton(Rectangle{L.x, L.y, L.w, 24}, h.label.c_str())) {
                        view.centerLat = h.lat; view.centerLon = h.lon; view.pxPerDeg = std::max(view.pxPerDeg, 25.0);
                    }
                    L.y += 27;
                }
                if (q.size() >= 2 && hits.empty()) L.note("Nothing found.");
            }
            L.heading("MEASURE");
            GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 4) / 2, 26}, "Off;Measure distance", &measureTool);
            L.y += 32;
            if (measureTool == 1) L.note(measurePts.empty() ? "Click points on the map. Right-click to clear." : "Keep clicking to add points; right-click to clear.");
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
            L.label(TextFormat("Global temperature: %+.0f C", tempOffUi)); L.slider(&tempOffUi, -30, 30);
            L.label(TextFormat("Rainfall: x%.2f", rainMulUi)); L.slider(&rainMulUi, 0.1f, 3);
            if (L.button("Recalculate climate", 30)) {
                app.ui.axialTilt = tiltUi; app.ui.tempOffset = tempOffUi; app.ui.rainMultiplier = rainMulUi;
                app.recalcClimate();
            }
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
        } else if (tab == TabPeoples) {
            L.heading("CULTURES");
            std::string items;
            for (auto& c : app.doc.cultures) { if (!items.empty()) items += ";"; items += cleanList(c.name) + " (" + nameStyleName(c.style) + ")"; }
            GuiListView(Rectangle{L.x, L.y, L.w, 100}, items.c_str(), &cultureScroll, &cultureSel);
            L.y += 106;
            if (cultureSel >= (int)app.doc.cultures.size()) cultureSel = -1;
            if (cultureSel != lastCultureSel) { lastCultureSel = cultureSel; cultureNameEdit = false; if (cultureSel >= 0) std::strncpy(cultureNameBuf, app.doc.cultures[cultureSel].name.c_str(), 63); }
            float bw2 = (L.w - 8) / 2;
            if (cultureSel >= 0) {
                Culture& c = app.doc.cultures[cultureSel];
                if (GuiTextBox(Rectangle{L.x, L.y, L.w - 36, 28}, cultureNameBuf, 63, cultureNameEdit)) cultureNameEdit = !cultureNameEdit;
                c.name = cultureNameBuf;
                DrawRectangle((int)(L.x + L.w - 28), (int)L.y, 28, 28, Color{c.r, c.g, c.b, 255});
                L.y += 34;
                int st = (int)c.style;
                GuiComboBox(Rectangle{L.x, L.y, bw2, 28}, "Nordic;Latin;Slavic;Eastern;Desert;Elvish;Celtic;Mesoamerican", &st);
                c.style = (NameStyle)st;
                if (GuiButton(Rectangle{L.x + bw2 + 8, L.y, bw2, 28}, "Sample names")) {
                    uint32_t ns = (uint32_t)GetRandomValue(1, 1 << 30);
                    std::string t = "Places: " + makeName(c.style, ns) + ", " + makeName(c.style, ns) + ", " + makeName(c.style, ns) +
                                    ".  People: " + makeName(c.style, ns, false) + ", " + makeName(c.style, ns, false);
                    setStatus(t);
                }
                L.y += 36;
            }
            if (GuiButton(Rectangle{L.x, L.y, bw2, 28}, "New culture")) {
                Culture c; c.style = (NameStyle)GetRandomValue(0, (int)NameStyle::Count - 1);
                uint32_t ns = (uint32_t)GetRandomValue(1, 1 << 30); c.name = makeName(c.style, ns, false);
                randomColor(c.r, c.g, c.b);
                app.doc.cultures.push_back(c); cultureSel = (int)app.doc.cultures.size() - 1;
            }
            if (GuiButton(Rectangle{L.x + bw2 + 8, L.y, bw2, 28}, "Show on map")) { overlayUi = 13; map.invalidateAll(); }
            L.y += 36;
            L.heading("RELIGIONS");
            L.label(TextFormat("Religions to create: %d", (int)religionCountUi)); L.slider(&religionCountUi, 1, 20);
            if (L.button("Generate religions") && !app.doc.cities.empty()) {
                map.waitIdle();
                generateReligions(app.doc, app.world, (int)religionCountUi, (uint32_t)GetRandomValue(1, 1 << 30));
                overlayUi = 12; religionSel = -1; map.invalidateAll();
            }
            items.clear();
            for (auto& r : app.doc.religions) { if (!items.empty()) items += ";"; items += cleanList(r.name); }
            GuiListView(Rectangle{L.x, L.y, L.w, 100}, items.c_str(), &religionScroll, &religionSel);
            L.y += 106;
            if (religionSel >= (int)app.doc.religions.size()) religionSel = -1;
            if (religionSel != lastReligionSel) {
                lastReligionSel = religionSel; religionNameEdit = religionNotesEdit = false;
                if (religionSel >= 0) {
                    std::strncpy(religionNameBuf, app.doc.religions[religionSel].name.c_str(), 95);
                    std::strncpy(religionNotesBuf, app.doc.religions[religionSel].notes.c_str(), 255);
                }
            }
            if (religionSel >= 0) {
                Religion& r = app.doc.religions[religionSel];
                if (GuiTextBox(Rectangle{L.x, L.y, L.w - 36, 28}, religionNameBuf, 95, religionNameEdit)) religionNameEdit = !religionNameEdit;
                r.name = religionNameBuf;
                DrawRectangle((int)(L.x + L.w - 28), (int)L.y, 28, 28, Color{r.r, r.g, r.b, 255});
                L.y += 34;
                if (GuiTextBox(Rectangle{L.x, L.y, L.w, 28}, religionNotesBuf, 255, religionNotesEdit)) religionNotesEdit = !religionNotesEdit;
                r.notes = religionNotesBuf;
                L.y += 34;
                int followers = 0;
                for (auto f : app.doc.faith) if ((int)f - 1 == religionSel) followers++;
                L.label(TextFormat("Founded %d, about %.1fM km2 of followers", r.founded, followers * app.world.cellKm() * app.world.cellKm() * 0.7 / 1e6));
            }
            L.heading("RULERS");
            if (nationSel >= 0 && nationSel < (int)app.doc.nations.size()) {
                const Nation& n = app.doc.nations[nationSel];
                L.label(TextFormat("Rulers of %s (pick a nation in the Nations tab)", n.name.c_str()));
                std::string rl;
                for (auto& r : n.rulers) { if (!rl.empty()) rl += ";"; rl += TextFormat("%d to %d: ", r.start, r.end); rl += cleanList(r.name); }
                GuiListView(Rectangle{L.x, L.y, L.w, 110}, rl.c_str(), &rulerScroll, &rulerActive);
                L.y += 116;
                if (L.button("Generate rulers for all nations")) generateRulers(app.doc, (uint32_t)GetRandomValue(1, 1 << 30));
            } else L.note("Select a nation in the Nations tab to see its line of rulers.");
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
                generateReligions(app.doc, app.world, std::max(2, (int)nationCountUi / 7), (uint32_t)app.ui.seed + 41u);
                generateRulers(app.doc, (uint32_t)app.ui.seed + 53u);
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
                drawFlag(Rectangle{L.x, L.y, 66, 42}, n);
                GuiLabel(Rectangle{L.x + 76, L.y + 10, L.w - 76, 22}, TextFormat("Flag of %s", n.name.c_str()));
                L.y += 50;
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
                if (const Ruler* r = app.doc.rulerIn(nationSel, app.doc.currentYear))
                    L.label(TextFormat("Ruler in %d: %s", app.doc.currentYear, r->name.c_str()));
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
                if (app.econ.valid) {
                    auto ex = app.econ.topExports(citySel, 3), im = app.econ.topImports(citySel, 3);
                    std::string es, is;
                    for (auto& g : ex) { if (!es.empty()) es += ", "; es += goodDefs()[g.first].name; }
                    for (auto& g : im) { if (!is.empty()) is += ", "; is += goodDefs()[g.first].name; }
                    L.note(("Exports: " + (es.empty() ? std::string("nothing") : es)).c_str());
                    L.note(("Imports: " + (is.empty() ? std::string("nothing") : is)).c_str());
                }
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
        } else if (tab == TabTrade) {
            L.heading("ECONOMY");
            int eraUi = app.doc.era;
            GuiComboBox(Rectangle{L.x, L.y, L.w, 28}, "Era: Ancient;Era: Medieval;Era: Early modern (sailing);Era: Industrial (rail, steam);Era: Modern", &eraUi);
            L.y += 36;
            if (eraUi != app.doc.era) { app.doc.era = eraUi; app.econ.valid = false; }
            if (app.doc.cities.empty()) L.note("Add cities first (Nations or Cities tab), then simulate trade.");
            else if (L.button(app.econ.valid ? "Re-run trade simulation" : "Simulate trade")) { app.simulateTrade(); routeSel = -1; }
            if (app.econ.valid) {
                L.label(TextFormat("%zu routes, total trade %.3g coins/yr", app.econ.routes.size(), app.econ.totalTradeValue));
                L.check("Show trade routes", &showRoutes);
                L.heading("GOODS");
                const auto& G = goodDefs();
                std::string items = "All goods";
                for (int g = 0; g < (int)G.size(); g++) { items += ";"; items += G[g].name; }
                int gs = goodSel + 1;
                GuiListView(Rectangle{L.x, L.y, L.w, 150}, items.c_str(), &goodScroll, &gs);
                L.y += 158;
                goodSel = gs - 1;
                if (goodSel >= 0) {
                    double lo = 1e30, hi = 0; int sellers = 0;
                    for (size_t c = 0; c < app.econ.price.size(); c++) {
                        lo = std::min(lo, app.econ.price[c][goodSel]); hi = std::max(hi, app.econ.price[c][goodSel]);
                        if (app.econ.production[c][goodSel] > app.econ.demand[c][goodSel]) sellers++;
                    }
                    L.label(TextFormat("Made: %s per year", formatTonnes(app.econ.totalProduction[goodSel])));
                    L.label(TextFormat("Price: %.3g to %.3g coins/t   Exporters: %d", lo, hi, sellers));
                }
                L.heading("TOP ROUTES");
                std::string ritems;
                int shown = std::min<int>(40, (int)app.econ.routes.size());
                for (int i = 0; i < shown; i++) {
                    const TradeRoute& r = app.econ.routes[i];
                    if (r.from >= (int)app.doc.cities.size() || r.to >= (int)app.doc.cities.size()) continue;
                    if (!ritems.empty()) ritems += ";";
                    ritems += cleanList(app.doc.cities[r.from].name) + " > " + cleanList(app.doc.cities[r.to].name);
                }
                GuiListView(Rectangle{L.x, L.y, L.w, 120}, ritems.c_str(), &routeScroll, &routeSel);
                L.y += 128;
                if (routeSel >= 0 && routeSel < (int)app.econ.routes.size()) {
                    const TradeRoute& r = app.econ.routes[routeSel];
                    L.label(TextFormat("%.0f days, %.0f%% by water, %s/yr", r.days, r.seaShare * 100, formatTonnes(r.tonnes)));
                    std::string g;
                    for (size_t k = 0; k < r.goods.size() && k < 4; k++) { if (k) g += ", "; g += goodDefs()[r.goods[k].first].name; }
                    L.note(("Carries: " + g).c_str());
                }
                L.heading("YOUR OWN ROUTES");
                GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 4) / 2, 28}, "Off;Add route", &routeTool);
                L.y += 36;
                if (routeTool == 1) L.note(routePickA < 0 ? "Click the starting city on the map." : "Click the destination city.");
                if (!app.doc.customRoutes.empty() && L.button("Remove all my routes")) {
                    app.doc.customRoutes.clear();
                    app.econ.routes.erase(std::remove_if(app.econ.routes.begin(), app.econ.routes.end(), [](const TradeRoute& r) { return r.custom; }), app.econ.routes.end());
                }
            }
        } else if (tab == TabHistory) {
            L.heading("TIMELINE");
            float yUi = (float)app.doc.currentYear;
            L.label(TextFormat("Year: %d", app.doc.currentYear)); L.slider(&yUi, (float)app.doc.yearMin, (float)app.doc.yearMax);
            app.doc.currentYear = (int)std::round(yUi);
            {
                float bw4 = (L.w - 12) / 4;
                const char* lbl[4] = {"-100", "-10", "+10", "+100"};
                int dlt[4] = {-100, -10, 10, 100};
                for (int k = 0; k < 4; k++)
                    if (GuiButton(Rectangle{L.x + k * (bw4 + 4), L.y, bw4, 26}, lbl[k]))
                        app.doc.currentYear = std::max(app.doc.yearMin, std::min(app.doc.yearMax, app.doc.currentYear + dlt[k]));
                L.y += 34;
            }
            L.note("The map shows the world in this year: terrain events, cities founded and fallen.");
            if (L.button("Generate a history")) {
                generateHistory(app.doc, app.world, (uint32_t)app.ui.seed + 99u);
                generateRulers(app.doc, (uint32_t)app.ui.seed + 53u);
                app.eventsDirty = true; eventSel = -1;
                setStatus(TextFormat("Created %zu events", app.doc.events.size()));
            }
            L.heading("EVENTS");
            std::string items;
            for (auto& e : app.doc.events) { if (!items.empty()) items += ";"; items += TextFormat("%d: ", e.year); items += cleanList(e.title); }
            GuiListView(Rectangle{L.x, L.y, L.w, 140}, items.c_str(), &eventScroll, &eventSel);
            L.y += 148;
            if (eventSel >= (int)app.doc.events.size()) eventSel = -1;
            GuiComboBox(Rectangle{L.x, L.y, L.w * 0.55f, 28}, "Note;Meteor impact;Volcanic eruption;Upheaval;Sinking;Founding;Fall;War;Treaty;Disaster;Magical event", &eventTypeUi);
            if (GuiButton(Rectangle{L.x + L.w * 0.55f + 6, L.y, L.w * 0.45f - 6, 28}, eventTool == 1 ? "Click the map..." : "Place on map")) eventTool = 1;
            L.y += 34;
            if (eventTool == 1) L.note("Click the map to place the event in the current year.");
            if (eventSel != lastEventSel) {
                lastEventSel = eventSel; evTitleEdit = evTextEdit = false;
                if (eventSel >= 0) {
                    std::strncpy(evTitleBuf, app.doc.events[eventSel].title.c_str(), 127);
                    std::strncpy(evTextBuf, app.doc.events[eventSel].text.c_str(), 255);
                }
            }
            if (eventSel >= 0) {
                HistEvent& e = app.doc.events[eventSel];
                L.label(TextFormat("%s in year %d", eventTypeName(e.type), e.year));
                if (GuiTextBox(Rectangle{L.x, L.y, L.w, 28}, evTitleBuf, 127, evTitleEdit)) evTitleEdit = !evTitleEdit;
                e.title = evTitleBuf; L.y += 34;
                if (GuiTextBox(Rectangle{L.x, L.y, L.w, 28}, evTextBuf, 255, evTextEdit)) evTextEdit = !evTextEdit;
                e.text = evTextBuf; L.y += 34;
                {
                    float bw4 = (L.w - 12) / 4;
                    const char* lbl[4] = {"Year -10", "-1", "+1", "+10"};
                    int dlt[4] = {-10, -1, 1, 10};
                    for (int k = 0; k < 4; k++)
                        if (GuiButton(Rectangle{L.x + k * (bw4 + 4), L.y, bw4, 26}, lbl[k])) { e.year += dlt[k]; app.eventsDirty = true; }
                    L.y += 32;
                }
                if (eventChangesTerrain(e.type)) {
                    float rT = (float)std::log10(std::max(0.5, e.radiusKm));
                    L.label(TextFormat("Size: %s", formatKm(e.radiusKm))); L.slider(&rT, -0.3f, 3);
                    double nr = std::pow(10.0, (double)rT);
                    float st = (float)e.strength;
                    L.label(TextFormat("Strength: %.1f", e.strength)); L.slider(&st, 0.2f, 3);
                    if (std::fabs(nr - e.radiusKm) > 1e-6 || std::fabs(st - e.strength) > 1e-6) { e.radiusKm = nr; e.strength = st; app.eventsDirty = true; }
                }
                float bw2 = (L.w - 8) / 2;
                if (GuiButton(Rectangle{L.x, L.y, bw2, 28}, "Go to")) { view.centerLat = e.lat; view.centerLon = e.lon; view.pxPerDeg = std::max(view.pxPerDeg, 30.0); }
                if (GuiButton(Rectangle{L.x + bw2 + 8, L.y, bw2, 28}, "Delete event")) { app.doc.events.erase(app.doc.events.begin() + eventSel); eventSel = -1; app.eventsDirty = true; }
                L.y += 36;
            }
            if (L.button("Update climate and rivers for this year")) app.recalcClimate();
        } else if (tab == TabLore) {
            L.heading("LORE");
            GuiLabel(Rectangle{L.x, L.y, 60, 28}, "Search");
            if (GuiTextBox(Rectangle{L.x + 60, L.y, L.w - 60, 28}, loreSearchBuf, 63, loreSearchEdit)) loreSearchEdit = !loreSearchEdit;
            L.y += 34;
            std::string q = loreSearchBuf;
            for (char& ch : q) ch = (char)std::tolower((unsigned char)ch);
            std::vector<int> shown;
            std::string items;
            for (int i = 0; i < (int)app.doc.articles.size(); i++) {
                const Article& a = app.doc.articles[i];
                std::string t = a.title + " " + a.body;
                for (char& ch : t) ch = (char)std::tolower((unsigned char)ch);
                if (!q.empty() && t.find(q) == std::string::npos) continue;
                shown.push_back(i);
                if (!items.empty()) items += ";";
                items += cleanList(a.title.empty() ? std::string("(untitled)") : a.title) + "  [" + loreKindName(a.kind) + "]";
            }
            int row = -1;
            for (size_t k = 0; k < shown.size(); k++) if (shown[k] == loreSel) row = (int)k;
            int prevRow = row;
            GuiListView(Rectangle{L.x, L.y, L.w, 130}, items.c_str(), &loreScroll, &row);
            L.y += 138;
            if (row != prevRow) loreSel = row >= 0 && row < (int)shown.size() ? shown[row] : -1;
            float bw2 = (L.w - 8) / 2;
            if (GuiButton(Rectangle{L.x, L.y, bw2, 28}, "New article")) {
                Article a; a.title = "New article";
                app.doc.articles.push_back(a);
                loreSel = (int)app.doc.articles.size() - 1;
            }
            if (GuiButton(Rectangle{L.x + bw2 + 8, L.y, bw2, 28}, "Delete article") && loreSel >= 0) {
                app.doc.articles.erase(app.doc.articles.begin() + loreSel); loreSel = -1;
            }
            L.y += 36;
            if (loreSel >= (int)app.doc.articles.size()) loreSel = -1;
            if (loreSel != lastLoreSel) {
                lastLoreSel = loreSel; loreTitleEdit = loreBodyEdit = false; loreBodyScroll = 0;
                if (loreSel >= 0) std::strncpy(loreTitleBuf, app.doc.articles[loreSel].title.c_str(), 127);
            }
            if (loreSel >= 0) {
                Article& a = app.doc.articles[loreSel];
                if (GuiTextBox(Rectangle{L.x, L.y, L.w, 28}, loreTitleBuf, 127, loreTitleEdit)) loreTitleEdit = !loreTitleEdit;
                a.title = loreTitleBuf; L.y += 34;
                int kind = (int)a.kind;
                GuiComboBox(Rectangle{L.x, L.y, bw2, 28}, "Place;Person;Faction;Religion;Creature;Item;Event;Other", &kind);
                a.kind = (LoreKind)kind;
                if (GuiButton(Rectangle{L.x + bw2 + 8, L.y, bw2, 28}, a.pinned ? "Move map pin" : "Pin to map")) loreTool = 1;
                L.y += 34;
                if (loreTool == 1) L.note("Click the map where this belongs.");
                float bodyH = std::max(120.f, panel.y + panel.height - L.y - 90);
                textArea(Rectangle{L.x, L.y, L.w, bodyH}, a.body, loreBodyEdit, loreBodyScroll);
                L.y += bodyH + 6;
                // [[links]] to other articles
                std::vector<std::string> links;
                size_t p0 = 0;
                while ((p0 = a.body.find("[[", p0)) != std::string::npos) {
                    size_t p1 = a.body.find("]]", p0 + 2);
                    if (p1 == std::string::npos) break;
                    links.push_back(a.body.substr(p0 + 2, p1 - p0 - 2));
                    p0 = p1 + 2;
                }
                float lx = L.x;
                for (size_t k = 0; k < links.size() && k < 6; k++) {
                    float wbtn = (float)MeasureText(links[k].c_str(), 16) + 16;
                    if (lx + wbtn > L.x + L.w) break;
                    if (GuiButton(Rectangle{lx, L.y, wbtn, 24}, links[k].c_str())) {
                        int found = -1;
                        for (int i = 0; i < (int)app.doc.articles.size(); i++) if (app.doc.articles[i].title == links[k]) found = i;
                        if (found < 0) { Article na; na.title = links[k]; app.doc.articles.push_back(na); found = (int)app.doc.articles.size() - 1; }
                        loreSel = found;
                    }
                    lx += wbtn + 6;
                }
                if (links.empty()) L.note("Tip: write [[Another Article]] to link to it.");
            } else {
                L.note("Write articles about places, people, factions, religions, creatures and items. Pin them to the map, and link them with [[Title]].");
            }
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
                                              Overlay::Political, Overlay::Population, Overlay::Religion, Overlay::Culture};
        const int overlayCount = (int)(sizeof(overlayList) / sizeof(overlayList[0]));
        if (overlayUi < 0) overlayUi = 0;
        {
            Overlay o = overlayList[std::min(overlayUi, overlayCount - 1)];
            bool monthly = o == Overlay::Temperature || o == Overlay::Rainfall || o == Overlay::Wind;
            map.setOverlay(o, monthly ? month : app.world.selectedRes);
        }

        // ---- Measuring ----
        if (!measurePts.empty() && (measureTool == 1 || tab == TabMap)) {
            double totalKm = 0;
            for (size_t k = 0; k < measurePts.size(); k++) {
                Vector2 p = toScreen(measurePts[k].first, measurePts[k].second);
                DrawCircleV(p, 4, YELLOW);
                if (k > 0) {
                    Vector2 q = toScreen(measurePts[k - 1].first, measurePts[k - 1].second);
                    if (std::fabs(p.x - q.x) < sw / 2) DrawLineEx(q, p, 2.5f, YELLOW);
                    totalKm += angleBetween(TileMap::dirFromLatLon(measurePts[k - 1].first, measurePts[k - 1].second),
                                            TileMap::dirFromLatLon(measurePts[k].first, measurePts[k].second)) * R;
                }
            }
            if (measurePts.size() >= 2) {
                Vector2 p = toScreen(measurePts.back().first, measurePts.back().second);
                std::string t1 = std::string("Distance: ") + formatKm(totalKm);
                std::string t2 = TextFormat("On foot %.1f days, horse %.1f days, ship %.1f days", totalKm / 30, totalKm / 60, totalKm / 150);
                int tw = std::max(MeasureText(t1.c_str(), 18), MeasureText(t2.c_str(), 16));
                DrawRectangle((int)p.x + 12, (int)p.y - 50, tw + 20, 50, Fade(BLACK, 0.8f));
                DrawText(t1.c_str(), (int)p.x + 22, (int)p.y - 44, 18, YELLOW);
                DrawText(t2.c_str(), (int)p.x + 22, (int)p.y - 22, 16, RAYWHITE);
            }
        }
        if (showCompass) drawCompass({panel.x + panel.width + 70, (float)sh - 150}, 42, map.style() == MapStyle::Fantasy);
        if (showTitle) {
            std::string t = nameBuf;
            for (char& ch : t) ch = (char)std::toupper((unsigned char)ch);
            float fsz = 40, spacing = 10;
            Vector2 ts = MeasureTextEx(GetFontDefault(), t.c_str(), fsz, spacing);
            float cx = (panel.x + panel.width + sw) / 2;
            bool fantasy = map.style() == MapStyle::Fantasy;
            Rectangle box{cx - ts.x / 2 - 30, 14, ts.x + 60, ts.y + 20};
            DrawRectangleRec(box, fantasy ? Color{238, 224, 186, 235} : Fade(BLACK, 0.55f));
            DrawRectangleLinesEx(box, 2, fantasy ? Color{80, 50, 30, 255} : RAYWHITE);
            DrawRectangleLinesEx({box.x + 5, box.y + 5, box.width - 10, box.height - 10}, 1, fantasy ? Color{80, 50, 30, 255} : RAYWHITE);
            DrawTextEx(GetFontDefault(), t.c_str(), {cx - ts.x / 2, 24}, fsz, spacing, fantasy ? Color{80, 50, 30, 255} : RAYWHITE);
        }
        if (showMini) {
            DrawRectangle((int)miniRect.x - 3, (int)miniRect.y - 3, (int)miniRect.width + 6, (int)miniRect.height + 6, Fade(BLACK, 0.7f));
            map.drawMini(miniRect);
            float vw = (float)(sw / view.pxPerDeg / 360 * miniRect.width), vh = (float)(sh / view.pxPerDeg / 180 * miniRect.height);
            float vx = (float)(miniRect.x + (view.centerLon + 180) / 360 * miniRect.width - vw / 2);
            float vy = (float)(miniRect.y + (90 - view.centerLat) / 180 * miniRect.height - vh / 2);
            BeginScissorMode((int)miniRect.x, (int)miniRect.y, (int)miniRect.width, (int)miniRect.height);
            DrawRectangleLinesEx({vx, vy, std::max(4.f, vw), std::max(4.f, vh)}, 2, YELLOW);
            if (vx < miniRect.x) DrawRectangleLinesEx({vx + miniRect.width, vy, std::max(4.f, vw), std::max(4.f, vh)}, 2, YELLOW);
            if (vx + vw > miniRect.x + miniRect.width) DrawRectangleLinesEx({vx - miniRect.width, vy, std::max(4.f, vw), std::max(4.f, vh)}, 2, YELLOW);
            EndScissorMode();
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
            int rel = map.religionAt(mLat, mLon);
            if (rel >= 0 && rel < (int)app.doc.religions.size()) clim += std::string(", ") + app.doc.religions[rel].name;
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
        if ((hoverEvent >= 0 || hoverPin >= 0) && hoverCity < 0 && hoverDeposit < 0) {
            std::string t1, t2;
            if (hoverPin >= 0) { const Article& a = app.doc.articles[hoverPin]; t1 = a.title; t2 = std::string(loreKindName(a.kind)) + " - click to read"; }
            else { const HistEvent& e = app.doc.events[hoverEvent]; t1 = TextFormat("%s (%d)", e.title.c_str(), e.year); t2 = e.text; }
            if (t2.size() > 80) t2 = t2.substr(0, 77) + "...";
            int tw = std::max(MeasureText(t1.c_str(), 18), MeasureText(t2.c_str(), 16));
            int bx = (int)mouse.x + 16, by = (int)mouse.y + 10;
            if (bx + tw + 20 > sw) bx = (int)mouse.x - tw - 36;
            DrawRectangle(bx, by, tw + 20, 52, Fade(BLACK, 0.8f));
            DrawText(t1.c_str(), bx + 10, by + 6, 18, YELLOW);
            DrawText(t2.c_str(), bx + 10, by + 28, 16, RAYWHITE);
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
        if (showStats) {
            Rectangle box{(float)sw / 2 - 470, 60, 940, (float)std::min(820, sh - 120)};
            DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.45f));
            DrawRectangleRec(box, Color{248, 248, 244, 255});
            DrawRectangleLinesEx(box, 2, DARKGRAY);
            DrawText("World statistics", (int)box.x + 20, (int)box.y + 16, 26, Color{40, 40, 40, 255});
            if (GuiButton(Rectangle{box.x + box.width - 110, box.y + 14, 90, 30}, "Close")) showStats = false;
            int x0 = (int)box.x + 20, y = (int)box.y + 60;
            auto text = [&](const char* t, int x, int yy, int fs = 18) { DrawText(t, x, yy, fs, Color{40, 40, 40, 255}); };
            text(TextFormat("Land %.1f%%   Highest %.0f m   Deepest %.0f m   Area %.1fM km2", app.stats.landPercent, app.stats.highestM,
                            app.stats.deepestM, app.stats.areaMkm2), x0, y);
            y += 26;
            text(TextFormat("%zu plates, %zu volcanoes, %zu rivers, %zu deposits, %zu nations, %zu cities, %zu religions, %zu events",
                            app.world.plates.size(), app.world.volcanoes.size(), app.world.rivers.size(), app.world.deposits.size(),
                            app.doc.nations.size(), app.doc.cities.size(), app.doc.religions.size(), app.doc.events.size()), x0, y, 16);
            y += 34;
            // biomes by land share
            text("Biomes (share of land)", x0, y); y += 26;
            std::vector<double> bArea((int)Biome::Count, 0); double landA = 0;
            for (int i = 0; i < World::W * World::H; i++) {
                if (app.world.biome.empty() || app.world.elev[i] <= 0) continue;
                double a = std::cos(World::cellLat(i / World::W) * kPi / 180);
                bArea[app.world.biome[i]] += a; landA += a;
            }
            int by0 = y;
            for (int b = 2; b < (int)Biome::Count; b++) {
                double f = landA > 0 ? bArea[b] / landA : 0;
                unsigned char rgb[3]; biomeColor((Biome)b, rgb);
                DrawRectangle(x0 + 190, y + 2, (int)(f * 600), 14, Color{rgb[0], rgb[1], rgb[2], 255});
                DrawText(biomeName((Biome)b), x0, y, 15, DARKGRAY);
                DrawText(TextFormat("%.1f%%", f * 100), x0 + 196 + (int)(f * 600), y, 15, DARKGRAY);
                y += 19;
            }
            (void)by0;
            y += 14;
            // top nations
            if (!app.doc.nations.empty()) {
                std::vector<double> area; std::vector<long long> pop;
                nationStats(app.doc, app.world, area, pop);
                std::vector<int> ord(app.doc.nations.size());
                for (size_t i = 0; i < ord.size(); i++) ord[i] = (int)i;
                std::sort(ord.begin(), ord.end(), [&](int a, int b) { return pop[a] > pop[b]; });
                text("Largest nations by city population", x0, y); y += 26;
                long long maxP = std::max(1LL, pop[ord[0]]);
                for (int k = 0; k < (int)ord.size() && k < 8 && y < box.y + box.height - 30; k++) {
                    const Nation& n = app.doc.nations[ord[k]];
                    DrawRectangle(x0 + 190, y + 2, (int)(500.0 * pop[ord[k]] / maxP), 14, Color{n.r, n.g, n.b, 255});
                    DrawText(n.name.c_str(), x0, y, 15, DARKGRAY);
                    DrawText(TextFormat("%lld people, %.0fk km2", pop[ord[k]], area[ord[k]] / 1000), x0 + 196 + (int)(500.0 * pop[ord[k]] / maxP), y, 15, DARKGRAY);
                    y += 19;
                }
            }
        }
        if (showHelp) {
            const char* help[] = {
                "WORLDBUILDER - HELP (F1 to close)", "",
                "Navigate:  scroll to zoom (towards the cursor), right- or middle-drag to move, WASD to move",
                "Minimap:  click or drag on it to jump anywhere",
                "Select:  click a city, event or lore pin on the map to open it",
                "Sculpt tab:  1-5 switch tools, [ and ] change brush size, Ctrl+Z undo",
                "Typing:  click a text box to type, Escape or click elsewhere to stop",
                "", "Tabs:",
                "  World - generate planets, templates, sea level, tectonics, statistics",
                "  Map - style, overlays, labels, grid, hex grid, compass, find, measure",
                "  Sculpt - raise, lower, smooth, flatten terrain; update climate afterwards",
                "  Resources - where each resource forms and every deposit",
                "  Climate - tilt, global temperature, rainfall, month, and climate overlays",
                "  Nations / Cities / Peoples - countries, territory, cities, cultures, religions, rulers",
                "  Trade - simulate the economy for an era, routes, goods and prices",
                "  History - the year slider, events that change the map, generated histories",
                "  Lore - wiki articles, map pins and [[links]]",
                "  File - save, load, autosave, export images, heightmaps and data",
            };
            int n = (int)(sizeof(help) / sizeof(help[0]));
            Rectangle box{(float)sw / 2 - 430, (float)sh / 2 - n * 13.f - 20, 860, n * 26.f + 40};
            DrawRectangleRec(box, Fade(BLACK, 0.88f));
            DrawRectangleLinesEx(box, 2, YELLOW);
            for (int i = 0; i < n; i++) DrawText(help[i], (int)box.x + 24, (int)box.y + 20 + i * 26, i == 0 ? 22 : 18, i == 0 ? YELLOW : RAYWHITE);
        }
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
        typing = seedEdit || nameEdit || nationNameEdit || nationNotesEdit || cityNameEdit || cityNotesEdit || evTitleEdit || evTextEdit ||
                 loreTitleEdit || loreBodyEdit || loreSearchEdit || findEdit || cultureNameEdit || religionNameEdit || religionNotesEdit;

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
