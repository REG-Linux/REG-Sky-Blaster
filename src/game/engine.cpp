// engine.ts and player-factory.ts. Co-op is local (a second controller)
// or netplay (net/net.cpp: the guest's ship is P2 on the host).
#include <algorithm>
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
  p.bombs = BOMBS_START;
  p.aircraftId = craft.id;
  p.weaponId = w;
  return p;
}

void fieldFor(float w, float h, float& fw, float& fh) {
  fw = BASE_W, fh = BASE_H;
  if (w <= 0 || h <= 0) return;
  const float s = std::fmin(w / BASE_W, h / BASE_H);
  fw = std::round(w / s), fh = std::round(h / s);
}

bool setFieldSize(float w, float h) {
  if (w <= 0 || h <= 0) return false;
  float fw, fh;
  fieldFor(w, h, fw, fh);
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

bool isNetGuest(const GameData& g) { return g.netRole == NetRole::Guest; }
bool isNetGame(const GameData& g) { return g.netRole != NetRole::None; }

void requestPause(GameData& g, InputState& input) {
  if (isNetGuest(g)) input.pause = true;  // the host pauses both
  else togglePause(g);
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

bool unlockAutoFire(GameData& g) {
  if (isAutoFireUnlocked() || !tryUnlockAutoFire()) return false;
  g.autoFire = true;
  rememberAutoFire(true);
  return true;
}

bool toggleAutoFire(GameData& g) {
  if (!isAutoFireUnlocked()) return false;
  g.autoFire = !g.autoFire;
  rememberAutoFire(g.autoFire);
  return true;
}

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
  g.nameEntry = {};
  g.highScoreRank = -1;
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
  if (isNetGuest(g)) return false;  // the host starts the runs
  if (isCoopMode(g) && !g.p2Connected) return false;
  rememberMenu(g);
  resetGame(g);
  g.runSerial++;
  if (isCoopMode(g)) {
    // local: P2 flies the same loadout, seat colours tell the ships apart;
    // netplay: the guest's own
    g.player2 = isNetGame(g) ? createPlayer(g.netP2Aircraft, g.netP2Weapon)
                             : createPlayer(g.selectedAircraft, g.selectedWeapon);
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
  g.nameEntry = {};
  g.highScoreRank = -1;
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

static std::vector<MenuRow> menuRows(const GameData& g) {
  std::vector<MenuRow> rows = {MenuRow::Start,    MenuRow::Mode,      MenuRow::Aircraft,
                              MenuRow::Weapon,   MenuRow::AutoFire,  MenuRow::Difficulty};
  if (isPracticeMode(g)) rows.push_back(MenuRow::StartWave);
  return rows;
}

void menuMove(GameData& g, int dir) {
  const auto rows = menuRows(g);
  const int n = static_cast<int>(rows.size());
  int i = 0;
  while (i < n && rows[i] != g.menuRow) i++;
  g.menuRow = rows[((i < n ? i : 0) + dir + n) % n];
}

void menuChange(GameData& g, int dir) {
  switch (g.menuRow) {
    case MenuRow::Mode:
      if (!isNetGame(g)) cycleGameModeSelection(g, dir);  // netplay is co-op
      break;
    case MenuRow::Aircraft: cycleAircraftSelection(g, dir); break;
    case MenuRow::Weapon: cycleWeaponSelection(g, dir); break;
    case MenuRow::Difficulty:
      if (!isNetGuest(g)) cycleDifficultySelection(g, dir);  // the host's applies
      break;
    case MenuRow::StartWave: cyclePracticeStartWave(g, dir); break;
    case MenuRow::AutoFire: toggleAutoFire(g); break;
    case MenuRow::Start: break;
  }
}

void endRunScores(GameData& g) {
  g.highScoreRank = -1;
  g.nameEntry = {};
  // practice and co-op scores stay out of the (single-player) table
  if (isPracticeMode(g) || isCoopMode(g) || !qualifiesForHighScore(g.score)) return;
  // start from the last name, cursor on its last letter, so a returning
  // player only confirms
  NameEntry& n = g.nameEntry;
  n.active = true;
  const std::string& last = lastHighScoreName();
  for (size_t i = 0; i < last.size() && i < n.buf.size(); i++) n.buf[i] = last[i];
  n.cursor = last.empty() ? 0 : static_cast<int>(std::min(last.size(), n.buf.size()) - 1);
  if (last.empty()) n.buf[0] = 'A';
}

static bool blankName(const NameEntry& n) { return n.buf.find_first_not_of(' ') == std::string::npos; }

void nameEntryMove(GameData& g, int dir) {
  NameEntry& n = g.nameEntry;
  if (n.confirming) return;
  n.fresh = false;
  n.emptyHint = false;
  n.cursor = std::clamp(n.cursor + dir, 0, NAME_LEN - 1);
}

void nameEntryCycle(GameData& g, int dir) {
  NameEntry& n = g.nameEntry;
  if (n.confirming) return;
  n.fresh = false;
  n.emptyHint = false;
  const std::string chars = NAME_CHARS;
  const int count = static_cast<int>(chars.size());
  int i = static_cast<int>(chars.find(n.buf[n.cursor]));
  if (i < 0) i = 0;
  n.buf[n.cursor] = chars[(i + dir + count) % count];
}

void nameEntryType(GameData& g, char c) {
  NameEntry& n = g.nameEntry;
  if (n.confirming) return;
  if (n.fresh) {
    n.buf.assign(NAME_LEN, ' ');
    n.cursor = 0;
  }
  n.buf[n.cursor] = c;
  nameEntryMove(g, 1);
}

void nameEntryBackspace(GameData& g) {
  NameEntry& n = g.nameEntry;
  if (n.confirming) return;
  n.fresh = false;
  n.emptyHint = false;
  // on a blank slot, step back first: deletes the letter before, like a text field
  if (n.buf[n.cursor] == ' ') nameEntryMove(g, -1);
  n.buf[n.cursor] = ' ';
}

bool nameEntryRequestConfirm(GameData& g) {
  NameEntry& n = g.nameEntry;
  if (!n.active || n.confirming) return false;
  n.emptyHint = blankName(n);
  n.confirming = !n.emptyHint;
  return n.confirming;
}

void nameEntryCancelConfirm(GameData& g) { g.nameEntry.confirming = false; }

void nameEntryConfirm(GameData& g) {
  NameEntry& n = g.nameEntry;
  if (!n.active) return;
  if (blankName(n)) {
    n.confirming = false;
    n.emptyHint = true;
    return;
  }
  n.active = n.confirming = false;
  g.highScoreRank = saveHighScore(g.score, g.wave, n.buf);
}

void nameEntrySaveOnExit(GameData& g) {
  NameEntry& n = g.nameEntry;
  if (!n.active) return;
  if (blankName(n)) {
    const std::string& last = lastHighScoreName();
    n.buf = last.empty() ? "PLAYER" : last;
  }
  n.confirming = true;
  nameEntryConfirm(g);
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
