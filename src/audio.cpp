#include "audio.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <vector>

#include "game/game.h"

namespace {

enum class Wave { Square, Sawtooth, Sine, Triangle };

// One OscillatorNode + GainNode pair: exponential frequency ramp (optional)
// and exponential gain decay to 0.0001 over `dur`, as in scheduleTone().
struct Voice {
  Wave wave;
  double f0, f1, vol, dur;
  int64_t start;  // in samples
  double phase = 0;
  double freq = 0, gain = 0, fMul = 1, gMul = 1;  // per-sample ramps
};

constexpr int RATE = 48000;
constexpr size_t MAX_VOICES = 64;

SDL_AudioStream* g_stream = nullptr;
std::vector<Voice> g_voices;  // touched only with the stream locked
int64_t g_clock = 0;          // samples rendered so far
bool g_muted = false;

// polyBLEP: Web Audio oscillators are band-limited, naive ones alias badly
double blep(double t, double dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1;
  }
  if (t > 1 - dt) {
    t = (t - 1) / dt;
    return t * t + t + t + 1;
  }
  return 0;
}

double sample(Voice& v, double freq) {
  const double dt = freq / RATE;
  const double t = v.phase;
  double s = 0;
  switch (v.wave) {
    case Wave::Sine: s = std::sin(2 * M_PI * t); break;
    case Wave::Triangle: s = t < 0.5 ? 4 * t - 1 : 3 - 4 * t; break;
    case Wave::Sawtooth: s = 2 * t - 1 - blep(t, dt); break;
    case Wave::Square: {
      s = t < 0.5 ? 1 : -1;
      s += blep(t, dt);
      double t2 = t + 0.5;
      if (t2 >= 1) t2 -= 1;
      s -= blep(t2, dt);
      break;
    }
  }
  v.phase += dt;
  if (v.phase >= 1) v.phase -= std::floor(v.phase);
  return s;
}

void SDLCALL feed(void*, SDL_AudioStream* stream, int additional, int) {
  const int frames = additional / static_cast<int>(sizeof(float));
  if (frames <= 0) return;
  std::vector<float> buf(static_cast<size_t>(frames), 0.0f);
  for (int i = 0; i < frames; i++) {
    const int64_t now = g_clock + i;
    double mix = 0;
    for (Voice& v : g_voices) {
      if (now < v.start) continue;
      if (now >= v.start + static_cast<int64_t>(v.dur * RATE)) continue;
      mix += sample(v, v.freq) * v.gain;
      v.freq *= v.fMul;
      v.gain *= v.gMul;
    }
    buf[static_cast<size_t>(i)] = g_muted ? 0.0f : static_cast<float>(std::fmax(-1.0, std::fmin(1.0, mix)));
  }
  g_clock += frames;
  std::erase_if(g_voices, [](const Voice& v) {
    return g_clock >= v.start + static_cast<int64_t>(v.dur * RATE);
  });
  SDL_PutAudioStreamData(stream, buf.data(), frames * static_cast<int>(sizeof(float)));
}

void tone(double freq, double dur, Wave wave = Wave::Square, double vol = 0.1, double freqEnd = 0,
          double delay = 0) {
  if (!g_stream) return;
  if (freqEnd > 0) freqEnd = std::fmax(freqEnd, 20);
  SDL_LockAudioStream(g_stream);
  if (g_voices.size() < MAX_VOICES) {
    Voice v{wave, freq, freqEnd, vol, dur, g_clock + static_cast<int64_t>(delay * RATE)};
    const double n = dur * RATE;
    v.freq = freq;
    v.gain = vol;
    v.fMul = freqEnd > 0 ? std::pow(freqEnd / freq, 1 / n) : 1;
    v.gMul = std::pow(0.0001 / vol, 1 / n);
    g_voices.push_back(v);
  }
  SDL_UnlockAudioStream(g_stream);
}

}  // namespace

namespace audio {

bool init() {
  SDL_AudioSpec spec{SDL_AUDIO_F32, 1, RATE};
  g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, nullptr);
  if (!g_stream) {
    SDL_Log("audio: %s", SDL_GetError());
    return false;
  }
  SDL_ResumeAudioStreamDevice(g_stream);
  return true;
}

void shutdown() {
  if (g_stream) SDL_DestroyAudioStream(g_stream);
  g_stream = nullptr;
}

void setMuted(bool muted) { g_muted = muted; }

}  // namespace audio

namespace sfx {

void playShoot() { tone(880, 0.08, Wave::Square, 0.08, 440); }
void playEnemyShoot() { tone(330, 0.1, Wave::Sawtooth, 0.04, 220); }
void playHit() { tone(300, 0.1, Wave::Square, 0.08, 150); }
void playCombo() { tone(1047, 0.08, Wave::Sine, 0.07); }
void playMenuSelect() {
  tone(660, 0.1, Wave::Sine, 0.1);
  tone(880, 0.15, Wave::Sine, 0.1, 0, 0.08);
}
void playExplosion() {
  tone(200, 0.3, Wave::Sawtooth, 0.12, 30);
  tone(150, 0.2, Wave::Square, 0.08, 20, 0.05);
}
void playBigExplosion() {
  tone(150, 0.5, Wave::Sawtooth, 0.15, 20);
  tone(100, 0.4, Wave::Square, 0.1, 15, 0.1);
  tone(80, 0.6, Wave::Triangle, 0.08, 10, 0.15);
}
void playPowerUp() {
  tone(523, 0.1, Wave::Sine, 0.1);
  tone(659, 0.1, Wave::Sine, 0.1, 0, 0.1);
  tone(784, 0.15, Wave::Sine, 0.1, 0, 0.2);
}
void playHeal() {
  tone(440, 0.08, Wave::Sine, 0.1);
  tone(554, 0.08, Wave::Sine, 0.1, 0, 0.08);
  tone(659, 0.08, Wave::Sine, 0.1, 0, 0.16);
  tone(880, 0.2, Wave::Sine, 0.12, 0, 0.24);
}
void playGraze() { tone(1200, 0.05, Wave::Sine, 0.06, 800); }
void playGameOver() {
  tone(440, 0.3, Wave::Sine, 0.12, 220);
  tone(330, 0.3, Wave::Sine, 0.12, 165, 0.3);
  tone(262, 0.5, Wave::Sine, 0.12, 131, 0.6);
}

}  // namespace sfx
