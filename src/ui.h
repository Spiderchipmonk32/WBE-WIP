#pragma once
#include "raylib.h"
#include "raygui.h"

// Tiny helper for stacking raygui controls down the side panel.
struct Layout {
    float x, y, w;
    void label(const char* t) { GuiLabel(Rectangle{x, y, w, 22}, t); y += 24; }
    void heading(const char* t) { y += 6; GuiLabel(Rectangle{x, y, w, 22}, t); y += 26; }
    void slider(float* v, float mn, float mx) { GuiSliderBar(Rectangle{x, y, w, 18}, NULL, NULL, v, mn, mx); y += 26; }
    bool button(const char* t, float h = 30) { bool r = GuiButton(Rectangle{x, y, w, h}, t); y += h + 8; return r; }
    void check(const char* t, bool* v) { GuiCheckBox(Rectangle{x, y, 20, 20}, t, v); y += 28; }
    void legend(Color c, const char* t) {
        DrawRectangle((int)x, (int)y + 3, 16, 16, c);
        DrawRectangleLines((int)x, (int)y + 3, 16, 16, DARKGRAY);
        GuiLabel(Rectangle{x + 24, y, w - 24, 22}, t);
        y += 24;
    }
    void note(const char* t) {
        // simple word wrap for help text
        int fs = 16, maxW = (int)w;
        const char* p = t;
        char line[256];
        while (*p) {
            int n = 0, lastSpace = -1;
            while (p[n] && n < 255) {
                line[n] = p[n]; line[n + 1] = 0;
                if (p[n] == ' ') lastSpace = n;
                if (MeasureText(line, fs) > maxW && lastSpace > 0) { n = lastSpace; break; }
                n++;
            }
            line[n] = 0;
            DrawText(line, (int)x, (int)y, fs, GRAY);
            y += 20;
            p += n;
            while (*p == ' ') p++;
        }
        y += 4;
    }
};
