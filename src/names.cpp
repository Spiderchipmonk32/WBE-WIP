#include "document.h"
#include <cctype>

// Syllable-based name generator. Each style has its own sounds and typical place endings,
// so names from one culture sound related to each other.

const char* nameStyleName(NameStyle s) {
    static const char* n[] = {"Nordic", "Latin", "Slavic", "Eastern", "Desert", "Elvish", "Celtic", "Mesoamerican"};
    int i = (int)s;
    return i >= 0 && i < (int)NameStyle::Count ? n[i] : "?";
}

namespace {
struct Sounds {
    std::vector<const char*> onset, vowel, coda, placeEnd, personEnd;
};

const Sounds& sounds(NameStyle s) {
    static const Sounds tbl[] = {
        // Nordic
        {{"b", "d", "f", "g", "h", "k", "l", "m", "n", "r", "s", "t", "v", "bj", "dr", "fr", "gr", "hr", "sk", "st", "sv", "tr", "th"},
         {"a", "e", "i", "o", "u", "ei", "au", "y", "aa"},
         {"", "", "n", "r", "rd", "ld", "nd", "k", "g", "st", "rn", "m", "ll"},
         {"heim", "vik", "dal", "by", "fjord", "borg", "sund", "holm", "stad", "nes", "havn"},
         {"ulf", "ar", "rid", "a", "ir", "geir", "hild", "mund"}},
        // Latin
        {{"c", "l", "m", "n", "p", "r", "s", "t", "v", "qu", "fl", "pr", "tr", "b", "d", "g"},
         {"a", "e", "i", "o", "u", "ae", "ia", "au"},
         {"", "", "", "n", "s", "r", "l", "m", "x"},
         {"ia", "um", "a", "ona", "ium", "ensis", "ara", "ica", "entum", "ova"},
         {"us", "a", "ius", "ia", "ianus", "ina", "o"}},
        // Slavic
        {{"b", "v", "z", "k", "l", "m", "n", "p", "r", "s", "t", "br", "dr", "kr", "pr", "sl", "st", "zv", "ch", "sh", "zh", "vl", "j"},
         {"a", "e", "i", "o", "u", "ya", "ie", "y"},
         {"", "", "v", "n", "k", "sk", "d", "r", "l", "sl"},
         {"grad", "ov", "sk", "ice", "ava", "ovo", "in", "evo", "ograd", "nik"},
         {"slav", "mir", "ana", "ek", "ova", "ko", "imir"}},
        // Eastern (Japanese-like CV syllables)
        {{"k", "s", "t", "n", "h", "m", "y", "r", "w", "g", "z", "sh", "ch", "ts"},
         {"a", "i", "u", "e", "o"},
         {"", "", "", "", "n"},
         {"shima", "yama", "kawa", "to", "no", "ra", "saki", "hara", "mura", "zaki"},
         {"ko", "ro", "ta", "mi", "ki", "ru", "ji"}},
        // Desert (Arabic/Persian-like)
        {{"q", "kh", "z", "s", "sh", "d", "r", "m", "n", "b", "h", "j", "f", "t", "'", "gh"},
         {"a", "i", "u", "aa", "ai", "ii", "uu"},
         {"", "", "r", "l", "m", "n", "d", "b", "z", "sh"},
         {"abad", "qand", "ar", "an", "ah", "ir", "istan", "shahr", "kur", "aj"},
         {"im", "ah", "an", "ir", "ul", "a", "id"}},
        // Elvish
        {{"l", "th", "s", "n", "m", "f", "v", "gl", "el", "ar", "c", "r", "d", "g", "ny"},
         {"a", "e", "i", "ae", "ie", "ea", "o", "ui", "ia"},
         {"", "", "l", "n", "r", "s", "th", "nd", "ss"},
         {"dor", "wen", "las", "rin", "thil", "ion", "iel", "mar", "ost", "lond", "ndor", "loth"},
         {"iel", "wen", "ion", "ir", "as", "ril", "dir", "eth"}},
        // Celtic / Old English
        {{"b", "c", "d", "g", "l", "m", "n", "p", "r", "s", "t", "w", "br", "gl", "cr", "dw", "fl", "gw", "ll", "tr"},
         {"a", "e", "i", "o", "u", "ae", "ea", "y", "ow"},
         {"", "", "n", "r", "l", "th", "ck", "nn", "dd", "rn"},
         {"more", "dun", "bryn", "wick", "ford", "ton", "shire", "caster", "minster", "bury", "mouth", "dale"},
         {"wyn", "en", "an", "ric", "wen", "ith", "ald"}},
        // Mesoamerican (Nahuatl/Maya-like)
        {{"t", "tl", "x", "ch", "k", "p", "m", "n", "c", "y", "z", "h", "qu", "tz"},
         {"a", "e", "i", "o", "u", "aa", "ui"},
         {"", "", "", "l", "n", "c", "x", "tl", "k"},
         {"tlan", "pec", "co", "tla", "titlan", "huacan", "lco", "chen", "mal"},
         {"tl", "ac", "in", "oc", "atl", "i"}},
    };
    int i = (int)s;
    if (i < 0 || i >= (int)NameStyle::Count) i = 1;
    return tbl[i];
}

uint32_t next(uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }
template <class T> const T& pick(const std::vector<T>& v, uint32_t& s) { return v[next(s) % v.size()]; }
}

std::string makeName(NameStyle style, uint32_t& seed, bool place) {
    const Sounds& S = sounds(style);
    std::string n;
    int syl = 1 + (int)(next(seed) % 2);
    for (int k = 0; k < syl; k++) {
        n += pick(S.onset, seed);
        n += pick(S.vowel, seed);
        if (k == syl - 1 || next(seed) % 3 == 0) n += pick(S.coda, seed);
    }
    const auto& ends = place ? S.placeEnd : S.personEnd;
    if (next(seed) % 100 < (place ? 65u : 75u)) {
        std::string e = pick(ends, seed);
        // avoid ugly double letters at the join
        if (!n.empty() && !e.empty() && n.back() == e.front()) n.pop_back();
        n += e;
    }
    while (n.size() < 4) { n += pick(S.vowel, seed); n += pick(S.onset, seed); n += pick(S.vowel, seed); }
    if (n.size() > 14) n = n.substr(0, 14);
    if (!n.empty()) n[0] = (char)std::toupper((unsigned char)n[0]);
    if (!n.empty() && n[0] == '\'') n.erase(0, 1);
    if (!n.empty()) n[0] = (char)std::toupper((unsigned char)n[0]);
    return n;
}
