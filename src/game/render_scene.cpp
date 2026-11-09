// engine.ts render(), chapters.ts drawChapterBackground(), hazards.ts drawHazards()
#include <algorithm>
#include <cmath>

#include "../canvas.h"
#include "../prims.h"
#include "game.h"

// Through Prims: the gradient and nebulae are vertex colours, the stars one
// batch. `dim` darkens it all, as black at 1 - dim laid over it would.
void drawChapterBackground(Prims& prims, const GameData& g, float dim) {
  const ChapterDef& ch = getChapter(g.chapterId);
  auto d = [dim](Color c) { return Color{c.r * dim, c.g * dim, c.b * dim, c.a}; };
  // Low HP reddens the edges (#ff000044 beyond 0.7 H, clear inside 0.3 H).
  // That goes into the backdrop's own colours: one more layer over the
  // screen costs a Mali-400 half its frame rate.
  const float danger = g.state == GameState::Menu ? 0 : g.dangerAlpha * (0x44 / 255.0f);
  if (danger <= 0.004f) {
    prims.rectV(0, 0, CANVAS_W, CANVAS_H, d(ch.bgTop), 0.5f, d(ch.bgMid), d(ch.bgBottom), 1);
  } else {
    const Color top = d(ch.bgTop), mid = d(ch.bgMid), bottom = d(ch.bgBottom);
    const float cx = CANVAS_W / 2, cy = CANVAS_H / 2, r0 = CANVAS_H * 0.3f, r1 = CANVAS_H * 0.7f;
    auto mix = [](Color a, Color b, float t) {
      return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1};
    };
    auto at = [&](float x, float y) {
      const float t = y / CANVAS_H;
      const Color c = t < 0.5f ? mix(top, mid, t * 2) : mix(mid, bottom, t * 2 - 1);
      const float r = std::hypot(x - cx, y - cy);
      const float v = danger * std::clamp((r - r0) / (r1 - r0), 0.0f, 1.0f);
      return mix(c, d(C("#f00")), v);
    };
    // even rows: a row edge on the gradient's middle stop
    const int cols = 24, rows = std::max(8, 2 * static_cast<int>(std::lround(cols * CANVAS_H / CANVAS_W / 2)));
    prims.grid(0, 0, CANVAS_W, CANVAS_H, cols, rows, at);
  }
  for (const Nebula& n : g.nebulae) prims.radial(n.x, n.y, n.radius, d(n.color), TRANSPARENT, n.alpha);
  for (const Star& s : g.stars) {
    const Color c = d(s.speed > 1.8f ? ch.starBright : ch.starDim);
    const float sz = s.speed > 1.5f ? 2 : 1;
    prims.rect(s.x, s.y, sz, sz, c, s.brightness);
    if (s.speed > 1.8f) prims.rect(s.x, s.y - s.speed * 1.5f, 1, s.speed * 1.5f, c, s.brightness * 0.3f);
  }
}

void drawHazards(Canvas& ctx, const GameData& g) {
  for (const Hazard& h : g.hazards) {
    ctx.save();
    ctx.translate(h.x, h.y);
    if (h.type == HazardType::Asteroid) {
      ctx.rotate(h.rot);
      ctx.fillStyle(C("#876"));
      ctx.beginPath();
      float r = h.width / 2;
      for (int i = 0; i < 7; i++) {
        float a = static_cast<float>(i) / 7 * PI * 2;
        float rr = r * (0.75f + std::sin(i * 2.1f) * 0.2f);
        if (i == 0) ctx.moveTo(std::cos(a) * rr, std::sin(a) * rr);
        else ctx.lineTo(std::cos(a) * rr, std::sin(a) * rr);
      }
      ctx.closePath();
      ctx.fill();
      ctx.strokeStyle(C("#a98"));
      ctx.lineWidth(1);
      ctx.stroke();
    } else if (h.type == HazardType::Turret) {
      ctx.fillStyle(C("#422"));
      ctx.fillRect(-h.width / 2, -h.height / 2, h.width, h.height);
      ctx.fillStyle(C("#f44"));
      ctx.fillRect(h.side > 0 ? -4 : 0, -3, 8, 6);
      ctx.save();
      ctx.globalAlpha(0.35f);
      ctx.fillStyle(C("#f00"));
      ctx.beginPath();
      ctx.arc(0, 0, h.width * 0.55f, 0, PI * 2);
      ctx.fill();
      ctx.restore();
    } else if (h.type == HazardType::Teleporter) {
      float pulse = 0.6f + std::sin(h.pulse) * 0.25f;
      ctx.save();
      ctx.globalAlpha(0.25f + pulse * 0.2f);
      ctx.strokeStyle(C("#c8f"));
      ctx.lineWidth(2);
      ctx.beginPath();
      ctx.arc(0, 0, h.width / 2 + 4, 0, PI * 2);
      ctx.stroke();
      ctx.restore();
      ctx.save();
      ctx.lighter(true);
      ctx.globalAlpha(pulse * 0.5f);
      ctx.fillStyle(C("#a6f"));
      ctx.beginPath();
      ctx.arc(0, 0, h.width / 2, 0, PI * 2);
      ctx.fill();
      ctx.restore();
      ctx.fillStyle(C("#eef"));
      ctx.font(10);
      ctx.textAlign(Align::Center);
      ctx.fillText("◇", 0, 4);
    }
    ctx.restore();
  }
}

void renderGame(Canvas& ctx, Prims& prims, GameData& g, uint32_t nowMs) {
  ctx.save();
  if (g.screenShake.timer > 0) {
    float d = g.screenShake.duration != 0 ? g.screenShake.duration : 1;
    float a = g.screenShake.intensity * (g.screenShake.timer / d);
    ctx.translate((frand() - 0.5f) * a * 2, (frand() - 0.5f) * a * 2);
  }

  // the title dims the backdrop (black at 35% in the original): done in its
  // colours, not as one more layer over the whole screen
  drawChapterBackground(prims, g, g.state == GameState::Menu ? 0.65f : 1);
  // pause and game over dim the whole field behind their text, the same
  // way (black at 60% and 72% in the original)
  const bool dimmed = g.state == GameState::Paused || g.state == GameState::GameOver;
  if (dimmed) ctx.dim(g.state == GameState::Paused ? 0.4f : 0.28f);
  ctx.drawPrims(prims);

  if (g.state == GameState::Menu) {
    drawMenu(ctx, g, nowMs);
    drawAchievementToast(ctx, g);
    ctx.restore();
    return;
  }

  drawHazards(ctx, g);
  drawGlows(prims, g, nowMs);
  ctx.drawPrims(prims);
  for (const PowerUp& pw : g.powerUps) drawPowerUp(ctx, pw, nowMs);
  drawBullets(prims, g);
  ctx.drawPrims(prims);
  for (const Enemy& e : g.enemies) drawEnemy(ctx, g, e, g.frameCount);

  if (g.state != GameState::GameOver) {
    if (g.player.weaponId == WeaponId::Laser && g.player.laserRamp > 0) drawLaserBeam(ctx, g.player);
    drawPlayer(ctx, g, g.player, nowMs);
    if (g.player2) {
      if (g.player2->weaponId == WeaponId::Laser && g.player2->laserRamp > 0) drawLaserBeam(ctx, *g.player2);
      drawPlayer(ctx, g, *g.player2, nowMs);
    }
  }

  drawParticles(ctx, prims, g);
  drawFlashOverlay(ctx, g);
  drawGrazeIndicator(ctx, g);
  if (g.state == GameState::Playing || g.state == GameState::Paused) {
    drawHUD(ctx, g);
    drawComboBanner(ctx, g, nowMs);
    drawWaveAnnounce(ctx, g);
  }
  if (dimmed) ctx.dim(1);
  if (g.state == GameState::Paused) drawPausedOverlay(ctx, g);
  if (g.state == GameState::GameOver) drawGameOver(ctx, g, nowMs);
  drawAchievementToast(ctx, g);
  drawSlowMotionOverlay(ctx, g);
  ctx.restore();
}
