#pragma once
// The Characters tab: list, create, edit and link up people.
#include "document.h"
#include "ui.h"
#include <string>

struct CharUI {
    int sel = -1;                 // character id
    int lastSel = -2;
    int listScroll = 0, listActive = -1;
    char search[64] = "";
    bool searchEdit = false;
    int roleFilter = 0;           // 0 all, else role+1
    char nameBuf[96] = "", titleBuf[64] = "", houseBuf[64] = "", occBuf[64] = "";
    bool nameEdit = false, titleEdit = false, houseEdit = false, occEdit = false;
    bool looksEdit = false, notesEdit = false; int looksScroll = 0, notesScroll = 0;
    bool bornEdit = false, diedEdit = false;
    // adding a link
    int linkType = 4, linkKind = 0, linkScroll = 0;
    char linkSearch[64] = ""; bool linkSearchEdit = false;
    bool pickingHome = false;     // next map click sets the home city
    bool openWeb = false;         // ask main to show the web focused on sel
    bool goToCity = false;
    bool typing() const { return searchEdit || nameEdit || titleEdit || houseEdit || occEdit || looksEdit || notesEdit || bornEdit || diedEdit || linkSearchEdit; }
};

// Draws the tab into the panel. year = the timeline's current year.
void charactersPanel(CharUI& ui, Layout& L, Document& doc, int year, uint32_t seed, std::string& status);
// Name of anything a link can point at
std::string entityName(const Document& doc, EntityRef e);
