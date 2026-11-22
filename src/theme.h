#pragma once
// Look and feel: a dark "map room" theme for raygui, the icon rail down the left edge,
// the panel header and the status bar along the bottom of the map.
#include "raylib.h"
#include "raygui.h"
#include "fonts.h"
#include <cmath>

namespace theme {
inline const Color kRail{14, 17, 24, 255};
inline const Color kPanel{22, 26, 35, 250};
inline const Color kCard{30, 35, 47, 255};
inline const Color kControl{38, 45, 60, 255};
inline const Color kBorder{58, 67, 88, 255};
inline const Color kText{222, 227, 235, 255};
inline const Color kMuted{142, 152, 170, 255};
inline const Color kAccent{232, 178, 84, 255};      // warm brass
inline const Color kAccentHi{246, 202, 118, 255};
inline const Color kTeal{86, 190, 190, 255};

inline void apply() {
    auto set = [](int ctl, int prop, Color c) { GuiSetStyle(ctl, prop, ColorToInt(c)); };
    set(DEFAULT, BORDER_COLOR_NORMAL, kBorder);
    set(DEFAULT, BASE_COLOR_NORMAL, kControl);
    set(DEFAULT, TEXT_COLOR_NORMAL, kText);
    set(DEFAULT, BORDER_COLOR_FOCUSED, kAccent);
    set(DEFAULT, BASE_COLOR_FOCUSED, Color{52, 60, 80, 255});
    set(DEFAULT, TEXT_COLOR_FOCUSED, kAccentHi);
    set(DEFAULT, BORDER_COLOR_PRESSED, kAccentHi);
    set(DEFAULT, BASE_COLOR_PRESSED, kAccent);
    set(DEFAULT, TEXT_COLOR_PRESSED, Color{25, 20, 12, 255});
    set(DEFAULT, BORDER_COLOR_DISABLED, Color{45, 50, 62, 255});
    set(DEFAULT, BASE_COLOR_DISABLED, Color{30, 34, 44, 255});
    set(DEFAULT, TEXT_COLOR_DISABLED, Color{90, 98, 112, 255});
    set(DEFAULT, BACKGROUND_COLOR, kPanel);
    set(DEFAULT, LINE_COLOR, kBorder);
    GuiSetStyle(DEFAULT, BORDER_WIDTH, 1);
    // labels are plain text on the panel
    set(LABEL, TEXT_COLOR_NORMAL, kText);
    // sliders: dark track, brass fill
    set(SLIDER, BASE_COLOR_NORMAL, Color{30, 35, 46, 255});
    set(SLIDER, BASE_COLOR_PRESSED, kAccent);
    set(SLIDER, BORDER_COLOR_NORMAL, Color{50, 58, 76, 255});
    GuiSetStyle(SLIDER, SLIDER_PADDING, 0);
    set(PROGRESSBAR, BASE_COLOR_PRESSED, kAccent);
    // text boxes look sunken
    set(TEXTBOX, BASE_COLOR_NORMAL, Color{17, 20, 27, 255});
    set(TEXTBOX, BASE_COLOR_PRESSED, Color{12, 14, 19, 255});
    set(TEXTBOX, TEXT_COLOR_PRESSED, kText);
    set(VALUEBOX, BASE_COLOR_NORMAL, Color{17, 20, 27, 255});
    set(VALUEBOX, BASE_COLOR_PRESSED, Color{12, 14, 19, 255});
    set(VALUEBOX, TEXT_COLOR_PRESSED, kText);
    set(CHECKBOX, BASE_COLOR_PRESSED, kAccent);
    set(CHECKBOX, BASE_COLOR_NORMAL, Color{17, 20, 27, 255});
    GuiSetStyle(CHECKBOX, CHECK_PADDING, 4);
    set(LISTVIEW, BASE_COLOR_NORMAL, Color{26, 30, 40, 255});
    set(DROPDOWNBOX, BASE_COLOR_NORMAL, kControl);
    GuiSetStyle(LISTVIEW, LIST_ITEMS_HEIGHT, 26);
    GuiSetStyle(DEFAULT, TEXT_LINE_SPACING, 20);
    GuiEnableTooltip();
}

// ---- Tab icons, drawn as simple vector shapes so they stay sharp ----
inline void icon(int tab, Vector2 c, float s, Color col) {
    float t = std::max(1.5f, s * 0.08f);
    switch (tab) {
    case 0:   // World: globe
        DrawRing(c, s * 0.42f - t, s * 0.42f, 0, 360, 40, col);
        DrawLineEx({c.x - s * 0.42f, c.y}, {c.x + s * 0.42f, c.y}, t * 0.8f, col);
        for (float k : {-0.5f, 0.5f}) {
            float r = s * 0.42f * std::sqrt(1 - k * k);
            DrawLineEx({c.x - r, c.y + k * s * 0.42f}, {c.x + r, c.y + k * s * 0.42f}, t * 0.7f, col);
        }
        DrawEllipseLines((int)c.x, (int)c.y, s * 0.18f, s * 0.42f, col);
        DrawEllipseLines((int)c.x, (int)c.y, s * 0.18f - 1, s * 0.42f, col);
        break;
    case 1: {  // Map: folded sheet
        Vector2 p[5] = {{c.x - s * 0.45f, c.y - s * 0.3f}, {c.x - s * 0.15f, c.y - s * 0.4f}, {c.x + s * 0.15f, c.y - s * 0.3f}, {c.x + s * 0.45f, c.y - s * 0.4f}, {}};
        Vector2 q[4] = {{c.x - s * 0.45f, c.y + s * 0.4f}, {c.x - s * 0.15f, c.y + s * 0.3f}, {c.x + s * 0.15f, c.y + s * 0.4f}, {c.x + s * 0.45f, c.y + s * 0.3f}};
        for (int i = 0; i < 3; i++) { DrawLineEx(p[i], p[i + 1], t, col); DrawLineEx(q[i], q[i + 1], t, col); }
        for (int i = 0; i < 4; i++) DrawLineEx(p[i], q[i], t, col);
        break;
    }
    case 2:   // Sculpt: mountain + brush stroke
        DrawTriangleLines({c.x - s * 0.45f, c.y + s * 0.35f}, {c.x + s * 0.3f, c.y + s * 0.35f}, {c.x - s * 0.08f, c.y - s * 0.35f}, col);
        DrawLineEx({c.x - s * 0.45f, c.y + s * 0.35f}, {c.x - s * 0.08f, c.y - s * 0.35f}, t, col);
        DrawLineEx({c.x - s * 0.08f, c.y - s * 0.35f}, {c.x + s * 0.3f, c.y + s * 0.35f}, t, col);
        DrawLineEx({c.x - s * 0.45f, c.y + s * 0.35f}, {c.x + s * 0.45f, c.y + s * 0.35f}, t, col);
        DrawLineEx({c.x + s * 0.12f, c.y - s * 0.05f}, {c.x + s * 0.45f, c.y - s * 0.42f}, t * 1.4f, col);
        break;
    case 3: {  // Resources: gem
        Vector2 a{c.x - s * 0.4f, c.y - s * 0.12f}, b{c.x - s * 0.2f, c.y - s * 0.38f}, d{c.x + s * 0.2f, c.y - s * 0.38f}, e{c.x + s * 0.4f, c.y - s * 0.12f}, f{c.x, c.y + s * 0.42f};
        DrawLineEx(a, b, t, col); DrawLineEx(b, d, t, col); DrawLineEx(d, e, t, col); DrawLineEx(e, f, t, col); DrawLineEx(f, a, t, col);
        DrawLineEx(a, e, t * 0.8f, col); DrawLineEx({c.x - s * 0.12f, c.y - s * 0.12f}, f, t * 0.7f, col); DrawLineEx({c.x + s * 0.12f, c.y - s * 0.12f}, f, t * 0.7f, col);
        break;
    }
    case 4:   // Climate: sun and cloud
        DrawRing({c.x - s * 0.12f, c.y - s * 0.12f}, s * 0.15f, s * 0.15f + t, 0, 360, 24, col);
        for (int i = 0; i < 8; i++) {
            float a = i * PI / 4;
            if (i == 1 || i == 0 || i == 7) continue;
            Vector2 o{c.x - s * 0.12f, c.y - s * 0.12f};
            DrawLineEx({o.x + std::cos(a) * s * 0.23f, o.y + std::sin(a) * s * 0.23f}, {o.x + std::cos(a) * s * 0.32f, o.y + std::sin(a) * s * 0.32f}, t, col);
        }
        DrawCircleV({c.x + s * 0.05f, c.y + s * 0.18f}, s * 0.17f, col);
        DrawCircleV({c.x + s * 0.25f, c.y + s * 0.1f}, s * 0.17f, col);
        DrawRectangleV({c.x - s * 0.1f, c.y + s * 0.18f}, {s * 0.52f, s * 0.17f}, col);
        DrawCircleV({c.x - s * 0.08f, c.y + s * 0.26f}, s * 0.09f, col);
        break;
    case 5:   // Nations: flag
        DrawLineEx({c.x - s * 0.32f, c.y - s * 0.42f}, {c.x - s * 0.32f, c.y + s * 0.42f}, t * 1.2f, col);
        DrawTriangle({c.x - s * 0.3f, c.y - s * 0.4f}, {c.x - s * 0.3f, c.y + s * 0.05f}, {c.x + s * 0.4f, c.y - s * 0.17f}, col);
        break;
    case 6:   // Cities: towers
        DrawRectangleLinesEx({c.x - s * 0.42f, c.y - s * 0.05f, s * 0.28f, s * 0.45f}, t, col);
        DrawRectangleLinesEx({c.x - s * 0.14f, c.y - s * 0.38f, s * 0.28f, s * 0.78f}, t, col);
        DrawRectangleLinesEx({c.x + s * 0.14f, c.y - s * 0.18f, s * 0.28f, s * 0.58f}, t, col);
        break;
    case 7:   // Trade: sailing ship
        DrawTriangle({c.x - s * 0.42f, c.y + s * 0.18f}, {c.x + s * 0.3f, c.y + s * 0.42f}, {c.x + s * 0.42f, c.y + s * 0.18f}, col);
        DrawTriangle({c.x - s * 0.42f, c.y + s * 0.18f}, {c.x - s * 0.3f, c.y + s * 0.42f}, {c.x + s * 0.3f, c.y + s * 0.42f}, col);
        DrawLineEx({c.x, c.y + s * 0.18f}, {c.x, c.y - s * 0.45f}, t, col);
        DrawTriangle({c.x + s * 0.04f, c.y - s * 0.42f}, {c.x + s * 0.04f, c.y + s * 0.1f}, {c.x + s * 0.38f, c.y + s * 0.1f}, col);
        DrawTriangle({c.x - s * 0.04f, c.y - s * 0.3f}, {c.x - s * 0.3f, c.y + s * 0.1f}, {c.x - s * 0.04f, c.y + s * 0.1f}, col);
        break;
    case 8:   // History: hourglass
        DrawLineEx({c.x - s * 0.3f, c.y - s * 0.42f}, {c.x + s * 0.3f, c.y - s * 0.42f}, t * 1.2f, col);
        DrawLineEx({c.x - s * 0.3f, c.y + s * 0.42f}, {c.x + s * 0.3f, c.y + s * 0.42f}, t * 1.2f, col);
        DrawLineEx({c.x - s * 0.24f, c.y - s * 0.42f}, {c.x + s * 0.24f, c.y + s * 0.42f}, t, col);
        DrawLineEx({c.x + s * 0.24f, c.y - s * 0.42f}, {c.x - s * 0.24f, c.y + s * 0.42f}, t, col);
        DrawTriangle({c.x - s * 0.17f, c.y + s * 0.36f}, {c.x + s * 0.17f, c.y + s * 0.36f}, {c.x, c.y + s * 0.12f}, col);
        break;
    case 9:   // Lore: open book
        DrawLineEx({c.x, c.y - s * 0.28f}, {c.x, c.y + s * 0.38f}, t, col);
        for (float sgn : {-1.f, 1.f}) {
            DrawLineEx({c.x, c.y - s * 0.28f}, {c.x + sgn * s * 0.44f, c.y - s * 0.36f}, t, col);
            DrawLineEx({c.x + sgn * s * 0.44f, c.y - s * 0.36f}, {c.x + sgn * s * 0.44f, c.y + s * 0.3f}, t, col);
            DrawLineEx({c.x + sgn * s * 0.44f, c.y + s * 0.3f}, {c.x, c.y + s * 0.38f}, t, col);
            for (int k = 0; k < 3; k++) DrawLineEx({c.x + sgn * s * 0.1f, c.y - s * 0.13f + k * s * 0.15f}, {c.x + sgn * s * 0.33f, c.y - s * 0.17f + k * s * 0.15f}, t * 0.6f, col);
        }
        break;
    case 10:  // File: floppy disk
        DrawRectangleLinesEx({c.x - s * 0.4f, c.y - s * 0.4f, s * 0.8f, s * 0.8f}, t, col);
        DrawRectangleV({c.x - s * 0.22f, c.y - s * 0.4f}, {s * 0.4f, s * 0.26f}, col);
        DrawRectangleLinesEx({c.x - s * 0.25f, c.y + s * 0.05f, s * 0.5f, s * 0.3f}, t, col);
        break;
    case 11:  // Peoples: two figures
        DrawCircleV({c.x - s * 0.17f, c.y - s * 0.2f}, s * 0.13f, col);
        DrawCircleV({c.x + s * 0.2f, c.y - s * 0.14f}, s * 0.11f, col);
        DrawCircleSector({c.x - s * 0.17f, c.y + s * 0.35f}, s * 0.27f, 180, 360, 20, col);
        DrawCircleSector({c.x + s * 0.2f, c.y + s * 0.35f}, s * 0.22f, 180, 360, 20, col);
        break;
    }
}

inline const char* tabName(int t) {
    static const char* n[] = {"World", "Map", "Sculpt", "Resources", "Climate", "Nations", "Cities", "Trade", "History", "Lore", "File", "Peoples"};
    return n[t];
}
inline const char* tabBlurb(int t) {
    static const char* n[] = {
        "Generate the planet: size, oceans, plates.",
        "Styles, overlays, layers and tools.",
        "Paint land, mountains, seas and coasts.",
        "Ores, gems, fuels and your own resources.",
        "Seasons, winds, rain and ocean currents.",
        "Borders, rulers, flags and diplomacy.",
        "Found cities and open them in the editor.",
        "Goods, routes, ports and caravans.",
        "Timeline events that reshape the map.",
        "Your world's wiki: articles and pins.",
        "Save, load and export.",
        "Cultures, languages and religions."};
    return n[t];
}

// Order of the tabs down the rail (grouped: shape the world, then people, then records)
inline const int kRailOrder[] = {0, 2, 1, 4, 3, -1, 11, 5, 6, 7, -1, 8, 9, 10};

// Draws the rail; returns the tab clicked (or -1)
inline int rail(Rectangle r, int active) {
    DrawRectangleRec(r, kRail);
    DrawLineEx({r.x + r.width - 0.5f, r.y}, {r.x + r.width - 0.5f, r.y + r.height}, 1, Color{40, 46, 60, 255});
    Vector2 m = GetMousePosition();
    int clicked = -1;
    float y = r.y + 10;
    float itemH = std::min(56.f, (r.height - 30) / 13.f);
    for (int t : kRailOrder) {
        if (t < 0) { DrawLineEx({r.x + 14, y + 4}, {r.x + r.width - 14, y + 4}, 1, Color{40, 46, 60, 255}); y += 9; continue; }
        Rectangle it{r.x + 4, y, r.width - 8, itemH};
        bool hov = CheckCollisionPointRec(m, it);
        bool on = t == active;
        if (on) {
            DrawRectangleRounded(it, 0.25f, 6, Color{40, 46, 62, 255});
            DrawRectangleRec({r.x, it.y + 6, 3, it.height - 12}, kAccent);
        } else if (hov) DrawRectangleRounded(it, 0.25f, 6, Color{30, 35, 48, 255});
        Color c = on ? kAccentHi : hov ? kText : kMuted;
        icon(t, {it.x + it.width / 2, it.y + itemH * 0.38f}, itemH * 0.45f, c);
        const char* nm = tabName(t);
        float fs = 11.5f;
        Vector2 ts = measureTextK(FontKind::Ui, nm, fs);
        drawTextK(FontKind::Ui, nm, {it.x + it.width / 2 - ts.x / 2, it.y + itemH * 0.72f}, fs, c);
        if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) clicked = t;
        y += itemH + 2;
    }
    return clicked;
}

// Panel background + header with the tab's title and a one-line description
inline float header(Rectangle p, int tab) {
    DrawRectangleRec(p, kPanel);
    DrawLineEx({p.x + p.width - 0.5f, p.y}, {p.x + p.width - 0.5f, p.y + p.height}, 1, Color{40, 46, 60, 255});
    drawTextK(FontKind::UiBold, tabName(tab), {p.x + 18, p.y + 14}, 24, kText);
    drawTextK(FontKind::Ui, tabBlurb(tab), {p.x + 18, p.y + 44}, 14, kMuted);
    DrawRectangle((int)p.x + 18, (int)p.y + 66, 28, 2, kAccent);
    return p.y + 76;
}

// Status bar along the bottom of the map
inline void statusBar(Rectangle r, const char* hint, const char* right) {
    DrawRectangleRec(r, Color{14, 17, 24, 225});
    DrawLineEx({r.x, r.y}, {r.x + r.width, r.y}, 1, Color{44, 50, 66, 255});
    float fs = 14;
    float x = r.x + 14;
    // hint text: segments separated by "|" get a little key-cap style first word if wrapped in []
    drawTextK(FontKind::Ui, hint, {x, r.y + (r.height - fs) / 2 - 1}, fs, kText);
    if (right && *right) {
        Vector2 ts = measureTextK(FontKind::Ui, right, fs);
        drawTextK(FontKind::Ui, right, {r.x + r.width - ts.x - 14, r.y + (r.height - fs) / 2 - 1}, fs, kMuted);
    }
}

// Floating card used for hover info and pop-ups on the map
inline void card(Rectangle r, Color edge = Color{70, 80, 104, 255}) {
    DrawRectangleRounded({r.x + 3, r.y + 4, r.width, r.height}, 0.06f, 6, Fade(BLACK, 0.35f));
    DrawRectangleRounded(r, 0.06f, 6, Color{20, 24, 33, 240});
    DrawRectangleRoundedLinesEx(r, 0.06f, 6, 1, edge);
}
}  // namespace theme
