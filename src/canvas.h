// A small subset of the HTML canvas 2D API, so the original drawing code
// ports line by line. Shapes are flattened and triangulated here, gradients
// become vertex colours and edges get a one-pixel fading fringe, all handed
// to a Renderer that draws a frame in a few calls (see renderer.h).
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "color.h"
#include "renderer.h"

class Prims;
struct FONScontext;

enum class Font { Sans, Bold, Mono };
enum class Align { Left, Center, Right };
enum class Baseline { Alphabetic, Middle };

struct Gradient {
  bool radial = false;
  float x0 = 0, y0 = 0, r0 = 0, x1 = 0, y1 = 0, r1 = 0;
  std::vector<std::pair<float, Color>> stops;

  static Gradient linear(float x0, float y0, float x1, float y1);
  static Gradient radialGradient(float x0, float y0, float r0, float x1, float y1, float r1);
  Gradient& add(float offset, Color c) {
    stops.emplace_back(offset, c);
    return *this;
  }
};

class Canvas {
 public:
  explicit Canvas(Renderer& renderer);
  ~Canvas();
  bool loadFonts(const std::string& dir);
  void setAntiAlias(bool on) { aa_ = on; }

  void beginFrame(int w, int h);
  void endFrame();
  // What `prims` queued, here in the drawing order, with the current transform.
  void drawPrims(Prims& prims);
  int drawCalls() const { return drawCalls_; }  // fills, strokes and texts this frame

  // Large plain layers (dimming, flashes, tints); fill colour and alpha as set.
  void tintRect(float x, float y, float w, float h);
  void setPrims(Prims* prims) { prims_ = prims; }
  // darkens what follows (Renderer::dim)
  void dim(float k);

  void save();
  void restore();
  void resetTransform();
  void translate(float x, float y);
  void rotate(float a);
  void scale(float x, float y);
  void scale(float s) { scale(s, s); }

  void beginPath();
  void moveTo(float x, float y);
  void lineTo(float x, float y);
  void closePath();
  void arc(float x, float y, float r, float a0, float a1);
  void ellipse(float x, float y, float rx, float ry);
  void rect(float x, float y, float w, float h);
  void roundRect(float x, float y, float w, float h, float r);
  void roundRect(float x, float y, float w, float h, float tl, float tr, float br, float bl);
  void fill();
  void stroke();
  void fillRect(float x, float y, float w, float h);
  void fillText(const std::string& text, float x, float y);

  void fillStyle(Color c);
  void fillStyle(const Gradient& g);
  void strokeStyle(Color c);
  void lineWidth(float w);
  void globalAlpha(float a);
  void lighter(bool on);  // globalCompositeOperation 'lighter' vs 'source-over'
  void font(float px, Font f = Font::Sans);
  void textAlign(Align a);
  void textBaseline(Baseline b);
  void shadow(Color c, float blur);
  void lineDash(float on, float off);  // 0, 0 clears

 private:
  struct Cmd {
    enum Kind { Move, Line, Close, Arc, Ellipse, Rect, RoundRect } kind;
    float v[8];
  };
  struct State {
    float xf[6] = {1, 0, 0, 1, 0, 0};  // a b c d e f: x' = a x + c y + e, y' = b x + d y + f
    bool fillGradient = false;
    Color fill{0, 0, 0, 1};
    Gradient gradient;
    Color strokeColor{0, 0, 0, 1};
    float lineWidth = 1;
    float alpha = 1;
    float fontPx = 10;
    Font font = Font::Sans;
    Align align = Align::Left;
    Baseline baseline = Baseline::Alphabetic;
    Color shadowColor = TRANSPARENT;
    float shadowBlur = 0;
    float dashOn = 0, dashOff = 0;
    bool lighter = false;
  };
  struct Pt {
    float x, y;
  };
  struct Sub {
    std::vector<Pt> pts;
    bool closed = false;
  };
  // the colour source of one fill or stroke, in pixel space
  struct Paint {
    int kind = 0;  // 0 solid, 1 linear, 2 radial
    Color solid;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, r0 = 0, r1 = 0;
    std::vector<std::pair<float, Color>> stops;
    float alpha = 1;
  };

  Pt apply(float x, float y) const;
  float pixelScale() const;
  void flatten(std::vector<Sub>& subs, bool dashed) const;
  Paint fillPaint() const;
  Paint solidPaint(Color c) const;
  float paintT(const Paint& p, Pt q) const;
  Vtx vtx(Pt q, Color premul) const;
  Color paintAt(const Paint& p, float t) const;  // premultiplied, alpha applied
  void fillSubs(const std::vector<Sub>& subs, const Paint& paint);
  void fillTriangle(Pt a, Pt b, Pt c, const Paint& paint);
  void fillRadial(const Paint& paint);
  void fringe(const std::vector<Pt>& ring, bool closed, const Paint& paint, float inner, float outer);
  void strokeSubs(const std::vector<Sub>& subs, const Paint& paint, float width);
  void glowPass(bool forFill);
  void fillCurrent();
  void flushRenderer();

  Renderer& r_;
  FONScontext* fs_ = nullptr;
  int fontIds_[3] = {-1, -1, -1};
  bool aa_ = true;
  std::vector<Cmd> path_;
  std::vector<State> stack_;
  State st_;
  int frameW_ = 0, frameH_ = 0;
  int drawCalls_ = 0;
  Prims* prims_ = nullptr;
  std::vector<Vtx> out_;
};
