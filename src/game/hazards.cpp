// hazards.ts (simulation half; drawing is in render_scene.cpp)
#include <cmath>

#include "game.h"

namespace {

constexpr int MAX_ASTEROIDS = 5;

void spawnAsteroid(GameData& g) {
  Hazard h{HazardType::Asteroid};
  float size = 18 + g.rng.next() * 16;
  h.x = 30 + g.rng.next() * (CANVAS_W - 60);
  h.y = -size;
  h.width = h.height = size;
  h.vx = (g.rng.next() - 0.5) * 1.2;
  h.vy = 2 + g.rng.next() * 2;
  h.rot = g.rng.next() * PI * 2;
  h.rotSpeed = (g.rng.next() - 0.5) * 0.06;
  g.hazards.push_back(h);
}

void spawnTurrets(GameData& g) {
  const float slots[3][2] = {{36, 140}, {CANVAS_W - 36, 220}, {56, 320}};
  for (const auto& s : slots) {
    Hazard h{HazardType::Turret};
    h.x = s[0];
    h.y = s[1];
    h.width = h.height = 28;
    h.shootTimer = 40 + g.rng.next() * 40;
    h.shootInterval = 70 + g.rng.next() * 30;
    h.side = s[0] < CANVAS_W / 2 ? -1 : 1;
    g.hazards.push_back(h);
  }
}

void spawnTeleporters(GameData& g) {
  Hazard a{HazardType::Teleporter};
  a.x = 100, a.y = CANVAS_H * 0.45f, a.width = a.height = 44, a.padId = 0, a.pulse = 0;
  Hazard b{HazardType::Teleporter};
  b.x = CANVAS_W - 100, b.y = CANVAS_H * 0.62f, b.width = b.height = 44, b.padId = 1, b.pulse = PI;
  g.hazards.push_back(a);
  g.hazards.push_back(b);
}

Player& nearestPlayer(GameData& g, float x, float y) {
  auto players = activePlayers(g);
  Player* best = players[0];
  for (Player* p : players) {
    float d = (p->x - x) * (p->x - x) + (p->y - y) * (p->y - y);
    float bd = (best->x - x) * (best->x - x) + (best->y - y) * (best->y - y);
    if (d < bd) best = p;
  }
  return *best;
}

void fireTurret(GameData& g, const Hazard& h) {
  const Player& p = nearestPlayer(g, h.x, h.y);
  float dx = p.x - h.x, dy = p.y - h.y;
  float d = std::sqrt(dx * dx + dy * dy);
  if (d == 0) d = 1;
  float s = 3.5f + g.wave * 0.08f;
  sfx::playEnemyShoot();
  g.bullets.push_back(Bullet{h.x, h.y, dx / d * s, dy / d * s, 5, 5, 1, false, C("#f84")});
}

void tryTeleport(GameData& g, Player& p, const Hazard& pad) {
  const Hazard* other = nullptr;
  for (const Hazard& h : g.hazards)
    if (h.type == HazardType::Teleporter && h.padId != pad.padId) {
      other = &h;
      break;
    }
  if (!other) return;
  p.x = other->x;
  p.y = other->y;
  p.invincibleTimer = 0.6f;
  sfx::playPowerUp();
  for (int i = 0; i < 12; i++)
    g.particles.push_back({p.x, p.y, (frand() - 0.5f) * 5, (frand() - 0.5f) * 5, 0.35f, 0.35f,
                           2 + frand() * 2, C("#c8f"), ParticleType::Spark, {}, 0});
  for (Hazard& h : g.hazards)
    if (h.type == HazardType::Teleporter) h.cooldown = 1.2f;
}

bool overlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
  return std::fabs(ax - bx) < (aw + bw) / 2 && std::fabs(ay - by) < (ah + bh) / 2;
}

}  // namespace

void initChapterHazards(GameData& g) {
  g.hazards.clear();
  g.hazardSpawnTimer = 0;
  HazardType t = getChapter(g.chapterId).hazardType;
  if (t == HazardType::Turret) spawnTurrets(g);
  else if (t == HazardType::Teleporter) spawnTeleporters(g);
}

void updateHazards(GameData& g, float dt, float tm) {
  if (getChapter(g.chapterId).hazardType == HazardType::Asteroid) {
    g.hazardSpawnTimer -= tm;
    int count = 0;
    for (const Hazard& h : g.hazards) count += h.type == HazardType::Asteroid;
    if (g.hazardSpawnTimer <= 0 && count < MAX_ASTEROIDS) {
      spawnAsteroid(g);
      g.hazardSpawnTimer = 50 + g.rng.next() * 40;
    }
  }
  for (Hazard& h : g.hazards)
    if (h.cooldown > 0) h.cooldown -= dt;

  for (int i = static_cast<int>(g.hazards.size()) - 1; i >= 0; i--) {
    Hazard& h = g.hazards[i];
    if (h.type == HazardType::Asteroid) {
      h.x += h.vx * tm;
      h.y += h.vy * tm;
      h.rot += h.rotSpeed * tm;
      if (h.y > CANVAS_H + 40) g.hazards.erase(g.hazards.begin() + i);
      continue;
    }
    if (h.type == HazardType::Turret) {
      h.shootTimer -= tm;
      if (h.shootTimer <= 0) {
        fireTurret(g, h);
        h.shootTimer = h.shootInterval != 0 ? h.shootInterval : 80;
      }
      continue;
    }
    if (h.type == HazardType::Teleporter) h.pulse += dt * 3;
  }
}

bool handleHazardCollisions(GameData& g, Player& p) {
  if (!isPlayerVulnerable(p)) return false;
  for (Hazard& h : g.hazards) {
    if (h.type == HazardType::Teleporter) {
      if (h.cooldown > 0) continue;
      if (!overlap(p.x, p.y, p.width * 0.5f, p.height * 0.5f, h.x, h.y, h.width, h.height)) continue;
      tryTeleport(g, p, h);
      return false;
    }
    if (h.type == HazardType::Asteroid) {
      if (!overlap(p.x, p.y, p.width * 0.4f, p.height * 0.4f, h.x, h.y, h.width, h.height)) continue;
      hurtPlayer(g, p);
      return g.state == GameState::GameOver;
    }
  }
  return false;
}
