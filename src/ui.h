#pragma once
#include "raylib.h"
#include "fonts.h"
#include "raygui.h"
#include <string>
#include <vector>
#include <cstring>
#include <cctype>

// Tiny helper for stacking raygui controls down the side panel.
// Labels of the form "Name: value" show the value right-aligned in a brighter colour.
struct Layout {
    float x, y, w;
    const char* nextTip = nullptr;
    void tip(const char* t) { nextTip = t; }
    void applyTip() { GuiSetTooltip(nextTip); nextTip = nullptr; }
    void label(const char* t) {
        const char* colon = strstr(t, ": ");
        if (colon && !strstr(colon + 2, ": ")) {
            std::string name(t, colon - t);
            drawTextK(FontKind::Ui, name.c_str(), {x, y + 2}, 16, Color{190, 198, 212, 255});
            const char* v = colon + 2;
            Vector2 ts = measureTextK(FontKind::UiBold, v, 16);
            drawTextK(FontKind::UiBold, v, {x + w - ts.x, y + 2}, 16, Color{240, 230, 210, 255});
        } else drawTextK(FontKind::Ui, t, {x, y + 2}, 16, Color{210, 216, 226, 255});
        y += 23;
    }
    void heading(const char* t) {
        y += 12;
        std::string up = t;
        for (char& c : up) c = (char)toupper((unsigned char)c);
        Vector2 ts = measureTextK(FontKind::UiBold, up.c_str(), 13);
        // letter-spaced small caps
        float cx = x;
        for (char c : up) {
            char b[2] = {c, 0};
            drawTextK(FontKind::UiBold, b, {cx, y}, 13, Color{232, 178, 84, 255});
            cx += measureTextK(FontKind::UiBold, b, 13).x + 1.6f;
        }
        (void)ts;
        DrawLineEx({cx + 8, y + 7.5f}, {x + w, y + 7.5f}, 1, Color{52, 60, 78, 255});
        y += 24;
    }
    void slider(float* v, float mn, float mx) {
        applyTip();
        GuiSliderBar(Rectangle{x, y + 2, w, 12}, NULL, NULL, v, mn, mx);
        // knob
        float f = (mx > mn) ? (*v - mn) / (mx - mn) : 0;
        f = f < 0 ? 0 : f > 1 ? 1 : f;
        DrawCircleV({x + f * w, y + 8}, 7, Color{246, 202, 118, 255});
        DrawCircleLinesV({x + f * w, y + 8}, 7, Color{25, 20, 12, 255});
        y += 24;
    }
    bool button(const char* t, float h = 30) { applyTip(); bool r = GuiButton(Rectangle{x, y, w, h}, t); y += h + 8; return r; }
    // A filled, brass-coloured button for the main action of a section
    bool primary(const char* t, float h = 36) {
        Rectangle r{x, y, w, h};
        Vector2 m = GetMousePosition();
        bool hov = CheckCollisionPointRec(m, r) && !GuiIsLocked();
        DrawRectangleRounded(r, 0.2f, 6, hov ? Color{246, 202, 118, 255} : Color{232, 178, 84, 255});
        Vector2 ts = measureTextK(FontKind::UiBold, t, 17);
        drawTextK(FontKind::UiBold, t, {x + w / 2 - ts.x / 2, y + h / 2 - ts.y / 2}, 17, Color{30, 22, 10, 255});
        y += h + 10;
        return hov && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    }
    void check(const char* t, bool* v) { applyTip(); GuiCheckBox(Rectangle{x, y, 18, 18}, t, v); y += 26; }
    void legend(Color c, const char* t) {
        DrawRectangleRounded({x, y + 3, 16, 16}, 0.3f, 4, c);
        drawTextK(FontKind::Ui, t, {x + 24, y + 2}, 15, Color{210, 216, 226, 255});
        y += 22;
    }
    void note(const char* t) {
        int fs = 14;
        for (const std::string& line : wrapLines(t, (int)w, fs)) {
            drawTextK(FontKind::Ui, line.c_str(), {x, y}, (float)fs, Color{142, 152, 170, 255});
            y += 18;
        }
        y += 6;
    }
    static std::vector<std::string> wrapLines(const char* t, int width, int fs) {
        std::vector<std::string> out;
        std::string line, word;
        auto flush = [&]() {
            std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && measureTextK(FontKind::Ui, trial.c_str(), (float)fs).x > width) { out.push_back(line); line = word; }
            else line = trial;
            word.clear();
        };
        for (const char* p = t; ; p++) {
            if (*p == ' ' || *p == 0 || *p == '\n') {
                if (!word.empty()) flush();
                if (*p == '\n') { out.push_back(line); line.clear(); }
                if (*p == 0) break;
            } else word += *p;
        }
        if (!line.empty()) out.push_back(line);
        return out;
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
            if (!line.empty() && uiMeasure(trial.c_str(), fs) > width) { out.push_back(line); line = word; }
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
    DrawRectangleRec(r, editing ? Color{12, 14, 19, 255} : Color{17, 20, 27, 255});
    DrawRectangleLinesEx(r, 1.f, editing ? Color{232, 178, 84, 255} : Color{58, 67, 88, 255});
    for (int i = 0; i < visible && scroll + i < (int)lines.size(); i++)
        uiText(lines[scroll + i].c_str(), (int)r.x + 8, (int)r.y + 6 + i * lh, fs, Color{222, 227, 235, 255});
    if (editing && ((int)(GetTime() * 2) % 2 == 0)) {
        int li = std::max(0, (int)lines.size() - 1);
        if (li - scroll >= 0 && li - scroll < visible) {
            int x = (int)r.x + 8 + (lines.empty() ? 0 : uiMeasure(lines[li].c_str(), fs));
            if (!text.empty() && text.back() == '\n') { li++; x = (int)r.x + 8; }
            if (li - scroll < visible) DrawRectangle(x + 1, (int)r.y + 6 + (li - scroll) * lh, 2, fs, Color{246, 202, 118, 255});
        }
    }
    if (text.empty() && !editing) uiText("Click to write...", (int)r.x + 8, (int)r.y + 6, fs, Color{110, 118, 135, 255});
}
