#include "fonts.h"
#include <cmath>
#include <map>
#include <vector>
#include <cstddef>

extern const unsigned char kFiraRegular[], kFiraSemiBold[], kCrimsonSemiBold[], kCrimsonItalic[];
extern const size_t kFiraRegularSize, kFiraSemiBoldSize, kCrimsonSemiBoldSize, kCrimsonItalicSize;

namespace {
std::map<int, Font> g_cache;
std::vector<int> g_codepoints;
bool g_ready = false;

int quantise(float size) {
    int s = (int)std::lround(size);
    if (s < 8) s = 8;
    if (s > 24) s = (s + 2) / 4 * 4;     // fewer atlases for big label sizes
    if (s > 96) s = 96;
    return s;
}
}

void fontsInit() {
    if (g_ready) return;
    for (int c = 32; c < 127; c++) g_codepoints.push_back(c);
    for (int c = 160; c < 256; c++) g_codepoints.push_back(c);
    for (int c : {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192, 0x2193, 0x2212, 0x2248, 0x2264, 0x2265, 0x221E})
        g_codepoints.push_back(c);
    g_ready = true;
}

void fontsShutdown() {
    for (auto& kv : g_cache) UnloadFont(kv.second);
    g_cache.clear();
}

Font fontFor(FontKind k, float size) {
    if (!g_ready) fontsInit();
    int s = quantise(size);
    int key = (int)k * 1000 + s;
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second;
    const unsigned char* data = kFiraRegular; size_t n = kFiraRegularSize;
    if (k == FontKind::UiBold) { data = kFiraSemiBold; n = kFiraSemiBoldSize; }
    else if (k == FontKind::Serif) { data = kCrimsonSemiBold; n = kCrimsonSemiBoldSize; }
    else if (k == FontKind::SerifItalic) { data = kCrimsonItalic; n = kCrimsonItalicSize; }
    Font f = LoadFontFromMemory(".ttf", data, (int)n, s, g_codepoints.data(), (int)g_codepoints.size());
    if (f.texture.id == 0) f = GetFontDefault();
    else SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    g_cache[key] = f;
    return f;
}

void drawTextK(FontKind k, const char* text, Vector2 pos, float size, Color c) {
    Font f = fontFor(k, size);
    DrawTextEx(f, text, Vector2{std::round(pos.x), std::round(pos.y)}, size, 0, c);
}

Vector2 measureTextK(FontKind k, const char* text, float size) {
    return MeasureTextEx(fontFor(k, size), text, size, 0);
}

void uiText(const char* text, int x, int y, int size, Color c) { drawTextK(FontKind::Ui, text, Vector2{(float)x, (float)y}, (float)size, c); }
int uiMeasure(const char* text, int size) { return (int)std::ceil(measureTextK(FontKind::Ui, text, (float)size).x); }
