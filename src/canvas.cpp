#include "canvas.h"

#include <cmath>

#include "nanovg.h"

namespace {

NVGcolor nc(Color c) { return nvgRGBAf(c.r, c.g, c.b, c.a); }

constexpr float kBig = 1e5f;

}  // namespace

Gradient Gradient::linear(float x0, float y0, float x1, float y1) {
  Gradient g;
  g.x0 = x0, g.y0 = y0, g.x1 = x1, g.y1 = y1;
  return g;
}

Gradient Gradient::radialGradient(float x0, float y0, float r0, float x1, float y1, float r1) {
  Gradient g;
  g.radial = true;
  g.x0 = x0, g.y0 = y0, g.r0 = r0, g.x1 = x1, g.y1 = y1, g.r1 = r1;
  return g;
}

Canvas::Canvas(NVGcontext* vg) : vg_(vg) {}

// Liberation Sans is metric-compatible with Arial, what the original's
// '"Segoe UI",Arial' gets off Windows; DejaVu fills in the symbols
// (◀ ▶ ◇ ★ ⚠ ...) Liberation lacks.
bool Canvas::loadFonts(const std::string& dir) {
  const char* files[3] = {"LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf", "DejaVuSansMono.ttf"};
  const char* names[3] = {"sans", "bold", "mono"};
  for (int i = 0; i < 3; i++) {
    fontIds_[i] = nvgCreateFont(vg_, names[i], (dir + "/" + files[i]).c_str());
    if (fontIds_[i] < 0) return false;
  }
  int symbols = nvgCreateFont(vg_, "symbols", (dir + "/DejaVuSans.ttf").c_str());
  int symbolsBold = nvgCreateFont(vg_, "symbols-bold", (dir + "/DejaVuSans-Bold.ttf").c_str());
  if (symbols < 0 || symbolsBold < 0) return false;
  nvgAddFallbackFontId(vg_, fontIds_[0], symbols);
  nvgAddFallbackFontId(vg_, fontIds_[1], symbolsBold);
  nvgAddFallbackFontId(vg_, fontIds_[2], symbols);
  return true;
}

void Canvas::save() {
  stack_.push_back(st_);
  nvgSave(vg_);
}

void Canvas::restore() {
  if (stack_.empty()) return;
  st_ = stack_.back();
  stack_.pop_back();
  nvgRestore(vg_);
}

void Canvas::translate(float x, float y) { nvgTranslate(vg_, x, y); }
void Canvas::rotate(float a) { nvgRotate(vg_, a); }
void Canvas::scale(float x, float y) { nvgScale(vg_, x, y); }

void Canvas::beginPath() { path_.clear(); }
void Canvas::moveTo(float x, float y) { path_.push_back({Cmd::Move, {x, y}}); }
void Canvas::lineTo(float x, float y) { path_.push_back({Cmd::Line, {x, y}}); }
void Canvas::closePath() { path_.push_back({Cmd::Close, {}}); }
void Canvas::arc(float x, float y, float r, float a0, float a1) {
  path_.push_back({Cmd::Arc, {x, y, r, a0, a1}});
}
void Canvas::ellipse(float x, float y, float rx, float ry) {
  path_.push_back({Cmd::Ellipse, {x, y, rx, ry}});
}
void Canvas::rect(float x, float y, float w, float h) { path_.push_back({Cmd::Rect, {x, y, w, h}}); }
void Canvas::roundRect(float x, float y, float w, float h, float r) { roundRect(x, y, w, h, r, r, r, r); }
void Canvas::roundRect(float x, float y, float w, float h, float tl, float tr, float br, float bl) {
  path_.push_back({Cmd::RoundRect, {x, y, w, h, tl, tr, br, bl}});
}

void Canvas::replay(bool dashed) {
  nvgBeginPath(vg_);
  float cx = 0, cy = 0, sx = 0, sy = 0;
  bool have = false;
  // dash phase carries along a subpath, as in the browser
  float phase = 0;
  auto dashLine = [&](float x0, float y0, float x1, float y1) {
    const float period = st_.dashOn + st_.dashOff;
    float len = std::hypot(x1 - x0, y1 - y0);
    if (len <= 0 || period <= 0) return;
    float dx = (x1 - x0) / len, dy = (y1 - y0) / len;
    float t = 0;
    while (t < len) {
      float inPeriod = std::fmod(phase, period);
      bool on = inPeriod < st_.dashOn;
      float step = on ? st_.dashOn - inPeriod : period - inPeriod;
      float t1 = std::fmin(len, t + step);
      if (on) {
        nvgMoveTo(vg_, x0 + dx * t, y0 + dy * t);
        nvgLineTo(vg_, x0 + dx * t1, y0 + dy * t1);
      }
      phase += t1 - t;
      t = t1;
    }
  };
  for (const Cmd& c : path_) {
    const float* v = c.v;
    switch (c.kind) {
      case Cmd::Move:
        if (!dashed) nvgMoveTo(vg_, v[0], v[1]);
        cx = sx = v[0], cy = sy = v[1], have = true, phase = 0;
        break;
      case Cmd::Line:
        if (!have) {
          // canvas treats lineTo on an empty path as moveTo
          if (!dashed) nvgMoveTo(vg_, v[0], v[1]);
          cx = sx = v[0], cy = sy = v[1], have = true;
          break;
        }
        if (dashed) dashLine(cx, cy, v[0], v[1]);
        else nvgLineTo(vg_, v[0], v[1]);
        cx = v[0], cy = v[1];
        break;
      case Cmd::Close:
        if (dashed) dashLine(cx, cy, sx, sy);
        else nvgClosePath(vg_);
        cx = sx, cy = sy;
        break;
      case Cmd::Arc:
        nvgArc(vg_, v[0], v[1], v[2], v[3], v[4], NVG_CW);
        cx = v[0] + std::cos(v[4]) * v[2], cy = v[1] + std::sin(v[4]) * v[2], have = true;
        break;
      case Cmd::Ellipse:
        nvgEllipse(vg_, v[0], v[1], v[2], v[3]);
        have = false;
        break;
      case Cmd::Rect:
        nvgRect(vg_, v[0], v[1], v[2], v[3]);
        have = false;
        break;
      case Cmd::RoundRect:
        nvgRoundedRectVarying(vg_, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
        have = false;
        break;
    }
  }
}

// Canvas shadowBlur approximated as a soft outline glow around the shape.
void Canvas::glowPass(bool forFill) {
  if (st_.shadowBlur <= 0 || st_.shadowColor.a <= 0) return;
  Color c = st_.shadowColor;
  const float base = forFill ? 0 : st_.lineWidth;
  for (float k : {1.0f, 0.6f, 0.3f}) {
    Color layer = c;
    layer.a *= 0.22f;
    nvgStrokeColor(vg_, nc(layer));
    nvgStrokeWidth(vg_, base + st_.shadowBlur * k);
    replay(false);
    nvgStroke(vg_);
  }
}

void Canvas::fillCurrent() {
  if (!st_.fillGradient) {
    nvgFillColor(vg_, nc(st_.fill));
    replay(false);
    nvgFill(vg_);
    return;
  }
  const Gradient& g = st_.gradient;
  if (g.stops.empty()) return;
  if (g.radial) {
    const auto& a = g.stops.front();
    const auto& b = g.stops.back();
    float inr = g.r0 + a.first * (g.r1 - g.r0);
    float outr = g.r0 + b.first * (g.r1 - g.r0);
    nvgFillPaint(vg_, nvgRadialGradient(vg_, g.x0, g.y0, inr, outr, nc(a.second), nc(b.second)));
    replay(false);
    nvgFill(vg_);
    return;
  }
  const float dx = g.x1 - g.x0, dy = g.y1 - g.y0;
  auto paintFor = [&](size_t i) {
    const auto& a = g.stops[i];
    const auto& b = g.stops[i + 1];
    return nvgLinearGradient(vg_, g.x0 + dx * a.first, g.y0 + dy * a.first, g.x0 + dx * b.first,
                             g.y0 + dy * b.first, nc(a.second), nc(b.second));
  };
  if (g.stops.size() <= 2) {
    nvgFillPaint(vg_, g.stops.size() == 1 ? nvgLinearGradient(vg_, 0, 0, 1, 0, nc(g.stops[0].second),
                                                              nc(g.stops[0].second))
                                          : paintFor(0));
    replay(false);
    nvgFill(vg_);
    return;
  }
  // NanoVG gradients have two stops: fill once per band, each band cut out
  // with a scissor laid along the gradient axis.
  const float len = std::hypot(dx, dy);
  const float angle = std::atan2(dy, dx);
  const size_t bands = g.stops.size() - 1;
  for (size_t i = 0; i < bands; i++) {
    float a = i == 0 ? -kBig : g.stops[i].first * len;
    float b = i + 1 == bands ? kBig : g.stops[i + 1].first * len;
    if (b <= a) continue;
    nvgSave(vg_);
    float xf[6];
    nvgCurrentTransform(vg_, xf);
    nvgTranslate(vg_, g.x0, g.y0);
    nvgRotate(vg_, angle);
    nvgIntersectScissor(vg_, a, -kBig, b - a, 2 * kBig);
    nvgResetTransform(vg_);
    nvgTransform(vg_, xf[0], xf[1], xf[2], xf[3], xf[4], xf[5]);
    nvgFillPaint(vg_, paintFor(i));
    replay(false);
    nvgFill(vg_);
    nvgRestore(vg_);
  }
}

void Canvas::fill() {
  glowPass(true);
  fillCurrent();
}

void Canvas::stroke() {
  glowPass(false);
  nvgStrokeColor(vg_, nc(st_.strokeColor));
  nvgStrokeWidth(vg_, st_.lineWidth);
  replay(st_.dashOn > 0);
  nvgStroke(vg_);
}

void Canvas::fillRect(float x, float y, float w, float h) {
  std::vector<Cmd> saved;
  saved.swap(path_);
  path_.push_back({Cmd::Rect, {x, y, w, h}});
  fillCurrent();
  path_.swap(saved);
}

void Canvas::applyFont() {
  int i = static_cast<int>(st_.font);
  nvgFontFaceId(vg_, fontIds_[i]);
  nvgFontSize(vg_, st_.fontPx);  // fontstash sizes by em, as canvas does
  int a = st_.align == Align::Left ? NVG_ALIGN_LEFT : st_.align == Align::Right ? NVG_ALIGN_RIGHT : NVG_ALIGN_CENTER;
  a |= st_.baseline == Baseline::Middle ? NVG_ALIGN_MIDDLE : NVG_ALIGN_BASELINE;
  nvgTextAlign(vg_, a);
}

void Canvas::fillText(const std::string& text, float x, float y) {
  if (text.empty()) return;
  applyFont();
  if (st_.shadowBlur > 0 && st_.shadowColor.a > 0) {
    // fontstash caps blur at 20; the canvas blur is about twice sigma
    nvgFontBlur(vg_, std::fmin(20.0f, st_.shadowBlur * 0.6f));
    nvgFillColor(vg_, nc(st_.shadowColor));
    nvgText(vg_, x, y, text.c_str(), nullptr);
    nvgFontBlur(vg_, 0);
  }
  nvgFillColor(vg_, nc(st_.fillGradient && !st_.gradient.stops.empty() ? st_.gradient.stops[0].second : st_.fill));
  nvgText(vg_, x, y, text.c_str(), nullptr);
}

void Canvas::fillStyle(Color c) {
  st_.fillGradient = false;
  st_.fill = c;
}

void Canvas::fillStyle(const Gradient& g) {
  st_.fillGradient = true;
  st_.gradient = g;
}

void Canvas::strokeStyle(Color c) { st_.strokeColor = c; }
void Canvas::lineWidth(float w) { st_.lineWidth = w; }

void Canvas::globalAlpha(float a) {
  st_.alpha = a < 0 ? 0 : a > 1 ? 1 : a;
  nvgGlobalAlpha(vg_, st_.alpha);
}

void Canvas::lighter(bool on) { nvgGlobalCompositeOperation(vg_, on ? NVG_LIGHTER : NVG_SOURCE_OVER); }

void Canvas::font(float px, Font f) {
  st_.fontPx = px;
  st_.font = f;
}

void Canvas::textAlign(Align a) { st_.align = a; }
void Canvas::textBaseline(Baseline b) { st_.baseline = b; }

void Canvas::shadow(Color c, float blur) {
  st_.shadowColor = c;
  st_.shadowBlur = blur;
}

void Canvas::lineDash(float on, float off) {
  st_.dashOn = on;
  st_.dashOff = off;
}
