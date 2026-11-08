# Sky Blaster for REG-Linux

A C++ port of [Sky Blaster](https://github.com/phaethix/polished-arcade-shooter)
by cosmoli (phaethix), a vertical shoot-'em-up originally written in
TypeScript for the browser. The game logic, drawing and sounds follow the
original function by function; see `UPSTREAM` for the commit it was ported
from.

- **SDL3** for the window, GL context, controllers and audio
- **NanoVG** (GLES2 or GL2) for the canvas-style vector drawing
- sounds are synthesised at run time, as in the original; there are no
  image or audio assets

## Differences from the original

- **Controllers** drive everything, menus included (the original only read
  a pad during play). The keyboard layout of the original still works.
- **Co-op Endless is local**: the second controller flies the second ship.
  The online PartyKit co-op is not ported.
- **Rotation**: `--rotate 90` turns the picture a quarter left, so the
  400×700 play field fills a landscape screen held turned clockwise;
  `--turned` makes player 1's d-pad, sticks and face buttons follow the turn.
- **The field fills the screen.** The original 400×700 field keeps its
  short side and grows along the other to the screen's shape: spawns,
  hazards, HUD and stars use the whole area, and the menus stay centred.
  Turned on a 16:9 handheld it stays close to the original; on a TV it
  becomes a wide field.
- Back to the title from pause (SELECT) or game over (B); the original had
  no way back short of a page reload.
- No touch or mouse input yet.

## Controls

| Controller | Menu | In game |
|---|---|---|
| D-pad / left stick | ↑↓ mode, ←→ aircraft | move |
| L1 / R1 | weapon | |
| L2 / R2 | difficulty | R2 fires |
| SELECT + ←→ | practice start wave | toggle invincibility (practice) |
| A | start | fire (auto-fire is on by default) |
| B | | bomb |
| X | | skill |
| Y | unlock selection | toggle auto-fire |
| START | start | pause |

## Building

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release   # -DSKYBLASTER_GL=GL2 for desktop GL
cmake --build build
./build/sky-blaster --windowed --data-dir data
```

Options: `--rotate 0|90|180|270`, `--turned`, `--windowed`, `--window WxH`, `--save-dir DIR`,
`--data-dir DIR`, `--mute`. For testing: `--demo [--demo-wave N]
[--demo-weapon 0-4]` starts an invincible practice run, `--shot FILE.png
--shot-frame N` saves frame N and exits.

Progress (coins, unlocks, achievements, high scores) is kept in
`save.ini` in the save directory.

## Licenses

- Game code: MIT, see `LICENSE` (original © cosmoli, port © REG-Linux)
- NanoVG: zlib, `third_party/nanovg/LICENSE.txt`
- stb_image_write: public domain / MIT, `third_party/stb/stb_image_write.h`
- Liberation Sans: SIL OFL 1.1, `data/fonts/LICENSE.liberation`
- DejaVu fonts: Bitstream Vera license, `data/fonts/LICENSE.dejavu`
