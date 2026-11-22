#include "webview.h"
#include "characters.h"
#include "charpanel.h"
#include "extras.h"
#include "theme.h"
#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <map>

namespace {
Color relColor(RelType t) {
    switch (t) {
        case RelType::Parent: case RelType::Sibling: return Color{230, 190, 90, 255};
        case RelType::Spouse: case RelType::Lover: return Color{240, 120, 170, 255};
        case RelType::Friend: case RelType::Ally: return Color{110, 210, 130, 255};
        case RelType::Rival: return Color{240, 160, 60, 255};
        case RelType::Enemy: return Color{235, 70, 60, 255};
        case RelType::Mentor: return Color{150, 200, 240, 255};
        case RelType::Employs: case RelType::Serves: case RelType::Owes: return Color{100, 150, 230, 255};
        case RelType::Rules: return Color{190, 120, 240, 255};
        default: return Color{150, 160, 175, 255};
    }
}
Color nationRelColor(Relation r) {
    static const Color rc[] = {{40, 200, 80, 255}, {60, 160, 230, 255}, {240, 160, 40, 255}, {230, 30, 30, 255}, {170, 90, 220, 255}};
    return rc[std::min(4, (int)r)];
}
const char* nationRelName(Relation r) {
    static const char* n[] = {"Alliance", "Trade pact", "Rivalry", "At war", "Vassal of"};
    return n[std::min(4, (int)r)];
}
uint32_t hashRef(EntityRef r) { return (uint32_t)r.id * 2654435761u ^ ((uint32_t)r.kind * 40503u); }
}

void WebView::build(const Document& doc, int year) {
    struct E { EntityRef a, b; Color col; std::string label; bool dir, dashed; float w; int link; };
    std::vector<EntityRef> cand;
    std::vector<E> es;
    auto shownKind = [&](EntKind k) {
        return (k == EntKind::Character && showCharacters) || (k == EntKind::Nation && showNations) || (k == EntKind::City && showCities) ||
               (k == EntKind::Religion && showFaiths) || (k == EntKind::Culture && showCultures);
    };
    auto valid = [&](EntityRef r) {
        switch (r.kind) {
            case EntKind::Character: { const Character* c = doc.character(r.id); return c && (!onlyLiving || c->alive(year) || c->userMade); }
            case EntKind::Nation: return r.id >= 0 && r.id < (int)doc.nations.size() && doc.nations[r.id].fell > year;
            case EntKind::City: return r.id >= 0 && r.id < (int)doc.cities.size();
            case EntKind::Religion: return r.id >= 0 && r.id < (int)doc.religions.size();
            case EntKind::Culture: return r.id >= 0 && r.id < (int)doc.cultures.size();
        }
        return false;
    };
    if (showNations) for (int i = 0; i < (int)doc.nations.size(); i++) if (valid({EntKind::Nation, i})) cand.push_back({EntKind::Nation, i});
    if (showCharacters) for (auto& c : doc.characters) if (valid({EntKind::Character, c.id})) cand.push_back({EntKind::Character, c.id});
    if (showCities) for (int i = 0; i < (int)doc.cities.size(); i++) cand.push_back({EntKind::City, i});
    if (showFaiths) for (int i = 0; i < (int)doc.religions.size(); i++) cand.push_back({EntKind::Religion, i});
    if (showCultures) for (int i = 0; i < (int)doc.cultures.size(); i++) cand.push_back({EntKind::Culture, i});
    if (showPolitics && showNations)
        for (auto& r : doc.relations)
            if (valid({EntKind::Nation, r.a}) && valid({EntKind::Nation, r.b}))
                es.push_back({{EntKind::Nation, r.a}, {EntKind::Nation, r.b}, nationRelColor(r.type), nationRelName(r.type), r.type == Relation::Vassal,
                              r.type == Relation::Rivalry, r.type == Relation::War ? 4.f : 3.f, -1});
    if (showPersonal)
        for (int i = 0; i < (int)doc.links.size(); i++) {
            const Link& l = doc.links[i];
            if (!shownKind(l.a.kind) || !shownKind(l.b.kind) || !valid(l.a) || !valid(l.b)) continue;
            es.push_back({l.a, l.b, relColor(l.type), relTypeName(l.type), relTypeDirected(l.type), l.type == RelType::Rival || l.type == RelType::Enemy,
                          1.f + l.strength * 0.5f, i});
        }
    if (showMembership) {
        if (showCities && showNations)
            for (int i = 0; i < (int)doc.cities.size(); i++)
                if (valid({EntKind::Nation, doc.cities[i].nation}))
                    es.push_back({{EntKind::City, i}, {EntKind::Nation, doc.cities[i].nation}, Color{120, 130, 150, 160}, doc.cities[i].capital ? "Capital of" : "City of", true, false, 1.2f, -1});
        for (auto& c : doc.characters) {
            if (!valid({EntKind::Character, c.id}) || !showCharacters) continue;
            if (showCities && c.city >= 0 && c.city < (int)doc.cities.size())
                es.push_back({{EntKind::Character, c.id}, {EntKind::City, c.city}, Color{120, 130, 150, 140}, "Lives in", true, false, 1, -1});
            if (showFaiths && c.religion >= 0 && c.religion < (int)doc.religions.size())
                es.push_back({{EntKind::Character, c.id}, {EntKind::Religion, c.religion}, Color{220, 210, 120, 120}, "Follows", true, false, 1, -1});
            if (showCultures && c.culture >= 0 && c.culture < (int)doc.cultures.size())
                es.push_back({{EntKind::Character, c.id}, {EntKind::Culture, c.culture}, Color{150, 200, 200, 120}, "Of the", true, false, 1, -1});
            // people who aren't linked to anything in their nation still belong to it
            if (showNations && !showCities && c.role != Role::Ruler && valid({EntKind::Nation, c.nation}) && hasFocus)
                es.push_back({{EntKind::Character, c.id}, {EntKind::Nation, c.nation}, Color{120, 130, 150, 90}, "Subject of", true, false, 1, -1});
        }
    }
    // keep only what is near the focus (or a political overview when there's too much)
    std::unordered_set<uint64_t> keep;
    if (hasFocus && valid(focus)) {
        std::unordered_map<uint64_t, std::vector<uint64_t>> adj;
        for (auto& e : es) { adj[key(e.a)].push_back(key(e.b)); adj[key(e.b)].push_back(key(e.a)); }
        std::vector<uint64_t> frontier{key(focus)};
        keep.insert(key(focus));
        for (int d = 0; d < depth; d++) {
            std::vector<uint64_t> next;
            for (uint64_t k : frontier)
                for (uint64_t n : adj[k])
                    if (!keep.count(n) && keep.size() < 500) { keep.insert(n); next.push_back(n); }
            frontier.swap(next);
        }
    } else {
        hasFocus = false;
        bool big = cand.size() > 260;
        for (auto& r : cand) {
            if (big && r.kind == EntKind::Character) {
                const Character* c = doc.character(r.id);
                if (!c || (c->role != Role::Ruler && !c->userMade && !c->followed)) continue;
            }
            if (big && r.kind == EntKind::City && !doc.cities[r.id].capital) continue;
            keep.insert(key(r));
        }
    }
    // nodes
    nodes.clear();
    std::unordered_map<uint64_t, int> index;
    for (auto& r : cand) {
        uint64_t k = key(r);
        if (!keep.count(k)) continue;
        auto it = layout.find(k);
        if (it == layout.end()) {
            WebNode n; n.ref = r;
            uint32_t h = hashRef(r);
            float a = (h % 6283) / 1000.f, rad = 150 + (h >> 13) % 500;
            n.x = std::cos(a) * rad + (hasFocus ? camX : 0); n.y = std::sin(a) * rad + (hasFocus ? camY : 0);
            it = layout.emplace(k, n).first;
        }
        index[k] = (int)nodes.size();
        nodes.push_back(&it->second);
    }
    edges.clear();
    for (auto& e : es) {
        auto ia = index.find(key(e.a)), ib = index.find(key(e.b));
        if (ia == index.end() || ib == index.end() || ia->second == ib->second) continue;
        edges.push_back({ia->second, ib->second, e.col, e.label, e.dir, e.dashed, e.w, e.link});
    }
}

static void drawDashed(Vector2 a, Vector2 b, float w, Color c) {
    float len = std::hypot(b.x - a.x, b.y - a.y);
    int n = std::max(1, (int)(len / 10));
    for (int k = 0; k < n; k += 2) {
        float t0 = (float)k / n, t1 = (float)(k + 1) / n;
        DrawLineEx({a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0}, {a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1}, w, c);
    }
}

void WebView::frame(Document& doc, int year, Rectangle area, float dt, bool mouseFree) {
    build(doc, year);
    const int n = (int)nodes.size();
    // ---- physics: nodes push apart, links pull together ----
    for (int it = 0; it < 2; it++) {
        std::vector<float> fx(n, 0), fy(n, 0);
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                float dx = nodes[i]->x - nodes[j]->x, dy = nodes[i]->y - nodes[j]->y;
                float d2 = dx * dx + dy * dy + 25;
                if (d2 > 640000) continue;
                float f = 9000.f / d2;
                float d = std::sqrt(d2);
                fx[i] += f * dx / d; fy[i] += f * dy / d; fx[j] -= f * dx / d; fy[j] -= f * dy / d;
            }
        for (auto& e : edges) {
            WebNode* A = nodes[e.a]; WebNode* B = nodes[e.b];
            float dx = B->x - A->x, dy = B->y - A->y, d = std::sqrt(dx * dx + dy * dy) + 0.01f;
            float rest = (A->ref.kind == EntKind::Nation && B->ref.kind == EntKind::Nation) ? 260.f : 130.f;
            float f = (d - rest) * 0.02f;
            fx[e.a] += f * dx / d; fy[e.a] += f * dy / d; fx[e.b] -= f * dx / d; fy[e.b] -= f * dy / d;
        }
        float cx = hasFocus ? camX : 0, cy = hasFocus ? camY : 0;
        for (int i = 0; i < n; i++) {
            WebNode* p = nodes[i];
            if (p->pinned || i == dragNode) { p->vx = p->vy = 0; continue; }
            fx[i] += (cx - p->x) * 0.0015f; fy[i] += (cy - p->y) * 0.0015f;
            if (hasFocus && p->ref == focus) { fx[i] += (camX - p->x) * 0.05f; fy[i] += (camY - p->y) * 0.05f; }
            p->vx = (p->vx + fx[i]) * 0.82f; p->vy = (p->vy + fy[i]) * 0.82f;
            float sp = std::sqrt(p->vx * p->vx + p->vy * p->vy);
            if (sp > 25) { p->vx *= 25 / sp; p->vy *= 25 / sp; }
            p->x += p->vx; p->y += p->vy;
        }
    }
    (void)dt;
    // ---- view and mouse ----
    Vector2 m = GetMousePosition();
    Vector2 c0{area.x + area.width / 2, area.y + area.height / 2};
    auto toS = [&](float x, float y) { return Vector2{c0.x + (x - camX) * zoom, c0.y + (y - camY) * zoom}; };
    auto toW = [&](Vector2 s) { return Vector2{(s.x - c0.x) / zoom + camX, (s.y - c0.y) / zoom + camY}; };
    bool inArea = mouseFree && CheckCollisionPointRec(m, area);
    if (inArea) {
        float wv = GetMouseWheelMove();
        if (wv != 0) {
            Vector2 before = toW(m);
            zoom = std::max(0.15f, std::min(3.f, zoom * std::pow(1.15f, wv)));
            Vector2 after = toW(m);
            camX += before.x - after.x; camY += before.y - after.y;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            Vector2 d = GetMouseDelta();
            camX -= d.x / zoom; camY -= d.y / zoom;
        }
    }
    hoverNode = -1;
    float nodeR = 20 * std::max(0.6f, zoom);
    for (int i = n - 1; i >= 0; i--) {
        Vector2 p = toS(nodes[i]->x, nodes[i]->y);
        if (std::hypot(p.x - m.x, p.y - m.y) < nodeR + 2) { hoverNode = i; break; }
    }
    static Vector2 pressAt{0, 0};
    if (inArea && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        pressAt = m;
        if (hoverNode >= 0) {
            if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) { linking = true; linkFrom = hoverNode; }
            else dragNode = hoverNode;
        } else selNode = -1;
    }
    if (dragNode >= 0 && dragNode < n && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 w = toW(m);
        nodes[dragNode]->x = w.x; nodes[dragNode]->y = w.y;
        if (std::hypot(m.x - pressAt.x, m.y - pressAt.y) > 4) nodes[dragNode]->pinned = true;
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (dragNode >= 0 && std::hypot(m.x - pressAt.x, m.y - pressAt.y) <= 4) {
            // a click: select; a click on the selected node focuses the web on it
            if (selNode == dragNode) focusOn(nodes[dragNode]->ref);
            selNode = dragNode;
        }
        if (linking && hoverNode >= 0 && hoverNode != linkFrom && linkFrom < n) {
            doc.addLink(nodes[linkFrom]->ref, nodes[hoverNode]->ref, (RelType)newLinkType, year, 3);
        }
        dragNode = -1; linking = false;
    }
    // ---- draw ----
    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    DrawRectangleRec(area, Color{16, 19, 27, 255});
    // faint dot grid
    float gs = 60 * zoom;
    if (gs > 14) {
        float ox = std::fmod(c0.x - camX * zoom, gs), oy = std::fmod(c0.y - camY * zoom, gs);
        for (float x = area.x + ox; x < area.x + area.width; x += gs)
            for (float y = area.y + oy; y < area.y + area.height; y += gs) DrawPixel((int)x, (int)y, Color{50, 58, 76, 255});
    }
    hoverEdge = -1;
    float bestE = 6;
    for (int i = 0; i < (int)edges.size(); i++) {
        const WebEdge& e = edges[i];
        Vector2 a = toS(nodes[e.a]->x, nodes[e.a]->y), b = toS(nodes[e.b]->x, nodes[e.b]->y);
        bool hl = selNode >= 0 && (e.a == selNode || e.b == selNode);
        Color col = selNode >= 0 && !hl ? Fade(e.col, 0.25f) : e.col;
        float w = e.width * std::max(0.6f, zoom) * (hl ? 1.4f : 1.f);
        if (e.dashed) drawDashed(a, b, w, col); else DrawLineEx(a, b, w, col);
        if (e.directed) {
            float ang = std::atan2(b.y - a.y, b.x - a.x);
            Vector2 tip{b.x - std::cos(ang) * (nodeR + 3), b.y - std::sin(ang) * (nodeR + 3)};
            float as = 8 * std::max(0.7f, zoom);
            DrawTriangle(tip, {tip.x - as * std::cos(ang + 0.45f), tip.y - as * std::sin(ang + 0.45f)}, {tip.x - as * std::cos(ang - 0.45f), tip.y - as * std::sin(ang - 0.45f)}, col);
            DrawTriangle(tip, {tip.x - as * std::cos(ang - 0.45f), tip.y - as * std::sin(ang - 0.45f)}, {tip.x - as * std::cos(ang + 0.45f), tip.y - as * std::sin(ang + 0.45f)}, col);
        }
        // distance from mouse to the line, for the tooltip
        float dx = b.x - a.x, dy = b.y - a.y, L2 = dx * dx + dy * dy;
        float t = L2 > 0 ? std::max(0.f, std::min(1.f, ((m.x - a.x) * dx + (m.y - a.y) * dy) / L2)) : 0;
        float d = std::hypot(a.x + t * dx - m.x, a.y + t * dy - m.y);
        if (inArea && d < bestE && hoverNode < 0) { bestE = d; hoverEdge = i; }
    }
    if (linking && linkFrom >= 0 && linkFrom < n) {
        Vector2 a = toS(nodes[linkFrom]->x, nodes[linkFrom]->y);
        drawDashed(a, m, 2.5f, theme::kAccentHi);
    }
    for (int i = 0; i < n; i++) {
        const WebNode* p = nodes[i];
        Vector2 s = toS(p->x, p->y);
        if (s.x < area.x - 80 || s.x > area.x + area.width + 80 || s.y < area.y - 80 || s.y > area.y + area.height + 80) continue;
        bool sel = i == selNode, hov = i == hoverNode, foc = hasFocus && p->ref == focus;
        bool dim = selNode >= 0 && !sel;
        if (dim) for (auto& e : edges) if ((e.a == selNode && e.b == i) || (e.b == selNode && e.a == i)) { dim = false; break; }
        float r = nodeR * (foc ? 1.25f : 1.f);
        std::string label = entityName(doc, p->ref);
        Color ring = sel ? theme::kAccentHi : hov ? theme::kText : Color{70, 80, 100, 255};
        switch (p->ref.kind) {
            case EntKind::Character: {
                const Character* c = doc.character(p->ref.id);
                if (!c) break;
                Rectangle pr{s.x - r, s.y - r, r * 2, r * 2};
                drawPortrait(pr, *c, year);
                DrawRectangleLinesEx(pr, sel || foc ? 3.f : 1.5f, ring);
                if (c->role == Role::Ruler && !c->title.empty()) label = c->title + " " + label;
                break;
            }
            case EntKind::Nation: {
                const Nation& nat = doc.nations[p->ref.id];
                Rectangle fr{s.x - r * 1.3f, s.y - r * 0.85f, r * 2.6f, r * 1.7f};
                drawFlag(fr, nat);
                DrawRectangleLinesEx(fr, sel || foc ? 3.f : 1.5f, ring);
                break;
            }
            case EntKind::City: {
                DrawCircleV(s, r * 0.7f, Color{60, 70, 90, 255});
                DrawRectangleRec({s.x - r * 0.35f, s.y - r * 0.3f, r * 0.2f, r * 0.6f}, theme::kText);
                DrawRectangleRec({s.x - r * 0.1f, s.y - r * 0.45f, r * 0.2f, r * 0.75f}, theme::kText);
                DrawRectangleRec({s.x + r * 0.15f, s.y - r * 0.2f, r * 0.2f, r * 0.5f}, theme::kText);
                DrawCircleLinesV(s, r * 0.7f, ring);
                break;
            }
            case EntKind::Religion: {
                const Religion& rl = doc.religions[p->ref.id];
                DrawPoly(s, 6, r * 0.8f, 0, Color{rl.r, rl.g, rl.b, 255});
                DrawPolyLinesEx(s, 6, r * 0.8f, 0, 2, ring);
                break;
            }
            case EntKind::Culture: {
                const Culture& cu = doc.cultures[p->ref.id];
                DrawPoly(s, 4, r * 0.8f, 45, Color{cu.r, cu.g, cu.b, 255});
                DrawPolyLinesEx(s, 4, r * 0.8f, 45, 2, ring);
                break;
            }
        }
        if (dim) DrawCircleV(s, r * 1.4f, Color{16, 19, 27, 150});
        if (zoom > 0.55f || sel || hov || foc || p->ref.kind == EntKind::Nation) {
            float fs = p->ref.kind == EntKind::Nation ? 15.f : 13.f;
            Vector2 ts = measureTextK(p->ref.kind == EntKind::Nation ? FontKind::Serif : FontKind::Ui, label.c_str(), fs);
            Vector2 at{s.x - ts.x / 2, s.y + r + (p->ref.kind == EntKind::Nation ? -2.f : 3.f)};
            DrawRectangleRounded({at.x - 4, at.y - 1, ts.x + 8, ts.y + 2}, 0.4f, 4, Color{16, 19, 27, 190});
            drawTextK(p->ref.kind == EntKind::Nation ? FontKind::Serif : FontKind::Ui, label.c_str(), at, fs, dim ? theme::kMuted : theme::kText);
        }
    }
    if (hoverEdge >= 0) {
        const WebEdge& e = edges[hoverEdge];
        std::string t = entityName(doc, nodes[e.a]->ref) + "  -  " + e.label + "  -  " + entityName(doc, nodes[e.b]->ref);
        Vector2 ts = measureTextK(FontKind::Ui, t.c_str(), 14);
        theme::card(Rectangle{m.x + 14, m.y + 10, ts.x + 20, 28}, e.col);
        drawTextK(FontKind::Ui, t.c_str(), {m.x + 24, m.y + 16}, 14, theme::kText);
    }
    if (n == 0) {
        const char* t = doc.characters.empty() && doc.nations.empty() ? "Nothing to show yet: make nations (Nations tab) or characters (Characters tab)." : "Nothing matches these filters.";
        Vector2 ts = measureTextK(FontKind::Ui, t, 17);
        drawTextK(FontKind::Ui, t, {c0.x - ts.x / 2, c0.y}, 17, theme::kMuted);
    }
    // legend
    {
        static const RelType lt[] = {RelType::Parent, RelType::Spouse, RelType::Friend, RelType::Rival, RelType::Enemy, RelType::Serves, RelType::Rules, RelType::Mentor};
        static const char* ln[] = {"Family", "Love", "Friends, allies", "Rivals", "Enemies", "Work, service", "Rules", "Teaching"};
        float x = area.x + 14, y = area.y + area.height - 8 - 8 * 20 - 120;
        theme::card(Rectangle{x - 6, y - 8, 170, 8 * 20 + 5 * 20 + 20});
        for (int i = 0; i < 8; i++) { DrawLineEx({x, y + i * 20 + 8}, {x + 26, y + i * 20 + 8}, 3, relColor(lt[i])); drawTextK(FontKind::Ui, ln[i], {x + 34, y + i * 20}, 13, theme::kText); }
        y += 8 * 20;
        static const char* nl[] = {"Alliance", "Trade pact", "Rivalry", "War", "Vassal"};
        for (int i = 0; i < 5; i++) { DrawLineEx({x, y + i * 20 + 8}, {x + 26, y + i * 20 + 8}, 4, nationRelColor((Relation)i)); drawTextK(FontKind::Ui, nl[i], {x + 34, y + i * 20}, 13, theme::kText); }
    }
    EndScissorMode();
}

void WebView::panel(Layout& L, Document& doc, int year, std::string& status) {
    L.heading("VIEW");
    {
        int fm = familyMode ? 1 : 0;
        GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 4) / 2, 30}, "Relationship web;Family tree", &fm);
        familyMode = fm == 1;
        L.y += 38;
    }
    L.heading("SHOW");
    {
        float hw = L.w / 2;
        auto two = [&](const char* a, bool* va, const char* b, bool* vb) {
            GuiCheckBox(Rectangle{L.x, L.y, 18, 18}, a, va);
            if (b) GuiCheckBox(Rectangle{L.x + hw, L.y, 18, 18}, b, vb);
            L.y += 26;
        };
        two("Nations", &showNations, "Characters", &showCharacters);
        two("Cities", &showCities, "Faiths", &showFaiths);
        two("Cultures", &showCultures, "Only the living", &onlyLiving);
        two("Politics", &showPolitics, "Personal ties", &showPersonal);
        two("Belongs to", &showMembership, nullptr, nullptr);
    }
    L.heading("FOCUS");
    if (hasFocus) {
        L.label(TextFormat("On: %s", entityName(doc, focus).c_str()));
        float d = (float)depth;
        L.label(TextFormat("Steps out: %d", depth)); L.slider(&d, 1, 4); depth = (int)std::round(d);
        if (L.button("Show everyone")) { hasFocus = false; selNode = -1; camX = camY = 0; }
    } else L.note("Showing the big picture. Click someone twice (or search) to centre the web on them and see their circle.");
    if (GuiTextBox(Rectangle{L.x, L.y, L.w, 28}, search, 63, searchEdit)) searchEdit = !searchEdit;
    if (!searchEdit && !search[0]) drawTextK(FontKind::Ui, "Find a person, nation or city...", {L.x + 8, L.y + 6}, 15, Color{110, 118, 135, 255});
    L.y += 34;
    if (search[0]) {
        std::string q = search; for (char& ch : q) ch = (char)tolower((unsigned char)ch);
        int shown = 0;
        auto offer = [&](EntityRef r) {
            if (shown >= 6) return;
            std::string nm = entityName(doc, r), lo = nm; for (char& ch : lo) ch = (char)tolower((unsigned char)ch);
            if (lo.find(q) == std::string::npos) return;
            if (GuiButton(Rectangle{L.x, L.y, L.w, 24}, nm.c_str())) { focusOn(r); search[0] = 0; selNode = -1; }
            L.y += 27; shown++;
        };
        for (int i = 0; i < (int)doc.nations.size(); i++) offer({EntKind::Nation, i});
        for (auto& c : doc.characters) offer({EntKind::Character, c.id});
        for (int i = 0; i < (int)doc.cities.size(); i++) offer({EntKind::City, i});
    }
    L.heading("DRAW LINKS");
    {
        drawTextK(FontKind::Ui, "New links are", {L.x, L.y + 5}, 15, theme::kMuted);
        float x0 = L.x + 100, w = L.w - 100;
        if (GuiButton(Rectangle{x0, L.y, 26, 26}, "<")) newLinkType = (newLinkType + (int)RelType::Count - 1) % (int)RelType::Count;
        if (GuiButton(Rectangle{x0 + w - 26, L.y, 26, 26}, ">")) newLinkType = (newLinkType + 1) % (int)RelType::Count;
        const char* t = relTypeName((RelType)newLinkType);
        Vector2 ts = measureTextK(FontKind::Ui, t, 15);
        drawTextK(FontKind::Ui, t, {x0 + w / 2 - ts.x / 2, L.y + 5}, 15, theme::kText);
        L.y += 34;
        L.note("Hold Shift and drag from one to another to link them. Drag to arrange (it stays put), right-drag to move, scroll to zoom.");
    }
    if (selNode >= 0 && selNode < (int)nodes.size()) {
        EntityRef r = nodes[selNode]->ref;
        L.heading("SELECTED");
        L.label(entityName(doc, r).c_str());
        int removeLink = -1;
        for (auto& e : edges) {
            if (e.a != selNode && e.b != selNode) continue;
            EntityRef other = nodes[e.a == selNode ? e.b : e.a]->ref;
            drawTextK(FontKind::Ui, e.label.c_str(), {L.x, L.y + 3}, 13, theme::kMuted);
            drawTextK(FontKind::UiBold, entityName(doc, other).c_str(), {L.x + 100, L.y + 2}, 14, e.col);
            if (e.linkIndex >= 0 && GuiButton(Rectangle{L.x + L.w - 24, L.y, 24, 22}, "x")) removeLink = e.linkIndex;
            L.y += 24;
        }
        if (removeLink >= 0 && removeLink < (int)doc.links.size()) { doc.links.erase(doc.links.begin() + removeLink); status = "Link removed"; }
        float bw = (L.w - 8) / 2;
        if (GuiButton(Rectangle{L.x, L.y, bw, 28}, "Open")) { openRef = r; openTab = 1; }
        if (GuiButton(Rectangle{L.x + bw + 8, L.y, bw, 28}, "Centre on")) focusOn(r);
        L.y += 36;
        if (nodes[selNode]->pinned && L.button("Let it move again")) nodes[selNode]->pinned = false;
    }
    L.y += 6;
    if (L.button("Tidy up (unpin all)")) for (auto& kv : layout) kv.second.pinned = false;
    (void)year;
}

// ---- family tree: grandparents down to grandchildren, spouses side by side ----
void WebView::familyFrame(Document& doc, int year, Rectangle area, bool mouseFree) {
    DrawRectangleRec(area, Color{16, 19, 27, 255});
    int root = focus.kind == EntKind::Character && hasFocus ? focus.id : -1;
    if (root < 0 || !doc.character(root)) {
        const char* t = "Pick someone to see their family: search on the left, or click a person twice in the web.";
        Vector2 ts = measureTextK(FontKind::Ui, t, 17);
        drawTextK(FontKind::Ui, t, {area.x + area.width / 2 - ts.x / 2, area.y + area.height / 2}, 17, theme::kMuted);
        return;
    }
    auto parents = [&](int id) { std::vector<int> v; for (auto& l : doc.links) if (l.type == RelType::Parent && l.b.kind == EntKind::Character && l.b.id == id && l.a.kind == EntKind::Character) v.push_back(l.a.id); return v; };
    auto children = [&](int id) { std::vector<int> v; for (auto& l : doc.links) if (l.type == RelType::Parent && l.a.kind == EntKind::Character && l.a.id == id && l.b.kind == EntKind::Character) v.push_back(l.b.id); return v; };
    auto spouses = [&](int id) { std::vector<int> v; for (auto& l : doc.links) if (l.type == RelType::Spouse && l.a.kind == EntKind::Character && l.b.kind == EntKind::Character) { if (l.a.id == id) v.push_back(l.b.id); else if (l.b.id == id) v.push_back(l.a.id); } return v; };
    // generations -2 .. +2
    std::map<int, std::vector<int>> gen;
    std::unordered_set<int> placed;
    auto put = [&](int g, int id) { if (placed.count(id) || !doc.character(id)) return; placed.insert(id); gen[g].push_back(id); };
    put(0, root);
    for (int s2 : spouses(root)) put(0, s2);
    for (int p : parents(root)) { for (int c : children(p)) put(0, c); }   // siblings
    for (int p : parents(root)) { put(-1, p); for (int s2 : spouses(p)) put(-1, s2); for (int gp : parents(p)) put(-2, gp); }
    for (int c : children(root)) { put(1, c); for (int s2 : spouses(c)) put(1, s2); for (int gc : children(c)) put(2, gc); }
    for (int s2 : spouses(root)) for (int c : children(s2)) put(1, c);
    std::unordered_map<int, Vector2> pos;
    float rowH = std::min(160.f, area.height / 5.4f), nodeW = 110;
    for (auto& g : gen) {
        float y = area.y + area.height / 2 + g.first * rowH - 40;
        int n = (int)g.second.size();
        float gap = std::min(nodeW + 30, (area.width - 40) / std::max(1, n));
        float x0 = area.x + area.width / 2 - gap * (n - 1) / 2;
        for (int k = 0; k < n; k++) pos[g.second[k]] = {x0 + gap * k, y};
    }
    // lines: parent to child, and spouses
    for (auto& l : doc.links) {
        if (l.a.kind != EntKind::Character || l.b.kind != EntKind::Character) continue;
        auto pa = pos.find(l.a.id), pb = pos.find(l.b.id);
        if (pa == pos.end() || pb == pos.end()) continue;
        if (l.type == RelType::Parent) {
            Vector2 a = {pa->second.x, pa->second.y + 46}, b = {pb->second.x, pb->second.y - 46};
            float mid = (a.y + b.y) / 2;
            DrawLineEx(a, {a.x, mid}, 2, Color{230, 190, 90, 220}); DrawLineEx({a.x, mid}, {b.x, mid}, 2, Color{230, 190, 90, 220}); DrawLineEx({b.x, mid}, b, 2, Color{230, 190, 90, 220});
        } else if (l.type == RelType::Spouse) {
            DrawLineEx({pa->second.x, pa->second.y - 3}, {pb->second.x, pb->second.y - 3}, 2, Color{240, 120, 170, 220});
            DrawLineEx({pa->second.x, pa->second.y + 3}, {pb->second.x, pb->second.y + 3}, 2, Color{240, 120, 170, 220});
        }
    }
    Vector2 m = GetMousePosition();
    for (auto& kv : pos) {
        const Character* c = doc.character(kv.first);
        Vector2 p = kv.second;
        Rectangle card{p.x - nodeW / 2, p.y - 46, nodeW, 92};
        bool hov = mouseFree && CheckCollisionPointRec(m, card);
        theme::card(card, kv.first == root ? theme::kAccentHi : hov ? theme::kText : Color{70, 80, 104, 255});
        drawPortrait({p.x - 22, p.y - 40, 44, 50}, *c, year);
        std::string nm = c->name;
        while (nm.size() > 3 && measureTextK(FontKind::UiBold, nm.c_str(), 12).x > nodeW - 8) nm = nm.substr(0, nm.size() - 4) + "..";
        Vector2 ts = measureTextK(FontKind::UiBold, nm.c_str(), 12);
        drawTextK(FontKind::UiBold, nm.c_str(), {p.x - ts.x / 2, p.y + 13}, 12, theme::kText);
        std::string yrs = c->died < 99999 ? TextFormat("%d - %d", c->born, c->died) : TextFormat("born %d", c->born);
        ts = measureTextK(FontKind::Ui, yrs.c_str(), 11);
        drawTextK(FontKind::Ui, yrs.c_str(), {p.x - ts.x / 2, p.y + 28}, 11, theme::kMuted);
        if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { focus = {EntKind::Character, kv.first}; hasFocus = true; }
    }
    drawTextK(FontKind::Serif, ("The family of " + doc.character(root)->name).c_str(), {area.x + 20, area.y + 14}, 24, theme::kAccentHi);
    drawTextK(FontKind::Ui, "Click anyone to see their family. Add parents, children and spouses in the Characters tab.", {area.x + 20, area.y + 44}, 14, theme::kMuted);
}
