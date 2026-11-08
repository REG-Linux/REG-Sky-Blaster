// engine.ts and player-factory.ts (the coop network bits dropped; co-op
// is local, the second ship driven by a second controller).
#include <chrono>
#include <cmath>
#include <random>

#include "game.h"

Player createPlayer(AircraftId a, WeaponId w) {
  const AircraftDef& craft = getAircraft(a);
  Player p{};
  p.x = CANVAS_W / 2;
  p.y = CANVAS_H - 80;
  p.width = PLAYER_W;
  p.height = PLAYER_H;
  p.speed = craft.speed;
  p.shootTimer = 0;
  p.shootInterval = 8;
  p.hp = craft.startHp;
  p.maxHp = craft.maxHp;
  p.aircraftId = craft.id;
  p.weaponId = w;
  return p;
}

bool setFieldSize(float w, float h) {
  if (w <= 0 || h <= 0) return false;
  const float s = std::fmin(w / BASE_W, h / BASE_H);
  const float fw = std::round(w / s), fh = std::round(h / s);
  if (fw == CANVAS_W && fh == CANVAS_H) return false;
  CANVAS_W = fw;
  CANVAS_H = fh;
  return true;
}

static void ensureValidMenuSelection(GameData& g) {
  if (!isAircraftUnlocked(g.selectedAircraft)) g.selectedAircraft = AircraftId::Falcon;
  if (!isWeaponUnlocked(g.selectedWeapon)) g.selectedWeapon = WeaponId::Standard;
  g.player = createPlayer(g.selectedAircraft, g.selectedWeapon);
}

GameData createGameData() {
  GameData g{createPlayer()};
  ensureValidMenuSelection(g);
  applyChapterToGame(g, ChapterId::Space);
  return g;
}

bool canStartGame(const GameData& g) {
  return isAircraftUnlocked(g.selectedAircraft) && isWeaponUnlocked(g.selectedWeapon);
}

void togglePause(GameData& g) {
  if (g.state == GameState::Playing) g.state = GameState::Paused;
  else if (g.state == GameState::Paused) g.state = GameState::Playing;
}

bool tryUnlockSelectedAircraft(GameData& g) {
  if (tryUnlockAircraft(g.selectedAircraft)) {
    g.player = createPlayer(g.selectedAircraft, g.selectedWeapon);
    return true;
  }
  return false;
}

bool tryUnlockSelectedWeapon(GameData& g) { return tryUnlockWeapon(g.selectedWeapon); }

void cycleAircraftSelection(GameData& g, int dir) {
  g.selectedAircraft = nextAircraft(g.selectedAircraft, dir);
  g.player = createPlayer(g.selectedAircraft, g.selectedWeapon);
}

void cycleWeaponSelection(GameData& g, int dir) {
  g.selectedWeapon = nextWeapon(g.selectedWeapon, dir);
  g.player.weaponId = g.selectedWeapon;
}

void cycleGameModeSelection(GameData& g, int dir) { g.gameMode = nextGameMode(g.gameMode, dir); }
void cycleDifficultySelection(GameData& g, int dir) { g.difficulty = nextDifficulty(g.difficulty, dir); }

void cyclePracticeStartWave(GameData& g, int dir) {
  if (g.gameMode != GameMode::Practice) return;
  g.practiceStartWave = nextPracticeStartWave(g.practiceStartWave, dir);
}

void resetGame(GameData& g) {
  if (!canStartGame(g)) return;
  initModeState(g);
  g.player = createPlayer(g.selectedAircraft, g.selectedWeapon);
  g.player2.reset();
  g.p2Input = {};
  g.bullets.clear();
  g.enemies.clear();
  g.particles.clear();
  g.powerUps.clear();
  g.score = 0;
  g.wave = g.gameMode == GameMode::Practice ? clampPracticeStartWave(g.practiceStartWave) : 1;
  g.waveTimer = 25;
  g.waveDelay = 90;
  g.enemiesSpawned = 0;
  g.enemiesPerWave = 5;
  g.combo = 0;
  g.comboTimer = 0;
  g.maxCombo = 0;
  g.flashAlpha = 0;
  g.flashColor = C("#fff");
  g.waveAnnounceTimer = 90;
  g.dangerAlpha = 0;
  g.slowMotion = 1;
  g.slowMotionTimer = 0;
  g.frameCount = 0;
  g.screenShake = {};
  g.spawnedSniper = g.spawnedHealer = false;
  g.hazardSpawnTimer = 0;
  g.waveDamageTaken = false;
  g.dailyBonusAwarded = false;
  g.runCoinsEarned = 0;
  g.achievementToast.reset();
  g.shotsFired = g.shotsHit = 0;
  g.damageDealt = 0;
  g.enemiesKilled = 0;
  g.state = GameState::Playing;
  if (g.gameMode == GameMode::Daily) {
    g.rng = Rng(static_cast<uint32_t>(g.dailySeed));
  } else {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    uint32_t ms = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
    g.rng = Rng(ms ^ std::random_device{}());
  }
  applyDailyPlayerMods(g);
  int bonus = getPlayerHpBonus(g);
  if (bonus > 0) {
    g.player.hp += bonus;
    g.player.maxHp += bonus;
  }
  syncChapterForMode(g);
  g.enemiesPerWave = getEnemiesPerWave(g);
  initChapterHazards(g);
}

bool startRun(GameData& g) {
  if (!canStartGame(g)) return false;
  if (isCoopMode(g) && !g.p2Connected) return false;
  resetGame(g);
  if (isCoopMode(g)) {
    // P2 flies the same loadout; seat colours tell the ships apart
    g.player2 = createPlayer(g.selectedAircraft, g.selectedWeapon);
    int bonus = getPlayerHpBonus(g);
    g.player2->hp += bonus;
    g.player2->maxHp += bonus;
    constexpr float GAP = 48;
    g.player.x = CANVAS_W / 2 - GAP;
    g.player2->x = CANVAS_W / 2 + GAP;
  }
  return true;
}

// Not in the original, which has no way back from a run but a page reload.
void returnToMenu(GameData& g) {
  g.state = GameState::Menu;
  g.bullets.clear();
  g.enemies.clear();
  g.powerUps.clear();
  g.hazards.clear();
  g.player2.reset();
  g.screenShake = {};
  g.flashAlpha = 0;
  g.dangerAlpha = 0;
  g.slowMotion = 1;
  g.slowMotionTimer = 0;
  ensureValidMenuSelection(g);
}

void update(GameData& g, InputState& input, float dt) {
  if (g.state != GameState::Playing) return;
  g.frameCount++;
  tickAchievementToast(g, dt);
  float tm = tickSlowMotion(g, dt);
  updatePlayerFromInput(g, input, dt);
  if (updateWaves(g)) return;
  updateEnemies(g, dt, tm);
  updateHazards(g, dt, tm);
  updateBullets(g, dt, tm);
  if (updateCollisions(g)) return;
  updatePowerUps(g);
  cullOffscreenEnemies(g);
  updateAmbientEffects(g, dt, tm);
}

void updateBackground(GameData& g, float dt) {
  tickAchievementToast(g, dt);
  updateBackgroundAmbient(g, dt);
}
