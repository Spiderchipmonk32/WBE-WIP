#pragma once
// The codex: the world's wiki. Article templates with infoboxes, tags, secrets only the
// author sees, [[links]] and "mentioned in" backlinks, shown as a page over the map.
#include "document.h"
#include "ui.h"
#include <string>
#include <vector>

std::vector<std::string> templateFields(LoreKind k);
std::string templateBody(LoreKind k);

struct CodexUI {
    int newKind = 9;                // template for new articles
    bool showSecrets = true, pageOpen = true;
    int editField = -1; char fieldBuf[256] = "";
    char newFieldBuf[64] = ""; bool newFieldEdit = false;
    char tagsBuf[160] = ""; bool tagsEdit = false;
    char titleBuf[128] = ""; bool titleEdit = false;
    char searchBuf[64] = ""; bool searchEdit = false;
    int listScroll = 0, lastSel = -2, tagFilter = 0;
    bool bodyEdit = false, secretEdit = false;
    int bodyScroll = 0, secretScroll = 0;
    bool wantPin = false;
    bool typing() const { return editField >= 0 || newFieldEdit || tagsEdit || titleEdit || searchEdit || bodyEdit || secretEdit; }
};

// Side panel: find, create and set up articles. sel is the selected article (or -1).
void codexPanel(CodexUI& ui, Layout& L, Document& doc, int& sel);
// The article page drawn over the map area
void codexPage(CodexUI& ui, Document& doc, int& sel, Rectangle area);
