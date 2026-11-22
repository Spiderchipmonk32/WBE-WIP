#include "sim.h"
#include "characters.h"
#include "politics.h"
#include <cmath>
#include <algorithm>
#include <queue>
#include <cstdio>

#include <cstdarg>
static std::string TextFormatSafe(const char* fmt, ...) {
    char b[512];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    return b;
}

const int kSimSpeeds[] = {0, 1, 2, 5, 10, 20, 50, 100};
const int kSimSpeedCount = 8;

double simDaysPerSecond(int base, int speedIdx) {
    if (speedIdx <= 0 || speedIdx >= kSimSpeedCount) return 0;
    double m = kSimSpeeds[speedIdx];
    if (base == 0) return m / 86400.0;   // real time
    if (base == 1) return m;              // a day a second
    return m * 30.0;                      // a month a second
}

const char* monthName(const Document& doc, int m) {
    static const char* def[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
    if (m >= 0 && m < (int)doc.monthNames.size() && !doc.monthNames[m].empty()) return doc.monthNames[m].c_str();
    return def[((m % 12) + 12) % 12];
}

std::string dateString(const Document& doc, int year, int month, int day) {
    char b[96];
    snprintf(b, sizeof b, "%d %s, year %d", day + 1, monthName(doc, month), year);
    return b;
}

double Simulation::r01() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; }

namespace {
int rulerOf(const Document& doc, int nation, int year) {
    for (const Link& l : doc.links)
        if (l.type == RelType::Rules && l.a.kind == EntKind::Character && l.b.kind == EntKind::Nation && l.b.id == nation) {
            const Character* c = doc.character(l.a.id);
            if (c && c->alive(year)) return c->id;
        }
    return -1;
}
int spouseOf(const Document& doc, int id, int year) {
    for (const Link& l : doc.links)
        if (l.type == RelType::Spouse && l.a.kind == EntKind::Character && l.b.kind == EntKind::Character) {
            int o = l.a.id == id ? l.b.id : l.b.id == id ? l.a.id : -1;
            if (o < 0) continue;
            const Character* c = doc.character(o);
            if (c && c->alive(year)) return o;
        }
    return -1;
}
std::vector<int> childrenOf(const Document& doc, int id) {
    std::vector<int> out;
    for (const Link& l : doc.links)
        if (l.type == RelType::Parent && l.a.kind == EntKind::Character && l.a.id == id && l.b.kind == EntKind::Character) out.push_back(l.b.id);
    return out;
}
double cityDistKm(const Document& doc, const World& w, int a, int b) {
    const City& A = doc.cities[a]; const City& B = doc.cities[b];
    auto dir = [](double la, double lo) { la *= kPi / 180; lo *= kPi / 180; return Vec3d{std::cos(la) * std::sin(lo), std::sin(la), std::cos(la) * std::cos(lo)}; };
    return angleBetween(dir(A.lat, A.lon), dir(B.lat, B.lon)) * w.radiusKm;
}
bool atWar(const Document& doc, int n) {
    for (auto& r : doc.relations) if (r.type == Relation::War && (r.a == n || r.b == n)) return true;
    return false;
}
int cityCount(const Document& doc, int n) { int k = 0; for (auto& c : doc.cities) if (c.nation == n) k++; return k; }
}

void Simulation::say(Document& doc, Character* c, const std::string& text, bool important, int city) {
    if (c) {
        c->log.push_back({doc.simYear, doc.simMonth, text});
        if (c->log.size() > 250) c->log.erase(c->log.begin(), c->log.begin() + 50);
        if (c->followed) important = true;
    }
    if (!important) return;
    news.push_back({doc.simYear, doc.simMonth, c ? c->name + ": " + text : text, c ? c->id : -1, city >= 0 ? city : (c ? c->city : -1), important});
    if (news.size() > 400) news.erase(news.begin(), news.begin() + 100);
}

void Simulation::history(Document& doc, EventType t, int city, const std::string& title, const std::string& text) {
    HistEvent e;
    e.year = doc.simYear; e.type = t; e.title = title; e.text = text; e.radiusKm = 40;
    if (city >= 0 && city < (int)doc.cities.size()) { e.lat = doc.cities[city].lat; e.lon = doc.cities[city].lon; }
    doc.events.push_back(e);
    news.push_back({doc.simYear, doc.simMonth, title + (text.empty() ? "" : ": " + text), -1, city, true});
}

void Simulation::begin(Document& doc, const World& w) {
    (void)w;
    if (!doc.simStarted) {
        doc.simYear = doc.currentYear; doc.simMonth = 0; doc.simDay = 0;
        doc.simStarted = true;
        rng ^= (uint32_t)doc.simYear * 2654435761u;
    }
    if (doc.characters.empty() && !doc.nations.empty()) generateNotables(doc, doc.simYear, rng);
    // every standing nation needs a living ruler
    for (int n = 0; n < (int)doc.nations.size(); n++) {
        if (doc.nations[n].fell <= doc.simYear) continue;
        if (rulerOf(doc, n, doc.simYear) < 0) succession(doc, n, -1);
    }
}

int Simulation::personToFollow(Document& doc, int city, Role role) {
    for (auto& c : doc.characters)
        if (c.city == city && c.role == role && c.alive(doc.simYear) && ageIn(c, doc.simYear) >= 16) { c.followed = true; return c.id; }
    if (role == Role::Ruler && city >= 0 && city < (int)doc.cities.size() && doc.cities[city].nation >= 0) {
        int r = rulerOf(doc, doc.cities[city].nation, doc.simYear);
        if (r >= 0) { doc.character(r)->followed = true; return r; }
    }
    Character c = makeCharacter(doc, role == Role::Ruler ? Role::Noble : role, city, -1, doc.simYear, rng);
    c.followed = true; c.userMade = false;
    int id = doc.addCharacter(c);
    say(doc, doc.character(id), "You start following " + doc.character(id)->name + ", a " + doc.character(id)->occupation + " of " +
        (city >= 0 && city < (int)doc.cities.size() ? doc.cities[city].name : std::string("the wilds")) + ".", true);
    return id;
}

int Simulation::advanceDays(Document& doc, const World& w, double days) {
    int months = 0;
    doc.simCarryDays += days;
    while (doc.simCarryDays >= 1) {
        doc.simCarryDays -= 1;
        if (++doc.simDay >= doc.daysPerMonth) {
            doc.simDay = 0;
            monthTick(doc, w);
            months++;
            if (++doc.simMonth >= 12) { doc.simMonth = 0; doc.simYear++; yearTick(doc, w); }
        }
        if (months > 2400) { doc.simCarryDays = 0; break; }   // never stall for more than ~200 years at once
    }
    doc.currentYear = doc.simYear;
    doc.presentYear = doc.simYear;
    return months;
}

int Simulation::update(Document& doc, const World& w, double realSeconds) {
    double d = realSeconds * simDaysPerSecond(doc.simBase, doc.simSpeed);
    if (d <= 0) return 0;
    // at most a couple of years per frame so the app stays responsive
    d = std::min(d, 720.0);
    return advanceDays(doc, w, d);
}

void Simulation::monthTick(Document& doc, const World& w) {
    people(doc, w);
    nations(doc, w);
}

void Simulation::yearTick(Document& doc, const World& w) {
    (void)w;
    // cities grow (more in peace, less in war), with the odd plague or fire
    static const double growth[5] = {0.002, 0.003, 0.005, 0.015, 0.012};
    double g = growth[std::max(0, std::min(4, doc.era))];
    for (int i = 0; i < (int)doc.cities.size(); i++) {
        City& c = doc.cities[i];
        if (c.destroyed <= doc.simYear) continue;
        double st = c.nation >= 0 && c.nation < (int)doc.nations.size() ? doc.nations[c.nation].stability : 0.5;
        double rate = g * (0.4 + st) - (c.nation >= 0 && atWar(doc, c.nation) ? 0.01 : 0);
        if (r01() < 0.0015) {
            rate -= 0.15 + r01() * 0.2;
            if (c.population > 15000) history(doc, EventType::Disaster, i, "Plague in " + c.name, "Sickness swept through the city and many died.");
        } else if (r01() < 0.001) {
            rate -= 0.05;
            if (c.population > 15000) history(doc, EventType::Disaster, i, "Great fire of " + c.name, "A fire burned whole districts of the city.");
        }
        c.population = (int)std::max(200.0, c.population * (1 + rate));
    }
    for (auto& n : doc.nations) {
        if (n.fell <= doc.simYear) continue;
        n.treasury = (float)std::max(0.0, n.treasury * 0.97 + 200 + 50 * r01());
    }
}

int Simulation::succession(Document& doc, int nation, int oldRuler) {
    if (nation < 0 || nation >= (int)doc.nations.size()) return -1;
    Nation& N = doc.nations[nation];
    int year = doc.simYear;
    int heir = -1;
    if (oldRuler >= 0) {
        // eldest living adult child, then any child, then the spouse
        int bestAge = -1;
        for (int k : childrenOf(doc, oldRuler)) {
            const Character* c = doc.character(k);
            if (!c || !c->alive(year)) continue;
            int a = ageIn(*c, year);
            if (a > bestAge) { bestAge = a; heir = k; }
        }
        if (heir < 0) heir = spouseOf(doc, oldRuler, year);
    }
    if (heir < 0) {
        // a noble of the realm, or a newcomer
        for (auto& c : doc.characters)
            if (c.nation == nation && c.role == Role::Noble && c.alive(year) && ageIn(c, year) >= 18) { heir = c.id; break; }
    }
    if (heir < 0) {
        int capital = -1;
        for (int i = 0; i < (int)doc.cities.size(); i++) if (doc.cities[i].nation == nation && (capital < 0 || doc.cities[i].capital)) capital = i;
        Character c = makeCharacter(doc, Role::Noble, capital, nation, year, rng);
        c.born = year - (25 + (int)(r01() * 25));
        heir = doc.addCharacter(c);
    }
    Character* h = doc.character(heir);
    h->role = Role::Ruler;
    h->title = rulerTitle(N.government, h->sex == 1);
    h->occupation = "ruler";
    h->nation = nation;
    h->renown = std::max(h->renown, 60.0);
    doc.addLink({EntKind::Character, heir}, {EntKind::Nation, nation}, RelType::Rules, year, 5);
    if (!N.rulers.empty() && N.rulers.back().end > year) N.rulers.back().end = year;
    Ruler r; r.name = h->title + " " + h->name; r.start = year; r.end = year + 200;
    N.rulers.push_back(r);
    say(doc, h, "Became " + h->title + " of " + N.name + ".", true);
    if (ageIn(*h, year) < 16) say(doc, h, "Being so young, a regent rules in their name.");
    N.stability = std::max(0.f, N.stability - 0.1f);
    return heir;
}

void Simulation::people(Document& doc, const World& w) {
    const int year = doc.simYear;
    const int era = std::max(0, std::min(4, doc.era));
    const double eraHealth[5] = {1.3, 1.2, 1.0, 0.7, 0.35};
    int living = 0;
    for (auto& c : doc.characters) if (c.alive(year)) living++;
    const bool crowded = living > 2500;
    // index loop: new people are appended while we go
    const size_t count = doc.characters.size();
    for (size_t idx = 0; idx < count; idx++) {
        Character* c = &doc.characters[idx];
        if (!c->alive(year)) continue;
        const int id = c->id;
        int age = ageIn(*c, year);
        bool notable = c->followed || c->userMade || c->role == Role::Ruler || c->role == Role::Noble || c->renown > 25;
        // ---- death ----
        double hz = (0.004 + 0.00035 * std::exp((age - 30) / 9.5)) * eraHealth[era] * (1.6 - c->health);
        if (age < 5) hz += 0.04 * eraHealth[era];
        if (c->role == Role::Criminal || c->role == Role::Adventurer) hz += 0.02;
        if (c->role == Role::Soldier && c->nation >= 0 && atWar(doc, c->nation)) hz += 0.08;
        if (r01() < hz / 12) {
            c->died = year;
            const char* how = age > 65 ? "Died peacefully in old age." : c->role == Role::Soldier && atWar(doc, c->nation) ? "Fell in battle." :
                              c->role == Role::Criminal ? "Was killed in a fight in the back streets." : age < 5 ? "Died young of a fever." : "Died of an illness.";
            bool wasRuler = false; int nation = -1;
            for (const Link& l : doc.links) if (l.type == RelType::Rules && l.a.kind == EntKind::Character && l.a.id == id && l.b.kind == EntKind::Nation) { wasRuler = true; nation = l.b.id; }
            say(doc, c, how, wasRuler);
            if (c->followed) {
                // the story goes on with their eldest living child
                int best = -1, bestAge = -1;
                for (int k : childrenOf(doc, id)) { const Character* ch = doc.character(k); if (ch && ch->alive(year) && ageIn(*ch, year) > bestAge) { bestAge = ageIn(*ch, year); best = k; } }
                if (best >= 0) { doc.character(best)->followed = true; say(doc, doc.character(best), "You now follow " + doc.character(best)->name + ", child of " + c->name + ".", true); }
                c = doc.character(id);
            }
            if (wasRuler && nation >= 0 && doc.nations[nation].fell > year) {
                std::string old = c->title + " " + c->name;
                int where = c->city;
                int heir = succession(doc, nation, id);   // may add people: c is not valid after this
                if (heir >= 0) history(doc, EventType::Note, where, "Death of " + old, doc.character(heir)->title + " " + doc.character(heir)->name + " takes the throne of " + doc.nations[nation].name + ".");
            }
            peopleChanged = true;
            continue;
        }
        // ---- growing up ----
        if (age == 16 && c->occupation == "child" && doc.simMonth == 0) {
            static const Role grow[] = {Role::Commoner, Role::Farmer, Role::Artisan, Role::Merchant, Role::Soldier, Role::Priest, Role::Scholar, Role::Adventurer, Role::Criminal, Role::Mage};
            if (c->role != Role::Noble) c->role = grow[(int)(r01() * 10)];
            static const char* occ[] = {"labourer", "ruler", "lord", "trader", "thief", "soldier", "priest", "scholar", "smith", "farmer", "sellsword", "mage"};
            c->occupation = c->role == Role::Noble ? "courtier" : occ[(int)c->role];
            say(doc, c, std::string("Came of age and became a ") + c->occupation + ".");
        }
        if (age < 16) continue;
        // ---- love and family ----
        int sp = spouseOf(doc, id, year);
        if (sp < 0 && age >= 18 && age <= 45 && r01() < (notable ? 0.09 : 0.03) / 12 && !crowded) {
            Character partner = makeCharacter(doc, c->role == Role::Ruler ? Role::Noble : c->role, c->city, c->nation, year, rng);
            partner.sex = c->sex == 0 ? 1 : c->sex == 1 ? 0 : (int)(r01() * 2);
            partner.born = year - std::max(17, age + (int)(r01() * 10) - 5);
            NameStyle st = partner.culture >= 0 && partner.culture < (int)doc.cultures.size() ? doc.cultures[partner.culture].style : NameStyle::Latin;
            partner.name = makeName(st, rng, false) + " " + partner.house;
            if (c->role == Role::Ruler && c->nation >= 0) partner.title = rulerTitle(doc.nations[c->nation].government, partner.sex == 1);
            int pid = doc.addCharacter(partner);
            c = doc.character(id);
            doc.addLink({EntKind::Character, id}, {EntKind::Character, pid}, RelType::Spouse, year, 4);
            std::string where = c->city >= 0 && c->city < (int)doc.cities.size() ? " in " + doc.cities[c->city].name : "";
            say(doc, c, "Married " + doc.character(pid)->name + where + ".", c->role == Role::Ruler);
            say(doc, doc.character(pid), "Married " + c->name + ".");
            sp = pid;
        }
        if (sp >= 0) {
            Character* s2 = doc.character(sp);
            const Character* mother = c->sex == 1 ? c : (s2 && s2->sex == 1 ? s2 : nullptr);
            if (mother && mother->id == id) {   // only count each couple once
                int ma = ageIn(*mother, year);
                if (ma >= 18 && ma <= 42 && r01() < (crowded && !notable ? 0.02 : 0.16) / 12) {
                    int kid = makeChild(doc, id, sp, year, rng);
                    c = doc.character(id);
                    if (kid >= 0) {
                        Character* k = doc.character(kid);
                        k->followed = false;
                        say(doc, c, "Had a child, " + k->name + ".", c->role == Role::Ruler);
                        if (Character* s3 = doc.character(sp)) say(doc, s3, "Had a child, " + k->name + ".");
                        if (c->followed) { std::string nm = k->name; say(doc, c, "(Follow " + nm + " from the Characters tab.)"); }
                    }
                }
            }
        }
        // ---- what they do ----
        int home = c->city;
        bool hasHome = home >= 0 && home < (int)doc.cities.size();
        std::string town = hasHome ? doc.cities[home].name : "the countryside";
        double luck = c->hasTrait(23) ? 0.15 : 0;
        switch (c->role) {
            case Role::Merchant: {
                if (c->travelTo < 0 && hasHome && r01() < 0.3) {
                    // pick a destination among the nearer cities
                    // a market town within reach (a few weeks' travel)
                    std::vector<int> nearby;
                    for (int k = 0; k < (int)doc.cities.size(); k++)
                        if (k != home && doc.cities[k].destroyed > year && cityDistKm(doc, w, home, k) < 1100) nearby.push_back(k);
                    int best = nearby.empty() ? -1 : nearby[(size_t)(r01() * nearby.size()) % nearby.size()];
                    if (best >= 0) { c->travelTo = best; c->travelT = 0; say(doc, c, "Set out from " + town + " for " + doc.cities[best].name + " with a caravan of goods."); }
                } else if (c->travelTo >= 0 && c->travelTo < (int)doc.cities.size() && hasHome) {
                    double km = std::max(50.0, cityDistKm(doc, w, home, c->travelTo));
                    c->travelT += (float)(30.0 * 35.0 / km);   // ~35 km a day
                    if (r01() < 0.015 - luck * 0.05) {
                        c->wealth *= 0.7;
                        say(doc, c, "Bandits robbed the caravan on the road to " + doc.cities[c->travelTo].name + ".");
                    }
                    if (c->travelT >= 1) {
                        double margin = (r01() * 0.25 - 0.04 + (c->hasTrait(4) ? 0.04 : 0) + (c->hasTrait(6) ? 0.03 : 0) + luck);
                        double gain = std::max(200.0, c->wealth) * margin;
                        c->wealth = std::max(0.0, c->wealth + gain);
                        std::string dest = doc.cities[c->travelTo].name;
                        say(doc, c, gain >= 0 ? TextFormatSafe("Sold goods in %s for a profit of %.0f coins.", dest.c_str(), gain)
                                              : TextFormatSafe("Prices were bad in %s; lost %.0f coins.", dest.c_str(), -gain));
                        c->travelTo = -1; c->travelT = 0;
                        if (c->wealth > 20000 && c->renown < 40) { c->renown += 5; say(doc, c, "Is now one of the richest merchants in " + town + "."); }
                    }
                }
                break;
            }
            case Role::Criminal: {
                if (r01() < 0.3) {
                    double skill = 0.65 + (c->hasTrait(4) ? 0.15 : 0) + luck - (c->hasTrait(1) ? 0.1 : 0);
                    if (r01() < skill) {
                        double loot = 20 + r01() * 200 * (hasHome ? std::sqrt(doc.cities[home].population / 10000.0) : 0.5);
                        c->wealth += loot; c->renown = std::min(100.0, c->renown + 0.6);
                        static const char* crimes[] = {"Picked pockets in the market of %s (%.0f coins).", "Broke into a merchant's house in %s (%.0f coins).",
                                                       "Smuggled goods through %s (%.0f coins).", "Ran a crooked dice game in %s (%.0f coins).",
                                                       "Robbed travellers outside %s (%.0f coins)."};
                        say(doc, c, TextFormatSafe(crimes[(int)(r01() * 5)], town.c_str(), loot));
                    } else if (r01() < 0.5) {
                        c->wealth *= 0.4; c->health = std::max(0.2, c->health - 0.1);
                        say(doc, c, "Was caught by the watch in " + town + " and thrown in jail.");
                        if (r01() < 0.08) { c->died = year; say(doc, c, "Was hanged for their crimes.", true); continue; }
                    }
                }
                if (c->renown > 30 && c->title.empty()) {
                    c->title = "Boss"; c->occupation = "gang boss";
                    say(doc, c, "Now runs the biggest gang in " + town + ".", true);
                }
                break;
            }
            case Role::Soldier: {
                if (c->nation >= 0 && atWar(doc, c->nation) && r01() < 0.25) {
                    c->renown = std::min(100.0, c->renown + 2 + r01() * 3);
                    say(doc, c, r01() < 0.5 ? "Fought bravely in a battle." : "Marched with the army to the front.");
                    if (c->renown > 30 && c->title.empty()) { c->title = "Captain"; say(doc, c, "Was made a captain."); }
                    else if (c->renown > 60 && c->title == "Captain") { c->title = "General"; say(doc, c, "Was made a general.", true); }
                } else if (r01() < 0.04) say(doc, c, "Stood guard on the walls of " + town + ".");
                c->wealth += 8;
                break;
            }
            case Role::Noble: {
                int nation = c->nation;
                if (nation >= 0 && nation < (int)doc.nations.size() && doc.nations[nation].fell > year) {
                    int ruler = rulerOf(doc, nation, year);
                    bool schemer = c->hasTrait(2) || c->hasTrait(4);
                    if (ruler >= 0 && ruler != id && schemer && age >= 20 && r01() < 0.004) {
                        Character* R = doc.character(ruler);
                        std::string rn = R->title + " " + R->name;
                        double chance = 0.3 + (0.5 - doc.nations[nation].stability) * 0.5;
                        if (r01() < chance) {
                            R->died = year;
                            say(doc, R, "Was overthrown and killed in a palace plot.", true);
                            for (auto& l : doc.links) if (l.type == RelType::Rules && l.a.kind == EntKind::Character && l.a.id == ruler) l.type = RelType::Enemy;
                            c = doc.character(id);
                            c->role = Role::Ruler; c->title = rulerTitle(doc.nations[nation].government, c->sex == 1);
                            doc.addLink({EntKind::Character, id}, {EntKind::Nation, nation}, RelType::Rules, year, 5);
                            Nation& N = doc.nations[nation];
                            if (!N.rulers.empty()) N.rulers.back().end = year;
                            N.rulers.push_back({c->title + " " + c->name, year, year + 200});
                            N.stability = std::max(0.f, N.stability - 0.25f);
                            say(doc, c, "Seized the throne of " + N.name + " from " + rn + ".", true);
                            history(doc, EventType::Note, c->city, "Coup in " + N.name, c->name + " overthrew " + rn + ".");
                        } else {
                            c->died = r01() < 0.5 ? year : 99999;
                            if (c->died == year) say(doc, c, "A plot against " + rn + " was discovered; executed for treason.", true);
                            else { c->title = ""; c->role = Role::Commoner; c->city = -1; say(doc, c, "A plot against " + rn + " failed; stripped of lands and exiled.", true); }
                            continue;
                        }
                    } else if (r01() < 0.03) say(doc, c, r01() < 0.5 ? "Hosted a feast for the local lords." : "Went hunting in the royal forests.");
                }
                c->wealth += 40;
                break;
            }
            case Role::Ruler: {
                int nation = -1;
                for (const Link& l : doc.links) if (l.type == RelType::Rules && l.a.kind == EntKind::Character && l.a.id == id && l.b.kind == EntKind::Nation) nation = l.b.id;
                if (nation < 0 || nation >= (int)doc.nations.size() || doc.nations[nation].fell <= year) break;
                Nation& N = doc.nations[nation];
                double target = 0.6 + (c->hasTrait(9) ? 0.1 : 0) + (c->hasTrait(16) ? 0.08 : 0) - (c->hasTrait(8) ? 0.12 : 0) - (c->hasTrait(17) ? 0.1 : 0) +
                                (c->hasTrait(14) ? 0.06 : 0) - (atWar(doc, nation) ? 0.15 : 0);
                N.stability += (float)((target - N.stability) * 0.02 + (r01() - 0.5) * 0.02);
                N.stability = std::max(0.f, std::min(1.f, N.stability));
                c->wealth += N.treasury * 0.002;
                if (r01() < 0.02) {
                    static const char* deeds[] = {"Held court in %s.", "Raised taxes across the realm.", "Ordered new roads built from %s.",
                                                  "Hosted envoys from abroad in %s.", "Toured the provinces.", "Commissioned a great monument in %s."};
                    say(doc, c, TextFormatSafe(deeds[(int)(r01() * 6)], town.c_str()));
                }
                // war-like rulers turn rivalries into wars
                double aggression = 0.002 + (c->hasTrait(10) ? 0.004 : 0) + (c->hasTrait(2) ? 0.004 : 0) + (c->hasTrait(0) ? 0.002 : 0) - (c->hasTrait(9) ? 0.0015 : 0);
                for (auto& r : doc.relations) {
                    if (r.type != Relation::Rivalry || (r.a != nation && r.b != nation)) continue;
                    int other = r.a == nation ? r.b : r.a;
                    if (other < 0 || other >= (int)doc.nations.size() || doc.nations[other].fell <= year) continue;
                    if (r01() < aggression) {
                        r.type = Relation::War;
                        say(doc, c, "Declared war on " + doc.nations[other].name + ".", true);
                        int cap = -1; for (int i = 0; i < (int)doc.cities.size(); i++) if (doc.cities[i].nation == other && doc.cities[i].capital) cap = i;
                        history(doc, EventType::War, cap, "War between " + N.name + " and " + doc.nations[other].name, c->title + " " + c->name + " declared war.");
                        break;
                    }
                }
                break;
            }
            default: {
                if (r01() < 0.06) {
                    std::vector<const char*> t;
                    switch (c->role) {
                        case Role::Farmer: t = {"Brought in a fine harvest.", "Hail ruined part of the crop.", "Bought a new plough ox.", "Lost sheep to wolves."}; break;
                        case Role::Artisan: t = {"Finished a masterpiece that sold for a good price.", "Took on an apprentice.", "Joined the craft guild of %s.", "Had a slow season with few buyers."}; break;
                        case Role::Priest: t = {"Preached to a packed temple in %s.", "Blessed the harvest.", "Went on a pilgrimage.", "Argued with the temple elders."}; break;
                        case Role::Scholar: t = {"Finished a treatise on the history of %s.", "Found an old map in the archives.", "Took on students.", "Was laughed at for a bold new theory."}; break;
                        case Role::Mage: t = {"Learned a new spell.", "An experiment went badly wrong.", "Was hired by a lord of %s.", "Found a rare reagent in the hills."}; break;
                        case Role::Adventurer: t = {"Cleared a monster's lair near %s.", "Was wounded fighting bandits.", "Found a buried hoard.", "Guided travellers through the mountains."}; break;
                        default: t = {"Found work at the docks of %s.", "Fell ill for a while.", "Made a new friend at the tavern.", "Moved to a better house.", "Lost their job.", "Celebrated a festival in %s."}; break;
                    }
                    const char* pick = t[(int)(r01() * t.size()) % t.size()];
                    say(doc, c, TextFormatSafe(pick, town.c_str()));
                    if (std::string(pick).find("wounded") != std::string::npos || std::string(pick).find("ill") != std::string::npos) c->health = std::max(0.2, c->health - 0.1);
                    if (std::string(pick).find("hoard") != std::string::npos) c->wealth += 300 + r01() * 2000;
                    if (std::string(pick).find("lair") != std::string::npos) c->renown += 4;
                    // friendships
                    if (std::string(pick).find("friend") != std::string::npos && hasHome) {
                        for (int k = 0; k < 6; k++) {
                            Character& o = doc.characters[(size_t)(r01() * doc.characters.size())];
                            if (o.id != id && o.city == home && o.alive(year)) { doc.addLink({EntKind::Character, id}, {EntKind::Character, o.id}, RelType::Friend, year, 2); break; }
                        }
                    }
                }
                c->wealth += c->role == Role::Commoner ? 3 : 6;
                // the poor move to the big city now and then
                if (c->role == Role::Commoner && hasHome && r01() < 0.002) {
                    int best = -1;
                    for (int i = 0; i < (int)doc.cities.size(); i++)
                        if (doc.cities[i].nation == doc.cities[home].nation && doc.cities[i].population > doc.cities[home].population * 2 &&
                            (best < 0 || r01() < 0.3)) best = i;
                    if (best >= 0) { c->city = best; say(doc, c, "Moved from " + town + " to " + doc.cities[best].name + " looking for work."); }
                }
                break;
            }
        }
    }
}

void Simulation::transferLand(Document& doc, const World& w, int from, int to, int cells) {
    (void)w;
    const int W = World::W, H = World::H, N = W * H;
    if ((int)doc.owner.size() != N) return;
    uint16_t F = (uint16_t)(from + 1), T = (uint16_t)(to + 1);
    std::vector<int> frontier;
    for (int i = 0; i < N; i++) {
        if (doc.owner[i] != F) continue;
        int x = i % W, y = i / W;
        if (doc.owner[World::idx(x + 1, y)] == T || doc.owner[World::idx(x - 1, y)] == T ||
            (y > 0 && doc.owner[World::idx(x, y - 1)] == T) || (y < H - 1 && doc.owner[World::idx(x, y + 1)] == T)) frontier.push_back(i);
    }
    if (frontier.empty()) {
        // no shared border (across the sea): land around one of their cities
        for (auto& c : doc.cities) if (c.nation == from) { frontier.push_back(World::nearestCell(c.lat, c.lon)); break; }
    }
    int taken = 0;
    std::vector<uint8_t> seen(N, 0);
    std::queue<int> q;
    for (int i : frontier) { q.push(i); seen[i] = 1; }
    while (!q.empty() && taken < cells) {
        int i = q.front(); q.pop();
        if (doc.owner[i] != F) continue;
        doc.owner[i] = T; taken++;
        int x = i % W, y = i / W;
        const int nb[4] = {World::idx(x + 1, y), World::idx(x - 1, y), World::idx(x, y + (y < H - 1 ? 1 : 0)), World::idx(x, y - (y > 0 ? 1 : 0))};
        for (int n : nb) if (!seen[n] && doc.owner[n] == F && r01() < 0.85) { seen[n] = 1; q.push(n); }
    }
    if (taken > 0) territoryChanged = true;
    // cities now inside the winner's land change hands
    for (int i = 0; i < (int)doc.cities.size(); i++) {
        City& c = doc.cities[i];
        if (c.nation != from) continue;
        int cell = World::nearestCell(c.lat, c.lon);
        if (doc.owner[cell] == T) {
            bool wasCap = c.capital;
            c.nation = to; c.capital = false;
            history(doc, EventType::War, i, c.name + " falls to " + doc.nations[to].name, wasCap ? "The capital of " + doc.nations[from].name + " was taken." : "");
            if (wasCap) {
                int best = -1;
                for (int k = 0; k < (int)doc.cities.size(); k++) if (doc.cities[k].nation == from && (best < 0 || doc.cities[k].population > doc.cities[best].population)) best = k;
                if (best >= 0) { doc.cities[best].capital = true; news.push_back({doc.simYear, doc.simMonth, doc.nations[from].name + " moves its capital to " + doc.cities[best].name, -1, best, true}); }
            }
        }
    }
}

void Simulation::fallNation(Document& doc, int n, int conqueror) {
    Nation& N = doc.nations[n];
    if (N.fell <= doc.simYear) return;
    N.fell = doc.simYear;
    if (!N.rulers.empty() && N.rulers.back().end > doc.simYear) N.rulers.back().end = doc.simYear;
    uint16_t F = (uint16_t)(n + 1), T = conqueror >= 0 ? (uint16_t)(conqueror + 1) : 0;
    for (auto& o : doc.owner) if (o == F) o = T;
    for (auto& c : doc.cities) if (c.nation == n) { c.nation = conqueror; c.capital = false; }
    doc.relations.erase(std::remove_if(doc.relations.begin(), doc.relations.end(), [&](const NationRelation& r) { return r.a == n || r.b == n; }), doc.relations.end());
    int ruler = rulerOf(doc, n, doc.simYear);
    if (ruler >= 0) {
        Character* R = doc.character(ruler);
        R->role = Role::Noble; R->title = "Former " + R->title;
        say(doc, R, "Lost the realm of " + N.name + ".", true);
        for (auto& l : doc.links) if (l.type == RelType::Rules && l.a.kind == EntKind::Character && l.a.id == ruler && l.b.id == n) l.type = RelType::MemberOf;
    }
    territoryChanged = true;
    history(doc, EventType::Fall, -1, "The fall of " + N.name, conqueror >= 0 ? "Its lands were taken by " + doc.nations[conqueror].name + "." : "Its lands broke apart.");
}

void Simulation::nations(Document& doc, const World& w) {
    const int year = doc.simYear;
    // ---- wars: a battle a month, the winner takes land ----
    for (size_t ri = 0; ri < doc.relations.size(); ri++) {
        NationRelation r = doc.relations[ri];
        if (r.type != Relation::War) continue;
        if (r.a >= (int)doc.nations.size() || r.b >= (int)doc.nations.size()) continue;
        auto strength = [&](int n) {
            double p = 0; for (auto& c : doc.cities) if (c.nation == n) p += c.population;
            return std::sqrt(p + 1000) * (0.5 + doc.nations[n].stability) * (0.6 + 0.8 * r01());
        };
        double sa = strength(r.a), sb = strength(r.b);
        int win = sa > sb ? r.a : r.b, lose = win == r.a ? r.b : r.a;
        double margin = std::fabs(sa - sb) / std::max(sa, sb);
        transferLand(doc, w, lose, win, (int)(8 + margin * 80));
        doc.nations[lose].stability = std::max(0.f, doc.nations[lose].stability - 0.03f);
        if (cityCount(doc, lose) == 0) { fallNation(doc, lose, win); continue; }
        // peace
        if (r01() < 0.035) {
            NationRelation& rr = doc.relations[ri];
            double roll = r01();
            rr.type = roll < 0.55 ? Relation::Rivalry : roll < 0.8 ? Relation::Vassal : Relation::TradePact;
            if (rr.type == Relation::Vassal) { rr.a = lose; rr.b = win; }
            int cap = -1; for (int i = 0; i < (int)doc.cities.size(); i++) if (doc.cities[i].nation == win && doc.cities[i].capital) cap = i;
            history(doc, EventType::Treaty, cap, "Peace between " + doc.nations[win].name + " and " + doc.nations[lose].name,
                    rr.type == Relation::Vassal ? doc.nations[lose].name + " bows to " + doc.nations[win].name + " as a vassal." : "The war is over, for now.");
        }
    }
    // ---- rebellions in unhappy realms ----
    if (doc.simMonth == 6) {
        for (int n = 0; n < (int)doc.nations.size(); n++) {
            Nation& N = doc.nations[n];
            if (N.fell <= year || N.stability > 0.25f || cityCount(doc, n) < 3 || r01() > 0.2) continue;
            int capital = -1, rebel = -1; double far = -1;
            for (int i = 0; i < (int)doc.cities.size(); i++) if (doc.cities[i].nation == n && doc.cities[i].capital) capital = i;
            if (capital < 0) continue;
            for (int i = 0; i < (int)doc.cities.size(); i++)
                if (doc.cities[i].nation == n && i != capital) { double d = cityDistKm(doc, w, capital, i); if (d > far) { far = d; rebel = i; } }
            if (rebel < 0) continue;
            Nation nn;
            NameStyle st = N.culture >= 0 && N.culture < (int)doc.cultures.size() ? doc.cultures[N.culture].style : NameStyle::Latin;
            nn.name = makeName(st, rng, true);
            nn.culture = N.culture; nn.government = (int)(r01() * 4);
            nn.r = (unsigned char)(60 + r01() * 190); nn.g = (unsigned char)(60 + r01() * 190); nn.b = (unsigned char)(60 + r01() * 190);
            nn.founded = year; nn.stability = 0.55f;
            doc.nations.push_back(nn);
            int nid = (int)doc.nations.size() - 1;
            Nation& Old = doc.nations[n];
            // land nearer the rebel city than the capital goes with it
            auto dir = [](double la, double lo) { la *= kPi / 180; lo *= kPi / 180; return Vec3d{std::cos(la) * std::sin(lo), std::sin(la), std::cos(la) * std::cos(lo)}; };
            Vec3d dc = dir(doc.cities[capital].lat, doc.cities[capital].lon), dr = dir(doc.cities[rebel].lat, doc.cities[rebel].lon);
            for (int i = 0; i < (int)doc.owner.size(); i++)
                if (doc.owner[i] == n + 1 && angleBetween(w.cellDir[i], dr) * 1.15 < angleBetween(w.cellDir[i], dc)) doc.owner[i] = (uint16_t)(nid + 1);
            for (auto& c : doc.cities)
                if (c.nation == n && doc.owner[World::nearestCell(c.lat, c.lon)] == nid + 1) c.nation = nid;
            doc.cities[rebel].capital = true;
            Old.stability = std::min(1.f, Old.stability + 0.15f);
            doc.relations.push_back({n, nid, r01() < 0.6 ? Relation::War : Relation::Rivalry});
            int leader = succession(doc, nid, -1);
            if (Character* L = doc.character(leader)) { L->city = rebel; L->culture = Old.culture; }
            territoryChanged = true;
            history(doc, EventType::Founding, rebel, "Rebellion: " + nn.name + " breaks away", doc.cities[rebel].name + " and the lands around it rise against " + Old.name + ".");
        }
    }
    // ---- empty realms fall ----
    for (int n = 0; n < (int)doc.nations.size(); n++) {
        if (doc.nations[n].fell <= year) continue;
        if (cityCount(doc, n) == 0 && doc.nations[n].founded != year) {
            int land = 0; for (auto o : doc.owner) if (o == n + 1) land++;
            if (land < 3) fallNation(doc, n, -1);
        }
    }
}
