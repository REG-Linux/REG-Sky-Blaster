#include "net.h"

#include <SDL3/SDL.h>
#include <arpa/inet.h>
#include <enet/enet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <type_traits>

namespace net {

namespace {

constexpr uint32_t MAGIC = 0x42594B53;  // "SKYB", little endian
constexpr uint16_t VERSION = 2;  // 2: the hello carries the screen's field
constexpr size_t NAME_MAX = 24;
constexpr uint32_t PEER_TIMEOUT_MS = 6000;  // a silent peer is gone after this
enum Channel : uint8_t { Reliable, Unreliable, ChannelCount };
enum Msg : uint8_t { Hello = 1, Bye, Start, Menu, Press, Fx, Input, Snap };
enum Refusal : uint8_t { Busy = 1, OtherVersion };
enum PressBit : uint8_t { BombBit = 1, SkillBit = 2, PauseBit = 4 };

// Both sides copy these structs as bytes: a build that lays them out
// differently is refused at hello.
static_assert(std::is_trivially_copyable_v<Player> && std::is_trivially_copyable_v<Enemy> &&
              std::is_trivially_copyable_v<Hazard> && std::is_trivially_copyable_v<PowerUp>);
constexpr uint32_t layout() {
  return VERSION ^ static_cast<uint32_t>(sizeof(Player)) ^ (static_cast<uint32_t>(sizeof(Enemy)) << 8) ^
         (static_cast<uint32_t>(sizeof(Hazard)) << 16) ^ (static_cast<uint32_t>(sizeof(PowerUp)) << 24);
}

struct Writer {
  std::vector<uint8_t> b;
  template <typename T>
  void put(const T& v) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    b.insert(b.end(), p, p + sizeof v);
  }
  void u8(unsigned v) { put(static_cast<uint8_t>(v)); }
  void str(const std::string& s) {
    const size_t n = std::min(s.size(), static_cast<size_t>(255));
    u8(static_cast<unsigned>(n));
    b.insert(b.end(), s.begin(), s.begin() + static_cast<long>(n));
  }
  void color(const Color& c) {
    auto q = [](float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255 + 0.5f); };
    u8(q(c.r)), u8(q(c.g)), u8(q(c.b)), u8(q(c.a));
  }
};

struct Reader {
  const uint8_t* p;
  const uint8_t* end;
  bool ok = true;
  template <typename T>
  T get() {
    T v{};
    if (end - p < static_cast<long>(sizeof v)) {
      ok = false;
      return v;
    }
    std::memcpy(&v, p, sizeof v);
    p += sizeof v;
    return v;
  }
  uint8_t u8() { return get<uint8_t>(); }
  std::string str() {
    const size_t n = u8();
    if (!ok || static_cast<size_t>(end - p) < n) {
      ok = false;
      return {};
    }
    std::string s(reinterpret_cast<const char*>(p), n);
    p += n;
    return s;
  }
  Color color() {
    const float r = u8() / 255.0f, g = u8() / 255.0f, b = u8() / 255.0f, a = u8() / 255.0f;
    return {r, g, b, a};
  }
};

template <typename T>
void putList(Writer& w, const std::vector<T>& v) {
  w.put(static_cast<uint16_t>(std::min(v.size(), static_cast<size_t>(0xFFFF))));
  for (size_t i = 0; i < v.size() && i < 0xFFFF; i++) w.put(v[i]);
}

template <typename T>
void getList(Reader& r, std::vector<T>& v) {
  const unsigned n = r.get<uint16_t>();
  v.clear();
  for (unsigned i = 0; i < n && r.ok; i++) v.push_back(r.get<T>());
}

uint64_t nowMs() { return SDL_GetTicks(); }

template <typename E>
bool inRange(unsigned v) { return v < static_cast<unsigned>(E::Count); }

}  // namespace

Session::~Session() { shutdown(); }

bool Session::host(GameData& g, const std::string& name) {
  if (enet_initialize() != 0) return false;
  ENetAddress addr{};
  addr.host = ENET_HOST_ANY;
  addr.port = GAME_PORT;
  // two peers: the guest, and room to turn a second one away
  enet_ = enet_host_create(&addr, 2, ChannelCount, 0, 0);
  if (!enet_) {
    SDL_Log("netplay: cannot listen on UDP %u", GAME_PORT);
    enet_deinitialize();
    return false;
  }
  beaconFd_ = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  int on = 1;
  if (beaconFd_ >= 0) setsockopt(beaconFd_, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
  isHost_ = true;
  name_ = name.substr(0, NAME_MAX);
  g.netRole = NetRole::Host;
  g.netStatus = NetStatus::Waiting;
  g.gameMode = GameMode::Coop;
  g.p2Connected = false;
  SDL_Log("netplay: hosting as \"%s\" on UDP %u", name_.c_str(), GAME_PORT);
  return true;
}

bool Session::join(GameData& g, const std::string& address, const std::string& name) {
  if (enet_initialize() != 0) return false;
  enet_ = enet_host_create(nullptr, 1, ChannelCount, 0, 0);
  if (!enet_) {
    enet_deinitialize();
    return false;
  }
  isHost_ = false;
  name_ = name.substr(0, NAME_MAX);
  address_ = address;
  g.netRole = NetRole::Guest;
  g.netStatus = NetStatus::Connecting;
  g.netWhere = address;
  g.gameMode = GameMode::Coop;
  connectToHost();
  return true;
}

void Session::connectToHost() {
  ENetAddress addr{};
  if (enet_address_set_host(&addr, address_.c_str()) != 0) {
    SDL_Log("netplay: cannot resolve %s", address_.c_str());
    nextConnectMs_ = nowMs() + 3000;
    return;
  }
  addr.port = GAME_PORT;
  peer_ = enet_host_connect(enet_, &addr, ChannelCount, 0);
  if (peer_) enet_peer_timeout(peer_, 32, PEER_TIMEOUT_MS / 2, PEER_TIMEOUT_MS);
  else nextConnectMs_ = nowMs() + 3000;
}

void Session::shutdown() {
  if (!enet_) return;
  if (peer_) {
    enet_peer_disconnect_now(peer_, 0);
    peer_ = nullptr;
  }
  enet_host_flush(enet_);
  enet_host_destroy(enet_);
  enet_ = nullptr;
  enet_deinitialize();
  if (beaconFd_ >= 0) close(beaconFd_);
  beaconFd_ = -1;
}

void Session::send(const std::vector<uint8_t>& msg, bool reliable) {
  if (!peer_ || (!helloed_ && msg[0] != Hello && msg[0] != Bye)) return;
  const enet_uint32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT;
  ENetPacket* pkt = enet_packet_create(msg.data(), msg.size(), flags);
  if (enet_peer_send(peer_, reliable ? Reliable : Unreliable, pkt) != 0) enet_packet_destroy(pkt);
}

void Session::sendHello() {
  Writer w;
  w.u8(Hello);
  w.put(MAGIC);
  w.put(VERSION);
  w.put(layout());
  w.u8(static_cast<unsigned>(sentAircraft_ == AircraftId::Count ? AircraftId::Falcon : sentAircraft_));
  w.u8(static_cast<unsigned>(sentWeapon_ == WeaponId::Count ? WeaponId::Standard : sentWeapon_));
  sentW_ = ownW_, sentH_ = ownH_;
  w.put(ownW_);
  w.put(ownH_);
  w.str(name_);
  send(w.b, true);
}

void Session::service(GameData& g) {
  if (!enet_) return;
  if (!isHost_ && !peer_ && g.netStatus != NetStatus::Refused && nowMs() >= nextConnectMs_) connectToHost();
  ENetEvent ev;
  while (enet_ && enet_host_service(enet_, &ev, 0) > 0) {
    switch (ev.type) {
      case ENET_EVENT_TYPE_CONNECT: onConnect(g, ev.peer); break;
      case ENET_EVENT_TYPE_DISCONNECT: onDisconnect(g, ev.peer); break;
      case ENET_EVENT_TYPE_RECEIVE:
        if (ev.peer == peer_) onReceive(g, ev.peer, ev.packet->data, ev.packet->dataLength);
        enet_packet_destroy(ev.packet);
        break;
      default: break;
    }
  }
}

void Session::onConnect(GameData& g, ENetPeer* peer) {
  if (isHost_) {
    if (peer_ && peer_ != peer) {
      enet_peer_disconnect_later(peer, Busy);  // one guest at a time
      return;
    }
    peer_ = peer;
    lastGuestTick_ = 0;  // a new guest counts from 0
    enet_peer_timeout(peer_, 32, PEER_TIMEOUT_MS / 2, PEER_TIMEOUT_MS);
    char ip[64] = "";
    enet_address_get_host_ip(&peer->address, ip, sizeof ip);
    SDL_Log("netplay: %s connected", ip);
    return;  // its hello makes it a player
  }
  // the guest's hello carries the loadout it would fly
  sentAircraft_ = canStartGame(g) ? g.selectedAircraft : AircraftId::Falcon;
  sentWeapon_ = canStartGame(g) ? g.selectedWeapon : WeaponId::Standard;
  sendHello();
}

void Session::onDisconnect(GameData& g, ENetPeer* peer) {
  if (peer != peer_) return;  // one turned away
  const bool wasPlayer = helloed_;
  peer_ = nullptr;
  helloed_ = false;
  if (isHost_) {
    SDL_Log("netplay: player 2 left");
    lose(g, NetStatus::Waiting);
  } else {
    if (wasPlayer) SDL_Log("netplay: the host left");
    lose(g, g.netStatus == NetStatus::Refused ? NetStatus::Refused
                                               : wasPlayer ? NetStatus::Lost : NetStatus::Connecting);
    nextConnectMs_ = nowMs() + 1000;
  }
}

// The other player is gone: a run in progress ends.
void Session::lose(GameData& g, NetStatus why) {
  g.netStatus = why;
  peerW_ = peerH_ = 0;
  g.netPeerName.clear();
  g.p2Connected = false;
  log_.clear();
  if (isHost_) {
    if (g.state == GameState::Playing || g.state == GameState::Paused) {
      g.state = GameState::GameOver;
      endRunScores(g);
    }
  } else {
    fieldW_ = fieldH_ = 0;
    if (g.state != GameState::Menu) returnToMenu(g);
  }
}

void Session::onReceive(GameData& g, ENetPeer* peer, const uint8_t* data, size_t len) {
  if (len == 0) return;
  Reader r{data + 1, data + len};
  switch (data[0]) {
    case Hello: {
      const uint32_t magic = r.get<uint32_t>();
      const uint16_t version = r.get<uint16_t>();
      const uint32_t lay = r.get<uint32_t>();
      const unsigned aircraft = r.u8(), weapon = r.u8();
      const float fieldW = r.get<float>(), fieldH = r.get<float>();
      std::string name = r.str();
      if (!r.ok || magic != MAGIC) return;
      if (version != VERSION || lay != layout()) {
        SDL_Log("netplay: the other side runs another version");
        if (isHost_) {
          Writer w;
          w.u8(Bye);
          w.u8(OtherVersion);
          send(w.b, true);
          enet_peer_disconnect_later(peer, OtherVersion);
        } else {
          g.netStatus = NetStatus::Refused;
        }
        return;
      }
      for (char& c : name)
        if (static_cast<unsigned char>(c) < 32) c = ' ';
      g.netPeerName = name.empty() ? (isHost_ ? "PLAYER 2" : "HOST") : name;
      if (isHost_) {
        g.netP2Aircraft = inRange<AircraftId>(aircraft) ? static_cast<AircraftId>(aircraft) : AircraftId::Falcon;
        g.netP2Weapon = inRange<WeaponId>(weapon) ? static_cast<WeaponId>(weapon) : WeaponId::Standard;
        // its screen's field (sane, or none: the host's own then)
        const bool sane = fieldW >= BASE_W && fieldH >= BASE_H && fieldW < 10000 && fieldH < 10000;
        peerW_ = sane ? fieldW : 0, peerH_ = sane ? fieldH : 0;
        if (!helloed_) {
          helloed_ = true;
          sendHello();  // our name back
          SDL_Log("netplay: player 2 is \"%s\"", g.netPeerName.c_str());
        }
        g.p2Connected = true;
      } else {
        helloed_ = true;
        SDL_Log("netplay: joined \"%s\"", g.netPeerName.c_str());
      }
      g.netStatus = NetStatus::Connected;
      break;
    }
    case Bye:
      if (!isHost_ && r.u8() == OtherVersion) g.netStatus = NetStatus::Refused;
      break;
    case Start:
      if (!isHost_) beginGuestRun(g, data, len);
      break;
    case Menu:
      if (!isHost_) {
        fieldW_ = fieldH_ = 0;
        if (g.state != GameState::Menu) returnToMenu(g);
      }
      break;
    case Press:
      if (isHost_) {
        const unsigned bits = r.u8();
        if (bits & BombBit) g.p2Input.bomb = true;
        if (bits & SkillBit) g.p2Input.skill = true;
        if (bits & PauseBit) togglePause(g);
      }
      break;
    case Input:
      if (isHost_) {
        const uint32_t tick = r.get<uint32_t>();
        const unsigned bits = r.u8();
        if (!r.ok || tick <= lastGuestTick_) break;
        lastGuestTick_ = tick;
        InputState& in = g.p2Input;
        in.left = bits & 1, in.right = bits & 2, in.up = bits & 4, in.down = bits & 8, in.shoot = bits & 16;
      }
      break;
    case Fx:
      if (!isHost_) {
        const int run = r.get<int32_t>();
        const unsigned n = r.get<uint16_t>();
        if (!r.ok || run != g.runSerial || g.state == GameState::Menu) break;
        for (unsigned i = 0; i < n && r.ok; i++) {
          FxEvent e{static_cast<FxEvent::Kind>(r.u8())};
          e.x = r.get<float>();
          e.y = r.get<float>();
          e.color = r.color();
          switch (e.kind) {
            case FxEvent::Particles:
              e.n = r.get<uint16_t>();
              e.spd = r.get<float>();
              e.type = static_cast<ParticleType>(r.u8());
              e.sz0 = r.get<float>();
              e.sz1 = r.get<float>();
              break;
            case FxEvent::Ring: e.sz0 = r.get<float>(); break;
            case FxEvent::Popup: e.text = r.str(); break;
            case FxEvent::Sound: e.n = r.u8(); break;
            default: r.ok = false; break;
          }
          if (r.ok) replayFx(g, e);
        }
      }
      break;
    case Snap:
      if (!isHost_) applySnapshot(g, data, len);
      break;
    default: break;
  }
}

void Session::sendStart(GameData& g) {
  Writer w;
  w.u8(Start);
  w.put(static_cast<int32_t>(g.runSerial));
  w.u8(static_cast<unsigned>(g.difficulty));
  w.put(CANVAS_W);
  w.put(CANVAS_H);
  w.u8(static_cast<unsigned>(g.netP2Aircraft));
  w.u8(static_cast<unsigned>(g.netP2Weapon));
  send(w.b, true);
}

void Session::beginGuestRun(GameData& g, const uint8_t* data, size_t len) {
  Reader r{data + 1, data + len};
  const int run = r.get<int32_t>();
  const unsigned difficulty = r.u8();
  const float fw = r.get<float>(), fh = r.get<float>();
  const unsigned aircraft = r.u8(), weapon = r.u8();
  if (!r.ok || !inRange<Difficulty>(difficulty) || fw <= 0 || fh <= 0) return;
  // the host's field: positions are the host's (main letterboxes it)
  fieldW_ = fw, fieldH_ = fh;
  CANVAS_W = fw, CANVAS_H = fh;
  g.gameMode = GameMode::Coop;
  g.difficulty = static_cast<Difficulty>(difficulty);
  if (!canStartGame(g)) g.selectedAircraft = AircraftId::Falcon, g.selectedWeapon = WeaponId::Standard;
  resetGame(g);
  g.runSerial = run;
  g.player2 = createPlayer(inRange<AircraftId>(aircraft) ? static_cast<AircraftId>(aircraft) : AircraftId::Falcon,
                           inRange<WeaponId>(weapon) ? static_cast<WeaponId>(weapon) : WeaponId::Standard);
  log_.clear();
}

void Session::sendSnapshot(GameData& g) {
  Writer w;
  w.b.reserve(8192);
  w.u8(Snap);
  w.put(static_cast<int32_t>(g.runSerial));
  w.put(lastGuestTick_);
  w.u8(static_cast<unsigned>(g.state));
  w.u8(static_cast<unsigned>(g.chapterId));
  w.put(static_cast<int32_t>(g.wave));
  w.put(g.score);
  w.put(g.waveTimer);
  w.put(g.waveDelay);
  w.put(static_cast<int32_t>(g.enemiesSpawned));
  w.put(static_cast<int32_t>(g.enemiesPerWave));
  w.put(static_cast<int32_t>(g.combo));
  w.put(g.comboTimer);
  w.put(static_cast<int32_t>(g.maxCombo));
  w.put(g.slowMotion);
  w.put(g.slowMotionTimer);
  w.put(g.flashAlpha);
  w.color(g.flashColor);
  w.put(g.waveAnnounceTimer);
  w.put(g.screenShake);
  w.put(static_cast<int32_t>(g.shotsFired));
  w.put(static_cast<int32_t>(g.shotsHit));
  w.put(g.damageDealt);
  w.put(static_cast<int32_t>(g.enemiesKilled));
  w.put(static_cast<int32_t>(g.runCoinsEarned));
  w.u8(g.modeVictory);
  w.put(g.player);
  w.u8(g.player2.has_value());
  if (g.player2) w.put(*g.player2);
  putList(w, g.enemies);
  putList(w, g.hazards);
  putList(w, g.powerUps);
  // bullets: only what drawBullets reads
  const size_t nb = std::min(g.bullets.size(), static_cast<size_t>(0xFFFF));
  w.put(static_cast<uint16_t>(nb));
  for (size_t i = 0; i < nb; i++) {
    const Bullet& b = g.bullets[i];
    w.put(b.x);
    w.put(b.y);
    w.put(static_cast<uint16_t>(std::clamp(b.width * 16, 0.0f, 65535.0f)));
    w.put(static_cast<uint16_t>(std::clamp(b.height * 16, 0.0f, 65535.0f)));
    w.color(b.color);
    w.u8(b.isPlayer);
  }
  send(w.b, false);
}

void Session::applySnapshot(GameData& g, const uint8_t* data, size_t len) {
  Reader r{data + 1, data + len};
  const int run = r.get<int32_t>();
  const uint32_t acked = r.get<uint32_t>();
  if (!r.ok || run != g.runSerial || g.state == GameState::Menu) return;  // an old run's
  const unsigned state = r.u8(), chapter = r.u8();
  if (state > static_cast<unsigned>(GameState::GameOver) || !inRange<ChapterId>(chapter)) return;
  GameData s;  // read whole before touching g: a short packet changes nothing
  s.wave = r.get<int32_t>();
  s.score = r.get<int64_t>();
  s.waveTimer = r.get<float>();
  s.waveDelay = r.get<float>();
  s.enemiesSpawned = r.get<int32_t>();
  s.enemiesPerWave = r.get<int32_t>();
  s.combo = r.get<int32_t>();
  s.comboTimer = r.get<float>();
  s.maxCombo = r.get<int32_t>();
  s.slowMotion = r.get<float>();
  s.slowMotionTimer = r.get<float>();
  s.flashAlpha = r.get<float>();
  s.flashColor = r.color();
  s.waveAnnounceTimer = r.get<float>();
  s.screenShake = r.get<ScreenShake>();
  s.shotsFired = r.get<int32_t>();
  s.shotsHit = r.get<int32_t>();
  s.damageDealt = r.get<float>();
  s.enemiesKilled = r.get<int32_t>();
  s.runCoinsEarned = r.get<int32_t>();
  s.modeVictory = r.u8();
  const Player p1 = r.get<Player>();
  std::optional<Player> p2;
  if (r.u8()) p2 = r.get<Player>();
  getList(r, s.enemies);
  getList(r, s.hazards);
  getList(r, s.powerUps);
  const unsigned nb = r.get<uint16_t>();
  for (unsigned i = 0; i < nb && r.ok; i++) {
    Bullet b{};
    b.x = r.get<float>();
    b.y = r.get<float>();
    b.width = r.get<uint16_t>() / 16.0f;
    b.height = r.get<uint16_t>() / 16.0f;
    b.color = r.color();
    b.isPlayer = r.u8();
    s.bullets.push_back(b);
  }
  if (!r.ok) return;

  const auto newState = static_cast<GameState>(state);
  if (newState == GameState::GameOver && g.state != GameState::GameOver) SDL_Log("netplay: run over");
  g.state = newState;
  if (static_cast<ChapterId>(chapter) != g.chapterId) applyChapterToGame(g, static_cast<ChapterId>(chapter));
  g.wave = s.wave, g.score = s.score, g.waveTimer = s.waveTimer, g.waveDelay = s.waveDelay;
  g.enemiesSpawned = s.enemiesSpawned, g.enemiesPerWave = s.enemiesPerWave;
  g.combo = s.combo, g.comboTimer = s.comboTimer, g.maxCombo = s.maxCombo;
  g.slowMotion = s.slowMotion, g.slowMotionTimer = s.slowMotionTimer;
  g.flashAlpha = s.flashAlpha, g.flashColor = s.flashColor;
  g.waveAnnounceTimer = s.waveAnnounceTimer, g.screenShake = s.screenShake;
  g.shotsFired = s.shotsFired, g.shotsHit = s.shotsHit, g.damageDealt = s.damageDealt;
  g.enemiesKilled = s.enemiesKilled, g.runCoinsEarned = s.runCoinsEarned, g.modeVictory = s.modeVictory;
  g.enemies = std::move(s.enemies);
  g.hazards = std::move(s.hazards);
  g.powerUps = std::move(s.powerUps);
  g.bullets = std::move(s.bullets);
  g.player = p1;
  // our ship: the host's, plus the inputs it has not applied yet
  std::erase_if(log_, [acked](const LoggedInput& l) { return l.tick <= acked; });
  if (p2) {
    Player mine = *p2;
    if (g.state == GameState::Playing)
      for (const LoggedInput& l : log_) moveShip(mine, l.in);
    g.player2 = mine;
  } else {
    g.player2.reset();
  }
}

void Session::guestTick(GameData& g, InputState& in) {
  const unsigned press = (in.bomb ? BombBit : 0) | (in.skill ? SkillBit : 0) | (in.pause ? PauseBit : 0);
  in.bomb = in.skill = in.pause = false;
  if (!helloed_) return;
  if (press) {
    Writer w;
    w.u8(Press);
    w.u8(press);
    send(w.b, true);
  }
  if (g.state != GameState::Playing) return;
  tick_++;
  Writer w;
  w.u8(Input);
  w.put(tick_);
  const bool shoot = in.shoot || g.autoFire;  // our own auto-fire
  w.u8((in.left ? 1u : 0u) | (in.right ? 2u : 0u) | (in.up ? 4u : 0u) | (in.down ? 8u : 0u) | (shoot ? 16u : 0u));
  send(w.b, false);
  log_.push_back({tick_, in});
  if (log_.size() > 240) log_.erase(log_.begin());  // 4 s unanswered: the host is gone anyway
}

void Session::sync(GameData& g) {
  if (!enet_) return;
  if (isHost_) {
    beacon(g);
    if (helloed_) {
      if (g.state == GameState::Playing && g.runSerial != sentRun_) {
        sentRun_ = g.runSerial;
        sendStart(g);
      }
      if (g.state == GameState::Menu && sentState_ != GameState::Menu) {
        Writer w;
        w.u8(Menu);
        send(w.b, true);
      }
      if (g.state != GameState::Menu) {
        if (!fx_.empty()) {
          Writer w;
          w.u8(Fx);
          w.put(static_cast<int32_t>(g.runSerial));
          const size_t n = std::min(fx_.size(), static_cast<size_t>(0xFFFF));
          w.put(static_cast<uint16_t>(n));
          for (size_t i = 0; i < n; i++) {
            const FxEvent& e = fx_[i];
            w.u8(e.kind);
            w.put(e.x);
            w.put(e.y);
            w.color(e.color);
            switch (e.kind) {
              case FxEvent::Particles:
                w.put(static_cast<uint16_t>(std::clamp(e.n, 0, 0xFFFF)));
                w.put(e.spd);
                w.u8(static_cast<unsigned>(e.type));
                w.put(e.sz0);
                w.put(e.sz1);
                break;
              case FxEvent::Ring: w.put(e.sz0); break;
              case FxEvent::Popup: w.str(e.text); break;
              case FxEvent::Sound: w.u8(static_cast<unsigned>(e.n)); break;
            }
          }
          send(w.b, true);
        }
        sendSnapshot(g);
      }
    }
    sentState_ = g.state;
    fx_.clear();
  } else if (helloed_ && g.state == GameState::Menu) {
    // the loadout we would fly, as the menu changes it
    const AircraftId a = canStartGame(g) ? g.selectedAircraft : AircraftId::Falcon;
    const WeaponId w = canStartGame(g) ? g.selectedWeapon : WeaponId::Standard;
    if (a != sentAircraft_ || w != sentWeapon_ || ownW_ != sentW_ || ownH_ != sentH_) {
      sentAircraft_ = a, sentWeapon_ = w;
      sendHello();
    }
  }
  enet_host_flush(enet_);
}

bool Session::commonField(float& w, float& h) const {
  if (!isHost_ || !helloed_ || peerW_ <= 0 || ownW_ <= 0) return false;
  w = std::min(ownW_, peerW_);
  h = std::min(ownH_, peerH_);
  return true;
}

// Once a second, to every IPv4 broadcast address (and the limited one):
// "SKYB", version, game port, name[32], players, max players, status
// (0 waiting for a player, 1 full), all little endian.
void Session::beacon(const GameData& g) {
  const uint64_t now = nowMs();
  if (beaconFd_ < 0 || now < nextBeaconMs_) return;
  nextBeaconMs_ = now + 1000;
  Writer w;
  w.put(MAGIC);
  w.put(VERSION);
  w.put(GAME_PORT);
  char name[32] = {};
  std::memcpy(name, name_.data(), std::min(name_.size(), sizeof name - 1));
  w.put(name);
  w.u8(helloed_ ? 2 : 1);
  w.u8(2);
  w.u8(helloed_ ? 1 : 0);
  w.u8(0);
  std::vector<in_addr_t> targets = {htonl(INADDR_BROADCAST)};
  ifaddrs* ifs = nullptr;
  if (getifaddrs(&ifs) == 0) {
    for (ifaddrs* i = ifs; i; i = i->ifa_next) {
      if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_BROADCAST) ||
          (i->ifa_flags & IFF_LOOPBACK) || !i->ifa_broadaddr)
        continue;
      const in_addr_t b = reinterpret_cast<sockaddr_in*>(i->ifa_broadaddr)->sin_addr.s_addr;
      if (std::find(targets.begin(), targets.end(), b) == targets.end()) targets.push_back(b);
    }
    freeifaddrs(ifs);
  }
  for (in_addr_t t : targets) {
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(BEACON_PORT);
    to.sin_addr.s_addr = t;
    sendto(beaconFd_, w.b.data(), w.b.size(), 0, reinterpret_cast<sockaddr*>(&to), sizeof to);
  }
  (void)g;
}

}  // namespace net
