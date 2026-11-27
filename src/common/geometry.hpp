// Small 2D/3D math shared by vision, gestures and output. Pixels of the analysed frame unless a
// name says otherwise; y points down, angles are radians, positive = clockwise on screen.
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace racg {

struct Vec2 {
  float x = 0, y = 0;
  Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
  Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
  Vec2 operator*(float k) const { return {x * k, y * k}; }
  Vec2 operator/(float k) const { return {x / k, y / k}; }
  Vec2 &operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
};

inline float dist(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

struct Vec3 {
  float x = 0, y = 0, z = 0;
};

// A point found by a model. x, y in pixels of the analysed frame; z in the same scale as x (MediaPipe's
// convention); visibility and presence are probabilities (1 when the model doesn't give them).
struct Landmark {
  float x = 0, y = 0, z = 0;
  float visibility = 1, presence = 1;
  Vec2 xy() const { return {x, y}; }
};

// An axis-aligned box: left, top, width, height.
struct Box {
  float x = 0, y = 0, w = 0, h = 0;
  float right() const { return x + w; }
  float bottom() const { return y + h; }
  Vec2 center() const { return {x + w / 2, y + h / 2}; }
};

inline float iou(const Box &a, const Box &b) {
  const float ix = std::max(0.f, std::min(a.right(), b.right()) - std::max(a.x, b.x));
  const float iy = std::max(0.f, std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y));
  const float inter = ix * iy, uni = a.w * a.h + b.w * b.h - inter;
  return uni > 0 ? inter / uni : 0;
}

// A rotated rectangle: centre, size and rotation. MediaPipe's NormalizedRect, in pixels.
struct RotRect {
  float cx = 0, cy = 0, w = 0, h = 0, rotation = 0;
  bool empty() const { return w <= 0 || h <= 0; }
  // Corner order: top-left, top-right, bottom-right, bottom-left (before rotation).
  Vec2 corner(int i) const {
    const float hx = (i == 1 || i == 2) ? w / 2 : -w / 2, hy = (i >= 2) ? h / 2 : -h / 2;
    const float c = std::cos(rotation), s = std::sin(rotation);
    return {cx + hx * c - hy * s, cy + hx * s + hy * c};
  }
  Box bounds() const {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < 4; ++i) {
      const Vec2 p = corner(i);
      x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
    }
    return {x0, y0, x1 - x0, y1 - y0};
  }
};

inline float normalizeRadians(float a) {
  constexpr float tau = 2 * std::numbers::pi_v<float>;
  return a - tau * std::floor((a + std::numbers::pi_v<float>) / tau);
}

inline float sigmoid(float x) { return 1.f / (1.f + std::exp(-x)); }

}  // namespace racg
