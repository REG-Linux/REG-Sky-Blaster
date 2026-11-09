// progress.ts + storage/highscores.ts. The browser's localStorage becomes
// one key=value file in the save directory, rewritten on every change.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "game.h"

namespace {

const char* AIRCRAFT_KEYS[] = {"falcon", "phantom", "fortress"};
const char* WEAPON_KEYS[] = {"standard", "armor_piercing", "shotgun", "laser", "homing"};
const char* ACHIEVEMENT_KEYS[] = {"first_blood", "combo_master", "graze_king",
                                  "boss_slayer", "survivor", "untouchable"};

struct Save {
  int coins = 0;
  bool aircraft[3] = {true, false, false};
  bool weapons[5] = {true, false, false, false, false};
  bool achievements[6] = {};
  LifetimeStats stats;
  std::vector<HighScore> highScores;
  std::string lastName;
};

std::string g_path;
Save g_save;

template <size_t N>
void parseList(const std::string& v, const char* (&keys)[N], bool (&out)[N]) {
  std::stringstream ss(v);
  std::string item;
  while (std::getline(ss, item, ','))
    for (size_t i = 0; i < N; i++)
      if (item == keys[i]) out[i] = true;
}

template <size_t N>
std::string joinList(const char* (&keys)[N], const bool (&on)[N]) {
  std::string s;
  for (size_t i = 0; i < N; i++)
    if (on[i]) s += (s.empty() ? "" : ",") + std::string(keys[i]);
  return s;
}

// Only NAME_CHARS, at most NAME_LEN, no leading/trailing spaces.
std::string cleanName(const std::string& in) {
  std::string out;
  for (char c : in) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (std::strchr(NAME_CHARS, c) && c != '\0' && out.size() < static_cast<size_t>(NAME_LEN)) out += c;
  }
  auto b = out.find_first_not_of(' ');
  if (b == std::string::npos) return "";
  return out.substr(b, out.find_last_not_of(' ') - b + 1);
}

void load() {
  std::ifstream in(g_path);
  std::string line;
  while (std::getline(in, line)) {
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    if (k == "coins") g_save.coins = std::max(0, std::atoi(v.c_str()));
    else if (k == "aircraft") parseList(v, AIRCRAFT_KEYS, g_save.aircraft);
    else if (k == "weapons") parseList(v, WEAPON_KEYS, g_save.weapons);
    else if (k == "achievements") parseList(v, ACHIEVEMENT_KEYS, g_save.achievements);
    else if (k == "enemy_kills") g_save.stats.enemyKills = std::atoi(v.c_str());
    else if (k == "boss_kills") g_save.stats.bossKills = std::atoi(v.c_str());
    else if (k == "highscore") {
      // score,wave,date[,name]
      HighScore hs{0, "", 0, ""};
      auto c1 = v.find(','), c2 = v.find(',', c1 == std::string::npos ? c1 : c1 + 1);
      if (c1 == std::string::npos || c2 == std::string::npos) continue;
      auto c3 = v.find(',', c2 + 1);
      hs.score = std::atoll(v.substr(0, c1).c_str());
      hs.wave = std::atoi(v.substr(c1 + 1, c2 - c1 - 1).c_str());
      hs.date = v.substr(c2 + 1, c3 == std::string::npos ? std::string::npos : c3 - c2 - 1);
      if (c3 != std::string::npos) hs.name = cleanName(v.substr(c3 + 1));
      g_save.highScores.push_back(hs);
    } else if (k == "last_name") {
      g_save.lastName = cleanName(v);
    }
  }
  g_save.aircraft[0] = g_save.weapons[0] = true;
  std::stable_sort(g_save.highScores.begin(), g_save.highScores.end(),
                   [](const HighScore& a, const HighScore& b) { return a.score > b.score; });
  if (g_save.highScores.size() > 10) g_save.highScores.resize(10);
}

void store() {
  if (g_path.empty()) return;
  std::string tmp = g_path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) return;
    out << "coins=" << g_save.coins << "\n";
    out << "aircraft=" << joinList(AIRCRAFT_KEYS, g_save.aircraft) << "\n";
    out << "weapons=" << joinList(WEAPON_KEYS, g_save.weapons) << "\n";
    out << "achievements=" << joinList(ACHIEVEMENT_KEYS, g_save.achievements) << "\n";
    out << "enemy_kills=" << g_save.stats.enemyKills << "\n";
    out << "boss_kills=" << g_save.stats.bossKills << "\n";
    for (const HighScore& h : g_save.highScores)
      out << "highscore=" << h.score << "," << h.wave << "," << h.date << "," << h.name << "\n";
    out << "last_name=" << g_save.lastName << "\n";
    if (!out.flush()) return;
  }
  std::error_code ec;
  std::filesystem::rename(tmp, g_path, ec);
}

bool spendCoins(int amount) {
  if (amount <= 0 || g_save.coins < amount) return false;
  g_save.coins -= amount;
  store();
  return true;
}

}  // namespace

void progressInit(const std::string& saveDir) {
  std::error_code ec;
  std::filesystem::create_directories(saveDir, ec);
  g_path = saveDir + "/save.ini";
  g_save = Save{};
  load();
}

int loadCoins() { return g_save.coins; }

int addCoins(int amount) {
  if (amount <= 0) return g_save.coins;
  g_save.coins += amount;
  store();
  return g_save.coins;
}

bool isAircraftUnlocked(AircraftId id) { return g_save.aircraft[static_cast<int>(id)]; }
bool isWeaponUnlocked(WeaponId id) { return g_save.weapons[static_cast<int>(id)]; }
bool canAffordUnlock(int cost) { return g_save.coins >= cost; }

bool tryUnlockAircraft(AircraftId id) {
  const AircraftDef& craft = getAircraft(id);
  if (craft.unlockedByDefault || isAircraftUnlocked(id)) return true;
  if (!spendCoins(craft.coinCost)) return false;
  g_save.aircraft[static_cast<int>(id)] = true;
  store();
  return true;
}

bool tryUnlockWeapon(WeaponId id) {
  const WeaponDef& w = getWeapon(id);
  if (w.unlockedByDefault || isWeaponUnlocked(id)) return true;
  if (!spendCoins(w.coinCost)) return false;
  g_save.weapons[static_cast<int>(id)] = true;
  store();
  return true;
}

LifetimeStats recordEnemyKill(bool isBoss) {
  g_save.stats.enemyKills++;
  if (isBoss) g_save.stats.bossKills++;
  store();
  return g_save.stats;
}

bool unlockAchievement(AchievementId id) {
  bool& slot = g_save.achievements[static_cast<int>(id)];
  if (slot) return false;
  slot = true;
  store();
  return true;
}

int coinRewardForEnemy(EnemyType t) {
  switch (t) {
    case EnemyType::Boss: return 50;
    case EnemyType::Tank: return 10;
    case EnemyType::Splitter: return 8;
    case EnemyType::Sniper: return 9;
    case EnemyType::Shielded: return 9;
    case EnemyType::Healer: return 10;
    case EnemyType::Kamikaze: return 7;
    case EnemyType::Fast: return 4;
    case EnemyType::Mini: return 2;
    default: return 3;
  }
}

const std::vector<HighScore>& loadHighScores() { return g_save.highScores; }

bool qualifiesForHighScore(int64_t score) {
  // a tie goes below the scores already there, as stable_sort keeps it
  return g_save.highScores.size() < 10 || score > g_save.highScores.back().score;
}

const std::string& lastHighScoreName() { return g_save.lastName; }

int saveHighScore(int64_t score, int wave, const std::string& rawName) {
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_r(&now, &tm);
  char date[40];
  // en-US toLocaleDateString(): M/D/YYYY
  std::snprintf(date, sizeof date, "%d/%d/%d", tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900);
  const std::string name = cleanName(rawName);
  auto& list = g_save.highScores;
  // after every score >= this one: where stable_sort of the original put it
  auto at = std::find_if(list.begin(), list.end(), [&](const HighScore& h) { return h.score < score; });
  const int rank = static_cast<int>(at - list.begin());
  list.insert(at, {score, date, wave, name});
  if (list.size() > 10) list.resize(10);
  if (!name.empty()) g_save.lastName = name;
  store();
  return rank < 10 ? rank : -1;
}
