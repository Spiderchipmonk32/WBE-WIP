#pragma once
// Constructed languages from a culture's naming style: the same English word always gives
// the same word in that culture's tongue, so you can translate phrases and keep a lexicon.
#include "document.h"
#include <string>
#include <vector>

inline std::string conWord(const Culture& c, const std::string& english) {
    std::string w;
    for (char ch : english) if (isalpha((unsigned char)ch)) w += (char)tolower((unsigned char)ch);
    if (w.empty()) return english;
    uint32_t h = 2166136261u;
    for (char ch : c.name + "|" + w) { h ^= (unsigned char)ch; h *= 16777619u; }
    std::string out = makeName(c.style, h, false);
    for (char& ch : out) ch = (char)tolower((unsigned char)ch);
    // little words stay little
    if (w.size() <= 3 && out.size() > 4) out = out.substr(0, 2 + (h >> 7) % 2);
    if (out.find_first_of("aeiouy") == std::string::npos) out += "aeiou"[h % 5];
    return out;
}

inline std::string translate(const Culture& c, const std::string& text) {
    std::string out, word;
    auto flush = [&]() {
        if (word.empty()) return;
        std::string t = conWord(c, word);
        if (isupper((unsigned char)word[0]) && !t.empty()) t[0] = (char)toupper((unsigned char)t[0]);
        out += t; word.clear();
    };
    for (char ch : text) { if (isalpha((unsigned char)ch) || ch == '\'') word += ch; else { flush(); out += ch; } }
    flush();
    return out;
}

inline const char* soundOf(NameStyle s) {
    switch (s) {
        case NameStyle::Nordic: return "Hard consonants and long vowels; words end in -r, -d and -heim sounds.";
        case NameStyle::Latin: return "Flowing vowels, clear syllables, endings in -us, -a and -ium.";
        case NameStyle::Slavic: return "Clusters like zh, sk and vl; soft endings in -ov, -ka and -grad.";
        case NameStyle::Eastern: return "Short open syllables with tones implied by doubled vowels.";
        case NameStyle::Desert: return "Guttural sounds and doubled consonants; words built around three-letter roots.";
        case NameStyle::Elvish: return "Liquid l and th sounds, many vowels, long musical words.";
        case NameStyle::Celtic: return "Rolling w, dd and ll sounds; words that look harder than they sound.";
        case NameStyle::Mesoamerican: return "tl and tz clusters, x sounds, words ending in -an, -co and -tl.";
        default: return "";
    }
}

inline const std::vector<const char*>& coreWords() {
    static const std::vector<const char*> w = {"hello", "goodbye", "friend", "enemy", "king", "god", "water", "fire", "sun", "moon",
                                               "home", "war", "peace", "gold", "sword", "love", "death", "sea", "mountain", "city",
                                               "yes", "no", "thank you", "honour"};
    return w;
}
