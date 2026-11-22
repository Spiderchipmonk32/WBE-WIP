#pragma once
// Animated wind and ocean-current particles drawn over the map. Each particle drifts with the
// local wind (or current) for the chosen month and leaves a short fading trail, so the flow
// can be read at a glance on any map layer, e.g. while planning sea routes.
#include "raylib.h"
#include "world.h"
#include <cmath>
#include <vector>
#include <functional>

struct FlowLayer {
    struct P { float x, y, age, life; float tx[16], ty[16]; int n = 0; };
    std::vector<P> ps;
    bool currents = false;
    unsigned rng = 12345;
    float frand() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.f; }

    // toLatLon: screen -> (lat, lon); returns false when off the planet
    // speedPx: screen pixels per second per m/s
    void update(const World& w, int month, Rectangle area, float dt,
                const std::function<bool(float, float, double&, double&)>& toLatLon, float speedPx, int count) {
        if ((int)ps.size() != count) { ps.assign(count, P{}); for (auto& p : ps) respawn(p, area, true); }
        dt = std::min(dt, 0.05f);
        for (auto& p : ps) {
            p.age += dt;
            double lat, lon;
            if (p.age > p.life || !CheckCollisionPointRec({p.x, p.y}, area) || !toLatLon(p.x, p.y, lat, lon)) { respawn(p, area, false); continue; }
            float u, v;
            if (!velocity(w, month, lat, lon, u, v)) { respawn(p, area, false); continue; }
            // keep the trail
            for (int k = std::min(p.n, 15); k > 0; k--) { p.tx[k] = p.tx[k - 1]; p.ty[k] = p.ty[k - 1]; }
            p.tx[0] = p.x; p.ty[0] = p.y; p.n = std::min(p.n + 1, 16);
            float k = speedPx * (currents ? 12.f : 1.f);
            p.x += u * k * dt;
            p.y -= v * k * dt;
        }
    }

    bool velocity(const World& w, int month, double lat, double lon, float& u, float& v) const {
        if (std::fabs(lat) > 86) return false;
        if (currents) {
            if (w.currentU.empty() || w.elev[World::nearestCell(lat, lon)] > 0) return false;
            u = (float)World::sample(w.currentU, lat, lon); v = (float)World::sample(w.currentV, lat, lon);
            return true;
        }
        if (w.windU[0].empty()) return false;
        if (month >= 0 && month < 12) {
            u = (float)World::sample(w.windU[month], lat, lon); v = (float)World::sample(w.windV[month], lat, lon);
        } else {   // whole-year average
            u = v = 0;
            for (int m = 0; m < 12; m += 2) { u += (float)World::sample(w.windU[m], lat, lon); v += (float)World::sample(w.windV[m], lat, lon); }
            u /= 6; v /= 6;
        }
        return true;
    }

    void respawn(P& p, Rectangle area, bool scatterAge) {
        p.x = area.x + frand() * area.width;
        p.y = area.y + frand() * area.height;
        p.life = 1.5f + frand() * 2.5f;
        p.age = scatterAge ? frand() * p.life : 0;
        p.n = 0;
    }

    void draw(bool inkStyle) const {
        for (const auto& p : ps) {
            if (p.n < 2) continue;
            float fadeIn = std::min(1.f, p.age / 0.4f), fadeOut = std::min(1.f, (p.life - p.age) / 0.5f);
            float a0 = std::max(0.f, std::min(fadeIn, fadeOut));
            float len = std::hypot(p.x - p.tx[p.n - 1], p.y - p.ty[p.n - 1]);
            float strength = std::min(1.f, len / 40.f);
            Color base = currents ? Color{110, 225, 255, 255}
                       : inkStyle ? Color{70, 45, 25, 255}
                                  : Color{(unsigned char)(235 + 20 * strength), (unsigned char)(245 - 40 * strength), (unsigned char)(255 - 150 * strength), 255};
            Vector2 prev{p.x, p.y};
            for (int k = 0; k < p.n; k++) {
                Vector2 q{p.tx[k], p.ty[k]};
                if (std::fabs(q.x - prev.x) > 200) break;   // wrapped round the map edge
                float a = a0 * (1 - (float)k / p.n) * 0.95f;
                DrawLineEx(prev, q, 2.2f - 1.2f * k / p.n, Fade(base, a));
                prev = q;
            }
        }
    }
};
