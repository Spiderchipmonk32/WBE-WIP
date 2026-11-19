#pragma once
#include <cmath>

// Double-precision vector. The planet is modelled in kilometres, and doubles keep
// street-level detail (metres) accurate on a 6,371 km sphere.
struct Vec3d {
    double x = 0, y = 0, z = 0;
    Vec3d() = default;
    Vec3d(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    Vec3d operator+(const Vec3d& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3d operator-(const Vec3d& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3d operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3d operator-() const { return {-x, -y, -z}; }
    Vec3d& operator+=(const Vec3d& o) { x += o.x; y += o.y; z += o.z; return *this; }
    double dot(const Vec3d& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3d cross(const Vec3d& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3d normalized() const {
        double l = length();
        return l > 0 ? Vec3d{x / l, y / l, z / l} : Vec3d{0, 1, 0};
    }
};

constexpr double kPi = 3.14159265358979323846;

inline double angleBetween(const Vec3d& a, const Vec3d& b) {
    double d = a.dot(b);
    d = d > 1 ? 1 : (d < -1 ? -1 : d);
    return std::acos(d);
}

inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

inline double smoothstepd(double a, double b, double x) {
    double t = clampd((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
