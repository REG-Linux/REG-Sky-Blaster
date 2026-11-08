// weapons.ts
#include <algorithm>
#include <cmath>

#include "game.h"

namespace {

constexpr float BULLET_SPEED = 12;

Bullet shot(float x, float y, float vx, float vy, float w, float h, float damage, Color color,
            WeaponId weapon) {
  Bullet b{x, y, vx, vy, w, h, damage, true, color};
  b.weapon = weapon;
  return b;
}

Color bulletColor(const Player& p) {
  return p.weaponId == WeaponId::Standard ? getAircraft(p.aircraftId).bulletColor
                                          : getWeapon(p.weaponId).bulletColor;
}

// The standard and armor-piercing spreads share one layout.
void pushSpread(GameData& g, const Player& p, float damage, Color color, int pierce) {
  auto mk = [&](float x, float y, float vx, float vy, float w, float h) {
    Bullet b = shot(x, y, vx, vy, w, h, damage, color, p.weaponId);
    b.pierceRemaining = pierce;
    g.bullets.push_back(b);
    g.shotsFired++;
  };
  const int l = p.powerLevel;
  mk(p.x, p.y - p.height / 2 - 5, 0, -BULLET_SPEED, 4, 14);
  if (l >= 1)
    for (float d : {-10.0f, 10.0f}) mk(p.x + d, p.y - p.height / 2, 0, -BULLET_SPEED, 3, 12);
  if (l >= 2)
    for (float d : {-1.0f, 1.0f}) mk(p.x + d * 8, p.y - p.height / 2, d * 2, -BULLET_SPEED, 3, 10);
  if (l >= 3)
    for (float d : {-1.0f, 1.0f})
      mk(p.x + d * 5, p.y - p.height / 2, d * 3.5f, -BULLET_SPEED * 0.9f, 3, 10);
}

float takeDamage(Player& p) {
  float damage = 1.0f + p.damageBoost;
  p.damageBoost = 0;
  return damage;
}

void fireStandard(GameData& g, Player& p) {
  float damage = takeDamage(p);
  sfx::playShoot();
  pushSpread(g, p, damage, bulletColor(p), -1);
}

void fireArmorPiercing(GameData& g, Player& p) {
  float damage = takeDamage(p);
  sfx::playShoot();
  pushSpread(g, p, damage, getWeapon(WeaponId::ArmorPiercing).bulletColor, 3);
}

void fireShotgun(GameData& g, Player& p) {
  float damage = takeDamage(p);
  Color color = getWeapon(WeaponId::Shotgun).bulletColor;
  sfx::playShoot();
  const int pellets = 5 + p.powerLevel * 2;
  for (int i = 0; i < pellets; i++) {
    float t = pellets == 1 ? 0 : (static_cast<float>(i) / (pellets - 1) - 0.5f) * 1.1f;
    const float speed = 9;
    Bullet b = shot(p.x, p.y - p.height / 2, std::sin(t) * speed * 0.55f, -std::cos(t) * speed, 4, 4,
                    damage, color, WeaponId::Shotgun);
    b.maxTravel = 95;
    g.bullets.push_back(b);
    g.shotsFired++;
  }
}

void fireHoming(GameData& g, Player& p) {
  float damage = takeDamage(p);
  Color color = getWeapon(WeaponId::Homing).bulletColor;
  sfx::playShoot();
  const int count = 1 + std::min(2, p.powerLevel);
  for (int i = 0; i < count; i++) {
    float spread = count == 1 ? 0 : (i - 0.5f) * 0.35f;
    float angle = -PI / 2 + spread;
    const float speed = 7;
    Bullet b = shot(p.x + std::cos(angle) * 4, p.y - p.height / 2, std::cos(angle) * speed,
                    std::sin(angle) * speed, 5, 9, damage, color, WeaponId::Homing);
    b.homingStrength = 0.14f;
    g.bullets.push_back(b);
    g.shotsFired++;
  }
}

}  // namespace

void fireWeapon(GameData& g, Player& p) {
  switch (p.weaponId) {
    case WeaponId::ArmorPiercing: fireArmorPiercing(g, p); break;
    case WeaponId::Shotgun: fireShotgun(g, p); break;
    case WeaponId::Homing: fireHoming(g, p); break;
    case WeaponId::Laser: break;
    default: fireStandard(g, p); break;
  }
}

void updateBulletTravel(GameData& g) {
  for (Bullet& b : g.bullets) {
    if (!b.isPlayer || b.maxTravel == 0) continue;
    b.distanceTraveled += std::hypot(b.vx, b.vy);
    if (b.distanceTraveled >= b.maxTravel) b.distanceTraveled = b.maxTravel + 1;
  }
  std::erase_if(g.bullets, [](const Bullet& b) { return b.maxTravel != 0 && b.distanceTraveled > b.maxTravel; });
}

void consumePierce(Bullet& b) {
  if (b.pierceRemaining < 0) return;
  b.pierceRemaining--;
  b.damage = std::max(1.0f, std::floor(b.damage * 0.75f));
}
