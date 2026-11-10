#include "renderer.h"

#include <SDL3/SDL.h>

#include "gl.h"

namespace {

const char* kVert = R"(
#ifdef GL_ES
precision highp float;
#endif
attribute vec2 pos;
attribute vec2 uv;
attribute vec4 col;
uniform vec2 view;
uniform float dim;
varying vec2 vuv;
varying vec4 vcol;
void main() {
  gl_Position = vec4(pos.x / view.x * 2.0 - 1.0, 1.0 - pos.y / view.y * 2.0, 0.0, 1.0);
  vuv = uv;
  vcol = vec4(col.rgb * dim, col.a);
}
)";

const char* kFrag = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform sampler2D atlas;
varying vec2 vuv;
varying vec4 vcol;
void main() { gl_FragColor = vcol * texture2D(atlas, vuv).a; }
)";

GLuint compile(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(s, sizeof log, nullptr, log);
    SDL_Log("shader: %s", log);
  }
  return s;
}

}  // namespace

bool Renderer::init(int atlasW, int atlasH) {
  prog_ = glCreateProgram();
  GLuint vs = compile(GL_VERTEX_SHADER, kVert), fs = compile(GL_FRAGMENT_SHADER, kFrag);
  glAttachShader(prog_, vs);
  glAttachShader(prog_, fs);
  glBindAttribLocation(prog_, 0, "pos");
  glBindAttribLocation(prog_, 1, "uv");
  glBindAttribLocation(prog_, 2, "col");
  glLinkProgram(prog_);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(prog_, GL_LINK_STATUS, &ok);
  if (!ok) return false;
  uView_ = glGetUniformLocation(prog_, "view");
  uDim_ = glGetUniformLocation(prog_, "dim");
  glUseProgram(prog_);
  glUniform1i(glGetUniformLocation(prog_, "atlas"), 0);
  glGenTextures(1, &tex_);
  atlasResize(atlasW, atlasH, nullptr);
  return true;
}

void Renderer::atlasResize(int w, int h, const unsigned char* data) {
  atlasW_ = w, atlasH_ = h;
  // fontstash keeps a 2x2 white block at the origin: solid shapes sample it
  whiteU_ = 1.0f / w, whiteV_ = 1.0f / h;
  glBindTexture(GL_TEXTURE_2D, tex_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, w, h, 0, GL_ALPHA, GL_UNSIGNED_BYTE, data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// GLES2 has no row stride for uploads: send the dirty rows whole.
void Renderer::atlasUpdate(const int* rect, const unsigned char* data) {
  const int y0 = rect[1], y1 = rect[3];
  if (y1 <= y0) return;
  glBindTexture(GL_TEXTURE_2D, tex_);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y0, atlasW_, y1 - y0, GL_ALPHA, GL_UNSIGNED_BYTE, data + y0 * atlasW_);
}

void Renderer::begin(int viewW, int viewH) {
  viewW_ = viewW, viewH_ = viewH;
  drawCalls_ = 0;
  vertices_ = 0;
  verts_.clear();
  runs_.clear();
  additive_ = false;
  dim_ = 1;
}

void Renderer::blend(bool additive) { additive_ = additive; }

void Renderer::dim(float k) { dim_ = k; }

void Renderer::tris(const Vtx* v, size_t n) {
  if (n == 0) return;
  if (runs_.empty() || runs_.back().additive != additive_ || runs_.back().dim != dim_)
    runs_.push_back({additive_, dim_, verts_.size(), 0});
  verts_.insert(verts_.end(), v, v + n);
  runs_.back().count += n;
}

void Renderer::flush() {
  if (verts_.empty()) return;
  glUseProgram(prog_);
  glUniform2f(uView_, static_cast<float>(viewW_), static_cast<float>(viewH_));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex_);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_SCISSOR_TEST);
  glEnable(GL_BLEND);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), &verts_[0].x);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), &verts_[0].u);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx), &verts_[0].r);
  for (const Run& run : runs_) {
    if (run.additive) glBlendFunc(GL_ONE, GL_ONE);
    else glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUniform1f(uDim_, run.dim);
    glDrawArrays(GL_TRIANGLES, static_cast<GLint>(run.first), static_cast<GLsizei>(run.count));
    drawCalls_++;
    vertices_ += run.count;
  }
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glDisableVertexAttribArray(2);
  verts_.clear();
  runs_.clear();
}
