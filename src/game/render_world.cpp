// render/world.ts and render/enemies.ts
#include <cmath>

#include "../canvas.h"
#include "game.h"

void drawLaserBeam(Canvas& ctx, const Player& p) {
  float beamW = 10 + p.powerLevel * 2;
  float top = std::fmax(0, p.y - p.height / 2 - 280);
  ctx.save();
  ctx.lighter(true);
  ctx.globalAlpha(0.35f + p.laserRamp * 0.15f);
  ctx.fillStyle(Gradient::linear(p.x, p.y - p.height / 2, p.x, top)
                    .add(0, C("#fff"))
                    .add(0.2f, C("#f8f"))
                    .add(1, C("#f0f0")));
  ctx.fillRect(p.x - beamW / 2, top, beamW, p.y - p.height / 2 - top);
  ctx.strokeStyle(C("#fff"));
  ctx.globalAlpha(0.5f);
  ctx.lineWidth(2);
  ctx.beginPath();
  ctx.moveTo(p.x, p.y - p.height / 2);
  ctx.lineTo(p.x, top);
  ctx.stroke();
  ctx.restore();
}

void drawPlayer(Canvas& ctx, const GameData& g, const Player& p, uint32_t nowMs) {
  const int seat = coopSeatForShip(g, p);
  const ShipPalette craft = resolveShipPalette(p.aircraftId, seat);
  if (p.invincibleTimer > 0 && static_cast<int>(std::floor(p.invincibleTimer * 10)) % 2 == 0) return;
  if (p.skillActiveTimer > 0 && static_cast<int>(std::floor(p.skillActiveTimer * 20)) % 2 == 0) return;
  const float now = static_cast<float>(nowMs);
  const float W = p.width, H = p.height;

  ctx.save();
  ctx.translate(p.x, p.y);
  ctx.rotate(p.tilt * 0.18f);  // banking
  float hpR = static_cast<float>(p.hp) / p.maxHp;
  Color hpCol = hpR > 0.6f ? C("#44ffaa") : hpR > 0.34f ? C("#ffaa44") : C("#ff4444");
  ctx.save();
  ctx.globalAlpha(0.15f + (1 - hpR) * 0.2f);
  ctx.shadow(hpCol, 12);
  ctx.strokeStyle(hpCol);
  ctx.lineWidth(1.5f);
  ctx.beginPath();
  ctx.arc(0, 2, 20, 0, PI * 2);
  ctx.stroke();
  ctx.restore();

  if (p.shieldActive) {
    ctx.strokeStyle(C("#4af"));
    ctx.lineWidth(2);
    ctx.globalAlpha(0.5f + std::sin(now * 0.01f) * 0.2f);
    ctx.beginPath();
    ctx.arc(0, 0, 24, 0, PI * 2);
    ctx.stroke();
    ctx.globalAlpha(0.08f);
    ctx.fillStyle(C("#4af"));
    ctx.beginPath();
    ctx.arc(0, 0, 22, 0, PI * 2);
    ctx.fill();
    ctx.globalAlpha(1);
  }
  if (p.skillShieldActive) {
    ctx.strokeStyle(C("#fd4"));
    ctx.lineWidth(2);
    ctx.globalAlpha(0.55f + std::sin(now * 0.012f) * 0.2f);
    ctx.beginPath();
    for (int i = 0; i < 6; i++) {
      float a = PI / 6 + i * PI / 3;
      if (i == 0) ctx.moveTo(std::cos(a) * 26, std::sin(a) * 26);
      else ctx.lineTo(std::cos(a) * 26, std::sin(a) * 26);
    }
    ctx.closePath();
    ctx.stroke();
    ctx.globalAlpha(1);
  }

  float flicker = 0.8f + frand() * 0.4f;
  float flameH = 10 + std::sin(now * 0.02f) * 3;
  ctx.fillStyle(Gradient::linear(0, H / 3, 0, H / 2 + flameH * flicker)
                    .add(0, craft.cockpitColor)
                    .add(0.4f, craft.engineColor)
                    .add(1, TRANSPARENT));
  ctx.beginPath();
  ctx.moveTo(-5, H / 3);
  ctx.lineTo(0, H / 2 + flameH * flicker);
  ctx.lineTo(5, H / 3);
  ctx.fill();
  ctx.fillStyle(Gradient::linear(0, H / 3, 0, H / 2 + flameH * flicker * 0.7f)
                    .add(0, withAlpha(craft.engineColor, 0x44))
                    .add(1, TRANSPARENT));
  ctx.beginPath();
  ctx.moveTo(-8, H / 3);
  ctx.lineTo(0, H / 2 + flameH * flicker * 0.7f);
  ctx.lineTo(8, H / 3);
  ctx.fill();

  ctx.fillStyle(Gradient::linear(0, -H / 2, 0, H / 2)
                    .add(0, craft.hullTop)
                    .add(0.4f, craft.hullMid)
                    .add(1, craft.hullBottom));
  ctx.beginPath();
  ctx.moveTo(0, -H / 2);
  ctx.lineTo(W / 2, H / 3);
  ctx.lineTo(W / 2 + 4, H / 2);
  ctx.lineTo(W / 4, H / 4);
  ctx.lineTo(0, H / 3);
  ctx.lineTo(-W / 4, H / 4);
  ctx.lineTo(-W / 2 - 4, H / 2);
  ctx.lineTo(-W / 2, H / 3);
  ctx.closePath();
  ctx.fill();
  ctx.strokeStyle(withAlpha(craft.hullTop, 0x33));
  ctx.lineWidth(1);
  ctx.stroke();
  if (seat) {
    ctx.strokeStyle(craft.accent);
    ctx.globalAlpha(0.55f);
    ctx.lineWidth(1.5f);
    ctx.beginPath();
    ctx.moveTo(-W / 2 + 2, H / 5);
    ctx.lineTo(W / 2 - 2, H / 5);
    ctx.stroke();
    ctx.globalAlpha(1);
  }
  ctx.fillStyle(craft.cockpitColor);
  ctx.beginPath();
  ctx.ellipse(0, -4, 4, 7);
  ctx.fill();
  ctx.save();
  ctx.globalAlpha(0.3f);
  ctx.shadow(craft.cockpitColor, 8);
  ctx.fillStyle(craft.cockpitColor);
  ctx.beginPath();
  ctx.ellipse(0, -4, 3, 5);
  ctx.fill();
  ctx.restore();
  ctx.strokeStyle(withAlpha(craft.accent, 0x66));
  ctx.lineWidth(1);
  ctx.beginPath();
  ctx.moveTo(-4, 0);
  ctx.lineTo(-W / 2 + 2, H / 3 - 2);
  ctx.moveTo(4, 0);
  ctx.lineTo(W / 2 - 2, H / 3 - 2);
  ctx.stroke();
  ctx.restore();
}

void drawBullet(Canvas& ctx, const Bullet& b) {
  ctx.save();
  ctx.translate(b.x, b.y);
  if (b.isPlayer) {
    ctx.fillStyle(Gradient::linear(0, -b.height / 2, 0, b.height / 2)
                      .add(0, C("#fff"))
                      .add(0.3f, b.color)
                      .add(1, C("#036")));
    ctx.fillRect(-b.width / 2, -b.height / 2, b.width, b.height);
    ctx.save();
    ctx.lighter(true);
    ctx.globalAlpha(0.35f);
    ctx.fillStyle(b.color);
    ctx.fillRect(-b.width * 1.2f, -b.height / 2, b.width * 2.4f, b.height);
    ctx.restore();
  } else {
    ctx.save();
    ctx.lighter(true);
    ctx.globalAlpha(0.3f);
    ctx.fillStyle(b.color);
    ctx.beginPath();
    ctx.arc(0, 0, b.width / 2 + 3, 0, PI * 2);
    ctx.fill();
    ctx.restore();
    ctx.fillStyle(b.color);
    ctx.beginPath();
    ctx.arc(0, 0, b.width / 2 + 1, 0, PI * 2);
    ctx.fill();
    ctx.fillStyle(C("#fff"));
    ctx.beginPath();
    ctx.arc(0, 0, b.width / 4, 0, PI * 2);
    ctx.fill();
  }
  ctx.restore();
}

void drawPowerUp(Canvas& ctx, const PowerUp& pw, uint32_t nowMs) {
  ctx.save();
  ctx.translate(pw.x, pw.y);
  float pulse = 1 + std::sin(static_cast<float>(nowMs) * 0.008f) * 0.15f;
  ctx.scale(pulse);
  Color c;
  const char* ico = "?";
  switch (pw.type) {
    case PowerUpType::Spread: c = C("#f80"), ico = "S"; break;
    case PowerUpType::Speed: c = C("#0f8"), ico = "F"; break;
    case PowerUpType::Shield: c = C("#48f"), ico = "◇"; break;
    case PowerUpType::Bomb: c = C("#f44"), ico = "B"; break;
    case PowerUpType::Heal: c = C("#4f8"), ico = "+"; break;
    case PowerUpType::Weapon: c = C("#fd4"), ico = "W"; break;
  }
  if (pw.type == PowerUpType::Weapon && pw.weaponId) c = getWeapon(*pw.weaponId).hudColor;
  ctx.save();
  ctx.lighter(true);
  ctx.globalAlpha(0.15f);
  ctx.fillStyle(c);
  ctx.beginPath();
  ctx.arc(0, 0, 18, 0, PI * 2);
  ctx.fill();
  ctx.restore();
  ctx.fillStyle(c);
  ctx.globalAlpha(0.25f);
  ctx.beginPath();
  ctx.arc(0, 0, 14, 0, PI * 2);
  ctx.fill();
  ctx.globalAlpha(1);
  ctx.strokeStyle(c);
  ctx.lineWidth(2);
  ctx.beginPath();
  ctx.arc(0, 0, 12, 0, PI * 2);
  ctx.stroke();
  ctx.fillStyle(C("#fff"));
  if (pw.type == PowerUpType::Heal) {
    ctx.fillRect(-6, -2, 12, 4);
    ctx.fillRect(-2, -6, 4, 12);
  } else {
    bool weapon = pw.type == PowerUpType::Weapon && pw.weaponId;
    ctx.font(weapon ? 10 : 12, Font::Bold);
    ctx.textAlign(Align::Center);
    ctx.textBaseline(Baseline::Middle);
    ctx.fillText(weapon ? getWeapon(*pw.weaponId).shortName : ico, 0, 1);
  }
  ctx.restore();
}

void drawEnemy(Canvas& ctx, const GameData& g, const Enemy& e, int frame) {
  ctx.save();
  ctx.translate(e.x, e.y);
  const bool f = e.flashTimer > 0;
  const float W = e.width, H = e.height;
  const float fr = static_cast<float>(frame);

  ctx.save();
  ctx.globalAlpha(0.2f);
  ctx.lighter(true);
  Color glow = e.type == EnemyType::Boss       ? bossAccentColors(e.bossPattern).glow
               : e.type == EnemyType::Healer   ? C("#4f8")
               : e.type == EnemyType::Kamikaze ? C("#fa4")
               : e.type == EnemyType::Sniper   ? C("#f6a")
                                               : C("#ff8844");
  ctx.fillStyle(Gradient::radialGradient(0, -H / 3, 0, 0, -H / 3, W * 0.4f).add(0, glow).add(1, TRANSPARENT));
  ctx.fillRect(-W, -H, W * 2, H * 0.8f);
  ctx.restore();

  if (e.type == EnemyType::Healer) {
    ctx.save();
    ctx.globalAlpha(0.25f + std::sin(fr * 0.06f) * 0.1f);
    ctx.strokeStyle(C("#4f8"));
    ctx.lineWidth(1.5f);
    ctx.beginPath();
    ctx.arc(0, 0, HEAL_RADIUS * 0.55f, 0, PI * 2);
    ctx.stroke();
    ctx.restore();
  }

  if (e.type == EnemyType::Sniper && e.state == EnemyState::Aim && e.aimTimer > 0) {
    const Player& p = g.player;
    ctx.save();
    ctx.globalAlpha(0.35f + (1 - e.aimTimer / SNIPER_AIM_FRAMES) * 0.55f);
    ctx.strokeStyle(C("#f44"));
    ctx.lineWidth(1.5f);
    ctx.lineDash(4, 4);
    ctx.beginPath();
    ctx.moveTo(0, H / 2);
    ctx.lineTo(p.x - e.x, p.y - e.y);
    ctx.stroke();
    ctx.restore();
  }

  switch (e.type) {
    case EnemyType::Boss: {
      BossAccent accent = bossAccentColors(e.bossPattern);
      ctx.fillStyle(Gradient::linear(0, -H / 2, 0, H / 2)
                        .add(0, f ? C("#fff") : accent.hullTop)
                        .add(1, f ? C("#f88") : accent.hullBottom));
      ctx.beginPath();
      ctx.moveTo(0, H / 2);
      ctx.lineTo(-W / 2, -H / 4);
      ctx.lineTo(-W / 3, -H / 2);
      ctx.lineTo(0, -H / 3);
      ctx.lineTo(W / 3, -H / 2);
      ctx.lineTo(W / 2, -H / 4);
      ctx.closePath();
      ctx.fill();
      float eyeR = 6 + std::sin(fr * 0.08f) * 2;
      ctx.fillStyle(accent.eye);
      ctx.beginPath();
      ctx.arc(0, 0, eyeR, 0, PI * 2);
      ctx.fill();
      ctx.save();
      ctx.globalAlpha(0.3f);
      ctx.shadow(accent.glow, 15);
      ctx.beginPath();
      ctx.arc(0, 0, eyeR, 0, PI * 2);
      ctx.fill();
      ctx.restore();
      ctx.save();
      ctx.globalAlpha(0.15f + std::sin(fr * 0.05f) * 0.1f);
      ctx.strokeStyle(accent.eye);
      ctx.lineWidth(1);
      ctx.stroke();
      ctx.restore();
      break;
    }
    case EnemyType::Splitter:
      ctx.fillStyle(f ? C("#fff") : C("#94f"));
      ctx.beginPath();
      ctx.moveTo(0, H / 2);
      ctx.lineTo(-W / 2, 0);
      ctx.lineTo(-W / 4, -H / 2);
      ctx.lineTo(W / 4, -H / 2);
      ctx.lineTo(W / 2, 0);
      ctx.closePath();
      ctx.fill();
      ctx.strokeStyle(f ? C("#eef") : C("#c6f"));
      ctx.lineWidth(1.5f);
      ctx.beginPath();
      ctx.moveTo(0, -H / 3);
      ctx.lineTo(0, H / 3);
      ctx.stroke();
      break;
    case EnemyType::Sniper:
      ctx.fillStyle(f ? C("#fff") : C("#624"));
      ctx.fillRect(-W / 2, -H / 2, W, H);
      ctx.fillStyle(f ? C("#faa") : C("#f84"));
      ctx.fillRect(-4, -H / 2 - 6, 8, 10);
      break;
    case EnemyType::Shielded:
      ctx.fillStyle(f ? C("#fff") : C("#468"));
      ctx.beginPath();
      ctx.moveTo(0, H / 2);
      ctx.lineTo(-W / 2, 0);
      ctx.lineTo(-W / 3, -H / 2);
      ctx.lineTo(W / 3, -H / 2);
      ctx.lineTo(W / 2, 0);
      ctx.closePath();
      ctx.fill();
      ctx.fillStyle(f ? C("#bdf") : C("#8cf"));
      ctx.beginPath();
      ctx.moveTo(-W / 2, H / 4);
      ctx.lineTo(0, H / 2 + 4);
      ctx.lineTo(W / 2, H / 4);
      ctx.lineTo(W / 3, H / 6);
      ctx.lineTo(0, H / 3);
      ctx.lineTo(-W / 3, H / 6);
      ctx.closePath();
      ctx.fill();
      break;
    case EnemyType::Kamikaze: {
      bool hot = e.state == EnemyState::Rush;
      ctx.fillStyle(f ? C("#fff") : hot ? C("#f80") : C("#c60"));
      ctx.beginPath();
      ctx.moveTo(0, H / 2);
      ctx.lineTo(-W / 2, -H / 4);
      ctx.lineTo(0, -H / 2);
      ctx.lineTo(W / 2, -H / 4);
      ctx.closePath();
      ctx.fill();
      if (hot) {
        ctx.save();
        ctx.globalAlpha(0.35f + std::sin(fr * 0.2f) * 0.2f);
        ctx.fillStyle(C("#ff4"));
        ctx.beginPath();
        ctx.arc(0, 0, W * 0.55f, 0, PI * 2);
        ctx.fill();
        ctx.restore();
      }
      break;
    }
    case EnemyType::Healer:
      ctx.fillStyle(f ? C("#fff") : C("#282"));
      ctx.beginPath();
      ctx.arc(0, 0, W / 2, 0, PI * 2);
      ctx.fill();
      ctx.fillStyle(f ? C("#afa") : C("#4f8"));
      ctx.fillRect(-3, -H / 4, 6, H / 2);
      ctx.fillRect(-W / 4, -3, W / 2, 6);
      break;
    case EnemyType::Mini:
      ctx.fillStyle(f ? C("#fff") : C("#b6f"));
      ctx.beginPath();
      ctx.moveTo(0, H / 2);
      ctx.lineTo(-W / 2, -H / 2);
      ctx.lineTo(W / 2, -H / 2);
      ctx.closePath();
      ctx.fill();
      break;
    case EnemyType::Tank:
      ctx.fillStyle(f ? C("#fff") : C("#862"));
      ctx.beginPath();
      ctx.moveTo(0, H / 2);
      ctx.lineTo(-W / 2, 0);
      ctx.lineTo(-W / 3, -H / 2);
      ctx.lineTo(W / 3, -H / 2);
      ctx.lineTo(W / 2, 0);
      ctx.closePath();
      ctx.fill();
      ctx.fillStyle(f ? C("#fda") : C("#a83"));
      ctx.fillRect(-6, -H / 4, 12, H / 2);
      break;
    case EnemyType::Fast:
      ctx.fillStyle(f ? C("#fff") : C("#4c4"));
      ctx.beginPath();
      ctx.moveTo(0, H / 2);
      ctx.lineTo(-W / 2, -H / 2);
      ctx.lineTo(0, -H / 4);
      ctx.lineTo(W / 2, -H / 2);
      ctx.closePath();
      ctx.fill();
      break;
    default:
      ctx.fillStyle(Gradient::linear(0, -H / 2, 0, H / 2)
                        .add(0, f ? C("#fff") : C("#c44"))
                        .add(1, f ? C("#faa") : C("#822")));
      ctx.beginPath();
      ctx.moveTo(0, H / 2);
      ctx.lineTo(-W / 2, -H / 4);
      ctx.lineTo(-W / 4, -H / 2);
      ctx.lineTo(W / 4, -H / 2);
      ctx.lineTo(W / 2, -H / 4);
      ctx.closePath();
      ctx.fill();
      ctx.fillStyle(f ? C("#fff8") : C("#f668"));
      ctx.beginPath();
      ctx.arc(0, 0, 4, 0, PI * 2);
      ctx.fill();
      break;
  }
  ctx.restore();
}
