// render/overlays.ts, hud.ts, gameover.ts, achievement-toast.ts, menu.ts and
// the draw helpers of skills.ts / weapons.ts. Keyboard and touch hints are
// replaced by controller ones.
#include <cmath>
#include <cstdio>

#include "../canvas.h"
#include "../prims.h"
#include "game.h"

namespace {

std::string upper(std::string s) {
  for (char& c : s)
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  return s;
}

std::string repeat(const char* s, int n) {
  std::string out;
  for (int i = 0; i < n; i++) out += s;
  return out;
}

float blink(uint32_t nowMs) { return 0.5f + std::sin(static_cast<float>(nowMs) * 0.004f) * 0.5f; }

// "1. CJOM        12,345  W7  11/9/2026"; the title screen's leaves the date out.
std::string scoreLine(int i, const HighScore& s, bool date = true) {
  char buf[128];
  std::snprintf(buf, sizeof buf, "%2d. %-8s %10s  W%-2d%s%s", i + 1, s.name.c_str(), formatThousands(s.score).c_str(),
                s.wave, date ? "  " : "", date ? s.date.c_str() : "");
  return buf;
}

}  // namespace

void drawParticles(Canvas& ctx, Prims& prims, const GameData& g) {
  prims.additive(true);  // glow on the dark background
  for (const Particle& pt : g.particles) {
    if (pt.type == ParticleType::Score) continue;
    const float a = std::fmax(0, pt.life / pt.maxLife);
    if (pt.type == ParticleType::Ring) {
      const float r = (pt.startSize != 0 ? pt.startSize : 30) * (1 - a);
      prims.ring(pt.x, pt.y, r, std::fmax(0.5f, 3 * a), pt.color, a);
    } else if (pt.type == ParticleType::Spark) {
      // a 2size x 0.6size bar along the direction of travel
      const float ang = std::atan2(pt.vy, pt.vx), c = std::cos(ang), s = std::sin(ang);
      const float xs[4] = {-pt.size, pt.size, pt.size, -pt.size};
      const float ys[4] = {-pt.size / 3, -pt.size / 3, pt.size * 0.6f - pt.size / 3, pt.size * 0.6f - pt.size / 3};
      float q[8];
      for (int i = 0; i < 4; i++) q[i * 2] = pt.x + xs[i] * c - ys[i] * s, q[i * 2 + 1] = pt.y + xs[i] * s + ys[i] * c;
      prims.quad(q, pt.color, a);
    } else if (pt.type == ParticleType::Ember) {
      prims.disc(pt.x, pt.y, std::fmax(0.3f, pt.size * a * 0.7f), pt.color, a);
    } else {
      prims.disc(pt.x, pt.y, std::fmax(0.5f, pt.size * a), pt.color, a);
    }
  }
  prims.additive(false);
  ctx.drawPrims(prims);

  for (const Particle& pt : g.particles) {
    if (pt.type != ParticleType::Score) continue;
    ctx.globalAlpha(std::fmax(0, pt.life / pt.maxLife));
    ctx.fillStyle(pt.color);
    ctx.font(pt.size, Font::Bold);
    ctx.textAlign(Align::Center);
    ctx.fillText(pt.text, pt.x, pt.y);
  }
  ctx.globalAlpha(1);
}

void drawFlashOverlay(Canvas& ctx, const GameData& g) {
  if (g.flashAlpha <= 0) return;
  ctx.globalAlpha(g.flashAlpha);
  ctx.fillStyle(g.flashColor);
  ctx.tintRect(0, 0, CANVAS_W, CANVAS_H);
  ctx.globalAlpha(1);
}

void drawGrazeIndicator(Canvas& ctx, const GameData& g) {
  if (g.player.grazeTimer <= 0) return;
  ctx.globalAlpha(g.player.grazeTimer / 0.3f * 0.4f);
  ctx.strokeStyle(C("#aaeeff"));
  ctx.lineWidth(1);
  ctx.beginPath();
  ctx.arc(g.player.x, g.player.y, GRAZE_RADIUS, 0, PI * 2);
  ctx.stroke();
  ctx.globalAlpha(1);
}

void drawComboBanner(Canvas& ctx, const GameData& g, uint32_t nowMs) {
  if (g.state != GameState::Playing || g.combo < 3) return;
  ctx.globalAlpha(std::fmin(1, g.comboTimer));
  ctx.fillStyle(C("#fd0"));
  ctx.font(28, Font::Bold);
  ctx.textAlign(Align::Center);
  float bx = std::sin(static_cast<float>(nowMs) * 0.01f) * 3;
  ctx.fillText(std::to_string(g.combo) + "x COMBO!", CANVAS_W / 2, 120 + bx);
  ctx.globalAlpha(1);
}

void drawWaveAnnounce(Canvas& ctx, const GameData& g) {
  if (g.waveAnnounceTimer <= 0 || g.wave <= 0) return;
  float t = g.waveAnnounceTimer;
  float a = 1;
  if (t > 70) a = (90 - t) / 20;
  else if (t < 20) a = t / 20;
  a = std::fmax(0, std::fmin(1, a));
  WaveAnnounce announce = getWaveAnnounce(g);
  ctx.save();
  ctx.globalAlpha(a);
  ctx.translate(CANVAS_W / 2, CANVAS_H / 2 - 30);
  ctx.scale(1 + (1 - a) * 0.3f);
  ctx.font(32, Font::Bold);
  ctx.textAlign(Align::Center);
  if (announce.boss) {
    ctx.fillStyle(C("#f44"));
    ctx.shadow(C("#f00"), 20);
  } else {
    ctx.fillStyle(C("#fff"));
  }
  ctx.fillText(announce.main, 0, 0);
  if (!announce.sub.empty()) {
    ctx.font(14);
    ctx.fillStyle(getChapter(g.chapterId).hudColor);
    ctx.fillText(announce.sub, 0, 24);
  }
  ctx.restore();
}

void drawPausedOverlay(Canvas& ctx, const GameData& g) {
  if (g.state != GameState::Paused) return;
  ctx.fillStyle(C("#fff"));
  ctx.font(40, Font::Bold);
  ctx.textAlign(Align::Center);
  ctx.fillText("PAUSED", CANVAS_W / 2, CANVAS_H / 2 - 20);
  ctx.font(18);
  ctx.fillStyle(C("#aac"));
  ctx.fillText("Press START to resume", CANVAS_W / 2, CANVAS_H / 2 + 20);
  ctx.font(14);
  ctx.fillStyle(C("#889"));
  ctx.fillText("SELECT \xE2\x80\x94 quit to title", CANVAS_W / 2, CANVAS_H / 2 + 46);
}

void drawSlowMotionOverlay(Canvas& ctx, const GameData& g) {
  if (g.slowMotion >= 1) return;
  ctx.globalAlpha(0.08f);
  ctx.fillStyle(C("#88aaff"));
  ctx.tintRect(0, 0, CANVAS_W, CANVAS_H);
  ctx.globalAlpha(1);
}

void drawSkillIndicator(Canvas& ctx, const Player& p, float x, float y) {
  const AircraftDef& craft = getAircraft(p.aircraftId);
  const bool ready = p.skillCooldown <= 0;
  const float ratio = getSkillCooldownRatio(p);
  const float radius = 14;
  ctx.save();
  ctx.translate(x, y);
  ctx.beginPath();
  ctx.arc(0, 0, radius, 0, PI * 2);
  ctx.fillStyle(ready ? C("#ffffff18") : C("#00000055"));
  ctx.fill();
  ctx.strokeStyle(ready ? C("#8ef") : C("#556"));
  ctx.lineWidth(2);
  ctx.stroke();
  if (!ready) {
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.arc(0, 0, radius - 2, -PI / 2, -PI / 2 + PI * 2 * ratio);
    ctx.closePath();
    ctx.fillStyle(C("#4af5"));
    ctx.fill();
  }
  ctx.fillStyle(ready ? C("#fff") : C("#889"));
  ctx.font(10, Font::Bold);
  ctx.textAlign(Align::Center);
  ctx.textBaseline(Baseline::Middle);
  ctx.fillText("X", 0, 1);  // the skill button ("C" on the keyboard)
  ctx.fillStyle(C("#889"));
  ctx.font(8);
  std::string first = craft.skillName;
  first = upper(first.substr(0, first.find(' ')));
  ctx.fillText(first, 0, radius + 10);
  ctx.restore();
}

void drawWeaponLabel(Canvas& ctx, WeaponId id, float x, float y) {
  const WeaponDef& w = getWeapon(id);
  ctx.save();
  ctx.textAlign(Align::Left);
  ctx.font(11, Font::Bold);
  ctx.fillStyle(w.hudColor);
  ctx.fillText(w.shortName, x, y);
  ctx.font(9);
  ctx.fillStyle(C("#889"));
  ctx.fillText(w.name, x, y + 12);
  ctx.restore();
}

static float drawHpBar(Canvas& ctx, const Player& p, float barY, const std::string& label, Color labelColor) {
  const float barX = CANVAS_W - 10, segW = 28, segH = 16, segGap = 5;
  const float totalW = p.maxHp * segW + (p.maxHp - 1) * segGap;
  for (int i = 0; i < p.maxHp; i++) {
    float sx = barX - totalW + i * (segW + segGap);
    bool filled = i < p.hp;
    ctx.fillStyle(C("#1a1a2e"));
    ctx.beginPath();
    ctx.roundRect(sx, barY, segW, segH, 4);
    ctx.fill();
    if (filled) {
      float hpR = static_cast<float>(p.hp) / p.maxHp;
      ctx.fillStyle(hpR > 0.6f ? C("#44ffaa") : hpR > 0.34f ? C("#ffaa44") : C("#ff4455"));
      ctx.beginPath();
      ctx.roundRect(sx, barY, segW, segH, 4);
      ctx.fill();
      ctx.save();
      ctx.globalAlpha(0.3f);
      ctx.fillStyle(C("#fff"));
      ctx.beginPath();
      ctx.roundRect(sx + 2, barY, segW - 4, segH / 2, 4, 4, 0, 0);
      ctx.fill();
      ctx.restore();
    }
    ctx.strokeStyle(filled ? C("#ffffff30") : C("#ffffff10"));
    ctx.lineWidth(1);
    ctx.beginPath();
    ctx.roundRect(sx, barY, segW, segH, 4);
    ctx.stroke();
  }
  ctx.textAlign(Align::Right);
  ctx.font(16);
  ctx.fillStyle(labelColor);
  ctx.fillText(label, barX - totalW - 8, barY + segH - 2);
  return barY + segH;
}

// Bomb stock under a ship's HP bar: a slot per bomb it can hold.
static float drawBombs(Canvas& ctx, const Player& p, float y) {
  const float r = 7, gap = 6, right = CANVAS_W - 10;
  const float totalW = BOMBS_MAX * 2 * r + (BOMBS_MAX - 1) * gap;
  for (int i = 0; i < BOMBS_MAX; i++) {
    const float cx = right - totalW + r + i * (2 * r + gap), cy = y + r;
    ctx.fillStyle(i < p.bombs ? C("#f44") : C("#1a1a2e"));
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, PI * 2);
    ctx.fill();
    ctx.strokeStyle(i < p.bombs ? C("#ffffff40") : C("#ffffff10"));
    ctx.lineWidth(1);
    ctx.stroke();
  }
  ctx.textAlign(Align::Right);
  ctx.font(16);
  ctx.fillStyle(C("#889"));
  ctx.fillText("BOMB", right - totalW - 8, y + 2 * r - 1);
  return y + 2 * r;
}

void drawHUD(Canvas& ctx, GameData& g) {
  const Player& p = g.player;
  const float top = g.showFps ? 14 : 0;  // below the frame counter
  ctx.fillStyle(C("#fff"));
  ctx.font(40, Font::Bold);
  ctx.textAlign(Align::Left);
  ctx.fillText(formatThousands(g.score), 10, top + 44);
  ctx.font(14);
  ctx.fillStyle(C("#aac"));
  ctx.fillText(getWaveLabel(g), 10, top + 64);
  const ChapterDef& chapter = getChapter(g.chapterId);
  ctx.font(11);
  ctx.fillStyle(chapter.hudColor);
  ctx.fillText(chapter.name, 10, top + 78);
  if (g.gameMode == GameMode::Daily && g.dailyModifier != DailyModifier::None) {
    ctx.fillStyle(C("#da8"));
    ctx.fillText(dailyModifierLabel(g.dailyModifier), 10, top + 92);
  }
  auto seatColor = [&](const Player& s) { return resolveShipPalette(s.aircraftId, coopSeatForShip(g, s)).accent; };
  float barBottom = drawHpBar(ctx, p, 14, g.player2 ? "P1" : "HP", g.player2 ? seatColor(p) : C("#889"));
  barBottom = drawBombs(ctx, p, barBottom + 6);
  if (g.player2) {
    barBottom = drawHpBar(ctx, *g.player2, barBottom + 8, "P2", seatColor(*g.player2));
    barBottom = drawBombs(ctx, *g.player2, barBottom + 6);
  }
  const float statusY = barBottom + 10;
  ctx.textAlign(Align::Right);
  if (p.powerLevel > 0) {
    ctx.font(11);
    ctx.fillStyle(C("#4df"));
    ctx.fillText("PWR " + repeat("▮", p.powerLevel) + repeat("▯", 3 - p.powerLevel), CANVAS_W - 10, statusY);
  }
  if (p.shieldActive) {
    ctx.fillStyle(C("#48f"));
    ctx.font(11);
    ctx.fillText("SHIELD", CANVAS_W - 10, statusY + (p.powerLevel > 0 ? 14 : 0));
  }
  if (p.grazeCount > 0) {
    ctx.textAlign(Align::Left);
    ctx.font(11);
    ctx.fillStyle(C("#8ac"));
    ctx.globalAlpha(0.6f);
    ctx.fillText("GRAZE ×" + std::to_string(p.grazeCount), 10, CANVAS_H - 12);
    ctx.globalAlpha(1);
  }

  drawSkillIndicator(ctx, p, CANVAS_W / 2, CANVAS_H - 36);
  drawWeaponLabel(ctx, p.weaponId, 10, CANVAS_H - 36);
  float footerY = CANVAS_H - 12;
  if (g.autoFire) {
    ctx.textAlign(Align::Right);
    ctx.font(11);
    ctx.fillStyle(C("#4f8"));
    ctx.globalAlpha(0.85f);
    ctx.fillText("AUTO", CANVAS_W - 10, footerY);
    ctx.globalAlpha(1);
    footerY -= 14;
  }
  if (g.gameMode == GameMode::Practice && g.practiceInvincible) {
    ctx.textAlign(Align::Right);
    ctx.font(11);
    ctx.fillStyle(C("#fd4"));
    ctx.globalAlpha(0.85f);
    ctx.fillText("INV", CANVAS_W - 10, footerY);
    ctx.globalAlpha(1);
  }
}

void drawAchievementToast(Canvas& ctx, const GameData& g) {
  if (!g.achievementToast) return;
  const AchievementDef& def = achievementDef(g.achievementToast->id);
  float fadeIn = std::fmin(1, (3.5f - g.achievementToast->timer) / 0.4f);
  float fadeOut = std::fmin(1, g.achievementToast->timer / 0.5f);
  const float boxW = 300, boxH = 50, x = (CANVAS_W - boxW) / 2, y = 78;
  ctx.save();
  ctx.globalAlpha(std::fmin(fadeIn, fadeOut));
  ctx.fillStyle(rgba(8, 12, 24, 0.85f));
  ctx.strokeStyle(C("#fd4"));
  ctx.lineWidth(1.5f);
  ctx.beginPath();
  ctx.roundRect(x, y, boxW, boxH, 8);
  ctx.fill();
  ctx.stroke();
  ctx.textAlign(Align::Center);
  ctx.fillStyle(C("#fd4"));
  ctx.font(11, Font::Bold);
  ctx.fillText("ACHIEVEMENT UNLOCKED", CANVAS_W / 2, y + 16);
  ctx.fillStyle(C("#fff"));
  ctx.font(15, Font::Bold);
  ctx.fillText(def.name, CANVAS_W / 2, y + 36);
  ctx.restore();
}

// The name for a top-10 score: eight letter boxes, the cursor's pulsing,
// with ▲ ▼ over and under it for the letter change.
static void drawNameEntry(Canvas& ctx, const GameData& g, float top, uint32_t nowMs) {
  const NameEntry& n = g.nameEntry;
  int place = 1;
  for (const HighScore& h : loadHighScores()) place += h.score >= g.score;
  ctx.textAlign(Align::Center);
  ctx.fillStyle(C("#fd4"));
  ctx.font(15, Font::Bold);
  ctx.fillText(place == 1 ? "★ NEW HIGH SCORE! ★" : "TOP 10 \xE2\x80\x94 #" + std::to_string(place), CANVAS_W / 2, top);
  ctx.fillStyle(C("#fff"));
  ctx.font(13, Font::Bold);
  ctx.fillText("ENTER YOUR NAME", CANVAS_W / 2, top + 22);

  const float cw = 28, ch = 36, gap = 6;
  const float x0 = CANVAS_W / 2 - (NAME_LEN * cw + (NAME_LEN - 1) * gap) / 2;
  const float y0 = top + 48;
  const float pulse = 0.5f + std::sin(static_cast<float>(nowMs) * 0.008f) * 0.5f;
  for (int i = 0; i < NAME_LEN; i++) {
    const float x = x0 + i * (cw + gap);
    const bool cur = i == n.cursor;
    ctx.beginPath();
    ctx.roundRect(x, y0, cw, ch, 5);
    ctx.fillStyle(cur ? rgba(68, 170, 255, 0.18f + 0.12f * pulse) : rgba(255, 255, 255, 0.06f));
    ctx.fill();
    ctx.save();
    if (cur) ctx.globalAlpha(0.6f + 0.4f * pulse);
    ctx.strokeStyle(cur ? C("#4af") : C("#ffffff30"));
    ctx.lineWidth(cur ? 2 : 1);
    ctx.stroke();
    ctx.restore();
    ctx.fillStyle(C("#fff"));
    ctx.font(22, Font::Bold);  // sans: O and 0 stay apart
    ctx.textBaseline(Baseline::Middle);
    ctx.fillText(std::string(1, n.buf[i]), x + cw / 2, y0 + ch / 2 + 1);
    ctx.textBaseline(Baseline::Alphabetic);
    if (cur) {
      ctx.fillStyle(C("#4af"));
      ctx.font(10);
      ctx.fillText("▲", x + cw / 2, y0 - 4);
      ctx.fillText("▼", x + cw / 2, y0 + ch + 12);
    }
  }
  ctx.fillStyle(C("#889"));
  ctx.font(11);
  ctx.fillText("↑ ↓ letter  ·  ← → move  ·  B delete  ·  A save", CANVAS_W / 2, y0 + ch + 34);
}

void drawGameOver(Canvas& ctx, GameData& g, uint32_t nowMs) {
  auto lineStep = [](float y, float fontSize, float gap = 14) { return y + fontSize + gap; };
  ctx.textAlign(Align::Center);
  ctx.save();
  ctx.translate(0, (CANVAS_H - BASE_H) / 2);

  ctx.save();
  if (g.modeVictory) {
    ctx.shadow(C("#4f8"), 25);
    ctx.fillStyle(C("#4f8"));
    ctx.font(40, Font::Bold);
    ctx.fillText("MISSION COMPLETE", CANVAS_W / 2, 195);
    ctx.restore();
    ctx.fillStyle(C("#aac"));
    ctx.font(14);
    ctx.fillText("All four chapters cleared", CANVAS_W / 2, 230);
  } else {
    ctx.shadow(C("#f44"), 25);
    ctx.fillStyle(C("#f44"));
    ctx.font(44, Font::Bold);
    ctx.fillText("GAME OVER", CANVAS_W / 2, 195);
    ctx.restore();
  }

  ctx.fillStyle(C("#fff"));
  ctx.font(24);
  ctx.fillText("Score: " + formatThousands(g.score), CANVAS_W / 2, g.modeVictory ? 270 : 255);

  ctx.fillStyle(C("#aac"));
  ctx.font(14);
  std::string progress = (g.gameMode == GameMode::Story ? "Stage " : "Wave ") + std::to_string(g.wave);
  float progressY = g.modeVictory ? 300 : 285;
  ctx.fillText(progress + "  ·  Combo " + std::to_string(g.maxCombo) + "x  ·  Graze " +
                   std::to_string(g.player.grazeCount),
               CANVAS_W / 2, progressY);

  ctx.fillStyle(C("#889"));
  ctx.font(13);
  float y = lineStep(progressY, 13, 12);
  ctx.fillText("Accuracy " + formatAccuracy(g.shotsHit, g.shotsFired) + "  ·  Damage " +
                   formatThousands(static_cast<int64_t>(std::round(g.damageDealt))) + "  ·  Kills " +
                   std::to_string(g.enemiesKilled),
               CANVAS_W / 2, y);
  y = lineStep(y, 13, 15);
  const auto& sc = loadHighScores();
  const bool entering = g.nameEntry.active;
  const bool isNewHighScore = g.highScoreRank == 0 && !g.modeVictory;

  if (g.runCoinsEarned > 0) {
    ctx.fillStyle(C("#fd4"));
    ctx.font(15, Font::Bold);
    ctx.fillText("+" + formatThousands(g.runCoinsEarned) + " coins earned", CANVAS_W / 2, y);
    y = lineStep(y, 15);
  }
  if (isNewHighScore) {
    ctx.fillStyle(C("#fd4"));
    ctx.font(18, Font::Bold);
    ctx.fillText("★ NEW HIGH SCORE! ★", CANVAS_W / 2, y);
    y = lineStep(y, 18);
  }

  float top = std::fmax(y + 18, 372);
  if (entering) {
    drawNameEntry(ctx, g, top, nowMs);
  } else {
    ctx.fillStyle(C("#fd4"));
    ctx.font(15, Font::Bold);
    ctx.fillText("HIGH SCORES", CANVAS_W / 2, top);
    ctx.font(12, Font::Mono);
    // five rows, slid down if this run placed below fifth
    const int first = g.highScoreRank > 4 ? g.highScoreRank - 4 : 0;
    for (int i = first; i < static_cast<int>(sc.size()) && i < first + 5; i++) {
      ctx.fillStyle(i == g.highScoreRank ? C("#fd4") : C("#aac"));
      ctx.fillText(scoreLine(i, sc[i]), CANVAS_W / 2, top + 22 + (i - first) * 20);
    }
  }
  if (entering) {
    ctx.restore();
    return;
  }

  ctx.globalAlpha(blink(nowMs));
  ctx.fillStyle(C("#fff"));
  ctx.font(20);
  ctx.fillText("PRESS A or START to restart", CANVAS_W / 2, 540);
  ctx.globalAlpha(1);
  ctx.fillStyle(C("#889"));
  ctx.font(13);
  ctx.fillText("B \xE2\x80\x94 back to title", CANVAS_W / 2, 566);
  ctx.restore();
}

void drawMenu(Canvas& ctx, GameData& g, uint32_t nowMs) {
  // menu-layout.ts rows; practice and co-op add one row each
  const bool showPractice = isPracticeMode(g);
  const bool showCoop = isCoopMode(g);
  const float offset = showPractice || showCoop ? 52 : 0;  // 46 in the original; room for the cursor box
  const float startY = 452 + offset;
  const float controlsTop = 474 + offset;
  const float controlsStep = 15;

  ctx.textAlign(Align::Center);  // the backdrop comes dimmed (renderGame)
  // the layout is the original's 700 rows, centred on a taller field
  const float yoff = (CANVAS_H - BASE_H) / 2;
  ctx.save();
  ctx.translate(0, yoff);

  ctx.save();
  ctx.shadow(C("#0af"), 40);
  ctx.fillStyle(C("#fff"));
  ctx.font(42, Font::Bold);
  ctx.fillText("SKY BLASTER", CANVAS_W / 2, 130);
  ctx.restore();

  ctx.fillStyle(C("#6bf"));
  ctx.font(14);
  ctx.fillText("SPACE SHOOTER", CANVAS_W / 2, 156);

  ctx.textAlign(Align::Right);
  ctx.fillStyle(C("#fd4"));
  ctx.font(28, Font::Bold);
  ctx.fillText("¢ " + formatThousands(loadCoins()), CANVAS_W - 12, 38 - yoff);
  ctx.textAlign(Align::Center);

  auto label = [&](const char* text, float y) {
    ctx.fillStyle(C("#fd4"));
    ctx.font(13, Font::Bold);
    ctx.fillText(text, CANVAS_W / 2, y);
  };
  // the cursor's row: a pulsing box behind it
  const float pulse = 0.5f + std::sin(static_cast<float>(nowMs) * 0.006f) * 0.5f;
  auto focusBox = [&](MenuRow row, float top, float bottom) {
    if (g.menuRow != row) return;
    ctx.save();
    ctx.beginPath();
    ctx.roundRect(24, top, CANVAS_W - 48, bottom - top, 8);
    ctx.fillStyle(rgba(68, 170, 255, 0.10f + 0.06f * pulse));
    ctx.fill();
    ctx.globalAlpha(0.55f + 0.45f * pulse);
    ctx.strokeStyle(C("#4af"));
    ctx.lineWidth(1.5f);
    ctx.stroke();
    ctx.restore();
  };
  // a row's value: on the cursor's row it is a little larger and gets the
  // ◀ ▶ that left/right act on
  auto value = [&](MenuRow row, const std::string& text, float y, float px, Color color) {
    const bool focused = g.menuRow == row;
    ctx.save();
    ctx.fillStyle(color);
    ctx.font(px, Font::Bold);
    ctx.translate(CANVAS_W / 2, y);
    if (focused) ctx.scale(1.08f);
    ctx.fillText(focused ? "◀  " + text + "  ▶" : text, 0, 0);
    ctx.restore();
  };

  const ModeInfo& mode = modeInfo(g.gameMode);
  focusBox(MenuRow::Mode, 166, g.gameMode == GameMode::Daily ? 243 : 229);
  label("SELECT MODE", 182);
  value(MenuRow::Mode, upper(mode.name), 206, 20, C("#8df"));
  ctx.fillStyle(C("#889"));
  ctx.font(11);
  ctx.fillText(mode.tagline, CANVAS_W / 2, 222);
  if (g.gameMode == GameMode::Daily) {
    ctx.fillStyle(C("#da8"));
    ctx.fillText(std::string("Today: ") + dailyModifierLabel(pickDailyModifier(getDailySeed())), CANVAS_W / 2, 236);
  }

  const AircraftDef& craft = getAircraft(g.selectedAircraft);
  const WeaponDef& weapon = getWeapon(g.selectedWeapon);
  const bool craftUnlocked = isAircraftUnlocked(g.selectedAircraft);
  const bool weaponUnlocked = isWeaponUnlocked(g.selectedWeapon);
  focusBox(MenuRow::Aircraft, 246, 322);
  label("SELECT AIRCRAFT", 262);
  value(MenuRow::Aircraft, upper(craft.name) + (craftUnlocked ? "" : "  [LOCKED]"), 286, 20,
        craftUnlocked ? craft.hullTop : C("#888"));
  ctx.fillStyle(C("#aac"));
  ctx.font(11);
  ctx.fillText(craft.tagline, CANVAS_W / 2, 302);
  ctx.font(10);
  if (!craftUnlocked) {
    ctx.fillStyle(canAffordUnlock(craft.coinCost) ? C("#8f8") : C("#f88"));
    ctx.fillText(std::to_string(craft.coinCost) + " coins \xE2\x80\x94 press Y to unlock", CANVAS_W / 2, 316);
  } else {
    ctx.fillStyle(C("#889"));
    char buf[128];
    std::snprintf(buf, sizeof buf, "SPD %g  ·  HP %d/%d  ·  %s", craft.speed, craft.startHp, craft.maxHp,
                  craft.skillName);
    ctx.fillText(buf, CANVAS_W / 2, 316);
  }

  focusBox(MenuRow::Weapon, 325, 382);
  label("SELECT WEAPON", 340);
  value(MenuRow::Weapon, upper(weapon.name) + (weaponUnlocked ? "" : "  [LOCKED]"), 362, 18,
        weaponUnlocked ? weapon.hudColor : C("#888"));
  ctx.font(10);
  if (!weaponUnlocked) {
    ctx.fillStyle(canAffordUnlock(weapon.coinCost) ? C("#8f8") : C("#f88"));
    ctx.fillText(std::to_string(weapon.coinCost) + " coins \xE2\x80\x94 press Y to unlock", CANVAS_W / 2, 376);
  } else {
    ctx.fillStyle(C("#889"));
    ctx.fillText(weapon.tagline, CANVAS_W / 2, 376);
  }

  const DifficultyInfo& diff = difficultyInfo(g.difficulty);
  focusBox(MenuRow::Difficulty, 381, 434);
  label("DIFFICULTY", 394);
  value(MenuRow::Difficulty, upper(diff.name), 414, 18, diff.color);
  ctx.fillStyle(C("#889"));
  ctx.font(10);
  ctx.fillText(diff.tagline, CANVAS_W / 2, 428);

  if (showPractice) {
    focusBox(MenuRow::StartWave, 433, 486);
    label("START WAVE", 446);
    value(MenuRow::StartWave, "WAVE " + std::to_string(g.practiceStartWave), 466, 18, C("#8df"));
    ctx.fillStyle(C("#889"));
    ctx.font(10);
    ctx.fillText(practiceStartWaveLabel(g.practiceStartWave), CANVAS_W / 2, 480);
  }
  if (showCoop) {
    label("PLAYER 2", 446);
    ctx.fillStyle(g.p2Connected ? C("#8df") : C("#f88"));
    ctx.font(15, Font::Bold);
    ctx.fillText(g.p2Connected ? "SECOND CONTROLLER READY" : "CONNECT A SECOND CONTROLLER", CANVAS_W / 2, 466);
    ctx.fillStyle(C("#889"));
    ctx.font(10);
    ctx.fillText("Both ships fly the selected loadout", CANVAS_W / 2, 480);
  }

  bool startReady = canStartGame(g);
  std::string startText = startReady ? "PRESS A or START" : "UNLOCK SELECTION TO START";
  if (showCoop && startReady && !g.p2Connected) {
    startReady = false;
    startText = "WAITING FOR PLAYER 2…";
  }
  focusBox(MenuRow::Start, startY - 21, startY + 8);
  ctx.globalAlpha(startReady ? blink(nowMs) : 0.7f);
  ctx.fillStyle(startReady ? C("#fff") : C("#f88"));
  ctx.font(startReady ? 20 : 16);
  ctx.fillText(startText, CANVAS_W / 2, startY);
  ctx.globalAlpha(1);

  static const char* lines[][2] = {
      {"↑ / ↓", "Select an option"},
      {"← / →", "Change it"},
      {"A · START", "Start · shoot"},
      {"Y", "Unlock · toggle auto-fire"},
      {"B", "Bomb"},
      {"X", "Skill"},
      {"START", "Pause"},
      {"SELECT", "Invincibility (practice)"},
  };
  const int nLines = 8;
  for (int i = 0; i < nLines; i++) {
    float ly = controlsTop + i * controlsStep;
    ctx.fillStyle(C("#aac"));
    ctx.font(11);
    ctx.textAlign(Align::Right);
    ctx.fillText(lines[i][0], CANVAS_W / 2 - 8, ly);
    ctx.fillStyle(C("#88a"));
    ctx.textAlign(Align::Left);
    ctx.fillText(lines[i][1], CANVAS_W / 2 + 8, ly);
  }
  ctx.textAlign(Align::Center);

  const auto& sc = loadHighScores();
  if (!sc.empty()) {
    float scoresTop = controlsTop + nLines * controlsStep + 18;
    ctx.strokeStyle(C("#ffffff22"));
    ctx.lineWidth(1);
    ctx.beginPath();
    ctx.moveTo(40, scoresTop - 10);
    ctx.lineTo(CANVAS_W - 40, scoresTop - 10);
    ctx.stroke();
    ctx.textAlign(Align::Center);
    ctx.fillStyle(C("#fd4"));
    ctx.font(13, Font::Bold);
    ctx.fillText("HIGH SCORES", CANVAS_W / 2, scoresTop);
    ctx.font(10, Font::Mono);
    for (size_t i = 0; i < sc.size() && i < 3; i++) {
      ctx.fillStyle(i == 0 ? C("#fd4") : C("#aac"));
      ctx.fillText(scoreLine(static_cast<int>(i), sc[i], false), CANVAS_W / 2, scoresTop + 16 + i * 15);
    }
  }
  ctx.restore();
}
