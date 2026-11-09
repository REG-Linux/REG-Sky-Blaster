// Sky Blaster game state and simulation. Ported from the TypeScript
// original (src/game/*), one function per original function where possible.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../color.h"
#include "rng.h"

class Canvas;

// The original field is 400x700. The port keeps one side of it and
// stretches the other to the screen's shape (setFieldSize), so the game
// fills any screen instead of being letterboxed.
constexpr float BASE_W = 400;
constexpr float BASE_H = 700;
inline float CANVAS_W = BASE_W;
inline float CANVAS_H = BASE_H;
// Sets the field for a screen area of w x h; true when it changed.
bool setFieldSize(float w, float h);
constexpr float PLAYER_W = 32;
constexpr float PLAYER_H = 36;
constexpr size_t MAX_PARTICLES = 600;
constexpr float COMBO_WINDOW_S = 1.5f;
constexpr float GRAZE_RADIUS = 22;
constexpr double FIXED_TIMESTEP_S = 1.0 / 60.0;
constexpr double MAX_FRAME_DELTA_S = 0.1;
constexpr float PI = 3.14159265358979f;

enum class AircraftId { Falcon, Phantom, Fortress, Count };
enum class AircraftSkill { MissileSalvo, Dash, EnergyShield };
enum class WeaponId { Standard, ArmorPiercing, Shotgun, Laser, Homing, Count };
enum class ChapterId { Space, Asteroid, Carrier, Wormhole, Count };
enum class BossPattern { Fan, Rain, Broadside, Ring };
enum class GameMode { Story, Endless, BossRush, Daily, Practice, Coop, Count };
enum class Difficulty { Easy, Normal, Hard, Count };
enum class DailyModifier { None, DoubleSpeed, NoPowerups, SingleHp, KamikazeOnly };
enum class AchievementId { FirstBlood, ComboMaster, GrazeKing, BossSlayer, Survivor, Untouchable, Count };
enum class EnemyType { Basic, Fast, Tank, Boss, Mini, Splitter, Sniper, Shielded, Kamikaze, Healer };
enum class MovePattern { Straight, Sine, Zigzag };
enum class EnemyState { None, Patrol, Rush, Aim };
enum class ParticleType { Explosion, Trail, Spark, Star, Ring, Ember, Score };
enum class PowerUpType { Spread, Speed, Shield, Bomb, Heal, Weapon };
enum class HazardType { Asteroid, Turret, Teleporter, None };
enum class GameState { Menu, Playing, Paused, GameOver };
// Title-screen rows the cursor moves between (StartWave only in Practice).
enum class MenuRow { Mode, Aircraft, Weapon, Difficulty, StartWave, Start };

struct Particle {
  float x, y, vx, vy, life, maxLife, size;
  Color color;
  ParticleType type;
  std::string text;      // score popups
  float startSize = 0;   // rings expand from 0 to startSize
};

struct Bullet {
  float x, y, vx, vy, width, height, damage;
  bool isPlayer;
  Color color;
  bool grazed = false;
  float homingStrength = 0;     // 0 = none
  std::optional<WeaponId> weapon;
  int pierceRemaining = -1;     // -1 = not piercing
  float maxTravel = 0;          // 0 = unlimited
  float distanceTraveled = 0;
};

struct Enemy {
  float x = 0, y = 0, width = 0, height = 0, hp = 0, maxHp = 0, speed = 0;
  EnemyType type = EnemyType::Basic;
  float shootTimer = 0, shootInterval = 0;
  MovePattern movePattern = MovePattern::Straight;
  float movePhase = 0;
  int scoreValue = 0;
  float flashTimer = 0;
  EnemyState state = EnemyState::None;
  float aimTimer = 0;
  float healPulse = 0;
  BossPattern bossPattern = BossPattern::Fan;
  int bossVolley = 0;
};

struct PowerUp {
  float x, y, width, height;
  PowerUpType type;
  float vy;
  std::optional<WeaponId> weaponId;
};

struct Player {
  float x, y, width, height, speed;
  float shootTimer, shootInterval;
  int hp, maxHp;
  float invincibleTimer;
  int powerLevel;
  bool shieldActive;
  float shieldTimer;
  float tilt;
  float grazeTimer;
  int grazeCount;
  AircraftId aircraftId;
  float skillCooldown, skillActiveTimer;
  bool skillShieldActive;
  float skillShieldTimer;
  int skillAbsorbedHits;
  int damageBoost;
  float dashVx, dashVy;
  WeaponId weaponId;
  float laserRamp;
};

struct HighScore {
  int64_t score;
  std::string date;
  int wave;
  std::string name;  // up to NAME_LEN of NAME_CHARS; empty for old saves
};

constexpr int NAME_LEN = 8;
constexpr const char* NAME_CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

// Typing a name for a new high score, on the game-over screen.
struct NameEntry {
  bool active = false;
  std::string buf = std::string(NAME_LEN, ' ');
  int cursor = 0;
  bool fresh = true;  // still the remembered name: typing replaces it whole
};

struct ScreenShake {
  float intensity = 0, duration = 0, timer = 0;
};

struct Nebula {
  float x, y, radius;
  Color color;
  float speed, alpha;
};

struct Star {
  float x, y, speed, brightness;
};

struct Hazard {
  HazardType type;
  float x = 0, y = 0, width = 0, height = 0;
  float vx = 0, vy = 0, rot = 0, rotSpeed = 0;
  float shootTimer = 0, shootInterval = 0;
  float side = 0;
  int padId = 0;
  float pulse = 0, cooldown = 0;
};

// Platform input for one ship: keyboard and gamepad merged.
struct InputState {
  bool left = false, right = false, up = false, down = false, shoot = false;
  bool bomb = false, skill = false;  // one-shot, consumed by the sim
};

struct AchievementToast {
  AchievementId id;
  float timer;
};

struct GameData {
  Player player;
  std::optional<Player> player2;  // local co-op second ship
  bool p2Connected = false;       // a second pad is plugged in (menu copy)
  InputState p2Input;
  std::vector<Bullet> bullets;
  std::vector<Enemy> enemies;
  std::vector<Particle> particles;
  std::vector<PowerUp> powerUps;
  int64_t score = 0;
  int wave = 0;
  float waveTimer = 0, waveDelay = 90;
  int enemiesSpawned = 0, enemiesPerWave = 0;
  GameState state = GameState::Menu;
  ScreenShake screenShake;
  int combo = 0;
  float comboTimer = 0;
  int maxCombo = 0;
  float flashAlpha = 0;
  Color flashColor = C("#fff");
  float waveAnnounceTimer = 0;
  float dangerAlpha = 0;
  float slowMotion = 1, slowMotionTimer = 0;
  int frameCount = 0;
  std::vector<Star> stars;
  std::vector<Nebula> nebulae;
  ChapterId chapterId = ChapterId::Space;
  std::vector<Hazard> hazards;
  float hazardSpawnTimer = 0;
  AircraftId selectedAircraft = AircraftId::Falcon;
  WeaponId selectedWeapon = WeaponId::Standard;
  bool spawnedSniper = false, spawnedHealer = false;
  GameMode gameMode = GameMode::Endless;
  Difficulty difficulty = Difficulty::Normal;
  int dailySeed = 0;
  DailyModifier dailyModifier = DailyModifier::None;
  bool modeVictory = false;
  bool waveDamageTaken = false;
  bool dailyBonusAwarded = false;
  int runCoinsEarned = 0;
  std::optional<AchievementToast> achievementToast;
  Rng rng{0};
  int shotsFired = 0, shotsHit = 0;
  float damageDealt = 0;
  int enemiesKilled = 0;
  bool autoFire = true;
  bool practiceInvincible = false;
  int practiceStartWave = 1;
  MenuRow menuRow = MenuRow::Mode;
  NameEntry nameEntry;
  int highScoreRank = -1;  // this run's place in the table, -1 none
};

// ---- static data (data.cpp) ----
struct AircraftDef {
  AircraftId id;
  const char* name;
  const char* tagline;
  float speed;
  int startHp, maxHp;
  AircraftSkill skill;
  const char* skillName;
  const char* skillDescription;
  float skillCooldown;
  bool unlockedByDefault;
  int coinCost;
  Color hullTop, hullMid, hullBottom, cockpitColor, engineColor, bulletColor;
};
struct WeaponDef {
  WeaponId id;
  const char* name;
  const char* shortName;
  const char* tagline;
  Color hudColor, bulletColor;
  bool unlockedByDefault;
  int coinCost;
};
struct ChapterDef {
  ChapterId id;
  const char* name;
  const char* tagline;
  Color bgTop, bgMid, bgBottom;
  Color nebulaColors[5];
  Color starBright, starDim;
  HazardType hazardType;
  Color hudColor;
};
struct ModeInfo {
  const char* name;
  const char* tagline;
};
struct DifficultyInfo {
  const char* name;
  const char* tagline;
  Color color;
};
struct AchievementDef {
  AchievementId id;
  const char* name;
  const char* description;
};
struct BossAccent {
  Color hullTop, hullBottom, eye, glow;
};
struct ShipPalette {
  Color hullTop, hullMid, hullBottom, cockpitColor, engineColor, accent;
};

const AircraftDef& getAircraft(AircraftId id);
AircraftId nextAircraft(AircraftId id, int dir);
const WeaponDef& getWeapon(WeaponId id);
WeaponId nextWeapon(WeaponId id, int dir);
const ChapterDef& getChapter(ChapterId id);
ChapterId getChapterForWave(int wave);
const ModeInfo& modeInfo(GameMode m);
const DifficultyInfo& difficultyInfo(Difficulty d);
const AchievementDef& achievementDef(AchievementId id);
BossPattern bossPatternForChapter(ChapterId id);
BossAccent bossAccentColors(BossPattern p);
float bossShootInterval(BossPattern p);

// ---- random (Math.random stand-in, cosmetic only) ----
float frand();

// ---- progress (progress.cpp) ----
void progressInit(const std::string& saveDir);
int loadCoins();
int addCoins(int amount);
bool isAircraftUnlocked(AircraftId id);
bool isWeaponUnlocked(WeaponId id);
bool canAffordUnlock(int cost);
bool tryUnlockAircraft(AircraftId id);
bool tryUnlockWeapon(WeaponId id);
struct LifetimeStats {
  int enemyKills = 0, bossKills = 0;
};
LifetimeStats recordEnemyKill(bool isBoss);
bool unlockAchievement(AchievementId id);  // true when newly unlocked
int coinRewardForEnemy(EnemyType t);
constexpr int STORY_STAGE_CLEAR_COINS = 25;
constexpr int DAILY_COMPLETE_COINS = 100;
constexpr int DAILY_COMPLETE_WAVE = 10;
const std::vector<HighScore>& loadHighScores();
bool qualifiesForHighScore(int64_t score);  // would make the top 10
// Stores the score and returns its place in the table (0 = best), or -1.
int saveHighScore(int64_t score, int wave, const std::string& name);
const std::string& lastHighScoreName();  // to start the next entry from

// ---- modes (modes.cpp) ----
GameMode nextGameMode(GameMode m, int dir);
Difficulty nextDifficulty(Difficulty d, int dir);
int getDailySeed();
DailyModifier pickDailyModifier(int seed);
const char* dailyModifierLabel(DailyModifier m);
void initModeState(GameData& g);
bool isPracticeMode(const GameData& g);
bool isPracticeInvincible(const GameData& g);
bool isCoopMode(const GameData& g);
int clampPracticeStartWave(int w);
int nextPracticeStartWave(int w, int dir);
std::string practiceStartWaveLabel(int w);
void applyDailyPlayerMods(GameData& g);
bool syncChapterForMode(GameData& g);
bool isBossWave(const GameData& g);
int getEnemiesPerWave(const GameData& g);
bool isStoryComplete(const GameData& g);
std::string getWaveLabel(const GameData& g);
struct WaveAnnounce {
  std::string main, sub;
  bool boss;
};
WaveAnnounce getWaveAnnounce(const GameData& g);
bool powerUpsEnabled(const GameData& g);
float getEnemySpeedMult(const GameData& g);
float getEnemyHpMult(const GameData& g);
float getSpawnIntervalMult(const GameData& g);
int getPlayerHpBonus(const GameData& g);
int getBossHp(const GameData& g);

// ---- co-op helpers ----
std::vector<Player*> activePlayers(GameData& g);
bool shouldTeamWipe(const GameData& g);
int coopSeatForShip(const GameData& g, const Player& p);  // 0 none, 1 P1, 2 P2
ShipPalette resolveShipPalette(AircraftId id, int seat);

// ---- chapters ----
void applyChapterToGame(GameData& g, ChapterId id);

// ---- effects ----
void shake(GameData& g, float intensity, float duration);
void addParticles(GameData& g, float x, float y, int n, Color color, float spd = 3,
                  ParticleType type = ParticleType::Explosion, float sz0 = 2, float sz1 = 5);
void addRing(GameData& g, float x, float y, Color color, float maxR);
void addScorePopup(GameData& g, float x, float y, const std::string& text, Color color = C("#fff"));

// ---- run progress ----
void tickAchievementToast(GameData& g, float dt);
void queueAchievement(GameData& g, AchievementId id);
void awardRunCoins(GameData& g, int amount, std::optional<float> x = {}, std::optional<float> y = {});
void onWaveCleared(GameData& g);

// ---- weapons ----
void fireWeapon(GameData& g, Player& p);
void updateBulletTravel(GameData& g);
void consumePierce(Bullet& b);

// ---- enemies ----
void spawnEnemy(GameData& g);
void spawnSplitterChildren(GameData& g, float x, float y);
bool blocksCenterShot(const Enemy& e, float shotX);
bool isFrontalShieldBlock(const Enemy& e, const Bullet& b);
float kamikazeExplosionRadius();
bool isKamikazeBlastHit(const Enemy& e, float px, float py);
void updateEnemies(GameData& g, float dt, float tm);
constexpr float HEAL_RADIUS = 100;
constexpr float HEAL_INTERVAL = 2;
constexpr float KAMIKAZE_RUSH_SPEED = 5.5f;
constexpr float KAMIKAZE_TRIGGER_DIST = 160;
constexpr float SNIPER_AIM_FRAMES = 50;

// ---- hazards ----
void initChapterHazards(GameData& g);
void updateHazards(GameData& g, float dt, float tm);
bool handleHazardCollisions(GameData& g, Player& p);  // true when the run ended

// ---- combat ----
void playerShoot(GameData& g, Player& p);
void explodeKamikaze(GameData& g, Enemy& e);
bool playerHitFromEnemy(GameData& g, Player& p);
void onEnemyKilled(GameData& g, const Enemy& e, float x, float y);
void updateLaserFire(GameData& g, bool shooting, float dt, Player& p);
void activateBomb(GameData& g, Player& p);
void hurtPlayer(GameData& g, Player& p);
void killPlayer(GameData& g, Player& p);

// ---- skills ----
void updateHomingBullets(GameData& g, float dt);
bool isPlayerVulnerable(const Player& p);
void tickSkills(GameData& g, Player& p, float dt);
bool tryActivateSkill(GameData& g, Player& p, float moveX, float moveY);
float getSkillCooldownRatio(const Player& p);

// ---- systems ----
float tickSlowMotion(GameData& g, float dt);
void updatePlayerFromInput(GameData& g, InputState& input, float dt);
bool updateWaves(GameData& g);
void updateBullets(GameData& g, float dt, float tm);
bool updateCollisions(GameData& g);
void cullOffscreenEnemies(GameData& g);
void updatePowerUps(GameData& g);
void updateAmbientEffects(GameData& g, float dt, float tm);
void updateBackgroundAmbient(GameData& g, float dt);

// ---- engine ----
Player createPlayer(AircraftId a = AircraftId::Falcon, WeaponId w = WeaponId::Standard);
GameData createGameData();
bool canStartGame(const GameData& g);
void togglePause(GameData& g);
bool tryUnlockSelectedAircraft(GameData& g);
bool tryUnlockSelectedWeapon(GameData& g);
void cycleAircraftSelection(GameData& g, int dir);
void cycleWeaponSelection(GameData& g, int dir);
void cycleGameModeSelection(GameData& g, int dir);
void cycleDifficultySelection(GameData& g, int dir);
void cyclePracticeStartWave(GameData& g, int dir);
void resetGame(GameData& g);
bool startRun(GameData& g);  // menu / game over → playing (co-op aware)
void returnToMenu(GameData& g);
void menuMove(GameData& g, int dir);    // cursor up/down, wraps
void menuChange(GameData& g, int dir);  // left/right on the cursor's row
// Run over: a top-10 score opens the name entry, saved on confirm.
void endRunScores(GameData& g);
void nameEntryMove(GameData& g, int dir);   // left/right between letters
void nameEntryCycle(GameData& g, int dir);  // up/down through NAME_CHARS
void nameEntryType(GameData& g, char c);    // keyboard: set and advance
void nameEntryBackspace(GameData& g);
void nameEntryConfirm(GameData& g);
void update(GameData& g, InputState& input, float dt);
void updateBackground(GameData& g, float dt);

// ---- render (render_*.cpp) ----
// Draws the 400x700 play field; the caller sets the transform.
void renderGame(Canvas& ctx, GameData& g, uint32_t nowMs);
void drawChapterBackground(Canvas& ctx, const GameData& g);
void drawHazards(Canvas& ctx, const GameData& g);
void drawMenu(Canvas& ctx, GameData& g, uint32_t nowMs);
void drawHUD(Canvas& ctx, GameData& g);
void drawGameOver(Canvas& ctx, GameData& g, uint32_t nowMs);
void drawAchievementToast(Canvas& ctx, const GameData& g);
void drawLaserBeam(Canvas& ctx, const Player& p);
void drawPlayer(Canvas& ctx, const GameData& g, const Player& p, uint32_t nowMs);
void drawBullet(Canvas& ctx, const Bullet& b);
void drawPowerUp(Canvas& ctx, const PowerUp& pw, uint32_t nowMs);
void drawEnemy(Canvas& ctx, const GameData& g, const Enemy& e, int frame);
void drawParticles(Canvas& ctx, const GameData& g);
void drawFlashOverlay(Canvas& ctx, const GameData& g);
void drawDangerVignette(Canvas& ctx, const GameData& g);
void drawGrazeIndicator(Canvas& ctx, const GameData& g);
void drawComboBanner(Canvas& ctx, const GameData& g, uint32_t nowMs);
void drawWaveAnnounce(Canvas& ctx, const GameData& g);
void drawPausedOverlay(Canvas& ctx, const GameData& g);
void drawSlowMotionOverlay(Canvas& ctx, const GameData& g);
void drawSkillIndicator(Canvas& ctx, const Player& p, float x, float y);
void drawWeaponLabel(Canvas& ctx, WeaponId id, float x, float y);

// ---- formatting helpers ----
std::string formatThousands(int64_t v);
std::string formatAccuracy(int shotsHit, int shotsFired);

// ---- sound effects (audio.cpp) ----
namespace sfx {
void playShoot();
void playEnemyShoot();
void playHit();
void playCombo();
void playMenuSelect();
void playExplosion();
void playBigExplosion();
void playPowerUp();
void playHeal();
void playGraze();
void playGameOver();
}  // namespace sfx
