// Controllers and keyboard. The first controller drives P1 (and the menus),
// the second one P2 in co-op. With `turned`, P1's controls follow the
// device held turned clockwise for a vertical game: the picture is turned a
// quarter left, so the d-pad and sticks turn with it and the face buttons
// move round one place (REG-Linux rotate_vertical, regmsg tate.lua).
#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include "game/game.h"

class Input {
 public:
  explicit Input(bool turned) : turned_(turned) {}
  ~Input();

  // Returns false when the player asked to quit.
  bool handleEvent(const SDL_Event& e, GameData& g, InputState& p1);
  // Held controls (movement, fire), once per frame.
  void poll(GameData& g, InputState& p1);

 private:
  enum Button { South, East, West, North, L1, R1, L2, R2, Start, Back, Up, Down, Left, Right, Count };

  struct Pad {
    SDL_Gamepad* pad;
    SDL_JoystickID id;
    bool axisHeld[Count] = {};  // trigger / stick edges
  };

  Button logical(int padIndex, SDL_GamepadButton b) const;
  void axes(int padIndex, float& x, float& y) const;
  bool held(int padIndex, Button b) const;
  void press(GameData& g, InputState& p1, int padIndex, Button b);
  bool keyDown(GameData& g, InputState& p1, SDL_Scancode sc);
  int indexOf(SDL_JoystickID id) const;
  void track(const GameData& g);
  bool anyHeld() const;

  bool turned_;
  std::vector<Pad> pads_;
  bool keys_[SDL_SCANCODE_COUNT] = {};
  // Game over ignores input until everything held at the death (fire,
  // the stick) is let go: it would otherwise edit the high-score name.
  GameState seen_ = GameState::Menu;
  bool quiet_ = false;
  uint64_t quietSince_ = 0;
};
