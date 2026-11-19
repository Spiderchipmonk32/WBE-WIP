// Worldbuilder: 2D world map of a round planet, with generation, sculpting and map styles.
#include "raylib.h"
#include "raygui.h"
#include "planet.h"
#include "tilemap.h"
#include <cmath>
#include <algorithm>

namespace {

const char* formatKm(double km) {
    if (km < 1.0) return TextFormat("%.0f m", km * 1000);
    if (km < 100.0) return TextFormat("%.1f km", km);
    return TextFormat("%.0f km", km);
}

double wrapLon(double lon) { return lon - 360.0 * std::floor((lon + 180.0) / 360.0); }

struct Theme { Color bg, grid, text, panelNote; };
Theme themeFor(MapStyle s) {
    if (s == MapStyle::Fantasy) return {{214, 200, 164, 255}, {110, 80, 50, 90}, {70, 50, 30, 255}, {90, 70, 50, 255}};
    return {{12, 16, 24, 255}, {255, 255, 255, 50}, {235, 238, 245, 255}, {150, 160, 175, 255}};
}

// Latitude/longitude grid with labels, step chosen to suit the zoom
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
        double w = wrapLon(lon);
        if (x > left) DrawText(TextFormat(fmt, std::fabs(w)), x + 3, std::max(yTop, 0) + 50, 14, c);
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

} // namespace

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1600, 900, "Worldbuilder");
    SetTargetFPS(144);
    GuiSetStyle(DEFAULT, TEXT_SIZE, 20);

    PlanetSettings ui;
    Planet planet;
    planet.generate(ui);
    TileMap map(&planet);
    PlanetStats stats = planet.computeStats();

    MapView view;
    view.pxPerDeg = GetScreenHeight() / 180.0 * 1.1;
    bool seedEdit = false, painting = false, panning = false, showGrid = true;
    int tool = 0;                 // 0 pan, 1 raise, 2 lower, 3 smooth, 4 flatten
    int styleUi = 0;
    float brushT = 0.80f;         // log slider: 50 m .. 3,000 km
    float strengthT = 0.851f;     // log slider: 0.1 .. 5,000 m/s
    float radiusUi = ui.radiusKm, oceanUi = ui.oceanPercent, mountUi = ui.mountains, contUi = ui.continentSize;

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        const int sw = GetScreenWidth(), sh = GetScreenHeight();
        const double R = planet.radiusKm();
        const double brushKm = 0.05 * std::pow(60000.0, (double)brushT);
        const double strength = 0.1 * std::pow(50000.0, (double)strengthT);
        const Rectangle panel = {10, 10, 320, (float)sh - 20};
        const Vector2 mouse = GetMousePosition();
        const bool overPanel = CheckCollisionPointRec(mouse, panel);
        const double minZoom = sh / 180.0, maxZoom = TileMap::TILE / 90.0 * (1 << TileMap::MAX_LEVEL) * 2;

        // ---- Zoom (keeps the point under the cursor fixed) ----
        float wheel = overPanel ? 0 : GetMouseWheelMove();
        if (wheel != 0) {
            double lon0 = view.centerLon + (mouse.x - sw / 2.0) / view.pxPerDeg;
            double lat0 = view.centerLat - (mouse.y - sh / 2.0) / view.pxPerDeg;
            view.pxPerDeg = clampd(view.pxPerDeg * std::pow(1.2, wheel), minZoom, maxZoom);
            view.centerLon = lon0 - (mouse.x - sw / 2.0) / view.pxPerDeg;
            view.centerLat = lat0 + (mouse.y - sh / 2.0) / view.pxPerDeg;
        }
        // ---- Pan ----
        if (!overPanel && (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) ||
                           (tool == 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))))
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
            if (IsKeyPressed(KEY_ONE)) tool = 0;
            if (IsKeyPressed(KEY_TWO)) tool = 1;
            if (IsKeyPressed(KEY_THREE)) tool = 2;
            if (IsKeyPressed(KEY_FOUR)) tool = 3;
            if (IsKeyPressed(KEY_FIVE)) tool = 4;
            if (IsKeyPressed(KEY_LEFT_BRACKET)) brushT = std::max(0.f, brushT - 0.03f);
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) brushT = std::min(1.f, brushT + 0.03f);
        }
        view.centerLon = wrapLon(view.centerLon);
        double halfH = sh / 2.0 / view.pxPerDeg;
        view.centerLat = halfH >= 90 ? 0 : clampd(view.centerLat, -90 + halfH, 90 - halfH);

        // ---- What's under the mouse ----
        double mLon = wrapLon(view.centerLon + (mouse.x - sw / 2.0) / view.pxPerDeg);
        double mLat = view.centerLat - (mouse.y - sh / 2.0) / view.pxPerDeg;
        bool hit = !overPanel && std::fabs(mLat) <= 90;
        Vec3d hitDir = TileMap::dirFromLatLon(clampd(mLat, -90, 90), mLon);

        // ---- Sculpting ----
        if (tool > 0 && hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            painting = true;
            planet.beginStrokeGroup((BrushType)tool, hitDir, brushKm);
        }
        if (painting && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && hit) {
            double amount = (tool == 1 || tool == 2) ? strength * dt : std::min(1.0, dt * 3 * strength / 1000);
            planet.addDab((BrushType)tool, hitDir, brushKm, amount);
            map.invalidate(hitDir, brushKm * 1.1);
        }
        if (painting && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            painting = false;
            stats = planet.computeStats();
        }
        auto doUndo = [&]() {
            for (auto& a : planet.undo()) map.invalidate(a.first, a.second * 1.1);
            stats = planet.computeStats();
        };
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_Z)) doUndo();

        map.update(view, sw, sh);

        // ---- Draw ----
        Theme th = themeFor(map.style());
        BeginDrawing();
        ClearBackground(th.bg);
        map.draw(view, sw, sh);
        if (showGrid) drawGrid(view, sw, sh, (int)(panel.x + panel.width), th.grid);
        if (hit && tool > 0) {   // brush outline (an ellipse, because the map stretches east-west away from the equator)
            double kmPerDeg = 111.195 * R / 6371.0;
            float ry = (float)(brushKm / kmPerDeg * view.pxPerDeg);
            float rx = (float)(ry / std::max(0.02, std::cos(mLat * kPi / 180)));
            DrawEllipseLines((int)mouse.x, (int)mouse.y, rx, ry, map.style() == MapStyle::Fantasy ? Color{60, 40, 25, 255} : RAYWHITE);
        }

        // ---- UI panel ----
        GuiPanel(panel, NULL);
        float x = 24, w = 292, y = 22;
        auto label = [&](const char* t) { GuiLabel(Rectangle{x, y, w, 22}, t); y += 24; };
        auto heading = [&](const char* t) { y += 6; GuiLabel(Rectangle{x, y, w, 22}, t); y += 28; };
        auto slider = [&](float* val, float mn, float mx) { GuiSliderBar(Rectangle{x, y, w, 20}, NULL, NULL, val, mn, mx); y += 28; };

        heading("PLANET");
        GuiLabel(Rectangle{x, y, 60, 28}, "Seed");
        if (GuiValueBox(Rectangle{x + 60, y, 130, 28}, NULL, &ui.seed, 0, 999999, seedEdit)) seedEdit = !seedEdit;
        bool randomClicked = GuiButton(Rectangle{x + 198, y, 94, 28}, "Random");
        y += 36;
        label(TextFormat("Radius: %.0f km", radiusUi)); slider(&radiusUi, 1000, 20000);
        label(TextFormat("Ocean cover: %.0f%%", oceanUi)); slider(&oceanUi, 0, 95);
        label(TextFormat("Mountains: %.1f", mountUi)); slider(&mountUi, 0, 2.5f);
        label(TextFormat("Continent size: %.1f", contUi)); slider(&contUi, 0.4f, 2.5f);
        bool genClicked = GuiButton(Rectangle{x, y, w, 34}, "Generate planet");
        y += 42;

        heading("MAP STYLE");
        GuiToggleGroup(Rectangle{x, y, 95, 28}, "Realistic;Fantasy;Elevation", &styleUi);
        y += 36;
        GuiCheckBox(Rectangle{x, y, 22, 22}, "Latitude/longitude grid", &showGrid); y += 30;

        heading("SCULPT");
        GuiToggleGroup(Rectangle{x, y, 95, 28}, "Pan;Raise;Lower\nSmooth;Flatten", &tool);
        y += 64;
        label(TextFormat("Brush radius: %s", formatKm(brushKm))); slider(&brushT, 0, 1);
        label(TextFormat("Strength: %.1f m/s", strength)); slider(&strengthT, 0, 1);
        if (GuiButton(Rectangle{x, y, w, 30}, "Undo (Ctrl+Z)")) doUndo();
        y += 38;

        heading("STATS");
        label(TextFormat("Land: %.1f%%", stats.landPercent));
        label(TextFormat("Highest: %.0f m", stats.highestM));
        label(TextFormat("Deepest: %.0f m", stats.deepestM));
        label(TextFormat("Area: %.1fM km2", stats.areaMkm2));

        if ((MapStyle)styleUi != map.style()) map.setStyle((MapStyle)styleUi);
        if (randomClicked) { ui.seed = GetRandomValue(0, 999999); genClicked = true; }
        if (genClicked) {
            ui.radiusKm = std::round(radiusUi); ui.oceanPercent = oceanUi; ui.mountains = mountUi; ui.continentSize = contUi;
            map.waitIdle();          // background threads must stop reading the old planet first
            planet.generate(ui);
            map.reset();
            stats = planet.computeStats();
        }

        // ---- Readouts ----
        const int fs = 20;
        double kmPerPx = 111.195 * R / 6371.0 * std::cos(view.centerLat * kPi / 180) / view.pxPerDeg;
        const char* info = TextFormat("1 px = %s   Tiles %d   Drawing %d   %d FPS", formatKm(kmPerPx), map.cached(), map.pending(), GetFPS());
        int iw = MeasureText(info, fs);
        DrawRectangle(sw - iw - 30, 10, iw + 20, 34, Fade(BLACK, 0.55f));
        DrawText(info, sw - iw - 20, 17, fs, RAYWHITE);
        if (hit) {
            double spacing = std::max(0.0005, kmPerPx);
            double e = planet.elevation(hitDir, Planet::detailOctavesFor(spacing, R));
            const char* t = TextFormat("%.4f %c, %.4f %c   %s %.0f m", std::fabs(mLat), mLat >= 0 ? 'N' : 'S', std::fabs(mLon),
                                       mLon >= 0 ? 'E' : 'W', e > 0 ? "Elevation" : "Depth", std::fabs(e));
            int tw = MeasureText(t, fs);
            DrawRectangle(sw - tw - 30, sh - 44, tw + 20, 34, Fade(BLACK, 0.55f));
            DrawText(t, sw - tw - 20, sh - 37, fs, RAYWHITE);
        }
        drawScaleBar(view, R, (int)(panel.x + panel.width) + 24, sh - 60, th.text);
        DrawText("Scroll: zoom   Drag (Pan tool), right or middle-drag: move   WASD: move   1-5: tools   [ ]: brush size",
                 (int)(panel.x + panel.width) + 24, sh - 28, 16, Fade(th.text, 0.8f));
        EndDrawing();
    }

    map.shutdown();
    CloseWindow();
    return 0;
}
