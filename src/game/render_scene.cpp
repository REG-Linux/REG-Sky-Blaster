// engine.ts render(), chapters.ts drawChapterBackground(), hazards.ts drawHazards()
#include <cmath>

#include "../canvas.h"
#include "game.h"

void drawChapterBackground(Canvas& ctx, const GameData& g) {
  const ChapterDef& ch = getChapter(g.chapterId);
  ctx.fillStyle(Gradient::linear(0, 0, 0, CANVAS_H).add(0, ch.bgTop).add(0.5f, ch.bgMid).add(1, ch.bgBottom));
  ctx.fillRect(0, 0, CANVAS_W, CANVAS_H);

  for (const Nebula& n : g.nebulae) {
    ctx.globalAlpha(n.alpha);
    ctx.fillStyle(Gradient::radialGradient(n.x, n.y, 0, n.x, n.y, n.radius).add(0, n.color).add(1, TRANSPARENT));
    ctx.fillRect(n.x - n.radius, n.y - n.radius, n.radius * 2, n.radius * 2);
  }
  ctx.globalAlpha(1);

  for (const Star& s : g.stars) {
    ctx.globalAlpha(s.brightness);
    ctx.fillStyle(s.speed > 1.8f ? ch.starBright : ch.starDim);
    float sz = s.speed > 1.5f ? 2 : 1;
    ctx.fillRect(s.x, s.y, sz, sz);
    if (s.speed > 1.8f) {
      ctx.globalAlpha(s.brightness * 0.3f);
      ctx.fillRect(s.x, s.y - s.speed * 1.5f, 1, s.speed * 1.5f);
    }
  }
  ctx.globalAlpha(1);
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

void renderGame(Canvas& ctx, GameData& g, uint32_t nowMs) {
  ctx.save();
  ctx.fillStyle(C("#000"));
  ctx.fillRect(0, 0, CANVAS_W, CANVAS_H);

  if (g.screenShake.timer > 0) {
    float d = g.screenShake.duration != 0 ? g.screenShake.duration : 1;
    float a = g.screenShake.intensity * (g.screenShake.timer / d);
    ctx.translate((frand() - 0.5f) * a * 2, (frand() - 0.5f) * a * 2);
  }

  drawChapterBackground(ctx, g);

  if (g.state == GameState::Menu) {
    drawMenu(ctx, g, nowMs);
    drawAchievementToast(ctx, g);
    ctx.restore();
    return;
  }

  drawHazards(ctx, g);
  for (const PowerUp& pw : g.powerUps) drawPowerUp(ctx, pw, nowMs);
  for (const Bullet& b : g.bullets)
    if (!b.isPlayer) drawBullet(ctx, b);
  for (const Bullet& b : g.bullets)
    if (b.isPlayer) drawBullet(ctx, b);
  for (const Enemy& e : g.enemies) drawEnemy(ctx, g, e, g.frameCount);

  if (g.state != GameState::GameOver) {
    if (g.player.weaponId == WeaponId::Laser && g.player.laserRamp > 0) drawLaserBeam(ctx, g.player);
    drawPlayer(ctx, g, g.player, nowMs);
    if (g.player2) {
      if (g.player2->weaponId == WeaponId::Laser && g.player2->laserRamp > 0) drawLaserBeam(ctx, *g.player2);
      drawPlayer(ctx, g, *g.player2, nowMs);
    }
  }

  drawParticles(ctx, g);
  drawFlashOverlay(ctx, g);
  drawDangerVignette(ctx, g);
  drawGrazeIndicator(ctx, g);
  if (g.state == GameState::Playing || g.state == GameState::Paused) {
    drawHUD(ctx, g);
    drawComboBanner(ctx, g, nowMs);
    drawWaveAnnounce(ctx, g);
  }
  if (g.state == GameState::Paused) drawPausedOverlay(ctx, g);
  if (g.state == GameState::GameOver) drawGameOver(ctx, g, nowMs);
  drawAchievementToast(ctx, g);
  drawSlowMotionOverlay(ctx, g);
  ctx.restore();
}
