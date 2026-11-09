// Sky Blaster: window, GL context, fixed-step loop, rotation.
#include <SDL3/SDL.h>

#include <algorithm>
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
#include "prims.h"
#include "renderer.h"

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
  bool fps = false;      // log frame rate and time spent per frame
  bool showFps = false;  // frame counter on screen
  bool vsync = true;
  int msaa = 0;  // multisample anti-aliasing samples (0: the renderer's own edge fade)
  bool demo = false;   // start a run at once (testing): Practice, invincible
  int demoWave = 1;
  int demoWeapon = 0;
  bool demoMortal = false;
  std::string saveDir, dataDir, shot;
  int shotFrame = 120;
};

void usage() {
  SDL_Log(
      "usage: sky-blaster [--rotate 0|90|180|270] [--turned] [--windowed] [--window WxH] [--save-dir DIR]\n"
      "                   [--data-dir DIR] [--mute] [--show-fps] [--shot FILE.png] [--shot-frame N]\n"
      "                   [--demo] [--demo-wave N] [--demo-weapon 0-4] [--demo-mortal]");
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
    else if (a == "--fps") o.fps = true;
    else if (a == "--show-fps") o.showFps = true;
    else if (a == "--no-vsync") o.vsync = false;
    else if (a == "--msaa") {
      const char* v = value();
      if (!v) return false;
      o.msaa = std::clamp(std::atoi(v), 0, 16);
    }

    else if (a == "--demo") o.demo = true;
    else if (a == "--demo-mortal") o.demoMortal = true;
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
void applyView(Canvas& ctx, int w, int h, int rotate) {
  float aw = static_cast<float>(w), ah = static_cast<float>(h);
  switch (rotate) {
    case 90:  // top of the field to the screen's left
      ctx.translate(0, ah);
      ctx.rotate(-PI / 2);
      std::swap(aw, ah);
      break;
    case 180:
      ctx.translate(aw, ah);
      ctx.rotate(PI);
      break;
    case 270:
      ctx.translate(aw, 0);
      ctx.rotate(PI / 2);
      std::swap(aw, ah);
      break;
    default: break;
  }
  float s = std::fmin(aw / CANVAS_W, ah / CANVAS_H);
  ctx.translate((aw - CANVAS_W * s) / 2, (ah - CANVAS_H * s) / 2);
  ctx.scale(s, s);
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
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  if (opt.msaa > 0) {
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, opt.msaa);
  }

  const bool sideways = opt.rotate == 90 || opt.rotate == 270;
  int ww = sideways ? 700 : 400, wh = sideways ? 400 : 700;
  if (opt.winW > 0) ww = opt.winW, wh = opt.winH;
  // fullscreen takes the mode the screen is in (the frontend's): a KMS
  // backend otherwise picks a mode near the window size
  const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
  if (!opt.windowed && desk) ww = desk->w, wh = desk->h;
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
  if (!opt.windowed) SDL_SetWindowFullscreenMode(win, nullptr);  // desktop mode, no switch
  SDL_GL_SetSwapInterval(opt.vsync ? 1 : 0);
  if (!opt.windowed) SDL_HideCursor();

  GLint samples = 0;
  glGetIntegerv(GL_SAMPLES, &samples);
  Renderer renderer;
  if (!renderer.init(1024, 1024)) {
    SDL_Log("renderer: shader failed");
    return 1;
  }
  Canvas canvas(renderer);
  canvas.setAntiAlias(samples <= 1);  // multisampling smooths the edges itself
  Prims prims;
  canvas.setPrims(&prims);
  {
    int dw = 0, dh = 0;
    SDL_GetWindowSizeInPixels(win, &dw, &dh);
    SDL_Log("video %s, screen %dx%d@%.0f, drawing %dx%d, GL %s, %d samples", SDL_GetCurrentVideoDriver(),
            desk ? desk->w : 0, desk ? desk->h : 0, desk ? desk->refresh_rate : 0.0, dw, dh,
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)), samples);
  }
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
    if (opt.demoMortal) game.practiceInvincible = false;  // reaches game over
  }

  uint64_t last = SDL_GetTicksNS();
  double accumulator = 0;
  // --fps: frames, and where the time goes (simulation, drawing, swap)
  uint64_t fpsStart = last, simNs = 0, drawNs = 0, swapNs = 0;
  int fpsFrames = 0;
  size_t maxVerts = 0;
  // --show-fps: the counter on screen, refreshed twice a second
  game.showFps = opt.showFps;
  uint64_t shownStart = last;
  int shownFrames = 0;
  char shownFps[16] = "";
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
    const uint64_t simmed = SDL_GetTicksNS();

    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(win, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    canvas.beginFrame(w, h);
    applyView(canvas, w, h, opt.rotate);
    renderGame(canvas, prims, game, static_cast<uint32_t>(SDL_GetTicks()));
    canvas.resetTransform();  // the field's own view, without the shake
    applyView(canvas, w, h, opt.rotate);
    // letterbox: hide what shakes or spills past the field
    canvas.fillStyle(C("#000"));
    canvas.fillRect(-4000, -4000, 4000, 8000 + CANVAS_H);
    canvas.fillRect(CANVAS_W, -4000, 4000, 8000 + CANVAS_H);
    canvas.fillRect(0, -4000, CANVAS_W, 4000);
    canvas.fillRect(0, CANVAS_H, CANVAS_W, 4000);
    if (opt.showFps) {
      if (++shownFrames, now - shownStart >= 500000000ull) {
        std::snprintf(shownFps, sizeof shownFps, "%.0f FPS", shownFrames * 1e9 / (now - shownStart));
        shownStart = now, shownFrames = 0;
      }
      canvas.font(11, Font::Bold);
      canvas.textAlign(Align::Left);
      canvas.fillStyle(C("#4f8"));
      canvas.fillText(shownFps, 10, 14);
    }
    const int shapes = canvas.drawCalls();
    canvas.endFrame();
    const int draws = renderer.drawCalls();
    maxVerts = std::max(maxVerts, renderer.vertices());

    if (!opt.shot.empty() && ++frame >= opt.shotFrame) {
      saveShot(opt.shot, w, h);
      running = false;
    }
    const uint64_t drawn = SDL_GetTicksNS();
    SDL_GL_SwapWindow(win);
    if (opt.fps) {
      const uint64_t swapped = SDL_GetTicksNS();
      simNs += simmed - now, drawNs += drawn - simmed, swapNs += swapped - drawn;
      if (++fpsFrames, swapped - fpsStart >= 1000000000ull) {
        const double n = fpsFrames;
        SDL_Log("fps %.1f  per frame: sim %.1f ms, draw %.1f ms, swap %.1f ms, %d shapes in %d GL draws, "
                "up to %zu vertices, %zu particles",
                n * 1e9 / (swapped - fpsStart), simNs / n / 1e6, drawNs / n / 1e6, swapNs / n / 1e6, shapes, draws,
                maxVerts, game.particles.size());
        fpsStart = swapped, fpsFrames = 0, simNs = drawNs = swapNs = 0, maxVerts = 0;
      }
    }
  }

  nameEntryConfirm(game);  // quitting mid-entry still keeps the score
  audio::shutdown();
  SDL_GL_DestroyContext(gl);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
