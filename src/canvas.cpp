#include "canvas.h"

#include <algorithm>
#include <cmath>

#include "prims.h"

extern "C" {
#include "fontstash.h"
int fonsAddFallbackFont(FONScontext* stash, int base, int fallback);
}

namespace {

constexpr float kTau = 6.28318530718f;
constexpr float kTolerance = 0.25f;  // px a curve may stray from its polygon
constexpr int kAtlasMax = 2048;

uint8_t u8(float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); }

Color premul(Color c, float alpha) {
  const float a = std::clamp(c.a * alpha, 0.0f, 1.0f);
  return {c.r * a, c.g * a, c.b * a, a};
}

Color lerp(Color a, Color b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

int arcSegments(float sweep, float radiusPx) {
  if (radiusPx <= kTolerance) return std::max(1, static_cast<int>(std::ceil(sweep / (kTau / 4))));
  const float step = 2 * std::acos(std::max(-1.0f, 1 - kTolerance / radiusPx));
  return std::clamp(static_cast<int>(std::ceil(sweep / step)), 1, 256);
}

float cross(float ax, float ay, float bx, float by) { return ax * by - ay * bx; }

// fontstash renders glyphs into its own buffer; the renderer only mirrors it
int atlasCreate(void*, int, int) { return 1; }
int atlasResize(void* r, int w, int h) {
  static_cast<Renderer*>(r)->atlasResize(w, h, nullptr);
  return 1;
}

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

Canvas::Canvas(Renderer& renderer) : r_(renderer) {
  FONSparams p{};
  p.width = 1024;
  p.height = 1024;
  p.flags = FONS_ZERO_TOPLEFT;
  p.userPtr = &r_;
  p.renderCreate = atlasCreate;
  p.renderResize = atlasResize;
  fs_ = fonsCreateInternal(&p);
}

Canvas::~Canvas() {
  if (fs_) fonsDeleteInternal(fs_);
}

// Liberation Sans is metric-compatible with Arial, what the original's
// '"Segoe UI",Arial' gets off Windows; DejaVu fills in the symbols
// (◀ ▶ ◇ ★ ⚠ ...) Liberation lacks.
bool Canvas::loadFonts(const std::string& dir) {
  if (!fs_) return false;
  const char* files[3] = {"LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf", "DejaVuSansMono.ttf"};
  const char* names[3] = {"sans", "bold", "mono"};
  for (int i = 0; i < 3; i++) {
    fontIds_[i] = fonsAddFont(fs_, names[i], (dir + "/" + files[i]).c_str(), 0);
    if (fontIds_[i] < 0) return false;
  }
  int symbols = fonsAddFont(fs_, "symbols", (dir + "/DejaVuSans.ttf").c_str(), 0);
  int symbolsBold = fonsAddFont(fs_, "symbols-bold", (dir + "/DejaVuSans-Bold.ttf").c_str(), 0);
  if (symbols < 0 || symbolsBold < 0) return false;
  fonsAddFallbackFont(fs_, fontIds_[0], symbols);
  fonsAddFallbackFont(fs_, fontIds_[1], symbolsBold);
  fonsAddFallbackFont(fs_, fontIds_[2], symbols);
  return true;
}

// ---- frame ----

void Canvas::beginFrame(int w, int h) {
  frameW_ = w, frameH_ = h, drawCalls_ = 0;
  stack_.clear();
  st_ = State{};
  r_.begin(w, h);
}

void Canvas::flushRenderer() {
  int dirty[4];
  if (fonsValidateTexture(fs_, dirty)) {
    int w = 0, h = 0;
    const unsigned char* data = fonsGetTextureData(fs_, &w, &h);
    r_.atlasUpdate(dirty, data);
  }
  r_.flush();
}

void Canvas::endFrame() { flushRenderer(); }

void Canvas::drawPrims(Prims& prims) {
  const float s = pixelScale();
  for (const Prims::Run& run : prims.runs()) {
    out_.clear();
    for (size_t i = run.first; i < run.first + run.count; i++) {
      const Prims::Vert& v = prims.verts()[i];
      const Pt p = apply(v.x, v.y);
      out_.push_back({p.x, p.y, r_.whiteU(), r_.whiteV(), v.r, v.g, v.b, v.a});
    }
    r_.blend(run.additive);
    r_.tris(out_.data(), out_.size());
  }
  prims.clear();
  prims.setScale(s);
}

void Canvas::tintRect(float x, float y, float w, float h) {
  const bool aa = aa_;
  aa_ = false;  // the edges are off screen or under the letterbox
  fillRect(x, y, w, h);
  aa_ = aa;
}

void Canvas::dim(float k) { r_.dim(k); }

// ---- state ----

void Canvas::save() { stack_.push_back(st_); }

void Canvas::restore() {
  if (stack_.empty()) return;
  st_ = stack_.back();
  stack_.pop_back();
}

void Canvas::resetTransform() {
  const float id[6] = {1, 0, 0, 1, 0, 0};
  std::copy(id, id + 6, st_.xf);
}

void Canvas::translate(float x, float y) {
  float* m = st_.xf;
  m[4] += m[0] * x + m[2] * y;
  m[5] += m[1] * x + m[3] * y;
}

void Canvas::rotate(float a) {
  float* m = st_.xf;
  const float c = std::cos(a), s = std::sin(a);
  const float a0 = m[0], b0 = m[1], c0 = m[2], d0 = m[3];
  m[0] = a0 * c + c0 * s;
  m[1] = b0 * c + d0 * s;
  m[2] = -a0 * s + c0 * c;
  m[3] = -b0 * s + d0 * c;
}

void Canvas::scale(float x, float y) {
  float* m = st_.xf;
  m[0] *= x, m[1] *= x, m[2] *= y, m[3] *= y;
}

Canvas::Pt Canvas::apply(float x, float y) const {
  const float* m = st_.xf;
  return {m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]};
}

float Canvas::pixelScale() const {
  const float* m = st_.xf;
  return std::sqrt(std::fabs(m[0] * m[3] - m[1] * m[2]));
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
void Canvas::globalAlpha(float a) { st_.alpha = std::clamp(a, 0.0f, 1.0f); }
void Canvas::lighter(bool on) { st_.lighter = on; }

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

// ---- paths ----

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

// The path as polygons in pixels. Dashed: split into the dash segments.
void Canvas::flatten(std::vector<Sub>& subs, bool dashed) const {
  const float s = pixelScale();
  Sub cur;
  bool haveStart = false;
  float startX = 0, startY = 0;  // the current subpath's first point, local
  auto finish = [&](bool closed) {
    cur.closed = closed;
    if (!cur.pts.empty()) subs.push_back(std::move(cur));
    cur = Sub{};
  };
  auto add = [&](float x, float y) {
    if (cur.pts.empty()) startX = x, startY = y, haveStart = true;
    cur.pts.push_back(apply(x, y));
  };
  auto arcPts = [&](float cx, float cy, float rx, float ry, float a0, float sweep) {
    const int n = arcSegments(std::fabs(sweep), std::max(rx, ry) * s);
    for (int i = 0; i <= n; i++) {
      const float t = a0 + sweep * i / n;
      add(cx + std::cos(t) * rx, cy + std::sin(t) * ry);
    }
  };
  for (const Cmd& c : path_) {
    const float* v = c.v;
    switch (c.kind) {
      case Cmd::Move:
        finish(false);
        add(v[0], v[1]);
        break;
      case Cmd::Line:
        // after a close the next segment starts where the closed one did
        if (cur.pts.empty() && haveStart) add(startX, startY);
        add(v[0], v[1]);
        break;
      case Cmd::Close:
        finish(true);
        break;
      case Cmd::Arc: {
        float sweep = v[4] - v[3];
        if (sweep >= kTau) sweep = kTau;
        else if (sweep < 0) sweep = std::fmod(sweep, kTau) + kTau;
        if (cur.pts.empty() && haveStart) add(startX, startY);
        arcPts(v[0], v[1], v[2], v[2], v[3], sweep);
        break;
      }
      case Cmd::Ellipse:
        finish(false);
        arcPts(v[0], v[1], v[2], v[3], 0, kTau);
        finish(true);
        break;
      case Cmd::Rect:
        finish(false);
        add(v[0], v[1]);
        add(v[0] + v[2], v[1]);
        add(v[0] + v[2], v[1] + v[3]);
        add(v[0], v[1] + v[3]);
        finish(true);
        break;
      case Cmd::RoundRect: {
        finish(false);
        const float x = v[0], y = v[1], w = v[2], h = v[3];
        const float lim = std::min(std::fabs(w), std::fabs(h)) / 2;
        const float tl = std::min(v[4], lim), tr = std::min(v[5], lim), br = std::min(v[6], lim),
                    bl = std::min(v[7], lim);
        auto corner = [&](float cx, float cy, float r, float a0) {
          if (r <= 0) return add(cx, cy);
          arcPts(cx, cy, r, r, a0, kTau / 4);
        };
        corner(x + tl, y + tl, tl, kTau / 2);
        corner(x + w - tr, y + tr, tr, kTau * 3 / 4);
        corner(x + w - br, y + h - br, br, 0);
        corner(x + bl, y + h - bl, bl, kTau / 4);
        finish(true);
        break;
      }
    }
  }
  finish(false);

  // drop repeated points; a closed ring does not repeat its first point
  for (Sub& sub : subs) {
    std::vector<Pt> pts;
    for (const Pt& p : sub.pts)
      if (pts.empty() || std::fabs(p.x - pts.back().x) + std::fabs(p.y - pts.back().y) > 1e-3f) pts.push_back(p);
    if (sub.closed && pts.size() > 1 &&
        std::fabs(pts.front().x - pts.back().x) + std::fabs(pts.front().y - pts.back().y) <= 1e-3f)
      pts.pop_back();
    sub.pts = std::move(pts);
  }

  if (!dashed) return;
  const float on = st_.dashOn * s, period = (st_.dashOn + st_.dashOff) * s;
  std::vector<Sub> dashes;
  for (const Sub& sub : subs) {
    float phase = 0;
    const size_t n = sub.pts.size();
    const size_t segs = sub.closed ? n : (n > 0 ? n - 1 : 0);
    for (size_t i = 0; i < segs; i++) {
      const Pt a = sub.pts[i], b = sub.pts[(i + 1) % n];
      const float len = std::hypot(b.x - a.x, b.y - a.y);
      if (len <= 0) continue;
      float t = 0;
      while (t < len) {
        const float in = std::fmod(phase, period);
        const bool onDash = in < on;
        // at least a thousandth of a pixel: a step below float precision at
        // `t` would not move and the loop would never end
        const float t1 = std::min(len, t + std::max(1e-3f, onDash ? on - in : period - in));
        if (onDash) {
          Sub d;
          d.pts = {{a.x + (b.x - a.x) * t / len, a.y + (b.y - a.y) * t / len},
                   {a.x + (b.x - a.x) * t1 / len, a.y + (b.y - a.y) * t1 / len}};
          dashes.push_back(std::move(d));
        }
        phase += t1 - t;
        t = t1;
      }
    }
  }
  subs = std::move(dashes);
}

// ---- paint ----

Canvas::Paint Canvas::solidPaint(Color c) const {
  Paint p;
  p.solid = c;
  p.alpha = st_.alpha;
  return p;
}

Canvas::Paint Canvas::fillPaint() const {
  if (!st_.fillGradient || st_.gradient.stops.empty()) return solidPaint(st_.fill);
  const Gradient& g = st_.gradient;
  Paint p;
  p.kind = g.radial ? 2 : 1;
  const Pt a = apply(g.x0, g.y0), b = apply(g.x1, g.y1);
  const float s = pixelScale();
  p.x0 = a.x, p.y0 = a.y, p.x1 = b.x, p.y1 = b.y, p.r0 = g.r0 * s, p.r1 = g.r1 * s;
  p.stops = g.stops;
  std::stable_sort(p.stops.begin(), p.stops.end(),
                   [](const auto& x, const auto& y) { return x.first < y.first; });
  p.alpha = st_.alpha;
  return p;
}

// where q falls along the gradient: stop offsets live on this scale
float Canvas::paintT(const Paint& p, Pt q) const {
  if (p.kind == 1) {
    const float dx = p.x1 - p.x0, dy = p.y1 - p.y0, dd = dx * dx + dy * dy;
    return dd > 0 ? ((q.x - p.x0) * dx + (q.y - p.y0) * dy) / dd : 0;
  }
  if (p.kind == 2) {
    const float d = std::hypot(q.x - p.x0, q.y - p.y0);
    return p.r1 != p.r0 ? (d - p.r0) / (p.r1 - p.r0) : 0;
  }
  return 0;
}

Color Canvas::paintAt(const Paint& p, float t) const {
  if (p.kind == 0) return premul(p.solid, p.alpha);
  const auto& s = p.stops;
  if (t <= s.front().first) return premul(s.front().second, p.alpha);
  if (t >= s.back().first) return premul(s.back().second, p.alpha);
  for (size_t i = 0; i + 1 < s.size(); i++) {
    if (t <= s[i + 1].first) {
      const float span = s[i + 1].first - s[i].first;
      const float f = span > 0 ? (t - s[i].first) / span : 1;
      // premultiplied, as NanoVG did: fading to 'transparent' does not darken
      return lerp(premul(s[i].second, p.alpha), premul(s[i + 1].second, p.alpha), f);
    }
  }
  return premul(s.back().second, p.alpha);
}

Vtx Canvas::vtx(Pt q, Color c) const {
  return {q.x, q.y, r_.whiteU(), r_.whiteV(), u8(c.r), u8(c.g), u8(c.b), u8(c.a)};
}

// ---- fill ----

// One triangle. A linear gradient is exact as vertex colours inside one band
// between stops, so a triangle that crosses stops is cut along them.
void Canvas::fillTriangle(Pt a, Pt b, Pt c, const Paint& paint) {
  struct TP {
    Pt p;
    float t;
  };
  const TP tri[3] = {{a, paintT(paint, a)}, {b, paintT(paint, b)}, {c, paintT(paint, c)}};
  auto emitFan = [&](const std::vector<TP>& poly) {
    for (size_t i = 1; i + 1 < poly.size(); i++)
      for (const TP* q : {&poly[0], &poly[i], &poly[i + 1]}) out_.push_back(vtx(q->p, paintAt(paint, q->t)));
  };
  const float tmin = std::min({tri[0].t, tri[1].t, tri[2].t}), tmax = std::max({tri[0].t, tri[1].t, tri[2].t});
  bool crosses = false;
  if (paint.kind == 1)
    for (const auto& s : paint.stops) crosses = crosses || (s.first > tmin && s.first < tmax);
  if (!crosses) return emitFan({tri[0], tri[1], tri[2]});

  // bands: below the first stop, between each pair, above the last
  std::vector<float> edges = {-1e9f};
  for (const auto& s : paint.stops) edges.push_back(s.first);
  edges.push_back(1e9f);
  for (size_t bnd = 0; bnd + 1 < edges.size(); bnd++) {
    const float lo = edges[bnd], hi = edges[bnd + 1];
    if (hi <= tmin || lo >= tmax) continue;
    std::vector<TP> poly(tri, tri + 3);
    for (int side = 0; side < 2 && poly.size() >= 3; side++) {
      const float limit = side == 0 ? lo : hi;
      auto inside = [&](const TP& q) { return side == 0 ? q.t >= limit : q.t <= limit; };
      std::vector<TP> clipped;
      for (size_t i = 0; i < poly.size(); i++) {
        const TP& p = poly[i];
        const TP& q = poly[(i + 1) % poly.size()];
        if (inside(p)) clipped.push_back(p);
        if (inside(p) != inside(q)) {
          const float f = (limit - p.t) / (q.t - p.t);
          clipped.push_back({{p.p.x + (q.p.x - p.p.x) * f, p.p.y + (q.p.y - p.p.y) * f}, limit});
        }
      }
      poly = std::move(clipped);
    }
    if (poly.size() >= 3) emitFan(poly);
  }
}

// The edge that fades out: from the outline (opaque) to `outer` pixels beyond
// it along the outward normals, so shapes get antialiased without MSAA.
void Canvas::fringe(const std::vector<Pt>& ring, bool closed, const Paint& paint, float inner, float outer) {
  const size_t n = ring.size();
  if (n < 2) return;
  float area = 0;
  for (size_t i = 0; i < n; i++) area += cross(ring[i].x, ring[i].y, ring[(i + 1) % n].x, ring[(i + 1) % n].y);
  const float orient = area >= 0 ? 1.0f : -1.0f;
  auto edgeNormal = [&](size_t i) {
    const Pt a = ring[i], b = ring[(i + 1) % n];
    const float dx = b.x - a.x, dy = b.y - a.y;
    float l = std::hypot(dx, dy);
    if (l <= 0) l = 1;
    return Pt{dy / l * orient, -dx / l * orient};
  };
  std::vector<Pt> normals(n);
  for (size_t i = 0; i < n; i++) {
    const bool first = i == 0 && !closed, last = i == n - 1 && !closed;
    const Pt n0 = edgeNormal(first ? 0 : (i + n - 1) % n), n1 = edgeNormal(last ? n - 2 : i);
    Pt m{n0.x + n1.x, n0.y + n1.y};
    float l = std::hypot(m.x, m.y);
    if (l < 1e-4f) m = n1, l = 1;
    m.x /= l, m.y /= l;
    const float k = 1 / std::max(0.25f, m.x * n1.x + m.y * n1.y);  // miter, at most 4x
    normals[i] = {m.x * k, m.y * k};
  }
  const size_t segs = closed ? n : n - 1;
  for (size_t i = 0; i < segs; i++) {
    const size_t j = (i + 1) % n;
    const Pt ai{ring[i].x + normals[i].x * inner, ring[i].y + normals[i].y * inner};
    const Pt aj{ring[j].x + normals[j].x * inner, ring[j].y + normals[j].y * inner};
    const Pt bi{ring[i].x + normals[i].x * outer, ring[i].y + normals[i].y * outer};
    const Pt bj{ring[j].x + normals[j].x * outer, ring[j].y + normals[j].y * outer};
    const Color ci = paintAt(paint, paintT(paint, ring[i])), cj = paintAt(paint, paintT(paint, ring[j]));
    out_.insert(out_.end(), {vtx(ai, ci), vtx(bi, TRANSPARENT), vtx(bj, TRANSPARENT), vtx(ai, ci),
                             vtx(bj, TRANSPARENT), vtx(aj, cj)});
  }
}

void Canvas::fillSubs(const std::vector<Sub>& subs, const Paint& paint) {
  for (const Sub& sub : subs) {
    const std::vector<Pt>& p = sub.pts;
    const size_t n = p.size();
    if (n < 3) continue;
    float area = 0;
    for (size_t i = 0; i < n; i++) area += cross(p[i].x, p[i].y, p[(i + 1) % n].x, p[(i + 1) % n].y);
    if (std::fabs(area) < 1e-4f) continue;
    const float orient = area > 0 ? 1.0f : -1.0f;
    auto turn = [&](size_t a, size_t b, size_t c) {
      return cross(p[b].x - p[a].x, p[b].y - p[a].y, p[c].x - p[b].x, p[c].y - p[b].y) * orient;
    };
    bool convex = true;
    for (size_t i = 0; i < n && convex; i++) convex = turn(i, (i + 1) % n, (i + 2) % n) >= -1e-3f;
    if (convex) {
      for (size_t i = 1; i + 1 < n; i++) fillTriangle(p[0], p[i], p[i + 1], paint);
    } else {
      // ear clipping: few vertices here, O(n^2) is fine
      std::vector<size_t> idx(n);
      for (size_t i = 0; i < n; i++) idx[i] = i;
      while (idx.size() > 3) {
        bool clipped = false;
        for (size_t k = 0; k < idx.size(); k++) {
          const size_t a = idx[(k + idx.size() - 1) % idx.size()], b = idx[k], c = idx[(k + 1) % idx.size()];
          if (turn(a, b, c) <= 0) continue;  // reflex
          bool empty = true;
          for (size_t m : idx) {
            if (m == a || m == b || m == c) continue;
            if (turn(a, b, m) > 0 && turn(b, c, m) > 0 && turn(c, a, m) > 0) {
              empty = false;
              break;
            }
          }
          if (!empty) continue;
          fillTriangle(p[a], p[b], p[c], paint);
          idx.erase(idx.begin() + static_cast<long>(k));
          clipped = true;
          break;
        }
        if (!clipped) break;  // degenerate: fan what is left
      }
      for (size_t i = 1; i + 1 < idx.size(); i++) fillTriangle(p[idx[0]], p[idx[i]], p[idx[i + 1]], paint);
    }
    if (aa_) fringe(p, true, paint, 0, 1);
  }
}

// A radial gradient fading to transparent: drawn as the disc it is, whatever
// rectangle it was asked to fill (the game's glows sit inside theirs).
void Canvas::fillRadial(const Paint& paint) {
  const Pt c{paint.x0, paint.y0};
  auto ringAt = [&](float t) { return paint.r0 + (paint.r1 - paint.r0) * t; };
  const auto& s = paint.stops;
  const float rIn = std::max(0.0f, ringAt(s.front().first));
  const int n = std::clamp(static_cast<int>(ringAt(s.back().first) * 0.7f), 12, 64);
  auto at = [&](float r, int i) {
    const float a = kTau * static_cast<float>(i) / static_cast<float>(n);
    return Pt{c.x + std::cos(a) * r, c.y + std::sin(a) * r};
  };
  if (rIn > 0) {
    const Color col = paintAt(paint, s.front().first);
    for (int i = 0; i < n; i++) out_.insert(out_.end(), {vtx(c, col), vtx(at(rIn, i), col), vtx(at(rIn, i + 1), col)});
  }
  for (size_t k = 0; k + 1 < s.size(); k++) {
    const float r0 = std::max(0.0f, ringAt(s[k].first)), r1 = ringAt(s[k + 1].first);
    const Color c0 = paintAt(paint, s[k].first), c1 = paintAt(paint, s[k + 1].first);
    for (int i = 0; i < n; i++) {
      const Pt i0 = at(r0, i), i1 = at(r0, i + 1), o0 = at(r1, i), o1 = at(r1, i + 1);
      out_.insert(out_.end(), {vtx(i0, c0), vtx(o0, c1), vtx(o1, c1), vtx(i0, c0), vtx(o1, c1), vtx(i1, c0)});
    }
  }
}

void Canvas::fillCurrent() {
  std::vector<Sub> subs;
  flatten(subs, false);
  const Paint paint = fillPaint();
  out_.clear();
  if (paint.kind == 2 && paint.stops.back().second.a == 0) fillRadial(paint);
  else fillSubs(subs, paint);
  r_.blend(st_.lighter);
  r_.tris(out_.data(), out_.size());
}

// ---- stroke ----

void Canvas::strokeSubs(const std::vector<Sub>& subs, const Paint& paint0, float width) {
  Paint paint = paint0;
  float hw = width / 2;
  if (width < 1) {  // hairlines: a pixel wide, fainter
    paint.alpha *= std::max(0.0f, width);
    hw = 0.5f;
  }
  const float fr = aa_ ? 1.0f : 0.0f;
  for (const Sub& sub : subs) {
    const std::vector<Pt>& p = sub.pts;
    const size_t n = p.size();
    if (n < 2) continue;
    const bool closed = sub.closed && n >= 3;
    auto segNormal = [&](size_t i) {
      const Pt a = p[i], b = p[(i + 1) % n];
      const float dx = b.x - a.x, dy = b.y - a.y;
      float l = std::hypot(dx, dy);
      if (l <= 0) l = 1;
      return Pt{-dy / l, dx / l};
    };
    std::vector<Pt> off(n);
    for (size_t i = 0; i < n; i++) {
      Pt m;
      if (!closed && i == 0) m = segNormal(0);
      else if (!closed && i == n - 1) m = segNormal(n - 2);
      else {
        const Pt n0 = segNormal((i + n - 1) % n), n1 = segNormal(i);
        m = {n0.x + n1.x, n0.y + n1.y};
        float l = std::hypot(m.x, m.y);
        if (l < 1e-4f) m = n1, l = 1;
        m.x /= l, m.y /= l;
        const float k = 1 / std::max(0.25f, m.x * n1.x + m.y * n1.y);
        m.x *= k, m.y *= k;
      }
      off[i] = m;
    }
    const size_t segs = closed ? n : n - 1;
    for (size_t i = 0; i < segs; i++) {
      const size_t j = (i + 1) % n;
      const Color ci = paintAt(paint, paintT(paint, p[i])), cj = paintAt(paint, paintT(paint, p[j]));
      auto at = [&](size_t k, float d) { return Pt{p[k].x + off[k].x * d, p[k].y + off[k].y * d}; };
      // the line, then the fading fringe on each side
      const float bands[3][2] = {{hw, -hw}, {hw + fr, hw}, {-hw, -hw - fr}};
      for (int b = 0; b < (aa_ ? 3 : 1); b++) {
        const float d0 = bands[b][0], d1 = bands[b][1];
        const Color i0 = b == 1 ? TRANSPARENT : ci, i1 = b == 2 ? TRANSPARENT : ci;
        const Color j0 = b == 1 ? TRANSPARENT : cj, j1 = b == 2 ? TRANSPARENT : cj;
        out_.insert(out_.end(), {vtx(at(i, d0), i0), vtx(at(j, d0), j0), vtx(at(j, d1), j1), vtx(at(i, d0), i0),
                                 vtx(at(j, d1), j1), vtx(at(i, d1), i1)});
      }
    }
  }
}

// Canvas shadowBlur approximated as a soft outline glow around the shape.
void Canvas::glowPass(bool forFill) {
  if (st_.shadowBlur <= 0 || st_.shadowColor.a <= 0) return;
  std::vector<Sub> subs;
  flatten(subs, false);
  const float s = pixelScale();
  const float base = forFill ? 0 : st_.lineWidth;
  Color c = st_.shadowColor;
  c.a *= 0.22f;
  out_.clear();
  for (float k : {1.0f, 0.6f, 0.3f}) strokeSubs(subs, solidPaint(c), (base + st_.shadowBlur * k) * s);
  r_.blend(st_.lighter);
  r_.tris(out_.data(), out_.size());
}

void Canvas::fill() {
  drawCalls_++;
  glowPass(true);
  fillCurrent();
}

void Canvas::stroke() {
  drawCalls_++;
  glowPass(false);
  std::vector<Sub> subs;
  flatten(subs, st_.dashOn > 0);
  out_.clear();
  strokeSubs(subs, solidPaint(st_.strokeColor), st_.lineWidth * pixelScale());
  r_.blend(st_.lighter);
  r_.tris(out_.data(), out_.size());
}

void Canvas::fillRect(float x, float y, float w, float h) {
  drawCalls_++;
  std::vector<Cmd> saved;
  saved.swap(path_);
  path_.push_back({Cmd::Rect, {x, y, w, h}});
  fillCurrent();
  path_.swap(saved);
}

// ---- text ----

void Canvas::fillText(const std::string& text, float x, float y) {
  if (text.empty() || !fs_) return;
  drawCalls_++;
  // glyphs rasterised at their on-screen size, as NanoVG does
  const float scale = std::min(4.0f, std::round(pixelScale() * 100) / 100);
  if (scale <= 0) return;
  const float inv = 1 / scale;
  int align = st_.align == Align::Left    ? FONS_ALIGN_LEFT
              : st_.align == Align::Right ? FONS_ALIGN_RIGHT
                                          : FONS_ALIGN_CENTER;
  align |= st_.baseline == Baseline::Middle ? FONS_ALIGN_MIDDLE : FONS_ALIGN_BASELINE;

  auto pass = [&](Color color, float blur) {
    fonsSetFont(fs_, fontIds_[static_cast<int>(st_.font)]);
    fonsSetSize(fs_, st_.fontPx * scale);
    fonsSetSpacing(fs_, 0);
    fonsSetBlur(fs_, blur);
    fonsSetAlign(fs_, align);
    const Color c = premul(color, st_.alpha);
    const uint8_t cr = u8(c.r), cg = u8(c.g), cb = u8(c.b), ca = u8(c.a);
    out_.clear();
    FONStextIter iter, prev;
    FONSquad q;
    fonsTextIterInit(fs_, &iter, x * scale, y * scale, text.c_str(), nullptr, FONS_GLYPH_BITMAP_REQUIRED);
    prev = iter;
    while (fonsTextIterNext(fs_, &iter, &q)) {
      if (iter.prevGlyphIndex == -1) {
        // atlas full: draw what is queued, then grow it (or start it over)
        r_.blend(st_.lighter);
        r_.tris(out_.data(), out_.size());
        out_.clear();
        flushRenderer();
        int w = 0, h = 0;
        fonsGetAtlasSize(fs_, &w, &h);
        if (w < kAtlasMax || h < kAtlasMax) fonsExpandAtlas(fs_, std::min(kAtlasMax, w * 2), std::min(kAtlasMax, h * 2));
        else fonsResetAtlas(fs_, w, h);
        iter = prev;
        fonsTextIterNext(fs_, &iter, &q);
        if (iter.prevGlyphIndex == -1) break;
      }
      prev = iter;
      const Pt p0 = apply(q.x0 * inv, q.y0 * inv), p1 = apply(q.x1 * inv, q.y0 * inv),
               p2 = apply(q.x1 * inv, q.y1 * inv), p3 = apply(q.x0 * inv, q.y1 * inv);
      const Vtx v0{p0.x, p0.y, q.s0, q.t0, cr, cg, cb, ca}, v1{p1.x, p1.y, q.s1, q.t0, cr, cg, cb, ca},
          v2{p2.x, p2.y, q.s1, q.t1, cr, cg, cb, ca}, v3{p3.x, p3.y, q.s0, q.t1, cr, cg, cb, ca};
      out_.insert(out_.end(), {v0, v1, v2, v0, v2, v3});
    }
    r_.blend(st_.lighter);
    r_.tris(out_.data(), out_.size());
  };

  if (st_.shadowBlur > 0 && st_.shadowColor.a > 0)
    pass(st_.shadowColor, std::min(20.0f, st_.shadowBlur * 0.6f * scale));
  pass(st_.fillGradient && !st_.gradient.stops.empty() ? st_.gradient.stops[0].second : st_.fill, 0);
}
