// enemies/spawn.ts, enemies/ai.ts, enemies/helpers.ts, enemies/boss-ai.ts
#include <algorithm>
#include <cmath>

#include "game.h"

namespace {

std::vector<EnemyType> buildSpawnPool(const GameData& g) {
  if (g.gameMode == GameMode::Daily && g.dailyModifier == DailyModifier::KamikazeOnly)
    return {EnemyType::Kamikaze, EnemyType::Kamikaze, EnemyType::Kamikaze, EnemyType::Kamikaze, EnemyType::Fast};
  const int w = g.wave;
  std::vector<EnemyType> pool = {EnemyType::Basic, EnemyType::Basic, EnemyType::Basic};
  if (w >= 2) pool.push_back(EnemyType::Fast);
  if (w >= 3) {
    pool.push_back(EnemyType::Tank);
    pool.push_back(EnemyType::Kamikaze);
  }
  if (w >= 4) pool.push_back(EnemyType::Splitter);
  if (w >= 5) pool.push_back(EnemyType::Shielded);
  if (w >= 6 && !isBossWave(g) && !g.spawnedSniper) pool.push_back(EnemyType::Sniper);
  if (w >= 8 && !isBossWave(g) && !g.spawnedHealer) pool.push_back(EnemyType::Healer);
  return pool;
}

// baseEnemy(): random shoot timer and phase first, then the type's fields.
Enemy baseEnemy(Rng& rng) {
  Enemy e;
  e.shootTimer = 60 + rng.next() * 60;
  e.movePattern = MovePattern::Straight;
  e.movePhase = rng.next() * PI * 2;
  e.flashTimer = 0;
  return e;
}

void setStats(Enemy& e, EnemyType type, float x, float y, float w, float h, float hp, float speed,
              float shootInterval, int score, MovePattern pat) {
  e.type = type;
  e.x = x, e.y = y, e.width = w, e.height = h;
  e.hp = e.maxHp = hp;
  e.speed = speed;
  e.shootInterval = shootInterval;
  e.scoreValue = score;
  e.movePattern = pat;
}

// Same rng draw order as the original: pattern, then the object literal's
// fields in source order, then baseEnemy's own draws.
Enemy createEnemy(EnemyType type, int wave, Rng& rng) {
  static const MovePattern pats[] = {MovePattern::Straight, MovePattern::Sine, MovePattern::Zigzag};
  const MovePattern pat = pats[static_cast<int>(rng.next() * 3)];
  float x, speed;
  Enemy e;
  switch (type) {
    case EnemyType::Fast:
      x = 30 + rng.next() * (CANVAS_W - 60);
      speed = 3 + rng.next();
      e = baseEnemy(rng);
      setStats(e, type, x, -30, 24, 24, 1, speed, 80, 150, pat);
      return e;
    case EnemyType::Tank: {
      float hp = 5 + wave / 2;
      x = 40 + rng.next() * (CANVAS_W - 80);
      speed = 0.8f + rng.next() * 0.5f;
      e = baseEnemy(rng);
      setStats(e, type, x, -40, 40, 40, hp, speed, 50, 300, MovePattern::Straight);
      return e;
    }
    case EnemyType::Splitter: {
      float hp = 3 + wave / 4;
      x = 30 + rng.next() * (CANVAS_W - 60);
      speed = 1 + rng.next() * 0.5f;
      e = baseEnemy(rng);
      setStats(e, type, x, -36, 34, 34, hp, speed, 70, 220, MovePattern::Sine);
      return e;
    }
    case EnemyType::Sniper:
      x = 50 + rng.next() * (CANVAS_W - 100);
      e = baseEnemy(rng);
      setStats(e, type, x, -40, 30, 30, 2, 0.6f, 110, 350, MovePattern::Straight);
      e.state = EnemyState::Patrol;
      return e;
    case EnemyType::Shielded: {
      float hp = 8 + wave / 2;
      x = 40 + rng.next() * (CANVAS_W - 80);
      speed = 0.7f + rng.next() * 0.3f;
      e = baseEnemy(rng);
      setStats(e, type, x, -44, 38, 38, hp, speed, 65, 280, MovePattern::Straight);
      return e;
    }
    case EnemyType::Kamikaze:
      x = 20 + rng.next() * (CANVAS_W - 40);
      speed = 1.4f + rng.next() * 0.6f;
      e = baseEnemy(rng);
      setStats(e, type, x, -28, 26, 26, 2, speed, 9999, 180, MovePattern::Straight);
      e.state = EnemyState::Patrol;
      return e;
    case EnemyType::Healer:
      x = 60 + rng.next() * (CANVAS_W - 120);
      e = baseEnemy(rng);
      setStats(e, type, x, -36, 32, 32, 4, 0.5f, 120, 400, MovePattern::Sine);
      e.healPulse = 0;
      return e;
    case EnemyType::Mini:
      speed = 2.5f + rng.next();
      e = baseEnemy(rng);
      setStats(e, type, 0, 0, 16, 16, 1, speed, 100, 75, MovePattern::Zigzag);
      return e;
    default: {
      float hp = 1 + wave / 3;
      x = 20 + rng.next() * (CANVAS_W - 40);
      speed = 1.2f + rng.next() * 0.8f + wave * 0.05f;
      e = baseEnemy(rng);
      setStats(e, EnemyType::Basic, x, -30, 28, 28, hp, speed, std::max(50, 90 - wave * 3), 100, pat);
      return e;
    }
  }
}

void pushEnemyBullet(GameData& g, float x, float y, float vx, float vy, float size, Color color,
                     float homing = 0) {
  Bullet b{x, y, vx, vy, size, size, 1, false, color};
  b.homingStrength = homing;
  g.bullets.push_back(b);
}

void fireEnemyBullet(GameData& g, const Enemy& e, float speed, float damage = 1, float size = 5,
                     Color color = C("#f64")) {
  const Player& p = g.player;
  float dx = p.x - e.x, dy = p.y - e.y;
  float d = std::sqrt(dx * dx + dy * dy);
  if (d == 0) d = 1;
  Bullet b{e.x, e.y + e.height / 2, dx / d * speed, dy / d * speed, size, size, damage, false, color};
  g.bullets.push_back(b);
}

void fireFan(GameData& g, const Enemy& e, float speed) {
  float dx = g.player.x - e.x, dy = g.player.y - e.y;
  for (int i = -2; i <= 2; i++) {
    float a = std::atan2(dy, dx) + i * 0.25f;
    pushEnemyBullet(g, e.x, e.y + e.height / 2, std::cos(a) * speed, std::sin(a) * speed, 6, C("#f46"));
  }
}

void fireRain(GameData& g, const Enemy& e, float speed, int volley) {
  float y = e.y + e.height / 2;
  if (volley % 3 == 2) {
    float dir = volley % 6 == 2 ? 1 : -1;
    float baseX = dir > 0 ? e.x - e.width / 2 : e.x + e.width / 2;
    for (int i = 0; i < 4; i++)
      pushEnemyBullet(g, baseX + dir * i * 14, y, dir * (speed * 0.85f), speed * 0.35f, 5, C("#fa4"));
    return;
  }
  const int count = 5;
  for (int i = 0; i < count; i++) {
    float t = static_cast<float>(i) / (count - 1);
    pushEnemyBullet(g, e.x - e.width / 2 + t * e.width, y, (t - 0.5f) * 0.4f, speed * 0.95f, 5, C("#fc6"));
  }
}

void aimedShot(GameData& g, const Enemy& e, float y, float speed, float size, Color color, float homing = 0) {
  float dx = g.player.x - e.x, dy = g.player.y - e.y;
  float d = std::sqrt(dx * dx + dy * dy);
  if (d == 0) d = 1;
  pushEnemyBullet(g, e.x, y, dx / d * speed, dy / d * speed, size, color, homing);
}

void fireBroadside(GameData& g, const Enemy& e, float speed, int volley) {
  float y = e.y + e.height / 2;
  float side = volley % 2 == 0 ? -1 : 1;
  float originX = e.x + side * (e.width / 2);
  for (int i = -1; i <= 1; i++)
    pushEnemyBullet(g, originX, y + i * 10, side * speed * 1.05f, speed * 0.25f + i * 0.15f, 5, C("#8cf"));
  if (volley % 2 == 1) aimedShot(g, e, y, speed + 0.8f, 6, C("#4af"));
}

void fireRing(GameData& g, const Enemy& e, float speed, int volley) {
  float y = e.y + e.height / 2;
  float ringSpeed = speed * 0.75f;
  for (int i = 0; i < 10; i++) {
    float a = static_cast<float>(i) / 10 * PI * 2;
    pushEnemyBullet(g, e.x, y, std::cos(a) * ringSpeed, std::sin(a) * ringSpeed, 5, C("#c8f"));
  }
  if (volley % 2 == 1) aimedShot(g, e, y, speed * 0.45f, 7, C("#eaf"), 0.06f);
}

void fireBossPattern(GameData& g, Enemy& e) {
  float speed = 3 + g.wave * 0.1f;
  int volley = e.bossVolley;
  e.bossVolley = volley + 1;
  switch (e.bossPattern) {
    case BossPattern::Rain: fireRain(g, e, speed, volley); break;
    case BossPattern::Broadside: fireBroadside(g, e, speed, volley); break;
    case BossPattern::Ring: fireRing(g, e, speed, volley); break;
    default: fireFan(g, e, speed); break;
  }
}

void enemyShoot(GameData& g, Enemy& e) {
  float s = 3 + g.wave * 0.1f;
  sfx::playEnemyShoot();
  if (e.type == EnemyType::Boss) {
    fireBossPattern(g, e);
    return;
  }
  if (e.type == EnemyType::Sniper) {
    fireEnemyBullet(g, e, s + 2.5f, 1, 6, C("#f28"));
    return;
  }
  if (e.type == EnemyType::Kamikaze || e.type == EnemyType::Healer) return;
  fireEnemyBullet(g, e, s);
}

void tickHealerAuras(GameData& g, float dt) {
  for (Enemy& healer : g.enemies) {
    if (healer.type != EnemyType::Healer) continue;
    healer.healPulse += dt;
    if (healer.healPulse < HEAL_INTERVAL) continue;
    healer.healPulse = 0;
    for (Enemy& e : g.enemies) {
      if (&e == &healer || e.type == EnemyType::Boss) continue;
      float dx = e.x - healer.x, dy = e.y - healer.y;
      if (dx * dx + dy * dy > HEAL_RADIUS * HEAL_RADIUS) continue;
      if (e.hp >= e.maxHp) continue;
      e.hp = std::min(e.maxHp, e.hp + 1);
      e.flashTimer = 0.12f;
    }
  }
}

void updateKamikaze(Enemy& e, const GameData& g, float tm) {
  const Player& p = g.player;
  float dx = p.x - e.x, dy = p.y - e.y;
  float dist = std::sqrt(dx * dx + dy * dy);
  if (e.state != EnemyState::Rush && (e.y > 120 || dist < KAMIKAZE_TRIGGER_DIST)) e.state = EnemyState::Rush;
  if (e.state == EnemyState::Rush) {
    float d = dist != 0 ? dist : 1;
    e.x += dx / d * KAMIKAZE_RUSH_SPEED * tm;
    e.y += dy / d * KAMIKAZE_RUSH_SPEED * tm;
  } else {
    e.y += e.speed * tm;
  }
}

void updateSniperAim(GameData& g, Enemy& e, float tm) {
  if (e.state == EnemyState::Aim) {
    e.aimTimer -= tm;
    if (e.aimTimer <= 0) {
      e.state = EnemyState::Patrol;
      enemyShoot(g, e);
      e.shootTimer = e.shootInterval;
    }
    return;
  }
  e.shootTimer -= tm;
  if (e.shootTimer <= 0 && e.y > 0) {
    e.state = EnemyState::Aim;
    e.aimTimer = SNIPER_AIM_FRAMES;
  }
}

}  // namespace

void spawnEnemy(GameData& g) {
  const int w = g.wave;
  if (isBossWave(g) && g.enemiesSpawned == 0) {
    float hp = getBossHp(g);
    BossPattern pattern = bossPatternForChapter(g.chapterId);
    Enemy e = baseEnemy(g.rng);
    setStats(e, EnemyType::Boss, CANVAS_W / 2, -60, 64, 56, hp, 0.5f, bossShootInterval(pattern), 2000,
             MovePattern::Sine);
    e.shootTimer = 40;
    e.bossPattern = pattern;
    e.bossVolley = 0;
    g.enemies.push_back(e);
    g.enemiesSpawned++;
    return;
  }
  std::vector<EnemyType> pool = buildSpawnPool(g);
  EnemyType type = pool[static_cast<size_t>(g.rng.next() * pool.size())];
  if (type == EnemyType::Sniper) g.spawnedSniper = true;
  if (type == EnemyType::Healer) g.spawnedHealer = true;
  Enemy e = createEnemy(type, w, g.rng);
  float mult = getEnemyHpMult(g);
  if (mult != 1) {
    e.hp = std::max(1.0f, std::round(e.hp * mult));
    e.maxHp = std::max(1.0f, std::round(e.maxHp * mult));
  }
  g.enemies.push_back(e);
  g.enemiesSpawned++;
}

void spawnSplitterChildren(GameData& g, float x, float y) {
  for (float ox : {-18.0f, 18.0f}) {
    Enemy mini = createEnemy(EnemyType::Mini, g.wave, g.rng);
    mini.x = x + ox;
    mini.y = y;
    g.enemies.push_back(mini);
  }
  addParticles(g, x, y, 8, C("#a6f"), 2, ParticleType::Spark, 2, 4);
}

bool blocksCenterShot(const Enemy& e, float shotX) {
  if (e.type != EnemyType::Shielded) return false;
  return std::fabs(shotX - e.x) < e.width * 0.38f;
}

bool isFrontalShieldBlock(const Enemy& e, const Bullet& b) {
  if (e.type != EnemyType::Shielded) return false;
  if (b.vy >= 0) return false;  // the shield faces down, toward the player
  return blocksCenterShot(e, b.x);
}

float kamikazeExplosionRadius() { return 55; }

bool isKamikazeBlastHit(const Enemy& e, float px, float py) {
  float r = kamikazeExplosionRadius();
  float dx = px - e.x, dy = py - e.y;
  return dx * dx + dy * dy <= r * r;
}

void updateEnemies(GameData& g, float dt, float tm) {
  tickHealerAuras(g, dt);
  const float speedMult = getEnemySpeedMult(g);
  // enemyShoot never adds enemies, so plain iteration is safe
  for (Enemy& e : g.enemies) {
    e.movePhase += 0.03f * tm;
    if (e.flashTimer > 0) e.flashTimer -= dt;

    if (e.type == EnemyType::Kamikaze) {
      updateKamikaze(e, g, tm * speedMult);
    } else {
      e.y += e.speed * tm * speedMult;
      if (e.movePattern == MovePattern::Sine) e.x += std::sin(e.movePhase) * 2 * tm * speedMult;
      else if (e.movePattern == MovePattern::Zigzag)
        e.x += (std::sin(e.movePhase * 2) > 0 ? 1.5f : -1.5f) * tm * speedMult;
    }

    if (e.type == EnemyType::Boss && e.y > 100) {
      e.y = 100;
      e.speed = 0;
      e.movePattern = MovePattern::Sine;
    } else if (e.type == EnemyType::Sniper && e.y > 55) {
      e.y = 55;
      e.speed = 0;
    } else if (e.type == EnemyType::Healer && e.y > 75) {
      e.y = 75;
      e.speed = 0;
      e.movePattern = MovePattern::Sine;
    }

    e.x = std::max(e.width / 2, std::min(CANVAS_W - e.width / 2, e.x));

    if (e.type == EnemyType::Sniper) {
      updateSniperAim(g, e, tm);
      continue;
    }
    if (e.type == EnemyType::Kamikaze || e.type == EnemyType::Healer) continue;

    e.shootTimer -= tm;
    if (e.shootTimer <= 0 && e.y > 0) {
      enemyShoot(g, e);
      e.shootTimer = e.shootInterval;
    }
  }
}
