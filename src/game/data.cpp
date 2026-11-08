// Static tables: aircraft.ts, weapons.ts, chapters.ts, modes.ts,
// progress.ts (achievements), enemies/boss.ts, coop.ts (palettes).
#include <random>

#include "game.h"

namespace {

const AircraftDef AIRCRAFT[] = {
    {AircraftId::Falcon, "Falcon", "Balanced all-rounder", 6, 3, 5, AircraftSkill::MissileSalvo,
     "Missile Salvo", "Launch homing missiles at nearby enemies", 8, true, 0, C("#44ddff"),
     C("#2288cc"), C("#115577"), C("#88eeff"), C("#0088ff"), C("#0ff")},
    {AircraftId::Phantom, "Phantom", "Fast and fragile", 8, 2, 4, AircraftSkill::Dash, "Dash",
     "Blink forward with brief invincibility", 4, false, 500, C("#cc88ff"), C("#8844cc"),
     C("#442266"), C("#ddaaff"), C("#aa44ff"), C("#c8f")},
    {AircraftId::Fortress, "Fortress", "Heavy and slow", 4, 5, 6, AircraftSkill::EnergyShield,
     "Energy Shield", "Absorb hits and empower your next attack", 10, false, 800, C("#ffcc44"),
     C("#cc8822"), C("#664411"), C("#ffdd88"), C("#ff8800"), C("#fc4")},
};

const WeaponDef WEAPONS[] = {
    {WeaponId::Standard, "Standard Blaster", "STD", "Reliable spread-fire blaster", C("#0ff"),
     C("#0ff"), true, 0},
    {WeaponId::ArmorPiercing, "Armor Piercing", "AP", "Rounds pierce through enemies", C("#8f8"),
     C("#afa"), false, 300},
    {WeaponId::Shotgun, "Shotgun", "SG", "Wide burst at close range", C("#fa4"), C("#fc8"), false,
     400},
    {WeaponId::Laser, "Laser", "LSR", "Continuous beam ramps damage", C("#f4f"), C("#f8f"), false,
     600},
    {WeaponId::Homing, "Homing", "HMG", "Slow missiles track targets", C("#f88"), C("#f88"), false,
     600},
};

const ChapterDef CHAPTERS[] = {
    {ChapterId::Space, "Deep Space", "Standard patrol zone", C("#060614"), C("#0a0e22"),
     C("#060614"), {C("#1a0030"), C("#001830"), C("#0a1828"), C("#180a28"), C("#002018")},
     C("#ccddff"), C("#ffffff"), HazardType::None, C("#8cf")},
    {ChapterId::Asteroid, "Asteroid Belt", "Dodge falling debris", C("#100c08"), C("#1a140c"),
     C("#0c0806"), {C("#281808"), C("#201410"), C("#301c0c"), C("#181008"), C("#24180a")},
     C("#ddbbaa"), C("#aa9988"), HazardType::Asteroid, C("#da8")},
    {ChapterId::Carrier, "Enemy Carrier", "Beware fixed turrets", C("#080a10"), C("#10141c"),
     C("#06080c"), {C("#180808"), C("#101018"), C("#201010"), C("#0c1018"), C("#180c0c")},
     C("#ffaa99"), C("#cc8877"), HazardType::Turret, C("#f88")},
    {ChapterId::Wormhole, "Wormhole", "Unstable teleport pads", C("#100818"), C("#1a0830"),
     C("#080410"), {C("#300840"), C("#200830"), C("#401060"), C("#180828"), C("#280450")},
     C("#ddaaff"), C("#bb88ff"), HazardType::Teleporter, C("#c8f")},
};

const ModeInfo MODES[] = {
    {"Story Mode", "4 chapters \xC2\xB7 20 stages"},
    {"Endless", "Survive infinite waves"},
    {"Boss Rush", "Back-to-back boss fights"},
    {"Daily Challenge", "Seeded run with a twist"},
    {"Practice", "Invincible sandbox \xC2\xB7 no rewards"},
    {"Co-op Endless", "2P \xC2\xB7 two controllers \xC2\xB7 team wipe"},
};

const DifficultyInfo DIFFICULTIES[] = {
    {"Easy", "Slower enemies, +1 HP", C("#4f8")},
    {"Normal", "Balanced challenge", C("#8df")},
    {"Hard", "Faster, tankier, relentless", C("#f88")},
};

const AchievementDef ACHIEVEMENTS[] = {
    {AchievementId::FirstBlood, "First Blood", "Destroy your first enemy"},
    {AchievementId::ComboMaster, "Combo Master", "Reach a 20\xC3\x97 combo"},
    {AchievementId::GrazeKing, "Graze King", "Graze 50 bullets in one run"},
    {AchievementId::BossSlayer, "Boss Slayer", "Defeat 10 bosses (lifetime)"},
    {AchievementId::Survivor, "Survivor", "Reach wave 10"},
    {AchievementId::Untouchable, "Untouchable", "Clear a stage without taking damage"},
};

template <typename E>
E cycle(E v, int dir) {
  const int n = static_cast<int>(E::Count);
  return static_cast<E>((static_cast<int>(v) + dir + n) % n);
}

}  // namespace

const AircraftDef& getAircraft(AircraftId id) { return AIRCRAFT[static_cast<int>(id)]; }
AircraftId nextAircraft(AircraftId id, int dir) { return cycle(id, dir); }
const WeaponDef& getWeapon(WeaponId id) { return WEAPONS[static_cast<int>(id)]; }
WeaponId nextWeapon(WeaponId id, int dir) { return cycle(id, dir); }
const ChapterDef& getChapter(ChapterId id) { return CHAPTERS[static_cast<int>(id)]; }
const ModeInfo& modeInfo(GameMode m) { return MODES[static_cast<int>(m)]; }
const DifficultyInfo& difficultyInfo(Difficulty d) { return DIFFICULTIES[static_cast<int>(d)]; }
const AchievementDef& achievementDef(AchievementId id) { return ACHIEVEMENTS[static_cast<int>(id)]; }
GameMode nextGameMode(GameMode m, int dir) { return cycle(m, dir); }
Difficulty nextDifficulty(Difficulty d, int dir) { return cycle(d, dir); }

ChapterId getChapterForWave(int wave) {
  int idx = ((wave - 1 > 0 ? wave - 1 : 0) / 5) % static_cast<int>(ChapterId::Count);
  return static_cast<ChapterId>(idx);
}

BossPattern bossPatternForChapter(ChapterId id) {
  switch (id) {
    case ChapterId::Asteroid: return BossPattern::Rain;
    case ChapterId::Carrier: return BossPattern::Broadside;
    case ChapterId::Wormhole: return BossPattern::Ring;
    default: return BossPattern::Fan;
  }
}

BossAccent bossAccentColors(BossPattern p) {
  switch (p) {
    case BossPattern::Rain: return {C("#cc8822"), C("#664411"), C("#fa4"), C("#ff8800")};
    case BossPattern::Broadside: return {C("#4488cc"), C("#224466"), C("#8cf"), C("#44aaff")};
    case BossPattern::Ring: return {C("#9944cc"), C("#442266"), C("#c8f"), C("#aa44ff")};
    default: return {C("#cc2244"), C("#611"), C("#f46"), C("#ff2244")};
  }
}

float bossShootInterval(BossPattern p) {
  switch (p) {
    case BossPattern::Rain: return 28;
    case BossPattern::Broadside: return 30;
    case BossPattern::Ring: return 40;
    default: return 25;
  }
}

// P1 = cyan team, P2 = amber team: distinct even with the same craft.
ShipPalette resolveShipPalette(AircraftId id, int seat) {
  if (seat == 1)
    return {C("#4de8ff"), C("#1a8fd4"), C("#0a4a7a"), C("#b8f6ff"), C("#00aaff"), C("#44ddff")};
  if (seat == 2)
    return {C("#ffb347"), C("#e07020"), C("#8a3a08"), C("#ffe0a8"), C("#ff8800"), C("#ffaa44")};
  const AircraftDef& c = getAircraft(id);
  return {c.hullTop, c.hullMid, c.hullBottom, c.cockpitColor, c.engineColor, c.hullTop};
}

float frand() {
  static std::mt19937 gen{std::random_device{}()};
  static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  float v = dist(gen);
  return v >= 1.0f ? 0.0f : v;
}
