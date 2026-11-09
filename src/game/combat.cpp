// combat.ts and skills.ts (simulation)
#include <algorithm>
#include <cmath>

#include "game.h"

namespace {

void tryPowerUp(GameData& g, float x, float y) {
  if (!powerUpsEnabled(g)) return;
  if (g.rng.next() > 0.18) return;
  static const PowerUpType types[] = {PowerUpType::Spread, PowerUpType::Speed, PowerUpType::Shield,
                                      PowerUpType::Bomb, PowerUpType::Heal};
  static const double wts[] = {0.28, 0.2, 0.17, 0.15, 0.2};
  double r = g.rng.next();
  PowerUpType t = PowerUpType::Spread;
  for (int i = 0; i < 5; i++) {
    r -= wts[i];
    if (r <= 0) {
      t = types[i];
      break;
    }
  }
  g.powerUps.push_back({x, y, 20, 20, t, 1.5f, {}});
}

void tryWeaponDrop(GameData& g, float x, float y) {
  if (!powerUpsEnabled(g)) return;
  if (g.rng.next() > 0.07) return;
  static const WeaponId alts[] = {WeaponId::ArmorPiercing, WeaponId::Shotgun, WeaponId::Laser, WeaponId::Homing};
  WeaponId w = alts[static_cast<int>(g.rng.next() * 4)];
  g.powerUps.push_back({x, y, 22, 22, PowerUpType::Weapon, 1.4f, w});
}

void startSkillCooldown(Player& p) { p.skillCooldown = getAircraft(p.aircraftId).skillCooldown; }

constexpr int MISSILE_COUNT = 5;
constexpr float MISSILE_SPEED = 9;

void steerBulletToward(Bullet& b, float tx, float ty, float dt, float fallbackSpeed) {
  if (b.homingStrength == 0) return;
  float current = std::atan2(b.vy, b.vx);
  float desired = std::atan2(ty - b.y, tx - b.x);
  float delta = desired - current;
  while (delta > PI) delta -= PI * 2;
  while (delta < -PI) delta += PI * 2;
  float turn = b.homingStrength * dt * 4;
  float next = current + std::max(-turn, std::min(turn, delta));
  float speed = std::hypot(b.vx, b.vy);
  if (speed == 0) speed = fallbackSpeed;
  b.vx = std::cos(next) * speed;
  b.vy = std::sin(next) * speed;
}

bool activateMissileSalvo(GameData& g, Player& p) {
  bool targets = !g.enemies.empty();
  for (int i = 0; i < MISSILE_COUNT; i++) {
    float spread = (i - (MISSILE_COUNT - 1) / 2.0f) * 0.18f;
    float angle = -PI / 2 + spread;
    Bullet b{p.x + std::cos(angle) * 6, p.y - p.height / 2, std::cos(angle) * MISSILE_SPEED,
             std::sin(angle) * MISSILE_SPEED, 5, 10, 2, true, C("#f84")};
    b.homingStrength = targets ? 0.12f : 0.04f;
    g.bullets.push_back(b);
    g.shotsFired++;
  }
  startSkillCooldown(p);
  sfx::playPowerUp();
  return true;
}

bool activateDash(GameData& g, Player& p, float dx, float dy) {
  if (dx == 0 && dy == 0) dy = -1;
  float len = std::hypot(dx, dy);
  if (len == 0) len = 1;
  p.dashVx = dx / len * 16;  // px per frame
  p.dashVy = dy / len * 16;
  p.skillActiveTimer = 0.22f;
  startSkillCooldown(p);
  sfx::playMenuSelect();
  if (g.particles.size() < MAX_PARTICLES)
    g.particles.push_back({p.x, p.y, -p.dashVx * 0.15f, -p.dashVy * 0.15f, 0.25f, 0.25f, 4, C("#c8f"),
                           ParticleType::Trail, {}, 0});
  return true;
}

bool activateEnergyShield(Player& p) {
  p.skillShieldActive = true;
  p.skillShieldTimer = 3;
  p.skillAbsorbedHits = 0;
  p.damageBoost = 0;
  startSkillCooldown(p);
  sfx::playPowerUp();
  return true;
}

}  // namespace

void playerShoot(GameData& g, Player& p) {
  fireWeapon(g, p);
  addParticles(g, p.x, p.y - p.height / 2, 3, getWeapon(p.weaponId).bulletColor, 2, ParticleType::Spark, 1, 3);
}

void explodeKamikaze(GameData& g, Enemy& e) {
  for (Color c : {C("#f80"), C("#fa0"), C("#f40"), C("#fff")})
    addParticles(g, e.x, e.y, 5, c, 4, ParticleType::Explosion, 2, 5);
  addRing(g, e.x, e.y, C("#ff884488"), kamikazeExplosionRadius());
  shake(g, 6, 10);
  sfx::playExplosion();
  e.hp = 0;
}

bool playerHitFromEnemy(GameData& g, Player& p) {
  if (p.skillShieldActive) {
    p.skillAbsorbedHits++;
    p.damageBoost = p.skillAbsorbedHits;
    addParticles(g, p.x, p.y, 10, C("#fd4"), 3, ParticleType::Spark);
    sfx::playHit();
    return false;
  }
  if (p.shieldActive) {
    p.shieldActive = false;
    p.shieldTimer = 0;
    addParticles(g, p.x, p.y, 15, C("#4af"), 3, ParticleType::Spark);
    sfx::playHit();
    return false;
  }
  hurtPlayer(g, p);
  return g.state == GameState::GameOver;
}

void onEnemyKilled(GameData& g, const Enemy& e, float x, float y) {
  g.enemiesKilled++;
  const bool boss = e.type == EnemyType::Boss;
  std::vector<Color> cols = boss ? std::vector<Color>{C("#f40"), C("#fa0"), C("#f60"), C("#fff"), C("#f20")}
                                 : std::vector<Color>{C("#f60"), C("#fa0"), C("#f30"), C("#fff")};
  for (Color c : cols)
    addParticles(g, x, y, (boss ? 50 : 20) / static_cast<int>(cols.size()), c, boss ? 6 : 4,
                 ParticleType::Explosion, boss ? 3 : 2, boss ? 8 : 6);
  addParticles(g, x, y, boss ? 15 : 6, C("#ff8800"), boss ? 4 : 2.5f, ParticleType::Ember, 1, 3);
  addRing(g, x, y, boss ? C("#ff6644") : C("#ff994488"), boss ? 60 : 35);
  g.combo++;
  g.comboTimer = COMBO_WINDOW_S;
  if (g.combo > g.maxCombo) g.maxCombo = g.combo;
  if (g.combo > 2) sfx::playCombo();
  int pts = static_cast<int>(std::floor(e.scoreValue * (1 + (g.combo / 5) * 0.5)));
  g.score += pts;
  addScorePopup(g, x, y - 10, "+" + std::to_string(pts), g.combo >= 5 ? C("#ffdd00") : C("#fff"));
  shake(g, boss ? 12 : 4, boss ? 15 : 6);
  if (boss) {
    sfx::playBigExplosion();
    g.flashAlpha = 0.4f;
    g.flashColor = C("#fff");
    g.slowMotion = 0.3f;
    g.slowMotionTimer = 0.6f;
  } else {
    sfx::playExplosion();
  }
  if (e.type == EnemyType::Splitter) spawnSplitterChildren(g, x, y);
  tryPowerUp(g, x, y);
  tryWeaponDrop(g, x, y);

  awardRunCoins(g, coinRewardForEnemy(e.type), x, y - 22);
  LifetimeStats stats = recordEnemyKill(boss);
  if (stats.enemyKills == 1) queueAchievement(g, AchievementId::FirstBlood);
  if (stats.bossKills >= 10) queueAchievement(g, AchievementId::BossSlayer);
  if (g.combo >= 20) queueAchievement(g, AchievementId::ComboMaster);
}

void updateLaserFire(GameData& g, bool shooting, float dt, Player& p) {
  if (p.weaponId != WeaponId::Laser || !shooting) {
    p.laserRamp = std::max(0.0f, p.laserRamp - dt * 4);
    return;
  }
  p.laserRamp = std::min(3.0f, p.laserRamp + dt * 2.5f);
  g.shotsFired++;
  float dps = 0.35f + p.laserRamp * 0.45f;
  float damage = dps * dt * 60;
  float beamW = 10 + p.powerLevel * 2;
  for (int ei = static_cast<int>(g.enemies.size()) - 1; ei >= 0; ei--) {
    // onEnemyKilled may append splitter children, so index, never hold a reference
    if (ei >= static_cast<int>(g.enemies.size())) continue;
    Enemy& e = g.enemies[ei];
    if (std::fabs(e.x - p.x) > (e.width + beamW) / 2) continue;
    if (e.y >= p.y - p.height / 2 || e.y < 0) continue;
    if (blocksCenterShot(e, p.x)) continue;
    e.hp -= damage;
    g.shotsHit++;
    g.damageDealt += damage;
    e.flashTimer = 0.05f;
    if (e.hp <= 0) {
      Enemy dead = e;
      g.enemies.erase(g.enemies.begin() + ei);
      onEnemyKilled(g, dead, dead.x, dead.y);
    }
  }
  if (frand() > 0.5f)
    addParticles(g, p.x + (frand() - 0.5f) * beamW, p.y - p.height / 2 - frand() * 80, 1, C("#f8f"), 1,
                 ParticleType::Spark, 1, 2);
}

void activateBomb(GameData& g, Player& p) {
  std::erase_if(g.bullets, [](const Bullet& b) { return !b.isPlayer; });
  for (int ei = static_cast<int>(g.enemies.size()) - 1; ei >= 0; ei--) {
    if (ei >= static_cast<int>(g.enemies.size())) continue;
    Enemy& e = g.enemies[ei];
    e.hp -= 3;
    g.damageDealt += 3;
    e.flashTimer = 0.15f;
    addParticles(g, e.x, e.y, 8, C("#fff"), 4, ParticleType::Spark);
    if (e.hp <= 0) {
      Enemy dead = e;
      g.enemies.erase(g.enemies.begin() + ei);
      onEnemyKilled(g, dead, dead.x, dead.y);
    }
  }
  g.flashAlpha = 0.6f;
  g.flashColor = C("#fff");
  shake(g, 8, 20);
  sfx::playBigExplosion();
  addParticles(g, p.x, p.y, 40, C("#fff"), 6, ParticleType::Explosion, 3, 8);
  addRing(g, p.x, p.y, C("#88ddff"), 120);
}

void hurtPlayer(GameData& g, Player& p) {
  if (isPracticeInvincible(g)) return;
  g.waveDamageTaken = true;
  p.hp--;
  p.invincibleTimer = 2;
  shake(g, 10, 12);
  g.flashAlpha = 0.25f;
  g.flashColor = C("#ff2200");
  addParticles(g, p.x, p.y, 25, C("#f44"), 4, ParticleType::Explosion, 2, 6);
  addParticles(g, p.x, p.y, 10, C("#ff8800"), 3, ParticleType::Ember, 1, 3);
  sfx::playExplosion();
  if (isCoopMode(g)) {
    if (shouldTeamWipe(g)) killPlayer(g, p);
  } else if (p.hp <= 0) {
    killPlayer(g, p);
  }
}

void killPlayer(GameData& g, Player& p) {
  g.state = GameState::GameOver;
  addParticles(g, p.x, p.y, 60, C("#f60"), 6, ParticleType::Explosion, 3, 8);
  addParticles(g, p.x, p.y, 30, C("#fff"), 5, ParticleType::Spark, 2, 5);
  addParticles(g, p.x, p.y, 20, C("#ff8800"), 4, ParticleType::Ember, 2, 4);
  addRing(g, p.x, p.y, C("#ff4444"), 80);
  shake(g, 15, 30);
  g.flashAlpha = 0.8f;
  g.flashColor = C("#fff");
  sfx::playGameOver();
  endRunScores(g);
}

// ---- skills ----

void updateHomingBullets(GameData& g, float dt) {
  for (Bullet& b : g.bullets) {
    if (b.homingStrength == 0) continue;
    if (b.isPlayer) {
      const Enemy* target = nullptr;
      float best = INFINITY;
      for (const Enemy& e : g.enemies) {
        float dx = e.x - b.x, dy = e.y - b.y;
        float d = dx * dx + dy * dy;
        if (d < best) {
          best = d;
          target = &e;
        }
      }
      if (!target) continue;
      steerBulletToward(b, target->x, target->y, dt, MISSILE_SPEED);
      continue;
    }
    steerBulletToward(b, g.player.x, g.player.y, dt, 3);
  }
}

bool isPlayerVulnerable(const Player& p) { return p.invincibleTimer <= 0 && p.skillActiveTimer <= 0; }

void tickSkills(GameData&, Player& p, float dt) {
  if (p.skillCooldown > 0) p.skillCooldown = std::max(0.0f, p.skillCooldown - dt);
  if (p.skillActiveTimer > 0) {
    p.skillActiveTimer = std::max(0.0f, p.skillActiveTimer - dt);
    p.x += p.dashVx * dt * 60;
    p.y += p.dashVy * dt * 60;
  }
  if (p.skillShieldTimer > 0) {
    p.skillShieldTimer -= dt;
    if (p.skillShieldTimer <= 0) {
      p.skillShieldActive = false;
      p.skillAbsorbedHits = 0;
    }
  }
}

bool tryActivateSkill(GameData& g, Player& p, float moveX, float moveY) {
  if (p.skillCooldown > 0) return false;
  switch (getAircraft(p.aircraftId).skill) {
    case AircraftSkill::MissileSalvo: return activateMissileSalvo(g, p);
    case AircraftSkill::Dash: return activateDash(g, p, moveX, moveY);
    case AircraftSkill::EnergyShield: return activateEnergyShield(p);
  }
  return false;
}

float getSkillCooldownRatio(const Player& p) {
  float cd = getAircraft(p.aircraftId).skillCooldown;
  if (cd <= 0) return 1;
  return 1 - p.skillCooldown / cd;
}
