#pragma once
// A World Anvil-style timeline: named ages, events, the lives of nations with their
// rulers, and the lifespans of your characters, on one zoomable strip.
#include "document.h"
#include "raylib.h"

struct TimelineView {
    double startYear = -1000, yearsPerPx = 4;
    bool fitted = false;
    bool showNations = true, showPeople = true;
    int hoverEvent = -1;
    // returns: clicked event (or -1); year is set when the "now" marker is dragged
    int frame(const Document& doc, Rectangle area, bool mouseFree, int& year);
    void fit(const Document& doc, float width);
};
