#pragma once
#include "raylib.h"
#include "raygui.h"
#include <string>
#include <vector>

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

// Word-wrap text into lines that fit a width (keeps the user's own line breaks)
inline std::vector<std::string> wrapText(const std::string& text, int width, int fs) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string para = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string line;
        size_t p = 0;
        if (para.empty()) out.push_back("");
        while (p < para.size()) {
            size_t sp = para.find(' ', p);
            std::string word = para.substr(p, sp == std::string::npos ? std::string::npos : sp - p + 1);
            std::string trial = line + word;
            if (!line.empty() && MeasureText(trial.c_str(), fs) > width) { out.push_back(line); line = word; }
            else line = trial;
            p = sp == std::string::npos ? para.size() : sp + 1;
        }
        if (!line.empty()) out.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

// A simple multi-line text editor: click to start typing (cursor at the end), Enter for a new line,
// Backspace to delete, Ctrl+V to paste, mouse wheel to scroll. Click outside to stop.
inline void textArea(Rectangle r, std::string& text, bool& editing, int& scroll) {
    const int fs = 16, lh = 19;
    Vector2 m = GetMousePosition();
    bool inside = CheckCollisionPointRec(m, r);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) editing = inside;
    bool typed = false;
    if (editing) {
        int ch = GetCharPressed();
        while (ch > 0) {
            if (ch >= 32) { int n = 0; const char* u = CodepointToUTF8(ch, &n); text.append(u, n); typed = true; }
            ch = GetCharPressed();
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressedRepeat(KEY_ENTER)) { text += '\n'; typed = true; }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !text.empty()) {
            // remove one UTF-8 character
            size_t n = text.size() - 1;
            while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;
            text.erase(n);
        }
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V)) {
            const char* c = GetClipboardText();
            if (c) text += c;
        }
        if (IsKeyPressed(KEY_ESCAPE)) editing = false;
    }
    std::vector<std::string> lines = wrapText(text, (int)r.width - 16, fs);
    int visible = (int)((r.height - 10) / lh);
    if (inside) scroll -= (int)(GetMouseWheelMove() * 3);
    int maxScroll = std::max(0, (int)lines.size() - visible);
    if (typed) scroll = maxScroll;   // follow the cursor while typing
    if (scroll > maxScroll) scroll = maxScroll;
    if (scroll < 0) scroll = 0;
    DrawRectangleRec(r, editing ? Color{255, 255, 250, 255} : Color{245, 245, 245, 255});
    DrawRectangleLinesEx(r, editing ? 2.f : 1.f, editing ? Color{60, 120, 200, 255} : GRAY);
    for (int i = 0; i < visible && scroll + i < (int)lines.size(); i++)
        DrawText(lines[scroll + i].c_str(), (int)r.x + 8, (int)r.y + 6 + i * lh, fs, Color{30, 30, 30, 255});
    if (editing && ((int)(GetTime() * 2) % 2 == 0)) {
        int li = std::max(0, (int)lines.size() - 1);
        if (li - scroll >= 0 && li - scroll < visible) {
            int x = (int)r.x + 8 + (lines.empty() ? 0 : MeasureText(lines[li].c_str(), fs));
            if (!text.empty() && text.back() == '\n') { li++; x = (int)r.x + 8; }
            if (li - scroll < visible) DrawRectangle(x + 1, (int)r.y + 6 + (li - scroll) * lh, 2, fs, Color{30, 30, 30, 255});
        }
    }
    if (text.empty() && !editing) DrawText("Click to write...", (int)r.x + 8, (int)r.y + 6, fs, GRAY);
}
