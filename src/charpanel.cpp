#include "charpanel.h"
#include "characters.h"
#include "politics.h"
#include "theme.h"
#include <cstring>
#include <algorithm>
#include <cstdio>

std::string entityName(const Document& doc, EntityRef e) {
    switch (e.kind) {
        case EntKind::Character: { const Character* c = doc.character(e.id); return c ? c->name : "(gone)"; }
        case EntKind::Nation: return e.id >= 0 && e.id < (int)doc.nations.size() ? doc.nations[e.id].name : "(gone)";
        case EntKind::City: return e.id >= 0 && e.id < (int)doc.cities.size() ? doc.cities[e.id].name : "(gone)";
        case EntKind::Religion: return e.id >= 0 && e.id < (int)doc.religions.size() ? doc.religions[e.id].name : "(gone)";
        case EntKind::Culture: return e.id >= 0 && e.id < (int)doc.cultures.size() ? doc.cultures[e.id].name : "(gone)";
    }
    return "?";
}

static std::string lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }
static std::string clean(std::string t) { for (char& ch : t) if (ch == ';' || ch == '\n') ch = ','; return t; }

// "< value >" stepper; returns true when changed
static bool stepper(Layout& L, const char* label, const std::string& value, int& v, int lo, int hi) {
    drawTextK(FontKind::Ui, label, {L.x, L.y + 5}, 15, theme::kMuted);
    float x0 = L.x + 86, w = L.w - 86;
    bool ch = false;
    if (GuiButton(Rectangle{x0, L.y, 26, 26}, "<")) { v = v <= lo ? hi : v - 1; ch = true; }
    if (GuiButton(Rectangle{x0 + w - 26, L.y, 26, 26}, ">")) { v = v >= hi ? lo : v + 1; ch = true; }
    std::string t = value;
    while (t.size() > 3 && measureTextK(FontKind::Ui, t.c_str(), 15).x > w - 64) t = t.substr(0, t.size() - 4) + "..";
    Vector2 ts = measureTextK(FontKind::Ui, t.c_str(), 15);
    drawTextK(FontKind::Ui, t.c_str(), {x0 + w / 2 - ts.x / 2, L.y + 5}, 15, theme::kText);
    L.y += 32;
    return ch;
}

static bool textRow(Layout& L, const char* label, char* buf, int size, bool& edit) {
    drawTextK(FontKind::Ui, label, {L.x, L.y + 6}, 15, theme::kMuted);
    bool done = false;
    if (GuiTextBox(Rectangle{L.x + 86, L.y, L.w - 86, 28}, buf, size, edit)) { edit = !edit; done = true; }
    L.y += 34;
    return done;
}

void charactersPanel(CharUI& ui, Layout& L, Document& doc, int year, uint32_t seed, std::string& status) {
    L.heading("CHARACTERS");
    float bw = (L.w - 8) / 2;
    if (GuiButton(Rectangle{L.x, L.y, bw, 32}, "New character")) {
        static uint32_t s = 12345u;
        s += (uint32_t)GetRandomValue(1, 1 << 30);
        int city = -1;
        if (!doc.cities.empty()) city = (int)(charRand(s) % doc.cities.size());
        Character c = makeCharacter(doc, Role::Commoner, city, -1, year, s);
        c.userMade = true;
        ui.sel = doc.addCharacter(c);
        status = "New character made - change anything you like";
    }
    if (GuiButton(Rectangle{L.x + bw + 8, L.y, bw, 32}, "Generate notable people")) {
        if (doc.nations.empty()) status = "Make nations first (Nations tab)";
        else {
            generateNotables(doc, year, seed + (uint32_t)GetRandomValue(0, 99999));
            status = TextFormat("%d people now live in the world", (int)doc.characters.size());
        }
    }
    L.y += 40;
    // search and filter
    if (GuiTextBox(Rectangle{L.x, L.y, L.w * 0.55f, 28}, ui.search, 63, ui.searchEdit)) ui.searchEdit = !ui.searchEdit;
    if (!ui.searchEdit && !ui.search[0]) drawTextK(FontKind::Ui, "Search...", {L.x + 8, L.y + 6}, 15, Color{110, 118, 135, 255});
    std::string roles = "All roles";
    for (int r = 0; r < (int)Role::Count; r++) { roles += ";"; roles += roleName((Role)r); }
    GuiComboBox(Rectangle{L.x + L.w * 0.55f + 6, L.y, L.w * 0.45f - 6, 28}, roles.c_str(), &ui.roleFilter);
    L.y += 34;
    std::vector<int> shown;
    std::string items;
    std::string q = lower(ui.search);
    for (auto& c : doc.characters) {
        if (ui.roleFilter > 0 && (int)c.role != ui.roleFilter - 1) continue;
        if (!q.empty() && lower(c.name).find(q) == std::string::npos && lower(c.title).find(q) == std::string::npos) continue;
        if (!items.empty()) items += ";";
        items += clean((c.followed ? "> " : "") + (c.title.empty() ? "" : c.title + " ") + c.name + (c.died <= year ? " (dead)" : ""));
        shown.push_back(c.id);
    }
    int active = -1;
    for (int k = 0; k < (int)shown.size(); k++) if (shown[k] == ui.sel) active = k;
    int prev = active;
    float lh = std::min(190.f, std::max(60.f, 26.f * shown.size() + 6));
    GuiListView(Rectangle{L.x, L.y, L.w, lh}, items.c_str(), &ui.listScroll, &active);
    L.y += lh + 8;
    if (active != prev && active >= 0 && active < (int)shown.size()) ui.sel = shown[active];
    if (doc.characters.empty()) L.note("Create your own characters, or generate rulers, nobles, merchants, criminals and townsfolk for every nation.");

    Character* c = doc.character(ui.sel);
    if (!c) { ui.sel = -1; return; }
    if (ui.lastSel != ui.sel) {
        ui.lastSel = ui.sel;
        snprintf(ui.nameBuf, sizeof ui.nameBuf, "%s", c->name.c_str());
        snprintf(ui.titleBuf, sizeof ui.titleBuf, "%s", c->title.c_str());
        snprintf(ui.houseBuf, sizeof ui.houseBuf, "%s", c->house.c_str());
        snprintf(ui.occBuf, sizeof ui.occBuf, "%s", c->occupation.c_str());
        ui.nameEdit = ui.titleEdit = ui.houseEdit = ui.occEdit = ui.looksEdit = ui.notesEdit = false;
    }
    // ---- header card: portrait, name, who they are ----
    L.y += 6;
    Rectangle pr{L.x, L.y, 96, 112};
    drawPortrait(pr, *c, year);
    DrawRectangleLinesEx(pr, 1, theme::kAccent);
    float tx = L.x + 108, tw = L.w - 108;
    {
        std::string nm = c->name;
        float fs = 22;
        while (fs > 14 && measureTextK(FontKind::Serif, nm.c_str(), fs).x > tw) fs -= 1;
        drawTextK(FontKind::Serif, nm.c_str(), {tx, L.y}, fs, theme::kAccentHi);
        std::string who = (c->title.empty() ? std::string(roleName(c->role)) : c->title) + (c->occupation.empty() ? "" : ", " + c->occupation);
        for (const std::string& ln : Layout::wrapLines(who.c_str(), (int)tw, 14)) { drawTextK(FontKind::Ui, ln.c_str(), {tx, L.y + 30}, 14, theme::kText); break; }
        std::string age = c->died <= year ? TextFormat("Died %d, aged %d", c->died, ageIn(*c, year)) : (year < c->born ? TextFormat("Born in %d", c->born) : TextFormat("Age %d (born %d)", ageIn(*c, year), c->born));
        drawTextK(FontKind::Ui, age.c_str(), {tx, L.y + 50}, 14, theme::kMuted);
        std::string home = c->city >= 0 && c->city < (int)doc.cities.size() ? "Lives in " + doc.cities[c->city].name : std::string("No fixed home");
        drawTextK(FontKind::Ui, home.c_str(), {tx, L.y + 70}, 14, theme::kMuted);
        std::string traits;
        for (int t : c->traits) { if (!traits.empty()) traits += ", "; traits += kTraits[t]; }
        if (!traits.empty()) drawTextK(FontKind::Ui, traits.c_str(), {tx, L.y + 90}, 14, theme::kTeal);
    }
    L.y += 124;
    {
        float b3 = (L.w - 12) / 3;
        if (GuiButton(Rectangle{L.x, L.y, b3, 28}, c->followed ? "Unfollow" : "Follow")) c->followed = !c->followed;
        if (GuiButton(Rectangle{L.x + b3 + 6, L.y, b3, 28}, "On the web")) ui.openWeb = true;
        if (GuiButton(Rectangle{L.x + 2 * (b3 + 6), L.y, b3, 28}, "Go to home")) ui.goToCity = true;
        L.y += 36;
    }
    // ---- identity ----
    L.heading("WHO THEY ARE");
    if (textRow(L, "Name", ui.nameBuf, 95, ui.nameEdit) || ui.nameEdit) c->name = ui.nameBuf;
    {
        float b2 = (L.w - 92) / 2;
        if (GuiButton(Rectangle{L.x + 86, L.y, b2, 24}, "Random name")) {
            uint32_t s = (uint32_t)GetRandomValue(1, 1 << 30);
            NameStyle st = c->culture >= 0 && c->culture < (int)doc.cultures.size() ? doc.cultures[c->culture].style : NameStyle::Latin;
            c->name = makeName(st, s, false) + " " + (c->house.empty() ? makeName(st, s, true) : c->house);
            snprintf(ui.nameBuf, sizeof ui.nameBuf, "%s", c->name.c_str());
        }
        if (GuiButton(Rectangle{L.x + 92 + b2, L.y, b2, 24}, "Random looks")) { c->look = (uint32_t)GetRandomValue(1, 1 << 30); c->appearance = describeLooks(*c, c->look); }
        L.y += 32;
    }
    if (textRow(L, "Title", ui.titleBuf, 63, ui.titleEdit) || ui.titleEdit) c->title = ui.titleBuf;
    if (textRow(L, "House", ui.houseBuf, 63, ui.houseEdit) || ui.houseEdit) c->house = ui.houseBuf;
    if (textRow(L, "Job", ui.occBuf, 63, ui.occEdit) || ui.occEdit) c->occupation = ui.occBuf;
    {
        int r = (int)c->role;
        if (stepper(L, "Role", roleName(c->role), r, 0, (int)Role::Count - 1)) c->role = (Role)r;
        int sx = c->sex;
        drawTextK(FontKind::Ui, "Sex", {L.x, L.y + 5}, 15, theme::kMuted);
        GuiToggleGroup(Rectangle{L.x + 86, L.y, (L.w - 86 - 8) / 3, 26}, "Man;Woman;Other", &sx);
        c->sex = sx;
        L.y += 32;
        drawTextK(FontKind::Ui, "Born", {L.x, L.y + 5}, 15, theme::kMuted);
        if (GuiValueBox(Rectangle{L.x + 86, L.y, 90, 26}, NULL, &c->born, -100000, 100000, ui.bornEdit)) ui.bornEdit = !ui.bornEdit;
        drawTextK(FontKind::Ui, "Died", {L.x + 186, L.y + 5}, 15, theme::kMuted);
        bool alive = c->died >= 99999;
        if (!alive) { if (GuiValueBox(Rectangle{L.x + 222, L.y, L.w - 222 - 64, 26}, NULL, &c->died, -100000, 99998, ui.diedEdit)) ui.diedEdit = !ui.diedEdit; }
        GuiCheckBox(Rectangle{L.x + L.w - 58, L.y + 4, 18, 18}, "alive", &alive);
        if (alive) c->died = 99999; else if (c->died >= 99999) c->died = year;
        L.y += 34;
    }
    {
        int cu = c->culture + 1, na = c->nation + 1, re = c->religion + 1;
        std::string cn = c->culture >= 0 && c->culture < (int)doc.cultures.size() ? doc.cultures[c->culture].name : "None";
        if (stepper(L, "Culture", cn, cu, 0, (int)doc.cultures.size())) c->culture = cu - 1;
        std::string nn = c->nation >= 0 && c->nation < (int)doc.nations.size() ? doc.nations[c->nation].name : "None";
        if (stepper(L, "Nation", nn, na, 0, (int)doc.nations.size())) c->nation = na - 1;
        std::string rn = c->religion >= 0 && c->religion < (int)doc.religions.size() ? doc.religions[c->religion].name : "None";
        if (stepper(L, "Faith", rn, re, 0, (int)doc.religions.size())) c->religion = re - 1;
        drawTextK(FontKind::Ui, "Home", {L.x, L.y + 5}, 15, theme::kMuted);
        std::string hn = c->city >= 0 && c->city < (int)doc.cities.size() ? doc.cities[c->city].name : "None";
        drawTextK(FontKind::Ui, hn.c_str(), {L.x + 86, L.y + 5}, 15, theme::kText);
        if (GuiButton(Rectangle{L.x + L.w - 120, L.y, 120, 26}, ui.pickingHome ? "Click a city..." : "Pick on map")) ui.pickingHome = !ui.pickingHome;
        L.y += 34;
    }
    // ---- personality ----
    L.heading("PERSONALITY");
    {
        float tw4 = (L.w - 9) / 4;
        for (int t = 0; t < kTraitCount; t++) {
            Rectangle r{L.x + (t % 4) * (tw4 + 3), L.y + (t / 4) * 28, tw4, 25};
            bool on = c->hasTrait(t);
            if (on) GuiSetState(STATE_PRESSED);
            if (GuiButton(r, kTraits[t])) {
                if (on) c->traits.erase(std::find(c->traits.begin(), c->traits.end(), t));
                else {
                    int o = traitOpposite(t);
                    if (o >= 0 && c->hasTrait(o)) c->traits.erase(std::find(c->traits.begin(), c->traits.end(), o));
                    if (c->traits.size() < 5) c->traits.push_back(t);
                }
            }
            GuiSetState(STATE_NORMAL);
        }
        L.y += ((kTraitCount + 3) / 4) * 28 + 6;
        L.note("Up to five traits; opposites cancel. In the living world traits steer choices: the ambitious scheme, the greedy hoard, the brave go to war.");
    }
    L.heading("LIFE");
    {
        float w = (float)c->wealth, rn = (float)c->renown, hp = (float)c->health;
        L.label(TextFormat("Wealth: %.0f coins", c->wealth));
        float lw = (float)std::log10(std::max(1.0, c->wealth)), lw0 = lw;
        L.slider(&lw, 0, 7);
        if (lw != lw0) c->wealth = std::pow(10.0, lw);
        L.label(TextFormat("Renown: %.0f", rn)); L.slider(&rn, 0, 100); c->renown = rn;
        L.label(TextFormat("Health: %.0f%%", hp * 100)); L.slider(&hp, 0, 1); c->health = hp;
        (void)w;
    }
    drawTextK(FontKind::UiBold, "Appearance", {L.x, L.y}, 14, theme::kTeal); L.y += 20;
    textArea(Rectangle{L.x, L.y, L.w, 70}, c->appearance, ui.looksEdit, ui.looksScroll); L.y += 78;
    drawTextK(FontKind::UiBold, "Notes and backstory", {L.x, L.y}, 14, theme::kTeal); L.y += 20;
    textArea(Rectangle{L.x, L.y, L.w, 120}, c->notes, ui.notesEdit, ui.notesScroll); L.y += 128;

    // ---- relationships ----
    L.heading("RELATIONSHIPS");
    int removeLink = -1;
    EntityRef me{EntKind::Character, c->id};
    for (int i = 0; i < (int)doc.links.size(); i++) {
        const Link& l = doc.links[i];
        bool a = l.a == me, b = l.b == me;
        if (!a && !b) continue;
        EntityRef other = a ? l.b : l.a;
        std::string t;
        if (l.type == RelType::Parent) t = a ? "Parent of" : "Child of";
        else if (l.type == RelType::Mentor) t = a ? "Mentor of" : "Student of";
        else if (l.type == RelType::Employs) t = a ? "Employs" : "Works for";
        else if (l.type == RelType::Serves) t = a ? "Serves" : "Served by";
        else if (l.type == RelType::Rules) t = a ? "Rules" : "Ruled by";
        else if (l.type == RelType::Owes) t = a ? "Owes" : "Is owed by";
        else t = relTypeName(l.type);
        static const Color kc[] = {{220, 200, 150, 255}, {230, 120, 120, 255}, {130, 200, 140, 255}, {140, 170, 230, 255}, {200, 150, 230, 255}};
        drawTextK(FontKind::Ui, t.c_str(), {L.x, L.y + 3}, 14, theme::kMuted);
        std::string on = entityName(doc, other);
        while (on.size() > 3 && measureTextK(FontKind::UiBold, on.c_str(), 15).x > L.w - 130) on = on.substr(0, on.size() - 4) + "..";
        drawTextK(FontKind::UiBold, on.c_str(), {L.x + 94, L.y + 2}, 15, kc[(int)other.kind % 5]);
        if (GuiButton(Rectangle{L.x + L.w - 26, L.y, 26, 22}, "x")) removeLink = i;
        L.y += 26;
    }
    if (removeLink >= 0) doc.links.erase(doc.links.begin() + removeLink);
    // add a link
    drawTextK(FontKind::UiBold, "Add a relationship", {L.x, L.y + 4}, 14, theme::kTeal); L.y += 24;
    {
        int lt = ui.linkType;
        std::string tn = relTypeName((RelType)lt);
        if (stepper(L, "Type", tn, lt, 0, (int)RelType::Count - 1)) ui.linkType = lt;
        GuiToggleGroup(Rectangle{L.x, L.y, (L.w - 16) / 5, 24}, "Person;Nation;City;Faith;Culture", &ui.linkKind);
        L.y += 30;
        if (GuiTextBox(Rectangle{L.x, L.y, L.w, 26}, ui.linkSearch, 63, ui.linkSearchEdit)) ui.linkSearchEdit = !ui.linkSearchEdit;
        if (!ui.linkSearchEdit && !ui.linkSearch[0]) drawTextK(FontKind::Ui, "Type a name, then click it below", {L.x + 8, L.y + 5}, 14, Color{110, 118, 135, 255});
        L.y += 30;
        std::string lq = lower(ui.linkSearch);
        std::vector<EntityRef> found;
        auto consider = [&](EntityRef e, const std::string& nm) {
            if (found.size() >= 6) return;
            if (e == me) return;
            if (!lq.empty() && lower(nm).find(lq) == std::string::npos) return;
            found.push_back(e);
        };
        EntKind k = (EntKind)ui.linkKind;
        if (k == EntKind::Character) for (auto& o : doc.characters) consider({k, o.id}, o.name);
        else if (k == EntKind::Nation) for (int i = 0; i < (int)doc.nations.size(); i++) consider({k, i}, doc.nations[i].name);
        else if (k == EntKind::City) for (int i = 0; i < (int)doc.cities.size(); i++) consider({k, i}, doc.cities[i].name);
        else if (k == EntKind::Religion) for (int i = 0; i < (int)doc.religions.size(); i++) consider({k, i}, doc.religions[i].name);
        else for (int i = 0; i < (int)doc.cultures.size(); i++) consider({k, i}, doc.cultures[i].name);
        for (auto& e : found) {
            if (GuiButton(Rectangle{L.x, L.y, L.w, 24}, (std::string("+ ") + entityName(doc, e)).c_str())) {
                doc.addLink(me, e, (RelType)ui.linkType, year, 3);
                if (ui.linkType == (int)RelType::Rules && e.kind == EntKind::Nation) c->nation = e.id;
                status = std::string("Linked: ") + c->name + " - " + relTypeName((RelType)ui.linkType) + " - " + entityName(doc, e);
            }
            L.y += 27;
        }
    }
    L.y += 6;
    if (!c->log.empty()) {
        L.heading("LIFE STORY");
        int shownLog = 0;
        for (int i = (int)c->log.size() - 1; i >= 0 && shownLog < 30; i--, shownLog++) {
            const LifeEvent& e = c->log[i];
            drawTextK(FontKind::UiBold, TextFormat("%d", e.year), {L.x, L.y}, 13, theme::kAccent);
            auto lines = Layout::wrapLines(e.text.c_str(), (int)L.w - 52, 14);
            for (auto& ln : lines) { drawTextK(FontKind::Ui, ln.c_str(), {L.x + 50, L.y}, 14, theme::kText); L.y += 18; }
            L.y += 3;
        }
    }
    L.y += 8;
    if (L.button("Delete this character")) { doc.removeCharacter(c->id); ui.sel = -1; }
}
