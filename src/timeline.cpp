#include "timeline.h"
#include "theme.h"
#include <algorithm>
#include <cmath>

void TimelineView::fit(const Document& doc, float width) {
    int lo = doc.currentYear - 100, hi = doc.currentYear + 20;
    for (auto& e : doc.events) { lo = std::min(lo, e.year); hi = std::max(hi, e.year); }
    for (auto& a : doc.ages) { lo = std::min(lo, a.start); hi = std::max(hi, a.end); }
    for (auto& n : doc.nations) for (auto& r : n.rulers) lo = std::min(lo, r.start);
    lo = std::max(lo, doc.currentYear - 5000);
    double span = std::max(50, hi - lo);
    startYear = lo - span * 0.03;
    yearsPerPx = span * 1.06 / std::max(200.f, width);
    fitted = true;
}

int TimelineView::frame(const Document& doc, Rectangle area, bool mouseFree, int& year) {
    if (!fitted) fit(doc, area.width);
    Vector2 m = GetMousePosition();
    bool in = mouseFree && CheckCollisionPointRec(m, area);
    auto xOf = [&](double y) { return (float)(area.x + (y - startYear) / yearsPerPx); };
    auto yearAt = [&](float x) { return startYear + (x - area.x) * yearsPerPx; };
    static bool draggingNow = false;
    if (in) {
        float wv = GetMouseWheelMove();
        if (wv != 0) {
            double at = yearAt(m.x);
            yearsPerPx = std::max(0.02, std::min(200.0, yearsPerPx * std::pow(0.85, wv)));
            startYear = at - (m.x - area.x) * yearsPerPx;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !draggingNow && hoverEvent < 0 && !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))
            startYear -= GetMouseDelta().x * yearsPerPx;
    }
    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    DrawRectangleRec(area, Color{16, 19, 27, 255});
    // year ruler
    double target = 110 * yearsPerPx, mag = std::pow(10.0, std::floor(std::log10(std::max(1e-9, target)))), stepY = mag;
    for (double k : {1.0, 2.0, 5.0, 10.0}) if (k * mag >= target) { stepY = k * mag; break; }
    stepY = std::max(1.0, stepY);
    float rulerY = area.y + 46;
    for (double y = std::floor(startYear / stepY) * stepY; xOf(y) < area.x + area.width; y += stepY) {
        float x = xOf(y);
        DrawLineEx({x, rulerY}, {x, area.y + area.height}, 1, Color{34, 40, 54, 255});
        drawTextK(FontKind::Ui, TextFormat("%.0f", y), {x + 4, rulerY - 18}, 13, theme::kMuted);
    }
    // ages
    float ageY = area.y + 8;
    for (auto& a : doc.ages) {
        float x0 = xOf(a.start), x1 = xOf(a.end);
        if (x1 < area.x || x0 > area.x + area.width) continue;
        DrawRectangleRounded({x0, ageY, std::max(2.f, x1 - x0), 22}, 0.3f, 4, Color{a.r, a.g, a.b, 200});
        drawTextK(FontKind::Serif, a.name.c_str(), {std::max(x0, area.x) + 8, ageY + 2}, 16, Color{20, 15, 10, 255});
    }
    int clicked = -1;
    float y = rulerY + 12;
    // events, stacked into rows so labels don't overlap
    hoverEvent = -1;
    {
        std::vector<int> order(doc.events.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
        std::sort(order.begin(), order.end(), [&](int a, int b) { return doc.events[a].year < doc.events[b].year; });
        std::vector<float> rowEnd(6, -1e9f);
        static const Color typeCol[] = {{200, 200, 210, 255}, {240, 120, 60, 255}, {230, 70, 40, 255}, {200, 160, 90, 255}, {80, 140, 220, 255},
                                        {120, 210, 120, 255}, {150, 150, 160, 255}, {235, 70, 70, 255}, {110, 200, 230, 255}, {220, 170, 60, 255}, {190, 110, 240, 255}};
        drawTextK(FontKind::UiBold, "EVENTS", {area.x + 10, y}, 12, theme::kAccent);
        y += 18;
        for (int i : order) {
            const HistEvent& e = doc.events[i];
            float x = xOf(e.year);
            if (x < area.x - 200 || x > area.x + area.width + 10) continue;
            float tw = measureTextK(FontKind::Ui, e.title.c_str(), 13).x + 18;
            int row = 0;
            while (row < 5 && rowEnd[row] > x) row++;
            rowEnd[row] = x + tw;
            float ry = y + row * 22;
            Color c = typeCol[std::min((int)e.type, 10)];
            DrawCircleV({x, ry + 8}, 5, c);
            bool hov = std::fabs(m.x - x) < tw && m.x >= x - 6 && std::fabs(m.y - (ry + 8)) < 10 && in;
            if (hov) hoverEvent = i;
            drawTextK(FontKind::Ui, e.title.c_str(), {x + 9, ry + 1}, 13, hov ? theme::kAccentHi : theme::kText);
            if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) clicked = i;
        }
        y += 6 * 22 + 8;
    }
    // nations with their rulers
    if (showNations && !doc.nations.empty()) {
        drawTextK(FontKind::UiBold, "NATIONS AND RULERS", {area.x + 10, y}, 12, theme::kAccent);
        y += 18;
        float lane = 20;
        int maxRows = (int)((area.y + area.height - y - (showPeople ? 120 : 10)) / lane);
        int row = 0;
        for (int n = 0; n < (int)doc.nations.size() && row < maxRows; n++, row++) {
            const Nation& N = doc.nations[n];
            int from = N.founded > -99999 ? N.founded : (N.rulers.empty() ? doc.yearMin : N.rulers.front().start);
            int to = N.fell < 99999 ? N.fell : std::max(doc.currentYear, doc.simYear);
            float x0 = xOf(from), x1 = xOf(to), ry = y + row * lane;
            Color c{N.r, N.g, N.b, 200};
            DrawRectangleRec({x0, ry + 3, std::max(2.f, x1 - x0), lane - 6}, Fade(c, 0.45f));
            for (auto& r : N.rulers) {
                float rx = xOf(r.start);
                if (rx > x0 && rx < x1) DrawLineEx({rx, ry + 3}, {rx, ry + lane - 3}, 1, Color{20, 20, 25, 200});
                float rx1 = xOf(std::min(r.end, to));
                if (m.x >= rx && m.x < rx1 && m.y >= ry && m.y < ry + lane && in) {
                    std::string t = N.name + ": " + r.name + TextFormat(" (%d-%d)", r.start, std::min(r.end, to));
                    Vector2 ts = measureTextK(FontKind::Ui, t.c_str(), 13);
                    theme::card({m.x + 12, m.y - 30, ts.x + 16, 24});
                    drawTextK(FontKind::Ui, t.c_str(), {m.x + 20, m.y - 26}, 13, theme::kText);
                }
            }
            drawTextK(FontKind::Ui, N.name.c_str(), {std::max(x0, area.x) + 4, ry + 3}, 13, N.fell < 99999 ? theme::kMuted : theme::kText);
            if (N.fell < 99999) drawTextK(FontKind::UiBold, "x", {x1 + 3, ry + 2}, 13, Color{230, 80, 70, 255});
        }
        y += row * lane + 10;
    }
    // people's lives
    if (showPeople) {
        std::vector<const Character*> people;
        for (auto& c : doc.characters) if (c.userMade || c.followed || c.role == Role::Ruler) people.push_back(&c);
        if (people.size() > 40) people.resize(40);
        if (!people.empty()) {
            drawTextK(FontKind::UiBold, "LIVES", {area.x + 10, y}, 12, theme::kAccent);
            y += 18;
            std::vector<float> rowEnd(8, -1e9f);
            for (const Character* c : people) {
                float x0 = xOf(c->born), x1 = xOf(c->died < 99999 ? c->died : std::max(doc.currentYear, doc.simYear));
                int row = 0;
                while (row < 7 && rowEnd[row] > x0) row++;
                rowEnd[row] = std::max(x1, x0 + measureTextK(FontKind::Ui, c->name.c_str(), 12).x + 10);
                float ry = y + row * 16;
                if (ry > area.y + area.height - 14) continue;
                DrawRectangleRounded({x0, ry + 2, std::max(3.f, x1 - x0), 10}, 0.5f, 4, c->followed ? theme::kAccent : Color{110, 160, 200, 200});
                drawTextK(FontKind::Ui, c->name.c_str(), {x0 + 4, ry}, 12, Color{15, 18, 25, 255});
            }
        }
    }
    // "now"
    float nx = xOf(year);
    DrawLineEx({nx, area.y}, {nx, area.y + area.height}, 2, theme::kAccentHi);
    drawTextK(FontKind::UiBold, TextFormat("%d", year), {nx + 5, area.y + area.height - 22}, 14, theme::kAccentHi);
    if (in && std::fabs(m.x - nx) < 6 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) draggingNow = true;
    if (draggingNow) { year = (int)std::lround(yearAt(m.x)); if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) draggingNow = false; }
    EndScissorMode();
    return clicked;
}
