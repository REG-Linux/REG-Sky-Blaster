// systems/*.ts: player controller, waves, bullets, collisions, power-ups,
// ambient effects.
#include <algorithm>
#include <cmath>

#include "game.h"

namespace {

bool overlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
  return std::fabs(ax - bx) < (aw + bw) / 2 && std::fabs(ay - by) < (ah + bh) / 2;
}

void clampToField(Player& p) {
  p.x = std::max(p.width / 2, std::min(CANVAS_W - p.width / 2, p.x));
  p.y = std::max(p.height / 2, std::min(CANVAS_H - p.height / 2, p.y));
}

// One ship from its input. The original drives the co-op guest with a
// trimmed copy (no skills); local co-op gives both ships the full set.
void updateShip(GameData& g, Player& p, InputState& input, float dt) {
  float mx = 0, my = 0;
  if (input.left) mx--;
  if (input.right) mx++;
  if (input.up) my--;
  if (input.down) my++;
  if (mx != 0 || my != 0) {
    float l = std::sqrt(mx * mx + my * my);
    p.x += mx / l * p.speed;
    p.y += my / l * p.speed;
  }
  clampToField(p);
  p.tilt += (mx * 0.6f - p.tilt) * 0.15f;

  const bool shooting = g.autoFire || input.shoot;
  if (p.weaponId == WeaponId::Laser) {
    updateLaserFire(g, shooting, dt, p);
  } else if (shooting) {
    p.shootTimer--;
    if (p.shootTimer <= 0) {
      playerShoot(g, p);
      p.shootTimer = p.shootInterval;
    }
  } else {
    p.shootTimer = std::min(p.shootTimer, 3.0f);
  }
  if (input.bomb) {
    input.bomb = false;
    if (p.bombs > 0) {
      p.bombs--;
      activateBomb(g, p);
    }
  }
  if (input.skill) {
    input.skill = false;
    tryActivateSkill(g, p, mx, my);
  }
  tickSkills(g, p, dt);
  clampToField(p);

  if (p.invincibleTimer > 0) p.invincibleTimer -= dt;
  if (p.shieldTimer > 0) {
    p.shieldTimer -= dt;
    if (p.shieldTimer <= 0) p.shieldActive = false;
  }
  if (p.grazeTimer > 0) p.grazeTimer -= dt;
  if (frand() > 0.3f)
    addParticles(g, p.x + (frand() - 0.5f) * 8, p.y + p.height / 2 + 2, 1, frand() > 0.5f ? C("#0af") : C("#06f"),
                 1.5f, ParticleType::Trail, 2, 4);
}

void updateGrazeFor(GameData& g, Player& p) {
  if (!isPlayerVulnerable(p)) return;
  for (Bullet& b : g.bullets) {
    if (b.isPlayer || b.grazed) continue;
    float dx = b.x - p.x, dy = b.y - p.y;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < GRAZE_RADIUS && dist > p.width * 0.25f) {
      b.grazed = true;
      g.score += 10;
      p.grazeTimer = 0.3f;
      p.grazeCount++;
      if (p.grazeCount >= 50) queueAchievement(g, AchievementId::GrazeKing);
      sfx::playGraze();
      addParticles(g, p.x + dx * 0.5f, p.y + dy * 0.5f, 3, C("#aaeeff"), 1.5f, ParticleType::Spark, 1, 2);
    }
  }
}

bool resolveEnemyBulletsFor(GameData& g, Player& p) {
  if (!isPlayerVulnerable(p)) return false;
  for (int bi = static_cast<int>(g.bullets.size()) - 1; bi >= 0; bi--) {
    // a hit makes the ship invincible: the other bullets on it this frame
    // must not take more lives (the original's did)
    if (!isPlayerVulnerable(p)) return false;
    if (bi >= static_cast<int>(g.bullets.size())) continue;
    const Bullet& b = g.bullets[bi];
    if (b.isPlayer) continue;
    if (!overlap(b.x, b.y, b.width, b.height, p.x, p.y, p.width * 0.4f, p.height * 0.4f)) continue;
    g.bullets.erase(g.bullets.begin() + bi);
    if (p.skillShieldActive) {
      p.skillAbsorbedHits++;
      p.damageBoost = p.skillAbsorbedHits;
      addParticles(g, p.x, p.y, 12, C("#fd4"), 3, ParticleType::Spark, 2, 4);
      addRing(g, p.x, p.y, C("#ffcc44"), 25);
      sfx::playHit();
    } else if (p.shieldActive) {
      p.shieldActive = false;
      p.shieldTimer = 0;
      addParticles(g, p.x, p.y, 20, C("#4af"), 4, ParticleType::Spark, 2, 5);
      addRing(g, p.x, p.y, C("#44aaff"), 30);
      sfx::playHit();
    } else {
      hurtPlayer(g, p);
      if (g.state == GameState::GameOver) return true;
    }
  }
  return false;
}

bool resolveEnemyContactFor(GameData& g, Player& p) {
  if (!isPlayerVulnerable(p)) return false;
  for (int ei = static_cast<int>(g.enemies.size()) - 1; ei >= 0; ei--) {
    if (!isPlayerVulnerable(p)) return false;  // one hit per frame, as with bullets
    if (ei >= static_cast<int>(g.enemies.size())) continue;
    Enemy& e = g.enemies[ei];
    if (!overlap(p.x, p.y, p.width * 0.4f, p.height * 0.4f, e.x, e.y, e.width, e.height)) continue;
    if (e.type == EnemyType::Kamikaze) {
      explodeKamikaze(g, e);
      if (isKamikazeBlastHit(e, p.x, p.y) && playerHitFromEnemy(g, p)) return true;
      Enemy dead = g.enemies[ei];
      g.enemies.erase(g.enemies.begin() + ei);
      onEnemyKilled(g, dead, dead.x, dead.y);
      continue;
    }
    e.hp -= 2;
    e.flashTimer = 0.1f;
    if (playerHitFromEnemy(g, p)) return true;
    break;
  }
  return false;
}

void applyPowerUp(GameData& g, Player& p, const PowerUp& pw) {
  addParticles(g, pw.x, pw.y, 15, C("#4f4"), 3, ParticleType::Spark, 2, 4);
  switch (pw.type) {
    case PowerUpType::Spread:
      p.powerLevel = std::min(3, p.powerLevel + 1);
      sfx::playPowerUp();
      addScorePopup(g, pw.x, pw.y, "POWER UP", C("#f80"));
      break;
    case PowerUpType::Speed:
      p.shootInterval = std::max(3.0f, p.shootInterval - 1);
      sfx::playPowerUp();
      addScorePopup(g, pw.x, pw.y, "FIRE RATE", C("#0f8"));
      break;
    case PowerUpType::Shield:
      p.shieldActive = true;
      p.shieldTimer = 10;
      sfx::playPowerUp();
      addScorePopup(g, pw.x, pw.y, "SHIELD", C("#48f"));
      break;
    case PowerUpType::Bomb:
      // one more in stock; with the stock full it goes off at once
      if (p.bombs < BOMBS_MAX) {
        p.bombs++;
        sfx::playPowerUp();
        addScorePopup(g, pw.x, pw.y, "+1 BOMB", C("#f44"));
      } else {
        activateBomb(g, p);
      }
      break;
    case PowerUpType::Heal:
      if (p.hp < p.maxHp) {
        p.hp = std::min(p.maxHp, p.hp + 1);
        sfx::playHeal();
        addParticles(g, p.x, p.y, 20, C("#44ff88"), 3, ParticleType::Spark, 2, 4);
        addParticles(g, p.x, p.y, 10, C("#88ffaa"), 2, ParticleType::Explosion, 1, 3);
        addRing(g, p.x, p.y, C("#44ff88"), 30);
        g.flashAlpha = 0.15f;
        g.flashColor = C("#44ff88");
        addScorePopup(g, pw.x, pw.y, "+1 HP", C("#4f8"));
      } else {
        g.score += 500;
        sfx::playPowerUp();
        addScorePopup(g, pw.x, pw.y, "+500", C("#fd4"));
      }
      break;
    case PowerUpType::Weapon:
      if (pw.weaponId) {
        p.weaponId = *pw.weaponId;
        sfx::playPowerUp();
        const WeaponDef& w = getWeapon(*pw.weaponId);
        addScorePopup(g, pw.x, pw.y, w.shortName, w.hudColor);
      }
      break;
  }
}

void stepParticles(GameData& g, float dt, float tm, bool playing) {
  for (Particle& pt : g.particles) {
    pt.x += pt.vx * tm;
    pt.y += pt.vy * tm;
    if (playing) {
      pt.life -= dt * (pt.type == ParticleType::Score ? 1 : tm);
      if (pt.type == ParticleType::Trail) {
        pt.vy += 0.1f;
        pt.vx *= 0.94f;
        pt.vy *= 0.94f;
      } else if (pt.type == ParticleType::Ember) {
        pt.vy += 0.04f;
        pt.vx *= 0.98f;
        pt.vy *= 0.98f;
      } else {
        pt.vx *= 0.96f;
        pt.vy *= 0.96f;
      }
    } else {
      pt.life -= dt;
      pt.vx *= 0.96f;
      pt.vy *= 0.96f;
    }
  }
  std::erase_if(g.particles, [](const Particle& pt) { return pt.life <= 0; });
}

void stepBackdrop(GameData& g, float tm) {
  for (Star& s : g.stars) {
    s.y += s.speed * tm;
    if (s.y > CANVAS_H) {
      s.y = -2;
      s.x = frand() * CANVAS_W;
    }
  }
  for (Nebula& n : g.nebulae) {
    n.y += n.speed * tm;
    if (n.y - n.radius > CANVAS_H) {
      n.y = -n.radius;
      n.x = frand() * CANVAS_W;
    }
  }
}

}  // namespace

float tickSlowMotion(GameData& g, float dt) {
  if (g.slowMotionTimer > 0) {
    g.slowMotionTimer -= dt;
    g.slowMotion = 0.3f;
    if (g.slowMotionTimer <= 0) g.slowMotion = 1;
  }
  return g.slowMotion;
}

void updatePlayerFromInput(GameData& g, InputState& input, float dt) {
  updateShip(g, g.player, input, dt);
  if (isCoopMode(g) && g.player2) updateShip(g, *g.player2, g.p2Input, dt);
}

bool updateWaves(GameData& g) {
  if (g.waveAnnounceTimer > 0) g.waveAnnounceTimer--;
  g.waveTimer--;
  if (g.enemiesSpawned >= g.enemiesPerWave && g.enemies.empty()) {
    if (g.waveTimer <= 0) {
      if (isStoryComplete(g)) {
        onWaveCleared(g);
        g.modeVictory = true;
        g.state = GameState::GameOver;
        endRunScores(g);
        sfx::playBigExplosion();
        return true;
      }
      onWaveCleared(g);
      g.wave++;
      g.enemiesSpawned = 0;
      g.waveDamageTaken = false;
      g.spawnedSniper = g.spawnedHealer = false;
      if (syncChapterForMode(g)) initChapterHazards(g);
      g.enemiesPerWave = getEnemiesPerWave(g);
      g.waveTimer = g.waveDelay;
      g.waveAnnounceTimer = 90;
    }
  } else if (g.enemiesSpawned < g.enemiesPerWave && g.waveTimer <= 0) {
    spawnEnemy(g);
    g.waveTimer = std::max(15.0f, (50 - g.wave * 3) * getSpawnIntervalMult(g));
  }
  return false;
}

void updateBullets(GameData& g, float dt, float tm) {
  for (Bullet& b : g.bullets) {
    float m = b.isPlayer ? 1 : tm;
    b.x += b.vx * m;
    b.y += b.vy * m;
    if (b.isPlayer && frand() > 0.65f && g.particles.size() < MAX_PARTICLES)
      g.particles.push_back({b.x, b.y + 4, (frand() - 0.5f) * 0.5f, frand(), 0.12f, 0.12f, 2, C("#0ff8"),
                             ParticleType::Trail, {}, 0});
  }
  updateHomingBullets(g, dt);
  updateBulletTravel(g);
  std::erase_if(g.bullets, [](const Bullet& b) {
    return !(b.x > -20 && b.x < CANVAS_W + 20 && b.y > -20 && b.y < CANVAS_H + 20);
  });
}

bool updateCollisions(GameData& g) {
  auto players = activePlayers(g);
  for (Player* p : players) updateGrazeFor(g, *p);

  for (int bi = static_cast<int>(g.bullets.size()) - 1; bi >= 0; bi--) {
    if (!g.bullets[bi].isPlayer) continue;
    for (int ei = static_cast<int>(g.enemies.size()) - 1; ei >= 0; ei--) {
      Bullet& b = g.bullets[bi];
      if (ei >= static_cast<int>(g.enemies.size())) continue;
      Enemy& e = g.enemies[ei];
      if (!overlap(b.x, b.y, b.width, b.height, e.x, e.y, e.width, e.height)) continue;
      if (isFrontalShieldBlock(e, b)) continue;
      e.hp -= b.damage;
      g.shotsHit++;
      g.damageDealt += b.damage;
      e.flashTimer = 0.08f;
      sfx::playHit();
      addParticles(g, b.x, b.y, 4, b.color, 2, ParticleType::Spark, 1, 3);
      if (e.hp <= 0) {
        Enemy dead = e;
        g.enemies.erase(g.enemies.begin() + ei);
        onEnemyKilled(g, dead, dead.x, dead.y);
      }
      Bullet& bb = g.bullets[bi];
      if (bb.pierceRemaining > 0) {
        consumePierce(bb);
        continue;
      }
      g.bullets.erase(g.bullets.begin() + bi);
      break;
    }
  }
  for (Player* p : players)
    if (resolveEnemyBulletsFor(g, *p)) return true;
  for (Player* p : players)
    if (resolveEnemyContactFor(g, *p)) return true;
  for (Player* p : players)
    if (handleHazardCollisions(g, *p)) return true;
  return false;
}

void cullOffscreenEnemies(GameData& g) {
  std::erase_if(g.enemies, [](const Enemy& e) { return e.y >= CANVAS_H + 60; });
}

void updatePowerUps(GameData& g) {
  auto players = activePlayers(g);
  for (int i = static_cast<int>(g.powerUps.size()) - 1; i >= 0; i--) {
    if (i >= static_cast<int>(g.powerUps.size())) continue;
    PowerUp& pw = g.powerUps[i];
    pw.y += pw.vy;
    if (pw.y > CANVAS_H + 30) {
      g.powerUps.erase(g.powerUps.begin() + i);
      continue;
    }
    Player* collector = nullptr;
    for (Player* p : players)
      if (overlap(p->x, p->y, p->width, p->height, pw.x, pw.y, pw.width * 2, pw.height * 2)) {
        collector = p;
        break;
      }
    if (!collector) continue;
    PowerUp taken = pw;
    g.powerUps.erase(g.powerUps.begin() + i);
    if (!powerUpsEnabled(g)) continue;
    applyPowerUp(g, *collector, taken);
  }
}

void updateAmbientEffects(GameData& g, float dt, float tm) {
  const Player& p = g.player;
  if (g.comboTimer > 0) {
    g.comboTimer -= dt;
    if (g.comboTimer <= 0) g.combo = 0;
  }
  float hpRatio = static_cast<float>(p.hp) / p.maxHp;
  if (hpRatio <= 0.34f) g.dangerAlpha = 0.15f + std::sin(g.frameCount * 0.08f) * 0.1f;
  else g.dangerAlpha *= 0.9f;
  if (g.screenShake.timer > 0) g.screenShake.timer--;
  if (g.flashAlpha > 0) g.flashAlpha = std::max(0.0f, g.flashAlpha - 0.04f);
  stepParticles(g, dt, tm, true);
  stepBackdrop(g, tm);
}

void updateBackgroundAmbient(GameData& g, float dt) {
  stepBackdrop(g, 1);
  stepParticles(g, dt, 1, false);
  if (g.screenShake.timer > 0) g.screenShake.timer--;
  if (g.flashAlpha > 0) g.flashAlpha = std::max(0.0f, g.flashAlpha - 0.04f);
  g.dangerAlpha *= 0.92f;
}
