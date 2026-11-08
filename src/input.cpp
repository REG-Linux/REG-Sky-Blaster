#include "input.h"

#include <cmath>

namespace {

constexpr float DEADZONE = 0.25f;  // GAMEPAD_DEADZONE in gamepad.ts
constexpr float EDGE_ON = 0.5f, EDGE_OFF = 0.3f;

void menuSound() { sfx::playMenuSelect(); }

}  // namespace

Input::~Input() {
  for (Pad& p : pads_) SDL_CloseGamepad(p.pad);
}

int Input::indexOf(SDL_JoystickID id) const {
  for (size_t i = 0; i < pads_.size(); i++)
    if (pads_[i].id == id) return static_cast<int>(i);
  return -1;
}

// SDL names the face buttons by position: south, east, west, north.
Input::Button Input::logical(int padIndex, SDL_GamepadButton b) const {
  const bool turn = turned_ && padIndex == 0;
  switch (b) {
    // tate.lua FACE: north acts as east, east as south, south as west, west as north
    case SDL_GAMEPAD_BUTTON_SOUTH: return turn ? West : South;
    case SDL_GAMEPAD_BUTTON_EAST: return turn ? South : East;
    case SDL_GAMEPAD_BUTTON_WEST: return turn ? North : West;
    case SDL_GAMEPAD_BUTTON_NORTH: return turn ? East : North;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return L1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return R1;
    case SDL_GAMEPAD_BUTTON_START: return Start;
    case SDL_GAMEPAD_BUTTON_BACK: return Back;
    // the d-pad turns with the picture: its left now points up
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return turn ? Right : Up;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return turn ? Down : Right;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return turn ? Left : Down;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return turn ? Up : Left;
    default: return Count;
  }
}

// Movement stick, deadzoned. Turned: the right stick, now under the right
// thumb, steers (tate.lua STICK); the left one turns the same way.
void Input::axes(int padIndex, float& x, float& y) const {
  SDL_Gamepad* p = pads_[static_cast<size_t>(padIndex)].pad;
  auto read = [&](SDL_GamepadAxis a) {
    float v = SDL_GetGamepadAxis(p, a) / 32767.0f;
    return std::fabs(v) < DEADZONE ? 0.0f : v;
  };
  if (turned_ && padIndex == 0) {
    float rx = read(SDL_GAMEPAD_AXIS_RIGHTX), ry = read(SDL_GAMEPAD_AXIS_RIGHTY);
    float lx = read(SDL_GAMEPAD_AXIS_LEFTX), ly = read(SDL_GAMEPAD_AXIS_LEFTY);
    float px = std::fabs(rx) + std::fabs(ry) > 0 ? rx : lx;
    float py = std::fabs(rx) + std::fabs(ry) > 0 ? ry : ly;
    x = -py;
    y = px;
    return;
  }
  x = read(SDL_GAMEPAD_AXIS_LEFTX);
  y = read(SDL_GAMEPAD_AXIS_LEFTY);
}

bool Input::held(int padIndex, Button b) const {
  if (padIndex < 0 || padIndex >= static_cast<int>(pads_.size())) return false;
  SDL_Gamepad* p = pads_[static_cast<size_t>(padIndex)].pad;
  for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++) {
    auto sb = static_cast<SDL_GamepadButton>(i);
    if (logical(padIndex, sb) == b && SDL_GetGamepadButton(p, sb)) return true;
  }
  if (b == L2 || b == R2) {
    auto a = b == L2 ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    return SDL_GetGamepadAxis(p, a) / 32767.0f > EDGE_ON;
  }
  return false;
}

// One button press, routed by game state. padIndex 1 is P2.
void Input::press(GameData& g, InputState& p1, int padIndex, Button b) {
  InputState& in = padIndex == 1 ? g.p2Input : p1;
  switch (g.state) {
    case GameState::Menu: {
      if (padIndex > 1) return;
      const bool back = held(padIndex, Back);
      switch (b) {
        case Up: cycleGameModeSelection(g, -1), menuSound(); break;
        case Down: cycleGameModeSelection(g, 1), menuSound(); break;
        case Left:
        case Right: {
          int dir = b == Left ? -1 : 1;
          if (back && isPracticeMode(g)) cyclePracticeStartWave(g, dir);
          else cycleAircraftSelection(g, dir);
          menuSound();
          break;
        }
        case L1: cycleWeaponSelection(g, -1), menuSound(); break;
        case R1: cycleWeaponSelection(g, 1), menuSound(); break;
        case L2: cycleDifficultySelection(g, -1), menuSound(); break;
        case R2: cycleDifficultySelection(g, 1), menuSound(); break;
        case North: {
          bool a = tryUnlockSelectedAircraft(g);
          bool w = tryUnlockSelectedWeapon(g);
          if (a || w) menuSound();
          break;
        }
        case South:
        case Start:
          if (startRun(g)) menuSound();
          break;
        default: break;
      }
      break;
    }
    case GameState::Playing:
      if (padIndex > 1) return;
      if (b == Start) togglePause(g);
      else if (b == East) in.bomb = true;
      else if (b == West) in.skill = true;
      else if (b == North) {
        g.autoFire = !g.autoFire;
        menuSound();
      } else if (b == Back && isPracticeMode(g)) {
        g.practiceInvincible = !g.practiceInvincible;
        menuSound();
      }
      break;
    case GameState::Paused:
      if (b == Start) togglePause(g);
      else if (b == Back) {
        returnToMenu(g);
        menuSound();
      }
      break;
    case GameState::GameOver:
      if (b == South || b == Start) {
        if (startRun(g)) menuSound();
      } else if (b == East) {
        returnToMenu(g);
        menuSound();
      }
      break;
  }
}

bool Input::handleEvent(const SDL_Event& e, GameData& g, InputState& p1) {
  switch (e.type) {
    case SDL_EVENT_QUIT: return false;
    case SDL_EVENT_GAMEPAD_ADDED:
      if (indexOf(e.gdevice.which) < 0) {
        if (SDL_Gamepad* p = SDL_OpenGamepad(e.gdevice.which)) pads_.push_back({p, e.gdevice.which});
      }
      break;
    case SDL_EVENT_GAMEPAD_REMOVED: {
      int i = indexOf(e.gdevice.which);
      if (i >= 0) {
        SDL_CloseGamepad(pads_[static_cast<size_t>(i)].pad);
        pads_.erase(pads_.begin() + i);
      }
      break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
      int i = indexOf(e.gbutton.which);
      Button b = i >= 0 ? logical(i, static_cast<SDL_GamepadButton>(e.gbutton.button)) : Count;
      if (b != Count) press(g, p1, i, b);
      break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
      // triggers and sticks act as buttons in the menus (edges only)
      int i = indexOf(e.gaxis.which);
      if (i < 0) break;
      Pad& pad = pads_[static_cast<size_t>(i)];
      auto edge = [&](Button b, float v) {
        if (!pad.axisHeld[b] && v > EDGE_ON) {
          pad.axisHeld[b] = true;
          press(g, p1, i, b);
        } else if (pad.axisHeld[b] && v < EDGE_OFF) {
          pad.axisHeld[b] = false;
        }
      };
      if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) edge(L2, e.gaxis.value / 32767.0f);
      else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) edge(R2, e.gaxis.value / 32767.0f);
      else if (g.state == GameState::Menu) {
        float x, y;
        axes(i, x, y);
        edge(Left, -x);
        edge(Right, x);
        edge(Up, -y);
        edge(Down, y);
      }
      break;
    }
    case SDL_EVENT_KEY_DOWN:
      if (e.key.repeat) break;
      keys_[e.key.scancode] = true;
      return keyDown(g, p1, e.key.scancode);
    case SDL_EVENT_KEY_UP: keys_[e.key.scancode] = false; break;
    default: break;
  }
  g.p2Connected = pads_.size() >= 2;
  return true;
}

// The original keyboard layout.
bool Input::keyDown(GameData& g, InputState& p1, SDL_Scancode sc) {
  const bool menu = g.state == GameState::Menu;
  switch (sc) {
    case SDL_SCANCODE_ESCAPE:
      if (menu) return false;
      [[fallthrough]];
    case SDL_SCANCODE_P:
      togglePause(g);
      break;
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_W:
      if (menu) press(g, p1, 0, Up);
      break;
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_S:
      if (menu) press(g, p1, 0, Down);
      break;
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_A:
      if (menu) cycleAircraftSelection(g, -1), menuSound();
      break;
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_D:
      if (menu) cycleAircraftSelection(g, 1), menuSound();
      break;
    case SDL_SCANCODE_LEFTBRACKET: if (menu) press(g, p1, 0, L1); break;
    case SDL_SCANCODE_RIGHTBRACKET: if (menu) press(g, p1, 0, R1); break;
    case SDL_SCANCODE_COMMA: if (menu) press(g, p1, 0, L2); break;
    case SDL_SCANCODE_PERIOD: if (menu) press(g, p1, 0, R2); break;
    case SDL_SCANCODE_MINUS: if (menu && isPracticeMode(g)) cyclePracticeStartWave(g, -1), menuSound(); break;
    case SDL_SCANCODE_EQUALS: if (menu && isPracticeMode(g)) cyclePracticeStartWave(g, 1), menuSound(); break;
    case SDL_SCANCODE_U: if (menu) press(g, p1, 0, North); break;
    case SDL_SCANCODE_SPACE:
    case SDL_SCANCODE_Z:
    case SDL_SCANCODE_RETURN:
      if (menu || g.state == GameState::GameOver) press(g, p1, 0, South);
      break;
    case SDL_SCANCODE_X:
    case SDL_SCANCODE_B:
      if (g.state == GameState::Playing) p1.bomb = true;
      else if (g.state == GameState::GameOver) returnToMenu(g);
      break;
    case SDL_SCANCODE_C:
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT:
      if (g.state == GameState::Playing) p1.skill = true;
      break;
    case SDL_SCANCODE_F:
      if (g.state == GameState::Playing) g.autoFire = !g.autoFire, menuSound();
      break;
    case SDL_SCANCODE_I:
      if (g.state == GameState::Playing && isPracticeMode(g)) g.practiceInvincible = !g.practiceInvincible, menuSound();
      break;
    case SDL_SCANCODE_Q:
      if (g.state == GameState::Paused) returnToMenu(g);
      break;
    default: break;
  }
  return true;
}

void Input::poll(GameData& g, InputState& p1) {
  g.p2Connected = pads_.size() >= 2;
  auto fill = [&](int i, InputState& in, bool withKeys) {
    float x = 0, y = 0;
    if (i < static_cast<int>(pads_.size())) axes(i, x, y);
    in.left = x < 0 || held(i, Left);
    in.right = x > 0 || held(i, Right);
    in.up = y < 0 || held(i, Up);
    in.down = y > 0 || held(i, Down);
    in.shoot = held(i, South) || held(i, R2);
    if (withKeys) {
      in.left |= keys_[SDL_SCANCODE_LEFT] || keys_[SDL_SCANCODE_A];
      in.right |= keys_[SDL_SCANCODE_RIGHT] || keys_[SDL_SCANCODE_D];
      in.up |= keys_[SDL_SCANCODE_UP] || keys_[SDL_SCANCODE_W];
      in.down |= keys_[SDL_SCANCODE_DOWN] || keys_[SDL_SCANCODE_S];
      in.shoot |= keys_[SDL_SCANCODE_SPACE] || keys_[SDL_SCANCODE_Z];
    }
  };
  fill(0, p1, true);
  fill(1, g.p2Input, false);
}
