#pragma once
// Characters: generating people for the world (rulers and their families, nobles, merchants,
// criminals, priests, ordinary folk), and drawing their portraits.
#include "document.h"
#include "raylib.h"

uint32_t charRand(uint32_t& s);
double charRand01(uint32_t& s);

// A new person of a role living in a city (or -1), born so they are an adult in `year`
Character makeCharacter(const Document& doc, Role role, int city, int nation, int year, uint32_t& seed);
// A child of two parents (or one), born in `year`, with Parent links
int makeChild(Document& doc, int parentA, int parentB, int year, uint32_t& seed);
// Fill the world with notable people: a ruler and family per nation, nobles, and townsfolk of
// every kind in the larger cities, with family, friends, rivals and employers linked up.
void generateNotables(Document& doc, int year, uint32_t seed, int perCity = 3);
// Random personality: a few traits, never two opposites
std::vector<int> randomTraits(uint32_t& s, int count);
int traitOpposite(int t);
// A short description of how someone looks
std::string describeLooks(const Character& c, uint32_t seed);
// Age in a year
inline int ageIn(const Character& c, int year) { return std::max(0, std::min(year, c.died) - c.born); }

// Portrait: a simple painted bust from the character's look seed, culture and age
void drawPortrait(Rectangle r, const Character& c, int year);
