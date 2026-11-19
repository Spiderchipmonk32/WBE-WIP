#pragma once
#include "raylib.h"
#include "document.h"
#include <cmath>
#include <string>

// Procedural flag for a nation: layout and colours come from its name and colour.
inline void drawFlag(Rectangle r, const Nation& n) {
    uint32_t h = 2166136261u;
    for (char c : n.name) { h ^= (unsigned char)c; h *= 16777619u; }
    auto pick = [&](int k) { h = h * 1664525u + 1013904223u; return (int)((h >> 8) % (uint32_t)k); };
    Color main{n.r, n.g, n.b, 255};
    const Color extras[] = {{245, 245, 240, 255}, {25, 25, 30, 255}, {230, 190, 40, 255}, {180, 30, 40, 255}, {30, 60, 140, 255}, {30, 110, 60, 255}};
    Color b = extras[pick(6)], c = extras[pick(6)];
    if (b.r == c.r && b.g == c.g) c = extras[(pick(5) + 1) % 6];
    float x = r.x, y = r.y, w = r.width, hh = r.height;
    switch (pick(8)) {
        case 0: DrawRectangleRec({x, y, w / 3, hh}, main); DrawRectangleRec({x + w / 3, y, w / 3, hh}, b); DrawRectangleRec({x + 2 * w / 3, y, w / 3 + 1, hh}, c); break;
        case 1: DrawRectangleRec({x, y, w, hh / 3}, main); DrawRectangleRec({x, y + hh / 3, w, hh / 3}, b); DrawRectangleRec({x, y + 2 * hh / 3, w, hh / 3 + 1}, c); break;
        case 2: DrawRectangleRec({x, y, w, hh / 2}, main); DrawRectangleRec({x, y + hh / 2, w, hh / 2}, b); break;
        case 3: DrawRectangleRec(r, main); DrawRectangleRec({x + w * 0.3f, y, w * 0.14f, hh}, b); DrawRectangleRec({x, y + hh * 0.42f, w, hh * 0.16f}, b); break;
        case 4: DrawRectangleRec(r, main);
            DrawLineEx({x, y}, {x + w, y + hh}, hh * 0.18f, b); DrawLineEx({x + w, y}, {x, y + hh}, hh * 0.18f, b); break;
        case 5: { int stripes = 5; for (int i = 0; i < stripes; i++) DrawRectangleRec({x, y + hh * i / stripes, w, hh / stripes + 1}, i % 2 ? b : main);
                  DrawRectangleRec({x, y, w * 0.42f, hh * 0.6f}, c); break; }
        case 6: DrawRectangleRec(r, b); DrawCircleV({x + w / 2, y + hh / 2}, hh * 0.3f, main); break;
        default: DrawRectangleRec(r, main); DrawTriangle({x, y}, {x, y + hh}, {x + w * 0.45f, y + hh / 2}, b); break;
    }
    if (pick(3) == 0) {   // a star emblem
        Vector2 ctr{x + w * (pick(2) ? 0.5f : 0.22f), y + hh * 0.5f};
        float rr = hh * 0.17f;
        for (int k = 0; k < 5; k++) {
            float a0 = -1.5708f + k * 1.2566f, a1 = a0 + 2.5133f;
            DrawTriangle(ctr, {ctr.x + std::cos(a1) * rr, ctr.y + std::sin(a1) * rr}, {ctr.x + std::cos(a0) * rr, ctr.y + std::sin(a0) * rr}, c);
            DrawTriangle(ctr, {ctr.x + std::cos(a0) * rr, ctr.y + std::sin(a0) * rr}, {ctr.x + std::cos(a1) * rr, ctr.y + std::sin(a1) * rr}, c);
        }
    }
    DrawRectangleLinesEx(r, 1, Color{20, 20, 20, 255});
}

// Compass rose for the map corner
inline void drawCompass(Vector2 c, float r, bool fantasy) {
    Color ink = fantasy ? Color{70, 45, 25, 255} : Color{240, 240, 240, 230};
    Color fill = fantasy ? Color{235, 220, 185, 230} : Color{20, 30, 45, 200};
    DrawCircleV(c, r * 0.95f, fill);
    DrawCircleLines((int)c.x, (int)c.y, r * 0.95f, ink);
    DrawCircleLines((int)c.x, (int)c.y, r * 0.55f, ink);
    for (int k = 0; k < 8; k++) {
        float a = k * 0.7854f - 1.5708f, len = k % 2 ? r * 0.55f : r * 0.9f, wdt = k % 2 ? r * 0.1f : r * 0.16f;
        Vector2 tip{c.x + std::cos(a) * len, c.y + std::sin(a) * len};
        Vector2 l{c.x + std::cos(a - 1.5708f) * wdt, c.y + std::sin(a - 1.5708f) * wdt};
        Vector2 rr{c.x + std::cos(a + 1.5708f) * wdt, c.y + std::sin(a + 1.5708f) * wdt};
        DrawTriangle(tip, rr, c, ink);
        DrawTriangle(tip, c, l, k % 2 ? ink : fill);
        DrawTriangleLines(tip, rr, l, ink);
    }
    DrawText("N", (int)c.x - MeasureText("N", (int)(r * 0.4f)) / 2, (int)(c.y - r * 1.38f), (int)(r * 0.4f), ink);
}
