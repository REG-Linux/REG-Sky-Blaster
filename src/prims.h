// Quick shapes for what the screen has many of (stars, nebulae, bullets,
// particles): coloured triangles queued in field units, handed to the
// renderer in drawing order by Canvas::drawPrims. Gradients are vertex
// colours; no edge antialiasing.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "color.h"

class Prims {
 public:
  void additive(bool on) { additive_ = on; }  // 'lighter' for what follows

  void rect(float x, float y, float w, float h, Color c, float a);
  // vertical gradient: stops at 0, mid and 1 of the height
  void rectV(float x, float y, float w, float h, Color top, float mid, Color middle, Color bottom, float a);
  void quad(const float (&xy)[8], Color c, float a);
  void disc(float x, float y, float r, Color c, float a);
  void radial(float x, float y, float r, Color inner, Color outer, float a);  // inner at the centre
  void ring(float x, float y, float r, float width, Color c, float a);
  // cols x rows cells, each corner coloured by `at` (opaque)
  void grid(float x, float y, float w, float h, int cols, int rows, const std::function<Color(float, float)>& at);

  struct Vert {
    float x, y;
    uint8_t r, g, b, a;  // premultiplied
  };
  struct Run {
    bool additive;
    size_t first, count;
  };
  const std::vector<Vert>& verts() const { return verts_; }
  const std::vector<Run>& runs() const { return runs_; }
  void clear();
  void setScale(float pixelsPerUnit) { scale_ = pixelsPerUnit; }  // for circle detail

 private:
  Vert vert(float x, float y, Color c, float a) const;
  void tri(const Vert& a, const Vert& b, const Vert& c);
  int segments(float r) const;

  std::vector<Vert> verts_;
  std::vector<Vert> row_;  // grid() scratch
  std::vector<Run> runs_;
  bool additive_ = false;
  float scale_ = 1;
};
