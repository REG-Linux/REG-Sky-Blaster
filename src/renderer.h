// The GL side: one vertex stream, one shader (vertex colour x atlas alpha),
// one texture (the font atlas, whose white corner serves solid shapes).
// Draw calls only split where the blend changes. Built for weak GLES2 GPUs
// (Mali-400), where a heavier per-pixel shader is what costs.
#pragma once

#include <cstdint>
#include <vector>

struct Vtx {
  float x, y;  // pixels, y down
  float u, v;  // atlas coordinates
  uint8_t r, g, b, a;  // premultiplied
};

class Renderer {
 public:
  bool init(int atlasW, int atlasH);
  void begin(int viewW, int viewH);
  void blend(bool additive);
  // Scales the colour of what follows: the same as a black layer of
  // alpha 1 - k over it, without the extra full-screen pass.
  void dim(float k);
  void tris(const Vtx* v, size_t n);  // n a multiple of 3
  void flush();  // draws what is queued
  int drawCalls() const { return drawCalls_; }  // GL draws since begin()
  size_t vertices() const { return vertices_; }  // drawn since begin()

  // the font atlas (alpha only)
  void atlasResize(int w, int h, const unsigned char* data);
  void atlasUpdate(const int* rect, const unsigned char* data);
  float whiteU() const { return whiteU_; }
  float whiteV() const { return whiteV_; }

 private:
  struct Run {
    bool additive;
    float dim;
    size_t first, count;
  };
  std::vector<Vtx> verts_;
  std::vector<Run> runs_;
  bool additive_ = false;
  float dim_ = 1;
  int viewW_ = 1, viewH_ = 1;
  int atlasW_ = 0, atlasH_ = 0;
  float whiteU_ = 0, whiteV_ = 0;
  unsigned prog_ = 0, tex_ = 0;
  int uView_ = -1, uDim_ = -1;
  int drawCalls_ = 0;
  size_t vertices_ = 0;
};
