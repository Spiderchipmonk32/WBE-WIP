// Worldbuilder: step 1b, a zoomable globe from orbit down to street level.
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "raygui.h"
#include "planet.h"
#include "terrain.h"
#include <cmath>
#include <algorithm>

static const char* kVertexShader = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 fragNormal;
out vec4 fragColor;
out vec3 fragPos;
void main() {
    fragNormal = vertexNormal;
    fragColor = vertexColor;
    fragPos = (matModel * vec4(vertexPosition, 1.0)).xyz;   // camera-relative position
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char* kFragmentShader = R"(#version 330
in vec3 fragNormal;
in vec4 fragColor;
in vec3 fragPos;
uniform vec3 sunDir;
uniform vec3 fogColor;
uniform float fogDensity;
out vec4 finalColor;
void main() {
    vec3 n = normalize(fragNormal);
    float diffuse = max(dot(n, sunDir), 0.0);
    vec3 c = fragColor.rgb * (0.30 + 0.85 * diffuse);
    float fog = 1.0 - exp(-length(fragPos) * fogDensity);
    c = mix(c, fogColor, clamp(fog, 0.0, 0.65));
    finalColor = vec4(c, 1.0);
}
)";

// Camera orbiting a point on the surface (like a map app): pan moves the point,
// right-drag turns and tilts, scroll changes distance.
struct GlobeCamera {
    Vec3d targetDir = Vec3d{0.2, 0.35, 1.0}.normalized();
    double distKm = 16000;
    double yaw = 0;                 // heading, radians from north
    double pitch = 89.0 * kPi / 180; // 90 = looking straight down
};

static void localFrame(const Vec3d& up, Vec3d& east, Vec3d& north) {
    Vec3d ref = std::fabs(up.y) > 0.999 ? Vec3d{0, 0, 1} : Vec3d{0, 1, 0};
    east = ref.cross(up).normalized();
    north = up.cross(east);
}

static Vector3 toV3(const Vec3d& v) { return {(float)v.x, (float)v.y, (float)v.z}; }

static const char* formatKm(double km) {
    if (km < 1.0) return TextFormat("%.0f m", km * 1000);
    if (km < 100.0) return TextFormat("%.1f km", km);
    return TextFormat("%.0f km", km);
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1600, 900, "Worldbuilder");
    SetTargetFPS(144);
    GuiSetStyle(DEFAULT, TEXT_SIZE, 20);

    Shader shader = LoadShaderFromMemory(kVertexShader, kFragmentShader);
    shader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(shader, "matModel");
    int locSun = GetShaderLocation(shader, "sunDir");
    int locFogColor = GetShaderLocation(shader, "fogColor");
    int locFogDensity = GetShaderLocation(shader, "fogDensity");
    Material material = LoadMaterialDefault();
    material.shader = shader;

    PlanetSettings ui;
    Planet planet;
    Terrain terrain;
    planet.generate(ui);
    terrain.reset(&planet);
    PlanetStats stats = planet.computeStats();

    GlobeCamera gc;
    bool seedEdit = false;
    int tool = 0;                       // 0 orbit, 1 raise, 2 lower, 3 smooth, 4 flatten
    float brushT = 0.849f;              // log slider: 5 m .. 3,000 km (default 400 km)
    float strengthT = 0.851f;           // log slider: 0.1 .. 5,000 m/s (default 1,000)
    float exag = terrain.exaggeration;
    bool drain = false, sunFollows = true, painting = false, panning = false, turning = false;
    float radiusUi = ui.radiusKm, oceanUi = ui.oceanPercent, mountUi = ui.mountains, contUi = ui.continentSize;

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        const int sw = GetScreenWidth(), sh = GetScreenHeight();
        const double R = planet.radiusKm();
        const double brushKm = 0.005 * std::pow(600000.0, (double)brushT);
        const double strength = 0.1 * std::pow(50000.0, (double)strengthT);
        const Rectangle panel = {10, 10, 320, (float)sh - 20};
        const Vector2 mouse = GetMousePosition();
        const bool overPanel = CheckCollisionPointRec(mouse, panel);

        // ---- Camera input ----
        Vec3d up = gc.targetDir, east, north;
        localFrame(up, east, north);
        Vec3d fwd = north * std::cos(gc.yaw) + east * std::sin(gc.yaw);
        Vec3d right = fwd.cross(up);
        const double fov = 45.0 * kPi / 180;
        const double angPerPixel = (2 * gc.distKm * std::tan(fov / 2) / sh) / R;

        if (!overPanel) {
            float wheel = GetMouseWheelMove();
            if (wheel != 0) gc.distKm = clampd(gc.distKm * std::pow(0.85, wheel), 0.003, 4 * R);
        }
        // A drag only moves the globe if it started over the globe (not on the panel)
        if (!overPanel && (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE) || (tool == 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))))
            panning = true;
        if (!IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) panning = false;
        if (!overPanel && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) turning = true;
        if (!IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) turning = false;
        Vector2 md = GetMouseDelta();
        if (panning && !painting) {
            gc.targetDir = (gc.targetDir + (right * (-md.x) + fwd * md.y) * angPerPixel).normalized();
        }
        if (turning) {
            gc.yaw += md.x * 0.005;
            gc.pitch = clampd(gc.pitch + md.y * 0.005, 5.0 * kPi / 180, 89.5 * kPi / 180);
        }
        if (!seedEdit) {
            double k = angPerPixel * 600 * dt;
            if (IsKeyDown(KEY_W)) gc.targetDir = (gc.targetDir + fwd * k).normalized();
            if (IsKeyDown(KEY_S)) gc.targetDir = (gc.targetDir - fwd * k).normalized();
            if (IsKeyDown(KEY_D)) gc.targetDir = (gc.targetDir + right * k).normalized();
            if (IsKeyDown(KEY_A)) gc.targetDir = (gc.targetDir - right * k).normalized();
            if (IsKeyDown(KEY_Q)) gc.yaw -= 1.5 * dt;
            if (IsKeyDown(KEY_E)) gc.yaw += 1.5 * dt;
            if (IsKeyPressed(KEY_ONE)) tool = 0;
            if (IsKeyPressed(KEY_TWO)) tool = 1;
            if (IsKeyPressed(KEY_THREE)) tool = 2;
            if (IsKeyPressed(KEY_FOUR)) tool = 3;
            if (IsKeyPressed(KEY_FIVE)) tool = 4;
            if (IsKeyPressed(KEY_LEFT_BRACKET)) brushT = std::max(0.f, brushT - 0.03f);
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) brushT = std::min(1.f, brushT + 0.03f);
        }

        // ---- Camera position (double precision, km) ----
        up = gc.targetDir;
        localFrame(up, east, north);
        fwd = north * std::cos(gc.yaw) + east * std::sin(gc.yaw);
        const double lodSpacing = std::max(0.001, gc.distKm / 200);
        Vec3d targetPos = up * (R + terrain.displayHeightKm(up, lodSpacing));
        Vec3d camPos = targetPos + (fwd * (-std::cos(gc.pitch)) + up * std::sin(gc.pitch)) * gc.distKm;
        {   // never go below the ground
            Vec3d cdir = camPos.normalized();
            double minR = R + terrain.displayHeightKm(cdir, lodSpacing) + 0.002;
            if (camPos.length() < minR) camPos = cdir * minR;
        }
        Vec3d view = (targetPos - camPos).normalized();
        Vec3d camRight = view.cross(up).normalized();
        Vec3d camUp = camRight.cross(view);
        double groundUnderCam = terrain.displayHeightKm(camPos.normalized(), lodSpacing);
        double altitude = camPos.length() - R - groundUnderCam;

        Camera3D cam = {};
        cam.position = {0, 0, 0};          // the world is drawn relative to the camera
        cam.target = toV3(view);
        cam.up = toV3(camUp);
        cam.fovy = 45;
        cam.projection = CAMERA_PERSPECTIVE;

        // ---- Mouse picking on the planet ----
        bool hit = false;
        Vec3d hitDir;
        if (!overPanel) {
            Ray ray = GetScreenToWorldRay(mouse, cam);
            Vec3d O = camPos, Dd{ray.direction.x, ray.direction.y, ray.direction.z};
            double r = R;
            for (int it = 0; it < 5; it++) {
                double b = O.dot(Dd), c = O.dot(O) - r * r, disc = b * b - c;
                if (disc < 0) break;
                double t = c > 0 ? -b - std::sqrt(disc) : -b + std::sqrt(disc);
                if (t < 0) break;
                hitDir = (O + Dd * t).normalized();
                hit = true;
                r = R + terrain.displayHeightKm(hitDir, std::max(0.001, brushKm / 40));
            }
        }

        // ---- Sculpting ----
        if (tool > 0 && !overPanel && hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            painting = true;
            planet.beginStrokeGroup((BrushType)tool, hitDir, brushKm);
        }
        if (painting && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && hit) {
            double amount = (tool == 1 || tool == 2) ? strength * dt : std::min(1.0, dt * 3 * strength / 1000);
            planet.addDab((BrushType)tool, hitDir, brushKm, amount);
            terrain.markDirty(hitDir, brushKm * 1.2);
        }
        if (painting && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            painting = false;
            stats = planet.computeStats();
        }
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_Z)) {
            for (auto& a : planet.undo()) terrain.markDirty(a.first, a.second * 1.2);
            stats = planet.computeStats();
        }

        terrain.update(camPos);

        // ---- Render ----
        double skyT = 1.0 - smoothstepd(30, 400, altitude);
        Color space = {8, 11, 18, 255}, sky = {135, 170, 215, 255};
        Color bg = {(unsigned char)(space.r + (sky.r - space.r) * skyT), (unsigned char)(space.g + (sky.g - space.g) * skyT),
                    (unsigned char)(space.b + (sky.b - space.b) * skyT), 255};
        float fogColor[3] = {bg.r / 255.f, bg.g / 255.f, bg.b / 255.f};
        float fogDensity = altitude < 1500 ? (float)((1.0 / (600 + altitude * 3)) * (1 - altitude / 1500)) : 0.f;
        Vec3d sun = sunFollows ? (up * 0.75 + east * 0.45 + north * 0.35).normalized() : Vec3d{0.6, 0.35, 0.7}.normalized();
        float sunV[3] = {(float)sun.x, (float)sun.y, (float)sun.z};
        SetShaderValue(shader, locSun, sunV, SHADER_UNIFORM_VEC3);
        SetShaderValue(shader, locFogColor, fogColor, SHADER_UNIFORM_VEC3);
        SetShaderValue(shader, locFogDensity, &fogDensity, SHADER_UNIFORM_FLOAT);

        double D = camPos.length();
        double horizonDist = std::sqrt(std::max(0.0, D * D - R * R));
        double hMax = terrain.maxDisplayKm();
        double farPlane = horizonDist + std::sqrt((R + hMax) * (R + hMax) - R * R) + 50;
        double nearPlane = clampd(altitude * 0.25, 0.0005, 1000);
        rlSetClipPlanes(nearPlane, std::max(farPlane, nearPlane * 10));

        BeginDrawing();
        ClearBackground(bg);
        BeginMode3D(cam);
        rlDisableBackfaceCulling();
        terrain.draw(camPos, material);
        rlEnableBackfaceCulling();
        if (hit && tool > 0) {   // brush outline
            rlDrawRenderBatchActive();
            rlDisableDepthTest();
            double a = brushKm / R;
            Vec3d e1 = hitDir.cross(std::fabs(hitDir.y) < 0.9 ? Vec3d{0, 1, 0} : Vec3d{1, 0, 0}).normalized();
            Vec3d e2 = hitDir.cross(e1);
            const int N = 64;
            Vec3d prev;
            for (int k = 0; k <= N; k++) {
                double th = 2 * kPi * k / N;
                Vec3d d = (hitDir * std::cos(a) + (e1 * std::cos(th) + e2 * std::sin(th)) * std::sin(a)).normalized();
                Vec3d p = d * (R + terrain.displayHeightKm(d, std::max(0.001, brushKm / 40)) + brushKm * 0.002) - camPos;
                if (k > 0) DrawLine3D(toV3(prev), toV3(p), RAYWHITE);
                prev = p;
            }
            rlDrawRenderBatchActive();
            rlEnableDepthTest();
        }
        EndMode3D();

        // ---- UI panel ----
        GuiPanel(panel, NULL);
        float x = 24, w = 292, y = 22;
        auto label = [&](const char* t) { GuiLabel(Rectangle{x, y, w, 22}, t); y += 24; };
        auto heading = [&](const char* t) { y += 6; GuiLabel(Rectangle{x, y, w, 22}, t); y += 28; };
        auto slider = [&](float* v, float mn, float mx) { GuiSliderBar(Rectangle{x, y, w, 20}, NULL, NULL, v, mn, mx); y += 28; };

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

        heading("SCULPT");
        GuiToggleGroup(Rectangle{x, y, 95, 28}, "Orbit;Raise;Lower\nSmooth;Flatten", &tool);
        y += 64;
        label(TextFormat("Brush radius: %s", formatKm(brushKm))); slider(&brushT, 0, 1);
        label(TextFormat("Strength: %.1f m/s", strength)); slider(&strengthT, 0, 1);
        if (GuiButton(Rectangle{x, y, w, 30}, "Undo (Ctrl+Z)")) {
            for (auto& a : planet.undo()) terrain.markDirty(a.first, a.second * 1.2);
            stats = planet.computeStats();
        }
        y += 38;

        heading("VIEW");
        label(TextFormat("Height exaggeration: %.0fx", exag)); slider(&exag, 1, 50);
        GuiCheckBox(Rectangle{x, y, 22, 22}, "Drain oceans", &drain); y += 30;
        GuiCheckBox(Rectangle{x, y, 22, 22}, "Sun follows camera", &sunFollows); y += 34;

        heading("STATS");
        label(TextFormat("Land: %.1f%%", stats.landPercent));
        label(TextFormat("Highest: %.0f m", stats.highestM));
        label(TextFormat("Deepest: %.0f m", stats.deepestM));
        label(TextFormat("Area: %.1fM km2", stats.areaMkm2));

        if (randomClicked) { ui.seed = GetRandomValue(0, 999999); genClicked = true; }
        if (genClicked) {
            ui.radiusKm = std::round(radiusUi); ui.oceanPercent = oceanUi; ui.mountains = mountUi; ui.continentSize = contUi;
            planet.generate(ui);
            terrain.reset(&planet);
            stats = planet.computeStats();
            gc.distKm = std::min(gc.distKm, 4.0 * planet.radiusKm());
        }
        if (std::fabs(exag - terrain.exaggeration) > 0.01f) { terrain.exaggeration = std::round(exag); terrain.markAllDirty(); }
        if (drain != terrain.drainOceans) { terrain.drainOceans = drain; terrain.markAllDirty(); }

        // ---- Readouts ----
        int fontSize = 20;
        const char* info = TextFormat("Altitude %s   Tiles %d   Building %d   %d FPS",
                                      formatKm(std::max(0.0, altitude)), terrain.drawnTiles(), terrain.pendingBuilds(), GetFPS());
        int iw = MeasureText(info, fontSize);
        DrawRectangle(sw - iw - 30, 10, iw + 20, 34, Fade(BLACK, 0.55f));
        DrawText(info, sw - iw - 20, 17, fontSize, RAYWHITE);
        if (hit) {
            double lat = std::asin(hitDir.y) * 180 / kPi, lon = std::atan2(hitDir.x, hitDir.z) * 180 / kPi;
            double e = planet.elevation(hitDir, Planet::detailOctavesFor(std::max(0.001, altitude / 300), R));
            const char* t = TextFormat("%.4f %c, %.4f %c   %s %.0f m", std::fabs(lat), lat >= 0 ? 'N' : 'S', std::fabs(lon),
                                       lon >= 0 ? 'E' : 'W', e > 0 ? "Elevation" : "Depth", std::fabs(e));
            int tw = MeasureText(t, fontSize);
            DrawRectangle(sw - tw - 30, sh - 44, tw + 20, 34, Fade(BLACK, 0.55f));
            DrawText(t, sw - tw - 20, sh - 37, fontSize, RAYWHITE);
        }
        const char* help = "Scroll: zoom   Right-drag: turn/tilt   WASD or middle-drag: move   Q/E: turn   1-5: tools   [ ]: brush size";
        DrawText(help, (int)panel.width + 30, sh - 30, 18, Fade(RAYWHITE, 0.75f));
        EndDrawing();
    }

    terrain.clear();
    UnloadMaterial(material);   // also unloads the shader
    CloseWindow();
    return 0;
}
