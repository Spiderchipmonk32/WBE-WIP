#pragma once
#include <cstdint>
#include <cmath>

// Seeded 3D simplex noise (after Stefan Gustavson's reference implementation).
class Simplex3 {
public:
    explicit Simplex3(uint32_t seed = 0) { reseed(seed); }

    void reseed(uint32_t seed) {
        uint8_t p[256];
        for (int i = 0; i < 256; i++) p[i] = (uint8_t)i;
        uint32_t state = seed * 2654435761u + 1u;
        for (int i = 255; i > 0; i--) {
            int j = (int)(next(state) % (uint32_t)(i + 1));
            uint8_t t = p[i]; p[i] = p[j]; p[j] = t;
        }
        for (int i = 0; i < 512; i++) { perm[i] = p[i & 255]; pm12[i] = perm[i] % 12; }
    }

    double operator()(double x, double y, double z) const {
        static const int G[36] = {1,1,0,-1,1,0,1,-1,0,-1,-1,0,1,0,1,-1,0,1,1,0,-1,-1,0,-1,0,1,1,0,-1,1,0,1,-1,0,-1,-1};
        const double F3 = 1.0 / 3.0, G3 = 1.0 / 6.0;
        double s = (x + y + z) * F3;
        double fi = std::floor(x + s), fj = std::floor(y + s), fk = std::floor(z + s);
        double t = (fi + fj + fk) * G3;
        double x0 = x - (fi - t), y0 = y - (fj - t), z0 = z - (fk - t);
        int i1, j1, k1, i2, j2, k2;
        if (x0 >= y0) {
            if (y0 >= z0)      { i1=1; j1=0; k1=0; i2=1; j2=1; k2=0; }
            else if (x0 >= z0) { i1=1; j1=0; k1=0; i2=1; j2=0; k2=1; }
            else               { i1=0; j1=0; k1=1; i2=1; j2=0; k2=1; }
        } else {
            if (y0 < z0)       { i1=0; j1=0; k1=1; i2=0; j2=1; k2=1; }
            else if (x0 < z0)  { i1=0; j1=1; k1=0; i2=0; j2=1; k2=1; }
            else               { i1=0; j1=1; k1=0; i2=1; j2=1; k2=0; }
        }
        double x1 = x0 - i1 + G3, y1 = y0 - j1 + G3, z1 = z0 - k1 + G3;
        double x2 = x0 - i2 + 2 * G3, y2 = y0 - j2 + 2 * G3, z2 = z0 - k2 + 2 * G3;
        double x3 = x0 - 1 + 3 * G3, y3 = y0 - 1 + 3 * G3, z3 = z0 - 1 + 3 * G3;
        // wrap lattice coords into 0..255 (works for negative and very large values)
        int ii = (int)((int64_t)fi & 255), jj = (int)((int64_t)fj & 255), kk = (int)((int64_t)fk & 255);
        double n = 0, tt; int g;
        tt = 0.6 - x0*x0 - y0*y0 - z0*z0;
        if (tt > 0) { g = pm12[ii + perm[jj + perm[kk]]] * 3; tt *= tt; n += tt * tt * (G[g]*x0 + G[g+1]*y0 + G[g+2]*z0); }
        tt = 0.6 - x1*x1 - y1*y1 - z1*z1;
        if (tt > 0) { g = pm12[ii + i1 + perm[jj + j1 + perm[kk + k1]]] * 3; tt *= tt; n += tt * tt * (G[g]*x1 + G[g+1]*y1 + G[g+2]*z1); }
        tt = 0.6 - x2*x2 - y2*y2 - z2*z2;
        if (tt > 0) { g = pm12[ii + i2 + perm[jj + j2 + perm[kk + k2]]] * 3; tt *= tt; n += tt * tt * (G[g]*x2 + G[g+1]*y2 + G[g+2]*z2); }
        tt = 0.6 - x3*x3 - y3*y3 - z3*z3;
        if (tt > 0) { g = pm12[ii + 1 + perm[jj + 1 + perm[kk + 1]]] * 3; tt *= tt; n += tt * tt * (G[g]*x3 + G[g+1]*y3 + G[g+2]*z3); }
        return 32.0 * n;
    }

private:
    static uint32_t next(uint32_t& a) {  // mulberry32
        a += 0x6D2B79F5u;
        uint32_t t = a;
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return t ^ (t >> 14);
    }
    uint8_t perm[512];
    uint8_t pm12[512];
};
