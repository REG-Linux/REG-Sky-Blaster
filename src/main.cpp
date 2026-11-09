// Sky Blaster: window, GL context, fixed-step loop, rotation.
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "audio.h"
#include "canvas.h"
#include "game/game.h"
#include "gl.h"
#include "input.h"
#include "nanovg.h"
#include "nanovg_gl.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifndef SKYBLASTER_DATADIR
#define SKYBLASTER_DATADIR ""
#endif

namespace {

struct Options {
  int rotate = 0;      // degrees counter-clockwise: 0, 90, 180, 270
  bool turned = false; // controls follow the turned device (P1)
  bool windowed = false;
  int winW = 0, winH = 0;  // --window WxH (implies --windowed)
  bool mute = false;
  bool demo = false;   // start a run at once (testing): Practice, invincible
  int demoWave = 1;
  int demoWeapon = 0;
  std::string saveDir, dataDir, shot;
  int shotFrame = 120;
};

void usage() {
  SDL_Log(
      "usage: sky-blaster [--rotate 0|90|180|270] [--turned] [--windowed] [--window WxH] [--save-dir DIR]\n"
      "                   [--data-dir DIR] [--mute] [--shot FILE.png] [--shot-frame N]\n"
      "                   [--demo] [--demo-wave N] [--demo-weapon 0-4]");
}

bool parse(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
    if (a == "--rotate") {
      const char* v = value();
      if (!v) return false;
      o.rotate = ((std::atoi(v) % 360) + 360) % 360;
      if (o.rotate % 90) return false;
    } else if (a == "--turned") o.turned = true;
    else if (a == "--windowed") o.windowed = true;
    else if (a == "--window") {
      const char* v = value();
      if (!v || std::sscanf(v, "%dx%d", &o.winW, &o.winH) != 2 || o.winW <= 0 || o.winH <= 0) return false;
      o.windowed = true;
    }
    else if (a == "--mute") o.mute = true;
    else if (a == "--demo") o.demo = true;
    else if (a == "--save-dir" || a == "--data-dir" || a == "--shot" || a == "--shot-frame" ||
             a == "--demo-wave" || a == "--demo-weapon") {
      const char* v = value();
      if (!v) return false;
      if (a == "--save-dir") o.saveDir = v;
      else if (a == "--data-dir") o.dataDir = v;
      else if (a == "--shot") o.shot = v;
      else if (a == "--demo-wave") o.demoWave = std::atoi(v);
      else if (a == "--demo-weapon") o.demoWeapon = std::atoi(v) % static_cast<int>(WeaponId::Count);
      else o.shotFrame = std::atoi(v);
    } else {
      return false;
    }
  }
  return true;
}

std::string findDataDir(const Options& o) {
  if (!o.dataDir.empty()) return o.dataDir;
  const char* base = SDL_GetBasePath();
  std::vector<std::string> tries;
  if (base) {
    tries.push_back(std::string(base) + "data");
    tries.push_back(std::string(base) + "../data");
  }
  if (*SKYBLASTER_DATADIR) tries.push_back(SKYBLASTER_DATADIR);
  for (const std::string& d : tries) {
    SDL_PathInfo info;
    if (SDL_GetPathInfo((d + "/fonts/DejaVuSans.ttf").c_str(), &info)) return d;
  }
  return tries.empty() ? "data" : tries.back();
}

void saveShot(const std::string& path, int w, int h) {
  std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  std::vector<unsigned char> flipped(px.size());
  for (int y = 0; y < h; y++)
    std::memcpy(&flipped[static_cast<size_t>(y) * w * 4], &px[static_cast<size_t>(h - 1 - y) * w * 4],
                static_cast<size_t>(w) * 4);
  stbi_write_png(path.c_str(), w, h, 4, flipped.data(), w * 4);
}

// The screen area the field gets, in the field's own orientation.
void fieldArea(SDL_Window* win, int rotate, float& aw, float& ah) {
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(win, &w, &h);
  aw = static_cast<float>(rotate == 90 || rotate == 270 ? h : w);
  ah = static_cast<float>(rotate == 90 || rotate == 270 ? w : h);
}

// A new screen shape: re-lay the field (stars for its area, ships inside).
void refit(GameData& g) {
  applyChapterToGame(g, g.chapterId);
  for (Player* p : activePlayers(g)) {
    p->x = std::fmin(std::fmax(p->x, p->width / 2), CANVAS_W - p->width / 2);
    p->y = std::fmin(std::fmax(p->y, p->height / 2), CANVAS_H - p->height / 2);
  }
  if (g.state == GameState::Menu) g.player = createPlayer(g.selectedAircraft, g.selectedWeapon);
}

// Fits the play field into the drawable, turned by `rotate`.
void applyView(NVGcontext* vg, int w, int h, int rotate) {
  float aw = static_cast<float>(w), ah = static_cast<float>(h);
  switch (rotate) {
    case 90:  // top of the field to the screen's left
      nvgTranslate(vg, 0, ah);
      nvgRotate(vg, -PI / 2);
      std::swap(aw, ah);
      break;
    case 180:
      nvgTranslate(vg, aw, ah);
      nvgRotate(vg, PI);
      break;
    case 270:
      nvgTranslate(vg, aw, 0);
      nvgRotate(vg, PI / 2);
      std::swap(aw, ah);
      break;
    default: break;
  }
  float s = std::fmin(aw / CANVAS_W, ah / CANVAS_H);
  nvgTranslate(vg, (aw - CANVAS_W * s) / 2, (ah - CANVAS_H * s) / 2);
  nvgScale(vg, s, s);
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  if (!parse(argc, argv, opt)) {
    usage();
    return 2;
  }
  SDL_SetAppMetadata("Sky Blaster", "1.0", "org.reglinux.skyblaster");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
    SDL_Log("SDL_Init: %s", SDL_GetError());
    return 1;
  }

#if defined(SKYBLASTER_GL2)
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);  // NanoVG fills concave paths through the stencil
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  const bool sideways = opt.rotate == 90 || opt.rotate == 270;
  int ww = sideways ? 700 : 400, wh = sideways ? 400 : 700;
  if (opt.winW > 0) ww = opt.winW, wh = opt.winH;
  SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
  if (!opt.windowed) flags |= SDL_WINDOW_FULLSCREEN;
  SDL_Window* win = SDL_CreateWindow("Sky Blaster", ww, wh, flags);
  if (!win) {
    SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
    return 1;
  }
  SDL_GLContext gl = SDL_GL_CreateContext(win);
  if (!gl) {
    SDL_Log("SDL_GL_CreateContext: %s", SDL_GetError());
    return 1;
  }
  SDL_GL_MakeCurrent(win, gl);
  SDL_GL_SetSwapInterval(1);
  if (!opt.windowed) SDL_HideCursor();

#if defined(SKYBLASTER_GL2)
  NVGcontext* vg = nvgCreateGL2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
#else
  NVGcontext* vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
#endif
  if (!vg) {
    SDL_Log("NanoVG init failed");
    return 1;
  }
  Canvas canvas(vg);
  const std::string dataDir = findDataDir(opt);
  if (!canvas.loadFonts(dataDir + "/fonts")) {
    SDL_Log("fonts not found under %s/fonts", dataDir.c_str());
    return 1;
  }

  std::string saveDir = opt.saveDir;
  if (saveDir.empty()) {
    char* pref = SDL_GetPrefPath("REG-Linux", "sky-blaster");
    saveDir = pref ? pref : ".";
    SDL_free(pref);
  }
  progressInit(saveDir);
  if (!audio::init()) SDL_Log("running without sound");
  audio::setMuted(opt.mute);

  float aw, ah;
  fieldArea(win, opt.rotate, aw, ah);
  setFieldSize(aw, ah);
  GameData game = createGameData();
  InputState p1;
  Input input(opt.turned);
  if (opt.demo) {
    game.gameMode = GameMode::Practice;
    game.practiceStartWave = opt.demoWave;
    startRun(game);
    game.player.weaponId = static_cast<WeaponId>(opt.demoWeapon);
  }

  uint64_t last = SDL_GetTicksNS();
  double accumulator = 0;
  int frame = 0;
  bool running = true;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e))
      if (!input.handleEvent(e, game, p1)) running = false;
    input.poll(game, p1);
    fieldArea(win, opt.rotate, aw, ah);
    if (setFieldSize(aw, ah)) refit(game);

    uint64_t now = SDL_GetTicksNS();
    double elapsed = std::fmin((now - last) / 1e9, MAX_FRAME_DELTA_S);
    last = now;
    if (!opt.shot.empty()) elapsed = FIXED_TIMESTEP_S;  // deterministic captures
    accumulator += elapsed;
    while (accumulator >= FIXED_TIMESTEP_S) {
      if (game.state == GameState::Playing) update(game, p1, static_cast<float>(FIXED_TIMESTEP_S));
      else updateBackground(game, static_cast<float>(FIXED_TIMESTEP_S));
      accumulator -= FIXED_TIMESTEP_S;
    }

    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(win, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    nvgBeginFrame(vg, static_cast<float>(w), static_cast<float>(h), 1);
    applyView(vg, w, h, opt.rotate);
    renderGame(canvas, game, static_cast<uint32_t>(SDL_GetTicks()));
    // letterbox: hide what shakes or spills past the field
    canvas.fillStyle(C("#000"));
    canvas.fillRect(-4000, -4000, 4000, 8000 + CANVAS_H);
    canvas.fillRect(CANVAS_W, -4000, 4000, 8000 + CANVAS_H);
    canvas.fillRect(0, -4000, CANVAS_W, 4000);
    canvas.fillRect(0, CANVAS_H, CANVAS_W, 4000);
    nvgEndFrame(vg);

    if (!opt.shot.empty() && ++frame >= opt.shotFrame) {
      saveShot(opt.shot, w, h);
      running = false;
    }
    SDL_GL_SwapWindow(win);
  }

  nameEntryConfirm(game);  // quitting mid-entry still keeps the score
  audio::shutdown();
#if defined(SKYBLASTER_GL2)
  nvgDeleteGL2(vg);
#else
  nvgDeleteGLES2(vg);
#endif
  SDL_GL_DestroyContext(gl);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
