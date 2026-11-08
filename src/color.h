// CSS-style colour literals ("#rgb", "#rgba", "#rrggbb", "#rrggbbaa",
// "transparent"), parsed at compile time.
#pragma once

#include <cstdint>

struct Color {
  float r = 0, g = 0, b = 0, a = 0;
};

namespace color_detail {

constexpr int hex(char c) {
  return c >= '0' && c <= '9'   ? c - '0'
         : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                : 0;
}

constexpr int len(const char* s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}

}  // namespace color_detail

constexpr Color C(const char* s) {
  using namespace color_detail;
  if (s[0] != '#') return Color{0, 0, 0, 0};  // "transparent"
  const int n = len(s) - 1;
  const char* h = s + 1;
  if (n == 3 || n == 4) {
    Color c{hex(h[0]) * 17 / 255.0f, hex(h[1]) * 17 / 255.0f, hex(h[2]) * 17 / 255.0f, 1.0f};
    if (n == 4) c.a = hex(h[3]) * 17 / 255.0f;
    return c;
  }
  Color c{(hex(h[0]) * 16 + hex(h[1])) / 255.0f, (hex(h[2]) * 16 + hex(h[3])) / 255.0f,
          (hex(h[4]) * 16 + hex(h[5])) / 255.0f, 1.0f};
  if (n == 8) c.a = (hex(h[6]) * 16 + hex(h[7])) / 255.0f;
  return c;
}

constexpr Color rgba(int r, int g, int b, float a) {
  return Color{r / 255.0f, g / 255.0f, b / 255.0f, a};
}

// `color + "44"` in the original: replace the alpha with a hex byte.
constexpr Color withAlpha(Color c, int byte) {
  c.a = byte / 255.0f;
  return c;
}

constexpr Color TRANSPARENT{0, 0, 0, 0};
