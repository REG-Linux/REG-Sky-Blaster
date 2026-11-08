// A small subset of the HTML canvas 2D API on top of NanoVG, so the
// original drawing code ports line by line.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "color.h"

struct NVGcontext;

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
  explicit Canvas(NVGcontext* vg);
  bool loadFonts(const std::string& dir);
  NVGcontext* nvg() const { return vg_; }

  void save();
  void restore();
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
  };

  void replay(bool dashed);
  void fillCurrent();
  void glowPass(bool forFill);
  void applyFont();

  NVGcontext* vg_;
  std::vector<Cmd> path_;
  std::vector<State> stack_;
  State st_;
  int fontIds_[3] = {-1, -1, -1};
};
