// Deterministic PRNG (mulberry32), bit-exact with the original so a Daily
// Challenge seed gives the same run. next() returns [0, 1).
#pragma once

#include <cstdint>

class Rng {
 public:
  explicit Rng(uint32_t seed) : state_(seed) {}

  double next() {
    state_ += 0x6d2b79f5u;
    uint32_t t = state_;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  }

 private:
  uint32_t state_;
};
