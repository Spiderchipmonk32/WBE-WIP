#include "characters.h"
#include "politics.h"
#include <cmath>
#include <algorithm>

uint32_t charRand(uint32_t& s) { s = s * 1664525u + 1013904223u; return s >> 8; }
double charRand01(uint32_t& s) { return charRand(s) / 16777216.0; }

int traitOpposite(int t) { return t < 20 ? (t ^ 1) : -1; }

std::vector<int> randomTraits(uint32_t& s, int count) {
    std::vector<int> out;
    for (int tries = 0; tries < 40 && (int)out.size() < count; tries++) {
        int t = (int)(charRand(s) % (uint32_t)kTraitCount);
        if (std::find(out.begin(), out.end(), t) != out.end()) continue;
        int o = traitOpposite(t);
        if (o >= 0 && std::find(out.begin(), out.end(), o) != out.end()) continue;
        out.push_back(t);
    }
    return out;
}

static NameStyle styleOf(const Document& doc, int culture) {
    return culture >= 0 && culture < (int)doc.cultures.size() ? doc.cultures[culture].style : NameStyle::Latin;
}

std::string describeLooks(const Character& c, uint32_t seed) {
    static const char* build[] = {"slight", "wiry", "broad-shouldered", "stocky", "tall", "short", "lean", "heavyset"};
    static const char* hair[] = {"black", "dark brown", "chestnut", "auburn", "fair", "red", "sandy", "ash-blond"};
    static const char* eyes[] = {"brown", "grey", "green", "blue", "hazel", "dark"};
    static const char* mark[] = {"a scar across one cheek", "a crooked nose", "a ready smile", "tired eyes", "ink-stained fingers",
                                 "a booming laugh", "a limp", "calloused hands", "a gold tooth", "freckles", "a sharp gaze", "a soft voice"};
    uint32_t s = seed * 2654435761u + 7u;
    std::string t = std::string("A ") + build[charRand(s) % 8] + " " + (c.sex == 1 ? "woman" : c.sex == 0 ? "man" : "person") + " with " +
                    hair[charRand(s) % 8] + " hair, " + eyes[charRand(s) % 6] + " eyes and " + mark[charRand(s) % 12] + ".";
    return t;
}

Character makeCharacter(const Document& doc, Role role, int city, int nation, int year, uint32_t& s) {
    Character c;
    c.userMade = false;
    c.role = role;
    c.sex = charRand01(s) < 0.5 ? 0 : 1;
    if (charRand01(s) < 0.02) c.sex = 2;
    int age = 18 + (int)(charRand01(s) * 42);
    c.born = year - age;
    c.city = city;
    if (city >= 0 && city < (int)doc.cities.size()) {
        c.nation = doc.cities[city].nation;
        c.culture = doc.cities[city].culture;
    } else c.nation = nation;
    if (c.nation >= 0 && c.nation < (int)doc.nations.size() && c.culture < 0) c.culture = doc.nations[c.nation].culture;
    if (!doc.religions.empty()) c.religion = (int)(charRand(s) % doc.religions.size());
    NameStyle st = styleOf(doc, c.culture);
    std::string first = makeName(st, s, false);
    c.house = makeName(st, s, true);
    c.name = first + " " + c.house;
    c.traits = randomTraits(s, 2 + (int)(charRand(s) % 2));
    c.look = charRand(s) | 1u;
    c.appearance = describeLooks(c, c.look);
    static const char* occ[] = {"labourer", "ruler", "lord", "trader", "thief", "soldier", "priest", "scholar", "smith", "farmer", "sellsword", "mage"};
    static const char* occAlt[][3] = {{"labourer", "fisher", "innkeeper"}, {"ruler", "ruler", "ruler"}, {"lord", "landholder", "courtier"},
                                     {"spice trader", "cloth merchant", "caravan master"}, {"pickpocket", "smuggler", "gang boss"},
                                     {"guard", "soldier", "captain"}, {"priest", "temple keeper", "pilgrim"}, {"scribe", "scholar", "alchemist"},
                                     {"smith", "weaver", "carpenter"}, {"farmer", "shepherd", "miller"}, {"sellsword", "explorer", "monster hunter"},
                                     {"hedge mage", "court wizard", "witch"}};
    c.occupation = occAlt[(int)role][charRand(s) % 3];
    (void)occ;
    static const double wealth[] = {40, 50000, 8000, 2500, 300, 120, 400, 600, 300, 150, 200, 900};
    c.wealth = wealth[(int)role] * (0.4 + charRand01(s) * 1.4);
    c.renown = role == Role::Ruler ? 70 : role == Role::Noble ? 35 : 3 + charRand01(s) * 12;
    c.health = 0.7 + charRand01(s) * 0.3;
    if (c.hasTrait(22)) c.health *= 0.6;   // sickly
    if (role == Role::Ruler && c.nation >= 0 && c.nation < (int)doc.nations.size()) c.title = rulerTitle(doc.nations[c.nation].government, c.sex == 1);
    else if (role == Role::Noble) c.title = c.sex == 1 ? "Lady" : "Lord";
    return c;
}

int makeChild(Document& doc, int parentA, int parentB, int year, uint32_t& s) {
    const Character* a = doc.character(parentA);
    if (!a) return -1;
    Character c = makeCharacter(doc, a->role == Role::Ruler ? Role::Noble : a->role, a->city, a->nation, year, s);
    c.born = year;
    c.house = a->house;
    std::string first = makeName(styleOf(doc, a->culture), s, false);
    c.name = first + " " + c.house;
    c.culture = a->culture; c.religion = a->religion; c.nation = a->nation; c.city = a->city;
    c.wealth = 0; c.renown = a->renown * 0.3;
    c.occupation = "child";
    int id = doc.addCharacter(c);
    doc.addLink({EntKind::Character, parentA}, {EntKind::Character, id}, RelType::Parent, year, 5);
    if (parentB >= 0) doc.addLink({EntKind::Character, parentB}, {EntKind::Character, id}, RelType::Parent, year, 5);
    return id;
}

void generateNotables(Document& doc, int year, uint32_t seed, int perCity) {
    uint32_t s = seed * 747796405u + 2891336453u;
    // forget earlier generated people (keep the user's own)
    std::vector<int> gone;
    for (auto& c : doc.characters) if (!c.userMade) gone.push_back(c.id);
    for (int id : gone) doc.removeCharacter(id);

    for (int n = 0; n < (int)doc.nations.size(); n++) {
        if (doc.nations[n].fell <= year) continue;
        int capital = -1;
        std::vector<int> cities;
        for (int i = 0; i < (int)doc.cities.size(); i++)
            if (doc.cities[i].nation == n) { cities.push_back(i); if (doc.cities[i].capital) capital = i; }
        if (capital < 0 && !cities.empty()) capital = cities[0];
        // the ruler, their spouse and children
        Character ruler = makeCharacter(doc, Role::Ruler, capital, n, year, s);
        ruler.born = year - (30 + (int)(charRand01(s) * 30));
        if (const Ruler* r = doc.rulerIn(n, year)) {
            // use the name from the ruler list, without the title
            std::string nm = r->name;
            size_t sp = nm.find(' ');
            if (sp != std::string::npos) nm = nm.substr(sp + 1);
            ruler.name = nm;
            ruler.house = nm.substr(0, nm.find(' '));
        }
        int rid = doc.addCharacter(ruler);
        doc.addLink({EntKind::Character, rid}, {EntKind::Nation, n}, RelType::Rules, year - 5, 5);
        Character sp = makeCharacter(doc, Role::Noble, capital, n, year, s);
        sp.sex = ruler.sex == 0 ? 1 : 0;
        sp.born = ruler.born + (int)(charRand01(s) * 10) - 5;
        sp.title = rulerTitle(doc.nations[n].government, sp.sex == 1);
        int sid = doc.addCharacter(sp);
        doc.addLink({EntKind::Character, rid}, {EntKind::Character, sid}, RelType::Spouse, year - 15, 4);
        int kids = 1 + (int)(charRand(s) % 3);
        for (int k = 0; k < kids; k++) makeChild(doc, rid, sid, ruler.born + 22 + (int)(charRand01(s) * 15), s);
        // nobles at court: some loyal, some scheming
        int nobles = 2 + (int)(charRand(s) % 2);
        for (int k = 0; k < nobles; k++) {
            int city = cities.empty() ? -1 : cities[charRand(s) % cities.size()];
            int id = doc.addCharacter(makeCharacter(doc, Role::Noble, city, n, year, s));
            const Character* c = doc.character(id);
            bool schemer = c->hasTrait(2) || c->hasTrait(4);
            doc.addLink({EntKind::Character, id}, {EntKind::Character, rid}, schemer ? RelType::Rival : RelType::Serves, year - 3, 3);
        }
        // townsfolk in the bigger cities
        std::sort(cities.begin(), cities.end(), [&](int a, int b) { return doc.cities[a].population > doc.cities[b].population; });
        for (size_t ci = 0; ci < cities.size() && ci < 3; ci++) {
            int city = cities[ci];
            std::vector<int> here;
            static const Role mix[] = {Role::Merchant, Role::Criminal, Role::Commoner, Role::Priest, Role::Soldier, Role::Artisan, Role::Scholar, Role::Farmer, Role::Adventurer, Role::Mage};
            int count = perCity + (ci == 0 ? 2 : 0);
            for (int k = 0; k < count; k++) {
                Role r = mix[k < 3 ? k : charRand(s) % 10];
                here.push_back(doc.addCharacter(makeCharacter(doc, r, city, n, year, s)));
            }
            // a web of acquaintances in town
            for (size_t a = 0; a < here.size(); a++)
                for (size_t b = a + 1; b < here.size(); b++) {
                    double r = charRand01(s);
                    const Character* A = doc.character(here[a]); const Character* B = doc.character(here[b]);
                    if (A->role == Role::Merchant && B->role != Role::Criminal && r < 0.35) doc.addLink({EntKind::Character, here[a]}, {EntKind::Character, here[b]}, RelType::Employs, year - 2);
                    else if ((A->role == Role::Criminal || B->role == Role::Criminal) && r < 0.3) doc.addLink({EntKind::Character, here[a]}, {EntKind::Character, here[b]}, RelType::Enemy, year - 1, 2);
                    else if (r < 0.25) doc.addLink({EntKind::Character, here[a]}, {EntKind::Character, here[b]}, RelType::Friend, year - 4);
                    else if (r < 0.33) doc.addLink({EntKind::Character, here[a]}, {EntKind::Character, here[b]}, RelType::Rival, year - 2, 2);
                    else if (r < 0.37) doc.addLink({EntKind::Character, here[a]}, {EntKind::Character, here[b]}, RelType::Lover, year - 1, 4);
                }
        }
    }
}

// ---- portraits ----
void drawPortrait(Rectangle r, const Character& c, int year) {
    uint32_t s = c.look * 2654435761u + 1u;
    auto rnd = [&]() { return charRand01(s); };
    static const Color roleBg[] = {{92, 104, 120, 255}, {120, 40, 50, 255}, {70, 60, 120, 255}, {40, 100, 80, 255}, {45, 45, 50, 255}, {95, 75, 50, 255},
                                   {130, 120, 70, 255}, {50, 80, 120, 255}, {110, 80, 60, 255}, {90, 110, 60, 255}, {60, 90, 90, 255}, {80, 50, 110, 255}};
    Color bg = roleBg[(int)c.role % 12];
    float cx = r.x + r.width / 2, sz = std::min(r.width, r.height);
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, Color{(unsigned char)std::min(255, bg.r + 40), (unsigned char)std::min(255, bg.g + 40), (unsigned char)std::min(255, bg.b + 40), 255}, bg);
    // skin: varied, a little influenced by the seed only (cultures differ in the world, not here)
    static const Color skins[] = {{247, 214, 190, 255}, {233, 190, 160, 255}, {214, 165, 128, 255}, {190, 135, 95, 255}, {150, 100, 70, 255}, {110, 72, 50, 255}};
    Color skin = skins[charRand(s) % 6];
    int age = ageIn(c, year);
    static const Color hairs[] = {{30, 25, 22, 255}, {70, 45, 30, 255}, {120, 70, 40, 255}, {160, 70, 35, 255}, {210, 175, 110, 255}, {225, 205, 150, 255}};
    Color hair = hairs[charRand(s) % 6];
    if (age > 50) { float g = std::min(1.f, (age - 50) / 25.f); hair = Color{(unsigned char)(hair.r + (200 - hair.r) * g), (unsigned char)(hair.g + (200 - hair.g) * g), (unsigned char)(hair.b + (205 - hair.b) * g), 255}; }
    static const Color cloth[] = {{110, 115, 100, 255}, {150, 30, 40, 255}, {60, 50, 130, 255}, {30, 110, 90, 255}, {35, 35, 40, 255}, {120, 100, 70, 255},
                                  {230, 225, 210, 255}, {40, 60, 110, 255}, {140, 90, 50, 255}, {120, 110, 60, 255}, {70, 80, 70, 255}, {90, 40, 130, 255}};
    Color cl = cloth[(int)c.role % 12];
    float headR = sz * 0.2f, headY = r.y + sz * 0.44f;
    // shoulders and neck
    DrawRectangleRounded({cx - sz * 0.42f, r.y + sz * 0.74f, sz * 0.84f, r.height - sz * 0.74f}, 0.7f, 10, cl);
    DrawRectangleRec({cx - sz * 0.07f, headY + headR * 0.6f, sz * 0.14f, sz * 0.16f}, skin);
    // long hair behind the head
    bool longHair = c.sex == 1 ? rnd() < 0.8 : rnd() < 0.2;
    if (longHair) DrawEllipse((int)cx, (int)(headY + headR * 0.45f), headR * 1.15f, headR * 1.45f, hair);
    // head
    DrawEllipse((int)cx, (int)headY, headR * 0.9f, headR * 1.12f, skin);
    // hair on top
    bool bald = c.sex == 0 && age > 45 && rnd() < 0.35;
    if (!bald) {
        DrawCircleSector({cx, headY - headR * 0.1f}, headR * 0.98f, 180, 360, 24, hair);
        DrawEllipse((int)cx, (int)(headY - headR * 0.75f), headR * 0.85f, headR * 0.4f, hair);
    }
    // eyes, brows, nose, mouth
    float ey = headY + headR * 0.05f, ex = headR * 0.36f;
    for (float sgn : {-1.f, 1.f}) {
        DrawEllipse((int)(cx + sgn * ex), (int)ey, headR * 0.13f, headR * 0.08f, Color{250, 250, 245, 255});
        DrawCircle((int)(cx + sgn * ex), (int)ey, headR * 0.06f, Color{50, 40, 35, 255});
        DrawLineEx({cx + sgn * (ex - headR * 0.16f), ey - headR * (0.2f + (c.hasTrait(10) ? 0.0f : 0.04f))}, {cx + sgn * (ex + headR * 0.16f), ey - headR * (c.hasTrait(10) ? 0.26f : 0.2f)}, std::max(1.5f, headR * 0.07f), hair);
    }
    DrawLineEx({cx, ey + headR * 0.05f}, {cx - headR * 0.06f, ey + headR * 0.38f}, std::max(1.f, headR * 0.05f), Color{(unsigned char)(skin.r * 0.8), (unsigned char)(skin.g * 0.75), (unsigned char)(skin.b * 0.75), 255});
    bool smile = c.hasTrait(9) || c.hasTrait(14) || c.hasTrait(7);
    float my = ey + headR * 0.6f;
    if (smile) DrawRing({cx, my - headR * 0.12f}, headR * 0.2f, headR * 0.2f + std::max(1.2f, headR * 0.05f), 20, 160, 12, Color{120, 50, 50, 255});
    else DrawLineEx({cx - headR * 0.18f, my}, {cx + headR * 0.18f, my}, std::max(1.2f, headR * 0.05f), Color{120, 50, 50, 255});
    // beard
    if (c.sex == 0 && age > 20 && rnd() < 0.5) {
        DrawCircleSector({cx, ey + headR * 0.35f}, headR * 0.8f, 0, 180, 20, hair);
        DrawRing({cx, my - headR * 0.05f}, 0, headR * 0.2f, 0, 360, 12, skin);
        DrawLineEx({cx - headR * 0.16f, my}, {cx + headR * 0.16f, my}, std::max(1.2f, headR * 0.05f), Color{120, 50, 50, 255});
    }
    // role details
    if (c.role == Role::Ruler) {
        Color gold{230, 185, 60, 255};
        float by = headY - headR * 1.02f;
        DrawRectangleRec({cx - headR * 0.75f, by, headR * 1.5f, headR * 0.28f}, gold);
        for (int k = 0; k < 5; k++) {
            float x = cx - headR * 0.75f + k * headR * 0.375f;
            DrawTriangle({x - headR * 0.12f, by}, {x + headR * 0.12f, by}, {x, by - headR * 0.35f}, gold);
        }
    } else if (c.role == Role::Criminal) {
        DrawCircleSector({cx, headY - headR * 0.05f}, headR * 1.2f, 180, 360, 24, Color{35, 35, 40, 255});
        DrawRectangleRec({cx - headR * 1.2f, headY - headR * 0.1f, headR * 0.3f, headR * 1.4f}, Color{35, 35, 40, 255});
        DrawRectangleRec({cx + headR * 0.9f, headY - headR * 0.1f, headR * 0.3f, headR * 1.4f}, Color{35, 35, 40, 255});
    } else if (c.role == Role::Mage) {
        DrawTriangle({cx - headR * 1.0f, headY - headR * 0.75f}, {cx + headR * 1.0f, headY - headR * 0.75f}, {cx + headR * 0.2f, headY - headR * 2.0f}, Color{70, 40, 120, 255});
    } else if (c.role == Role::Soldier) {
        DrawCircleSector({cx, headY - headR * 0.2f}, headR * 1.0f, 180, 360, 24, Color{150, 155, 165, 255});
    } else if (c.role == Role::Priest) {
        DrawRectangleRec({cx - sz * 0.04f, headY + headR * 1.5f, sz * 0.08f, sz * 0.2f}, Color{230, 200, 90, 255});
    }
    if (c.died <= year) DrawRectangleRec(r, Color{20, 20, 25, 140});   // the dead are shown greyed
}
