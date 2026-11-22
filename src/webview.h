#pragma once
// The relationship web: nations, rulers, characters, cities and faiths as a moving graph.
#include "document.h"
#include "ui.h"
#include <unordered_map>
#include <vector>
#include <string>

struct WebNode {
    EntityRef ref;
    float x = 0, y = 0, vx = 0, vy = 0;
    bool pinned = false;
};

struct WebEdge {
    int a, b;               // node indices
    Color col;
    std::string label;
    bool directed = false, dashed = false;
    float width = 2;
    int linkIndex = -1;     // into doc.links (-1 = nation relation or implied membership)
};

struct WebView {
    // what to show
    bool showNations = true, showCharacters = true, showCities = false, showFaiths = false, showCultures = false;
    bool showPolitics = true, showPersonal = true, showMembership = true, onlyLiving = true;
    int depth = 2;                  // hops from the focus
    EntityRef focus{EntKind::Character, -1};
    bool hasFocus = false;
    // view
    float camX = 0, camY = 0, zoom = 1;
    int dragNode = -1, hoverNode = -1, hoverEdge = -1, selNode = -1;
    bool linking = false; int linkFrom = -1;
    int newLinkType = 4;
    char search[64] = ""; bool searchEdit = false; int searchScroll = 0;
    std::unordered_map<uint64_t, WebNode> layout;   // positions survive rebuilding
    std::vector<WebNode*> nodes;
    std::vector<WebEdge> edges;
    // requests to the app
    int openTab = -1; EntityRef openRef;

    static uint64_t key(EntityRef r) { return ((uint64_t)r.kind << 32) | (uint32_t)r.id; }
    void focusOn(EntityRef r) { focus = r; hasFocus = true; camX = camY = 0; auto it = layout.find(key(r)); if (it != layout.end()) { camX = it->second.x; camY = it->second.y; } }
    // rebuild the graph from the document, settle it a little, handle the mouse and draw it
    void frame(Document& doc, int year, Rectangle area, float dt, bool mouseFree);
    bool familyMode = false;        // show a family tree of the focused person instead of the web
    void familyFrame(Document& doc, int year, Rectangle area, bool mouseFree);
    void panel(Layout& L, Document& doc, int year, std::string& status);
    bool typing() const { return searchEdit; }
private:
    void build(const Document& doc, int year);
};
