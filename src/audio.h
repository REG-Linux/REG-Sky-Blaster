// Procedural sound effects: the Web Audio oscillators of audio.ts on an SDL3
// audio stream.
#pragma once

namespace audio {

bool init();
void shutdown();
void setMuted(bool muted);

}  // namespace audio
