// Worldbuilder: 2D world map of a round planet, with simulations layered on top.
#include "raylib.h"
#include "fonts.h"
#include "raygui.h"
#include "planet.h"
#include "world.h"
#include "tilemap.h"
#include "ui.h"
#include "theme.h"
#include "flow.h"
#include "persistence.h"
#include "politics.h"
#include "trade.h"
#include "history.h"
#include "extras.h"
#include "cityeditor.h"
#include <climits>
#include <vector>
#include <thread>
#include <atomic>
#include <fstream>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <map>
#include <string>

namespace {

const char* formatKm(double km) {
    if (km < 1.0) return TextFormat("%.0f m", km * 1000);
    if (km < 100.0) return TextFormat("%.1f km", km);
    return TextFormat("%.0f km", km);
}

double wrapLon(double lon) { return lon - 360.0 * std::floor((lon + 180.0) / 360.0); }

// 1234567 -> "1,234,567"
const char* withCommas(double v) {
    static char buf[8][32]; static int k = 0;
    char* out = buf[k = (k + 1) % 8];
    char tmp[32]; snprintf(tmp, sizeof tmp, "%.0f", std::fabs(v));
    int n = (int)strlen(tmp), o = 0;
    if (v < 0) out[o++] = '-';
    for (int i = 0; i < n; i++) { out[o++] = tmp[i]; if ((n - i - 1) % 3 == 0 && i < n - 1) out[o++] = ','; }
    out[o] = 0;
    return out;
}
const char* formatTonnes(double t) {
    if (t >= 1e9) return TextFormat("%.1f billion t", t / 1e9);
    if (t >= 1e6) return TextFormat("%.1f million t", t / 1e6);
    if (t >= 1e3) return TextFormat("%.1f thousand t", t / 1e3);
    if (t >= 10) return TextFormat("%.0f t", t);
    if (t >= 1) return TextFormat("%.1f t", t);
    if (t >= 0.001) return TextFormat("%.0f kg", t * 1000);
    return TextFormat("%.0f g", t * 1e6);
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
        if (x > left) uiText(TextFormat(fmt, std::fabs(wrapLon(lon))), x + 3, std::max(yTop, 0) + 50, 14, c);
    }
    for (double lat = std::ceil(latMin / step) * step; lat <= latMax; lat += step) {
        int y = (int)(sh / 2.0 - (lat - v.centerLat) * v.pxPerDeg);
        DrawLine(0, y, sw, y, c);
        uiText(TextFormat(fmt, std::fabs(lat)), left + 6, y + 3, 14, c);
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
    uiText(formatKm(nice), x, y - 26, 18, c);
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

// Load an image file as a 0..1 greyscale grid (for the land shape)
static bool loadImportImage(const std::string& path, std::vector<float>& out, int& w, int& h) {
    if (!FileExists(path.c_str())) return false;
    Image img = LoadImage(path.c_str());
    if (!img.data) return false;
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    if (img.width > 2048) ImageResize(&img, 2048, std::max(1, img.height * 2048 / img.width));
    w = img.width; h = img.height;
    out.resize((size_t)w * h);
    const unsigned char* p = (const unsigned char*)img.data;
    for (int i = 0; i < w * h; i++) out[i] = (0.299f * p[i * 4] + 0.587f * p[i * 4 + 1] + 0.114f * p[i * 4 + 2]) / 255.f;
    UnloadImage(img);
    return true;
}

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
    // Builds the TradeRoute for one of the user's routes (path, days, goods)
    TradeRoute buildCustomRoute(int i, const std::vector<std::pair<int, double>>* fallbackGoods = nullptr) {
        TradeRoute r; r.custom = true; r.customIndex = i;
        const CustomRoute& cr = doc.customRoutes[i];
        r.from = cr.from; r.to = cr.to;
        if (cr.from < 0 || cr.to < 0 || cr.from >= (int)doc.cities.size() || cr.to >= (int)doc.cities.size()) { r.days = -1; return r; }
        std::vector<std::pair<double, double>> pts;
        pts.push_back({doc.cities[cr.from].lat, doc.cities[cr.from].lon});
        for (auto& v : cr.via) pts.push_back(v);
        pts.push_back({doc.cities[cr.to].lat, doc.cities[cr.to].lon});
        if (!econ.ready()) econ.prepare(world, (Era)std::max(0, std::min(4, doc.era)));
        econ.pathVia(world, pts, cr.mode, r);
        r.goods = !cr.goods.empty() ? cr.goods : fallbackGoods ? *fallbackGoods : std::vector<std::pair<int, double>>{};
        const auto& G = goodDefs();
        for (auto& g : r.goods) { r.tonnes += g.second; if (g.first >= 0 && g.first < (int)G.size()) r.value += g.second * G[g.first].basePrice; }
        return r;
    }
    static bool samePair(int a, int b, int c, int d) { return (a == c && b == d) || (a == d && b == c); }
    // Replace (or add) the economy's copy of custom route i
    void refreshCustomRoute(int i) {
        std::vector<std::pair<int, double>> old;
        for (auto& r : econ.routes) if (r.custom && r.customIndex == i) old = r.goods;
        econ.routes.erase(std::remove_if(econ.routes.begin(), econ.routes.end(), [&](const TradeRoute& r) { return r.custom && r.customIndex == i; }), econ.routes.end());
        TradeRoute r = buildCustomRoute(i, &old);
        if (r.days >= 0) econ.routes.push_back(r);
    }
    void simulateTrade() {
        econ.simulate(world, doc, (Era)std::max(0, std::min(4, doc.era)));
        econCities = doc.cities.size();
        // routes the user hid or replaced with their own are taken out; their goods carry over to the user's route
        std::vector<std::vector<std::pair<int, double>>> inherited(doc.customRoutes.size());
        std::vector<TradeRoute> kept;
        for (auto& r : econ.routes) {
            bool drop = false;
            for (auto& h : doc.hiddenRoutes) if (samePair(r.from, r.to, h.first, h.second)) drop = true;
            for (size_t i = 0; i < doc.customRoutes.size(); i++)
                if (samePair(r.from, r.to, doc.customRoutes[i].from, doc.customRoutes[i].to)) { drop = true; if (inherited[i].empty()) inherited[i] = r.goods; }
            if (!drop) kept.push_back(r);
        }
        econ.routes.swap(kept);
        for (int i = 0; i < (int)doc.customRoutes.size(); i++) {
            TradeRoute r = buildCustomRoute(i, &inherited[i]);
            if (r.days >= 0) econ.routes.push_back(r);
        }
    }
    bool labelsDirty = true;
    struct Label { double lat, lon, areaKm2, angle = 0, stretch = 1; };
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
        // the long axis of each territory, so names can run along it like on a printed map
        std::vector<double> sxx(sum.size(), 0), syy(sum.size(), 0), sxy(sum.size(), 0), sw(sum.size(), 0);
        for (int i = 0; i < (int)doc.owner.size(); i++) {
            int n = doc.nationAt(i);
            if (n < 0 || n >= (int)sum.size()) continue;
            double lat = World::cellLat(i / World::W), lon = World::cellLon(i % World::W);
            double dx = std::remainder(lon - nationLabels[n].lon, 360.0) * std::cos(nationLabels[n].lat * kPi / 180), dy = lat - nationLabels[n].lat;
            double w = std::cos(lat * kPi / 180);
            sxx[n] += dx * dx * w; syy[n] += dy * dy * w; sxy[n] += dx * dy * w; sw[n] += w;
        }
        for (size_t n = 0; n < sum.size(); n++) {
            if (sw[n] <= 0) continue;
            double a = sxx[n] / sw[n], b = syy[n] / sw[n], c = sxy[n] / sw[n];
            double ang = 0.5 * std::atan2(2 * c, a - b);           // principal axis (radians, east = 0, north positive)
            double l1 = (a + b) / 2 + std::sqrt(std::pow((a - b) / 2, 2.0) + c * c), l2 = (a + b) / 2 - std::sqrt(std::pow((a - b) / 2, 2.0) + c * c);
            nationLabels[n].angle = clampd(ang * 180 / kPi, -40, 40);
            nationLabels[n].stretch = l2 > 1e-9 ? std::sqrt(l1 / l2) : 3;
        }
    }
    TileMap* map = nullptr;
    PlanetStats stats;
    MapView view;

    void regenerate() {
        map->waitIdle();   // background threads must stop reading the old world first
        if (!ui.importPath.empty()) {
            std::vector<float> v; int w = 0, h = 0;
            if (loadImportImage(ui.importPath, v, w, h)) planet.setImport(v, w, h);
            else planet.clearImport();
        } else planet.clearImport();
        planet.generate(ui);
        world.generateTectonics(planet, ui.seed, ui.plateCount, ui.tectonicActivity, &ui);
        planet.setUplift(&world.uplift);
        world.sampleElevation(planet);
        world.generateClimate(ui.axialTilt, ui.tempOffset, ui.rainMultiplier);
        planet.setClimate(&world.tempSeaLevel, &world.rainfallYear);
        world.generateRivers();
        world.generateErosion(ui.erosion);
        planet.setErosion(&world.erosion);
        world.sampleElevation(planet);
        world.generateRivers();   // again, on the eroded land
        world.generateHabitability();
        std::vector<CustomResource> keepCustom = doc.customResources;   // your invented resources survive a new planet
        setCustomResources(keepCustom);
        world.generateResources(ui.seed);
        doc = Document{};
        doc.customResources = keepCustom;
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
        if (!doc.customResources.empty()) {   // place the saved fantasy resources too
            map->waitIdle();
            setCustomResources(doc.customResources);
            world.generateResources(ui.seed);
        }
        appliedYear = INT_MIN; lastEventStrokes.clear();
    }
    // After sculpting or changing the tilt: re-run the climate on the new terrain
    // Re-run the plates (after the user moved them) keeping sculpting, nations, cities and lore
    void rebuildPlates() {
        map->waitIdle();
        planet.setUplift(nullptr);
        planet.setErosion(nullptr);
        world.generateTectonics(planet, ui.seed, ui.plateCount, ui.tectonicActivity, &ui);
        planet.setUplift(&world.uplift);
        world.sampleElevation(planet);
        world.generateClimate(ui.axialTilt, ui.tempOffset, ui.rainMultiplier);
        planet.setClimate(&world.tempSeaLevel, &world.rainfallYear);
        world.generateRivers();
        world.generateErosion(ui.erosion);
        planet.setErosion(&world.erosion);
        world.sampleElevation(planet);
        world.generateRivers();
        world.generateHabitability();
        world.generateResources(ui.seed);
        if (world.selectedRes >= 0) world.selectedFavor = world.favorability(world.selectedRes);
        econ.valid = false;
        map->invalidateAll();
        stats = planet.computeStats();
    }
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
#ifdef WB_SHOTS
    InitWindow(1600, getenv("WB_H") ? atoi(getenv("WB_H")) : 900, "Worldbuilder");
#else
    InitWindow(1600, 900, "Worldbuilder");
#endif
    SetTargetFPS(144);
    SetExitKey(KEY_NULL);   // Escape shouldn't close the app (it's used to stop typing)
    fontsInit();
    GuiSetFont(fontFor(FontKind::Ui, 17));
    GuiSetStyle(DEFAULT, TEXT_SIZE, 17);
    GuiSetStyle(DEFAULT, TEXT_SPACING, 0);
    theme::apply();

    App app;
    CityEditor cityEd;
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
    float erosionUi = app.ui.erosion;
    std::string droppedPath;
    Texture2D traceTex{}; bool haveTrace = false, showTrace = true; float traceAlpha = 0.5f;
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
        tempOffUi = app.ui.tempOffset; rainMulUi = app.ui.rainMultiplier; seaUi = app.ui.seaLevel; erosionUi = app.ui.erosion;
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
    bool flowWind = false, flowCurrents = false;
    FlowLayer windFlow, curFlow; curFlow.currents = true; curFlow.rng = 999;
    bool showIntro = !FileExists("worldbuilder-intro-seen.txt");
    bool showStats = false, showHelp = false, showHex = false, globeMini = false, rotateLabels = true;
    Texture2D globeTex{}; bool haveGlobe = false; double globeLat = 999, globeLon = 999, globeTime = -10;
    std::vector<unsigned char> globePx(200 * 200 * 4, 0);
    float hexKmT = 0.5f;   // log slider for hex size
    char findBuf[64] = "";
    int measureTool = 0;
    int cultureSel = -1, cultureScroll = 0, religionSel = -1, religionScroll = 0, rulerScroll = 0, rulerActive = -1, lastCultureSel = -2, lastReligionSel = -2;
    char cultureNameBuf[64] = "", religionNameBuf[96] = "", religionNotesBuf[256] = "";
    bool cultureNameEdit = false, religionNameEdit = false, religionNotesEdit = false;
    float religionCountUi = 6;
    int relTypeUi = 0; bool relPick = false;
    int customSel = -1, lastCustomSel = -2;
    char customNameBuf[64] = "", customSettingBuf[128] = "";
    bool customNameEdit = false, customSettingEdit = false;
    int bookmarkScroll = 0, bookmarkSel = -1;
    bool showCaravans = true;
    std::vector<std::pair<double, double>> measurePts;
    int eventSel = -1, eventScroll = 0, eventTool = 0, eventTypeUi = (int)EventType::Note, lastEventSel = -2;
    char evTitleBuf[128] = "", evTextBuf[256] = "";
    bool evTitleEdit = false, evTextEdit = false;
    int loreSel = -1, loreScroll = 0, loreTool = 0, lastLoreSel = -2, loreBodyScroll = 0;
    char loreTitleBuf[128] = "", loreSearchBuf[64] = "";
    bool loreTitleEdit = false, loreBodyEdit = false, loreSearchEdit = false;
    int goodSel = -1, goodScroll = 0, routeSel = -1, routeScroll = 0, routeTool = 0, routePickA = -1;
    // route drawing / editing
    int myRouteSel = -1, myRouteScroll = 0, drawFrom = -1, dragHandle = -1, drawMode = 0, addGoodUi = 0, lastMyRouteSel = -2;
    std::vector<std::pair<double, double>> drawPts;
    bool routeNameEdit = false, addGoodOpen = false; char routeNameBuf[64] = ""; float addTonnesT = 0.4f;
    std::pair<double, double> dragStart{0, 0};
    Rectangle pendingGoodDropdown{0, 0, 0, 0}; std::string pendingGoodList;
    auto randomColor = [](unsigned char& r, unsigned char& g, unsigned char& b) {
        double hue = GetRandomValue(0, 5999) / 1000.0; int hi = (int)hue; double f = hue - hi;
        double rgb[6][3] = {{1, f, 0}, {1 - f, 1, 0}, {0, 1, f}, {0, 1 - f, 1}, {f, 0, 1}, {1, 0, 1 - f}};
        r = (unsigned char)(70 + 160 * rgb[hi][0]); g = (unsigned char)(70 + 160 * rgb[hi][1]); b = (unsigned char)(70 + 160 * rgb[hi][2]);
    };
    auto cleanList = [](std::string t) { for (char& ch : t) if (ch == ';' || ch == '\n') ch = ','; return t; };
    int climateViewUi = -1;
    float brushT = 0.80f, strengthT = 0.851f;
    float hardnessUi = 0.35f, roughUi = 0.45f, landHeightT = 0.35f, seaDepthT = 0.3f;
    std::vector<std::pair<double, double>> lassoPts; bool lassoing = false;
    bool plateMode = false; int plateSel = -1, plateDrag = 0, pendingPlateRebuild = 0; Vec3d plateDragDir{0, 0, 1};

#ifdef WB_SHOTS
    // Developer hook: render a few frames on a chosen tab, save a screenshot and quit (used for automated UI checks)
    int shotFrames = 0;
    if (const char* t = getenv("WB_TAB")) tab = atoi(t);
    if (const char* o = getenv("WB_OVERLAY")) overlayUi = atoi(o);
    if (getenv("WB_FLOW")) { flowWind = true; flowCurrents = true; }
    if (getenv("WB_DEMO")) {   // a populated world for UI checks
        map.waitIdle();
        generateCultures(app.doc, 6, (uint32_t)app.ui.seed + 5u);
        generateNations(app.doc, app.world, 40, (uint32_t)app.ui.seed + 17u);
        generateCities(app.doc, app.world, 1, (uint32_t)app.ui.seed + 29u);
        generateReligions(app.doc, app.world, 5, (uint32_t)app.ui.seed + 41u);
        generateRulers(app.doc, (uint32_t)app.ui.seed + 53u);
        generateRelations(app.doc, (uint32_t)app.ui.seed + 61u);
        app.labelsDirty = true;
        map.invalidateAll();
        if (getenv("WB_TRADE")) {
            app.simulateTrade();
            if (const char* r = getenv("WB_ROUTE")) {   // "from to lat lon" : a custom route with one waypoint
                CustomRoute cr; double la, lo;
                int got = sscanf(r, "%d %d %lf %lf", &cr.from, &cr.to, &la, &lo);
                if (got == 4) cr.via.push_back({la, lo});
                else if (got == 2) {
                    const City& A = app.doc.cities[cr.from]; const City& B = app.doc.cities[cr.to];
                    cr.via.push_back({(A.lat + B.lat) / 2 + 4, A.lon + std::remainder(B.lon - A.lon, 360.0) / 2});
                }
                view.centerLat = app.doc.cities[cr.from].lat; view.centerLon = app.doc.cities[cr.from].lon;
                cr.mode = getenv("WB_STRAIGHT") ? 1 : 0;
                app.doc.customRoutes.push_back(cr);
                app.refreshCustomRoute(0);
                myRouteSel = 0;
            }
        }
    }
    if (const char* pd = getenv("WB_PLATES")) {   // "drift plate type"
        plateMode = true; overlayUi = atoi(getenv("WB_OVERLAY") ? getenv("WB_OVERLAY") : "1");
        float drift = 0; int pl = -1, ty = 0;
        sscanf(pd, "%f %d %d", &drift, &pl, &ty);
        app.ui.driftMa = drift;
        if (pl >= 0) { app.ui.editFor(pl)->type = ty; plateSel = pl; }
        double t0 = GetTime();
        app.rebuildPlates();
        printf("PLATES rebuild %.2fs\n", GetTime() - t0);
    }
    if (const char* lz = getenv("WB_LASSO")) {   // test shapes for the brushes, around the view centre
        double cl = view.centerLat, co = view.centerLon;
        // find open ocean for the test
        for (double la = -40; la <= 40 && app.planet.elevation(TileMap::dirFromLatLon(cl, co), 3) > -2500; la += 5)
            for (double lo = -180; lo < 180; lo += 5) {
                bool ok = true;
                for (int k = 0; k < 16 && ok; k++) { double a = k * kPi / 8; if (app.planet.elevation(TileMap::dirFromLatLon(la + 14 * std::sin(a), lo + 20 * std::cos(a)), 3) > -1500) ok = false; }
                if (ok) { cl = la; co = lo; break; }
            }
        view.centerLat = cl; view.centerLon = co;
        std::vector<std::pair<double, double>> pts;
        for (int k = 0; k < 60; k++) {
            double a = k * 2 * kPi / 60, r = 9 + 4 * std::sin(3 * a) + 1.5 * std::cos(7 * a);
            pts.push_back({cl + r * std::sin(a), co + 1.4 * r * std::cos(a)});
        }
        app.planet.addLasso(BrushType::LandFill, pts, 150, 0.4, atof(lz));
        if (getenv("WB_BRUSHES")) {
        app.planet.beginStrokeGroup(BrushType::Raise, TileMap::dirFromLatLon(cl, co), 300);
        for (int k = 0; k < 40; k++) {
            double t = k / 39.0;
            app.planet.addDab(BrushType::Mountains, TileMap::dirFromLatLon(cl - 4 + 8 * t, co - 8 + 12 * t), 260, 400, 0.3, 0.4);
        }
        app.planet.beginStrokeGroup(BrushType::LandFill, TileMap::dirFromLatLon(cl + 14, co + 20), 300, 120);
        for (int k = 0; k < 30; k++) {
            double t = k / 29.0;
            app.planet.addDab(BrushType::LandFill, TileMap::dirFromLatLon(cl + 14 - 6 * t, co + 20 + 8 * std::sin(t * 5)), 350, 1, 0.35, 0.6);
        }
        }
        map.invalidateAll();
        if (getenv("WB_CLIMATE")) app.recalcClimate();
        printf("LASSO strokes=%zu elev centre=%.0f\n", app.planet.strokes().size(), app.planet.elevation(TileMap::dirFromLatLon(cl, co), 6));
    }
    if (const char* c = getenv("WB_CITY")) {
        citySel = atoi(c);
        if (getenv("WB_CITYED") && citySel < (int)app.doc.cities.size()) { map.waitIdle(); cityEd.open(citySel, app.planet, app.world, app.doc); }
        if (citySel < (int)app.doc.cities.size()) { view.centerLat = app.doc.cities[citySel].lat; view.centerLon = app.doc.cities[citySel].lon - 15; view.pxPerDeg *= 2.5; }
    }
    if (getenv("WB_POPS")) {
        for (int e = 0; e < 5; e++) {
            eraPopulations(app.doc, app.world, e);
            std::vector<int> p; long long tot = 0;
            for (auto& c : app.doc.cities) { p.push_back(c.population); tot += c.population; }
            std::sort(p.rbegin(), p.rend());
            printf("era %d: %zu cities, total %lld, top:", e, p.size(), tot);
            for (int k = 0; k < 8 && k < (int)p.size(); k++) printf(" %d", p[k]);
            printf(" median %d smallest %d\n", p[p.size() / 2], p.back());
        }
        eraPopulations(app.doc, app.world, app.doc.era);
    }
    if (const char* mo = getenv("WB_MONTH")) monthUi = (float)atof(mo);
    if (const char* z = getenv("WB_ZOOM")) view.pxPerDeg *= atof(z);
    if (const char* la = getenv("WB_LAT")) view.centerLat = atof(la);
    if (const char* lo = getenv("WB_LON")) view.centerLon = atof(lo);
#endif
    while (!WindowShouldClose()) {
        if (cityEd.active) {   // the city editor takes over the whole screen
            cityEd.frame(app.planet, app.world, app.doc);
#ifdef WB_SHOTS
            if (++shotFrames == (getenv("WB_FRAMES") ? atoi(getenv("WB_FRAMES")) : 30)) { TakeScreenshot(getenv("WB_SHOT") ? getenv("WB_SHOT") : "shot.png"); break; }
#endif
            continue;
        }
        if (IsFileDropped()) {
            FilePathList files = LoadDroppedFiles();
            if (files.count > 0) {
                std::string pth = files.paths[0];
                if (IsFileExtension(pth.c_str(), ".png;.jpg;.jpeg;.bmp;.tga")) droppedPath = pth;
                else if (IsFileExtension(pth.c_str(), ".world")) {
                    SaveData d; std::string err;
                    if (loadWorld(pth, d, err)) { app.applySave(d); syncSliders(); setStatus("Loaded " + pth); } else setStatus(err);
                }
            }
            UnloadDroppedFiles(files);
        }
        const float dt = GetFrameTime();
        const int sw = GetScreenWidth(), sh = GetScreenHeight();
        const double R = app.planet.radiusKm();
        const double brushKm = 0.05 * std::pow(60000.0, (double)brushT);
        const double strength = 0.1 * std::pow(50000.0, (double)strengthT);
        const Rectangle railRect = {0, 0, 64, (float)sh};
        const Rectangle panel = {64, 0, 350, (float)sh};
        const Vector2 mouse = GetMousePosition();
        // Quick layer chips along the top of the map
        static const struct { const char* name; int overlay; } kChips[] = {
            {"Terrain", 0}, {"Plates", 1}, {"Temperature", 5}, {"Rain", 6}, {"Biomes", 7},
            {"Wind", 8}, {"Currents", 9}, {"Nations", 10}, {"Population", 11}};
        const int chipCount = (int)(sizeof(kChips) / sizeof(kChips[0]));
        Rectangle chipRects[16];
        Rectangle chipBar{panel.x + panel.width + 12, 10, 0, 28};
        {
            float x = chipBar.x;
            for (int i = 0; i < chipCount; i++) {
                float w = measureTextK(FontKind::Ui, kChips[i].name, 14).x + 22;
                chipRects[i] = Rectangle{x, chipBar.y, w, 28};
                x += w + 4;
            }
            x += 10;
            for (int i = 0; i < 2; i++) {
                const char* nm = i == 0 ? "Wind flow" : "Current flow";
                float w = measureTextK(FontKind::Ui, nm, 14).x + 22;
                chipRects[chipCount + i] = Rectangle{x, chipBar.y, w, 28};
                x += w + 4;
            }
            chipBar.width = x - chipBar.x + 150;   // room for the month stepper
        }
        const bool overChips = CheckCollisionPointRec(mouse, chipBar);
        const bool overPanel = mouse.x < panel.x + panel.width || overChips;
        const double minZoom = sh / 180.0, maxZoom = TileMap::TILE / 90.0 * (1 << TileMap::MAX_LEVEL) * 2;
        const bool sculptTab = (tab == TabSculpt);
        const int activeTool = sculptTab ? tool : 0;
        const bool paintingTerritory = tab == TabNations && polTool > 0 && nationSel >= 0;
        const bool placingCity = tab == TabCities && cityTool == 1;
        const bool placingEvent = tab == TabHistory && eventTool == 1;
        const bool pinningLore = tab == TabLore && loreTool == 1 && loreSel >= 0;
        const bool measuring = tab == TabMap && measureTool == 1;
        const bool anyTool = activeTool > 0 || paintingTerritory || placingCity || placingEvent || pinningLore || measuring || relPick || (plateMode && tab == TabWorld);
        const Rectangle miniRect{(float)sw - 270, 50, 256, 128};   // below the layer chips
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
        bool routeConsumed = false;
        // ---- Drawing and editing trade routes (Trade tab) ----
        auto scr = [&](double lat, double lon) {
            double dl = std::remainder(lon - view.centerLon, 360.0);
            return Vector2{(float)(sw / 2.0 + dl * view.pxPerDeg), (float)(sh / 2.0 - (lat - view.centerLat) * view.pxPerDeg)};
        };
        auto mouseLatLon = [&]() {
            return std::pair<double, double>{clampd(view.centerLat - (mouse.y - sh / 2.0) / view.pxPerDeg, -89, 89),
                                             wrapLon(view.centerLon + (mouse.x - sw / 2.0) / view.pxPerDeg)};
        };
        auto segDist = [](Vector2 p, Vector2 a, Vector2 b) {
            float dx = b.x - a.x, dy = b.y - a.y, L2 = dx * dx + dy * dy;
            float t = L2 > 0 ? clampd(((p.x - a.x) * dx + (p.y - a.y) * dy) / L2, 0, 1) : 0;
            float ex = a.x + t * dx - p.x, ey = a.y + t * dy - p.y;
            return std::sqrt(ex * ex + ey * ey);
        };
        // the user's route i as handle points: from city, waypoints, to city
        auto handlesOf = [&](int i) {
            std::vector<std::pair<double, double>> h;
            if (i < 0 || i >= (int)app.doc.customRoutes.size()) return h;
            const CustomRoute& cr = app.doc.customRoutes[i];
            if (cr.from < 0 || cr.to < 0 || cr.from >= (int)app.doc.cities.size() || cr.to >= (int)app.doc.cities.size()) return h;
            h.push_back({app.doc.cities[cr.from].lat, app.doc.cities[cr.from].lon});
            for (auto& v : cr.via) h.push_back(v);
            h.push_back({app.doc.cities[cr.to].lat, app.doc.cities[cr.to].lon});
            return h;
        };
        auto econIndexOf = [&](int custom) {
            for (int k = 0; k < (int)app.econ.routes.size(); k++) if (app.econ.routes[k].custom && app.econ.routes[k].customIndex == custom) return k;
            return -1;
        };
        if (tab == TabTrade && !overPanel && !app.doc.cities.empty()) {
            if (routeTool == 1) {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    routeConsumed = true;
                    if (drawFrom < 0) {
                        if (hoverCity >= 0) { drawFrom = hoverCity; drawPts.clear(); setStatus("Click to add waypoints, then click the destination city"); }
                        else setStatus("Start on a city");
                    } else if (hoverCity >= 0 && hoverCity != drawFrom) {
                        CustomRoute cr; cr.from = drawFrom; cr.to = hoverCity; cr.via = drawPts; cr.mode = drawMode;
                        app.doc.customRoutes.push_back(cr);
                        int idx = (int)app.doc.customRoutes.size() - 1;
                        if (!app.econ.valid) app.simulateTrade(); else app.refreshCustomRoute(idx);
                        int ei = econIndexOf(idx);
                        if (ei >= 0) setStatus(TextFormat("Route added: %.0f days, %s", app.econ.routes[ei].days, formatKm(app.econ.routes[ei].km)));
                        else setStatus("No way to get there in this era - try adding waypoints or a straight route");
                        myRouteSel = idx; routeSel = ei; drawFrom = -1; drawPts.clear(); routeTool = 0;
                    } else drawPts.push_back(mouseLatLon());
                }
                if (!typing && (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_Z))) { if (!drawPts.empty()) drawPts.pop_back(); else drawFrom = -1; }
                if (IsKeyPressed(KEY_ESCAPE)) { drawFrom = -1; drawPts.clear(); routeTool = 0; }
            } else {
                auto H = handlesOf(myRouteSel);
                int hoverHandle = -1;
                for (int k = 0; k < (int)H.size(); k++) {
                    Vector2 p = scr(H[k].first, H[k].second);
                    if (std::hypot(p.x - mouse.x, p.y - mouse.y) < 9) hoverHandle = k;
                }
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if (hoverHandle >= 0) { dragHandle = hoverHandle; dragStart = H[hoverHandle]; routeConsumed = true; }
                    else {
                        // on the selected route's line: insert a waypoint there and start dragging it
                        int ei = econIndexOf(myRouteSel);
                        bool inserted = false;
                        if (ei >= 0) {
                            const TradeRoute& r = app.econ.routes[ei];
                            for (size_t k = 0; k + 1 < r.cells.size() && !inserted; k++) {
                                Vector2 a = scr(World::cellLat(r.cells[k] / World::W), World::cellLon(r.cells[k] % World::W));
                                Vector2 b = scr(World::cellLat(r.cells[k + 1] / World::W), World::cellLon(r.cells[k + 1] % World::W));
                                if (std::fabs(a.x - b.x) > sw / 2 || segDist(mouse, a, b) > 6) continue;
                                int leg = 0;
                                for (int L2 = 0; L2 < (int)r.legStart.size(); L2++) if ((int)k >= r.legStart[L2]) leg = L2;
                                auto& via = app.doc.customRoutes[myRouteSel].via;
                                leg = std::min(leg, (int)via.size());
                                via.insert(via.begin() + leg, mouseLatLon());
                                dragHandle = leg + 1; dragStart = via[leg];
                                inserted = true; routeConsumed = true;
                            }
                        }
                        if (!inserted && hoverCity < 0) {
                            // pick the route under the mouse
                            int best = -1; float bd = 6;
                            for (int ri = 0; ri < (int)app.econ.routes.size(); ri++) {
                                const TradeRoute& r = app.econ.routes[ri];
                                for (size_t k = 0; k + 1 < r.cells.size(); k++) {
                                    Vector2 a = scr(World::cellLat(r.cells[k] / World::W), World::cellLon(r.cells[k] % World::W));
                                    Vector2 b = scr(World::cellLat(r.cells[k + 1] / World::W), World::cellLon(r.cells[k + 1] % World::W));
                                    if (std::fabs(a.x - b.x) > sw / 2) continue;
                                    float d = segDist(mouse, a, b);
                                    if (d < bd) { bd = d; best = ri; }
                                }
                            }
                            if (best >= 0) {
                                routeSel = best; routeConsumed = true;
                                myRouteSel = app.econ.routes[best].custom ? app.econ.routes[best].customIndex : -1;
                            }
                        }
                    }
                }
                if (dragHandle >= 0 && myRouteSel >= 0) {
                    routeConsumed = true;
                    CustomRoute& cr = app.doc.customRoutes[myRouteSel];
                    int nv = (int)cr.via.size();
                    if (dragHandle >= 1 && dragHandle <= nv) cr.via[dragHandle - 1] = mouseLatLon();
                    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                        if (dragHandle == 0 || dragHandle == nv + 1) {
                            // dropping an end on another city moves that end of the route
                            int other = dragHandle == 0 ? cr.to : cr.from;
                            if (hoverCity >= 0 && hoverCity != other) { (dragHandle == 0 ? cr.from : cr.to) = hoverCity; }
                        }
                        app.refreshCustomRoute(myRouteSel);
                        routeSel = econIndexOf(myRouteSel);
                        dragHandle = -1;
                    }
                }
                if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && hoverHandle >= 1 && myRouteSel >= 0 && hoverHandle <= (int)app.doc.customRoutes[myRouteSel].via.size()) {
                    auto& via = app.doc.customRoutes[myRouteSel].via;
                    via.erase(via.begin() + (hoverHandle - 1));
                    app.refreshCustomRoute(myRouteSel);
                    routeSel = econIndexOf(myRouteSel);
                    routeConsumed = true;
                }
            }
        }
        if (routeConsumed) {
            clickedCity = true;
        } else if (!overPanel && pickingRoute && hoverCity >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            clickedCity = true;
        } else if (!overPanel && !anyTool && hoverPin >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            loreSel = hoverPin; clickedCity = true; tab = TabLore;
        } else if (!overPanel && !anyTool && hoverEvent >= 0 && hoverCity < 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            eventSel = hoverEvent; clickedCity = true; tab = TabHistory;
        } else if (!overPanel && !anyTool && hoverCity >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            citySel = hoverCity; clickedCity = true;
            if (tab != TabCities && tab != TabTrade) tab = TabCities;
        }
        if (!overPanel && !routeConsumed && (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) ||
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

        // ---- Moving tectonic plates (World tab, "Edit plates") ----
        const bool platesActive = plateMode && tab == TabWorld && !app.world.plates.empty();
        auto plateCentre = [&](int p) {
            const Vec3d& c = app.world.plates[p].seed;
            return std::pair<double, double>{std::asin(clampd(c.y, -1, 1)) * 180 / kPi, std::atan2(c.x, c.z) * 180 / kPi};
        };
        auto plateArrowTip = [&](int p) {
            auto cl = plateCentre(p);
            const Plate& pl = app.world.plates[p];
            Vec3d v = pl.pole.cross(pl.seed) * pl.speed, e, n;
            localAxes(cl.first, cl.second, e, n);
            Vector2 a = toScreen(cl.first, cl.second);
            return Vector2{a.x + (float)v.dot(e) * 70, a.y - (float)v.dot(n) * 70};
        };
        if (platesActive && !overPanel) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                plateDrag = 0;
                for (int p = 0; p < (int)app.world.plates.size() && !plateDrag; p++) {
                    if (p < (int)app.world.plateRemoved.size() && app.world.plateRemoved[p]) continue;
                    Vector2 tip = plateArrowTip(p);
                    auto cl = plateCentre(p);
                    Vector2 c = toScreen(cl.first, cl.second);
                    if (std::hypot(tip.x - mouse.x, tip.y - mouse.y) < 11) { plateDrag = 2; plateSel = p; }
                    else if (std::hypot(c.x - mouse.x, c.y - mouse.y) < 12) { plateDrag = 1; plateSel = p; }
                }
                if (!plateDrag && hit) plateSel = app.world.plateId[World::nearestCell(mLat, mLon)];
                plateDragDir = hitDir;
            }
            if (plateDrag && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && hit) plateDragDir = hitDir;
            if (plateDrag && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                PlateEdit* e = app.ui.editFor(plateSel);
                if (plateDrag == 1) { e->moveSeed = true; e->seed = plateDragDir; }
                else {
                    // the arrow gives the speed and direction at the plate's centre
                    const Vec3d& c = app.world.plates[plateSel].seed;
                    auto cl = plateCentre(plateSel);
                    Vector2 a = toScreen(cl.first, cl.second);
                    Vec3d ea, na; localAxes(cl.first, cl.second, ea, na);
                    Vec3d v = ea * ((mouse.x - a.x) / 70.0) + na * (-(mouse.y - a.y) / 70.0);
                    double sp = clampd(v.length(), 0.05, 2.0);
                    Vec3d pole = c.cross(v);
                    if (pole.length() > 1e-9) { e->moveMotion = true; e->pole = pole.normalized(); e->speed = sp; }
                }
                plateDrag = 0;
                pendingPlateRebuild = 2;
            }
        }

        // ---- Sculpting ----
        // tools: 0 pan, 1 raise, 2 lower, 3 smooth, 4 flatten, 5 make land, 6 make sea, 7-9 stamps,
        //        10 mountain range, 11 hills, 12 draw land (lasso), 13 draw sea (lasso)
        const double landHeightM = 20 * std::pow(150.0, (double)landHeightT);   // 20 .. 3000 m
        const double seaDepthM = 30 * std::pow(200.0, (double)seaDepthT);       // 30 .. 6000 m
        const bool invert = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        auto brushFor = [&](int t, double& target) {
            target = -1e9;
            switch (t) {
                case 1: return invert ? BrushType::Lower : BrushType::Raise;
                case 2: return invert ? BrushType::Raise : BrushType::Lower;
                case 3: return BrushType::Smooth;
                case 4: return BrushType::Flatten;
                case 5: if (invert) { target = -seaDepthM; return BrushType::SeaFill; } target = landHeightM; return BrushType::LandFill;
                case 6: if (invert) { target = landHeightM; return BrushType::LandFill; } target = -seaDepthM; return BrushType::SeaFill;
                case 10: return invert ? BrushType::Lower : BrushType::Mountains;
                case 11: return BrushType::Rough;
                default: return BrushType::Raise;
            }
        };
        const bool lassoTool = activeTool == 12 || activeTool == 13;
        if (activeTool >= 7 && activeTool <= 9 && hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            app.planet.addStamp(activeTool - 7, hitDir, brushKm);
            map.invalidate(hitDir, brushKm * 1.6);
            app.stats = app.planet.computeStats();
        } else if (lassoTool && hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            lassoing = true; lassoPts.clear(); lassoPts.push_back({mLat, mLon});
        } else if (activeTool > 0 && (activeTool < 7 || activeTool == 10 || activeTool == 11) && hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            painting = true;
            double target; BrushType bt = brushFor(activeTool, target);
            app.planet.beginStrokeGroup(bt == BrushType::Mountains || bt == BrushType::Rough ? BrushType::Raise : bt, hitDir, brushKm, target);
        }
        if (lassoing) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && hit) {
                // keep points a few pixels apart; longitudes stay continuous across the date line
                double plon = lassoPts.back().second, lon = plon + std::remainder(mLon - plon, 360.0);
                Vector2 a = toScreen(lassoPts.back().first, lassoPts.back().second);
                if (std::hypot(a.x - mouse.x, a.y - mouse.y) > 5) lassoPts.push_back({mLat, lon});
            }
            if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                lassoing = false;
                bool sea = (activeTool == 13) != invert;
                if (lassoPts.size() >= 3 && app.planet.addLasso(sea ? BrushType::SeaFill : BrushType::LandFill, lassoPts, sea ? -seaDepthM : landHeightM, hardnessUi, roughUi)) {
                    const Stroke& st = app.planet.strokes().back();
                    map.invalidate(st.dir, st.radiusKm * 1.05);
                    app.stats = app.planet.computeStats();
                    setStatus(sea ? "Sea drawn - Ctrl+Z to undo" : "Land drawn - Ctrl+Z to undo");
                } else setStatus("Draw a bigger shape (and less than a hemisphere)");
                lassoPts.clear();
            }
        }
        if (painting && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && hit) {
            double target; BrushType bt = brushFor(activeTool, target);
            double amount = additive(bt) ? strength * dt * (bt == BrushType::Mountains ? 1.6 : 1.0) : std::min(1.0, dt * 3 * strength / 1000);
            app.planet.addDab(bt, hitDir, brushKm, amount, hardnessUi, bt == BrushType::Smooth || bt == BrushType::Flatten ? 0 : roughUi);
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
        if (relPick && hit && !overPanel && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && nationSel >= 0) {
            int other = map.nationAt(mLat, mLon);
            if (other >= 0 && other != nationSel) {
                app.doc.relations.erase(std::remove_if(app.doc.relations.begin(), app.doc.relations.end(), [&](const NationRelation& r) {
                    return (r.a == nationSel && r.b == other) || (r.a == other && r.b == nationSel); }), app.doc.relations.end());
                NationRelation r; r.a = nationSel; r.b = other; r.type = (Relation)relTypeUi;
                app.doc.relations.push_back(r);
                setStatus(std::string(relationName(r.type)) + " set with " + app.doc.nations[other].name);
            }
            relPick = false;
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
        if (haveTrace && showTrace) {
            for (int k = -1; k <= 1; k++) {
                float x0 = (float)(sw / 2.0 + (-180 + 360 * k - view.centerLon) * view.pxPerDeg);
                float y0 = (float)(sh / 2.0 - (90 - view.centerLat) * view.pxPerDeg);
                float wpx = (float)(360 * view.pxPerDeg), hpx = (float)(180 * view.pxPerDeg);
                if (x0 > sw || x0 + wpx < 0) continue;
                DrawTexturePro(traceTex, Rectangle{0, 0, (float)traceTex.width, (float)traceTex.height}, Rectangle{x0, y0, wpx, hpx}, Vector2{0, 0}, 0,
                               Fade(WHITE, traceAlpha));
            }
        }
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
                float spacing = size * 0.18f;
                if (lb.stretch > 1.4) spacing = size * (float)clampd(0.18 + (lb.stretch - 1.4) * 0.25, 0.18, 0.8);   // spread out along long countries
                Font lf = fontFor(FontKind::Serif, size);
                Vector2 ts = MeasureTextEx(lf, up.c_str(), size, spacing);
                if (p.x + ts.x / 2 < panel.x + panel.width || p.x - ts.x / 2 > sw) continue;
                bool fantasy = map.style() == MapStyle::Fantasy;
                float rot = rotateLabels && lb.stretch > 1.25 ? (float)-lb.angle : 0.f;   // screen y points down
                Vector2 origin{ts.x / 2, ts.y / 2};
                if (!fantasy) DrawTextPro(lf, up.c_str(), {p.x + 1.5f, p.y + 1.5f}, origin, rot, size, spacing, Fade(BLACK, 0.6f));
                DrawTextPro(lf, up.c_str(), p, origin, rot, size, spacing, fantasy ? Color{80, 50, 30, 220} : Fade(RAYWHITE, 0.92f));
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
                float wpx = r.custom ? 3.f : (float)clampd(1 + 3.5 * std::log10(1 + 9 * r.value / maxV), 1, 5);
                if (ri == routeSel) wpx += 1.5f;
                Vector2 prev{0, 0}; bool have = false; int k = 0;
                for (int c : r.cells) {
                    Vector2 p = toScreen(World::cellLat(c / World::W), World::cellLon(c % World::W));
                    bool water = app.world.elev[c] <= 0 || (!app.world.discharge.empty() && app.world.discharge[c] > 400);
                    Color col = r.custom ? Color{250, 210, 40, 230} : (water ? Color{30, 90, 200, 210} : Color{150, 90, 40, 210});
                    if (sel) col = r.custom ? Color{255, 225, 120, 255} : Color{255, 90, 210, 245};
                    if (have && std::fabs(p.x - prev.x) < sw / 2) {
                        if (r.custom) DrawLineEx(prev, p, wpx + 3, Fade(BLACK, 0.55f));   // outline so the user's routes stand out
                        DrawLineEx(prev, p, wpx, col);
                    }
                    prev = p; have = true; k++;
                }
            }
        }
        // ---- Route editing handles and the route being drawn ----
        if (tab == TabTrade) {
            if (myRouteSel >= 0 && myRouteSel < (int)app.doc.customRoutes.size()) {
                auto H = handlesOf(myRouteSel);
                int ei = econIndexOf(myRouteSel);
                // days for each leg, written at the middle of the leg
                if (ei >= 0 && dragHandle < 0) {
                    const TradeRoute& r = app.econ.routes[ei];
                    Vector2 lastLabel{-1000, -1000};
                    for (size_t L2 = 0; L2 < r.legStart.size() && r.legStart.size() > 1; L2++) {
                        int a = r.legStart[L2], b = L2 + 1 < r.legStart.size() ? r.legStart[L2 + 1] : (int)r.cells.size() - 1;
                        int mid = r.cells[std::min((int)r.cells.size() - 1, (a + b) / 2)];
                        Vector2 p = scr(World::cellLat(mid / World::W), World::cellLon(mid % World::W));
                        if (std::hypot(p.x - lastLabel.x, p.y - lastLabel.y) < 70) continue;
                        lastLabel = p;
                        const char* t = TextFormat("%.1f days", r.legDays[L2]);
                        Vector2 ts = measureTextK(FontKind::UiBold, t, 13);
                        DrawRectangleRounded({p.x - ts.x / 2 - 6, p.y - 20, ts.x + 12, 18}, 0.5f, 6, Color{20, 24, 33, 220});
                        drawTextK(FontKind::UiBold, t, {p.x - ts.x / 2, p.y - 18}, 13, theme::kAccentHi);
                    }
                }
                for (int k = 0; k < (int)H.size(); k++) {
                    Vector2 p = scr(H[k].first, H[k].second);
                    if (dragHandle >= 0 && k > 0) {   // straight preview while dragging
                        Vector2 q = scr(H[k - 1].first, H[k - 1].second);
                        if (std::fabs(p.x - q.x) < sw / 2) DrawLineEx(q, p, 2, Fade(theme::kAccentHi, 0.7f));
                    }
                    bool end = k == 0 || k == (int)H.size() - 1;
                    bool hov = std::hypot(p.x - mouse.x, p.y - mouse.y) < 9;
                    float rr = hov ? 8.f : 6.f;
                    if (end) { DrawRectangleRec({p.x - rr, p.y - rr, rr * 2, rr * 2}, Color{20, 24, 33, 255}); DrawRectangleLinesEx({p.x - rr, p.y - rr, rr * 2, rr * 2}, 2, theme::kAccentHi); }
                    else { DrawCircleV(p, rr, Color{20, 24, 33, 255}); DrawCircleLinesV(p, rr, theme::kAccentHi); DrawCircleV(p, rr - 3, theme::kAccent); }
                }
            }
            if (routeTool == 1 && drawFrom >= 0 && drawFrom < (int)app.doc.cities.size()) {
                std::vector<std::pair<double, double>> pts{{app.doc.cities[drawFrom].lat, app.doc.cities[drawFrom].lon}};
                for (auto& v : drawPts) pts.push_back(v);
                Vector2 prev = scr(pts[0].first, pts[0].second);
                for (size_t k = 1; k < pts.size(); k++) {
                    Vector2 p = scr(pts[k].first, pts[k].second);
                    if (std::fabs(p.x - prev.x) < sw / 2) DrawLineEx(prev, p, 2.5f, theme::kAccentHi);
                    DrawCircleV(p, 5, theme::kAccent);
                    prev = p;
                }
                // dashed line to the mouse
                float len = std::hypot(mouse.x - prev.x, mouse.y - prev.y);
                int dashes = std::max(1, (int)(len / 10));
                for (int k = 0; k < dashes; k += 2) {
                    float t0 = (float)k / dashes, t1 = (float)(k + 1) / dashes;
                    DrawLineEx({prev.x + (mouse.x - prev.x) * t0, prev.y + (mouse.y - prev.y) * t0}, {prev.x + (mouse.x - prev.x) * t1, prev.y + (mouse.y - prev.y) * t1}, 2, Fade(theme::kAccentHi, 0.8f));
                }
                DrawRectangleLinesEx({prev.x - 7, prev.y - 7, 14, 14}, 2, theme::kAccentHi);
            }
        }
        // ---- Caravans and ships moving along the main routes ----
        if (showRoutes && showCaravans && app.econ.valid && (tab == TabTrade || ov == Overlay::Political)) {
            double tnow = GetTime();
            for (int ri = 0; ri < (int)app.econ.routes.size() && ri < 80; ri++) {
                const TradeRoute& r = app.econ.routes[ri];
                if (r.cells.size() < 2) continue;
                int n = (int)r.cells.size();
                for (int k = 0; k < 2; k++) {
                    double t = std::fmod(tnow * 6.0 / n + k * 0.5 + ri * 0.137, 1.0) * (n - 1);
                    int i0 = (int)t; double f = t - i0;
                    int c0 = r.cells[i0], c1 = r.cells[std::min(n - 1, i0 + 1)];
                    double lat = World::cellLat(c0 / World::W) * (1 - f) + World::cellLat(c1 / World::W) * f;
                    double lon0 = World::cellLon(c0 % World::W), lon1 = World::cellLon(c1 % World::W);
                    double dl = std::remainder(lon1 - lon0, 360.0);
                    Vector2 p = toScreen(lat, lon0 + dl * f);
                    if (p.x < panel.x + panel.width || p.x > sw || p.y < 0 || p.y > sh) continue;
                    bool waterCell = app.world.elev[c0] <= 0;
                    DrawCircleV(p, 3.5f, BLACK);
                    DrawCircleV(p, 2.5f, waterCell ? Color{240, 240, 255, 255} : Color{250, 210, 120, 255});
                }
            }
        }
        // ---- Relations between capitals ----
        if (ov == Overlay::Political && !app.doc.relations.empty()) {
            std::vector<int> capOf(app.doc.nations.size(), -1);
            for (int i = 0; i < (int)app.doc.cities.size(); i++)
                if (app.doc.cities[i].capital && app.doc.cities[i].nation >= 0 && app.doc.cities[i].nation < (int)capOf.size()) capOf[app.doc.cities[i].nation] = i;
            for (auto& r : app.doc.relations) {
                if (r.a >= (int)capOf.size() || r.b >= (int)capOf.size() || capOf[r.a] < 0 || capOf[r.b] < 0) continue;
                if (nationSel >= 0 && r.a != nationSel && r.b != nationSel) continue;
                const City& A = app.doc.cities[capOf[r.a]]; const City& B = app.doc.cities[capOf[r.b]];
                Vector2 pa = toScreen(A.lat, A.lon), pb = toScreen(B.lat, B.lon);
                if (std::fabs(pa.x - pb.x) > sw / 2) continue;
                static const Color rc[] = {{40, 200, 80, 220}, {60, 160, 230, 220}, {240, 160, 40, 220}, {230, 30, 30, 240}, {170, 90, 220, 220}};
                Color c = rc[std::min(4, (int)r.type)];
                float len = std::hypot(pb.x - pa.x, pb.y - pa.y);
                int dashes = std::max(1, (int)(len / 14));
                for (int k = 0; k < dashes; k += (r.type == Relation::Rivalry ? 2 : 1)) {
                    float t0 = (float)k / dashes, t1 = std::min(1.f, (k + 0.6f) / dashes);
                    DrawLineEx({pa.x + (pb.x - pa.x) * t0, pa.y + (pb.y - pa.y) * t0}, {pa.x + (pb.x - pa.x) * t1, pa.y + (pb.y - pa.y) * t1}, r.type == Relation::War ? 3.f : 2.f, c);
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
                        if (showLabels && view.pxPerDeg >= 30) uiText((c.name + " (ruins)").c_str(), (int)p.x + 8, (int)p.y - 7, 14, DARKGRAY);
                    }
                    continue;
                }
                // show bigger places first as you zoom in (populations grow over the timeline)
                double popNow = app.doc.populationAt(c, year);
                double need = c.capital ? 3.0 : (popNow > 500000 ? 8 : (popNow > 100000 ? 14 : (popNow > 20000 ? 24 : 40)));
                if (view.pxPerDeg < need && i != citySel) continue;
                Vector2 p = toScreen(c.lat, c.lon);
                if (p.x < panel.x + panel.width || p.x > sw || p.y < 0 || p.y > sh) continue;
                float r = (float)clampd(2 + std::log10(std::max(100.0, popNow)) - 3, 2.5, 6);
                Color fill = fantasy ? Color{60, 40, 25, 255} : RAYWHITE;
                if (c.capital) {
                    DrawCircleV(p, r + 3, BLACK); DrawCircleV(p, r + 2, fill); DrawCircleV(p, r - 0.5f, BLACK);
                } else {
                    DrawCircleV(p, r + 1, BLACK); DrawCircleV(p, r, fill);
                }
                if (i == citySel) DrawCircleLines((int)p.x, (int)p.y, r + 7, YELLOW);
                if (showLabels && view.pxPerDeg >= need * 1.6) {
                    int fsz = c.capital ? 18 : (popNow > 100000 ? 16 : 14);
                    if (!fantasy) uiText(c.name.c_str(), (int)p.x + 9, (int)p.y - 7, fsz, BLACK);
                    uiText(c.name.c_str(), (int)p.x + 8, (int)p.y - 8, fsz, fantasy ? Color{60, 40, 25, 255} : RAYWHITE);
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
                uiText(sym[t], (int)p.x - uiMeasure(sym[t], 14) / 2, (int)p.y - 7, 14, WHITE);
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
                    uiText(a.title.c_str(), (int)p.x + 10, (int)p.y - 6, 15, BLACK);
                    uiText(a.title.c_str(), (int)p.x + 9, (int)p.y - 7, 15, Color{255, 235, 190, 255});
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
        {
            // Animated wind / current flow (always shown with the Wind and Currents layers)
            Rectangle area{panel.x + panel.width, 0, (float)sw - panel.x - panel.width, (float)sh - 30};
            auto toLL = [&](float x, float y, double& lat, double& lon) {
                lat = view.centerLat - (y - sh / 2.0) / view.pxPerDeg;
                lon = wrapLon(view.centerLon + (x - sw / 2.0) / view.pxPerDeg);
                return std::fabs(lat) <= 90;
            };
            int count = (int)clampd(area.width * area.height / 700.0, 300, 2600);
            int mo = std::min(12, (int)std::round(monthUi));
            bool ink = map.style() == MapStyle::Fantasy;
            if (flowWind || ov == Overlay::Wind) { windFlow.update(app.world, mo, area, dt, toLL, 7.f, count); windFlow.draw(ink); }
            if (flowCurrents || ov == Overlay::Currents) { curFlow.update(app.world, mo, area, dt, toLL, 7.f, count / 2); curFlow.draw(ink); }
        }
        if (plateMode && tab == TabWorld && !app.world.plates.empty()) {
            for (int p = 0; p < (int)app.world.plates.size(); p++) {
                if (p < (int)app.world.plateRemoved.size() && app.world.plateRemoved[p]) continue;
                auto cl = plateCentre(p);
                Vector2 c = toScreen(cl.first, cl.second);
                Vector2 tip = plateArrowTip(p);
                bool sel = p == plateSel;
                if (plateDrag && sel) {
                    double la = std::asin(clampd(plateDragDir.y, -1, 1)) * 180 / kPi, lo = std::atan2(plateDragDir.x, plateDragDir.z) * 180 / kPi;
                    if (plateDrag == 1) { Vector2 to = toScreen(la, lo); DrawLineEx(c, to, 2, Fade(WHITE, 0.6f)); c = to; tip = Vector2{to.x + tip.x - c.x, to.y + tip.y - c.y}; }
                    else tip = mouse;
                }
                if (c.x < panel.x + panel.width - 20 || c.x > sw + 20) continue;
                Color pc{app.world.plates[p].r, app.world.plates[p].g, app.world.plates[p].b, 255};
                DrawLineEx(c, tip, sel ? 5.f : 4.f, Fade(BLACK, 0.6f));
                DrawLineEx(c, tip, sel ? 3.f : 2.f, sel ? theme::kAccentHi : WHITE);
                float ang = std::atan2(tip.y - c.y, tip.x - c.x);
                Vector2 l{tip.x - 12 * std::cos(ang - 0.45f), tip.y - 12 * std::sin(ang - 0.45f)}, r{tip.x - 12 * std::cos(ang + 0.45f), tip.y - 12 * std::sin(ang + 0.45f)};
                DrawTriangle(tip, r, l, sel ? theme::kAccentHi : WHITE);
                DrawTriangle(tip, l, r, sel ? theme::kAccentHi : WHITE);
                DrawCircleV(c, sel ? 11.f : 9.f, BLACK);
                DrawCircleV(c, sel ? 9.f : 7.f, pc);
                DrawCircleLinesV(c, sel ? 11.f : 9.f, sel ? theme::kAccentHi : WHITE);
                const char* t = TextFormat("%d", p + 1);
                Vector2 ts = measureTextK(FontKind::UiBold, t, 12);
                drawTextK(FontKind::UiBold, t, {c.x - ts.x / 2, c.y - ts.y / 2}, 12, BLACK);
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
        if (hit && ((activeTool > 0 && activeTool != 12 && activeTool != 13) || paintingTerritory)) {
            double kmPerDeg = 111.195 * R / 6371.0;
            float ry = (float)(brushKm / kmPerDeg * view.pxPerDeg);
            float rx = (float)(ry / std::max(0.02, std::cos(mLat * kPi / 180)));
            Color bc = map.style() == MapStyle::Fantasy ? Color{60, 40, 25, 255} : RAYWHITE;
            DrawEllipseLines((int)mouse.x, (int)mouse.y, rx, ry, bc);
            DrawEllipseLines((int)mouse.x, (int)mouse.y, rx + 1, ry + 1, Fade(BLACK, 0.4f));
            if (sculptTab && activeTool != 7 && activeTool != 8 && activeTool != 9) {   // inner ring: where the brush is at full strength
                float k = hardnessUi * 0.85f;
                if (k > 0.05f) DrawEllipseLines((int)mouse.x, (int)mouse.y, rx * k, ry * k, Fade(bc, 0.5f));
                const char* nm = activeTool == 5 ? (invert ? "sea" : "land") : activeTool == 6 ? (invert ? "land" : "sea") : activeTool == 1 ? (invert ? "lower" : "raise")
                               : activeTool == 2 ? (invert ? "raise" : "lower") : activeTool == 10 ? (invert ? "lower" : "mountains") : nullptr;
                if (nm) drawTextK(FontKind::UiBold, nm, {mouse.x + rx * 0.72f + 6, mouse.y + ry * 0.72f + 2}, 13, bc);
            }
        }
        if (lassoing && lassoPts.size() > 1) {
            bool sea = (activeTool == 13) != (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT));
            Color lc = sea ? Color{120, 200, 255, 255} : Color{140, 230, 120, 255};
            Vector2 first = toScreen(lassoPts[0].first, lassoPts[0].second), prev = first;
            for (size_t k = 1; k < lassoPts.size(); k++) {
                Vector2 p = toScreen(lassoPts[k].first, lassoPts[k].second);
                if (std::fabs(p.x - prev.x) < sw / 2) { DrawLineEx(prev, p, 4, Fade(BLACK, 0.5f)); DrawLineEx(prev, p, 2, lc); }
                prev = p;
            }
            if (std::fabs(prev.x - first.x) < sw / 2) DrawLineEx(prev, first, 1.5f, Fade(lc, 0.6f));   // closing edge
        }

        // ---- Layer chips ----
        {
            for (int i = 0; i < chipCount; i++) {
                Rectangle r = chipRects[i];
                bool on = overlayUi == kChips[i].overlay, hov = CheckCollisionPointRec(mouse, r);
                DrawRectangleRounded(r, 0.5f, 8, on ? theme::kAccent : hov ? Color{44, 52, 70, 235} : Color{20, 24, 33, 225});
                DrawRectangleRoundedLinesEx(r, 0.5f, 8, 1, on ? theme::kAccentHi : Color{60, 70, 92, 255});
                Vector2 ts = measureTextK(on ? FontKind::UiBold : FontKind::Ui, kChips[i].name, 14);
                drawTextK(on ? FontKind::UiBold : FontKind::Ui, kChips[i].name, {r.x + r.width / 2 - ts.x / 2, r.y + 6}, 14,
                          on ? Color{30, 22, 10, 255} : theme::kText);
                if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) overlayUi = on ? 0 : kChips[i].overlay;
            }
            for (int i = 0; i < 2; i++) {
                Rectangle r = chipRects[chipCount + i];
                bool& on = i == 0 ? flowWind : flowCurrents;
                bool hov = CheckCollisionPointRec(mouse, r);
                DrawRectangleRounded(r, 0.5f, 8, on ? theme::kTeal : hov ? Color{44, 52, 70, 235} : Color{20, 24, 33, 225});
                DrawRectangleRoundedLinesEx(r, 0.5f, 8, 1, on ? Color{150, 235, 235, 255} : Color{60, 70, 92, 255});
                const char* nm = i == 0 ? "Wind flow" : "Current flow";
                Vector2 ts = measureTextK(on ? FontKind::UiBold : FontKind::Ui, nm, 14);
                drawTextK(on ? FontKind::UiBold : FontKind::Ui, nm, {r.x + r.width / 2 - ts.x / 2, r.y + 6}, 14, on ? Color{10, 30, 30, 255} : theme::kText);
                if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) on = !on;
            }
            // month stepper for seasonal layers
            if (overlayUi == 5 || overlayUi == 6 || overlayUi == 8 || overlayUi == 14 || flowWind) {
                float x = chipRects[chipCount + 1].x + chipRects[chipCount + 1].width + 12;
                Rectangle box{x, chipBar.y, 140, 28};
                DrawRectangleRounded(box, 0.5f, 8, Color{20, 24, 33, 225});
                DrawRectangleRoundedLinesEx(box, 0.5f, 8, 1, Color{60, 70, 92, 255});
                int mo = (int)std::round(monthUi);
                const char* mn = mo >= 12 ? "Whole year" : kMonths[mo];
                Vector2 ts = measureTextK(FontKind::Ui, mn, 14);
                drawTextK(FontKind::Ui, mn, {box.x + 70 - ts.x / 2, box.y + 6}, 14, theme::kText);
                Rectangle lb{box.x, box.y, 26, 28}, rb{box.x + 114, box.y, 26, 28};
                drawTextK(FontKind::UiBold, "<", {lb.x + 9, lb.y + 5}, 16, CheckCollisionPointRec(mouse, lb) ? theme::kAccentHi : theme::kMuted);
                drawTextK(FontKind::UiBold, ">", {rb.x + 9, rb.y + 5}, 16, CheckCollisionPointRec(mouse, rb) ? theme::kAccentHi : theme::kMuted);
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if (CheckCollisionPointRec(mouse, lb)) monthUi = (float)((mo + 12) % 13);
                    if (CheckCollisionPointRec(mouse, rb)) monthUi = (float)((mo + 1) % 13);
                }
            }
        }

        // ---- Panel ----
        {
            int clicked = theme::rail(railRect, tab);
            if (clicked >= 0 && !typing) tab = clicked;
        }
        const float contentTop = theme::header(panel, tab);
        const float contentBottom = panel.y + panel.height - 8;
        static float panelScroll[16] = {0};
        static float panelContentH[16] = {0};
        float& pScroll = panelScroll[tab];
        if (overPanel && mouse.x > panel.x && !typing) {
            float wv = GetMouseWheelMove();
            if (wv != 0) pScroll -= wv * 60;
        }
        pScroll = std::max(0.f, std::min(pScroll, std::max(0.f, panelContentH[tab] - (contentBottom - contentTop))));
        Layout L{panel.x + 18, contentTop + 6 - pScroll, panel.width - 40};
        const float layoutStartY = L.y;
        BeginScissorMode((int)panel.x, (int)contentTop, (int)panel.width, (int)(contentBottom - contentTop));
        const bool lockHidden = overPanel && mouse.y < contentTop;   // controls scrolled up under the header can't be clicked
        if (lockHidden) GuiLock();
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
            if (!app.ui.importPath.empty()) {
                L.note(("Land shape from: " + std::string(GetFileName(app.ui.importPath.c_str()))).c_str());
                if (L.button("Stop using the image", 28)) { app.ui.importPath.clear(); setStatus("Press Generate planet to go back to random land"); }
            } else L.note("Tip: drag a PNG or JPG onto the window to use it as the land shape or as a tracing layer.");
            L.heading("TECTONICS");
            L.label(TextFormat("Plates: %d", (int)platesUi)); L.slider(&platesUi, 4, 30);
            L.label(TextFormat("Tectonic activity: %.1f", activityUi)); L.slider(&activityUi, 0, 2);
            L.label(TextFormat("Erosion: %.1f", erosionUi)); L.slider(&erosionUi, 0, 2.5f);
            bool genClicked = L.primary("Generate planet");
            L.heading("MOVE PLATES");
            {
                bool pm = plateMode;
                L.check("Edit plates on the map", &pm);
                if (pm != plateMode) { plateMode = pm; if (pm) { overlayUi = 1; } plateSel = -1; }
                if (plateMode) {
                    L.note("Drag a plate's dot to move it, or the tip of its arrow to change where it is heading and how fast. Plates pulling apart open rifts, plates pushing together raise mountains.");
                    L.label(TextFormat("Drift time: %.0f million years", app.ui.driftMa));
                    float dr = app.ui.driftMa;
                    L.slider(&dr, 0, 200);
                    app.ui.driftMa = std::round(dr / 5) * 5;
                    L.note("Longer drift opens wider new oceans where plates part and higher ranges where they meet.");
                    if (L.button("Apply drift", 30)) pendingPlateRebuild = 2;
                    if (plateSel >= 0 && plateSel < (int)app.world.plates.size()) {
                        Plate& pl = app.world.plates[plateSel];
                        L.heading(TextFormat("PLATE %d", plateSel + 1));
                        DrawRectangleRounded({L.x, L.y + 2, 18, 18}, 0.3f, 4, Color{pl.r, pl.g, pl.b, 255});
                        drawTextK(FontKind::Ui, TextFormat("%s, moving %.1f cm/yr", pl.continental ? "Continental" : "Oceanic", pl.speed * 5), {L.x + 26, L.y + 2}, 15, theme::kText);
                        L.y += 28;
                        PlateEdit* pe = nullptr;
                        for (auto& e : app.ui.plateEdits) if (e.plate == plateSel) pe = &e;
                        int ty = pe ? pe->type : 0;
                        GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 8) / 3, 28}, "Automatic;Continental;Oceanic", &ty);
                        L.y += 34;
                        if (ty != (pe ? pe->type : 0)) { app.ui.editFor(plateSel)->type = ty; pendingPlateRebuild = 2; }
                        L.note("Continental plates carry land; making a plate oceanic sinks its land, making it continental raises new land.");
                        float sp = (float)pl.speed, sp0 = sp;
                        L.label(TextFormat("Speed: %.1f cm/yr", sp * 5)); L.slider(&sp, 0.05f, 2.0f);
                        if (sp != sp0) { PlateEdit* e = app.ui.editFor(plateSel); e->moveMotion = true; e->pole = pl.pole; e->speed = sp; pl.speed = sp; }
                        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && sp != sp0) pendingPlateRebuild = 2;
                        float bw = (L.w - 8) / 2;
                        if (GuiButton(Rectangle{L.x, L.y, bw, 28}, "Apply speed")) pendingPlateRebuild = 2;
                        if (GuiButton(Rectangle{L.x + bw + 8, L.y, bw, 28}, "Merge away")) { app.ui.editFor(plateSel)->removed = true; plateSel = -1; pendingPlateRebuild = 2; }
                        L.y += 36;
                        if (pe && L.button("Reset this plate")) {
                            app.ui.plateEdits.erase(std::remove_if(app.ui.plateEdits.begin(), app.ui.plateEdits.end(), [&](const PlateEdit& e) { return e.plate == plateSel; }), app.ui.plateEdits.end());
                            pendingPlateRebuild = 2;
                        }
                    } else L.note("Click a plate to select it.");
                    if (!app.ui.plateEdits.empty() && L.button("Reset all plates")) { app.ui.plateEdits.clear(); app.ui.driftMa = 0; pendingPlateRebuild = 2; }
                }
            }
            L.heading("STATS");
            L.label(TextFormat("Land: %.1f%%", app.stats.landPercent));
            L.label(TextFormat("Highest: %.0f m   Deepest: %.0f m", app.stats.highestM, app.stats.deepestM));
            L.label(TextFormat("Area: %.1fM km2   Plates: %zu", app.stats.areaMkm2, app.world.plates.size()));
            L.label(TextFormat("Volcanoes: %zu   Rivers: %zu", app.world.volcanoes.size(), app.world.rivers.size()));
            if (L.button("Show world statistics")) showStats = true;
            if (L.button("Getting-started guide")) showIntro = true;
            L.note("Press F1 any time for help and keyboard shortcuts.");
            if (randomClicked) { app.ui.seed = GetRandomValue(0, 999999); genClicked = true; }
            if (genClicked) {
                if (app.ui.plateCount != (int)platesUi || randomClicked) app.ui.plateEdits.clear();   // edits belong to the old plates
                app.ui.radiusKm = std::round(radiusUi); app.ui.oceanPercent = oceanUi; app.ui.mountains = mountUi;
                app.ui.continentSize = contUi; app.ui.plateCount = (int)platesUi; app.ui.tectonicActivity = activityUi;
                app.ui.axialTilt = tiltUi; app.ui.tempOffset = tempOffUi; app.ui.rainMultiplier = rainMulUi; app.ui.seaLevel = seaUi;
                app.ui.erosion = erosionUi;
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
            if (showMini) check2("Globe minimap", &globeMini, "Rotate labels", &rotateLabels);
            check2("Compass", &showCompass, "Map title", &showTitle);
            {
                double hexKm = std::pow(10.0, 0.5 + hexKmT * 2.5);
                GuiCheckBox(Rectangle{L.x, L.y, 20, 20}, TextFormat("Hex grid (%s)", formatKm(hexKm)), &showHex);
                GuiSliderBar(Rectangle{L.x + L.w / 2 + 10, L.y + 2, L.w / 2 - 10, 16}, NULL, NULL, &hexKmT, 0, 1);
                L.y += 26;
            }
            if (haveTrace) {
                GuiCheckBox(Rectangle{L.x, L.y, 20, 20}, "Tracing image", &showTrace);
                GuiSliderBar(Rectangle{L.x + L.w / 2 + 10, L.y + 2, L.w / 2 - 10, 16}, NULL, NULL, &traceAlpha, 0.05f, 1);
                L.y += 26;
            }
            L.heading("OVERLAY");
            GuiListView(Rectangle{L.x, L.y, L.w, 170}, "None;Tectonic plates;Rock types;Earthquake zones;Resource (see Resources tab);Temperature;Rainfall;Biomes;Wind;Ocean currents;Nations (political);Population density;Religions;Cultures;Storm seasons", &overlayScroll, &overlayUi);
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
            } else if (overlayUi == 14) {
                L.legend(Color{230, 40, 120, 255}, "Hurricanes / typhoons (this month)");
                L.legend(Color{90, 90, 220, 255}, "Winter storm tracks");
                L.note("Pick the month in the Climate tab.");
            } else if (overlayUi == 10) {
                const Color rc[] = {{40, 200, 80, 255}, {60, 160, 230, 255}, {240, 160, 40, 255}, {230, 30, 30, 255}, {170, 90, 220, 255}};
                for (int r = 0; r < 5; r++) L.legend(rc[r], relationName((Relation)r));
            }
            L.heading("BOOKMARKS");
            {
                std::string items;
                for (auto& b : app.doc.bookmarks) { if (!items.empty()) items += ";"; items += cleanList(b.name); }
                if (!app.doc.bookmarks.empty()) {
                    int prev = bookmarkSel;
                    GuiListView(Rectangle{L.x, L.y, L.w, 74}, items.c_str(), &bookmarkScroll, &bookmarkSel);
                    L.y += 80;
                    if (bookmarkSel != prev && bookmarkSel >= 0 && bookmarkSel < (int)app.doc.bookmarks.size()) {
                        const Bookmark& b = app.doc.bookmarks[bookmarkSel];
                        view.centerLat = b.lat; view.centerLon = b.lon; view.pxPerDeg = b.zoom;
                    }
                }
                float bw2 = (L.w - 8) / 2;
                if (GuiButton(Rectangle{L.x, L.y, bw2, 26}, "Bookmark this view")) {
                    Bookmark b; b.lat = view.centerLat; b.lon = view.centerLon; b.zoom = view.pxPerDeg;
                    int best = -1; double bd = 1e9;
                    for (int i = 0; i < (int)app.doc.cities.size(); i++) {
                        double d = std::hypot(app.doc.cities[i].lat - b.lat, std::remainder(app.doc.cities[i].lon - b.lon, 360.0));
                        if (d < bd) { bd = d; best = i; }
                    }
                    b.name = best >= 0 && bd < 6 ? "Near " + app.doc.cities[best].name : std::string(TextFormat("%.1f, %.1f", b.lat, b.lon));
                    app.doc.bookmarks.push_back(b);
                }
                if (GuiButton(Rectangle{L.x + bw2 + 8, L.y, bw2, 26}, "Delete bookmark") && bookmarkSel >= 0 && bookmarkSel < (int)app.doc.bookmarks.size()) {
                    app.doc.bookmarks.erase(app.doc.bookmarks.begin() + bookmarkSel); bookmarkSel = -1;
                }
                L.y += 32;
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
            GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 8) / 3, 26}, "Metals;Gems;Fuels\nIndustrial;Building;Fantasy", &resGroupUi);
            L.y += 62;
            const auto& defs = resourceDefs();
            std::vector<int> ids;
            std::string items;
            for (int r = 0; r < (int)defs.size(); r++)
                if ((int)defs[r].group == resGroupUi) { if (!items.empty()) items += ";"; items += defs[r].name; ids.push_back(r); }
            int prevActive = resListActive;
            float listH = resGroupUi == (int)ResGroup::Fantasy ? 96.f : 230.f;
            GuiListView(Rectangle{L.x, L.y, L.w, listH}, items.c_str(), &resListScroll, &resListActive);
            L.y += listH + 8;
            if (resGroupUi == (int)ResGroup::Fantasy) {
                float bw2 = (L.w - 8) / 2;
                if (GuiButton(Rectangle{L.x, L.y, bw2, 28}, "New fantasy resource")) {
                    CustomResource c;
                    static const char* names[] = {"Mithril", "Mana crystal", "Dragonglass", "Starsteel", "Moonsilver", "Aetherium", "Bloodstone", "Sunstone"};
                    c.name = names[app.doc.customResources.size() % 8];
                    randomColor(c.r, c.g, c.b);
                    app.doc.customResources.push_back(c);
                    customSel = (int)app.doc.customResources.size() - 1;
                }
                if (GuiButton(Rectangle{L.x + bw2 + 8, L.y, bw2, 28}, "Place deposits")) {
                    map.waitIdle();
                    setCustomResources(app.doc.customResources);
                    app.world.generateResources(app.ui.seed);
                    if (app.world.selectedRes >= 0 && app.world.selectedRes < (int)resourceDefs().size())
                        app.world.selectedFavor = app.world.favorability(app.world.selectedRes);
                    else app.world.selectedRes = -1;
                    map.invalidateAll();
                    setStatus("Fantasy resources placed");
                }
                L.y += 34;
                if (customSel >= (int)app.doc.customResources.size()) customSel = -1;
                if (customSel < 0 && !app.doc.customResources.empty()) customSel = 0;
                if (customSel != lastCustomSel) {
                    lastCustomSel = customSel; customNameEdit = customSettingEdit = false;
                    if (customSel >= 0) {
                        std::strncpy(customNameBuf, app.doc.customResources[customSel].name.c_str(), 63);
                        std::strncpy(customSettingBuf, app.doc.customResources[customSel].setting.c_str(), 127);
                    }
                }
                if (customSel >= 0) {
                    CustomResource& c = app.doc.customResources[customSel];
                    if (GuiTextBox(Rectangle{L.x, L.y, L.w - 36, 26}, customNameBuf, 63, customNameEdit)) customNameEdit = !customNameEdit;
                    c.name = customNameBuf;
                    DrawRectangle((int)(L.x + L.w - 28), (int)L.y, 28, 26, Color{c.r, c.g, c.b, 255});
                    if (CheckCollisionPointRec(mouse, Rectangle{L.x + L.w - 28, L.y, 28, 26}) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) randomColor(c.r, c.g, c.b);
                    L.y += 30;
                    if (GuiTextBox(Rectangle{L.x, L.y, L.w, 26}, customSettingBuf, 127, customSettingEdit)) customSettingEdit = !customSettingEdit;
                    c.setting = customSettingBuf;
                    L.y += 30;
                    uiText("Rock:", (int)L.x, (int)L.y + 2, 15, theme::kMuted);
                    static const char* rk[] = {"Sea floor", "Sediment", "Volcanic", "Granite", "Metamorph.", "Shield"};
                    for (int i = 0; i < 6; i++) {
                        bool on = (c.rocks >> i) & 1;
                        GuiCheckBox(Rectangle{L.x + 50 + (i % 3) * 90.f, L.y + (i / 3) * 22.f, 16, 16}, rk[i], &on);
                        c.rocks = (uint8_t)((c.rocks & ~(1 << i)) | (on ? 1 << i : 0));
                    }
                    L.y += 46;
                    uiText("Near:", (int)L.x, (int)L.y + 2, 15, theme::kMuted);
                    static const char* bd[] = {"Anywhere", "Collision", "Subduction", "Island arc", "Rift", "Ridge", "Fault"};
                    for (int i = 0; i < 7; i++) {
                        bool on = (c.boundaries >> i) & 1;
                        GuiCheckBox(Rectangle{L.x + 50 + (i % 3) * 90.f, L.y + (i / 3) * 22.f, 16, 16}, bd[i], &on);
                        c.boundaries = (uint8_t)((c.boundaries & ~(1 << i)) | (on ? 1 << i : 0));
                    }
                    L.y += 68;
                    auto pair = [&](const char* la, float* a, float amin, float amax, const char* lb, float* b, float bmin, float bmax) {
                        float hw = (L.w - 10) / 2;
                        uiText(TextFormat(la, *a), (int)L.x, (int)L.y, 14, theme::kMuted);
                        uiText(TextFormat(lb, *b), (int)(L.x + hw + 10), (int)L.y, 14, theme::kMuted);
                        GuiSliderBar(Rectangle{L.x, L.y + 16, hw, 14}, NULL, NULL, a, amin, amax);
                        GuiSliderBar(Rectangle{L.x + hw + 10, L.y + 16, hw, 14}, NULL, NULL, b, bmin, bmax);
                        if (*a > *b && bmin == amin) std::swap(*a, *b);
                        L.y += 36;
                    };
                    pair("Min age %.0f Myr", &c.minAge, 0, 4000, "Max age %.0f Myr", &c.maxAge, 0, 4000);
                    pair("Min height %.0f m", &c.minElev, -11000, 9000, "Max height %.0f m", &c.maxElev, -11000, 9000);
                    pair("Min temp %.0f C", &c.minTemp, -60, 50, "Max temp %.0f C", &c.maxTemp, -60, 50);
                    pair("Min rain %.0f mm", &c.minRain, 0, 6000, "Max rain %.0f mm", &c.maxRain, 0, 6000);
                    float ab = (float)c.abundance, sz = (float)std::log10(std::max(1.0, c.typicalTonnes));
                    pair("Deposits: %.0f", &ab, 1, 300, "Size: 10^%.1f t", &sz, 0, 10);
                    c.abundance = (int)ab; c.typicalTonnes = std::pow(10.0, (double)sz);
                    if (L.button("Delete this resource", 26)) { app.doc.customResources.erase(app.doc.customResources.begin() + customSel); customSel = -1; }
                    L.note("Press Place deposits to put your changes on the map.");
                }
            }
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
            if (L.button("This view in high resolution (PNG)")) pendingExport = 6;
            if (L.button("Chronicle: nations, history and lore (Markdown)")) pendingExport = 5;
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
                generateRelations(app.doc, (uint32_t)app.ui.seed + 61u);
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
                {
                    std::string rel;
                    int shownRel = 0;
                    for (auto& r : app.doc.relations) {
                        if (r.a != nationSel && r.b != nationSel) continue;
                        int other = r.a == nationSel ? r.b : r.a;
                        if (other < 0 || other >= (int)app.doc.nations.size()) continue;
                        if (shownRel++ < 4) {
                            if (!rel.empty()) rel += ", ";
                            if (r.type == Relation::Vassal) rel += r.a == nationSel ? std::string("vassal of ") + app.doc.nations[other].name : app.doc.nations[other].name + " (vassal)";
                            else rel += std::string(relationName(r.type)) + " with " + app.doc.nations[other].name;
                        }
                    }
                    if (!rel.empty()) L.note(("Relations: " + rel + (shownRel > 4 ? "..." : "")).c_str());
                    GuiComboBox(Rectangle{L.x, L.y, bw, 26}, "Alliance;Trade pact;Rivalry;War;Vassal of", &relTypeUi);
                    if (GuiButton(Rectangle{L.x + bw + 8, L.y, bw, 26}, relPick ? "Click a nation..." : "Add relation")) relPick = true;
                    L.y += 32;
                }
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
            {
                int eraUi = app.doc.era;
                GuiComboBox(Rectangle{L.x, L.y, L.w, 28}, "Era: Ancient;Era: Medieval;Era: Early modern;Era: Industrial;Era: Modern", &eraUi);
                L.y += 36;
                if (eraUi != app.doc.era) { app.doc.era = eraUi; app.econ.valid = false; }
                double lo, hi; eraCityRange(app.doc.era, lo, hi);
                if (!app.doc.cities.empty() && L.button("Realistic populations for this era")) {
                    eraPopulations(app.doc, app.world, app.doc.era);
                    app.econ.valid = false;
                    setStatus(TextFormat("Populations set for the %s era", eraName((Era)app.doc.era)));
                }
                L.note(TextFormat("In this era towns start around %s people and the largest cities reach about %s.", withCommas(lo), withCommas(hi)));
            }
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
                const int era = std::max(0, std::min(4, app.doc.era));
                L.heading("CITY");
                GuiLabel(Rectangle{L.x, L.y, 60, 28}, "Name");
                if (GuiTextBox(Rectangle{L.x + 60, L.y, L.w - 60, 28}, cityNameBuf, 63, cityNameEdit)) cityNameEdit = !cityNameEdit;
                c.name = cityNameBuf;
                L.y += 36;
                if (c.nation >= 0 && c.nation < (int)app.doc.nations.size()) {
                    drawFlag(Rectangle{L.x, L.y + 2, 36, 24}, app.doc.nations[c.nation]);
                    drawTextK(FontKind::UiBold, app.doc.nations[c.nation].name.c_str(), {L.x + 46, L.y + 1}, 17, theme::kText);
                    drawTextK(FontKind::Ui, c.capital ? "Capital city" : kGovernments[app.doc.nations[c.nation].government], {L.x + 46, L.y + 20}, 13, theme::kMuted);
                    L.y += 44;
                } else L.label("Nation: independent");
                double popNow = app.doc.populationAt(c, year);
                L.label(TextFormat("Population: %s", withCommas(popNow)));
                {
                    // where it ranks, and what that size means in this era
                    int rankWorld = 1, rankNation = 1, inNation = 0;
                    for (auto& o : app.doc.cities) {
                        if (&o == &c) continue;
                        if (o.population > c.population) rankWorld++;
                        if (o.nation == c.nation) { inNation++; if (o.population > c.population) rankNation++; }
                    }
                    L.note(TextFormat("%s for the %s era. #%d of %d in its nation, #%d in the world.", citySizeClass(c.population, era),
                                      eraName((Era)era), rankNation, inNation + 1, rankWorld));
                }
                float lp = (float)std::log10(std::max(100, c.population)), lp0 = lp;
                L.slider(&lp, 2, 7.6f);
                if (lp != lp0) {   // round to two significant figures while dragging
                    double p = std::pow(10.0, (double)lp), mag = std::pow(10.0, std::floor(lp) - 1);
                    c.population = (int)(std::round(p / mag) * mag);
                }
                if (c.founded > -99999) L.label(TextFormat("Founded: %d (%d years ago)", c.founded, std::max(0, year - c.founded)));
                bool cap = c.capital;
                L.check("Capital of its nation", &cap);
                if (cap != c.capital) {
                    if (cap) for (auto& o : app.doc.cities) if (o.nation == c.nation) o.capital = false;
                    c.capital = cap;
                }
                // ---- the place ----
                L.heading("PLACE");
                {
                    int cell = World::nearestCell(c.lat, c.lon);
                    double el = app.planet.elevation(TileMap::dirFromLatLon(c.lat, c.lon), 6);
                    L.label(TextFormat("Elevation: %.0f m", std::max(0.0, el)));
                    if (!app.world.biome.empty()) {
                        L.label(TextFormat("Land: %s", biomeName((Biome)app.world.biome[cell])));
                        float lo = 1e9f, hi = -1e9f;
                        for (int m = 0; m < 12; m++) { lo = std::min(lo, app.world.tempMonth[m][cell]); hi = std::max(hi, app.world.tempMonth[m][cell]); }
                        L.label(TextFormat("Climate: %.0f to %.0f \xC2\xB0" "C", lo, hi));
                        L.label(TextFormat("Rain: %.0f mm/yr", app.world.rainfallYear[cell]));
                    }
                    std::string water;
                    if (!app.world.distOceanKm.empty() && app.world.distOceanKm[cell] < 40) water = "Sea port";
                    if (!app.world.discharge.empty()) {
                        double q = 0;
                        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                            int x = cell % World::W + dx, y = cell / World::W + dy;
                            if (y >= 0 && y < World::H) q = std::max(q, (double)app.world.discharge[World::idx(x, y)]);
                        }
                        if (q > 100) water += water.empty() ? "River town" : ", on a river";
                    }
                    if (!water.empty()) L.label(TextFormat("Water: %s", water.c_str()));
                    L.label(TextFormat("Location: %.2f\xC2\xB0%c %.2f\xC2\xB0%c", std::fabs(c.lat), c.lat >= 0 ? 'N' : 'S', std::fabs(c.lon), c.lon >= 0 ? 'E' : 'W'));
                    auto near = app.world.depositsNear(c.lat, c.lon, 250, 40);
                    std::map<std::string, double> res;
                    for (int d : near) res[resourceDefs()[app.world.deposits[d].res].name] += app.world.deposits[d].tonnes;
                    if (!res.empty()) {
                        std::vector<std::pair<double, std::string>> v;
                        for (auto& kv : res) v.push_back({kv.second, kv.first});
                        std::sort(v.rbegin(), v.rend());
                        std::string t;
                        for (size_t k = 0; k < v.size() && k < 6; k++) { if (k) t += ", "; t += v[k].second; }
                        L.note(("Resources within 250 km: " + t).c_str());
                    }
                }
                // ---- trade ----
                L.heading("TRADE");
                if (!app.econ.valid || app.econCities != app.doc.cities.size()) {
                    L.note("Work out what this city makes, buys and sells:");
                    if (L.primary("Simulate trade", 32)) { app.simulateTrade(); setStatus("Trade simulated"); }
                } else {
                    const auto& G = goodDefs();
                    // per good: tonnes and the biggest partner
                    auto flows = [&](bool exports) {
                        std::map<int, std::pair<double, std::pair<double, int>>> m;   // good -> (tonnes, (best tonnes, partner))
                        for (auto& r : app.econ.routes) {
                            if ((exports ? r.from : r.to) != citySel) continue;
                            int partner = exports ? r.to : r.from;
                            for (auto& g : r.goods) {
                                auto& e = m[g.first];
                                e.first += g.second;
                                if (g.second > e.second.first) e.second = {g.second, partner};
                            }
                        }
                        std::vector<std::pair<int, std::pair<double, int>>> v;
                        for (auto& kv : m) v.push_back({kv.first, {kv.second.first, kv.second.second.second}});
                        std::sort(v.begin(), v.end(), [&](auto& a, auto& b) { return a.second.first * G[a.first].basePrice > b.second.first * G[b.first].basePrice; });
                        return v;
                    };
                    auto table = [&](const char* title, const std::vector<std::pair<int, std::pair<double, int>>>& v, bool exports) {
                        drawTextK(FontKind::UiBold, title, {L.x, L.y}, 15, theme::kTeal);
                        L.y += 22;
                        if (v.empty()) { L.note(exports ? "Sells nothing to other cities." : "Buys nothing from other cities."); return; }
                        for (size_t k = 0; k < v.size() && k < 6; k++) {
                            const auto& e = v[k];
                            drawTextK(FontKind::Ui, G[e.first].name, {L.x, L.y}, 15, theme::kText);
                            const char* t = TextFormat("%s/yr", formatTonnes(e.second.first));
                            Vector2 ts = measureTextK(FontKind::UiBold, t, 15);
                            drawTextK(FontKind::UiBold, t, {L.x + L.w - ts.x, L.y}, 15, Color{240, 230, 210, 255});
                            L.y += 18;
                            if (e.second.second >= 0 && e.second.second < (int)app.doc.cities.size())
                                drawTextK(FontKind::Ui, TextFormat(exports ? "mostly to %s" : "mostly from %s", app.doc.cities[e.second.second].name.c_str()), {L.x + 10, L.y}, 13, theme::kMuted);
                            L.y += 20;
                        }
                        L.y += 4;
                    };
                    auto ex = flows(true), im = flows(false);
                    double exV = 0, imV = 0;
                    for (auto& e : ex) exV += e.second.first * G[e.first].basePrice;
                    for (auto& e : im) imV += e.second.first * G[e.first].basePrice;
                    L.label(TextFormat("Trade per year: %s coins", withCommas(exV + imV)));
                    L.label(TextFormat("Balance: %s%s coins", exV >= imV ? "+" : "-", withCommas(std::fabs(exV - imV))));
                    table("Exports", ex, true);
                    table("Imports", im, false);
                    // what it makes itself
                    if (citySel < (int)app.econ.production.size()) {
                        std::vector<std::pair<double, int>> made;
                        for (int g = 0; g < (int)G.size(); g++) if (app.econ.production[citySel][g] > 0.5) made.push_back({app.econ.production[citySel][g] * G[g].basePrice, g});
                        std::sort(made.rbegin(), made.rend());
                        std::string t;
                        for (size_t k = 0; k < made.size() && k < 6; k++) { if (k) t += ", "; t += G[made[k].second].name; }
                        if (!t.empty()) L.note(("Makes: " + t).c_str());
                    }
                    // partners
                    std::map<int, double> part;
                    for (auto& r : app.econ.routes) {
                        if (r.from == citySel) part[r.to] += r.value; else if (r.to == citySel) part[r.from] += r.value;
                    }
                    if (!part.empty()) {
                        std::vector<std::pair<double, int>> pv;
                        for (auto& kv : part) pv.push_back({kv.second, kv.first});
                        std::sort(pv.rbegin(), pv.rend());
                        std::string t;
                        for (size_t k = 0; k < pv.size() && k < 5; k++) { if (k) t += ", "; t += app.doc.cities[pv[k].second].name; }
                        L.note(("Main partners: " + t).c_str());
                    }
                }
                L.heading("NOTES");
                GuiLabel(Rectangle{L.x, L.y, 60, 28}, "Notes");
                if (GuiTextBox(Rectangle{L.x + 60, L.y, L.w - 60, 28}, cityNotesBuf, 255, cityNotesEdit)) cityNotesEdit = !cityNotesEdit;
                c.notes = cityNotesBuf;
                L.y += 36;
                float bw2 = (L.w - 8) / 2;
                if (L.primary("Open city editor", 34)) { map.waitIdle(); cityEd.open(citySel, app.planet, app.world, app.doc); }
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
                L.check("Animate caravans and ships", &showCaravans);
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
                L.heading("YOUR ROUTES");
                {
                    int rt = routeTool;
                    GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 4) / 2, 30}, "Select / edit;Draw a route", &rt);
                    if (rt != routeTool) { routeTool = rt; drawFrom = -1; drawPts.clear(); }
                    L.y += 38;
                }
                if (routeTool == 1) {
                    GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 4) / 2, 26}, "Follow terrain;Straight lines", &drawMode);
                    L.y += 32;
                    L.note(drawFrom < 0 ? "Click the city the route starts from." :
                           "Click the map to add waypoints, then click the destination city. Backspace removes the last point, Esc cancels.");
                    L.note(drawMode == 0 ? "Follow terrain: between your points the route finds the quickest way over land, rivers and sea."
                                         : "Straight lines: the route goes exactly where you click. Turn on Wind flow to sail with the wind.");
                }
                std::string mine;
                for (int i = 0; i < (int)app.doc.customRoutes.size(); i++) {
                    const CustomRoute& cr = app.doc.customRoutes[i];
                    if (!mine.empty()) mine += ";";
                    std::string nm = cr.name;
                    if (nm.empty() && cr.from >= 0 && cr.to >= 0 && cr.from < (int)app.doc.cities.size() && cr.to < (int)app.doc.cities.size())
                        nm = app.doc.cities[cr.from].name + " > " + app.doc.cities[cr.to].name;
                    mine += cleanList(nm);
                }
                if (!app.doc.customRoutes.empty()) {
                    int prev = myRouteSel;
                    GuiListView(Rectangle{L.x, L.y, L.w, std::min(130.f, 26.f * app.doc.customRoutes.size() + 6)}, mine.c_str(), &myRouteScroll, &myRouteSel);
                    L.y += std::min(130.f, 26.f * app.doc.customRoutes.size() + 6) + 8;
                    if (myRouteSel != prev && myRouteSel >= 0) routeSel = econIndexOf(myRouteSel);
                } else if (routeTool == 0) L.note("Draw your own routes, or click any route on the map and press Edit to change it.");
                if (myRouteSel != lastMyRouteSel) {
                    lastMyRouteSel = myRouteSel;
                    routeNameEdit = false;
                    if (myRouteSel >= 0 && myRouteSel < (int)app.doc.customRoutes.size()) snprintf(routeNameBuf, sizeof routeNameBuf, "%s", app.doc.customRoutes[myRouteSel].name.c_str());
                }
                if (myRouteSel >= 0 && myRouteSel < (int)app.doc.customRoutes.size()) {
                    CustomRoute& cr = app.doc.customRoutes[myRouteSel];
                    int ei = econIndexOf(myRouteSel);
                    L.heading("SELECTED ROUTE");
                    GuiLabel(Rectangle{L.x, L.y, 50, 28}, "Name");
                    if (GuiTextBox(Rectangle{L.x + 52, L.y, L.w - 52, 28}, routeNameBuf, 63, routeNameEdit)) {
                        routeNameEdit = !routeNameEdit;
                        cr.name = routeNameBuf;
                    }
                    L.y += 36;
                    if (cr.from < (int)app.doc.cities.size() && cr.to < (int)app.doc.cities.size())
                        L.label(TextFormat("From %s to %s", app.doc.cities[cr.from].name.c_str(), app.doc.cities[cr.to].name.c_str()));
                    if (ei >= 0) {
                        const TradeRoute& r = app.econ.routes[ei];
                        L.label(TextFormat("Travel time: %.1f days", r.days));
                        L.label(TextFormat("Length: %s", formatKm(r.km)));
                        L.label(TextFormat("By water: %.0f%%", r.seaShare * 100));
                        if (r.legDays.size() > 1) {
                            std::string legs;
                            for (size_t k = 0; k < r.legDays.size(); k++) { if (k) legs += ", "; legs += TextFormat("%.1f", r.legDays[k]); }
                            L.note(("Days per leg: " + legs).c_str());
                        }
                    } else L.note("No way through in this era. Add waypoints or switch to straight lines.");
                    int md = cr.mode;
                    GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 4) / 2, 26}, "Follow terrain;Straight lines", &md);
                    L.y += 34;
                    if (md != cr.mode) { cr.mode = md; app.refreshCustomRoute(myRouteSel); routeSel = econIndexOf(myRouteSel); }
                    L.note("Drag the square ends onto other cities, drag the round waypoints, click the line to add a waypoint, right-click a waypoint to remove it.");
                    L.heading("CARGO");
                    const auto& G = goodDefs();
                    std::vector<std::pair<int, double>>& goods = cr.goods;
                    if (goods.empty() && ei >= 0) goods = app.econ.routes[ei].goods;   // start from what the simulation carried
                    int removeAt = -1;
                    for (int k = 0; k < (int)goods.size(); k++) {
                        if (goods[k].first < 0 || goods[k].first >= (int)G.size()) continue;
                        drawTextK(FontKind::Ui, G[goods[k].first].name, {L.x, L.y + 4}, 15, theme::kText);
                        const char* t = formatTonnes(goods[k].second);
                        Vector2 ts = measureTextK(FontKind::UiBold, t, 15);
                        drawTextK(FontKind::UiBold, t, {L.x + L.w - 64 - ts.x, L.y + 4}, 15, Color{240, 230, 210, 255});
                        if (GuiButton(Rectangle{L.x + L.w - 56, L.y, 26, 24}, "-")) goods[k].second = std::max(1.0, goods[k].second / 2);
                        if (GuiButton(Rectangle{L.x + L.w - 28, L.y, 28, 24}, "+")) goods[k].second *= 2;
                        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && CheckCollisionPointRec(mouse, Rectangle{L.x, L.y, L.w - 60, 24})) removeAt = k;
                        L.y += 28;
                    }
                    if (removeAt >= 0) goods.erase(goods.begin() + removeAt);
                    if (goods.empty()) L.note("Nothing carried yet. Add goods below.");
                    else L.note("- and + halve or double the tonnes. Right-click a good to drop it.");
                    {
                        std::string gl;
                        for (int g = 0; g < (int)G.size(); g++) { if (g) gl += ";"; gl += G[g].name; }
                        Rectangle dd{L.x, L.y, L.w - 90, 28};
                        if (GuiButton(Rectangle{L.x + L.w - 82, L.y, 82, 28}, "Add")) {
                            bool found = false;
                            for (auto& g : goods) if (g.first == addGoodUi) { g.second += 1000; found = true; }
                            if (!found) goods.push_back({addGoodUi, 1000});
                        }
                        L.y += 36;
                        // the dropdown is drawn after the rest of the panel so its list sits on top
                        pendingGoodDropdown = dd; pendingGoodList = gl;
                        if (addGoodOpen) GuiLock();
                    }
                    if (ei >= 0) {
                        TradeRoute& r = app.econ.routes[ei];
                        r.goods = goods; r.tonnes = 0; r.value = 0;
                        for (auto& g : goods) { r.tonnes += g.second; r.value += g.second * G[g.first].basePrice; }
                        L.label(TextFormat("Total: %s/yr worth %.3g coins", formatTonnes(r.tonnes), r.value));
                    }
                    float bw = (L.w - 8) / 2;
                    if (GuiButton(Rectangle{L.x, L.y, bw, 28}, "Reverse")) { std::swap(cr.from, cr.to); std::reverse(cr.via.begin(), cr.via.end()); app.refreshCustomRoute(myRouteSel); }
                    if (GuiButton(Rectangle{L.x + bw + 8, L.y, bw, 28}, "Clear waypoints")) { cr.via.clear(); app.refreshCustomRoute(myRouteSel); }
                    L.y += 36;
                    if (L.button("Delete this route")) {
                        int del = myRouteSel;
                        app.doc.customRoutes.erase(app.doc.customRoutes.begin() + del);
                        app.econ.routes.erase(std::remove_if(app.econ.routes.begin(), app.econ.routes.end(), [&](const TradeRoute& r) { return r.custom && r.customIndex == del; }), app.econ.routes.end());
                        for (auto& r : app.econ.routes) if (r.custom && r.customIndex > del) r.customIndex--;
                        myRouteSel = -1; routeSel = -1;
                    }
                } else if (routeSel >= 0 && routeSel < (int)app.econ.routes.size() && !app.econ.routes[routeSel].custom) {
                    const TradeRoute& r = app.econ.routes[routeSel];
                    L.heading("SELECTED ROUTE");
                    if (r.from < (int)app.doc.cities.size() && r.to < (int)app.doc.cities.size())
                        L.label(TextFormat("From %s to %s", app.doc.cities[r.from].name.c_str(), app.doc.cities[r.to].name.c_str()));
                    L.label(TextFormat("Travel time: %.0f days", r.days));
                    L.label(TextFormat("By water: %.0f%%", r.seaShare * 100));
                    L.label(TextFormat("Cargo: %s/yr", formatTonnes(r.tonnes)));
                    std::string g;
                    for (size_t k = 0; k < r.goods.size() && k < 5; k++) { if (k) g += ", "; g += goodDefs()[r.goods[k].first].name; }
                    L.note(("Carries: " + g).c_str());
                    float bw = (L.w - 8) / 2;
                    if (GuiButton(Rectangle{L.x, L.y, bw, 30}, "Edit this route")) {
                        CustomRoute cr; cr.from = r.from; cr.to = r.to; cr.goods = r.goods; cr.mode = 0;
                        // keep its shape: a few waypoints along the simulated path
                        int n = (int)r.cells.size();
                        for (int k = 1; k <= 3 && n > 8; k++) { int c = r.cells[n * k / 4]; cr.via.push_back({World::cellLat(c / World::W), World::cellLon(c % World::W)}); }
                        app.doc.customRoutes.push_back(cr);
                        int idx = (int)app.doc.customRoutes.size() - 1;
                        app.econ.routes.erase(app.econ.routes.begin() + routeSel);
                        app.refreshCustomRoute(idx);
                        myRouteSel = idx; routeSel = econIndexOf(idx);
                        setStatus("This route is now yours: drag its points to change it");
                    }
                    if (GuiButton(Rectangle{L.x + bw + 8, L.y, bw, 30}, "Remove route")) {
                        app.doc.hiddenRoutes.push_back({r.from, r.to});
                        app.econ.routes.erase(app.econ.routes.begin() + routeSel);
                        routeSel = -1;
                    }
                    L.y += 38;
                }
                L.heading("BIGGEST ROUTES");
                std::string ritems;
                std::vector<int> listed;
                for (int i = 0; i < (int)app.econ.routes.size() && (int)listed.size() < 40; i++) {
                    const TradeRoute& r = app.econ.routes[i];
                    if (r.custom || r.from >= (int)app.doc.cities.size() || r.to >= (int)app.doc.cities.size()) continue;
                    if (!ritems.empty()) ritems += ";";
                    ritems += cleanList(app.doc.cities[r.from].name) + " > " + cleanList(app.doc.cities[r.to].name);
                    listed.push_back(i);
                }
                int li = -1;
                for (int k = 0; k < (int)listed.size(); k++) if (listed[k] == routeSel) li = k;
                int prevLi = li;
                GuiListView(Rectangle{L.x, L.y, L.w, 130}, ritems.c_str(), &routeScroll, &li);
                L.y += 138;
                if (li != prevLi && li >= 0 && li < (int)listed.size()) { routeSel = listed[li]; myRouteSel = -1; }
                if (!app.doc.hiddenRoutes.empty() && L.button(TextFormat("Bring back %d removed routes", (int)app.doc.hiddenRoutes.size()))) {
                    app.doc.hiddenRoutes.clear(); app.simulateTrade();
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
                    float wbtn = (float)uiMeasure(links[k].c_str(), 16) + 16;
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
            // tool buttons in groups, with the active one highlighted
            auto toolRow = [&](const char* const* names, const int* ids, int n) {
                float bw = (L.w - 6 * (n - 1)) / n;
                for (int i = 0; i < n; i++) {
                    Rectangle r{L.x + i * (bw + 6), L.y, bw, 30};
                    bool on = tool == ids[i];
                    if (on) GuiSetState(STATE_PRESSED);
                    if (GuiButton(r, names[i])) tool = ids[i];
                    GuiSetState(STATE_NORMAL);
                }
                L.y += 36;
            };
            L.heading("LAND AND SEA");
            { const char* n[] = {"Make land", "Make sea"}; int id[] = {5, 6}; toolRow(n, id, 2); }
            { const char* n[] = {"Draw land shape", "Draw sea shape"}; int id[] = {12, 13}; toolRow(n, id, 2); }
            L.heading("SHAPE");
            { const char* n[] = {"Raise", "Lower", "Mountains"}; int id[] = {1, 2, 10}; toolRow(n, id, 3); }
            { const char* n[] = {"Hills", "Smooth", "Flatten"}; int id[] = {11, 3, 4}; toolRow(n, id, 3); }
            L.heading("STAMPS");
            { const char* n[] = {"Crater", "Volcano", "Mesa", "Pan"}; int id[] = {7, 8, 9, 0}; toolRow(n, id, 4); }
            static const char* help[14] = {
                "Pan: drag to move the map.",
                "Raise land where you paint. Hold Shift to lower.",
                "Lower land where you paint. Hold Shift to raise.",
                "Smooth out bumps and sharp edges.",
                "Flatten to the height where you start.",
                "Paint new land up to the land height. Higher ground is left alone, and Roughness gives natural coasts. Shift paints sea.",
                "Paint sea down to the sea depth. Lower ground is left alone. Shift paints land.",
                "Click to stamp a meteor crater.", "Click to stamp a volcano.", "Click to stamp a flat-topped mesa.",
                "Paint a mountain range: drag along where the ridges should run. Shift lowers.",
                "Add rolling hills and hollows.",
                "Drag a loop round the shape of a continent or island; it fills with land when you let go. Shift makes sea.",
                "Drag a loop round a sea, bay or lake; it floods when you let go. Shift makes land."};
            L.note(help[std::max(0, std::min(13, tool))]);
            L.heading("BRUSH");
            if (tool != 12 && tool != 13) { L.label(TextFormat("Size: %s", formatKm(brushKm))); L.slider(&brushT, 0, 1); }
            if (tool == 1 || tool == 2 || tool == 10 || tool == 11 || tool == 3 || tool == 4 || tool == 5 || tool == 6) {
                L.label(TextFormat("Strength: %.0f%%", strengthT * 100)); L.slider(&strengthT, 0, 1);
            }
            if (tool != 0 && (tool < 7 || tool > 9)) {
                L.label(TextFormat("Edge: %s", hardnessUi < 0.33f ? "soft" : hardnessUi < 0.66f ? "medium" : "hard")); L.slider(&hardnessUi, 0, 1);
            }
            if (tool == 1 || tool == 2 || tool == 5 || tool == 6 || tool == 10 || tool == 11 || tool == 12 || tool == 13) {
                L.label(TextFormat("Roughness: %.0f%%", roughUi * 100)); L.slider(&roughUi, 0, 1);
            }
            if (tool == 5 || tool == 12 || tool == 6 || tool == 13) {
                L.label(TextFormat("Land height: %.0f m", landHeightM)); L.slider(&landHeightT, 0, 1);
                L.label(TextFormat("Sea depth: %.0f m", seaDepthM)); L.slider(&seaDepthT, 0, 1);
            }
            L.heading("AFTER SCULPTING");
            float bw2 = (L.w - 8) / 2;
            if (GuiButton(Rectangle{L.x, L.y, bw2, 30}, "Undo (Ctrl+Z)")) doUndo();
            if (GuiButton(Rectangle{L.x + bw2 + 8, L.y, bw2, 30}, "Update climate")) app.recalcClimate();
            L.y += 38;
            L.note("Rivers, climate and resources follow the terrain when you press Update climate. [ and ] resize the brush.");
        }
        if (pendingGoodDropdown.width > 0) {
            if (addGoodOpen) GuiUnlock();
            if (GuiDropdownBox(pendingGoodDropdown, pendingGoodList.c_str(), &addGoodUi, addGoodOpen)) addGoodOpen = !addGoodOpen;
            pendingGoodDropdown.width = 0;
        } else addGoodOpen = false;
        if (lockHidden) GuiUnlock();
        EndScissorMode();
        panelContentH[tab] = L.y - layoutStartY + 30;
        {
            float viewH = contentBottom - contentTop;
            if (panelContentH[tab] > viewH) {   // scrollbar
                float barH = std::max(30.f, viewH * viewH / panelContentH[tab]);
                float barY = contentTop + (viewH - barH) * (pScroll / (panelContentH[tab] - viewH));
                DrawRectangleRounded({panel.x + panel.width - 9, barY, 4, barH}, 1, 4, Color{80, 90, 112, 200});
                // fade at the bottom edge to hint there is more
                if (pScroll < panelContentH[tab] - viewH - 2)
                    DrawRectangleGradientV((int)panel.x, (int)contentBottom - 30, (int)panel.width - 1, 30, Fade(theme::kPanel, 0), theme::kPanel);
            }
        }
        if ((MapStyle)styleUi != map.style()) map.setStyle((MapStyle)styleUi);
        static const Overlay overlayList[] = {Overlay::None, Overlay::Plates, Overlay::Rock, Overlay::Earthquakes, Overlay::Resource,
                                              Overlay::Temperature, Overlay::Rainfall, Overlay::Biomes, Overlay::Wind, Overlay::Currents,
                                              Overlay::Political, Overlay::Population, Overlay::Religion, Overlay::Culture, Overlay::Storms};
        const int overlayCount = (int)(sizeof(overlayList) / sizeof(overlayList[0]));
        if (overlayUi < 0) overlayUi = 0;
        {
            Overlay o = overlayList[std::min(overlayUi, overlayCount - 1)];
            bool monthly = o == Overlay::Temperature || o == Overlay::Rainfall || o == Overlay::Wind || o == Overlay::Storms;
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
                int tw = std::max(uiMeasure(t1.c_str(), 18), uiMeasure(t2.c_str(), 16));
                theme::card(Rectangle{p.x + 12, p.y - 54, (float)tw + 20, 50});
                uiText(t1.c_str(), (int)p.x + 22, (int)p.y - 48, 18, theme::kAccentHi);
                uiText(t2.c_str(), (int)p.x + 22, (int)p.y - 26, 16, theme::kText);
            }
        }
        if (showCompass) drawCompass({panel.x + panel.width + 70, (float)sh - 150}, 42, map.style() == MapStyle::Fantasy);
        if (showTitle) {
            std::string t = nameBuf;
            for (char& ch : t) ch = (char)std::toupper((unsigned char)ch);
            float fsz = 44, spacing = 8;
            Font tf = fontFor(FontKind::Serif, fsz);
            Vector2 ts = MeasureTextEx(tf, t.c_str(), fsz, spacing);
            float cx = (panel.x + panel.width + sw) / 2;
            bool fantasy = map.style() == MapStyle::Fantasy;
            Rectangle box{cx - ts.x / 2 - 30, 14, ts.x + 60, ts.y + 20};
            DrawRectangleRec(box, fantasy ? Color{238, 224, 186, 235} : Fade(BLACK, 0.55f));
            DrawRectangleLinesEx(box, 2, fantasy ? Color{80, 50, 30, 255} : RAYWHITE);
            DrawRectangleLinesEx({box.x + 5, box.y + 5, box.width - 10, box.height - 10}, 1, fantasy ? Color{80, 50, 30, 255} : RAYWHITE);
            DrawTextEx(tf, t.c_str(), {cx - ts.x / 2, 24}, fsz, spacing, fantasy ? Color{80, 50, 30, 255} : RAYWHITE);
        }
        if (showMini && globeMini) {
            // orthographic globe centred on the view, redrawn on the CPU when the view moves
            if (std::fabs(globeLat - view.centerLat) > 0.3 || std::fabs(std::remainder(globeLon - view.centerLon, 360.0)) > 0.3 || GetTime() - globeTime > 2) {
                globeLat = view.centerLat; globeLon = view.centerLon; globeTime = GetTime();
                const int GS = 200;
                double phi0 = globeLat * kPi / 180, lam0 = globeLon * kPi / 180;
                for (int y = 0; y < GS; y++)
                    for (int x = 0; x < GS; x++) {
                        unsigned char* o = &globePx[(y * GS + x) * 4];
                        double X = (x + 0.5) / GS * 2 - 1, Y = 1 - (y + 0.5) / GS * 2, rho = std::sqrt(X * X + Y * Y);
                        if (rho > 1) { o[3] = 0; continue; }
                        double c = std::asin(std::min(1.0, rho));
                        double lat = rho < 1e-9 ? phi0 : std::asin(std::cos(c) * std::sin(phi0) + Y * std::sin(c) * std::cos(phi0) / rho);
                        double lon = lam0 + std::atan2(X * std::sin(c), rho * std::cos(phi0) * std::cos(c) - Y * std::sin(phi0) * std::sin(c));
                        unsigned char rgb[3] = {20, 40, 80};
                        map.worldColor(lat * 180 / kPi, wrapLon(lon * 180 / kPi), rgb);
                        double shade = 0.45 + 0.55 * std::cos(c);
                        o[0] = (unsigned char)(rgb[0] * shade); o[1] = (unsigned char)(rgb[1] * shade); o[2] = (unsigned char)(rgb[2] * shade); o[3] = 255;
                    }
                if (!haveGlobe) {
                    Image gi{globePx.data(), GS, GS, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                    globeTex = LoadTextureFromImage(gi); haveGlobe = true;
                    SetTextureFilter(globeTex, TEXTURE_FILTER_BILINEAR);
                } else UpdateTexture(globeTex, globePx.data());
            }
            float gs = miniRect.height + 40;
            Rectangle gr{miniRect.x + miniRect.width - gs, miniRect.y, gs, gs};
            DrawCircle((int)(gr.x + gs / 2), (int)(gr.y + gs / 2), gs / 2 + 4, Fade(Color{120, 170, 255, 255}, 0.35f));
            DrawTexturePro(globeTex, Rectangle{0, 0, 200, 200}, gr, Vector2{0, 0}, 0, WHITE);
            float vr = (float)std::min(gs / 2.0, (sh / view.pxPerDeg) / 180.0 * gs / 2);
            DrawCircleLines((int)(gr.x + gs / 2), (int)(gr.y + gs / 2), std::max(3.f, vr), YELLOW);
        } else if (showMini) {
            theme::card(Rectangle{miniRect.x - 4, miniRect.y - 4, miniRect.width + 8, miniRect.height + 8});
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
        // ---- Card beside the selected city ----
        if (citySel >= 0 && citySel < (int)app.doc.cities.size() && (tab == TabCities || tab == TabTrade) && showCities) {
            const City& c = app.doc.cities[citySel];
            Vector2 p = toScreen(c.lat, c.lon);
            if (p.x > panel.x + panel.width && p.x < sw && p.y > 0 && p.y < sh) {
                DrawCircleLinesV(p, 11, theme::kAccentHi);
                DrawCircleLinesV(p, 12, theme::kAccentHi);
                const int era = std::max(0, std::min(4, app.doc.era));
                std::vector<std::pair<std::string, Color>> lines;
                std::string nat = c.nation >= 0 && c.nation < (int)app.doc.nations.size() ? app.doc.nations[c.nation].name : std::string("Independent");
                lines.push_back({nat + (c.capital ? "  \xE2\x80\xA2  capital" : ""), theme::kMuted});
                lines.push_back({std::string(citySizeClass(c.population, era)) + " of " + withCommas(app.doc.populationAt(c, year)), theme::kText});
                if (app.econ.valid && app.econCities == app.doc.cities.size()) {
                    auto ex = app.econ.topExports(citySel, 3), im = app.econ.topImports(citySel, 3);
                    auto join = [&](const std::vector<std::pair<int, double>>& v) {
                        std::string t;
                        for (auto& g : v) { if (!t.empty()) t += ", "; t += goodDefs()[g.first].name; }
                        return t.empty() ? std::string("nothing") : t;
                    };
                    lines.push_back({"Sells: " + join(ex), Color{150, 220, 170, 255}});
                    lines.push_back({"Buys: " + join(im), Color{240, 190, 150, 255}});
                } else lines.push_back({"Simulate trade to see what it buys and sells", theme::kMuted});
                float tw = measureTextK(FontKind::Serif, c.name.c_str(), 24).x;
                for (auto& l : lines) tw = std::max(tw, measureTextK(FontKind::Ui, l.first.c_str(), 14).x);
                float bh = 40 + lines.size() * 19.f + 8;
                Rectangle box{p.x + 18, p.y - bh / 2, tw + 28, bh};
                if (box.x + box.width > sw - 8) box.x = p.x - 18 - box.width;
                box.y = std::max(48.f, std::min(box.y, sh - 40 - bh));
                theme::card(box, theme::kAccent);
                drawTextK(FontKind::Serif, c.name.c_str(), {box.x + 14, box.y + 8}, 24, theme::kAccentHi);
                for (size_t k = 0; k < lines.size(); k++) drawTextK(FontKind::Ui, lines[k].first.c_str(), {box.x + 14, box.y + 40 + k * 19.f}, 14, lines[k].second);
            }
        }
        // ---- Readouts ----
        const int fs = 15;
        double kmPerPx = 111.195 * R / 6371.0 * std::cos(view.centerLat * kPi / 180) / view.pxPerDeg;
        const char* info = TextFormat("1 px = %s   Tiles %d   Drawing %d   %d FPS", formatKm(kmPerPx), map.cached(), map.pending(), GetFPS());
        int iw = uiMeasure(info, fs);
        (void)iw;   // shown in the status bar
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
            std::string windLine;
            {
                float u, v; int mo = std::min(12, (int)std::round(monthUi));
                if (windFlow.velocity(app.world, mo, mLat, mLon, u, v)) {
                    static const char* dirs[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
                    double from = std::atan2(-u, -v) * 180 / kPi; if (from < 0) from += 360;
                    windLine = TextFormat("Wind: %.0f km/h from the %s (%s)", std::sqrt(u * u + v * v) * 3.6, dirs[(int)std::lround(from / 45) % 8], kMonths[mo]);
                    if (app.world.elev[c] <= 0 && !app.world.currentU.empty()) {
                        float cu = (float)World::sample(app.world.currentU, mLat, mLon), cv = (float)World::sample(app.world.currentV, mLat, mLon);
                        double to = std::atan2(cu, cv) * 180 / kPi; if (to < 0) to += 360;
                        windLine += TextFormat("   Current: %.1f km/h to the %s", std::sqrt(cu * cu + cv * cv) * 3.6, dirs[(int)std::lround(to / 45) % 8]);
                    }
                }
            }
            std::string lines[7] = {
                TextFormat("%.4f %c, %.4f %c", std::fabs(mLat), mLat >= 0 ? 'N' : 'S', std::fabs(mLon), mLon >= 0 ? 'E' : 'W'),
                TextFormat("%s %.0f m", e > 0 ? "Elevation" : "Depth", std::fabs(e)),
                TextFormat("Plate %d (%s), moving %.1f cm/yr", app.world.plateId[c] + 1, pl.continental ? "continental" : "oceanic", v.length() * 5),
                TextFormat("Rock: %s, %.0f million yrs", World::rockName((Rock)app.world.rock[c]), app.world.rockAge[c]),
                TextFormat("Nearest plate edge: %s%s", app.world.distBoundaryKm[c] > 1e8 ? "over 1500 km" : formatKm(app.world.distBoundaryKm[c]),
                    app.world.discharge.empty() || app.world.elev[c] <= 0 ? "" :
                    app.world.lakeType[c] == 1 ? "   Lake" : app.world.lakeType[c] == 2 ? "   Salt flat" :
                    app.world.discharge[c] > 40 ? TextFormat("   River flow %.0f m3/s", app.world.discharge[c]) : ""), clim, windLine};
            int tw = 0;
            for (auto& s : lines) tw = std::max(tw, uiMeasure(s.c_str(), fs));
            int nl = windLine.empty() ? 6 : 7;
            int bh = nl * 20 + 16;
            theme::card(Rectangle{(float)sw - tw - 34, (float)sh - bh - 42, (float)tw + 24, (float)bh});
            for (int i = 0; i < nl; i++) uiText(lines[i].c_str(), sw - tw - 22, sh - bh - 34 + i * 20, fs, i == 1 ? theme::kAccentHi : theme::kText);
        }
        if ((hoverEvent >= 0 || hoverPin >= 0) && hoverCity < 0 && hoverDeposit < 0) {
            std::string t1, t2;
            if (hoverPin >= 0) { const Article& a = app.doc.articles[hoverPin]; t1 = a.title; t2 = std::string(loreKindName(a.kind)) + " - click to read"; }
            else { const HistEvent& e = app.doc.events[hoverEvent]; t1 = TextFormat("%s (%d)", e.title.c_str(), e.year); t2 = e.text; }
            if (t2.size() > 80) t2 = t2.substr(0, 77) + "...";
            int tw = std::max(uiMeasure(t1.c_str(), 18), uiMeasure(t2.c_str(), 16));
            int bx = (int)mouse.x + 16, by = (int)mouse.y + 10;
            if (bx + tw + 20 > sw) bx = (int)mouse.x - tw - 36;
            theme::card(Rectangle{(float)bx, (float)by, (float)(tw + 20), (float)(52)});
            uiText(t1.c_str(), bx + 10, by + 6, 18, theme::kAccentHi);
            uiText(t2.c_str(), bx + 10, by + 28, 16, theme::kText);
        }
        if (hoverCity >= 0 && hoverDeposit < 0 && hoverCity != citySel) {
            const City& c = app.doc.cities[hoverCity];
            std::string t1 = c.name + (c.capital ? " (capital)" : "");
            std::string t2 = app.doc.populationAt(c, year) < c.population
                ? std::string(TextFormat("Population %s in %d (%s today)", withCommas(app.doc.populationAt(c, year)), year, withCommas(c.population)))
                : std::string(TextFormat("%s, population %s", citySizeClass(c.population, app.doc.era), withCommas(c.population)));
            std::string t3 = c.nation >= 0 && c.nation < (int)app.doc.nations.size() ? app.doc.nations[c.nation].name : std::string("Independent");
            int tw = std::max({uiMeasure(t1.c_str(), 18), uiMeasure(t2.c_str(), 18), uiMeasure(t3.c_str(), 18)});
            int bx = (int)mouse.x + 16, by = (int)mouse.y + 10;
            if (bx + tw + 20 > sw) bx = (int)mouse.x - tw - 36;
            theme::card(Rectangle{(float)bx, (float)by, (float)(tw + 20), (float)(3 * 22 + 12)});
            uiText(t1.c_str(), bx + 10, by + 6, 18, theme::kAccentHi);
            uiText(t2.c_str(), bx + 10, by + 28, 18, theme::kText);
            uiText(t3.c_str(), bx + 10, by + 50, 18, theme::kText);
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
            for (auto& x : t) tw = std::max(tw, uiMeasure(x.c_str(), 18));
            int bx = (int)mouse.x + 16, by = (int)mouse.y + 10;
            if (bx + tw + 20 > sw) bx = (int)mouse.x - tw - 36;
            theme::card(Rectangle{(float)bx, (float)by, (float)(tw + 20), (float)(4 * 22 + 12)});
            for (int i = 0; i < 4; i++) uiText(t[i].c_str(), bx + 10, by + 6 + i * 22, 18, i == 0 ? theme::kAccentHi : theme::kText);
        }
        drawScaleBar(view, R, (int)(panel.x + panel.width) + 24, sh - 60, th.text);
        if (!droppedPath.empty()) {
            Rectangle box{(float)sw / 2 - 300, (float)sh / 2 - 110, 600, 220};
            DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.45f));
            theme::card(box, theme::kAccent);
            uiText("Use this image as...", (int)box.x + 20, (int)box.y + 16, 24, theme::kText);
            uiText(GetFileName(droppedPath.c_str()), (int)box.x + 20, (int)box.y + 50, 18, theme::kMuted);
            uiText("Images should be a whole-world map (twice as wide as tall). Dark = sea, bright = high land.", (int)box.x + 20, (int)box.y + 78, 15, theme::kMuted);
            if (GuiButton(Rectangle{box.x + 20, box.y + 120, 270, 40}, "The land shape (regenerate)")) {
                app.ui.importPath = droppedPath;
                {   // match the ocean cover to the dark part of the image, so the coast follows the drawing
                    std::vector<float> v; int w = 0, h = 0;
                    if (loadImportImage(droppedPath, v, w, h)) {
                        double dark = 0, all = 0;
                        for (int y = 0; y < h; y++) {
                            double wt = std::cos((90 - (y + 0.5) * 180.0 / h) * kPi / 180);
                            for (int x = 0; x < w; x++) { all += wt; if (v[(size_t)y * w + x] < 0.4f) dark += wt; }
                        }
                        if (all > 0) oceanUi = (float)clampd(100 * dark / all, 5, 95);
                    }
                }
                app.ui.radiusKm = std::round(radiusUi); app.ui.oceanPercent = oceanUi; app.ui.mountains = mountUi;
                app.regenerate();
                setStatus("World rebuilt from the image. Adjust Ocean cover and regenerate to move the coasts.");
                droppedPath.clear();
            } else if (GuiButton(Rectangle{box.x + 310, box.y + 120, 270, 40}, "A tracing layer on top")) {
                if (haveTrace) UnloadTexture(traceTex);
                traceTex = LoadTexture(droppedPath.c_str());
                haveTrace = traceTex.id != 0; showTrace = true;
                setStatus(haveTrace ? "Tracing layer added (Map tab to adjust)" : "Couldn't load that image");
                droppedPath.clear();
            } else if (GuiButton(Rectangle{box.x + 20, box.y + 170, 560, 32}, "Cancel")) droppedPath.clear();
        }
        if (showStats) {
            Rectangle box{(float)sw / 2 - 470, 60, 940, (float)std::min(820, sh - 120)};
            DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.45f));
            theme::card(box, theme::kAccent);
            uiText("World statistics", (int)box.x + 20, (int)box.y + 16, 26, theme::kText);
            if (GuiButton(Rectangle{box.x + box.width - 110, box.y + 14, 90, 30}, "Close")) showStats = false;
            int x0 = (int)box.x + 20, y = (int)box.y + 60;
            auto text = [&](const char* t, int x, int yy, int fs = 18) { uiText(t, x, yy, fs, theme::kText); };
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
                uiText(biomeName((Biome)b), x0, y, 15, theme::kMuted);
                uiText(TextFormat("%.1f%%", f * 100), x0 + 196 + (int)(f * 600), y, 15, theme::kMuted);
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
                    uiText(n.name.c_str(), x0, y, 15, theme::kMuted);
                    uiText(TextFormat("%lld people, %.0fk km2", pop[ord[k]], area[ord[k]] / 1000), x0 + 196 + (int)(500.0 * pop[ord[k]] / maxP), y, 15, theme::kMuted);
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
                "Sculpt tab:  [ and ] change brush size, Shift reverses a brush, Ctrl+Z undo",
                "Layers:  the buttons along the top of the map; Wind flow and Current flow work over any layer",
                "Typing:  click a text box to type, Escape or click elsewhere to stop",
                "", "Tabs:",
                "  World - generate planets, templates, sea level, tectonics, move plates, drift, statistics",
                "  Map - style, overlays, labels, grid, hex grid, compass, find, measure",
                "  Sculpt - draw land and sea shapes, mountains, hills, raise, lower, smooth; update climate afterwards",
                "  Resources - where each resource forms and every deposit",
                "  Climate - tilt, global temperature, rainfall, month, and climate overlays",
                "  Nations / Cities / Peoples - countries, territory, cities, cultures, religions, rulers",
                "  Trade - simulate the economy for an era; draw and edit your own routes, cargo and waypoints",
                "  History - the year slider, events that change the map, generated histories",
                "  Lore - wiki articles, map pins and [[links]]",
                "  File - save, load, autosave, export images, heightmaps and data",
            };
            int n = (int)(sizeof(help) / sizeof(help[0]));
            Rectangle box{(float)sw / 2 - 430, (float)sh / 2 - n * 13.f - 20, 860, n * 26.f + 40};
            DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.45f));
            theme::card(box, theme::kAccent);
            for (int i = 0; i < n; i++) uiText(help[i], (int)box.x + 24, (int)box.y + 20 + i * 26, i == 0 ? 22 : 17, i == 0 ? theme::kAccentHi : theme::kText);
        }
        if (showIntro) {
            // First-run guide: the order things are usually built in, with buttons that jump to each tab
            static const struct { int tab; const char* title; const char* text; } steps[] = {
                {TabWorld, "Shape the planet", "Pick a template or set size, oceans and plates, then Generate."},
                {TabSculpt, "Sculpt the land", "Paint continents, raise mountains, carve seas and coastlines."},
                {TabClimate, "Check the climate", "Seasons, winds, rain and currents follow from your terrain."},
                {TabNations, "Add peoples and nations", "Generate cultures, nations and cities, or place your own."},
                {TabTrade, "Trade and history", "Simulate trade routes and add events that change the map."},
            };
            float cw = 560, chh = 120 + 5 * 64;
            float cx = (panel.x + panel.width + sw) / 2 - cw / 2, cy = sh / 2.f - chh / 2;
            DrawRectangle((int)(panel.x + panel.width), 0, sw, sh, Fade(BLACK, 0.35f));
            theme::card(Rectangle{cx, cy, cw, chh}, theme::kAccent);
            drawTextK(FontKind::Serif, "Welcome to Worldbuilder", {cx + 28, cy + 20}, 34, theme::kAccentHi);
            drawTextK(FontKind::Ui, "A good order to build a world in. You can come back to any step later.", {cx + 30, cy + 62}, 15, theme::kMuted);
            for (int i = 0; i < 5; i++) {
                float y = cy + 98 + i * 64;
                DrawCircleV({cx + 46, y + 20}, 16, Color{40, 46, 62, 255});
                DrawCircleLinesV({cx + 46, y + 20}, 16, theme::kAccent);
                char num[4]; snprintf(num, sizeof num, "%d", i + 1);
                Vector2 nt = measureTextK(FontKind::UiBold, num, 16);
                drawTextK(FontKind::UiBold, num, {cx + 46 - nt.x / 2, y + 11}, 16, theme::kAccentHi);
                drawTextK(FontKind::UiBold, steps[i].title, {cx + 76, y + 2}, 18, theme::kText);
                drawTextK(FontKind::Ui, steps[i].text, {cx + 76, y + 25}, 14, theme::kMuted);
                if (GuiButton(Rectangle{cx + cw - 92, y + 6, 66, 28}, "Go")) { tab = steps[i].tab; showIntro = false; }
            }
            if (GuiButton(Rectangle{cx + cw - 180, cy + chh - 46, 154, 32}, "Got it")) showIntro = false;
            if (!showIntro) SaveFileText("worldbuilder-intro-seen.txt", (char*)"The getting-started guide has been shown. Delete this file to see it again.\n");
        }
        if (pendingPlateRebuild > 0) {
            float cx = (panel.x + panel.width + sw) / 2;
            theme::card(Rectangle{cx - 190, (float)sh / 2 - 34, 380, 68}, theme::kAccent);
            const char* t = "Moving plates and rebuilding the land...";
            Vector2 ts = measureTextK(FontKind::UiBold, t, 18);
            drawTextK(FontKind::UiBold, t, {cx - ts.x / 2, (float)sh / 2 - 10}, 18, theme::kText);
            if (--pendingPlateRebuild == 0) { app.rebuildPlates(); app.labelsDirty = true; setStatus("Plates moved. Ctrl+Z doesn't undo this - use Reset in the World tab"); }
        }
        if (pendingExport > 0) {
            theme::card(Rectangle{(float)sw / 2 - 160, (float)sh / 2 - 30, 320, 60}, theme::kAccent);
            uiText("Exporting...", sw / 2 - uiMeasure("Exporting...", 24) / 2, sh / 2 - 12, 24, theme::kText);
        }
        if (GetTime() - statusTime < 6) {
            int tw = uiMeasure(status.c_str(), 20);
            float cx = (panel.x + panel.width + sw) / 2;
            theme::card(Rectangle{cx - tw / 2 - 16, 50, (float)tw + 32, 38}, theme::kAccent);
            uiText(status.c_str(), (int)(cx - tw / 2), 59, 20, theme::kAccentHi);
        }
        {
            // What can I do right now? One line of help for the current tool, plus where the mouse is.
            std::string hint = "Scroll to zoom   \xE2\x80\xA2   Right-drag or WASD to move   \xE2\x80\xA2   F1 for help";
            if (tab == TabSculpt && (tool == 12 || tool == 13)) hint = "Drag a loop round the shape   \xE2\x80\xA2   it fills when you let go   \xE2\x80\xA2   Shift swaps land and sea   \xE2\x80\xA2   Ctrl+Z undo";
            else if (tab == TabSculpt && tool > 0) hint = "Left-drag to paint   \xE2\x80\xA2   Shift reverses   \xE2\x80\xA2   [ and ] change brush size   \xE2\x80\xA2   Ctrl+Z undo   \xE2\x80\xA2   Right-drag to move";
            else if (false) hint = "Left-drag to paint   \xE2\x80\xA2   [ and ] change brush size   \xE2\x80\xA2   Ctrl+Z undo   \xE2\x80\xA2   Right-drag to move";
            else if (paintingTerritory) hint = "Left-drag to paint this nation's territory   \xE2\x80\xA2   Right-drag to move";
            else if (placingCity) hint = "Click on land to found a city";
            else if (placingEvent) hint = "Click the map where the event happened";
            else if (pinningLore) hint = "Click the map to pin this article";
            else if (measuring) hint = "Click to add measuring points   \xE2\x80\xA2   Right-click to clear";
            else if (relPick) hint = "Click another nation to set the relation";
            else if (tab == TabTrade) hint = "Click a city to start a route   \xE2\x80\xA2   Click a route to edit it   \xE2\x80\xA2   Right-drag to move";
            char right[200] = "";
            double kmPx = 111.195 * R / 6371.0 * std::cos(view.centerLat * kPi / 180) / view.pxPerDeg;
            if (hit) snprintf(right, sizeof right, "%.2f\xC2\xB0%c  %.2f\xC2\xB0%c     1 px = %s     %d FPS", std::fabs(mLat), mLat >= 0 ? 'N' : 'S', std::fabs(mLon), mLon >= 0 ? 'E' : 'W', formatKm(kmPx), GetFPS());
            else snprintf(right, sizeof right, "1 px = %s     %d FPS", formatKm(kmPx), GetFPS());
            theme::statusBar(Rectangle{panel.x + panel.width, (float)sh - 30, (float)sw - panel.x - panel.width, 30}, hint.c_str(), right);
        }
        EndDrawing();
#ifdef WB_SHOTS
        if (++shotFrames == (getenv("WB_FRAMES") ? atoi(getenv("WB_FRAMES")) : 30)) { TakeScreenshot(getenv("WB_SHOT") ? getenv("WB_SHOT") : "shot.png"); break; }
#endif
        typing = seedEdit || nameEdit || nationNameEdit || nationNotesEdit || cityNameEdit || cityNotesEdit || evTitleEdit || evTextEdit ||
                 loreTitleEdit || loreBodyEdit || loreSearchEdit || findEdit || routeNameEdit || cultureNameEdit || religionNameEdit || religionNotesEdit || customNameEdit || customSettingEdit;

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
            } else if (kind == 5) {
                std::ofstream o(base + "_chronicle.md");
                o << "# " << nameBuf << "\n\n";
                o << "A world of radius " << app.planet.radiusKm() << " km; " << app.stats.landPercent << "% land.\n\n";
                if (!app.doc.nations.empty()) {
                    o << "## Nations\n\n";
                    for (size_t n = 0; n < app.doc.nations.size(); n++) {
                        const Nation& nt = app.doc.nations[n];
                        o << "### " << nt.name << "\n\n" << kGovernments[nt.government];
                        if (nt.culture >= 0 && nt.culture < (int)app.doc.cultures.size()) o << ", " << app.doc.cultures[nt.culture].name << " culture";
                        o << ".\n\n";
                        for (auto& c : app.doc.cities) if (c.nation == (int)n && c.capital) o << "Capital: " << c.name << " (population " << c.population << ").\n\n";
                        if (!nt.notes.empty()) o << nt.notes << "\n\n";
                        if (!nt.rulers.empty()) {
                            o << "Rulers:\n\n";
                            for (auto& r : nt.rulers) o << "- " << r.start << " to " << r.end << ": " << r.name << "\n";
                            o << "\n";
                        }
                    }
                }
                if (!app.doc.religions.empty()) {
                    o << "## Religions\n\n";
                    for (auto& r : app.doc.religions) o << "- **" << r.name << "** (founded " << r.founded << "). " << r.notes << "\n";
                    o << "\n";
                }
                if (!app.doc.events.empty()) {
                    o << "## Timeline\n\n";
                    std::vector<HistEvent> ev = app.doc.events;
                    std::sort(ev.begin(), ev.end(), [](const HistEvent& a, const HistEvent& b) { return a.year < b.year; });
                    for (auto& e : ev) o << "- **" << e.year << "**: " << e.title << (e.text.empty() ? "" : ". ") << e.text << "\n";
                    o << "\n";
                }
                if (!app.doc.articles.empty()) {
                    o << "## Lore\n\n";
                    for (auto& a : app.doc.articles) o << "### " << a.title << "\n\n*" << loreKindName(a.kind) << "*\n\n" << a.body << "\n\n";
                }
                setStatus("Saved " + base + "_chronicle.md");
            } else if (kind == 6) {
                // render the tiles under the view two zoom levels sharper, stitched into one image
                MapView hv = view;
                hv.pxPerDeg *= 3;
                int W2 = std::min(9000, sw * 3), H2 = std::min(6000, sh * 3);
                std::vector<unsigned char> px((size_t)W2 * H2 * 4, 0);
                int L2 = TileMap::levelFor(hv.pxPerDeg);
                double T = TileMap::tileDeg(L2);
                int cols = 4 << L2, rows = 2 << L2;
                double lonMin = hv.centerLon - W2 / 2.0 / hv.pxPerDeg, latMax = hv.centerLat + H2 / 2.0 / hv.pxPerDeg;
                double lonMax = hv.centerLon + W2 / 2.0 / hv.pxPerDeg, latMin = hv.centerLat - H2 / 2.0 / hv.pxPerDeg;
                int tyA = std::max(0, (int)std::floor((90 - latMax) / T)), tyB = std::min(rows - 1, (int)std::floor((90 - latMin) / T));
                int txA = (int)std::floor((lonMin + 180) / T), txB = (int)std::floor((lonMax + 180) / T);
                std::vector<std::pair<int, int>> todo;   // (txu, ty)
                for (int ty = tyA; ty <= tyB; ty++)
                    for (int txu = txA; txu <= txB; txu++) todo.push_back({txu, ty});
                std::atomic<int> next{0};
                auto work = [&]() {
                    std::vector<unsigned char> tile;
                    for (;;) {
                        int k = next++;
                        if (k >= (int)todo.size()) break;
                        int txu = todo[k].first, ty = todo[k].second;
                        int tx = ((txu % cols) + cols) % cols;
                        map.renderTile(L2, tx, ty, tile);
                        double x0 = W2 / 2.0 + (-180 + txu * T - hv.centerLon) * hv.pxPerDeg, y0 = H2 / 2.0 - (90 - ty * T - hv.centerLat) * hv.pxPerDeg;
                        double scale = T * hv.pxPerDeg / TileMap::TILE;
                        int dw = (int)std::ceil(TileMap::TILE * scale);
                        for (int yy = 0; yy < dw; yy++) {
                            int Y = (int)y0 + yy; if (Y < 0 || Y >= H2) continue;
                            int sy = std::min(TileMap::TILE - 1, (int)(yy / scale));
                            for (int xx = 0; xx < dw; xx++) {
                                int X = (int)x0 + xx; if (X < 0 || X >= W2) continue;
                                int sx = std::min(TileMap::TILE - 1, (int)(xx / scale));
                                std::memcpy(&px[((size_t)Y * W2 + X) * 4], &tile[((size_t)sy * TileMap::TILE + sx) * 4], 4);
                            }
                        }
                    }
                };
                std::vector<std::thread> th;
                unsigned nt = std::max(2u, std::thread::hardware_concurrency());
                for (unsigned t = 0; t < nt; t++) th.emplace_back(work);
                for (auto& t : th) t.join();
                Image img{px.data(), W2, H2, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                std::string f = base + TextFormat("_view_hires_%d.png", (int)GetTime());
                ExportImage(img, f.c_str());
                setStatus("Saved " + f);
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

    cityEd.close();
    if (haveTrace) UnloadTexture(traceTex);
    if (haveGlobe) UnloadTexture(globeTex);
    map.shutdown();
    CloseWindow();
    return 0;
}
