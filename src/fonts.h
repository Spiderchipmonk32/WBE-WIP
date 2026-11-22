#pragma once
// Embedded TrueType fonts (Fira Sans for the interface, Crimson Text for map labels),
// rasterised on demand at each pixel size so text stays crisp at any size.
#include "raylib.h"

enum class FontKind { Ui, UiBold, Serif, SerifItalic };

void fontsInit();                         // call once after InitWindow
void fontsShutdown();
Font fontFor(FontKind k, float size);     // atlas suited to that size

// Drop-in replacements for raylib's DrawText / MeasureText using the interface font
void uiText(const char* text, int x, int y, int size, Color c);
int uiMeasure(const char* text, int size);
// Same with a chosen font and float positions
void drawTextK(FontKind k, const char* text, Vector2 pos, float size, Color c);
Vector2 measureTextK(FontKind k, const char* text, float size);
