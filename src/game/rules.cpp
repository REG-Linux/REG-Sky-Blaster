// modes.ts, chapters.ts, effects.ts, run-progress.ts, run-stats.ts and the
// non-network half of coop.ts.
#include <cmath>
#include <ctime>

#include "game.h"

// ---- modes ----

int getDailySeed() {
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_r(&now, &tm);
  return (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
}

DailyModifier pickDailyModifier(int seed) {
  static const DailyModifier mods[] = {DailyModifier::DoubleSpeed, DailyModifier::NoPowerups,
                                       DailyModifier::SingleHp, DailyModifier::KamikazeOnly};
  return mods[seed % 4];
}

const char* dailyModifierLabel(DailyModifier m) {
  switch (m) {
    case DailyModifier::DoubleSpeed: return "2\xC3\x97 enemy speed";
    case DailyModifier::NoPowerups: return "No power-ups";
    case DailyModifier::SingleHp: return "Single HP";
    case DailyModifier::KamikazeOnly: return "Kamikaze swarm";
    default: return "";
  }
}

void initModeState(GameData& g) {
  g.modeVictory = false;
  if (g.gameMode == GameMode::Daily) {
    g.dailySeed = getDailySeed();
    g.dailyModifier = pickDailyModifier(g.dailySeed);
  } else {
    g.dailySeed = 0;
    g.dailyModifier = DailyModifier::None;
  }
  g.practiceInvincible = g.gameMode == GameMode::Practice;
}

bool isPracticeMode(const GameData& g) { return g.gameMode == GameMode::Practice; }
bool isPracticeInvincible(const GameData& g) { return isPracticeMode(g) && g.practiceInvincible; }
bool isCoopMode(const GameData& g) { return g.gameMode == GameMode::Coop; }

constexpr int PRACTICE_START_WAVE_MIN = 1;
constexpr int PRACTICE_START_WAVE_MAX = 20;

int clampPracticeStartWave(int w) {
  return w < PRACTICE_START_WAVE_MIN ? PRACTICE_START_WAVE_MIN
         : w > PRACTICE_START_WAVE_MAX ? PRACTICE_START_WAVE_MAX
                                       : w;
}

int nextPracticeStartWave(int w, int dir) {
  int cur = clampPracticeStartWave(w);
  int span = PRACTICE_START_WAVE_MAX - PRACTICE_START_WAVE_MIN + 1;
  return PRACTICE_START_WAVE_MIN + ((cur - PRACTICE_START_WAVE_MIN + dir + span) % span);
}

std::string practiceStartWaveLabel(int w) {
  w = clampPracticeStartWave(w);
  std::string name = getChapter(getChapterForWave(w)).name;
  return w % 5 == 0 && w >= 5 ? name + " \xC2\xB7 boss" : name;
}

void applyDailyPlayerMods(GameData& g) {
  if (g.gameMode == GameMode::Daily && g.dailyModifier == DailyModifier::SingleHp) {
    g.player.maxHp = 1;
    g.player.hp = 1;
  }
}

static ChapterId getChapterForMode(const GameData& g) {
  if (g.gameMode == GameMode::BossRush)
    return static_cast<ChapterId>((g.wave - 1) % static_cast<int>(ChapterId::Count));
  return getChapterForWave(g.wave);
}

bool syncChapterForMode(GameData& g) {
  ChapterId next = getChapterForMode(g);
  if (next == g.chapterId) return false;
  applyChapterToGame(g, next);
  return true;
}

bool isBossWave(const GameData& g) {
  switch (g.gameMode) {
    case GameMode::BossRush: return true;
    case GameMode::Story: return g.wave % 5 == 0;
    default: return g.wave % 5 == 0 && g.wave >= 5;
  }
}

int getEnemiesPerWave(const GameData& g) {
  if (isBossWave(g)) return 1;
  switch (g.gameMode) {
    case GameMode::Story: return 4 + g.wave;
    case GameMode::BossRush: return 1;
    default: return 5 + g.wave * 2;
  }
}

bool isStoryComplete(const GameData& g) { return g.gameMode == GameMode::Story && g.wave >= 20; }

std::string getWaveLabel(const GameData& g) {
  if (g.gameMode == GameMode::Story) return "STAGE " + std::to_string(g.wave);
  if (g.gameMode == GameMode::BossRush) return "BOSS " + std::to_string(g.wave);
  return "WAVE " + std::to_string(g.wave);
}

static std::string upper(std::string s) {
  for (char& c : s)
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  return s;
}

WaveAnnounce getWaveAnnounce(const GameData& g) {
  static const char* WARN = "\xE2\x9A\xA0";
  const bool boss = isBossWave(g);
  const std::string w = std::to_string(g.wave);
  if (g.gameMode == GameMode::Story) {
    const char* bossName = g.wave == 5    ? "Space Commander"
                           : g.wave == 10 ? "Mining Rig"
                           : g.wave == 15 ? "Carrier Core"
                           : g.wave == 20 ? "Void Entity"
                                          : nullptr;
    if (boss && bossName)
      return {std::string(WARN) + " " + upper(bossName) + " " + WARN, "STAGE " + w, true};
    return {"STAGE " + w, upper(getChapter(getChapterForMode(g)).name), false};
  }
  if (g.gameMode == GameMode::BossRush) return {std::string(WARN) + " BOSS " + w + " " + WARN, "", true};
  if (boss) return {std::string(WARN) + " BOSS WAVE " + w + " " + WARN, "", true};
  return {"WAVE " + w, g.wave > 1 ? upper(getChapter(getChapterForMode(g)).name) : "", false};
}

bool powerUpsEnabled(const GameData& g) {
  return !(g.gameMode == GameMode::Daily && g.dailyModifier == DailyModifier::NoPowerups);
}

float getEnemySpeedMult(const GameData& g) {
  float mult = 1;
  if (g.difficulty == Difficulty::Easy) mult = 0.75f;
  else if (g.difficulty == Difficulty::Hard) mult = 1.3f;
  if (g.gameMode == GameMode::Daily && g.dailyModifier == DailyModifier::DoubleSpeed) mult *= 2;
  return mult;
}

float getEnemyHpMult(const GameData& g) {
  if (g.difficulty == Difficulty::Easy) return 0.75f;
  if (g.difficulty == Difficulty::Hard) return 1.25f;
  return 1;
}

float getSpawnIntervalMult(const GameData& g) {
  if (g.difficulty == Difficulty::Easy) return 1.3f;
  if (g.difficulty == Difficulty::Hard) return 0.8f;
  return 1;
}

int getPlayerHpBonus(const GameData& g) { return g.difficulty == Difficulty::Easy ? 1 : 0; }

int getBossHp(const GameData& g) {
  float base = g.gameMode == GameMode::BossRush ? 18 + g.wave * 10 : 20 + g.wave * 5;
  return static_cast<int>(std::round(base * getEnemyHpMult(g)));
}

// ---- co-op ----

std::vector<Player*> activePlayers(GameData& g) {
  if (g.player2) return {&g.player, &*g.player2};
  return {&g.player};
}

bool shouldTeamWipe(const GameData& g) {
  if (!isCoopMode(g) || !g.player2) return g.player.hp <= 0;
  return g.player.hp <= 0 || g.player2->hp <= 0;
}

int coopSeatForShip(const GameData& g, const Player& p) {
  if (!isCoopMode(g) || !g.player2) return 0;
  return &p == &g.player ? 1 : 2;
}

// ---- chapters ----

void applyChapterToGame(GameData& g, ChapterId id) {
  const ChapterDef& ch = getChapter(id);
  g.chapterId = id;
  const float area = CANVAS_W * CANVAS_H / (BASE_W * BASE_H);
  g.stars.clear();
  for (int i = 0; i < static_cast<int>(100 * area); i++)
    g.stars.push_back({frand() * CANVAS_W, frand() * CANVAS_H, 0.3f + frand() * 2.2f, 0.2f + frand() * 0.8f});
  g.nebulae.clear();
  for (int i = 0; i < static_cast<int>(std::round(6 * area)); i++) {
    Nebula n;
    n.x = frand() * CANVAS_W;
    n.y = frand() * CANVAS_H;
    n.radius = 60 + frand() * 100;
    n.color = ch.nebulaColors[static_cast<int>(frand() * 5)];
    n.speed = 0.15f + frand() * 0.3f;
    n.alpha = 0.15f + frand() * 0.2f;
    g.nebulae.push_back(n);
  }
}

// ---- effects ----

void shake(GameData& g, float intensity, float duration) {
  const ScreenShake& s = g.screenShake;
  float cur = s.intensity * (s.timer / (s.duration != 0 ? s.duration : 1));
  if (intensity > cur) g.screenShake = {intensity, duration, duration};
}

void addParticles(GameData& g, float x, float y, int n, Color color, float spd, ParticleType type,
                  float sz0, float sz1) {
  for (int i = 0; i < n && g.particles.size() < MAX_PARTICLES; i++) {
    float a = frand() * PI * 2;
    float s = (0.5f + frand()) * spd;
    float l = 0.35f + frand() * 0.55f;
    Particle p{x + (frand() - 0.5f) * 10, y + (frand() - 0.5f) * 10, std::cos(a) * s, std::sin(a) * s, l, l,
               sz0 + frand() * (sz1 - sz0), color, type, {}, 0};
    g.particles.push_back(std::move(p));
  }
}

void addRing(GameData& g, float x, float y, Color color, float maxR) {
  if (g.particles.size() >= MAX_PARTICLES) return;
  g.particles.push_back({x, y, 0, 0, 0.4f, 0.4f, 2, color, ParticleType::Ring, {}, maxR});
}

void addScorePopup(GameData& g, float x, float y, const std::string& text, Color color) {
  if (g.particles.size() >= MAX_PARTICLES) return;
  g.particles.push_back({x, y, (frand() - 0.5f) * 0.5f, -1.5f, 0.8f, 0.8f, 12, color, ParticleType::Score, text, 0});
}

// ---- run progress ----

void tickAchievementToast(GameData& g, float dt) {
  if (!g.achievementToast) return;
  g.achievementToast->timer -= dt;
  if (g.achievementToast->timer <= 0) g.achievementToast.reset();
}

void queueAchievement(GameData& g, AchievementId id) {
  if (isPracticeMode(g)) return;
  if (unlockAchievement(id)) {
    g.achievementToast = AchievementToast{id, 3.5f};
    sfx::playPowerUp();
  }
}

void awardRunCoins(GameData& g, int amount, std::optional<float> x, std::optional<float> y) {
  if (isPracticeMode(g) || amount <= 0) return;
  addCoins(amount);
  g.runCoinsEarned += amount;
  if (x && y) addScorePopup(g, *x, *y, "+" + std::to_string(amount) + "\xC2\xA2", C("#fd4"));
}

void onWaveCleared(GameData& g) {
  if (!g.waveDamageTaken) queueAchievement(g, AchievementId::Untouchable);
  if (g.gameMode == GameMode::Story) awardRunCoins(g, STORY_STAGE_CLEAR_COINS);
  if (g.gameMode == GameMode::Daily && g.wave >= DAILY_COMPLETE_WAVE && !g.dailyBonusAwarded) {
    awardRunCoins(g, DAILY_COMPLETE_COINS);
    g.dailyBonusAwarded = true;
  }
  if (g.wave >= 10) queueAchievement(g, AchievementId::Survivor);
}

// ---- formatting ----

std::string formatThousands(int64_t v) {
  std::string digits = std::to_string(v < 0 ? -v : v);
  std::string out;
  int n = static_cast<int>(digits.size());
  for (int i = 0; i < n; i++) {
    out += digits[i];
    if ((n - i - 1) % 3 == 0 && i != n - 1) out += ',';
  }
  return v < 0 ? "-" + out : out;
}

std::string formatAccuracy(int shotsHit, int shotsFired) {
  if (shotsFired <= 0) return "\xE2\x80\x94";
  return std::to_string(static_cast<int>(std::round(100.0 * shotsHit / shotsFired))) + "%";
}
