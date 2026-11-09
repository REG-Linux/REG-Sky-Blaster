#include "prims.h"

#include <algorithm>
#include <cmath>


namespace {

constexpr float TAU = 6.28318530718f;

}  // namespace

Prims::Vert Prims::vert(float x, float y, Color c, float a) const {
  const float k = std::clamp(c.a * a, 0.0f, 1.0f);
  auto u8 = [](float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
  return {x, y, u8(c.r * k), u8(c.g * k), u8(c.b * k), u8(k)};
}

void Prims::tri(const Vert& a, const Vert& b, const Vert& c) {
  if (runs_.empty() || runs_.back().additive != additive_) runs_.push_back({additive_, verts_.size(), 0});
  verts_.push_back(a);
  verts_.push_back(b);
  verts_.push_back(c);
  runs_.back().count += 3;
}

int Prims::segments(float r) const { return std::clamp(static_cast<int>(r * scale_ * 0.7f), 6, 32); }

void Prims::rect(float x, float y, float w, float h, Color c, float a) {
  const Vert p0 = vert(x, y, c, a), p1 = vert(x + w, y, c, a), p2 = vert(x + w, y + h, c, a),
             p3 = vert(x, y + h, c, a);
  tri(p0, p1, p2);
  tri(p0, p2, p3);
}

void Prims::rectV(float x, float y, float w, float h, Color top, float mid, Color middle, Color bottom, float a) {
  const float ym = y + h * mid;
  const Vert t0 = vert(x, y, top, a), t1 = vert(x + w, y, top, a), m0 = vert(x, ym, middle, a),
             m1 = vert(x + w, ym, middle, a), b0 = vert(x, y + h, bottom, a), b1 = vert(x + w, y + h, bottom, a);
  tri(t0, t1, m1);
  tri(t0, m1, m0);
  tri(m0, m1, b1);
  tri(m0, b1, b0);
}

void Prims::grid(float x, float y, float w, float h, int cols, int rows,
                 const std::function<Color(float, float)>& at) {
  row_.resize(static_cast<size_t>(cols + 1) * 2);
  Vert* up = row_.data();
  Vert* down = up + cols + 1;
  for (int j = 0; j <= rows; j++) {
    const float py = y + h * j / rows;
    for (int i = 0; i <= cols; i++) {
      const float px = x + w * i / cols;
      down[i] = vert(px, py, at(px, py), 1);
    }
    if (j > 0)
      for (int i = 0; i < cols; i++) {
        tri(up[i], up[i + 1], down[i + 1]);
        tri(up[i], down[i + 1], down[i]);
      }
    std::swap(up, down);
  }
}

void Prims::quad(const float (&p)[8], Color c, float a) {
  const Vert v0 = vert(p[0], p[1], c, a), v1 = vert(p[2], p[3], c, a), v2 = vert(p[4], p[5], c, a),
             v3 = vert(p[6], p[7], c, a);
  tri(v0, v1, v2);
  tri(v0, v2, v3);
}

void Prims::disc(float x, float y, float r, Color c, float a) { radial(x, y, r, c, c, a); }

void Prims::radial(float x, float y, float r, Color inner, Color outer, float a) {
  const int n = segments(r);
  const Vert centre = vert(x, y, inner, a);
  Vert prev = vert(x + r, y, outer, a);
  for (int i = 1; i <= n; i++) {
    const float t = TAU * i / n;
    const Vert next = vert(x + std::cos(t) * r, y + std::sin(t) * r, outer, a);
    tri(centre, prev, next);
    prev = next;
  }
}

void Prims::ring(float x, float y, float r, float width, Color c, float a) {
  const float r0 = std::max(0.0f, r - width / 2), r1 = r + width / 2;
  const int n = segments(r1);
  for (int i = 0; i < n; i++) {
    const float t0 = TAU * i / n, t1 = TAU * (i + 1) / n;
    const float c0 = std::cos(t0), s0 = std::sin(t0), c1 = std::cos(t1), s1 = std::sin(t1);
    const Vert i0 = vert(x + c0 * r0, y + s0 * r0, c, a), i1 = vert(x + c1 * r0, y + s1 * r0, c, a);
    const Vert o0 = vert(x + c0 * r1, y + s0 * r1, c, a), o1 = vert(x + c1 * r1, y + s1 * r1, c, a);
    tri(i0, o0, o1);
    tri(i0, o1, i1);
  }
}

void Prims::clear() {
  verts_.clear();
  runs_.clear();
  additive_ = false;
}
