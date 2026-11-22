#include "codex.h"
#include "theme.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

std::vector<std::string> templateFields(LoreKind k) {
    switch (k) {
        case LoreKind::Place: return {"Type", "Region", "Climate", "Ruled by", "Population", "Notable for"};
        case LoreKind::Person: return {"Born", "Died", "Species", "Title", "Allegiance", "Home", "Occupation"};
        case LoreKind::Faction: case LoreKind::Organization: return {"Type", "Founded", "Leader", "Headquarters", "Members", "Goals", "Allies", "Enemies"};
        case LoreKind::Religion: return {"Deities", "Founded", "Holy sites", "Holy text", "Clergy", "Followers"};
        case LoreKind::Creature: case LoreKind::Species: return {"Habitat", "Lifespan", "Size", "Diet", "Intelligence", "Danger"};
        case LoreKind::Item: return {"Type", "Made of", "Made by", "Owner", "Value", "Powers"};
        case LoreKind::Event: case LoreKind::Conflict: return {"Date", "Place", "Sides", "Leaders", "Result", "Casualties"};
        case LoreKind::Settlement: return {"Population", "Nation", "Ruler", "Founded", "Trade", "Defences", "Districts"};
        case LoreKind::Language: return {"Spoken by", "Writing", "Related to", "Speakers", "Hello", "Goodbye"};
        case LoreKind::Magic: return {"Source", "Users", "Cost", "Limits", "Schools", "Forbidden"};
        case LoreKind::Myth: return {"Told by", "Age", "Heroes", "Moral", "Truth behind it"};
        case LoreKind::Tradition: return {"Kept by", "When", "Where", "Origin", "Customs"};
        case LoreKind::Material: return {"Found in", "Rarity", "Uses", "Value", "Properties"};
        case LoreKind::Technology: return {"Invented by", "Invented", "Used for", "Spread to", "Drawbacks"};
        case LoreKind::Quest: return {"Given by", "Reward", "Where", "Status", "Party"};
        case LoreKind::Session: return {"Date played", "Players", "Where", "Next time"};
        case LoreKind::Document: return {"Author", "Written", "Language", "Kept at", "Copies"};
        case LoreKind::Title: return {"Held by", "Granted by", "Rank", "Duties", "Privileges"};
        default: return {};
    }
}

std::string templateBody(LoreKind k) {
    switch (k) {
        case LoreKind::Person: return "## Who they are\n\n## Early life\n\n## Deeds\n\n## Personality\n\n## Relationships\n";
        case LoreKind::Place: case LoreKind::Settlement: return "## Overview\n\n## History\n\n## Geography\n\n## People and culture\n\n## Points of interest\n";
        case LoreKind::Faction: case LoreKind::Organization: return "## Purpose\n\n## History\n\n## Structure\n\n## Members\n\n## Methods\n";
        case LoreKind::Religion: return "## Beliefs\n\n## Gods\n\n## Worship\n\n## Holy days\n\n## History\n";
        case LoreKind::Creature: case LoreKind::Species: return "## Appearance\n\n## Behaviour\n\n## Habitat\n\n## Uses and dangers\n";
        case LoreKind::Item: return "## Description\n\n## History\n\n## Powers\n";
        case LoreKind::Event: case LoreKind::Conflict: return "## Background\n\n## What happened\n\n## Aftermath\n";
        case LoreKind::Language: return "## Sound\n\n## Grammar\n\n## Writing\n\n## Common words\n";
        case LoreKind::Magic: return "## How it works\n\n## Who can use it\n\n## Costs and limits\n\n## History\n";
        case LoreKind::Myth: return "## The tale\n\n## Meaning\n\n## Variations\n";
        case LoreKind::Quest: return "## The hook\n\n## Steps\n\n## Twists\n\n## Outcome\n";
        case LoreKind::Session: return "## Recap\n\n## Loot\n\n## Loose threads\n";
        default: return "## Overview\n\n";
    }
}

static std::string lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }
static std::string clean(std::string t) { for (char& ch : t) if (ch == ';' || ch == '\n') ch = ','; return t; }

static std::vector<std::string> splitTags(const std::string& t) {
    std::vector<std::string> out; std::string cur;
    for (char c : t + ",") {
        if (c == ',') {
            size_t a = cur.find_first_not_of(' '), b = cur.find_last_not_of(' ');
            if (a != std::string::npos) out.push_back(cur.substr(a, b - a + 1));
            cur.clear();
        } else cur += c;
    }
    return out;
}

void codexPanel(CodexUI& ui, Layout& L, Document& doc, int& sel) {
    L.heading("CODEX");
    // tag filter from every tag in use
    std::vector<std::string> allTags;
    for (auto& a : doc.articles) for (auto& t : splitTags(a.tags)) if (std::find(allTags.begin(), allTags.end(), t) == allTags.end()) allTags.push_back(t);
    if (GuiTextBox(Rectangle{L.x, L.y, L.w * 0.55f, 28}, ui.searchBuf, 63, ui.searchEdit)) ui.searchEdit = !ui.searchEdit;
    if (!ui.searchEdit && !ui.searchBuf[0]) drawTextK(FontKind::Ui, "Search...", {L.x + 8, L.y + 6}, 15, Color{110, 118, 135, 255});
    std::string tagItems = "All tags";
    for (auto& t : allTags) tagItems += ";" + clean(t);
    if (ui.tagFilter > (int)allTags.size()) ui.tagFilter = 0;
    GuiComboBox(Rectangle{L.x + L.w * 0.55f + 6, L.y, L.w * 0.45f - 6, 28}, tagItems.c_str(), &ui.tagFilter);
    L.y += 34;
    std::string q = lower(ui.searchBuf);
    std::vector<int> shown; std::string items;
    for (int i = 0; i < (int)doc.articles.size(); i++) {
        const Article& a = doc.articles[i];
        if (!q.empty() && lower(a.title + " " + a.body + " " + a.tags).find(q) == std::string::npos) continue;
        if (ui.tagFilter > 0) { auto tg = splitTags(a.tags); if (std::find(tg.begin(), tg.end(), allTags[ui.tagFilter - 1]) == tg.end()) continue; }
        shown.push_back(i);
        if (!items.empty()) items += ";";
        items += clean(a.title.empty() ? std::string("(untitled)") : a.title) + "  [" + loreKindName(a.kind) + "]";
    }
    int row = -1;
    for (size_t k = 0; k < shown.size(); k++) if (shown[k] == sel) row = (int)k;
    int prev = row;
    float lh = std::min(180.f, std::max(60.f, 26.f * shown.size() + 6));
    GuiListView(Rectangle{L.x, L.y, L.w, lh}, items.c_str(), &ui.listScroll, &row);
    L.y += lh + 8;
    if (row != prev) sel = row >= 0 && row < (int)shown.size() ? shown[row] : -1;
    // new article from a template
    {
        float x0 = L.x, w = L.w - 120;
        if (GuiButton(Rectangle{x0, L.y, 26, 28}, "<")) ui.newKind = (ui.newKind + (int)LoreKind::Count - 1) % (int)LoreKind::Count;
        if (GuiButton(Rectangle{x0 + w - 26, L.y, 26, 28}, ">")) ui.newKind = (ui.newKind + 1) % (int)LoreKind::Count;
        const char* kn = loreKindName((LoreKind)ui.newKind);
        Vector2 ts = measureTextK(FontKind::Ui, kn, 15);
        drawTextK(FontKind::Ui, kn, {x0 + w / 2 - ts.x / 2, L.y + 6}, 15, theme::kText);
        if (GuiButton(Rectangle{L.x + L.w - 112, L.y, 112, 28}, "New article")) {
            Article a; a.kind = (LoreKind)ui.newKind;
            a.title = std::string("New ") + loreKindName(a.kind);
            a.body = templateBody(a.kind);
            for (auto& f : templateFields(a.kind)) a.fields.push_back({f, ""});
            doc.articles.push_back(a);
            sel = (int)doc.articles.size() - 1;
            ui.pageOpen = true;
        }
        L.y += 36;
    }
    if (sel >= (int)doc.articles.size()) sel = -1;
    if (sel != ui.lastSel) {
        ui.lastSel = sel; ui.editField = -1; ui.titleEdit = ui.tagsEdit = ui.bodyEdit = ui.secretEdit = false; ui.bodyScroll = ui.secretScroll = 0;
        if (sel >= 0) { snprintf(ui.titleBuf, sizeof ui.titleBuf, "%s", doc.articles[sel].title.c_str()); snprintf(ui.tagsBuf, sizeof ui.tagsBuf, "%s", doc.articles[sel].tags.c_str()); }
    }
    if (sel < 0) {
        L.note("Your world's wiki. Pick a template (person, settlement, organization, species, language, magic system, myth, quest, session notes...) and press New article. Link articles with [[Title]].");
        return;
    }
    Article& a = doc.articles[sel];
    L.heading("ARTICLE");
    drawTextK(FontKind::Ui, "Title", {L.x, L.y + 6}, 15, theme::kMuted);
    if (GuiTextBox(Rectangle{L.x + 60, L.y, L.w - 60, 28}, ui.titleBuf, 127, ui.titleEdit)) ui.titleEdit = !ui.titleEdit;
    a.title = ui.titleBuf; L.y += 34;
    {
        int k = (int)a.kind;
        drawTextK(FontKind::Ui, "Kind", {L.x, L.y + 6}, 15, theme::kMuted);
        float x0 = L.x + 60, w = L.w - 60;
        bool ch = false;
        if (GuiButton(Rectangle{x0, L.y, 26, 28}, "<")) { k = (k + (int)LoreKind::Count - 1) % (int)LoreKind::Count; ch = true; }
        if (GuiButton(Rectangle{x0 + w - 26, L.y, 26, 28}, ">")) { k = (k + 1) % (int)LoreKind::Count; ch = true; }
        const char* kn = loreKindName((LoreKind)k);
        Vector2 ts = measureTextK(FontKind::Ui, kn, 15);
        drawTextK(FontKind::Ui, kn, {x0 + w / 2 - ts.x / 2, L.y + 6}, 15, theme::kText);
        if (ch) {
            a.kind = (LoreKind)k;
            // add the template's fields that are missing
            for (auto& f : templateFields(a.kind)) {
                bool have = false; for (auto& e : a.fields) if (e.first == f) have = true;
                if (!have) a.fields.push_back({f, ""});
            }
        }
        L.y += 34;
    }
    drawTextK(FontKind::Ui, "Tags", {L.x, L.y + 6}, 15, theme::kMuted);
    if (GuiTextBox(Rectangle{L.x + 60, L.y, L.w - 60, 28}, ui.tagsBuf, 159, ui.tagsEdit)) ui.tagsEdit = !ui.tagsEdit;
    a.tags = ui.tagsBuf; L.y += 34;
    float bw = (L.w - 8) / 2;
    if (GuiButton(Rectangle{L.x, L.y, bw, 28}, a.pinned ? "Move map pin" : "Pin to map")) { ui.wantPin = true; ui.pageOpen = false; }
    if (GuiButton(Rectangle{L.x + bw + 8, L.y, bw, 28}, ui.pageOpen ? "Show the map" : "Show the page")) ui.pageOpen = !ui.pageOpen;
    L.y += 36;
    L.check("Show secrets", &ui.showSecrets);
    L.heading("INFOBOX");
    int remove = -1;
    for (int i = 0; i < (int)a.fields.size(); i++) {
        auto& f = a.fields[i];
        drawTextK(FontKind::Ui, f.first.c_str(), {L.x, L.y + 6}, 14, theme::kMuted);
        Rectangle vr{L.x + 110, L.y, L.w - 110 - 28, 26};
        if (ui.editField == i) {
            if (GuiTextBox(vr, ui.fieldBuf, 255, true)) { f.second = ui.fieldBuf; ui.editField = -1; }
            else f.second = ui.fieldBuf;
        } else if (GuiButton(vr, f.second.empty() ? "..." : f.second.c_str())) { ui.editField = i; snprintf(ui.fieldBuf, sizeof ui.fieldBuf, "%s", f.second.c_str()); }
        if (GuiButton(Rectangle{L.x + L.w - 24, L.y, 24, 26}, "x")) remove = i;
        L.y += 30;
    }
    if (remove >= 0) { a.fields.erase(a.fields.begin() + remove); ui.editField = -1; }
    if (GuiTextBox(Rectangle{L.x, L.y, L.w - 96, 26}, ui.newFieldBuf, 63, ui.newFieldEdit)) ui.newFieldEdit = !ui.newFieldEdit;
    if (!ui.newFieldEdit && !ui.newFieldBuf[0]) drawTextK(FontKind::Ui, "New field name", {L.x + 8, L.y + 5}, 14, Color{110, 118, 135, 255});
    if (GuiButton(Rectangle{L.x + L.w - 88, L.y, 88, 26}, "Add field") && ui.newFieldBuf[0]) { a.fields.push_back({ui.newFieldBuf, ""}); ui.newFieldBuf[0] = 0; }
    L.y += 34;
    L.note("Write the article itself on the page beside this panel. Lines starting with ## become headings.");
    if (L.button("Delete this article")) { doc.articles.erase(doc.articles.begin() + sel); sel = -1; }
}

void codexPage(CodexUI& ui, Document& doc, int& sel, Rectangle area) {
    if (sel < 0 || sel >= (int)doc.articles.size()) return;
    Article& a = doc.articles[sel];
    DrawRectangleRec(area, Color{16, 19, 27, 255});
    float pw = std::min(area.width - 40, 1100.f);
    Rectangle page{area.x + (area.width - pw) / 2, area.y + 20, pw, area.height - 40};
    DrawRectangleRounded(page, 0.01f, 6, Color{236, 228, 208, 255});   // parchment
    DrawRectangleRoundedLinesEx(page, 0.01f, 6, 2, Color{150, 120, 80, 255});
    const Color ink{50, 38, 28, 255}, faded{120, 100, 80, 255};
    float x = page.x + 36, y = page.y + 26;
    drawTextK(FontKind::Serif, a.title.c_str(), {x, y}, 38, ink);
    y += 48;
    std::string sub = loreKindName(a.kind);
    if (!a.tags.empty()) sub += "   \xE2\x80\xA2   " + a.tags;
    drawTextK(FontKind::SerifItalic, sub.c_str(), {x, y}, 18, faded);
    y += 30;
    DrawLineEx({x, y}, {page.x + page.width - 36, y}, 1.5f, Color{170, 140, 100, 255});
    y += 14;
    // infobox on the right
    float boxW = 300;
    Rectangle box{page.x + page.width - 36 - boxW, y, boxW, 0};
    if (!a.fields.empty()) {
        float bh = 44 + a.fields.size() * 26.f;
        box.height = bh;
        DrawRectangleRec(box, Color{224, 212, 186, 255});
        DrawRectangleLinesEx(box, 1, Color{150, 120, 80, 255});
        drawTextK(FontKind::Serif, a.title.c_str(), {box.x + 12, box.y + 8}, 20, ink);
        float fy = box.y + 38;
        for (auto& f : a.fields) {
            drawTextK(FontKind::UiBold, f.first.c_str(), {box.x + 12, fy}, 14, ink);
            std::string v = f.second.empty() ? "-" : f.second;
            while (v.size() > 3 && measureTextK(FontKind::Ui, v.c_str(), 14).x > boxW - 130) v = v.substr(0, v.size() - 4) + "..";
            drawTextK(FontKind::Ui, v.c_str(), {box.x + 118, fy}, 14, ink);
            fy += 26;
        }
    }
    // links out and links in
    std::vector<std::string> out;
    for (size_t p0 = 0; (p0 = a.body.find("[[", p0)) != std::string::npos;) {
        size_t p1 = a.body.find("]]", p0 + 2);
        if (p1 == std::string::npos) break;
        out.push_back(a.body.substr(p0 + 2, p1 - p0 - 2)); p0 = p1 + 2;
    }
    std::vector<int> in;
    for (int i = 0; i < (int)doc.articles.size(); i++)
        if (i != sel && !a.title.empty() && doc.articles[i].body.find("[[" + a.title + "]]") != std::string::npos) in.push_back(i);
    float sideY = box.y + box.height + 16;
    auto linkList = [&](const char* head, const std::vector<std::string>& names, std::vector<int> targets) {
        if (names.empty()) return;
        drawTextK(FontKind::UiBold, head, {box.x, sideY}, 13, faded); sideY += 20;
        for (size_t k = 0; k < names.size() && k < 10; k++) {
            Rectangle r{box.x, sideY, boxW, 22};
            bool hov = CheckCollisionPointRec(GetMousePosition(), r);
            drawTextK(FontKind::Ui, names[k].c_str(), {box.x + 8, sideY + 2}, 15, hov ? Color{150, 60, 30, 255} : Color{40, 80, 140, 255});
            if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                int t = targets.empty() ? -1 : targets[k];
                if (t < 0) {
                    for (int i = 0; i < (int)doc.articles.size(); i++) if (doc.articles[i].title == names[k]) t = i;
                    if (t < 0) { Article na; na.title = names[k]; na.body = templateBody(LoreKind::Other); doc.articles.push_back(na); t = (int)doc.articles.size() - 1; }
                }
                sel = t;
            }
            sideY += 24;
        }
        sideY += 8;
    };
    linkList("LINKS TO", out, {});
    std::vector<std::string> inNames; for (int i : in) inNames.push_back(doc.articles[i].title);
    linkList("MENTIONED IN", inNames, in);
    // the body: an editor while typing, a formatted page otherwise
    float bodyW = box.x - x - 24;
    float secretH = ui.showSecrets ? 130.f : 0.f;
    Rectangle bodyR{x, y, bodyW, page.y + page.height - y - 24 - (secretH > 0 ? secretH + 20 : 0)};
    if (ui.bodyEdit) {
        textArea(bodyR, a.body, ui.bodyEdit, ui.bodyScroll);
    } else {
        // formatted: ## headings, paragraphs, [[links]] shown plainly
        BeginScissorMode((int)bodyR.x, (int)bodyR.y, (int)bodyR.width, (int)bodyR.height);
        float by = bodyR.y - ui.bodyScroll * 20;
        size_t start = 0;
        while (start <= a.body.size()) {
            size_t nl = a.body.find('\n', start);
            std::string ln = a.body.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            std::string shownLn;
            for (size_t i = 0; i < ln.size(); i++) { if (ln.compare(i, 2, "[[") == 0 || ln.compare(i, 2, "]]") == 0) { i++; continue; } shownLn += ln[i]; }
            if (ln.rfind("## ", 0) == 0) {
                by += 8;
                drawTextK(FontKind::Serif, shownLn.substr(3).c_str(), {bodyR.x, by}, 24, ink);
                by += 30;
                DrawLineEx({bodyR.x, by}, {bodyR.x + bodyR.width, by}, 1, Color{190, 165, 125, 255});
                by += 6;
            } else if (shownLn.empty()) by += 10;
            else for (auto& w : Layout::wrapLines(shownLn.c_str(), (int)bodyR.width, 17)) { drawTextK(FontKind::Ui, w.c_str(), {bodyR.x, by}, 17, ink); by += 23; }
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        EndScissorMode();
        if (CheckCollisionPointRec(GetMousePosition(), bodyR)) {
            ui.bodyScroll = std::max(0, ui.bodyScroll - (int)(GetMouseWheelMove() * 3));
            drawTextK(FontKind::SerifItalic, "click to write", {bodyR.x + bodyR.width - 100, bodyR.y + bodyR.height - 20}, 14, faded);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) ui.bodyEdit = true;
        }
        if (a.body.empty()) drawTextK(FontKind::SerifItalic, "Click here to start writing...", {bodyR.x, bodyR.y}, 18, faded);
    }
    if (secretH > 0) {
        Rectangle sr{x, bodyR.y + bodyR.height + 18, bodyW, secretH};
        drawTextK(FontKind::UiBold, "SECRET - only you can see this", {sr.x, sr.y - 18}, 13, Color{170, 50, 40, 255});
        textArea(sr, a.secret, ui.secretEdit, ui.secretScroll);
    }
}
